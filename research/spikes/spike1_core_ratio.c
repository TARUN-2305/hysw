// EXPLORATORY SPIKE 1 (Phase 1, disposable) -- not project code.
// Question: how much faster is a P-core than an E-core on this machine,
// and does that ratio depend on the kind of workload?
// Method: pin the process to one logical CPU, time four single-threaded kernels.
// Build: gcc -O2 -march=native spike1_core_ratio.c -o spike1 -lm
// Run:   ./spike1 <cpu_id>
#define _GNU_SOURCE
#include <math.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static double now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}

// K1: scalar, branchy, compute-bound (Mandelbrot escape iterations)
static double k_mandel(void) {
    long total = 0;
    for (int py = 0; py < 300; py++)
        for (int px = 0; px < 300; px++) {
            double cr = -2.0 + 3.0 * px / 300, ci = -1.5 + 3.0 * py / 300;
            double zr = 0, zi = 0;
            int it = 0;
            while (zr * zr + zi * zi < 4.0 && it < 2000) {
                double t = zr * zr - zi * zi + cr;
                zi = 2 * zr * zi + ci;
                zr = t;
                it++;
            }
            total += it;
        }
    return (double)total;
}

// K2: vectorisable FP on data that fits in L1 (SIMD throughput)
static double k_simd_incache(void) {
    enum { N = 2048 };
    static float a[N], b[N];
    for (int i = 0; i < N; i++) { a[i] = i * 0.001f; b[i] = 1.0f; }
    for (int rep = 0; rep < 400000; rep++)
        for (int i = 0; i < N; i++) b[i] = b[i] * 0.999f + a[i] * 0.001f;
    return b[7];
}

// K3: memory-bandwidth-bound (STREAM-like triad, arrays >> L3)
static double *A, *B, *C;
enum { MN = 16 * 1000 * 1000 };
static double k_triad(void) {
    for (int rep = 0; rep < 8; rep++)
        for (long i = 0; i < MN; i++) A[i] = B[i] + 3.0 * C[i];
    return A[MN / 2];
}

// K4: latency-bound random access (pointer-chase style gather, like sparse codes)
static unsigned *IDX;
enum { GN = 8 * 1000 * 1000 };
static double k_gather(void) {
    double s = 0;
    for (int rep = 0; rep < 3; rep++)
        for (long i = 0; i < GN; i++) s += B[IDX[i]];
    return s;
}

int main(int argc, char **argv) {
    int cpu = argc > 1 ? atoi(argv[1]) : 0;
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
    if (sched_setaffinity(0, sizeof set, &set)) { perror("affinity"); return 1; }

    A = malloc(sizeof(double) * MN); B = malloc(sizeof(double) * MN); C = malloc(sizeof(double) * MN);
    IDX = malloc(sizeof(unsigned) * GN);
    for (long i = 0; i < MN; i++) { A[i] = 0; B[i] = 1.0; C[i] = 2.0; }
    unsigned x = 12345;
    for (long i = 0; i < GN; i++) { x = x * 1664525u + 1013904223u; IDX[i] = x % MN; }

    double (*k[4])(void) = {k_mandel, k_simd_incache, k_triad, k_gather};
    const char *nm[4] = {"mandel_scalar", "simd_incache", "triad_membound", "gather_random"};
    volatile double sink = 0;
    for (int j = 0; j < 4; j++) {
        double best = 1e30;
        for (int r = 0; r < 5; r++) {           // 5 reps, report minimum
            double t = now();
            sink += k[j]();
            t = now() - t;
            if (t < best) best = t;
        }
        printf("cpu=%d kernel=%s best_s=%.4f\n", cpu, nm[j], best);
    }
    return sink < 0;
}
