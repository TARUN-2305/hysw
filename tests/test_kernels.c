// Kernel-level correctness tests (blueprint §6, items 2–5). Built by build.sh as bin/test_kernels.
//   1. SIMD batch kernel == scalar kernel on random databases with very unequal lengths, partial
//      batches, empty and one-residue sequences, and all residue codes (B, Z, X, *)
//   2. 16-bit overflow path: sequences whose scores pass 32,767 still get the exact 32-bit score
//   3. every mapping policy and thread set gives output identical to the sequential run
//   4. traceback: alignment score == kernel score == score recomputed from the aligned strings,
//      and the aligned strings spell the reported sub-sequences
//   6. SIMD trace (Inside SIMD view): every recorded cell equals the textbook Gotoh H matrix of that lane
//   5. calibration: decision rule on fixed numbers; a strided database slice scores exactly like the
//      same sequences in the full database; a real calibration run returns a consistent choice
// Usage: bin/test_kernels [matrix]   (default data/BLOSUM62). Exit status 0 = all passed.
#include "hysw.h"
#include <omp.h>
#include <stdlib.h>
#include <string.h>

static unsigned long long rng = 88172645463325252ULL;
static unsigned rnd(unsigned n) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return (unsigned)(rng % n); }
static unsigned char rres(void) { unsigned r = rnd(100); return r < 96 ? (unsigned char)rnd(20) : (unsigned char)(20 + rnd(4)); }

static int failures, checks;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; if (failures < 20) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); } } } while (0)

static Matrix M;
static const Gaps G = {12, 1};

static void set_add(SeqSet *s, const unsigned char *seq, int len) {
    s->seq = realloc(s->seq, sizeof(*s->seq) * (s->n + 1));
    s->hdr = realloc(s->hdr, sizeof(*s->hdr) * (s->n + 1));
    s->len = realloc(s->len, sizeof(int) * (s->n + 1));
    s->nunknown = realloc(s->nunknown, sizeof(int) * (s->n + 1));
    s->seq[s->n] = malloc(len + 1);
    if (len) memcpy(s->seq[s->n], seq, len);
    char h[64]; snprintf(h, sizeof h, "sp|T%05d|TEST_%d synthetic", s->n, s->n);
    s->hdr[s->n] = strdup(h); s->len[s->n] = len; s->nunknown[s->n] = 0;
    s->n++; s->nres += len;
}

static unsigned char *mutate(const unsigned char *src, int n, int *outlen, int pct) {   // substitutions + indels
    unsigned char *d = malloc(2 * n + 16); int m = 0;
    for (int i = 0; i < n; i++) {
        unsigned r = rnd(100);
        if (r < (unsigned)pct / 4) continue;                                   // deletion
        if (r < (unsigned)pct / 2) { d[m++] = rres(); d[m++] = src[i]; continue; } // insertion
        d[m++] = r < (unsigned)pct ? rres() : src[i];                          // substitution
    }
    *outlen = m;
    return d;
}

// Expected scores via the scalar kernel, in sorted order.
static void expected(const Db *db, const unsigned char *q, int ql, int *out) {
    int *work = malloc(sizeof(int) * 2 * ql);
    unsigned char *buf = malloc(db->maxlen + 1);
    for (uint64_t k = 0; k < db->nseq; k++) { db_get_seq(db, k, buf); out[k] = sw_scalar(q, ql, buf, (int)db->len[k], &M, G, work); }
    free(work); free(buf);
}

static int g_profile_query;     // 1: SIMD profile rows only for the query's residues
static int g_untiled;           // 1: original untiled SIMD loop order
static int run(const Db *db, const unsigned char *q, int ql, int kernel, int policy, const int *cpus, int T, int *out, SearchStats *st, LoopSite *site) {
    SearchOpts o; memset(&o, 0, sizeof o);
    o.kernel = kernel; o.policy = policy; o.T = T; o.profile_query = g_profile_query; o.simd_untiled = g_untiled;
    memcpy(o.cpus, cpus, sizeof(int) * T);
    return search_scores(db, q, ql, &M, G, &o, site, out, st);
}

