// Substitution matrix loading (NCBI text format: '#' comments, a header row of letters, then one row
// per letter). The kernels index s[query residue][database residue]; the matrix must be symmetric.
#include "hysw.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

int matrix_load(const char *path, Matrix *m, char *err, size_t errlen) {
    memset(m, 0, sizeof *m);
    FILE *f = fopen(path, "r");
    if (!f) { snprintf(err, errlen, "cannot open matrix %s", path); return -1; }
    char line[1024];
    int cols[32], ncol = 0, seen[32] = {0}, nrow = 0;
    for (int a = 0; a < 32; a++) for (int b = 0; b < 32; b++) m->s[a][b] = HYSW_PAD_SCORE;
    while (fgets(line, sizeof line, f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\n' || *p == 0) continue;
        if (ncol == 0) {                                   // header row: column letters
            for (; *p; p++) {
                if (isspace((unsigned char)*p)) continue;
                if (ncol == 32) { snprintf(err, errlen, "%s: too many columns", path); fclose(f); return -1; }
                cols[ncol++] = *p == '*' ? 23 : hysw_encode(*p);
            }
            continue;
        }
        int r = *p == '*' ? 23 : hysw_encode(*p);
        p++;
        for (int c = 0; c < ncol; c++) {
            char *e;
            long v = strtol(p, &e, 10);
            if (e == p) { snprintf(err, errlen, "%s: short row for '%c'", path, line[0]); fclose(f); return -1; }
            m->s[r][cols[c]] = (signed char)v;
            p = e;
        }
        seen[r] = 1; nrow++;
    }
    fclose(f);
    for (int a = 0; a < HYSW_NCODES; a++)
        if (!seen[a]) { snprintf(err, errlen, "%s: no row for residue '%c'", path, hysw_decode(a)); return -1; }
    m->max = -1000; m->min = 1000;
    for (int a = 0; a < HYSW_NCODES; a++)
        for (int b = 0; b < HYSW_NCODES; b++) {
            if (m->s[a][b] != m->s[b][a]) { snprintf(err, errlen, "%s: matrix is not symmetric", path); return -1; }
            if (m->s[a][b] > m->max) m->max = m->s[a][b];
            if (m->s[a][b] < m->min) m->min = m->s[a][b];
        }
    const char *base = strrchr(path, '/');
    snprintf(m->name, sizeof m->name, "%s", base ? base + 1 : path);
    (void)nrow;
    return 0;
}
