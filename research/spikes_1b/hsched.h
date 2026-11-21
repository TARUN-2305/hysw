// EXPLORATORY (Phase 1B, disposable) -- shared scheduling harness for the application spikes.
// Not project code. One function, hs_for(), runs a loop of n iterations under one of several
// schedules so that every application spike compares exactly the same set of policies.
//
// Policies
//   static, dynamic,1, dynamic,n/(8T), guided  -> stock GCC OpenMP schedules
//   steal        equal contiguous blocks + work stealing                 (no weights)
//   share+steal  blocks sized by each thread's share of iterations last time + stealing
//                (the Phase 1 "WS" prototype)
//   cost+steal   blocks of equal *application cost* (cost hint per iteration) + stealing
//                (cost-aware, but assumes all cores are equally fast)
//   cost*cap+steal  blocks whose cost is proportional to each thread's measured speed
//                (cost-aware AND capacity-aware) + stealing
//   cost*cap     same, without stealing (shows what the model alone achieves)
#ifndef HSCHED_H
#define HSCHED_H
#define _GNU_SOURCE
#include <omp.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HS_MAXT 64
enum { HS_STATIC, HS_DYN1, HS_DYNC, HS_GUIDED, HS_STEAL, HS_SHARE, HS_COSTEQ, HS_COSTCAP, HS_COSTCAP_NS, HS_NSCHED };
static const char *hs_name[HS_NSCHED] = {"static", "dynamic,1", "dynamic,n/8T", "guided", "steal",
                                         "share+steal", "cost+steal", "cost*cap+steal", "cost*cap(no steal)"};

typedef void (*hs_body)(long lo, long hi, int tid, void *ctx);

typedef struct {                 // per-loop memory, kept between executions of the same loop
    double w[HS_MAXT];           // share+steal: fraction of iterations per thread
    double speed[HS_MAXT];       // cost*cap: relative speed per thread (sums to 1)
    int init;
    long steals, calls;
    double *ps;                  // prefix sums of the cost hint
    long ps_cap;
} hs_site;

typedef struct { omp_lock_t lk; long lo, hi; char pad[40]; } hs_range;
static hs_range hs_R[HS_MAXT];
static int hs_T, hs_cpu[HS_MAXT];

static void hs_init(const char *cpulist) {
    char *s = strdup(cpulist);
    for (char *p = strtok(s, ","); p; p = strtok(NULL, ",")) hs_cpu[hs_T++] = atoi(p);
    for (int k = 0; k < hs_T; k++) omp_init_lock(&hs_R[k].lk);
#pragma omp parallel num_threads(hs_T)
    {
        cpu_set_t c; CPU_ZERO(&c); CPU_SET(hs_cpu[omp_get_thread_num()], &c);
        sched_setaffinity(0, sizeof c, &c);
    }
}
static void hs_site_reset(hs_site *s) { s->init = 0; s->steals = 0; s->calls = 0; }

// first index m in [lo,hi] with ps[m] >= target
static long hs_find(const double *ps, long lo, long hi, double target) {
    while (lo < hi) { long m = (lo + hi) / 2; if (ps[m] < target) lo = m + 1; else hi = m; }
    return lo;
}

