// hysw_engine — command-line entry point. Internal interface for the web app and the benchmarks;
// every command writes JSON to stdout (errors: {"error": "..."} and exit status 1).
//
//   hysw_engine info
//   hysw_engine prep   --fasta <db.fasta> --out <db.bin>
//   hysw_engine dbinfo --db <db.bin|db.fasta>      size of a database (no search)
//   hysw_engine simdtrace --db <db> --query <q.fasta> (--accession ACC | --batch B) [--row0 R --rows N --col0 C --cols M]
//                         H values of one batch of 16 sequences, recorded by the real tiled kernel (Inside SIMD view)
//   hysw_engine search --db <db.bin|db.fasta> --query <q.fasta> [options]
//
// search options
//   --mode auto|scalar|simd|simd_threads   shorthand: scalar = scalar kernel on 1 thread, simd = SIMD on 1
//                                          thread, simd_threads/auto = SIMD on --cores (default auto)
//   --kernel scalar|simd|simd_untiled    --cores 1|p1|p|e|physical|all|auto|calibrate|<list>    --policy <name> (default dynamic,1)
//   --top-k N (25)   --align-k N (= top-k)   --matrix FILE   --gap-open 12   --gap-extend 1
//   --progress       JSON-lines progress on stderr while running
//   --trace          per-thread busy intervals in the result
//   --dump-scores F  all scores as int32 [query][sequence] in original FASTA order
//   --condition S    run an S-second all-core load just before each measured pass (benchmarks)
//   --repeat N       repeat the score pass N times (benchmarks; hits are from the last pass)
//   --profile query|full   SIMD profile rows only for the query's residues (default; faster for short
//                          queries, M6 E5) or for all 24 residue codes
//   hysw_engine condition --seconds S    run only the conditioning load (used before external yardsticks)
//   hysw_engine calibrate --db <db> [--budget S (1.0)] [--margin M (0.05)] [--rounds R (5)]
//                         time the candidate thread sets on a slice of the database and pick one (calibrate.c)
//   search --cores calibrate   the same calibration on the loaded database, then search with the chosen set
//                              (auto stays the fixed default "physical": no hidden start-up cost, reproducible)
#include "hysw.h"
#include <limits.h>
#include <omp.h>
#include <sched.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void fail(const char *msg) {
    printf("{\"error\":");
    json_str(stdout, msg);
    printf("}\n");
    fprintf(stderr, "hysw_engine: %s\n", msg);
    exit(1);
}

static const char *arg(int argc, char **argv, const char *name, const char *dflt) {
    for (int k = 2; k < argc; k++)
        if (!strcmp(argv[k], name)) {
            if (k + 1 >= argc) { char m[128]; snprintf(m, sizeof m, "%s needs a value", name); fail(m); }
            return argv[k + 1];
        }
    return dflt;
}
static int flag(int argc, char **argv, const char *name) {
    for (int k = 2; k < argc; k++) if (!strcmp(argv[k], name)) return 1;
    return 0;
}

static void default_matrix(char *out, size_t n) {   // <executable dir>/../data/BLOSUM62 (executable path capped at 3800 chars)
    char exe[PATH_MAX];
    ssize_t r = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (r <= 0) { snprintf(out, n, "data/BLOSUM62"); return; }
    exe[r] = 0;
    char *slash = strrchr(exe, '/');
    if (slash) *slash = 0;
    snprintf(out, n, "%.3800s/../data/BLOSUM62", exe);
}

static void read_file_line(const char *path, char *out, size_t n) {
    FILE *f = fopen(path, "r");
    out[0] = 0;
    if (!f) return;
    if (fgets(out, (int)n, f)) out[strcspn(out, "\n")] = 0;
    fclose(f);
}

