// EXPLORATORY SPIKE 1B-A (disposable) -- 2-D flood inundation on real terrain.
// Model: the "inertial" simplification of the shallow-water equations used by LISFLOOD-FP
//        (Bates, Horritt & Fewtrell 2010). Explicit, regular grid, two loops per time step:
//          A) flow across every cell face   (expensive where water is present, skipped where dry)
//          B) depth update from the flows   (cheap, memory-streaming)
// Terrain: SRTM 1-arc-second tile N12E077 (Bengaluru), cropped. Cell size taken as 30 m.
// THIS IS A WORKLOAD STUDY, NOT A VALIDATED FLOOD FORECAST (coarse surface model, no drains,
// closed boundaries, uniform roughness).
// Build: gcc -O2 -march=native -fopenmp -Wall flood.c -o flood -lm
// Run:   ./flood <tile.hgt> <row0> <col0> <size> <rain|breach> <rain_min> <total_min> <cpus> <rounds> [sched list e.g. 0,2,7]
#include "hsched.h"
#include <math.h>

#define G 9.81f
#define EPS 1e-3f          // depth threshold (1 mm): faces between cells shallower than this carry no flow
#define MANN 0.05f         // Manning roughness, uniform
#define DX 30.0f
#define ALPHA 0.7f
#define DTMAX 10.0f
#define SCANW 0.06f        // cost-hint weight of merely scanning one cell, relative to one wet cell

static int R, C;
static float *Z, *H, *QX, *QY;                 // terrain, depth, unit flows across east / south faces
static int *act, *actprev, *actnew;            // wet cells per row (now, previous step, being computed)
static char *procA;
static float *rowhmax, *costA, *costB;
static double *rowneg;
static float dt, rain_rate;                    // m/s
static int raining;

static inline float face(float z0, float h0, float z1, float h1, float q) {
    if (h0 <= EPS && h1 <= EPS) return 0.0f;                     // cheap path: both cells dry
    float e0 = z0 + h0, e1 = z1 + h1;
    float hf = (e0 > e1 ? e0 : e1) - (z0 > z1 ? z0 : z1);
    if (hf <= EPS) return 0.0f;
    float num = q - G * hf * dt * (e1 - e0) / DX;
    float den = 1.0f + G * dt * MANN * MANN * fabsf(q) / (hf * hf * cbrtf(hf));   // hf^(7/3)
    float qn = num / den;
    // flow limiter: one face may remove at most a quarter of the donor cell's water per step,
    // so four faces can never drain a cell below zero (keeps depths non-negative, conserves mass)
    float lim = (qn > 0 ? h0 : h1) * DX / (4.0f * dt);
    if (qn > lim) qn = lim; else if (qn < -lim) qn = -lim;
    return qn;
}

static void bodyA(long lo, long hi, int tid, void *ctx) {
    (void)tid; (void)ctx;
    for (long i = lo; i < hi; i++) {
        if (!procA[i]) continue;
        const float *z = Z + i * C, *h = H + i * C;
        float *qx = QX + i * C, *qy = QY + i * C;
        for (int j = 0; j < C - 1; j++) qx[j] = face(z[j], h[j], z[j + 1], h[j + 1], qx[j]);
        if (i < R - 1) {
            const float *zs = z + C, *hs = h + C;
            for (int j = 0; j < C; j++) qy[j] = face(z[j], h[j], zs[j], hs[j], qy[j]);
        }
    }
}

static void bodyB(long lo, long hi, int tid, void *ctx) {
    (void)tid; (void)ctx;
    const float k = dt / DX, add = raining ? rain_rate * dt : 0.0f;
    for (long i = lo; i < hi; i++) {
        if (!(raining || procA[i] || (i > 0 && procA[i - 1]))) { actnew[i] = act[i]; continue; }
        float *h = H + i * C;
        const float *qx = QX + i * C, *qy = QY + i * C, *qn = i > 0 ? qy - C : NULL;
        int a = 0; float mx = 0; double neg = 0;
        for (int j = 0; j < C; j++) {
            float in = (j > 0 ? qx[j - 1] : 0.0f) + (qn ? qn[j] : 0.0f);
            float v = h[j] + k * (in - qx[j] - qy[j]) + add;
            if (v < 0) { neg -= v; v = 0; }                       // over-drained cell: clamp, count the error
            h[j] = v;
            a += v > EPS;
            if (v > mx) mx = v;
        }
        actnew[i] = a; rowhmax[i] = mx; rowneg[i] += neg;
    }
}

typedef struct { double wall, wall_rain, wall_rec, vol, negvol, actfrac_rain, actfrac_rec; long steps, steps_rain; unsigned long long hash; } result;
static hs_site siteA, siteB;