static void test_simd_vs_scalar(void) {
    printf("[1] SIMD batch kernel vs scalar kernel (random databases)\n");
    int one[1] = {0};
    for (int trial = 0; trial < 6; trial++) {
        SeqSet s; memset(&s, 0, sizeof s);
        int nseq = 1 + (int)rnd(trial < 3 ? 40 : 500);                    // includes counts not divisible by 16
        unsigned char tmp[3000];
        for (int k = 0; k < nseq; k++) {
            unsigned r = rnd(10);
            int len = r == 0 ? 0 : r == 1 ? 1 : r < 4 ? (int)rnd(30) : r < 9 ? (int)rnd(600) : 1000 + (int)rnd(1900);
            for (int j = 0; j < len; j++) tmp[j] = rres();
            set_add(&s, tmp, len);
        }
        if (trial == 5) { set_add(&s, s.seq[0], s.len[0]); set_add(&s, s.seq[1], s.len[1]); }   // duplicates
        Db db; char err[256];
        if (db_build(&s, &db, err, sizeof err)) { CHECK(0, "db_build: %s", err); seqset_free(&s); continue; }
        for (int qn = 0; qn < 5; qn++) {
            int ql = qn == 0 ? 1 : qn == 1 ? 2 + (int)rnd(20) : 20 + (int)rnd(qn == 4 ? 2500 : 700);
            unsigned char *q = malloc(ql);
            if (qn == 3 && s.len[0] > 0) { int ml; unsigned char *mq = mutate(s.seq[0], s.len[0], &ml, 30); ql = ml > 0 ? ml : 1; q = realloc(q, ql); memcpy(q, mq, ql); free(mq); }
            else for (int i = 0; i < ql; i++) q[i] = rres();
            int *exp = malloc(sizeof(int) * db.nseq), *got = malloc(sizeof(int) * db.nseq);
            expected(&db, q, ql, exp);
            for (int v = 0; v < 4; v++) {                     // both profile modes x both loop orders
                g_profile_query = v & 1; g_untiled = v >> 1;
                SearchStats st; LoopSite site; map_site_init(&site, 1, one, 0);
                run(&db, q, ql, KERNEL_SIMD, MAP_STATIC, one, 1, got, &st, &site);
                int bad = 0;
                for (uint64_t k = 0; k < db.nseq; k++) bad += got[k] != exp[k];
                CHECK(bad == 0, "trial %d query %d (length %d, profile %s, %s): %d of %lu SIMD scores differ", trial, qn, ql,
                      g_profile_query ? "query" : "full", g_untiled ? "untiled" : "tiled", bad, (unsigned long)db.nseq);
                search_stats_free(&st); map_site_free(&site);
            }
            g_profile_query = 0; g_untiled = 0;
            free(exp); free(got); free(q);
        }
        printf("  database %d: %d sequences, %lu batches, longest %u -> ok so far: %s\n", trial, nseq, (unsigned long)db.nbatch, db.maxlen, failures ? "NO" : "yes");
        db_close(&db); seqset_free(&s);
    }
}