static void hs_for(int sched, hs_site *s, long n, hs_body body, void *ctx, const float *cost) {
    const int T = hs_T;
    if (n <= 0) return;
    if (sched <= HS_GUIDED) {
        long ch = n / (8L * T) > 0 ? n / (8L * T) : 1;
        omp_set_schedule(sched == HS_STATIC ? omp_sched_static : sched == HS_GUIDED ? omp_sched_guided : omp_sched_dynamic,
                         sched == HS_DYN1 ? 1 : sched == HS_DYNC ? (int)ch : 0);
#pragma omp parallel num_threads(T)
        {
            int tid = omp_get_thread_num();
#pragma omp for schedule(runtime)
            for (long i = 0; i < n; i++) body(i, i + 1, tid, ctx);
        }
        return;
    }
    if (!s->init) { for (int k = 0; k < T; k++) { s->w[k] = 1.0 / T; s->speed[k] = 1.0 / T; } s->init = 1; }
    int use_cost = sched >= HS_COSTEQ, steal = sched != HS_COSTCAP_NS;
    double *ps = NULL;
    if (use_cost) {                                  // prefix sums of the cost hint (serial, O(n))
        if (s->ps_cap < n + 1) { s->ps = realloc(s->ps, sizeof(double) * (n + 1)); s->ps_cap = n + 1; }
        ps = s->ps; ps[0] = 0;
        for (long i = 0; i < n; i++) ps[i + 1] = ps[i] + (cost ? cost[i] : 1.0) + 1e-9;
    }
    // ---- initial contiguous blocks ----
    double acc = 0; long lo = 0;
    for (int k = 0; k < T; k++) {
        double f = sched == HS_STEAL || sched == HS_COSTEQ ? 1.0 / T : sched == HS_SHARE ? s->w[k] : s->speed[k];
        acc += f;
        long hi = k == T - 1 ? n : use_cost ? hs_find(ps, lo, n, ps[n] * acc) : (long)(n * acc);
        if (hi < lo) hi = lo;
        if (hi > n) hi = n;
        hs_R[k].lo = lo; hs_R[k].hi = hi; lo = hi;
    }
    double busy[HS_MAXT] = {0}, units[HS_MAXT] = {0};
    long iters[HS_MAXT] = {0}, nsteal = 0;
#pragma omp parallel num_threads(T) reduction(+ : nsteal)
    {
        int t = omp_get_thread_num();
        hs_range *my = &hs_R[t];
        double b_acc = 0, u_acc = 0; long i_acc = 0;
        for (;;) {
            long b = 0, c = 0;
            omp_set_lock(&my->lk);
            long rem = my->hi - my->lo;
            if (rem > 0) {                           // owner takes ~1/8 of what is left (by cost if known)
                b = my->lo;
                long e = use_cost ? hs_find(ps, b + 1, my->hi, ps[b] + (ps[my->hi] - ps[b]) / 8) : b + (rem / 8 > 0 ? rem / 8 : 1);
                if (e <= b) e = b + 1;
                c = e - b; my->lo = e;
            }
            omp_unset_lock(&my->lk);
            if (c) {
                double t0 = omp_get_wtime();
                body(b, b + c, t, ctx);
                b_acc += omp_get_wtime() - t0;
                i_acc += c; u_acc += use_cost ? ps[b + c] - ps[b] : c;
                continue;
            }
            if (!steal) break;
            int v = -1; double best = 0;             // fullest victim (unlocked peek)
            for (int k = 0; k < T; k++) {
                long l2 = hs_R[k].lo, h2 = hs_R[k].hi;
                if (h2 <= l2) continue;
                double r2 = use_cost ? ps[h2] - ps[l2] : (double)(h2 - l2);
                if (r2 > best) { best = r2; v = k; }
            }
            if (v < 0) break;
            long slo = 0, shi = 0;
            omp_set_lock(&hs_R[v].lk);
            long l2 = hs_R[v].lo, h2 = hs_R[v].hi;
            if (h2 > l2) {                           // take the back half (by cost if known)
                long m = use_cost ? hs_find(ps, l2, h2, (ps[l2] + ps[h2]) / 2) : l2 + (h2 - l2) / 2;
                if (m >= h2) m = h2 - 1;
                slo = m; shi = h2; hs_R[v].hi = m;
            }
            omp_unset_lock(&hs_R[v].lk);
            if (shi > slo) {
                omp_set_lock(&my->lk); my->lo = slo; my->hi = shi; omp_unset_lock(&my->lk);
                nsteal++;
            }
        }
        busy[t] = b_acc; units[t] = u_acc; iters[t] = i_acc;
    }
    s->steals += nsteal; s->calls++;
    // ---- feedback for the next execution of this loop ----
    if (sched == HS_SHARE) {
        double tot = 0;
        for (int k = 0; k < T; k++) { s->w[k] = 0.5 * s->w[k] + 0.5 * (double)iters[k] / n; tot += s->w[k]; }
        for (int k = 0; k < T; k++) s->w[k] /= tot;
    } else if (sched == HS_COSTCAP || sched == HS_COSTCAP_NS) {
        double rate[HS_MAXT], rt = 0; int ok = 1;
        for (int k = 0; k < T; k++) {                // speed = cost units processed per second of busy time
            if (busy[k] < 1e-7 || units[k] <= 0) { ok = 0; break; }
            rate[k] = units[k] / busy[k]; rt += rate[k];
        }
        if (ok) {
            double tot = 0;
            for (int k = 0; k < T; k++) { s->speed[k] = 0.7 * s->speed[k] + 0.3 * rate[k] / rt; tot += s->speed[k]; }
            for (int k = 0; k < T; k++) s->speed[k] /= tot;
        }
    }
}

// FNV-1a over raw bytes: used to prove that parallel output is bit-identical to sequential output
static unsigned long long hs_hash(const void *p, size_t nbytes) {
    const unsigned char *b = p; unsigned long long h = 1469598103934665603ULL;
    for (size_t i = 0; i < nbytes; i++) { h ^= b[i]; h *= 1099511628211ULL; }
    return h;
}
static int hs_cmpd(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

// ---- fair-ordering helpers (added after run 1 exposed a carry-over effect) ----
// A config that leaves cores idle (e.g. static) lets the CPU recover power budget, so whatever
// runs NEXT is faster. Rotating the start of a fixed cyclic order does not remove that, because
// each config always follows the same neighbour. Fix: (a) a fresh random order every round,
// (b) the same all-core "conditioning" load before every measured run.
static void hs_condition(double seconds) {
#pragma omp parallel num_threads(hs_T)
    {
        double t0 = omp_get_wtime(), x = 1.0;
        while (omp_get_wtime() - t0 < seconds) for (int k = 0; k < 20000; k++) x = x * 1.0000001 + 1e-9;
        if (x < 0) abort();
    }
}
static int hs_pick(const int *list, int nl, int round, int o) {
    static int perm[HS_NSCHED]; static int have = -1;
    if (have != round) {
        unsigned st = 2463534242u ^ (unsigned)(round * 7919 + 17);
        for (int k = 0; k < nl; k++) perm[k] = list[k];
        for (int k = nl - 1; k > 0; k--) { st = st * 1664525u + 1013904223u; int j = (st >> 8) % (k + 1); int t = perm[k]; perm[k] = perm[j]; perm[j] = t; }
        have = round;
    }
    return perm[o];
}
#endif
