// EXPLORATORY SPIKE 3 (Phase 1, disposable) -- not project code.
// Question: for a loop that is executed many times over the same data (time-stepping
// stencil), can a feedback-weighted static partition (block sizes re-derived from the
// previous sweep's per-thread timings; same thread keeps roughly the same rows, so its
// cache stays warm) beat the stock schedules, including dynamic?
// Build: gcc -O2 -march=native -fopenmp [-DHEAVY] spike3_feedback_jacobi.c -o spike3 -lm
// Run:   ./spike3 <n> <sweeps> <cpu list>
#define _GNU_SOURCE
#include <math.h>
#include <omp.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXT 64
static int cpus[MAXT], T, n, sweeps;
static double *U, *V;

static inline void row(const double *restrict u, double *restrict v, int i) {
    for (int j = 1; j < n - 1; j++)
    {
        double x = 0.25 * (u[(i - 1) * n + j] + u[(i + 1) * n + j] + u[i * n + j - 1] + u[i * n + j + 1]);
#ifdef HEAVY   // reaction-diffusion variant: adds an Arrhenius-style exp() source term per cell (compute-bound)
        x += 1e-3 * exp(-1.0 / (fabs(x) + 0.1));
#endif
        v[i * n + j] = x;
    }
}

static void init(void) {
    for (long k = 0; k < (long)n * n; k++) { U[k] = 0; V[k] = 0; }
    for (int j = 0; j < n; j++) { U[j] = 100; V[j] = 100; }       // hot top edge
}

static double checksum(const double *g) {
    double s = 0;
    for (long k = 0; k < (long)n * n; k++) s += g[k];
    return s;
}

static double run_seq(void) {
    init();
    double *u = U, *v = V, t0 = omp_get_wtime();
    for (int s = 0; s < sweeps; s++) {
        for (int i = 1; i < n - 1; i++) row(u, v, i);
        double *t = u; u = v; v = t;
    }
    return omp_get_wtime() - t0;
}

static double run_stock(omp_sched_t kind, int chunk) {
    init();
    omp_set_schedule(kind, chunk);
    double *u = U, *v = V, t0 = omp_get_wtime();
#pragma omp parallel num_threads(T)
    for (int s = 0; s < sweeps; s++) {
#pragma omp for schedule(runtime)
        for (int i = 1; i < n - 1; i++) row(u, v, i);
#pragma omp single
        { double *t = u; u = v; v = t; }
    }
    return omp_get_wtime() - t0;
}

// Feedback-weighted static: sweep s uses row blocks sized by the speeds measured in sweep s-1.
static double final_w[MAXT];
static double run_feedback(void) {
    init();
    double w[MAXT], tm[MAXT];
    int lo[MAXT + 1];
    for (int k = 0; k < T; k++) w[k] = 1.0 / T;
    double *u = U, *v = V, t0 = omp_get_wtime();
#pragma omp parallel num_threads(T)
    {
        int t = omp_get_thread_num();
        for (int s = 0; s < sweeps; s++) {
#pragma omp single
            {
                double acc = 0;
                lo[0] = 1;
                for (int k = 0; k < T; k++) { acc += w[k]; lo[k + 1] = 1 + (int)((n - 2) * acc); }
                lo[T] = n - 1;
            }
            double a = omp_get_wtime();
            for (int i = lo[t]; i < lo[t + 1]; i++) row(u, v, i);
            tm[t] = omp_get_wtime() - a;
#pragma omp barrier
#pragma omp single
            {
                double rate[MAXT], tot = 0;
                for (int k = 0; k < T; k++) { rate[k] = (lo[k + 1] - lo[k]) / (tm[k] > 1e-9 ? tm[k] : 1e-9); tot += rate[k]; }
                double tot2 = 0;
                for (int k = 0; k < T; k++) { w[k] = 0.5 * w[k] + 0.5 * rate[k] / tot; tot2 += w[k]; }   // smoothing
                for (int k = 0; k < T; k++) w[k] /= tot2;
                double *x = u; u = v; v = x;
            }
        }
    }
    for (int k = 0; k < T; k++) final_w[k] = w[k];
    return omp_get_wtime() - t0;
}

static int cmp(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

int main(int argc, char **argv) {
    n = argc > 1 ? atoi(argv[1]) : 512;
    sweeps = argc > 2 ? atoi(argv[2]) : 500;
    char *list = strdup(argc > 3 ? argv[3] : "0,1,2,3,4,5,6,7,8,9,10,11");
    for (char *p = strtok(list, ","); p; p = strtok(NULL, ",")) cpus[T++] = atoi(p);
    U = malloc(sizeof(double) * n * n); V = malloc(sizeof(double) * n * n);
#pragma omp parallel num_threads(T)
    {
        cpu_set_t s; CPU_ZERO(&s); CPU_SET(cpus[omp_get_thread_num()], &s);
        sched_setaffinity(0, sizeof s, &s);
    }
    enum { REPS = 5 };
    double tseq = 1e30, ref = 0;
    for (int r = 0; r < 3; r++) { double t = run_seq(); if (t < tseq) tseq = t; }
    ref = checksum(U) + checksum(V);
    printf("jacobi n=%d sweeps=%d grid=%.1fMB(x2) threads=%d seq_on_cpu%d=%.4fs\n", n, sweeps,
           n * (double)n * 8 / 1e6, T, cpus[0], tseq);
    const char *nm[] = {"static", "dynamic,1row", "dynamic,rows/(8T)", "guided", "feedback-weighted"};
    for (int c = 0; c < 5; c++) {
        double w[REPS], cs = 0;
        for (int r = 0; r < REPS; r++) {
            int ch = (n - 2) / (8 * T) > 0 ? (n - 2) / (8 * T) : 1;
            w[r] = c == 0 ? run_stock(omp_sched_static, 0) : c == 1 ? run_stock(omp_sched_dynamic, 1)
                 : c == 2 ? run_stock(omp_sched_dynamic, ch) : c == 3 ? run_stock(omp_sched_guided, 0) : run_feedback();
            cs = checksum(U) + checksum(V);
        }
        qsort(w, REPS, sizeof(double), cmp);
        printf("%-18s median=%.4fs min=%.4fs max=%.4fs speedup_vs_seqP=%.2f checksum_match=%s\n", nm[c], w[REPS / 2],
               w[0], w[REPS - 1], tseq / w[REPS / 2], cs == ref ? "exact" : "DIFF");
        if (c == 4) {
            printf("  final weights:");
            for (int k = 0; k < T; k++) printf(" cpu%d=%.3f", cpus[k], final_w[k]);
            printf("\n");
        }
    }
    return 0;
}