// ------------------------------------------------------------------ info
static void cmd_info(void) {
    Topology t; topology_read(&t);
    printf("{\"cpu_model\":"); json_str(stdout, t.model);
    printf(",\"logical_cpus\":%d,\"hybrid\":%s,\"cpus\":[", t.ncpu, t.hybrid ? "true" : "false");
    for (int k = 0; k < t.ncpu; k++)
        printf("%s{\"cpu\":%d,\"type\":\"%c\",\"core\":%d,\"first_sibling\":%s}", k ? "," : "", t.cpu[k], t.type[k], t.core[k],
               t.first_sibling[k] ? "true" : "false");
    printf("],\"thread_sets\":{");
    const char *sets[] = {"1", "p1", "p", "e", "physical", "all"};
    for (int s = 0; s < 6; s++) {
        int cpus[HYSW_MAXCPU]; char err[128];
        int n = topology_set(&t, sets[s], cpus, HYSW_MAXCPU, err, sizeof err);
        printf("%s\"%s\":[", s ? "," : "", sets[s]);
        for (int k = 0; k < n; k++) printf("%s%d", k ? "," : "", cpus[k]);
        printf("]");
    }
    __builtin_cpu_init();
    printf("},\"simd\":{\"avx2\":%s,\"avx512bw\":%s,\"lanes\":%d,\"lane_bits\":16},",
           __builtin_cpu_supports("avx2") ? "true" : "false", __builtin_cpu_supports("avx512bw") ? "true" : "false", HYSW_LANES);
    char buf[256];
    read_file_line("/sys/firmware/acpi/platform_profile", buf, sizeof buf);
    printf("\"power\":{\"platform_profile\":"); json_str(stdout, buf);
    read_file_line("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor", buf, sizeof buf);
    printf(",\"governor\":"); json_str(stdout, buf);
    const char *zones[] = {"intel-rapl:0", "intel-rapl-mmio:0"};
    for (int z = 0; z < 2; z++) {
        char path[256], lim[64], win[64];
        snprintf(path, sizeof path, "/sys/class/powercap/%s/constraint_0_power_limit_uw", zones[z]);
        read_file_line(path, lim, sizeof lim);
        snprintf(path, sizeof path, "/sys/class/powercap/%s/constraint_0_time_window_us", zones[z]);
        read_file_line(path, win, sizeof win);
        printf(",\"%s_long_term_uw\":%s,\"%s_window_us\":%s", zones[z], lim[0] ? lim : "null", zones[z], win[0] ? win : "null");
    }
    snprintf(buf, sizeof buf, "/sys/class/powercap/intel-rapl:0/energy_uj");
    printf(",\"energy_readable\":%s}", access(buf, R_OK) == 0 ? "true" : "false");
    printf(",\"compiler\":\"gcc %s\"}\n", __VERSION__);
}

// ------------------------------------------------------------------ prep
static void cmd_prep(int argc, char **argv) {
    const char *in = arg(argc, argv, "--fasta", NULL), *out = arg(argc, argv, "--out", NULL);
    if (!in || !out) fail("prep needs --fasta and --out");
    char err[512];
    double t0 = omp_get_wtime();
    SeqSet s;
    if (fasta_read(in, &s, err, sizeof err)) fail(err);
    Db db;
    if (db_build(&s, &db, err, sizeof err)) fail(err);
    long unknown = 0;
    for (int k = 0; k < s.n; k++) unknown += s.nunknown[k];
    seqset_free(&s);
    if (db_write(&db, out, err, sizeof err)) fail(err);
    printf("{\"database\":"); json_str(stdout, out);
    printf(",\"sequences\":%lu,\"residues\":%lu,\"batches\":%lu,\"padded_residues\":%lu,\"longest\":%u,\"unknown_letters\":%ld,\"seconds\":%.3f}\n",
           (unsigned long)db.nseq, (unsigned long)db.nres, (unsigned long)db.nbatch, (unsigned long)db.cells, db.maxlen, unknown,
           omp_get_wtime() - t0);
    db_close(&db);
}

// ------------------------------------------------------------------ dbinfo
static void cmd_dbinfo(int argc, char **argv) {
    const char *dbp = arg(argc, argv, "--db", NULL);
    if (!dbp) fail("dbinfo needs --db");
    char err[512];
    double t0 = omp_get_wtime();
    Db db;
    if (db_open(dbp, &db, err, sizeof err)) fail(err);
    printf("{\"name\":"); json_str(stdout, db.name);
    printf(",\"path\":"); json_str(stdout, dbp);
    printf(",\"sequences\":%lu,\"residues\":%lu,\"batches\":%lu,\"padded_residues\":%lu,\"longest\":%u,\"load_seconds\":%.4f}\n",
           (unsigned long)db.nseq, (unsigned long)db.nres, (unsigned long)db.nbatch, (unsigned long)db.cells, db.maxlen,
           omp_get_wtime() - t0);
    db_close(&db);
}