// sched < 0 : plain sequential loops on the calling thread (the reference)
static result simulate(int sched, int breach, double rain_end, double t_end, double rain_mm_h) {
    result r = {0};
    memset(H, 0, sizeof(float) * R * C); memset(QX, 0, sizeof(float) * R * C); memset(QY, 0, sizeof(float) * R * C);
    memset(act, 0, sizeof(int) * R); memset(actprev, 0, sizeof(int) * R); memset(rowneg, 0, sizeof(double) * R);
    for (int i = 0; i < R; i++) rowhmax[i] = 0;
    hs_site_reset(&siteA); hs_site_reset(&siteB);
    double t = 0, inflow = 0, t0 = omp_get_wtime();
    dt = DTMAX;
    while (t < t_end) {
        double s0 = omp_get_wtime();
        raining = !breach && t < rain_end;
        rain_rate = (float)(rain_mm_h / 1000.0 / 3600.0);
        if (breach && t < rain_end) {                             // 9 inflow points (3x3 layout), 1000 m3/s each, 5x5-cell patches
            float add = 1000.0f * dt / (25 * DX * DX);
            for (int a = 1; a <= 3; a++)
                for (int i = a * R / 4 - 2; i <= a * R / 4 + 2; i++) {
                    for (int b = 1; b <= 3; b++)
                        for (int j = b * C / 4 - 2; j <= b * C / 4 + 2; j++) H[i * C + j] += add;
                    int n = 0; for (int j = 0; j < C; j++) n += H[i * C + j] > EPS;
                    act[i] = n;
                }
            inflow += 9 * 1000.0 * dt;
        }
        long nact = 0;
        for (int i = 0; i < R; i++) {                             // serial bookkeeping: O(rows)
            int nx = i < R - 1;
            procA[i] = act[i] || actprev[i] || (nx && (act[i + 1] || actprev[i + 1]));
            costA[i] = procA[i] ? 2 * C * SCANW + act[i] + (nx ? act[i + 1] : 0) : 0.0f;
            costB[i] = (raining || procA[i] || (i > 0 && procA[i - 1])) ? 1.0f : 0.0f;
            nact += act[i];
        }
        if (sched < 0) { bodyA(0, R, 0, NULL); bodyB(0, R, 0, NULL); }
        else { hs_for(sched, &siteA, R, bodyA, NULL, costA); hs_for(sched, &siteB, R, bodyB, NULL, costB); }
        float hmax = 0;
        for (int i = 0; i < R; i++) { if (rowhmax[i] > hmax) hmax = rowhmax[i]; }
        int *tmp = actprev; actprev = act; act = actnew; actnew = tmp;
        t += dt;
        if (raining) inflow += (double)rain_rate * dt * R * C * DX * DX;
        float d = hmax > 1e-6f ? ALPHA * DX / sqrtf(G * hmax) : DTMAX;
        dt = d < DTMAX ? d : DTMAX;
        double el = omp_get_wtime() - s0;
        r.steps++;
        if (t <= rain_end) { r.wall_rain += el; r.steps_rain++; r.actfrac_rain += (double)nact / (R * C); }
        else { r.wall_rec += el; r.actfrac_rec += (double)nact / (R * C); }
    }
    r.wall = omp_get_wtime() - t0;
    for (long k = 0; k < (long)R * C; k++) r.vol += (double)H[k] * DX * DX;
    for (int i = 0; i < R; i++) r.negvol += rowneg[i] * DX * DX;
    r.hash = hs_hash(H, sizeof(float) * R * C);
    if (r.steps_rain) r.actfrac_rain /= r.steps_rain;
    if (r.steps > r.steps_rain) r.actfrac_rec /= (r.steps - r.steps_rain);
    r.vol = r.vol / (inflow > 0 ? inflow : 1);                    // stored volume as a fraction of inflow
    return r;
}

