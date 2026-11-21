// EXPLORATORY SPIKE 2 (Phase 1, disposable) -- not project code.
// Question: on this hybrid CPU, does schedule(static) leave fast cores idle, do the
// stock dynamic/guided schedules fix it, and can a capacity-weighted static split
// (weights measured online from a short sampling phase) match or beat them?
// Build: gcc -O2 -march=native -fopenmp spike2_sched.c -o spike2 -lm
// Run:   ./spike2 <kernel: uniform|fine|triad|mandel> <cpu list e.g. 0,1,2,...>
#define _GNU_SOURCE
#include <math.h>
#include <omp.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXT 64
static int cpus[MAXT], T;
static double busy[MAXT];          // per-thread time spent inside the loop
static double *A, *B, *C;
static long N;
static int kernel;                 // 0 uniform, 1 fine, 2 triad, 3 mandel

// One loop iteration of the chosen kernel. Returns a value to be summed (checksum).
static inline double body(long i) {
    switch (kernel) {
    case 0: {                      // uniform, compute-bound: fixed inner work
        double x = 1.0 + (i & 7) * 1e-3;
        for (int k = 0; k < 2000; k++) x = x * 1.0000001 + 1e-9;
        return x;
    }
    case 1:                        // fine-grained: a few ns per iteration
        return sqrt((double)i) * 1e-9;
    case 2: {                      // memory-bound: one triad block of 4096 elements
        long b = i * 4096;
        for (long j = b; j < b + 4096; j++) A[j] = B[j] + 3.0 * C[j];
        return A[b];
    }
    default: {                     // irregular: one Mandelbrot row (cost varies by row)
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
        return (double)tot;
    }
    }
}

static void pin_all(void) {
#pragma omp parallel num_threads(T)
    {
        cpu_set_t s; CPU_ZERO(&s); CPU_SET(cpus[omp_get_thread_num()], &s);
        sched_setaffinity(0, sizeof s, &s);
    }
}

// Stock OpenMP schedule chosen at run time.
static double run_stock(omp_sched_t kind, int chunk, double *wall) {
    double sum = 0;
    omp_set_schedule(kind, chunk);
    double t0 = omp_get_wtime();
#pragma omp parallel num_threads(T) reduction(+ : sum)
    {
        double s = omp_get_wtime();
#pragma omp for schedule(runtime) nowait
        for (long i = 0; i < N; i++) sum += body(i);
        busy[omp_get_thread_num()] = omp_get_wtime() - s;
    }
    *wall = omp_get_wtime() - t0;
    return sum;
}

// Capacity-weighted static: (1) every thread runs an equal small sample of the real
// loop and is timed; (2) the remaining iterations are split into contiguous blocks
// whose sizes are proportional to each thread's measured speed.
static double w_last[MAXT];
static double run_weighted(double sample_frac, double *wall) {
    double sum = 0, rate[MAXT];
    long sample = (long)(N * sample_frac) / T;
    if (sample < 1) sample = 1;
    long start2 = sample * T, lo[MAXT + 1];
    double t0 = omp_get_wtime();
#pragma omp parallel num_threads(T) reduction(+ : sum)
    {
        int t = omp_get_thread_num();
        double s = omp_get_wtime();
        for (long i = t * sample; i < (t + 1) * sample; i++) sum += body(i);
        rate[t] = sample / (omp_get_wtime() - s);
#pragma omp barrier
#pragma omp single
        {
            double tot = 0;
            for (int k = 0; k < T; k++) tot += rate[k];
            double acc = 0;
            lo[0] = start2;
            for (int k = 0; k < T; k++) {
                acc += rate[k] / tot;
                w_last[k] = rate[k] / tot;
                lo[k + 1] = start2 + (long)((N - start2) * acc);
            }
            lo[T] = N;
        }                          // implicit barrier
        for (long i = lo[t]; i < lo[t + 1]; i++) sum += body(i);
        busy[t] = omp_get_wtime() - s;
    }
    *wall = omp_get_wtime() - t0;
    return sum;
}

static int cmp(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

int main(int argc, char **argv) {
    const char *kn = argc > 1 ? argv[1] : "uniform";
    kernel = !strcmp(kn, "uniform") ? 0 : !strcmp(kn, "fine") ? 1 : !strcmp(kn, "triad") ? 2 : 3;
    char *list = strdup(argc > 2 ? argv[2] : "0,1,2,3,4,5,6,7,8,9,10,11");
    for (char *p = strtok(list, ","); p; p = strtok(NULL, ",")) cpus[T++] = atoi(p);
    long Ns[4] = {400000, 200000000, 6000, 3000};
    N = Ns[kernel];
    if (kernel == 2) {
        long n = N * 4096;
        A = malloc(n * 8); B = malloc(n * 8); C = malloc(n * 8);
        for (long i = 0; i < n; i++) { A[i] = 0; B[i] = 1; C[i] = 2; }
    }
    pin_all();

    // sequential reference on the first listed CPU (a P-core when list starts with 0)
    double ref = 0, tseq = 1e30;
    for (int r = 0; r < 3; r++) {
        double s = 0, t0 = omp_get_wtime();
        for (long i = 0; i < N; i++) s += body(i);
        double t = omp_get_wtime() - t0;
        if (t < tseq) tseq = t;
        ref = s;
    }
    printf("kernel=%s N=%ld threads=%d seq_on_cpu%d=%.4fs\n", kn, N, T, cpus[0], tseq);

    struct { const char *name; int kind, chunk; } cfg[] = {
        {"static", omp_sched_static, 0},
        {"dynamic,1", omp_sched_dynamic, 1},
        {"dynamic,N/(64T)", omp_sched_dynamic, -1},
        {"guided", omp_sched_guided, 0},
        {"weighted(2%sample)", -1, 0},
    };
    enum { REPS = 7 };
    for (int c = 0; c < 5; c++) {
        double walls[REPS], sum = 0, bmin = 0, bmax = 0;
        if (kernel == 1 && c == 1) { printf("%-20s skipped (2e8 single-iteration dispatches)\n", cfg[c].name); continue; }
        for (int r = 0; r < REPS; r++) {
            int chunk = cfg[c].chunk < 0 ? (int)(N / (64L * T) > 0 ? N / (64L * T) : 1) : cfg[c].chunk;
            sum = cfg[c].kind < 0 ? run_weighted(0.02, &walls[r]) : run_stock(cfg[c].kind, chunk, &walls[r]);
            if (r == REPS - 1) {
                bmin = 1e30; bmax = 0;
                for (int t = 0; t < T; t++) { if (busy[t] < bmin) bmin = busy[t]; if (busy[t] > bmax) bmax = busy[t]; }
            }
        }
        qsort(walls, REPS, sizeof(double), cmp);
        double med = walls[REPS / 2];
        // checksum: relative error vs sequential (summation order differs => tiny FP drift expected)
        double rel = fabs(sum - ref) / fabs(ref);
        printf("%-20s median=%.4fs min=%.4fs max=%.4fs speedup_vs_seqP=%.2f idle_gap=%.0f%% relerr=%.1e\n",
               cfg[c].name, med, walls[0], walls[REPS - 1], tseq / med, 100 * (bmax - bmin) / bmax, rel);
        if (cfg[c].kind < 0) {
            printf("  measured weights:");
            for (int t = 0; t < T; t++) printf(" cpu%d=%.3f", cpus[t], w_last[t]);
            printf("\n");
        }
    }
    return 0;
}