// ------------------------------------------------------------------ simdtrace
static int header_accession(const char *h, char *acc, size_t n) {      // "sp|P69905|HBA_HUMAN ..." -> P69905
    const char *b1 = strchr(h, '|'), *sp = strchr(h, ' ');
    const char *b2 = b1 ? strchr(b1 + 1, '|') : NULL;
    if (b1 && b2 && (!sp || b2 < sp)) { snprintf(acc, n, "%.*s", (int)(b2 - b1 - 1), b1 + 1); return 1; }
    snprintf(acc, n, "%.*s", (int)(sp ? sp - h : (long)strlen(h)), h);
    return 0;
}

static void cmd_simdtrace(int argc, char **argv) {
    char err[512], mpath[PATH_MAX];
    const char *dbp = arg(argc, argv, "--db", NULL), *qp = arg(argc, argv, "--query", NULL);
    const char *acc = arg(argc, argv, "--accession", NULL), *bs = arg(argc, argv, "--batch", NULL);
    if (!dbp || !qp || (!acc && !bs)) fail("simdtrace needs --db, --query and --accession or --batch");
    default_matrix(mpath, sizeof mpath);
    Matrix m;
    if (matrix_load(arg(argc, argv, "--matrix", mpath), &m, err, sizeof err)) fail(err);
    Gaps g = {12, 1};
    SeqSet qs;
    if (fasta_read(qp, &qs, err, sizeof err)) fail(err);
    if (qs.n < 1 || qs.len[0] == 0) fail("the query is empty");
    const unsigned char *q = qs.seq[0]; const int ql = qs.len[0];
    Db db;
    if (db_open(dbp, &db, err, sizeof err)) fail(err);
    long long b = -1; int target = -1;
    if (acc) {
        char h[4096], a[64];
        for (uint64_t k = 0; k < db.nseq && b < 0; k++) {
            db_header(&db, k, h, sizeof h);
            header_accession(h, a, sizeof a);
            if (!strcmp(a, acc)) { b = (long long)(k / HYSW_LANES); target = (int)(k % HYSW_LANES); }
        }
        if (b < 0) fail("accession not found in the database");
    } else {
        b = atoll(bs);
        if (b < 0 || (uint64_t)b >= db.nbatch) fail("--batch out of range");
    }
    const int bl = (int)db.blen[b];
    int r0 = atoi(arg(argc, argv, "--row0", "0")), nr = atoi(arg(argc, argv, "--rows", "24"));
    int c0 = atoi(arg(argc, argv, "--col0", "0")), nc = atoi(arg(argc, argv, "--cols", "48"));
    if (r0 < 0) r0 = 0;
    if (c0 < 0) c0 = 0;
    if (r0 >= ql) r0 = ql - 1;
    if (c0 >= bl) c0 = bl - 1;
    if (nr < 1 || nr > 200 || nc < 1 || nc > 400) fail("--rows must be 1..200 and --cols 1..400");
    if (r0 + nr > ql) nr = ql - r0;
    if (c0 + nc > bl) nc = bl - c0;
    int16_t *tr = malloc(sizeof(int16_t) * HYSW_LANES * (size_t)nr * nc);
    int out[HYSW_LANES];
    if (!tr || sw_simd_trace(&db, (uint64_t)b, q, ql, &m, g, r0, nr, (uint32_t)c0, nc, tr, out) < 0) fail("out of memory");
    printf("{\"batch\":%lld,\"batches\":%lu,\"lanes\":%d,\"colblock\":%d,\"target_lane\":%d,\"order\":\"tiled\",",
           b, (unsigned long)db.nbatch, HYSW_LANES, HYSW_COLBLOCK, target);
    printf("\"query\":{\"header\":"); json_str(stdout, qs.hdr[0]);
    printf(",\"length\":%d},\"window\":{\"row0\":%d,\"rows\":%d,\"col0\":%d,\"cols\":%d,\"batch_length\":%d},", ql, r0, nr, c0, nc, bl);
    printf("\"query_residues\":\"");
    for (int i = 0; i < nr; i++) putchar(hysw_decode(q[r0 + i]));
    printf("\",\"gap_open\":%d,\"gap_extend\":%d,\"matrix\":", g.open, g.extend); json_str(stdout, m.name);
    printf(",\"sequences\":[");
    for (int l = 0; l < HYSW_LANES; l++) {
        uint64_t k = (uint64_t)b * HYSW_LANES + l;
        printf("%s", l ? "," : "");
        if (k >= db.nseq) { printf("null"); continue; }
        char h[4096], a[64];
        db_header(&db, k, h, sizeof h);
        header_accession(h, a, sizeof a);
        printf("{\"lane\":%d,\"accession\":", l); json_str(stdout, a);
        printf(",\"header\":"); json_str(stdout, h);
        printf(",\"length\":%u,\"score\":%d,\"residues\":\"", db.len[k], out[l]);
        const unsigned char *col = db.res + db.boff[b] + l;
        for (int j = 0; j < nc; j++) putchar(c0 + j < (int)db.len[k] ? hysw_decode(col[(size_t)(c0 + j) * HYSW_LANES]) : '.');
        printf("\"}");
    }
    printf("],\"H\":[");                           // H[row][col][lane]
    for (int i = 0; i < nr; i++) {
        printf("%s[", i ? "," : "");
        for (int j = 0; j < nc; j++) {
            printf("%s[", j ? "," : "");
            for (int l = 0; l < HYSW_LANES; l++) printf("%s%d", l ? "," : "", tr[((size_t)i * nc + j) * HYSW_LANES + l]);
            printf("]");
        }
        printf("]");
    }
    printf("]}\n");
    free(tr);
    db_close(&db);
    seqset_free(&qs);
}

