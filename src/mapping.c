// Batch-to-thread mapping policies behind one loop interface: map_for(policy, site, n, body, ctx, cost)
// runs body(lo, hi, tid, ctx) over the iterations 0..n-1 on site->T threads.
// Adapted from the Phase 1B harness (research/spikes_1b/hsched.h).
//
//   static          OpenMP schedule(static): n/T contiguous iterations per thread
//   dynamic,1       OpenMP schedule(dynamic,1): threads take one iteration at a time from a shared counter
//   dynamic,chunk   schedule(dynamic, n/(8T))
//   guided          schedule(guided): chunks shrinking from n/T
//   steal           equal contiguous blocks; a thread that runs out steals half of the fullest block
//   cost+steal      blocks of equal total *cost* (cost hint, e.g. batch length) + stealing
//   costcap+steal   blocks whose cost is proportional to each thread's measured speed (P-cores get more)
//                   + stealing; speeds are learned from earlier executions of the same loop
// Iterations are batches in length order (longest first), so iteration cost falls steadily with
// the index -- which is exactly what makes plain `static` unbalanced.
#include "hysw.h"
#include <omp.h>
#include <sched.h>
#include <stdlib.h>
#include <string.h>

const char *map_names[MAP_NPOLICY] = {"static", "dynamic,1", "dynamic,chunk", "guided", "steal", "cost+steal", "costcap+steal"};

int map_parse(const char *name) {
    if (!strcmp(name, "dynamic")) return MAP_DYN1;
    for (int k = 0; k < MAP_NPOLICY; k++) if (!strcmp(name, map_names[k])) return k;
    return -1;
}

void map_site_init(LoopSite *s, int T, const int *cpus, int pin) {
    memset(s, 0, sizeof *s);
    s->T = T; s->pin = pin;
    for (int k = 0; k < T; k++) s->cpus[k] = cpus ? cpus[k] : k;
}

void map_site_free(LoopSite *s) { free(s->ps); s->ps = NULL; s->ps_cap = 0; }

static _Thread_local int pinned_to = -1;
static void pin_self(const LoopSite *s, int tid) {           // each OpenMP thread pins itself once
    if (!s->pin || pinned_to == s->cpus[tid]) return;
    cpu_set_t c; CPU_ZERO(&c); CPU_SET(s->cpus[tid], &c);
    if (sched_setaffinity(0, sizeof c, &c) == 0) pinned_to = s->cpus[tid];
}

typedef struct { _Alignas(64) omp_lock_t lk; long lo, hi; } Range;   // one cache line per thread
static Range R[HYSW_MAXCPU];
static int R_init;

static long find_cost(const double *ps, long lo, long hi, double target) {   // first m in [lo,hi] with ps[m] >= target
    while (lo < hi) { long m = (lo + hi) / 2; if (ps[m] < target) lo = m + 1; else hi = m; }
    return lo;
}