static void test_overflow(void) {
    printf("[2] 16-bit overflow path\n");
    // W/W scores 11 in BLOSUM62: a run of n W's aligned to itself scores 11n. 32,767 / 11 = 2978.8
    const int W = 17;
    int lens[] = {2975, 2977, 2978, 2979, 3200, 3600, 50, 400};      // around and beyond the 16-bit limit
    SeqSet s; memset(&s, 0, sizeof s);
    unsigned char *buf = malloc(4000);
    for (size_t t = 0; t < sizeof lens / sizeof *lens; t++) {
        for (int j = 0; j < lens[t]; j++) buf[j] = W;
        if (t == 7) for (int j = 0; j < lens[t]; j += 7) buf[j] = rres();
        set_add(&s, buf, lens[t]);
    }
    for (int k = 0; k < 21; k++) { int len = 100 + (int)rnd(3000); for (int j = 0; j < len; j++) buf[j] = rres(); set_add(&s, buf, len); }
    // a mixed high-scoring sequence: mutated copy of a long W run with other residues sprinkled in
    for (int j = 0; j < 3500; j++) buf[j] = (j % 11 == 0) ? 4 /*C*/ : W;
    set_add(&s, buf, 3500);
    Db db; char err[256];
    if (db_build(&s, &db, err, sizeof err)) { CHECK(0, "db_build: %s", err); return; }
    int ql = 3400;
    unsigned char *q = malloc(ql);
    for (int i = 0; i < ql; i++) q[i] = W;                         // pure W run: W-runs score 11 x min(length, 3400)
    int *exp = malloc(sizeof(int) * db.nseq), *got = malloc(sizeof(int) * db.nseq);
    expected(&db, q, ql, exp);
    int one[1] = {0}, maxs = 0, above = 0;
    SearchStats st; LoopSite site; map_site_init(&site, 1, one, 0);
    g_untiled = 1;                                                 // original loop order first
    run(&db, q, ql, KERNEL_SIMD, MAP_STATIC, one, 1, got, &st, &site);
    int bad_untiled = 0;
    for (uint64_t k = 0; k < db.nseq; k++) bad_untiled += got[k] != exp[k];
    CHECK(bad_untiled == 0, "%d scores differ after the overflow path (untiled)", bad_untiled);
    search_stats_free(&st); map_site_free(&site); map_site_init(&site, 1, one, 0);
    g_untiled = 0;                                                 // default tiled loop order
    run(&db, q, ql, KERNEL_SIMD, MAP_STATIC, one, 1, got, &st, &site);
    int bad = 0;
    for (uint64_t k = 0; k < db.nseq; k++) { bad += got[k] != exp[k]; if (exp[k] > maxs) maxs = exp[k]; above += exp[k] > 32767; }
    CHECK(bad == 0, "%d scores differ after the overflow path (tiled)", bad);
    CHECK(above >= 3, "expected several scores above 32767, got %d", above);
    CHECK(st.rescored >= above, "rescored %ld < sequences above the limit %d", st.rescored, above);
    printf("  %lu sequences, %d with exact score > 32767 (max %d), %ld re-scored in 32-bit, mismatches %d\n",
           (unsigned long)db.nseq, above, maxs, st.rescored, bad);
    search_stats_free(&st); map_site_free(&site);
    free(exp); free(got); free(q); free(buf);
    db_close(&db); seqset_free(&s);
}

static void test_policies(const char *fasta) {
    printf("[3] mapping policies x thread sets x kernels vs sequential\n");
    int f0 = failures;
    Db db; char err[256];
    if (db_open(fasta, &db, err, sizeof err)) { CHECK(0, "%s", err); return; }
    Topology t; topology_read(&t);
    const char *sets[] = {"1", "p1", "p", "e", "physical", "all"};
    int ql = 300; unsigned char *q = malloc(ql);
    for (int i = 0; i < ql; i++) q[i] = rres();
    int *ref = malloc(sizeof(int) * db.nseq), *got = malloc(sizeof(int) * db.nseq);
    expected(&db, q, ql, ref);
    int combos = 0;
    for (size_t si = 0; si < sizeof sets / sizeof *sets; si++) {
        int cpus[HYSW_MAXCPU];
        int T = topology_set(&t, sets[si], cpus, HYSW_MAXCPU, err, sizeof err);
        if (T <= 0) { printf("  (thread set %s not available here)\n", sets[si]); continue; }
        for (int kern = 0; kern < 2; kern++)
            for (int p = 0; p < MAP_NPOLICY; p++) {
                if (kern == KERNEL_SCALAR && si != 5 && si != 0) continue;   // scalar is slow: only 1 and all threads
                LoopSite site; map_site_init(&site, T, cpus, 1);
                for (int rep = 0; rep < (p == MAP_COSTCAP_STEAL ? 3 : 1); rep++) {   // costcap learns across runs
                    memset(got, 0xff, sizeof(int) * db.nseq);
                    SearchStats st;
                    run(&db, q, ql, kern, p, cpus, T, got, &st, &site);
                    long items = 0;
                    for (int k = 0; k < T; k++) items += st.thread_items[k];
                    CHECK(!memcmp(got, ref, sizeof(int) * db.nseq), "set %s kernel %d policy %s: output differs", sets[si], kern, map_names[p]);
                    CHECK(items == (long)(kern == KERNEL_SIMD ? db.nbatch : db.nseq), "set %s policy %s: %ld items processed", sets[si], map_names[p], items);
                    search_stats_free(&st);
                    combos++;
                }
                map_site_free(&site);
            }
    }
    printf("  %d runs compared on %s (%lu sequences): %s\n", combos, fasta, (unsigned long)db.nseq, failures > f0 ? "see failures" : "all identical");
    free(ref); free(got); free(q);
    db_close(&db);
}