// ------------------------------------------------------------------ search helpers
static void condition_load(double seconds) {     // same all-core load before every measured run
    Topology t; topology_read(&t);
    int cpus[HYSW_MAXCPU]; char err[64];
    int n = topology_set(&t, "all", cpus, HYSW_MAXCPU, err, sizeof err);
    if (n <= 0) return;
#pragma omp parallel num_threads(n)
    {
        cpu_set_t c, old; sched_getaffinity(0, sizeof old, &old);
        CPU_ZERO(&c); CPU_SET(cpus[omp_get_thread_num()], &c); sched_setaffinity(0, sizeof c, &c);
        double t0 = omp_get_wtime(), x = 1.0;
        while (omp_get_wtime() - t0 < seconds) for (int k = 0; k < 20000; k++) x = x * 1.0000001 + 1e-9;
        if (x < 0) abort();
        sched_setaffinity(0, sizeof old, &old);
    }
}

// UniProt header "sp|P69905|HBA_HUMAN Hemoglobin subunit alpha OS=Homo sapiens OX=9606 GN=HBA1 PE=1 SV=2"
static void print_header_fields(const char *h) {
    char acc[64] = "", entry[64] = "", name[512] = "", org[256] = "";
    const char *sp = strchr(h, ' ');
    size_t idlen = sp ? (size_t)(sp - h) : strlen(h);
    const char *b1 = memchr(h, '|', idlen), *b2 = b1 ? memchr(b1 + 1, '|', idlen - (b1 + 1 - h)) : NULL;
    if (b1 && b2) {
        snprintf(acc, sizeof acc, "%.*s", (int)(b2 - b1 - 1), b1 + 1);
        snprintf(entry, sizeof entry, "%.*s", (int)(h + idlen - b2 - 1), b2 + 1);
    } else snprintf(acc, sizeof acc, "%.*s", (int)(idlen < 63 ? idlen : 63), h);
    if (sp) {
        const char *os = strstr(sp, " OS=");
        snprintf(name, sizeof name, "%.*s", (int)(os ? os - sp - 1 : (long)strlen(sp + 1)), sp + 1);
        if (os) {
            const char *ox = strstr(os + 4, " OX=");
            snprintf(org, sizeof org, "%.*s", (int)(ox ? ox - os - 4 : (long)strlen(os + 4)), os + 4);
        }
    }
    printf("\"accession\":"); json_str(stdout, acc);
    printf(",\"entry\":"); json_str(stdout, entry);
    printf(",\"name\":"); json_str(stdout, name);
    printf(",\"organism\":"); json_str(stdout, org);
}

