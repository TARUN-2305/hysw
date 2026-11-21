// Batch kernel: inter-sequence SIMD (the idea behind SWIPE, Rognes 2011), written in plain C.
// The 16 database sequences of one batch are aligned against the query at the same time, one per
// SIMD lane. For database column j we build prof[a][l] = score of residue a against lane l's residue
// j; the innermost loop over the 16 lanes is the same recurrence as sw_scalar.c, applied to 16
// independent problems. That loop has no dependencies between lanes, so GCC turns it into 256-bit
// vector instructions (16 x 16-bit) from `#pragma omp simd`. build.sh fails if the compiler stops
// vectorising either lane loop below.
//
// Two loop orders compute exactly the same cells with exactly the same arithmetic:
//   untiled (the original): for every column j, sweep all query rows i. The H/E arrays of the
//     previous column (64 B x query length) are read and written once per column. Above a query
//     length of ~500 they no longer fit in L1, and four E-cores sharing one L2 saturate it (M6 E5;
//     results/m6_v1_untiled/EXPLORATORY_e5_cache_diagnosis.md).
//   tiled (default): columns are processed in blocks of HYSW_COLBLOCK. Inside a block, each query
//     row i updates all the block's columns before moving to row i+1. The per-column state of the
//     block (H and F of row i-1) and the block's profile stay in L1; the long H/E arrays are read and
//     written once per block instead of once per column (HYSW_COLBLOCK times less traffic).
//
// 16-bit scores: E is clamped at 0 and F starts at 0 instead of "minus infinity", which keeps
// every value in range and cannot change any positive score (a gap value <= 0 never wins against
// H >= 0). A lane whose best score comes within m->max of 32767 could have wrapped, so it is
// reported in the returned mask and re-scored exactly with the 32-bit scalar kernel.
#include "hysw.h"
#include <stdlib.h>
#include <string.h>

// HYSW_COLBLOCK (columns per block, default 16) is defined in hysw.h.

int simd_work_init(SimdWork *w, int ql) {
    size_t bytes = ((sizeof(int16_t) * HYSW_LANES * (size_t)ql) + 63) & ~(size_t)63;
    if (bytes == 0) bytes = 64;
    w->H = aligned_alloc(64, bytes);
    w->E = aligned_alloc(64, bytes);
    w->cap = ql;
    w->untiled = 0;
    simd_work_profile(w, NULL, 0, 0);
    return w->H && w->E ? 0 : -1;
}

void simd_work_profile(SimdWork *w, const unsigned char *q, int ql, int only_query) {
    int present[HYSW_NCODES] = {0};
    for (int i = 0; i < ql; i++) if (q[i] < HYSW_NCODES) present[q[i]] = 1;
    w->ncodes = 0;
    for (int a = 0; a < HYSW_NCODES; a++)
        if (!only_query || present[a]) w->codes[w->ncodes++] = (unsigned char)a;
}

void simd_work_free(SimdWork *w) { free(w->H); free(w->E); w->H = w->E = NULL; w->cap = 0; }

static unsigned finish(const int16_t best[HYSW_LANES], const Matrix *m, int out[HYSW_LANES]) {
    const int limit = 32767 - m->max;
    unsigned risky = 0;
    for (int l = 0; l < HYSW_LANES; l++) {
        out[l] = best[l];
        if (best[l] >= limit) risky |= 1u << l;
    }
    return risky;
}

