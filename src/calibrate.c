// Thread-set calibration (blueprint §4.5): time a few candidate thread sets on a slice of the database
// and pick one. Used by `hysw_engine calibrate` and by `search --cores calibrate`.
//
// Workload: a real database sequence of length close to 400 as the query (typical protein length)
// against every s-th full batch of the database. The database is sorted by length, so a strided
// slice keeps the length mix of the whole database; a contiguous slice would be all short or all long.
// Every set does the same work in every trial (same slice, same number of passes), so the medians are
// directly comparable.
//
// Procedure:
//   1. Probe: each set runs twice on a small slice (1/64 of the batches); the second, timed run gives
//      a rough speed per set. This also starts and pins each set's threads.
//   2. Size: choose the slice stride (and, for small databases, passes per trial) so that
//      rounds x sum over sets of (trial cells / set speed) is about `budget` seconds.
//   3. One untimed warm-up pass on the trial slice (page faults of the memory-mapped database).
//   4. `rounds` rounds; in round r the sets run in rotated order starting at set r, so no set always
//      goes first. Each set's speed is the median over rounds.
// The database's residues are read at ~75 MB/s even at 30 GCUPS (about 400 cells per byte for a query
// of length 400), so whether the slice sits in cache does not matter: the kernel is compute-bound.
//
// Decision rule: keep the default set (one thread per physical core) unless the fastest set's median
// is more than `margin` above the default's. Sets that tie within the margin are not worth switching
// to: M6 E2 measured the top three sets within 5% of each other on this laptop.
//
// The calibration runs in whatever power state the machine is in. Started from idle it measures the
// short-burst regime (M6 E4: the first ~10-17 s), which is also the regime of a typical single search.
#include "hysw.h"
#include <omp.h>
#include <stdlib.h>
#include <string.h>

int calib_db_slice(const Db *db, uint64_t stride, uint64_t first, Db *out) {
    memset(out, 0, sizeof *out);
    const uint64_t full = db->nseq / HYSW_LANES;             // batches whose 16 lanes are all real sequences
    if (stride < 1) stride = 1;
    if (first >= full) first = 0;
    uint64_t nb = full > first ? (full - first + stride - 1) / stride : 0;
    if (nb == 0) return -1;
    out->blen = malloc(sizeof(uint32_t) * nb);
    out->boff = malloc(sizeof(uint64_t) * nb);
    out->len = malloc(sizeof(uint32_t) * nb * HYSW_LANES);
    if (!out->blen || !out->boff || !out->len) { calib_db_slice_free(out); return -1; }
    for (uint64_t i = 0; i < nb; i++) {
        uint64_t b = first + i * stride;
        out->blen[i] = db->blen[b];
        out->boff[i] = db->boff[b];
        out->cells += (uint64_t)db->blen[b] * HYSW_LANES;
        if (db->blen[b] > out->maxlen) out->maxlen = db->blen[b];
        for (int l = 0; l < HYSW_LANES; l++) {
            out->len[i * HYSW_LANES + l] = db->len[b * HYSW_LANES + l];
            out->nres += db->len[b * HYSW_LANES + l];
        }
    }
    out->nbatch = nb;
    out->nseq = nb * HYSW_LANES;
    out->res = db->res;                                      // shared with the full database, not owned
    snprintf(out->name, sizeof out->name, "%.100s (calibration slice)", db->name);
    return 0;
}

void calib_db_slice_free(Db *s) {
    free(s->blen); free(s->boff); free(s->len);
    s->blen = NULL; s->boff = NULL; s->len = NULL;
}

int calib_choose(const double *median, int n, int def, double margin) {
    if (n <= 0) return -1;
    int best = 0;
    for (int k = 1; k < n; k++) if (median[k] > median[best]) best = k;
    if (def < 0 || def >= n) return best;
    return median[best] > median[def] * (1.0 + margin) ? best : def;
}

static double median_of(const double *v, int n) {
    double s[CALIB_MAXROUND];
    memcpy(s, v, sizeof(double) * n);
    for (int i = 1; i < n; i++) for (int j = i; j > 0 && s[j] < s[j - 1]; j--) { double t = s[j]; s[j] = s[j - 1]; s[j - 1] = t; }
    return n % 2 ? s[n / 2] : 0.5 * (s[n / 2 - 1] + s[n / 2]);
}

