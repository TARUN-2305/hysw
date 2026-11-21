// EXPLORATORY SPIKE 4 (Phase 1, disposable) -- not project code.
// Question (the decisive one): can ONE policy stay close to the best stock schedule on
// every loop type?  Policy tested ("WS"): each thread owns a contiguous block sized by
// its measured speed (weights fed back from the previous invocation of the same loop);
// a thread that runs out steals the back half of the fullest remaining block.
//   -> weights handle unequal cores, contiguous blocks keep cache locality,
//      stealing handles irregular iteration costs and wrong weights.
// Build: gcc -O2 -march=native -fopenmp spike4_weighted_steal.c -o spike4 -lm
// Run:   ./spike4 <uniform|mandel|jacobi512|jacobi1024> [cpu list] [rounds]
#define _GNU_SOURCE
#include <math.h>
#include <omp.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXT 64
static int cpus[MAXT], T, kernel, n;
static long N;
static double *U, *V, *gu, *gv;           // jacobi grids (gu/gv = current in/out)
static double rowsum[4096];               // per-row outputs for uniform/mandel (checksum)

static void body(long i) {
    if (kernel == 0) {                    // uniform compute-bound, ~0.9 ms per iteration
        double acc = 0;
        for (int r = 0; r < 500; r++) {
            double x = 1.0 + ((i + r) & 7) * 1e-3;
            for (int k = 0; k < 2000; k++) x = x * 1.0000001 + 1e-9;
            acc += x;
        }
        rowsum[i] = acc;
    } else if (kernel == 1) {             // irregular: Mandelbrot row
        long W = 3000, tot = 0;
        double ci = -1.25 + 2.5 * i / N;
        for (long px = 0; px < W; px++) {
            double cr = -2.0 + 3.0 * px / W, zr = 0, zi = 0;
            int it = 0;
            while (zr * zr + zi * zi < 4.0 && it < 1000) {
                double t = zr * zr - zi * zi + cr; zi = 2 * zr * zi + ci; zr = t; it++;
            }
            tot += it;
        }
        rowsum[i] = (double)tot;
    } else {                              // locality-sensitive: Jacobi row i+1
        long r = i + 1;
        const double *restrict u = gu; double *restrict v = gv;
        for (int j = 1; j < n - 1; j++)
            v[r * n + j] = 0.25 * (u[(r - 1) * n + j] + u[(r + 1) * n + j] + u[r * n + j - 1] + u[r * n + j + 1]);
    }
}

static void reset(void) {
    if (kernel < 2) { memset(rowsum, 0, sizeof rowsum); return; }
    for (long k = 0; k < (long)n * n; k++) { U[k] = 0; V[k] = 0; }
    for (int j = 0; j < n; j++) { U[j] = 100; V[j] = 100; }
    gu = U; gv = V;
}
static void end_sweep(void) { if (kernel >= 2) { double *t = gu; gu = gv; gv = t; } }
static double checksum(void) {
    double s = 0;
    if (kernel < 2) { for (long i = 0; i < N; i++) s += rowsum[i]; return s; }
    for (long k = 0; k < (long)n * n; k++) s += U[k] + V[k];
    return s;
}

// ---------------- weighted + stealing parallel-for ----------------
typedef struct { omp_lock_t lk; long lo, hi; long done; char pad[64]; } range_t;
static range_t R[MAXT];
static double W[MAXT];                    // persistent weights (feedback across invocations)
static long steals;

static void ws_sweeps(int sweeps, double *per_sweep) {
#pragma omp parallel num_threads(T)
    {
        int t = omp_get_thread_num();
        for (int s = 0; s < sweeps; s++) {
#pragma omp single
            {
                per_sweep[s] = omp_get_wtime();
                double acc = 0; long lo = 0;
                for (int k = 0; k < T; k++) {
                    acc += W[k];
                    long hi = k == T - 1 ? N : (long)(N * acc);
                    R[k].lo = lo; R[k].hi = hi; R[k].done = 0; lo = hi;
                }
            }                             // implicit barrier
            range_t *my = &R[t];
            long done = 0;
            for (;;) {
                long b = 0, c = 0;
                omp_set_lock(&my->lk);
                long rem = my->hi - my->lo;
                if (rem > 0) { c = rem / 8 > 0 ? rem / 8 : 1; b = my->lo; my->lo += c; }
                omp_unset_lock(&my->lk);
                if (c) { for (long i = b; i < b + c; i++) body(i); done += c; continue; }
                int v = -1; long best = 0;                  // pick fullest victim (unlocked peek)
                for (int k = 0; k < T; k++) { long r2 = R[k].hi - R[k].lo; if (r2 > best) { best = r2; v = k; } }
                if (v < 0) break;
                long slo = 0, take = 0;
                omp_set_lock(&R[v].lk);
                rem = R[v].hi - R[v].lo;
                if (rem > 0) { take = (rem + 1) / 2; R[v].hi -= take; slo = R[v].hi; }
                omp_unset_lock(&R[v].lk);
                if (take) {
                    omp_set_lock(&my->lk); my->lo = slo; my->hi = slo + take; omp_unset_lock(&my->lk);
#pragma omp atomic
                    steals++;
                }
            }
            my->done = done;
#pragma omp barrier
#pragma omp single
            {
                double tot = 0;                             // feedback: share of work each thread really did
                for (int k = 0; k < T; k++) { W[k] = 0.5 * W[k] + 0.5 * (double)R[k].done / N; tot += W[k]; }
                for (int k = 0; k < T; k++) W[k] /= tot;
                end_sweep();
                per_sweep[s] = omp_get_wtime() - per_sweep[s];
            }
        }
    }
}