static unsigned batch_untiled(const Db *db, uint64_t b, const unsigned char *q, int ql,
                              const Matrix *m, Gaps g, SimdWork *w, int out[HYSW_LANES]) {
    const int16_t GO = (int16_t)g.open, GE = (int16_t)g.extend;
    int16_t (*H)[HYSW_LANES] = (int16_t (*)[HYSW_LANES])w->H;
    int16_t (*E)[HYSW_LANES] = (int16_t (*)[HYSW_LANES])w->E;
    int16_t prof[HYSW_NCODES][HYSW_LANES] __attribute__((aligned(32)));
    int16_t best[HYSW_LANES] __attribute__((aligned(32))) = {0};
    const uint32_t bl = db->blen[b];
    const unsigned char *cols = db->res + db->boff[b];

    memset(H, 0, sizeof(int16_t) * HYSW_LANES * ql);
    memset(E, 0, sizeof(int16_t) * HYSW_LANES * ql);
    for (uint32_t j = 0; j < bl; j++) {
        const unsigned char *col = cols + (size_t)j * HYSW_LANES;   // residue j of the 16 lanes (or padding)
        for (int r = 0; r < w->ncodes; r++) {               // profile rows (all 24, or only the query's residues)
            const int a = w->codes[r];
            const signed char *row = m->s[a];
            for (int l = 0; l < HYSW_LANES; l++) prof[a][l] = row[col[l]];
        }
        int16_t hdiag[HYSW_LANES] __attribute__((aligned(32))) = {0};
        int16_t hup[HYSW_LANES] __attribute__((aligned(32))) = {0};
        int16_t f[HYSW_LANES] __attribute__((aligned(32))) = {0};
        for (int i = 0; i < ql; i++) {
            const int16_t *p = prof[q[i]];
            int16_t *hrow = H[i], *erow = E[i];
#pragma omp simd aligned(hrow, erow, p : 32)
            for (int l = 0; l < HYSW_LANES; l++) {   /* HYSW_VEC_LOOP: must be vectorised (checked by build.sh) */
                int16_t e = erow[l] - GE, t = hrow[l] - GO;
                e = t > e ? t : e;
                int16_t ff = f[l] - GE;
                t = hup[l] - GO;
                ff = t > ff ? t : ff;
                int16_t h = hdiag[l] + p[l];
                h = e > h ? e : h;
                h = ff > h ? ff : h;
                h = h < 0 ? 0 : h;
                hdiag[l] = hrow[l];
                hrow[l] = h;
                erow[l] = e > 0 ? e : 0;
                f[l] = ff;
                hup[l] = h;
                best[l] = h > best[l] ? h : best[l];
            }
        }
    }
    return finish(best, m, out);
}

