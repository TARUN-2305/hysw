// EXPLORATORY SPIKE 1B-B2 (disposable) -- protein database search, SIMD + threads.
// Same search as swsearch.c, but 16 database sequences are aligned at once ("inter-sequence"
// vectorisation, the idea behind SWIPE): the innermost loop runs over 16 lanes of 16-bit scores
// and is vectorised by the compiler from plain C with `#pragma omp simd` (no intrinsics).
// Sequences are sorted by length so that the 16 lanes of a batch have similar lengths.
// One parallel-loop iteration = one batch of 16 sequences.
// Workload: each query protein is aligned against every sequence of a real proteome
// (E. coli K-12, reviewed UniProt entries). One loop iteration = one database sequence,
// so iteration cost is proportional to that sequence's length (known in advance).
// Scoring: BLOSUM62 (NCBI file), affine gaps 11 + 1 per residue (BLAST defaults). Score only.
// SCALAR implementation: the SIMD version a real project would need is NOT tested here.
// Build: gcc -O2 -march=native -fopenmp -Wall swsimd.c -o swsimd -lm
// Run:   ./swsimd <db.fasta> <BLOSUM62> sorted <cpus> <rounds> [sched list] [replicate K]
#include "hsched.h"
#include <ctype.h>

static int nseq, maxlen;
static unsigned char **seq; static int *len;
static signed char S[32][32];
static int *score;                      // output: best local score per database sequence
static const unsigned char *Q; static int QL;
static short *Hbuf[HS_MAXT], *Ebuf[HS_MAXT];
static float *cost;

static int sw(const unsigned char *d, int dl, short *Hh, short *E) {
    const int GO = 12, GE = 1;
    int best = 0;
    for (int i = 0; i < QL; i++) { Hh[i] = 0; E[i] = 0; }
    for (int j = 0; j < dl; j++) {
        const signed char *row = S[d[j]];
        int hdiag = 0, hup = 0, f = 0;
        for (int i = 0; i < QL; i++) {
            int e = E[i] - GE, t = Hh[i] - GO; if (t > e) e = t;       // gap along the database
            f -= GE; t = hup - GO; if (t > f) f = t;                   // gap along the query
            int h = hdiag + row[Q[i]];
            if (e > h) h = e;
            if (f > h) h = f;
            if (h < 0) h = 0;
            hdiag = Hh[i]; Hh[i] = (short)h; E[i] = (short)(e > 0 ? e : 0); hup = h;
            if (h > best) best = h;
        }
    }
    return best;
}
#define LN 16
static short *HB[HS_MAXT], *EB[HS_MAXT];          // per-thread [query position][lane] score arrays
static void sw_batch(long first, int nb, int tid) {
    const short GO = 12, GE = 1;
    short (*Hh)[LN] = (short (*)[LN])HB[tid], (*E)[LN] = (short (*)[LN])EB[tid];
    short prof[24][LN] __attribute__((aligned(32))), best[LN] __attribute__((aligned(32))) = {0};
    int mlen = 0;
    for (int l = 0; l < nb; l++) if (len[first + l] > mlen) mlen = len[first + l];
    memset(Hh, 0, sizeof(short) * LN * QL); memset(E, 0, sizeof(short) * LN * QL);
    for (int j = 0; j < mlen; j++) {
        for (int a = 0; a < 24; a++)               // score of every residue type against this column's 16 residues
            for (int l = 0; l < LN; l++) prof[a][l] = (l < nb && j < len[first + l]) ? S[seq[first + l][j]][a] : -100;
        short hdiag[LN] __attribute__((aligned(32))) = {0}, hup[LN] __attribute__((aligned(32))) = {0}, f[LN] __attribute__((aligned(32))) = {0};
        for (int i = 0; i < QL; i++) {
            const short *p = prof[Q[i]];
            short *hrow = Hh[i], *erow = E[i];
#pragma omp simd
            for (int l = 0; l < LN; l++) {
                short e = erow[l] - GE, t = hrow[l] - GO; e = t > e ? t : e;
                short ff = f[l] - GE; t = hup[l] - GO; ff = t > ff ? t : ff;
                short h = hdiag[l] + p[l];
                h = e > h ? e : h; h = ff > h ? ff : h; h = h < 0 ? 0 : h;
                hdiag[l] = hrow[l]; hrow[l] = h; erow[l] = e > 0 ? e : 0; f[l] = ff; hup[l] = h;
                best[l] = h > best[l] ? h : best[l];
            }
        }
    }
    for (int l = 0; l < nb; l++) score[first + l] = best[l];
}
static long NBATCH;
static void body_scalar(long lo, long hi, int tid, void *ctx) {
    (void)ctx;
    for (long k = lo; k < hi; k++) score[k] = sw(seq[k], len[k], Hbuf[tid], Ebuf[tid]);
}
static void body(long lo, long hi, int tid, void *ctx) {       // iterations are batches of 16 sequences
    (void)ctx;
    for (long b = lo; b < hi; b++) { long a = b * LN; int nb = a + LN > nseq ? nseq - (int)a : LN; sw_batch(a, nb, tid); }
}
static int bylen(const void *a, const void *b) { return len[*(const int *)b] - len[*(const int *)a]; }