// One timed trial: `passes` score passes of q over the slice. Returns GCUPS (real cells, as everywhere).
static double trial(const Db *sl, const unsigned char *q, int ql, const Matrix *m, Gaps g, const SearchOpts *o,
                    LoopSite *site, int *scores, int passes, int *fail) {
    double cells = 0, secs = 0;
    for (int p = 0; p < passes; p++) {
        SearchStats st;
        if (search_scores(sl, q, ql, m, g, o, site, scores, &st)) { *fail = 1; return 0; }
        cells += st.cells; secs += st.seconds;
        search_stats_free(&st);
    }
    return secs > 0 ? cells / secs / 1e9 : 0;
}

int calibrate(const Db *db, const Matrix *m, Gaps g, const Topology *t, double budget, double margin, int rounds,
              Calib *c, char *err, size_t errlen) {
    memset(c, 0, sizeof *c);
    double t0 = omp_get_wtime();
    if (rounds < 1) rounds = 1;
    if (rounds > CALIB_MAXROUND) rounds = CALIB_MAXROUND;
    c->rounds = rounds; c->budget = budget; c->margin = margin; c->def = -1;

    static const char *names[] = {"physical", "all", "e", "p"};    // the default first
    for (size_t k = 0; k < sizeof names / sizeof *names && c->nset < CALIB_MAXSET; k++) {
        CalibSet *s = &c->set[c->nset];
        char e2[128];
        int T = topology_set(t, names[k], s->cpus, HYSW_MAXCPU, e2, sizeof e2);
        if (T <= 0) continue;
        int dup = 0;                                         // e.g. "p" equals "all" on a non-hybrid CPU
        for (int j = 0; j < c->nset; j++)
            if (c->set[j].T == T && !memcmp(c->set[j].cpus, s->cpus, sizeof(int) * T)) dup = 1;
        if (dup) continue;
        s->name = names[k]; s->T = T;
        if (!strcmp(names[k], "physical")) c->def = c->nset;
        c->nset++;
    }
    if (c->nset == 0) { snprintf(err, errlen, "no thread set available for calibration"); return -1; }

    uint64_t full = db->nseq / HYSW_LANES;
    if (full == 0) { snprintf(err, errlen, "database too small to calibrate (fewer than %d sequences)", HYSW_LANES); return -1; }
    uint64_t qk = 0;                                         // query: the sequence with length closest to 400
    for (uint64_t k = 0; k < db->nseq; k++) {
        long d = labs((long)db->len[k] - 400), dq = labs((long)db->len[qk] - 400);
        if (d < dq) qk = k;
    }
    int ql = (int)db->len[qk];
    if (ql < 1) { snprintf(err, errlen, "database has no non-empty sequence"); return -1; }
    unsigned char *q = malloc((size_t)ql + 1);
    int *scores = malloc(sizeof(int) * full * HYSW_LANES);
    LoopSite site[CALIB_MAXSET];
    SearchOpts o[CALIB_MAXSET];
    if (!q || !scores) { free(q); free(scores); snprintf(err, errlen, "out of memory"); return -1; }
    db_get_seq(db, qk, q);
    c->query_len = ql;
    for (int s = 0; s < c->nset; s++) {
        memset(&o[s], 0, sizeof o[s]);
        o[s].kernel = KERNEL_SIMD; o[s].policy = MAP_DYN1; o[s].profile_query = 1;
        o[s].T = c->set[s].T; memcpy(o[s].cpus, c->set[s].cpus, sizeof(int) * c->set[s].T);
        map_site_init(&site[s], o[s].T, o[s].cpus, 1);
    }
    int fail = 0, ret = -1;
    const int d = c->def >= 0 ? c->def : 0;

    // 1. Probe: rough speed of every set on a small slice.
    Db probe, sl;
    uint64_t ps = full / 64 > 0 ? full / 64 : 1;
    if (calib_db_slice(db, ps, ps / 2, &probe)) { snprintf(err, errlen, "cannot build the probe slice"); goto out; }
    double inv_speed = 0;                                      // sum over sets of 1 / (cells per second)
    for (int s = 0; s < c->nset && !fail; s++) {
        trial(&probe, q, ql, m, g, &o[s], &site[s], scores, 1, &fail);
        double v = trial(&probe, q, ql, m, g, &o[s], &site[s], scores, 1, &fail);
        if (v <= 0) fail = 1; else inv_speed += 1.0 / (v * 1e9);
    }
    calib_db_slice_free(&probe);
    if (fail) { snprintf(err, errlen, "calibration probe failed"); goto out; }
    // 2. Size the trial: rounds x (trial cells x inv_speed) = budget.
    double want = budget / (rounds * inv_speed);               // cells per trial
    c->trial_target = want / (c->nset / inv_speed);            // the mean set's trial time, for the record
    double full_cells = 0;
    for (uint64_t b = 0; b < full; b++) for (int l = 0; l < HYSW_LANES; l++) full_cells += db->len[b * HYSW_LANES + l];
    full_cells *= ql;
    c->stride = (uint64_t)(full_cells / want);
    if (c->stride < 1) c->stride = 1;
    if (calib_db_slice(db, c->stride, c->stride / 2, &sl)) { snprintf(err, errlen, "cannot build the calibration slice"); goto out; }
    c->slice_batches = sl.nbatch; c->slice_residues = sl.nres;
    double slice_cells = (double)sl.nres * ql;
    c->passes = slice_cells >= want ? 1 : (int)(want / slice_cells + 0.5);
    if (c->passes < 1) c->passes = 1;
    if (c->passes > 1000) c->passes = 1000;
    // 3. Warm-up on the trial slice, 4. timed rounds.
    trial(&sl, q, ql, m, g, &o[d], &site[d], scores, 1, &fail);
    for (int r = 0; r < rounds && !fail; r++)
        for (int j = 0; j < c->nset; j++) {
            int s = (r + j) % c->nset;
            c->set[s].gcups[r] = trial(&sl, q, ql, m, g, &o[s], &site[s], scores, c->passes, &fail);
        }
    calib_db_slice_free(&sl);
    if (fail) { snprintf(err, errlen, "calibration search failed (out of memory?)"); goto out; }
    double med[CALIB_MAXSET];
    for (int s = 0; s < c->nset; s++) med[s] = c->set[s].median = median_of(c->set[s].gcups, rounds);
    c->best = calib_choose(med, c->nset, -1, 0);
    c->chosen = calib_choose(med, c->nset, c->def, margin);
    ret = 0;
out:
    for (int s = 0; s < c->nset; s++) map_site_free(&site[s]);
    free(q); free(scores);
    c->seconds = omp_get_wtime() - t0;
    return ret;
}

