// CPU topology from sysfs (no root needed): which logical CPUs are P-cores or E-cores, which share a
// physical core (hyperthreads), and named thread sets used by the engine and the benchmarks.
//   1         one thread on the first P-core
//   p1        one thread per P-core          p   all P-core hyperthreads
//   e         all E-cores                    physical  one thread per physical core (P and E)
//   all       every online logical CPU       auto      = physical (Phase 1B, M6 E2; calibration agrees, D5)
//   0,2,4-7   explicit list
#include "hysw.h"
#include <stdlib.h>
#include <string.h>

static int read_list(const char *path, int *mark, int max) {     // parses "0-3,5" into mark[]
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char buf[1024];
    if (!fgets(buf, sizeof buf, f)) { fclose(f); return -1; }
    fclose(f);
    int count = 0;
    for (char *p = strtok(buf, ",\n"); p; p = strtok(NULL, ",\n")) {
        int a, b;
        if (sscanf(p, "%d-%d", &a, &b) == 2) { for (int c = a; c <= b && c < max; c++) { mark[c] = 1; count++; } }
        else if (sscanf(p, "%d", &a) == 1 && a < max) { mark[a] = 1; count++; }
    }
    return count;
}

static int read_int(const char *path, int dflt) {
    FILE *f = fopen(path, "r");
    if (!f) return dflt;
    int v = dflt;
    if (fscanf(f, "%d", &v) != 1) v = dflt;
    fclose(f);
    return v;
}

void topology_read(Topology *t) {
    memset(t, 0, sizeof *t);
    int online[HYSW_MAXCPU] = {0}, pc[HYSW_MAXCPU] = {0}, ec[HYSW_MAXCPU] = {0};
    if (read_list("/sys/devices/system/cpu/online", online, HYSW_MAXCPU) <= 0) online[0] = 1;
    int np = read_list("/sys/devices/cpu_core/cpus", pc, HYSW_MAXCPU);
    int ne = read_list("/sys/devices/cpu_atom/cpus", ec, HYSW_MAXCPU);
    t->hybrid = np > 0 && ne > 0;
    for (int c = 0; c < HYSW_MAXCPU; c++) {
        if (!online[c]) continue;
        int k = t->ncpu++;
        t->cpu[k] = c;
        t->type[k] = pc[c] ? 'P' : ec[c] ? 'E' : (t->hybrid ? '?' : 'P');
        char path[256];
        snprintf(path, sizeof path, "/sys/devices/system/cpu/cpu%d/topology/core_id", c);
        int core = read_int(path, c);
        snprintf(path, sizeof path, "/sys/devices/system/cpu/cpu%d/topology/package_id", c);
        t->core[k] = read_int(path, 0) * 100000 + core;
        int sib[HYSW_MAXCPU] = {0};
        snprintf(path, sizeof path, "/sys/devices/system/cpu/cpu%d/topology/thread_siblings_list", c);
        t->first_sibling[k] = 1;
        if (read_list(path, sib, HYSW_MAXCPU) > 0)
            for (int s = 0; s < c; s++) if (sib[s]) { t->first_sibling[k] = 0; break; }
    }
    FILE *f = fopen("/proc/cpuinfo", "r");
    snprintf(t->model, sizeof t->model, "unknown");
    if (f) {
        char line[512];
        while (fgets(line, sizeof line, f))
            if (!strncmp(line, "model name", 10)) {
                char *p = strchr(line, ':');
                if (p) { p += 2; p[strcspn(p, "\n")] = 0; snprintf(t->model, sizeof t->model, "%s", p); }
                break;
            }
        fclose(f);
    }
}

int topology_set(const Topology *t, const char *spec, int *cpus, int maxn, char *err, size_t errlen) {
    int n = 0;
    if (!strcmp(spec, "auto")) spec = "physical";
    if (!strcmp(spec, "1")) {
        for (int k = 0; k < t->ncpu; k++) if (t->type[k] == 'P') { cpus[n++] = t->cpu[k]; break; }
        if (!n && t->ncpu) cpus[n++] = t->cpu[0];
        return n;
    }
    if (!strcmp(spec, "all") || !strcmp(spec, "physical") || !strcmp(spec, "p") || !strcmp(spec, "p1") || !strcmp(spec, "e")) {
        for (int k = 0; k < t->ncpu && n < maxn; k++) {
            int ok = !strcmp(spec, "all") ||
                     (!strcmp(spec, "physical") && t->first_sibling[k]) ||
                     (!strcmp(spec, "p") && t->type[k] == 'P') ||
                     (!strcmp(spec, "p1") && t->type[k] == 'P' && t->first_sibling[k]) ||
                     (!strcmp(spec, "e") && t->type[k] == 'E');
            if (ok) cpus[n++] = t->cpu[k];
        }
        if (!n) snprintf(err, errlen, "thread set '%s' is empty on this machine", spec);
        return n ? n : -1;
    }
    char *s = strdup(spec);                                  // explicit list
    for (char *p = strtok(s, ","); p; p = strtok(NULL, ",")) {
        int a, b, r = sscanf(p, "%d-%d", &a, &b);
        if (r == 1) b = a;
        if (r < 1 || a > b) { snprintf(err, errlen, "bad CPU list '%s'", spec); free(s); return -1; }
        for (int c = a; c <= b; c++) {
            int found = 0;
            for (int k = 0; k < t->ncpu; k++) if (t->cpu[k] == c) found = 1;
            if (!found) { snprintf(err, errlen, "CPU %d is not online", c); free(s); return -1; }
            if (n < maxn) cpus[n++] = c;
        }
    }
    free(s);
    if (!n) snprintf(err, errlen, "empty CPU list");
    return n ? n : -1;
}