int main(int argc, char **argv) {
    if (argc < 6) { fprintf(stderr, "usage: see header\n"); return 1; }
    const char *aa = "ARNDCQEGHILKMFPSTWYVBZX*";
    int idx[256]; for (int c = 0; c < 256; c++) idx[c] = 22;           // unknown -> X
    for (int k = 0; aa[k]; k++) idx[(int)aa[k]] = k;
    FILE *f = fopen(argv[2], "r"); char line[4096]; int r = 0;
    if (!f) { perror("matrix"); return 1; }
    while (fgets(line, sizeof line, f)) {                              // NCBI matrix: header row then 24 rows
        if (line[0] == '#' || line[0] == ' ') continue;
        char *p = line + 1;
        for (int c = 0; c < 24; c++) S[r][c] = (signed char)strtol(p, &p, 10);
        r++;
    }
    fclose(f);
    f = fopen(argv[1], "r");
    if (!f) { perror("fasta"); return 1; }
    int cap = 8192; seq = malloc(sizeof(*seq) * cap); len = malloc(sizeof(int) * cap);
    int cur = -1, ccap = 0; long total = 0;
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '>') { cur = nseq++; seq[cur] = NULL; len[cur] = 0; ccap = 0; continue; }
        for (char *p = line; *p; p++) {
            if (!isalpha((unsigned char)*p)) continue;
            if (len[cur] == ccap) { ccap = ccap ? ccap * 2 : 256; seq[cur] = realloc(seq[cur], ccap); }
            seq[cur][len[cur]++] = (unsigned char)idx[toupper((unsigned char)*p)];
            total++;
        }
    }
    fclose(f);
    for (int k = 0; k < nseq; k++) if (len[k] > maxlen) maxlen = len[k];
    // queries: three real proteins from the same proteome, chosen by length (short / medium / long)
    int want[3] = {150, 400, 1000}, qi[3];
    for (int w = 0; w < 3; w++) { int bestd = 1 << 30; for (int k = 0; k < nseq; k++) { int dd = abs(len[k] - want[w]); if (dd < bestd) { bestd = dd; qi[w] = k; } } }
    unsigned char *qs[3]; int ql[3];
    for (int w = 0; w < 3; w++) { ql[w] = len[qi[w]]; qs[w] = malloc(ql[w]); memcpy(qs[w], seq[qi[w]], ql[w]); }
    if (!strcmp(argv[3], "sorted")) {                                  // longest first, as batching SIMD tools arrange it
        int *ord = malloc(sizeof(int) * nseq); for (int k = 0; k < nseq; k++) ord[k] = k;
        qsort(ord, nseq, sizeof(int), bylen);
        unsigned char **s2 = malloc(sizeof(*s2) * nseq); int *l2 = malloc(sizeof(int) * nseq);
        for (int k = 0; k < nseq; k++) { s2[k] = seq[ord[k]]; l2[k] = len[ord[k]]; }
        seq = s2; len = l2;
    }
    int K = argc > 7 ? atoi(argv[7]) : 1;                              // replicate the database K times (keeps length order)
    if (K > 1) {
        unsigned char **s2 = malloc(sizeof(*s2) * nseq * K); int *l2 = malloc(sizeof(int) * nseq * K);
        for (int k = 0; k < nseq; k++) for (int r2 = 0; r2 < K; r2++) { s2[k * K + r2] = seq[k]; l2[k * K + r2] = len[k]; }
        seq = s2; len = l2; nseq *= K; total *= K;
    }
    hs_init(argv[4]);
    int rounds = atoi(argv[5]);
    int list[HS_NSCHED], nl = 0;
    if (argc > 6) { char *s = strdup(argv[6]); for (char *p = strtok(s, ","); p; p = strtok(NULL, ",")) list[nl++] = atoi(p); }
    else for (int k = 0; k < HS_NSCHED; k++) list[nl++] = k;
    for (int t = 0; t < hs_T; t++) { Hbuf[t] = malloc(sizeof(short) * 4096); Ebuf[t] = malloc(sizeof(short) * 4096);
                                      HB[t] = aligned_alloc(64, sizeof(short) * LN * 4096); EB[t] = aligned_alloc(64, sizeof(short) * LN * 4096); }
    NBATCH = (nseq + LN - 1) / LN;
    score = malloc(sizeof(int) * nseq * 3); cost = malloc(sizeof(float) * NBATCH);
    for (long b = 0; b < NBATCH; b++) cost[b] = (float)len[b * LN];        // batch cost ~ its longest sequence (sorted: the first)
    int *out = score;
    printf("swsearch: %d sequences, %ld residues (longest %d), order=%s, queries of length %d/%d/%d, threads=%d\n",
           nseq, total, maxlen, argv[3], ql[0], ql[1], ql[2], hs_T);

    double tseq = 1e30; unsigned long long ref = 0;
    for (int rep = 0; rep < 2 && K == 1; rep++) {                      // scalar reference (skipped when the database is replicated)
        double t0 = omp_get_wtime();
        for (int w = 0; w < 3; w++) { Q = qs[w]; QL = ql[w]; score = out + w * nseq; body_scalar(0, nseq, 0, NULL); }
        double t = omp_get_wtime() - t0; if (t < tseq) tseq = t;
        ref = hs_hash(out, sizeof(int) * nseq * 3);
    }
    double tsimd = 1e30; int simd_ok = 1;                                  // one thread, SIMD batches
    for (int rep = 0; rep < 3; rep++) {
        memset(out, 0, sizeof(int) * nseq * 3);
        double t0 = omp_get_wtime();
        for (int w = 0; w < 3; w++) { Q = qs[w]; QL = ql[w]; score = out + w * nseq; body(0, NBATCH, 0, NULL); }
        double t = omp_get_wtime() - t0; if (t < tsimd) tsimd = t;
        if (K == 1 && hs_hash(out, sizeof(int) * nseq * 3) != ref) simd_ok = 0;
        if (K > 1) ref = hs_hash(out, sizeof(int) * nseq * 3);         // replicated: 1-thread SIMD result is the reference
    }
    if (K > 1) { tseq = tsimd; printf("database replicated x%d: scalar run skipped; 'speedup' below is relative to ONE-THREAD SIMD\n", K); }
    printf("one thread, SIMD batches of %d: %.3f s = %.2f GCUPS, %.1fx over scalar, scores %s\n", LN, tsimd,
           (double)total * (ql[0] + ql[1] + ql[2]) / tsimd / 1e9, tseq / tsimd, simd_ok ? "identical to scalar" : "DIFFER");
    printf("sequential scalar (cpu%d): %.3f s for 3 queries = %.2f GCUPS (billion cell updates per second)\n", hs_cpu[0], tseq,
           (double)total * (ql[0] + ql[1] + ql[2]) / tseq / 1e9);

    static double w[HS_NSCHED][32]; int ok[HS_NSCHED]; double stl[HS_NSCHED] = {0}, sp[HS_NSCHED][HS_MAXT];
    static hs_site site;
    for (int k = 0; k < HS_NSCHED; k++) ok[k] = 1;
    for (int rd = 0; rd < rounds; rd++)
        for (int o = 0; o < nl; o++) {
            int c = hs_pick(list, nl, rd, o);                 // random order per round
            hs_condition(0.5);                                   // same all-core load before every measured run
            hs_site_reset(&site);                                      // weights are learned across the 3 queries only
            memset(out, 0, sizeof(int) * nseq * 3);
            double t0 = omp_get_wtime();
            for (int q = 0; q < 3; q++) { Q = qs[q]; QL = ql[q]; score = out + q * nseq; hs_for(c, &site, NBATCH, body, NULL, cost); }
            w[c][rd] = omp_get_wtime() - t0;
            if (hs_hash(out, sizeof(int) * nseq * 3) != ref) ok[c] = 0;
            stl[c] = site.calls ? (double)site.steals / site.calls : 0;
            for (int k = 0; k < hs_T; k++) sp[c][k] = c == HS_SHARE ? site.w[k] : site.speed[k];
        }
    double med[HS_NSCHED], best = 1e30;
    for (int o = 0; o < nl; o++) {
        int c = list[o];
        printf("  raw %-18s", hs_name[c]); for (int rd = 0; rd < rounds; rd++) printf(" %.3f", w[c][rd]); printf("\n");
        qsort(w[c], rounds, sizeof(double), hs_cmpd); med[c] = w[c][rounds / 2]; if (med[c] < best) best = med[c];
    }
    printf("%-19s %8s %8s %8s %9s  %s\n", "schedule", "total_s", "speedup", "regret", "steals", "output");
    for (int o = 0; o < nl; o++) {
        int c = list[o];
        printf("%-19s %8.3f %8.2f %+7.1f%% %9.1f  %s\n", hs_name[c], med[c], tseq / med[c], 100 * (med[c] / best - 1), stl[c], ok[c] ? "identical scores" : "DIFFERS");
    }
    if (ok[HS_COSTCAP] && med[HS_COSTCAP] > 0) { printf("  cost*cap+steal final per-thread speeds:"); for (int k = 0; k < hs_T; k++) printf(" %.3f", sp[HS_COSTCAP][k]); printf("\n"); }
    return 0;
}
