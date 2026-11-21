// Reference kernel: Smith–Waterman local alignment with affine gaps (Gotoh), 32-bit scores, score only.
//
//   H(i,j) = max(0, H(i-1,j-1) + s(q_i, d_j), E(i,j), F(i,j))
//   E(i,j) = max(E(i,j-1) - extend, H(i,j-1) - open)      gap in the query   (database residue vs '-')
//   F(i,j) = max(F(i-1,j) - extend, H(i-1,j) - open)      gap in the database (query residue vs '-')
//
// The database sequence is walked column by column (j); the column state is kept in two rows of
// length ql (H and E), so memory is O(ql). Values start at "minus infinity" (NEG) as in the textbook.
#include "hysw.h"

#define NEG (-(1 << 28))

int sw_scalar(const unsigned char *q, int ql, const unsigned char *d, int dl,
              const Matrix *m, Gaps g, int *work) {
    int *H = work, *E = work + ql;
    const int open = g.open, ext = g.extend;
    int best = 0;
    for (int i = 0; i < ql; i++) { H[i] = 0; E[i] = NEG; }
    for (int j = 0; j < dl; j++) {
        const signed char *col = m->s[d[j]];     // symmetric matrix: s[d][q] == s[q][d]
        int hdiag = 0, hup = 0, f = NEG;
        for (int i = 0; i < ql; i++) {
            int e = E[i] - ext, t = H[i] - open;
            if (t > e) e = t;
            f -= ext; t = hup - open;
            if (t > f) f = t;
            int h = hdiag + col[q[i]];
            if (e > h) h = e;
            if (f > h) h = f;
            if (h < 0) h = 0;
            hdiag = H[i]; H[i] = h; E[i] = e; hup = h;
            if (h > best) best = h;
        }
    }
    return best;
}
