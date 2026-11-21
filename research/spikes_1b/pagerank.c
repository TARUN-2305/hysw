// EXPLORATORY SPIKE 1B-C (disposable) -- PageRank on a real web graph (SNAP web-Google).
// Workload: the same irregular loop repeated every iteration. One loop iteration here = a block
// of 256 vertices; its cost is roughly its number of incoming links (known in advance), and the
// work is dominated by random memory reads (latency-bound).
// Simplified PageRank: rank lost through pages without out-links is not redistributed.
// Build: gcc -O2 -march=native -fopenmp -Wall pagerank.c -o pagerank -lm
// Run:   ./pagerank <edges.txt> <iterations> <cpus> <rounds> [sched list]
#include "hsched.h"

#define BLK 256
static int N; static long M;
static long *off; static int *src;           // incoming-edge CSR
static int *outdeg;
static double *rank_, *contrib, *nxt;
static float *cost1, *cost2;
static long NB;

static void body_contrib(long lo, long hi, int tid, void *ctx) {      // uniform, streaming
    (void)tid; (void)ctx;
    long a = lo * BLK, b = hi * BLK; if (b > N) b = N;
    for (long u = a; u < b; u++) contrib[u] = outdeg[u] ? rank_[u] / outdeg[u] : 0.0;
}
static void body_pull(long lo, long hi, int tid, void *ctx) {         // irregular, random reads
    (void)tid; (void)ctx;
    long a = lo * BLK, b = hi * BLK; if (b > N) b = N;
    const double base = 0.15 / N;
    for (long v = a; v < b; v++) {
        double s = 0;
        for (long e = off[v]; e < off[v + 1]; e++) s += contrib[src[e]];
        nxt[v] = base + 0.85 * s;
    }
}

int main(int argc, char **argv) {
    if (argc < 5) { fprintf(stderr, "usage: see header\n"); return 1; }
    FILE *f = fopen(argv[1], "r");
    if (!f) { perror("graph"); return 1; }
    long cap = 6000000; int *eu = malloc(sizeof(int) * cap), *ev = malloc(sizeof(int) * cap);
    char line[256]; int mx = 0;
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#') continue;
        int u, v; if (sscanf(line, "%d %d", &u, &v) != 2) continue;
        if (M == cap) { cap *= 2; eu = realloc(eu, sizeof(int) * cap); ev = realloc(ev, sizeof(int) * cap); }
        eu[M] = u; ev[M] = v; M++; if (u > mx) mx = u; if (v > mx) mx = v;
    }
    fclose(f);
    N = mx + 1; NB = (N + BLK - 1) / BLK;
    off = calloc(N + 1, sizeof(long)); outdeg = calloc(N, sizeof(int)); src = malloc(sizeof(int) * M);
    for (long e = 0; e < M; e++) { off[ev[e] + 1]++; outdeg[eu[e]]++; }
    for (int v = 0; v < N; v++) off[v + 1] += off[v];
    long *pos = malloc(sizeof(long) * N); memcpy(pos, off, sizeof(long) * N);
    for (long e = 0; e < M; e++) src[pos[ev[e]]++] = eu[e];
    free(pos); free(eu); free(ev);
    rank_ = malloc(sizeof(double) * N); contrib = malloc(sizeof(double) * N); nxt = malloc(sizeof(double) * N);
    cost1 = malloc(sizeof(float) * NB); cost2 = malloc(sizeof(float) * NB);
    float cmin = 1e30, cmax = 0;
    for (long b = 0; b < NB; b++) {
        long a = b * BLK, z = a + BLK > N ? N : a + BLK;
        cost1[b] = 1.0f; cost2[b] = (float)(off[z] - off[a]) + 0.5f * BLK;
        if (cost2[b] < cmin) cmin = cost2[b]; if (cost2[b] > cmax) cmax = cost2[b];
    }
    int iters = atoi(argv[2]);
    hs_init(argv[3]);
    int rounds = atoi(argv[4]);
    int list[HS_NSCHED], nl = 0;
    if (argc > 5) { char *s = strdup(argv[5]); for (char *p = strtok(s, ","); p; p = strtok(NULL, ",")) list[nl++] = atoi(p); }
    else for (int k = 0; k < HS_NSCHED; k++) list[nl++] = k;
    printf("pagerank: %d vertex ids, %ld edges, %ld blocks of %d vertices (block cost %.0f..%.0f), %d iterations, threads=%d\n",
           N, M, NB, BLK, cmin, cmax, iters, hs_T);

    double tseq = 1e30; unsigned long long ref = 0;
    for (int rep = 0; rep < 2; rep++) {
        for (int v = 0; v < N; v++) rank_[v] = 1.0 / N;
        double t0 = omp_get_wtime();
        for (int it = 0; it < iters; it++) { body_contrib(0, NB, 0, NULL); body_pull(0, NB, 0, NULL); double *t = rank_; rank_ = nxt; nxt = t; }
        double t = omp_get_wtime() - t0; if (t < tseq) tseq = t;
        ref = hs_hash(rank_, sizeof(double) * N);
    }
    printf("sequential (cpu%d): %.3f s (%.1f ms per iteration)\n", hs_cpu[0], tseq, 1000 * tseq / iters);

    static double w[HS_NSCHED][32]; int ok[HS_NSCHED]; double stl[HS_NSCHED] = {0}, sp[HS_MAXT];
    static hs_site s1, s2;
    for (int k = 0; k < HS_NSCHED; k++) ok[k] = 1;
    for (int rd = 0; rd < rounds; rd++)
        for (int o = 0; o < nl; o++) {
            int c = hs_pick(list, nl, rd, o);                 // random order per round
            hs_condition(0.5);                                   // same all-core load before every measured run
            hs_site_reset(&s1); hs_site_reset(&s2);
            for (int v = 0; v < N; v++) rank_[v] = 1.0 / N;
            double t0 = omp_get_wtime();
            for (int it = 0; it < iters; it++) {
                hs_for(c, &s1, NB, body_contrib, NULL, cost1);
                hs_for(c, &s2, NB, body_pull, NULL, cost2);
                double *t = rank_; rank_ = nxt; nxt = t;
            }
            w[c][rd] = omp_get_wtime() - t0;
            if (hs_hash(rank_, sizeof(double) * N) != ref) ok[c] = 0;
            stl[c] = s2.calls ? (double)s2.steals / s2.calls : 0;
            if (c == HS_COSTCAP) for (int k = 0; k < hs_T; k++) sp[k] = s2.speed[k];
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
        printf("%-19s %8.3f %8.2f %+7.1f%% %9.1f  %s\n", hs_name[c], med[c], tseq / med[c], 100 * (med[c] / best - 1), stl[c], ok[c] ? "bit-identical" : "DIFFERS");
    }
    printf("  cost*cap+steal final per-thread speeds (pull loop):"); for (int k = 0; k < hs_T; k++) printf(" %.3f", sp[k]); printf("\n");
    return 0;
}