static void check_alignment(const unsigned char *q, int ql, const unsigned char *d, int dl, const char *what) {
    int *work = malloc(sizeof(int) * 2 * (ql > 0 ? ql : 1));
    int s = sw_scalar(q, ql, d, dl, &M, G, work);
    Alignment a;
    int r = sw_align(q, ql, d, dl, &M, G, &a);
    CHECK(r == 0, "%s: sw_align failed", what);
    CHECK(a.score == s, "%s: alignment score %d != kernel %d", what, a.score, s);
    if (r == 0 && s > 0) {
        CHECK(alignment_rescore(&a, &M, G) == s, "%s: rescored alignment %d != %d", what, alignment_rescore(&a, &M, G), s);
        char *qs = malloc(a.length + 1), *ts = malloc(a.length + 1); int nq = 0, nt = 0;
        for (int k = 0; k < a.length; k++) { if (a.q[k] != '-') qs[nq++] = a.q[k]; if (a.t[k] != '-') ts[nt++] = a.t[k]; }
        int okq = nq == a.q_end - a.q_start + 1, okt = nt == a.t_end - a.t_start + 1;
        for (int k = 0; okq && k < nq; k++) okq = qs[k] == hysw_decode(q[a.q_start - 1 + k]);
        for (int k = 0; okt && k < nt; k++) okt = ts[k] == hysw_decode(d[a.t_start - 1 + k]);
        CHECK(okq && okt, "%s: aligned strings do not match the reported ranges", what);
        CHECK(a.q[0] != '-' && a.t[0] != '-' && a.q[a.length - 1] != '-' && a.t[a.length - 1] != '-', "%s: local alignment starts/ends with a gap", what);
        free(qs); free(ts);
    }
    if (r == 0) align_free(&a);
    free(work);
}

static void test_traceback(void) {
    printf("[4] traceback\n");
    int n = 0;
    unsigned char a[3000], b[3000];
    for (int t = 0; t < 400; t++) {
        int la = 1 + (int)rnd(t < 100 ? 30 : 800);
        for (int i = 0; i < la; i++) a[i] = rres();
        int lb;
        if (t % 3 == 0) { lb = 1 + (int)rnd(800); for (int i = 0; i < lb; i++) b[i] = rres(); }     // unrelated
        else { unsigned char *m = mutate(a, la, &lb, t % 3 == 1 ? 15 : 45); if (lb == 0) { lb = 1; m[0] = 0; } memcpy(b, m, lb); free(m); }
        char what[64]; snprintf(what, sizeof what, "pair %d (%d x %d)", t, la, lb);
        check_alignment(a, la, b, lb, what);
        n++;
    }
    check_alignment(a, 1, a, 1, "single residue vs itself");
    check_alignment(a, 500, a, 500, "identical 500-residue sequences");
    printf("  %d alignments checked\n", n + 2);
}