// ------------------------------------------------------------------ calibrate
static void cmd_calibrate(int argc, char **argv) {
    char err[512], mpath[PATH_MAX];
    const char *dbp = arg(argc, argv, "--db", NULL);
    if (!dbp) fail("calibrate needs --db");
    default_matrix(mpath, sizeof mpath);
    double budget = atof(arg(argc, argv, "--budget", "1.0")), margin = atof(arg(argc, argv, "--margin", "0.05"));
    int rounds = atoi(arg(argc, argv, "--rounds", "5"));
    if (!(budget >= 0.05 && budget <= 60)) fail("--budget must be 0.05..60 seconds");
    if (!(margin >= 0 && margin <= 1)) fail("--margin must be 0..1");
    if (rounds < 1 || rounds > CALIB_MAXROUND) fail("--rounds must be 1..32");
    Matrix m;
    if (matrix_load(arg(argc, argv, "--matrix", mpath), &m, err, sizeof err)) fail(err);
    Gaps g = {12, 1};
    Db db;
    if (db_open(dbp, &db, err, sizeof err)) fail(err);
    Topology topo; topology_read(&topo);
    Calib c;
    if (calibrate(&db, &m, g, &topo, budget, margin, rounds, &c, err, sizeof err)) fail(err);
    printf("{\"database\":"); json_str(stdout, db.name);
    printf(",\"cpu_model\":"); json_str(stdout, topo.model);
    printf(",\"calibration\":");
    calib_print(stdout, &c);
    printf("}\n");
    db_close(&db);
}

