// Per-query search driver: one parallel loop over the database, then top-K and traceback.
//   SIMD kernel:   loop iterations are batches of 16 sequences (iteration b = sorted sequences 16b..16b+15)
//   scalar kernel: loop iterations are single sequences
// Each thread owns its work buffers, so threads never write shared state except their own score slots.
#include "hysw.h"
#include <omp.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const Db *db; const unsigned char *q; int ql; const Matrix *m; Gaps g;
    int kernel; int *scores;
    SimdWork sw[HYSW_MAXCPU];
    int *swork[HYSW_MAXCPU];            // scalar kernel work (2*ql ints)
    unsigned char *sbuf[HYSW_MAXCPU];   // one database sequence, contiguous
    long items[HYSW_MAXCPU], rescored[HYSW_MAXCPU];
    double busy[HYSW_MAXCPU];
    Interval *tr[HYSW_MAXCPU]; int ntr[HYSW_MAXCPU], cap[HYSW_MAXCPU];
    int trace, T;
    long done, total, step, next;       // progress (iterations)
    FILE *progress;
    double t0;
} Ctx;

static void report(Ctx *c, long done) {
    fprintf(c->progress, "{\"type\":\"progress\",\"done\":%ld,\"total\":%ld,\"elapsed\":%.4f,\"thread_items\":[",
            done, c->total, omp_get_wtime() - c->t0);
    for (int t = 0; t < c->T; t++) fprintf(c->progress, "%s%ld", t ? "," : "", __atomic_load_n(&c->items[t], __ATOMIC_RELAXED));
    fprintf(c->progress, "]}\n");
    fflush(c->progress);
}

