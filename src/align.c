// Traceback for the reported hits (scalar, 32-bit). Only run for the top hits, so clarity beats speed.
//
// A full direction matrix for query x database sequence can be large (5,000 x 35,000 = 175 MB), so the
// alignment is found in three passes:
//   1. forward score pass       -> best score S and the cell (ie, je) where an optimal alignment ends;
//   2. reverse score pass on the reversed prefixes q[0..ie], d[0..je] -> a cell where an optimal
//      alignment starts, so some optimal alignment lies inside the rectangle [is..ie] x [js..je];
//   3. Smith–Waterman with a 1-byte direction per cell, inside that rectangle only, then traceback.
// Recurrences and tie-breaking are identical to sw_scalar.c, so the score always equals the kernel's.
#include "hysw.h"
#include <stdlib.h>
#include <string.h>

#define NEG (-(1 << 28))

// Score pass that also returns the first cell (in column order) holding the best score.
static int sw_end(const unsigned char *q, int ql, const unsigned char *d, int dl, int qstep, int dstep,
                  const Matrix *m, Gaps g, int *bi, int *bj) {
    int *H = malloc(sizeof(int) * ql), *E = malloc(sizeof(int) * ql);
    int best = 0; *bi = *bj = -1;
    for (int i = 0; i < ql; i++) { H[i] = 0; E[i] = NEG; }
    for (int j = 0; j < dl; j++) {
        const signed char *col = m->s[d[j * dstep]];
        int hdiag = 0, hup = 0, f = NEG;
        for (int i = 0; i < ql; i++) {
            int e = E[i] - g.extend, t = H[i] - g.open;
            if (t > e) e = t;
            f -= g.extend; t = hup - g.open;
            if (t > f) f = t;
            int h = hdiag + col[q[i * qstep]];
            if (e > h) h = e;
            if (f > h) h = f;
            if (h < 0) h = 0;
            hdiag = H[i]; H[i] = h; E[i] = e; hup = h;
            if (h > best) { best = h; *bi = i; *bj = j; }
        }
    }
    free(H); free(E);
    return best;
}

enum { SRC_ZERO = 0, SRC_DIAG = 1, SRC_E = 2, SRC_F = 3, E_EXT = 4, F_EXT = 8, DIAG_START = 16 };

int sw_align(const unsigned char *q, int ql, const unsigned char *d, int dl,
             const Matrix *m, Gaps g, Alignment *a) {
    memset(a, 0, sizeof *a);
    int ie, je;
    int S = sw_end(q, ql, d, dl, 1, 1, m, g, &ie, &je);
    a->score = S;
    if (S == 0) return 0;                                    // no positive-scoring local alignment
    int ri, rj;                                              // pass 2 on reversed prefixes
    int S2 = sw_end(q + ie, ie + 1, d + je, je + 1, -1, -1, m, g, &ri, &rj);
    if (S2 != S) return -1;
    int is = ie - ri, js = je - rj;
    int R = ie - is + 1, C = je - js + 1;
    if ((double)R * C > 1.5e9) return -1;
    unsigned char *dir = malloc((size_t)R * C);
    int *H = malloc(sizeof(int) * R), *E = malloc(sizeof(int) * R);
    if (!dir || !H || !E) { free(dir); free(H); free(E); return -1; }
    const unsigned char *qq = q + is, *dd = d + js;
    int best = 0, bi = -1, bj = -1;
    for (int i = 0; i < R; i++) { H[i] = 0; E[i] = NEG; }
    for (int j = 0; j < C; j++) {
        const signed char *col = m->s[dd[j]];
        int hdiag = 0, hup = 0, f = NEG;
        unsigned char *dcol = dir + (size_t)j * R;
        for (int i = 0; i < R; i++) {
            unsigned char bits = 0;
            int e = E[i] - g.extend, t = H[i] - g.open;
            if (e > t) bits |= E_EXT; else e = t;
            int fe = f - g.extend; t = hup - g.open;
            if (fe > t) { f = fe; bits |= F_EXT; } else f = t;
            int h = hdiag + col[qq[i]], src = SRC_DIAG;
            if (hdiag == 0) bits |= DIAG_START;
            if (e > h) { h = e; src = SRC_E; }
            if (f > h) { h = f; src = SRC_F; }
            if (h <= 0) { h = 0; src = SRC_ZERO; }
            dcol[i] = bits | (unsigned char)src;
            hdiag = H[i]; H[i] = h; E[i] = e; hup = h;
            if (h > best) { best = h; bi = i; bj = j; }
        }
    }
    free(H); free(E);
    if (best != S) { free(dir); return -1; }
    // traceback from (bi, bj)
    size_t cap = (size_t)R + C + 2;
    char *aq = malloc(cap), *at = malloc(cap);
    size_t n = 0;
    int i = bi, j = bj, state = 0;                            // 0 = H, 1 = E, 2 = F
    int si = bi, sj = bj;
    for (;;) {
        unsigned char c = dir[(size_t)j * R + i];
        if (state == 0) {
            int src = c & 3;
            if (src == SRC_DIAG) {
                aq[n] = hysw_decode(qq[i]); at[n] = hysw_decode(dd[j]); n++;
                si = i; sj = j;
                if (c & DIAG_START) break;
                i--; j--;
            } else if (src == SRC_E) state = 1;
            else if (src == SRC_F) state = 2;
            else break;                                       // unreachable for S > 0
        } else if (state == 1) {                              // database residue against a gap
            aq[n] = '-'; at[n] = hysw_decode(dd[j]); n++;
            sj = j;
            state = (c & E_EXT) ? 1 : 0;
            j--;
        } else {                                              // query residue against a gap
            aq[n] = hysw_decode(qq[i]); at[n] = '-'; n++;
            si = i;
            state = (c & F_EXT) ? 2 : 0;
            i--;
        }
        if (i < 0 || j < 0) break;
    }
    free(dir);
    a->q = malloc(n + 1); a->t = malloc(n + 1); a->mid = malloc(n + 1);
    for (size_t k = 0; k < n; k++) {
        char x = aq[n - 1 - k], y = at[n - 1 - k];
        a->q[k] = x; a->t[k] = y;
        if (x == '-' || y == '-') { a->mid[k] = ' '; a->gaps++; }
        else if (x == y) { a->mid[k] = x; a->identities++; a->positives++; }
        else if (m->s[hysw_encode(x)][hysw_encode(y)] > 0) { a->mid[k] = '+'; a->positives++; }
        else a->mid[k] = ' ';
    }
    a->q[n] = a->t[n] = a->mid[n] = 0;
    a->length = (int)n;
    a->q_start = is + si + 1; a->q_end = is + bi + 1;
    a->t_start = js + sj + 1; a->t_end = js + bj + 1;
    free(aq); free(at);
    return 0;
}

int alignment_rescore(const Alignment *a, const Matrix *m, Gaps g) {
    int s = 0;
    for (int k = 0; k < a->length; k++) {
        char x = a->q[k], y = a->t[k];
        if (x == '-' || y == '-') {
            int first = k == 0 || (x == '-' ? a->q[k - 1] != '-' : a->t[k - 1] != '-');
            s -= first ? g.open : g.extend;
        } else s += m->s[hysw_encode(x)][hysw_encode(y)];
    }
    return s;
}

void align_free(Alignment *a) { free(a->q); free(a->t); free(a->mid); a->q = a->t = a->mid = NULL; }