void map_for(int policy, LoopSite *s, long n, loop_body body, void *ctx, const float *cost) {
    const int T = s->T;
    if (n <= 0) return;
    if (policy <= MAP_GUIDED) {
        long ch = n / (8L * T) > 0 ? n / (8L * T) : 1;
        omp_set_schedule(policy == MAP_STATIC ? omp_sched_static : policy == MAP_GUIDED ? omp_sched_guided : omp_sched_dynamic,
                         policy == MAP_DYN1 ? 1 : policy == MAP_DYNC ? (int)ch : 0);
#pragma omp parallel num_threads(T)
        {
            int tid = omp_get_thread_num();
            pin_self(s, tid);
#pragma omp for schedule(runtime)
            for (long i = 0; i < n; i++) body(i, i + 1, tid, ctx);
        }
        return;
    }
    if (!R_init) { for (int k = 0; k < HYSW_MAXCPU; k++) omp_init_lock(&R[k].lk); R_init = 1; }
    if (!s->init) { for (int k = 0; k < T; k++) s->speed[k] = 1.0 / T; s->init = 1; }
    const int use_cost = policy >= MAP_COST_STEAL;
    double *ps = NULL;
    if (use_cost) {                                           // prefix sums of the cost hint
        if (s->ps_cap < n + 1) { s->ps = realloc(s->ps, sizeof(double) * (n + 1)); s->ps_cap = n + 1; }
        ps = s->ps; ps[0] = 0;
        for (long i = 0; i < n; i++) ps[i + 1] = ps[i] + (cost ? cost[i] : 1.0) + 1e-9;
    }
    double acc = 0; long lo = 0;                              // initial contiguous blocks
    for (int k = 0; k < T; k++) {
        acc += policy == MAP_COSTCAP_STEAL ? s->speed[k] : 1.0 / T;
        long hi = k == T - 1 ? n : use_cost ? find_cost(ps, lo, n, ps[n] * acc) : (long)(n * acc);
        if (hi < lo) hi = lo;
        if (hi > n) hi = n;
        R[k].lo = lo; R[k].hi = hi; lo = hi;
    }
    double busy[HYSW_MAXCPU] = {0}, units[HYSW_MAXCPU] = {0};
    long nsteal = 0;
#pragma omp parallel num_threads(T) reduction(+ : nsteal)
    {
        int t = omp_get_thread_num();
        pin_self(s, t);
        Range *my = &R[t];
        double b_acc = 0, u_acc = 0;
        for (;;) {
            long b = 0, c = 0;
            omp_set_lock(&my->lk);
            long rem = my->hi - my->lo;
            if (rem > 0) {                                    // owner takes ~1/8 of what is left (by cost if known)
                b = my->lo;
                long e = use_cost ? find_cost(ps, b + 1, my->hi, ps[b] + (ps[my->hi] - ps[b]) / 8) : b + (rem / 8 > 0 ? rem / 8 : 1);
                if (e <= b) e = b + 1;
                c = e - b; my->lo = e;
            }
            omp_unset_lock(&my->lk);
            if (c) {
                double t0 = omp_get_wtime();
                body(b, b + c, t, ctx);
                b_acc += omp_get_wtime() - t0;
                u_acc += use_cost ? ps[b + c] - ps[b] : c;
                continue;
            }
            int v = -1; double most = 0;                      // victim: the fullest block (unlocked peek)
            for (int k = 0; k < T; k++) {
                long l2 = R[k].lo, h2 = R[k].hi;
                if (h2 <= l2) continue;
                double r2 = use_cost ? ps[h2] - ps[l2] : (double)(h2 - l2);
                if (r2 > most) { most = r2; v = k; }
            }
            if (v < 0) break;
            long slo = 0, shi = 0;
            omp_set_lock(&R[v].lk);
            long l2 = R[v].lo, h2 = R[v].hi;
            if (h2 > l2) {                                    // take the back half (by cost if known)
                long mid = use_cost ? find_cost(ps, l2, h2, (ps[l2] + ps[h2]) / 2) : l2 + (h2 - l2) / 2;
                if (mid >= h2) mid = h2 - 1;
                slo = mid; shi = h2; R[v].hi = mid;
            }
            omp_unset_lock(&R[v].lk);
            if (shi > slo) {
                omp_set_lock(&my->lk); my->lo = slo; my->hi = shi; omp_unset_lock(&my->lk);
                nsteal++;
            }
        }
        busy[t] = b_acc; units[t] = u_acc;
    }
    s->steals += nsteal;
    if (policy == MAP_COSTCAP_STEAL) {                        // feedback: speed = cost units per busy second
        double rate[HYSW_MAXCPU], rt = 0;
        for (int k = 0; k < T; k++) {
            if (busy[k] < 1e-7 || units[k] <= 0) return;
            rate[k] = units[k] / busy[k]; rt += rate[k];
        }
        double tot = 0;
        for (int k = 0; k < T; k++) { s->speed[k] = 0.7 * s->speed[k] + 0.3 * rate[k] / rt; tot += s->speed[k]; }
        for (int k = 0; k < T; k++) s->speed[k] /= tot;
    }
}