static void after(Ctx *c, int tid, long n, double ta, double tb) {
    __atomic_add_fetch(&c->items[tid], n, __ATOMIC_RELAXED);
    c->busy[tid] += tb - ta;
    if (c->trace) {
        if (c->ntr[tid] == c->cap[tid]) { c->cap[tid] = c->cap[tid] ? 2 * c->cap[tid] : 1024; c->tr[tid] = realloc(c->tr[tid], sizeof(Interval) * c->cap[tid]); }
        c->tr[tid][c->ntr[tid]++] = (Interval){ta - c->t0, tb - c->t0};
    }
    if (c->progress) {
        long d = __atomic_add_fetch(&c->done, n, __ATOMIC_RELAXED);
        long nx = __atomic_load_n(&c->next, __ATOMIC_RELAXED);
        if (d >= nx && __atomic_compare_exchange_n(&c->next, &nx, nx + c->step, 0, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
#pragma omp critical(hysw_progress)
            report(c, d);
        }
    }
}

static void body_simd(long lo, long hi, int tid, void *vc) {
    Ctx *c = vc;
    double ta = omp_get_wtime();
    int out[HYSW_LANES];
    for (long b = lo; b < hi; b++) {
        unsigned risky = sw_simd_batch(c->db, (uint64_t)b, c->q, c->ql, c->m, c->g, &c->sw[tid], out);
        for (int l = 0; l < HYSW_LANES; l++) {
            uint64_t k = (uint64_t)b * HYSW_LANES + l;
            if (k >= c->db->nseq) break;
            if (risky >> l & 1) {                                 // 16-bit overflow risk: exact 32-bit re-score
                db_get_seq(c->db, k, c->sbuf[tid]);
                out[l] = sw_scalar(c->q, c->ql, c->sbuf[tid], (int)c->db->len[k], c->m, c->g, c->swork[tid]);
                c->rescored[tid]++;
            }
            c->scores[k] = out[l];
        }
    }
    after(c, tid, hi - lo, ta, omp_get_wtime());
}

static void body_scalar(long lo, long hi, int tid, void *vc) {
    Ctx *c = vc;
    double ta = omp_get_wtime();
    for (long k = lo; k < hi; k++) {
        db_get_seq(c->db, (uint64_t)k, c->sbuf[tid]);
        c->scores[k] = sw_scalar(c->q, c->ql, c->sbuf[tid], (int)c->db->len[k], c->m, c->g, c->swork[tid]);
    }
    after(c, tid, hi - lo, ta, omp_get_wtime());
}

int search_scores(const Db *db, const unsigned char *q, int ql, const Matrix *m, Gaps g,
                  const SearchOpts *o, LoopSite *site, int *scores, SearchStats *st) {
    Ctx *c = calloc(1, sizeof *c);
    if (!c) return -1;
    c->db = db; c->q = q; c->ql = ql; c->m = m; c->g = g; c->kernel = o->kernel; c->scores = scores;
    c->T = o->T; c->trace = o->trace; c->progress = o->progress;
    for (int t = 0; t < o->T; t++) {
        c->swork[t] = malloc(sizeof(int) * 2 * (size_t)ql);
        c->sbuf[t] = malloc(db->maxlen + 1);
        if (o->kernel == KERNEL_SIMD) {
            if (simd_work_init(&c->sw[t], ql)) return -1;
            simd_work_profile(&c->sw[t], q, ql, o->profile_query);
            c->sw[t].untiled = o->simd_untiled;
        }
    }
    long n = o->kernel == KERNEL_SIMD ? (long)db->nbatch : (long)db->nseq;
    float *cost = malloc(sizeof(float) * (n ? n : 1));            // cost hint: residues in the iteration
    for (long i = 0; i < n; i++) cost[i] = o->kernel == KERNEL_SIMD ? (float)db->blen[i] : (float)db->len[i];
    c->total = n; c->step = n / 100 > 0 ? n / 100 : 1; c->next = c->step;
    c->t0 = omp_get_wtime();
    map_for(o->policy, site, n, o->kernel == KERNEL_SIMD ? body_simd : body_scalar, c, cost);
    double t1 = omp_get_wtime();
    if (c->progress) report(c, n);
    memset(st, 0, sizeof *st);
    st->seconds = t1 - c->t0;
    st->cells = (double)ql * (double)db->nres;
    st->padded_cells = (double)ql * (double)(o->kernel == KERNEL_SIMD ? db->cells : db->nres);
    for (int t = 0; t < o->T; t++) {
        st->rescored += c->rescored[t];
        st->thread_items[t] = c->items[t];
        st->thread_busy[t] = c->busy[t];
        st->trace[t] = c->tr[t]; st->ntrace[t] = c->ntr[t];
        free(c->swork[t]); free(c->sbuf[t]);
        if (o->kernel == KERNEL_SIMD) simd_work_free(&c->sw[t]);
    }
    free(cost); free(c);
    return 0;
}

void search_stats_free(SearchStats *st) {
    for (int t = 0; t < HYSW_MAXCPU; t++) { free(st->trace[t]); st->trace[t] = NULL; st->ntrace[t] = 0; }
}

static int better(const Db *db, uint64_t a, int sa, uint64_t b, int sb) {   // a ranks above b?
    if (sa != sb) return sa > sb;
    return db->orig[a] < db->orig[b];
}

int search_top(const Db *db, const unsigned char *q, int ql, const Matrix *m, Gaps g,
               const int *scores, int top_k, int align_k, int T, Hit *hits) {
    int n = 0;
    if (top_k > (int)db->nseq) top_k = (int)db->nseq;
    for (uint64_t k = 0; k < db->nseq; k++) {                 // insertion into a short sorted list
        if (n == top_k && !better(db, k, scores[k], hits[n - 1].k, hits[n - 1].score)) continue;
        int p = n < top_k ? n++ : n - 1;
        while (p > 0 && better(db, k, scores[k], hits[p - 1].k, hits[p - 1].score)) { hits[p] = hits[p - 1]; p--; }
        hits[p].k = k; hits[p].score = scores[k]; hits[p].has_aln = 0;
    }
    if (align_k > n) align_k = n;
    int bad = 0;
#pragma omp parallel for schedule(dynamic, 1) num_threads(T > 0 ? T : 1) reduction(+ : bad)
    for (int h = 0; h < align_k; h++) {
        unsigned char *d = malloc(db->len[hits[h].k] + 1);
        db_get_seq(db, hits[h].k, d);
        if (sw_align(q, ql, d, (int)db->len[hits[h].k], m, g, &hits[h].aln) == 0 && hits[h].aln.score == hits[h].score)
            hits[h].has_aln = 1;
        else bad++;
        free(d);
    }
    return bad ? -bad : n;
}