int main(int argc, char **argv) {
    if (argc < 10) { fprintf(stderr, "usage: see header\n"); return 1; }
    int row0 = atoi(argv[2]), col0 = atoi(argv[3]); R = C = atoi(argv[4]);
    int breach = !strcmp(argv[5], "breach");
    double rain_end = atof(argv[6]) * 60, t_end = atof(argv[7]) * 60;
    hs_init(argv[8]);
    int rounds = atoi(argv[9]);
    int list[HS_NSCHED], nl = 0;
    if (argc > 10) { char *s = strdup(argv[10]); for (char *p = strtok(s, ","); p; p = strtok(NULL, ",")) list[nl++] = atoi(p); }
    else for (int k = 0; k < HS_NSCHED; k++) list[nl++] = k;

    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror("dem"); return 1; }
    Z = malloc(sizeof(float) * R * C); H = malloc(sizeof(float) * R * C); QX = malloc(sizeof(float) * R * C); QY = malloc(sizeof(float) * R * C);
    act = calloc(R, sizeof(int)); actprev = calloc(R, sizeof(int)); actnew = calloc(R, sizeof(int)); procA = calloc(R, 1);
    rowhmax = calloc(R, sizeof(float)); costA = calloc(R, sizeof(float)); costB = calloc(R, sizeof(float)); rowneg = calloc(R, sizeof(double));
    unsigned char *buf = malloc(2 * C);
    float zmin = 1e9, zmax = -1e9;
    for (int i = 0; i < R; i++) {                                 // SRTM .hgt: 3601x3601 big-endian int16, north row first
        fseek(f, ((long)(row0 + i) * 3601 + col0) * 2, SEEK_SET);
        if (fread(buf, 2, C, f) != (size_t)C) { fprintf(stderr, "short read\n"); return 1; }
        for (int j = 0; j < C; j++) {
            float z = (float)(short)((buf[2 * j] << 8) | buf[2 * j + 1]);
            Z[i * C + j] = z; if (z < zmin) zmin = z; if (z > zmax) zmax = z;
        }
    }
    fclose(f);
    printf("flood: grid %dx%d (%.1f km square), elevation %.0f-%.0f m, scenario=%s source=%.0f min total=%.0f min, threads=%d\n",
           R, C, R * DX / 1000, zmin, zmax, argv[5], rain_end / 60, t_end / 60, hs_T);

    result ref = simulate(-1, breach, rain_end, t_end, 60.0);
    result ref2 = simulate(-1, breach, rain_end, t_end, 60.0);
    if (ref2.wall < ref.wall) ref = ref2;
    printf("sequential (cpu%d): %.3f s  [source phase %.3f s, %ld steps, wet %.0f%% | recession %.3f s, %ld steps, wet %.0f%%]\n",
           hs_cpu[0], ref.wall, ref.wall_rain, ref.steps_rain, 100 * ref.actfrac_rain, ref.wall_rec, ref.steps - ref.steps_rain, 100 * ref.actfrac_rec);
    printf("mass check: stored/inflow = %.6f, clamped (negative-depth) volume = %.3g m3, repeat-run hash %s\n",
           ref.vol, ref.negvol, ref.hash == ref2.hash ? "identical" : "DIFFERENT");

    static double w[HS_NSCHED][32], wr[HS_NSCHED][32], wc[HS_NSCHED][32];
    int ok[HS_NSCHED]; double stl[HS_NSCHED] = {0}, sp[HS_NSCHED][HS_MAXT];
    for (int k = 0; k < HS_NSCHED; k++) ok[k] = 1;
    for (int r = 0; r < rounds; r++)
        for (int o = 0; o < nl; o++) {                            // interleaved, rotating start
            int c = hs_pick(list, nl, r, o);                 // random order per round
            hs_condition(0.5);                                   // same all-core load before every measured run
            result x = simulate(c, breach, rain_end, t_end, 60.0);
            w[c][r] = x.wall; wr[c][r] = x.wall_rain; wc[c][r] = x.wall_rec;
            if (x.hash != ref.hash) ok[c] = 0;
            stl[c] = siteA.calls ? (double)siteA.steals / siteA.calls : 0;
            for (int k = 0; k < hs_T; k++) sp[c][k] = c == HS_SHARE ? siteA.w[k] : siteA.speed[k];
        }
    double med[HS_NSCHED], best = 1e30;
    for (int o = 0; o < nl; o++) {
        int c = list[o];
        printf("  raw %-18s", hs_name[c]); for (int r = 0; r < rounds; r++) printf(" %.3f", w[c][r]); printf("\n");
        qsort(w[c], rounds, sizeof(double), hs_cmpd); qsort(wr[c], rounds, sizeof(double), hs_cmpd); qsort(wc[c], rounds, sizeof(double), hs_cmpd);
        med[c] = w[c][rounds / 2]; if (med[c] < best) best = med[c];
    }
    printf("%-19s %8s %8s %8s %8s %8s %9s  %s\n", "schedule", "total_s", "speedup", "regret", "source_s", "recess_s", "stealsA", "output");
    for (int o = 0; o < nl; o++) {
        int c = list[o];
        printf("%-19s %8.3f %8.2f %+7.1f%% %8.3f %8.3f %9.1f  %s\n", hs_name[c], med[c], ref.wall / med[c], 100 * (med[c] / best - 1),
               wr[c][rounds / 2], wc[c][rounds / 2], stl[c], ok[c] ? "bit-identical" : "DIFFERS");
    }
    for (int o = 0; o < nl; o++) {
        int c = list[o];
        if (c != HS_SHARE && c != HS_COSTCAP && c != HS_COSTCAP_NS) continue;
        printf("  %s final per-thread %s (loop A):", hs_name[c], c == HS_SHARE ? "shares" : "speeds");
        for (int k = 0; k < hs_T; k++) printf(" %.3f", sp[c][k]);
        printf("\n");
    }
    return 0;
}