// Textbook Gotoh H matrix (32-bit, "minus infinity" gap states), H[i*dl + j] for query row i, column j.
static void full_h(const unsigned char *q, int ql, const unsigned char *d, int dl, int *H) {
    const int NEG = -1000000;
    int *E = malloc(sizeof(int) * (dl + 1)), *Hp = calloc(dl + 1, sizeof(int)), *Hc = calloc(dl + 1, sizeof(int)), *Fp = malloc(sizeof(int) * (dl + 1));
    for (int j = 0; j <= dl; j++) Fp[j] = NEG;
    for (int i = 0; i < ql; i++) {
        int e = NEG;
        Hc[0] = 0;
        for (int j = 1; j <= dl; j++) {
            e = (e - G.extend) > (Hc[j - 1] - G.open) ? (e - G.extend) : (Hc[j - 1] - G.open);
            int f = (Fp[j] - G.extend) > (Hp[j] - G.open) ? (Fp[j] - G.extend) : (Hp[j] - G.open);
            int h = Hp[j - 1] + M.s[q[i]][d[j - 1]];
            h = h > e ? h : e; h = h > f ? h : f; h = h > 0 ? h : 0;
            Hc[j] = h; Fp[j] = f;
            H[(size_t)i * dl + (j - 1)] = h;
        }
        int *t = Hp; Hp = Hc; Hc = t;
    }
    free(E); free(Hp); free(Hc); free(Fp);
}

static void test_simd_trace(void) {
    printf("[6] SIMD trace: recorded cells == textbook Gotoh H matrix, for every lane, window and block boundary\n");
    int f0 = failures, cells = 0;
    SeqSet s; memset(&s, 0, sizeof s);
    unsigned char tmp[400];
    for (int k = 0; k < 70; k++) {
        int len = k % 9 == 0 ? (int)rnd(4) : 20 + (int)rnd(300);
        for (int j = 0; j < len; j++) tmp[j] = rres();
        set_add(&s, tmp, len);
    }
    Db db; char err[256];
    if (db_build(&s, &db, err, sizeof err)) { CHECK(0, "db_build: %s", err); seqset_free(&s); return; }
    unsigned char *buf = malloc(db.maxlen + 1);
    for (int qn = 0; qn < 3; qn++) {
        int ql = 30 + (int)rnd(150);
        unsigned char *q = malloc(ql);
        if (qn == 2) { int ml; unsigned char *mq = mutate(s.seq[5], s.len[5], &ml, 20); ql = ml > 0 ? ml : 1; q = realloc(q, ql); memcpy(q, mq, ql); free(mq); }
        else for (int i = 0; i < ql; i++) q[i] = rres();
        for (uint64_t b = 0; b < db.nbatch; b++) {
            const int bl = (int)db.blen[b];
            int windows[3][4] = {{0, ql, 0, bl}, {ql / 3, ql / 2 + 1, 10, 40}, {ql - 1, 1, (uint32_t)bl > 17 ? 15 : 0, 3}};
            int ref_out[HYSW_LANES];
            SimdWork w; simd_work_init(&w, ql); simd_work_profile(&w, q, ql, 1);
            sw_simd_batch(&db, b, q, ql, &M, G, &w, ref_out);
            simd_work_free(&w);
            for (int wi = 0; wi < 3; wi++) {
                int r0 = windows[wi][0], nr = windows[wi][1], c0 = windows[wi][2], nc = windows[wi][3];
                if (r0 + nr > ql) nr = ql - r0;
                if (c0 + nc > bl) nc = bl - c0;
                if (nr <= 0 || nc <= 0) continue;
                int16_t *tr = malloc(sizeof(int16_t) * HYSW_LANES * nr * nc);
                int out[HYSW_LANES], bad = 0, bad_out = 0;
                sw_simd_trace(&db, b, q, ql, &M, G, r0, nr, (uint32_t)c0, nc, tr, out);
                for (int l = 0; l < HYSW_LANES; l++) {
                    uint64_t k = b * HYSW_LANES + l;
                    if (k >= db.nseq) break;
                    if (out[l] != ref_out[l]) bad_out++;
                    int dl = (int)db.len[k];
                    if (dl == 0) continue;
                    db_get_seq(&db, k, buf);
                    int *H = malloc(sizeof(int) * ql * dl);
                    full_h(q, ql, buf, dl, H);
                    for (int i = 0; i < nr; i++)
                        for (int j = 0; j < nc && c0 + j < dl; j++, cells++)
                            if (tr[((size_t)i * nc + j) * HYSW_LANES + l] != H[(size_t)(r0 + i) * dl + c0 + j]) bad++;
                    free(H);
                }
                CHECK(bad == 0 && bad_out == 0, "query %d batch %lu window %d: %d cells and %d scores differ", qn, (unsigned long)b, wi, bad, bad_out);
                free(tr);
            }
        }
        free(q);
    }
    printf("  %d traced cells compared: %s\n", cells, failures > f0 ? "see failures" : "all identical");
    free(buf); db_close(&db); seqset_free(&s);
}