// The tiled kernel body, shared by the search (tr == NULL) and by sw_simd_trace (tr != NULL). It is always
// inlined, so in batch_tiled the constant NULL removes the trace code completely: the search runs exactly the
// loop it ran before tracing existed. With tr set, after each column of a block the 16 lanes' H values of
// cell (i, j) are copied to tr when (i, j) lies in the window rows [tr0, tr0+tnr) x columns [tc0, tc0+tnc).
static inline __attribute__((always_inline))
unsigned tiled_impl(const Db *db, uint64_t b, const unsigned char *q, int ql, const Matrix *m, Gaps g,
                    SimdWork *w, int out[HYSW_LANES], int16_t *tr, int tr0, int tnr, uint32_t tc0, int tnc) {
    const int16_t GO = (int16_t)g.open, GE = (int16_t)g.extend;
    int16_t (*H)[HYSW_LANES] = (int16_t (*)[HYSW_LANES])w->H;     // H(i, last column done), per row i
    int16_t (*E)[HYSW_LANES] = (int16_t (*)[HYSW_LANES])w->E;     // E(i, last column done), per row i
    int16_t prof[HYSW_COLBLOCK][HYSW_NCODES][HYSW_LANES] __attribute__((aligned(32)));
    int16_t hc[HYSW_COLBLOCK][HYSW_LANES] __attribute__((aligned(32)));   // H(i-1, column c of the block)
    int16_t fc[HYSW_COLBLOCK][HYSW_LANES] __attribute__((aligned(32)));   // F(i-1, column c of the block)
    int16_t best[HYSW_LANES] __attribute__((aligned(32))) = {0};
    const uint32_t bl = db->blen[b];
    const unsigned char *cols = db->res + db->boff[b];

    memset(H, 0, sizeof(int16_t) * HYSW_LANES * ql);
    memset(E, 0, sizeof(int16_t) * HYSW_LANES * ql);
    for (uint32_t j0 = 0; j0 < bl; j0 += HYSW_COLBLOCK) {
        const int nc = bl - j0 < HYSW_COLBLOCK ? (int)(bl - j0) : HYSW_COLBLOCK;
        for (int c = 0; c < nc; c++) {                       // profile of every column in the block
            const unsigned char *col = cols + (size_t)(j0 + c) * HYSW_LANES;
            for (int r = 0; r < w->ncodes; r++) {
                const int a = w->codes[r];
                const signed char *row = m->s[a];
                for (int l = 0; l < HYSW_LANES; l++) prof[c][a][l] = row[col[l]];
            }
        }
        memset(hc, 0, sizeof hc);                            // row -1: H = 0, F = 0 (as in the untiled kernel)
        memset(fc, 0, sizeof fc);
        int16_t dprev[HYSW_LANES] __attribute__((aligned(32))) = {0};   // H(i-1, j0-1) before this block overwrote it
        for (int i = 0; i < ql; i++) {
            int16_t *hrow = H[i], *erow = E[i];
            int16_t hl[HYSW_LANES] __attribute__((aligned(32)));        // H(i, j-1): the cell to the left
            int16_t el[HYSW_LANES] __attribute__((aligned(32)));        // E(i, j-1)
            int16_t hd[HYSW_LANES] __attribute__((aligned(32)));        // H(i-1, j-1): the diagonal cell
            for (int l = 0; l < HYSW_LANES; l++) {
                hd[l] = dprev[l];
                dprev[l] = hrow[l];
                hl[l] = hrow[l];
                el[l] = erow[l];
            }
            const int qi = q[i];
            for (int c = 0; c < nc; c++) {
                const int16_t *p = prof[c][qi];
                int16_t *hu = hc[c], *fu = fc[c];
#pragma omp simd aligned(hu, fu, p : 32)
                for (int l = 0; l < HYSW_LANES; l++) {   /* HYSW_VEC_LOOP_TILED: must be vectorised (checked by build.sh) */
                    int16_t e = el[l] - GE, t = hl[l] - GO;
                    e = t > e ? t : e;
                    int16_t ff = fu[l] - GE;
                    t = hu[l] - GO;
                    ff = t > ff ? t : ff;
                    int16_t h = hd[l] + p[l];
                    h = e > h ? e : h;
                    h = ff > h ? ff : h;
                    h = h < 0 ? 0 : h;
                    hd[l] = hu[l];
                    hu[l] = h;
                    fu[l] = ff;
                    hl[l] = h;
                    el[l] = e > 0 ? e : 0;
                    best[l] = h > best[l] ? h : best[l];
                }
                if (tr && i >= tr0 && i < tr0 + tnr && j0 + c >= tc0 && j0 + c < tc0 + (uint32_t)tnc)
                    memcpy(tr + ((size_t)(i - tr0) * tnc + (j0 + c - tc0)) * HYSW_LANES, hu, sizeof(int16_t) * HYSW_LANES);
            }
            for (int l = 0; l < HYSW_LANES; l++) { hrow[l] = hl[l]; erow[l] = el[l]; }
        }
    }
    return finish(best, m, out);
}

static unsigned batch_tiled(const Db *db, uint64_t b, const unsigned char *q, int ql,
                            const Matrix *m, Gaps g, SimdWork *w, int out[HYSW_LANES]) {
    return tiled_impl(db, b, q, ql, m, g, w, out, NULL, 0, 0, 0, 0);
}

int sw_simd_trace(const Db *db, uint64_t b, const unsigned char *q, int ql, const Matrix *m, Gaps g,
                  int r0, int nr, uint32_t c0, int nc, int16_t *tr, int out[HYSW_LANES]) {
    SimdWork w;
    if (simd_work_init(&w, ql)) { simd_work_free(&w); return -1; }
    simd_work_profile(&w, q, ql, 1);
    memset(tr, 0, sizeof(int16_t) * HYSW_LANES * (size_t)nr * nc);
    unsigned risky = tiled_impl(db, b, q, ql, m, g, &w, out, tr, r0, nr, c0, nc);
    simd_work_free(&w);
    return (int)risky;
}

unsigned sw_simd_batch(const Db *db, uint64_t b, const unsigned char *q, int ql,
                       const Matrix *m, Gaps g, SimdWork *w, int out[HYSW_LANES]) {
    return w->untiled ? batch_untiled(db, b, q, ql, m, g, w, out) : batch_tiled(db, b, q, ql, m, g, w, out);
}