void calib_print(FILE *f, const Calib *c) {
    fprintf(f, "{\"rule\":\"keep the default set unless the fastest median is more than margin above it\"");
    fprintf(f, ",\"default\":\"%s\",\"chosen\":\"%s\",\"fastest\":\"%s\"", c->def >= 0 ? c->set[c->def].name : "",
            c->set[c->chosen].name, c->set[c->best].name);
    fprintf(f, ",\"margin\":%.4f,\"rounds\":%d,\"passes_per_trial\":%d,\"budget_s\":%.3f,\"trial_target_s\":%.4f,\"seconds\":%.4f",
            c->margin, c->rounds, c->passes, c->budget, c->trial_target, c->seconds);
    fprintf(f, ",\"query_length\":%d,\"slice\":{\"stride\":%lu,\"batches\":%lu,\"residues\":%lu},\"sets\":[",
            c->query_len, (unsigned long)c->stride, (unsigned long)c->slice_batches, (unsigned long)c->slice_residues);
    for (int s = 0; s < c->nset; s++) {
        fprintf(f, "%s{\"name\":\"%s\",\"threads\":%d,\"cpus\":[", s ? "," : "", c->set[s].name, c->set[s].T);
        for (int k = 0; k < c->set[s].T; k++) fprintf(f, "%s%d", k ? "," : "", c->set[s].cpus[k]);
        fprintf(f, "],\"gcups\":[");
        for (int r = 0; r < c->rounds; r++) fprintf(f, "%s%.4f", r ? "," : "", c->set[s].gcups[r]);
        fprintf(f, "],\"median_gcups\":%.4f}", c->set[s].median);
    }
    fprintf(f, "]}");
}