static void test_calibration(const char *fasta) {
    printf("[5] calibration: decision rule, database slice, a short real run\n");
    int f0 = failures;
    // Decision rule (default = index 0, margin 5%)
    { double v[] = {10, 10.4, 9}; CHECK(calib_choose(v, 3, 0, 0.05) == 0, "4%% faster set must not replace the default"); }
    { double v[] = {10, 10.6, 9}; CHECK(calib_choose(v, 3, 0, 0.05) == 1, "6%% faster set must replace the default"); }
    { double v[] = {10, 9, 8};    CHECK(calib_choose(v, 3, 0, 0.05) == 0, "default fastest: keep it"); }
    { double v[] = {9, 10, 10};   CHECK(calib_choose(v, 3, 0, 0.0) == 1, "tie between non-defaults: the first one"); }
    { double v[] = {10, 10};      CHECK(calib_choose(v, 2, 0, 0.0) == 0, "exact tie with margin 0: keep the default"); }
    { double v[] = {7, 10};       CHECK(calib_choose(v, 2, -1, 0.5) == 1, "no default: fastest"); }
    { double v[] = {1};           CHECK(calib_choose(v, 0, 0, 0.05) == -1, "no sets: -1"); }

    Db db; char err[256];
    if (db_open(fasta, &db, err, sizeof err)) { CHECK(0, "%s", err); return; }
    const uint64_t full = db.nseq / HYSW_LANES;
    int ql = 250; unsigned char *q = malloc(ql);
    for (int i = 0; i < ql; i++) q[i] = rres();
    int *ref = malloc(sizeof(int) * db.nseq), *got = malloc(sizeof(int) * db.nseq), *sref = malloc(sizeof(int) * db.nseq);
    expected(&db, q, ql, ref);
    Topology t; topology_read(&t);
    int cpus[HYSW_MAXCPU]; int T = topology_set(&t, "physical", cpus, HYSW_MAXCPU, err, sizeof err);
    uint64_t strides[][2] = {{1, 0}, {3, 1}, {7, 6}, {64, 32}, {0, 0}, {5, 1000000}};   // {stride, first}
    for (size_t k = 0; k < sizeof strides / sizeof *strides; k++) {
        uint64_t st_ = strides[k][0] ? strides[k][0] : 1, fi = strides[k][1] < full ? strides[k][1] : 0;
        Db sl;
        if (calib_db_slice(&db, strides[k][0], strides[k][1], &sl)) { CHECK(0, "slice %lu/%lu failed", (unsigned long)strides[k][0], (unsigned long)strides[k][1]); continue; }
        uint64_t nb = (full - fi + st_ - 1) / st_, nres = 0, cells = 0;
        uint32_t mx = 0;
        int bad_map = 0, bad = 0;
        for (uint64_t i = 0; i < nb; i++) {
            uint64_t b = fi + i * st_;
            cells += (uint64_t)db.blen[b] * HYSW_LANES; if (db.blen[b] > mx) mx = db.blen[b];
            for (int l = 0; l < HYSW_LANES; l++) nres += db.len[b * HYSW_LANES + l];
        }
        CHECK(sl.nbatch == nb && sl.nseq == nb * HYSW_LANES && sl.nres == nres && sl.cells == cells && sl.maxlen == mx,
              "slice %lu/%lu: sizes (%lu batches, expected %lu)", (unsigned long)st_, (unsigned long)fi, (unsigned long)sl.nbatch, (unsigned long)nb);
        expected(&sl, q, ql, sref);                                  // scalar on the slice ...
        for (uint64_t i = 0; i < nb; i++)                            // ... equals scalar on the full database
            for (int l = 0; l < HYSW_LANES; l++) if (sref[i * HYSW_LANES + l] != ref[(fi + i * st_) * HYSW_LANES + l]) bad_map++;
        LoopSite site; map_site_init(&site, T, cpus, 1);
        SearchStats ss;
        memset(got, 0xff, sizeof(int) * sl.nseq);
        g_profile_query = 1; g_untiled = 0;
        run(&sl, q, ql, KERNEL_SIMD, MAP_DYN1, cpus, T, got, &ss, &site);
        for (uint64_t i = 0; i < sl.nseq; i++) if (got[i] != sref[i]) bad++;
        CHECK(bad_map == 0, "slice %lu/%lu: %d scores differ from the full database", (unsigned long)st_, (unsigned long)fi, bad_map);
        CHECK(bad == 0, "slice %lu/%lu: SIMD on the slice differs from scalar in %d sequences", (unsigned long)st_, (unsigned long)fi, bad);
        search_stats_free(&ss); map_site_free(&site);
        calib_db_slice_free(&sl);
    }
    { Db sl; CHECK(calib_db_slice(&db, 1, 0, &sl) == 0 && sl.res == db.res, "slice shares the residue array"); calib_db_slice_free(&sl); }

    Calib c;
    int r = calibrate(&db, &M, G, &t, 0.2, 0.05, 3, &c, err, sizeof err);
    CHECK(r == 0, "calibrate failed: %s", err);
    if (r == 0) {
        double med[CALIB_MAXSET]; int pos = 1;
        for (int s = 0; s < c.nset; s++) { med[s] = c.set[s].median; for (int k = 0; k < 3; k++) pos &= c.set[s].gcups[k] > 0; }
        CHECK(c.nset >= 1 && c.def >= 0 && !strcmp(c.set[c.def].name, "physical"), "default set must be physical");
        CHECK(c.chosen == calib_choose(med, c.nset, c.def, 0.05) && c.best == calib_choose(med, c.nset, -1, 0), "choice inconsistent with the rule");
        CHECK(pos, "every trial must report a positive speed");
        CHECK(c.query_len >= 1 && c.passes >= 1 && c.slice_batches >= 1, "calibration record incomplete");
        printf("  calibration on %s: %d sets, chose %s (fastest %s), %.2f s for a 0.2 s budget\n", fasta, c.nset,
               c.set[c.chosen].name, c.set[c.best].name, c.seconds);
    }
    printf("  %s\n", failures > f0 ? "see failures" : "all checks passed");
    free(q); free(ref); free(got); free(sref);
    db_close(&db);
}

int main(int argc, char **argv) {
    char err[256];
    if (matrix_load(argc > 1 ? argv[1] : "data/BLOSUM62", &M, err, sizeof err)) { printf("%s\n", err); return 1; }
    double t0 = omp_get_wtime();
    test_simd_vs_scalar();
    test_overflow();
    test_traceback();
    test_policies(argc > 2 ? argv[2] : "data/ecoli_k12.fasta");
    test_calibration(argc > 2 ? argv[2] : "data/ecoli_k12.fasta");
    test_simd_trace();
    printf("KERNEL TESTS %s: %d checks, %d failures, %.1f s\n", failures ? "FAIL" : "PASS", checks, failures, omp_get_wtime() - t0);
    return failures ? 1 : 0;
}