// ------------------------------------------------------------------ search
static void cmd_search(int argc, char **argv) {
    char err[512], mpath[PATH_MAX];
    const char *dbp = arg(argc, argv, "--db", NULL), *qp = arg(argc, argv, "--query", NULL);
    if (!dbp || !qp) fail("search needs --db and --query");
    default_matrix(mpath, sizeof mpath);
    const char *mp = arg(argc, argv, "--matrix", mpath);
    Gaps g = {atoi(arg(argc, argv, "--gap-open", "12")), atoi(arg(argc, argv, "--gap-extend", "1"))};
    if (g.open < g.extend || g.extend < 1 || g.open > 100) fail("need 1 <= gap-extend <= gap-open <= 100");
    const char *mode = arg(argc, argv, "--mode", "auto");
    const char *kernel = arg(argc, argv, "--kernel", NULL), *cores = arg(argc, argv, "--cores", NULL);
    const char *policy = arg(argc, argv, "--policy", "dynamic,1");
    SearchOpts o; memset(&o, 0, sizeof o);
    if (!strcmp(mode, "scalar")) { o.kernel = KERNEL_SCALAR; if (!cores) cores = "1"; }
    else if (!strcmp(mode, "simd")) { o.kernel = KERNEL_SIMD; if (!cores) cores = "1"; }
    else if (!strcmp(mode, "simd_threads") || !strcmp(mode, "auto")) { o.kernel = KERNEL_SIMD; if (!cores) cores = "auto"; }
    else fail("--mode must be auto, scalar, simd or simd_threads");
    if (kernel) {
        if (!strcmp(kernel, "scalar")) o.kernel = KERNEL_SCALAR;
        else if (!strcmp(kernel, "simd")) o.kernel = KERNEL_SIMD;
        else if (!strcmp(kernel, "simd_untiled")) { o.kernel = KERNEL_SIMD; o.simd_untiled = 1; }
        else fail("--kernel must be scalar, simd or simd_untiled");
    }
    if ((o.policy = map_parse(policy)) < 0) fail("unknown --policy (static, dynamic,1, dynamic,chunk, guided, steal, cost+steal, costcap+steal)");
    Topology topo; topology_read(&topo);
    const int calib = !strcmp(cores, "calibrate");
    if (!calib && (o.T = topology_set(&topo, cores, o.cpus, HYSW_MAXCPU, err, sizeof err)) <= 0) fail(err);
    o.top_k = atoi(arg(argc, argv, "--top-k", "25"));
    o.align_k = atoi(arg(argc, argv, "--align-k", arg(argc, argv, "--top-k", "25")));
    if (o.top_k < 1 || o.top_k > 1000) fail("--top-k must be 1..1000");
    o.progress = flag(argc, argv, "--progress") ? stderr : NULL;
    o.trace = flag(argc, argv, "--trace");
    const char *prof = arg(argc, argv, "--profile", "query");
    if (strcmp(prof, "full") && strcmp(prof, "query")) fail("--profile must be full or query");
    o.profile_query = !strcmp(prof, "query");
    int repeat = atoi(arg(argc, argv, "--repeat", "1"));
    double cond = atof(arg(argc, argv, "--condition", "0"));
    const char *dump = arg(argc, argv, "--dump-scores", NULL);
    if (repeat < 1) repeat = 1;

    Matrix m;
    if (matrix_load(mp, &m, err, sizeof err)) fail(err);
    if (g.open > 32767 - m.max) fail("gap penalties too large");
    SeqSet qs;
    if (fasta_read(qp, &qs, err, sizeof err)) fail(err);
    for (int k = 0; k < qs.n; k++) if (qs.len[k] == 0) fail("a query sequence is empty");
    double tl = omp_get_wtime();
    Db db;
    if (db_open(dbp, &db, err, sizeof err)) fail(err);
    tl = omp_get_wtime() - tl;
    Calib cal;
    if (calib) {
        if (calibrate(&db, &m, g, &topo, 1.0, 0.05, 5, &cal, err, sizeof err)) fail(err);
        o.T = cal.set[cal.chosen].T;
        memcpy(o.cpus, cal.set[cal.chosen].cpus, sizeof(int) * o.T);
    }
    LoopSite site; map_site_init(&site, o.T, o.cpus, 1);
    int *scores = malloc(sizeof(int) * db.nseq);
    FILE *df = dump ? fopen(dump, "wb") : NULL;
    if (dump && !df) fail("cannot write --dump-scores file");
    int *orig_order = dump ? malloc(sizeof(int) * db.nseq) : NULL;
    Hit *hits = calloc(o.top_k, sizeof(Hit));

    printf("{\"engine\":\"hysw 0.1\",\"database\":{\"name\":"); json_str(stdout, db.name);
    printf(",\"path\":"); json_str(stdout, dbp);
    printf(",\"sequences\":%lu,\"residues\":%lu,\"padded_residues\":%lu,\"longest\":%u,\"load_seconds\":%.4f},",
           (unsigned long)db.nseq, (unsigned long)db.nres, (unsigned long)db.cells, db.maxlen, tl);
    printf("\"settings\":{\"kernel\":\"%s\",\"mode\":", o.kernel == KERNEL_SIMD ? (o.simd_untiled ? "simd_untiled" : "simd") : "scalar"); json_str(stdout, mode);
    printf(",\"cores_spec\":"); json_str(stdout, cores);
    if (calib) { printf(",\"calibration\":"); calib_print(stdout, &cal); }
    printf(",\"threads\":%d,\"cpus\":[", o.T);
    for (int t = 0; t < o.T; t++) printf("%s%d", t ? "," : "", o.cpus[t]);
    printf("],\"policy\":\"%s\",\"profile\":\"%s\",\"matrix\":", map_names[o.policy], o.profile_query ? "query" : "full"); json_str(stdout, m.name);
    printf(",\"gap_open\":%d,\"gap_extend\":%d,\"lanes\":%d,\"top_k\":%d,\"condition_s\":%.3f,\"repeat\":%d},\"queries\":[",
           g.open, g.extend, o.kernel == KERNEL_SIMD ? HYSW_LANES : 1, o.top_k, cond, repeat);
    int status = 0;
    for (int qi = 0; qi < qs.n; qi++) {
        const unsigned char *q = qs.seq[qi]; int ql = qs.len[qi];
        SearchStats st; double times[1000]; int nt = 0;
        for (int r = 0; r < repeat; r++) {
            if (cond > 0) condition_load(cond);
            if (r) search_stats_free(&st);
            if (search_scores(&db, q, ql, &m, g, &o, &site, scores, &st)) fail("search failed (out of memory?)");
            if (nt < 1000) times[nt++] = st.seconds;
        }
        if (df) {
            for (uint64_t k = 0; k < db.nseq; k++) orig_order[db.orig[k]] = scores[k];
            fwrite(orig_order, sizeof(int), db.nseq, df);
        }
        double ta = omp_get_wtime();
        int nh = search_top(&db, q, ql, &m, g, scores, o.top_k, o.align_k, o.T, hits);
        st.align_seconds = omp_get_wtime() - ta;
        if (nh < 0) { status = 2; nh = o.top_k < (int)db.nseq ? o.top_k : (int)db.nseq; }
        printf("%s{\"query\":{\"header\":", qi ? "," : ""); json_str(stdout, qs.hdr[qi]);
        printf(",\"length\":%d,\"unknown_letters\":%d},", ql, qs.nunknown[qi]);
        printf("\"timing\":{\"seconds\":%.6f,\"gcups\":%.4f,\"cells\":%.0f,\"padded_cells\":%.0f,\"align_seconds\":%.6f,\"rescored\":%ld,\"runs\":[",
               st.seconds, st.cells / st.seconds / 1e9, st.cells, st.padded_cells, st.align_seconds, st.rescored);
        for (int r = 0; r < nt; r++) printf("%s%.6f", r ? "," : "", times[r]);
        printf("]},\"threads\":[");
        for (int t = 0; t < o.T; t++) {
            char type = '?';
            for (int k = 0; k < topo.ncpu; k++) if (topo.cpu[k] == o.cpus[t]) type = topo.type[k];
            printf("%s{\"cpu\":%d,\"type\":\"%c\",\"items\":%ld,\"busy_s\":%.6f", t ? "," : "", o.cpus[t], type, st.thread_items[t], st.thread_busy[t]);
            if (o.trace) {
                printf(",\"busy\":[");
                for (int k = 0; k < st.ntrace[t]; k++) printf("%s[%.6f,%.6f]", k ? "," : "", st.trace[t][k].t0, st.trace[t][k].t1);
                printf("]");
            }
            printf("}");
        }
        printf("],\"hits\":[");
        for (int h = 0; h < nh; h++) {
            uint64_t k = hits[h].k;
            char hdr[4096]; db_header(&db, k, hdr, sizeof hdr);
            printf("%s{\"rank\":%d,", h ? "," : "", h + 1);
            print_header_fields(hdr);
            printf(",\"header\":"); json_str(stdout, hdr);
            printf(",\"db_index\":%u,\"length\":%u,\"score\":%d", db.orig[k], db.len[k], hits[h].score);
            if (hits[h].has_aln && hits[h].aln.length > 0) {
                Alignment *a = &hits[h].aln;
                printf(",\"identity\":%.4f,\"positives\":%.4f,\"coverage\":%.4f,\"alignment\":{\"q_start\":%d,\"q_end\":%d,\"t_start\":%d,\"t_end\":%d,"
                       "\"length\":%d,\"identities\":%d,\"positives\":%d,\"gaps\":%d,\"q\":",
                       (double)a->identities / a->length, (double)a->positives / a->length, (double)(a->q_end - a->q_start + 1) / ql,
                       a->q_start, a->q_end, a->t_start, a->t_end, a->length, a->identities, a->positives, a->gaps);
                json_str(stdout, a->q); printf(",\"mid\":"); json_str(stdout, a->mid); printf(",\"t\":"); json_str(stdout, a->t);
                printf("}");
            }
            printf("}");
            if (hits[h].has_aln) align_free(&hits[h].aln);
        }
        printf("]}");
        search_stats_free(&st);
    }
    printf("],\"status\":\"%s\"}\n", status ? "alignment_mismatch" : "ok");
    if (df) fclose(df);
    free(scores); free(orig_order); free(hits);
    map_site_free(&site);
    db_close(&db);
    seqset_free(&qs);
    if (status) exit(status);
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: hysw_engine info | prep --fasta F --out DB.bin | search --db DB --query Q [options]\n"); return 1; }
    if (!strcmp(argv[1], "info")) cmd_info();
    else if (!strcmp(argv[1], "prep")) cmd_prep(argc, argv);
    else if (!strcmp(argv[1], "search")) cmd_search(argc, argv);
    else if (!strcmp(argv[1], "calibrate")) cmd_calibrate(argc, argv);
    else if (!strcmp(argv[1], "dbinfo")) cmd_dbinfo(argc, argv);
    else if (!strcmp(argv[1], "simdtrace")) cmd_simdtrace(argc, argv);
    else if (!strcmp(argv[1], "condition")) {           // the same all-core load, for external yardsticks
        double s = atof(arg(argc, argv, "--seconds", "2"));
        condition_load(s);
        printf("{\"conditioned_s\":%.3f}\n", s);
    }
    else fail("unknown command (info, prep, dbinfo, search, simdtrace, calibrate, condition)");
    return 0;
}