static void stock_sweeps(omp_sched_t kind, int chunk, int sweeps, double *per_sweep) {
    omp_set_schedule(kind, chunk);
#pragma omp parallel num_threads(T)
    for (int s = 0; s < sweeps; s++) {
#pragma omp single
        per_sweep[s] = omp_get_wtime();
#pragma omp for schedule(runtime)
        for (long i = 0; i < N; i++) body(i);
#pragma omp single
        { end_sweep(); per_sweep[s] = omp_get_wtime() - per_sweep[s]; }
    }
}

static int cmp(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

int main(int argc, char **argv) {
    const char *kn = argc > 1 ? argv[1] : "uniform";
    int sweeps;
    if (!strcmp(kn, "uniform")) { kernel = 0; N = 480; sweeps = 12; }
    else if (!strcmp(kn, "mandel")) { kernel = 1; N = 3000; sweeps = 8; }
    else if (!strcmp(kn, "jacobi512")) { kernel = 2; n = 512; N = n - 2; sweeps = 2000; }
    else { kernel = 3; n = 1024; N = n - 2; sweeps = 500; }
    char *list = strdup(argc > 2 ? argv[2] : "0,1,2,3,4,5,6,7,8,9,10,11");
    for (char *p = strtok(list, ","); p; p = strtok(NULL, ",")) cpus[T++] = atoi(p);
    if (kernel >= 2) { U = malloc(8L * n * n); V = malloc(8L * n * n); }
    for (int k = 0; k < T; k++) omp_init_lock(&R[k].lk);
#pragma omp parallel num_threads(T)
    {
        cpu_set_t s; CPU_ZERO(&s); CPU_SET(cpus[omp_get_thread_num()], &s);
        sched_setaffinity(0, sizeof s, &s);
    }
    double *ps = malloc(sizeof(double) * sweeps);

    // sequential reference (one sweep set on first listed CPU), also gives the checksum
    reset();
    double t0 = omp_get_wtime();
    for (int s = 0; s < sweeps; s++) { for (long i = 0; i < N; i++) body(i); end_sweep(); }
    double tseq = (omp_get_wtime() - t0) / sweeps, ref = checksum();
    printf("kernel=%s N=%ld sweeps=%d threads=%d seq_per_sweep_on_cpu%d=%.6fs\n", kn, N, sweeps, T, cpus[0], tseq);

    int ch = N / (8L * T) > 0 ? (int)(N / (8L * T)) : 1;
    const char *nm[] = {"static", "dynamic,1", "dynamic,N/(8T)", "guided", "WS(weighted+steal)"};
    // Fair ordering: the laptop power-throttles after ~20 s of all-core load (found in run 1/2),
    // so configs are INTERLEAVED: each round runs every config once, and the starting config
    // rotates per round. Optional argv[3] = number of rounds (default 7).
    enum { NC = 5, MAXR = 32 };
    int rounds = argc > 3 ? atoi(argv[3]) : 7;
    if (rounds > MAXR) rounds = MAXR;
    static double tot[NC][MAXR];
    double first[NC] = {0}, stl = 0;
    int ok[NC] = {1, 1, 1, 1, 1};
    for (int r = 0; r < rounds; r++)
        for (int oc = 0; oc < NC; oc++) {
            int c = (oc + r) % NC;
            reset();
            if (c == 4) { for (int k = 0; k < T; k++) W[k] = 1.0 / T; steals = 0; }   // cold start each time
            double a = omp_get_wtime();
            if (c == 0) stock_sweeps(omp_sched_static, 0, sweeps, ps);
            else if (c == 1) stock_sweeps(omp_sched_dynamic, 1, sweeps, ps);
            else if (c == 2) stock_sweeps(omp_sched_dynamic, ch, sweeps, ps);
            else if (c == 3) stock_sweeps(omp_sched_guided, 0, sweeps, ps);
            else ws_sweeps(sweeps, ps);
            tot[c][r] = (omp_get_wtime() - a) / sweeps;
            first[c] = ps[0];
            if (c == 4) stl = (double)steals / sweeps;
            if (checksum() != ref) ok[c] = 0;
        }
    double med[NC], best = 1e30;
    for (int c = 0; c < NC; c++) {
        printf("  raw %-19s", nm[c]);
        for (int r = 0; r < rounds; r++) printf(" %.6f", tot[c][r]);
        printf("\n");
        qsort(tot[c], rounds, sizeof(double), cmp);
        med[c] = tot[c][rounds / 2];
        if (med[c] < best) best = med[c];
    }
    for (int c = 0; c < NC; c++) {
        printf("%-19s per_sweep median=%.6fs min=%.6fs max=%.6fs speedup_vs_seqP=%.2f regret_vs_best=%+.1f%% checksum=%s",
               nm[c], med[c], tot[c][0], tot[c][rounds - 1], tseq / med[c], 100 * (med[c] / best - 1), ok[c] ? "exact" : "DIFF");
        if (c == 4) {
            printf(" first_sweep(cold)=%.6fs steals_per_sweep=%.1f\n  final weights:", first[c], stl);
            for (int k = 0; k < T; k++) printf(" %.3f", W[k]);
        }
        printf("\n");
    }
    return 0;
}
