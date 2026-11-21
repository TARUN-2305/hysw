// HySW — exact Smith–Waterman protein database search with SIMD + OpenMP threads.
// Shared types and module interfaces. Each .c file implements one section below.
#ifndef HYSW_H
#define HYSW_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

// ---------------------------------------------------------------- residues
// Residue codes follow the column order of the NCBI matrix files.
#define HYSW_ALPHABET "ARNDCQEGHILKMFPSTWYVBZX*"
#define HYSW_NCODES 24          // real residue codes 0..23
#define HYSW_X 22               // unknown letters (U, O, J, ...) become X
#define HYSW_PAD 24             // padding code used inside SIMD batches (never in real sequences)
#define HYSW_PAD_SCORE (-100)   // score of anything against padding (any value <= 0 is correct)
#define HYSW_LANES 16           // database sequences per SIMD batch (16 x 16-bit = 256-bit vector)

unsigned char hysw_encode(int c);   // ASCII letter -> code (case-insensitive); unknown -> HYSW_X
char hysw_decode(unsigned char code);

// ---------------------------------------------------------------- fasta.c
typedef struct {
    int n;                  // number of records
    long nres;              // total residues
    unsigned char **seq;    // encoded residues of record k
    int *len;
    char **hdr;             // header line without '>' and newline
    int *nunknown;          // letters that were mapped to X (for input warnings)
} SeqSet;

int fasta_read(const char *path, SeqSet *out, char *err, size_t errlen);
void seqset_free(SeqSet *s);

// ---------------------------------------------------------------- matrix.c
typedef struct {
    signed char s[32][32];  // s[a][b] for codes a, b; rows/columns >= 24 are padding
    int max;                // largest real score (used for the 16-bit overflow threshold)
    int min;
    char name[64];
} Matrix;

int matrix_load(const char *path, Matrix *m, char *err, size_t errlen);

// Gap of k residues costs open + (k - 1) * extend (Parasail's convention; open 12 / extend 1 = BLAST's 11 + k).
typedef struct { int open, extend; } Gaps;

// ---------------------------------------------------------------- db.c
// Database in "batch" form: sequences sorted by length (longest first), grouped into batches of 16.
// Residues of batch b are interleaved: residue j of lane l is res[boff[b] + j*16 + l]; short lanes are
// padded with HYSW_PAD. Sorted index k = 16*b + l.
typedef struct {
    uint64_t nseq, nres, nbatch, cells;   // cells = padded residue slots (sum of blen * 16)
    uint32_t maxlen;
    uint32_t *len;        // [nseq]   length, sorted order
    uint32_t *orig;       // [nseq]   sorted index -> position in the original FASTA
    uint32_t *blen;       // [nbatch] length of the longest sequence in the batch
    uint64_t *boff;       // [nbatch] offset of the batch in res
    uint64_t *hoff;       // [nseq+1] header k is hdr[hoff[k] .. hoff[k+1]-1] (no terminator)
    unsigned char *res;
    char *hdr;
    void *map; size_t maplen;   // set when opened from a file with mmap
    void *own;                  // set when built in memory
    char name[128];
} Db;

int db_build(const SeqSet *s, Db *db, char *err, size_t errlen);
int db_write(const Db *db, const char *path, char *err, size_t errlen);
int db_open(const char *path, Db *db, char *err, size_t errlen);   // .bin (mmap) or .fasta (built in memory)
void db_close(Db *db);
void db_get_seq(const Db *db, uint64_t k, unsigned char *out);     // contiguous residues of sorted index k
int db_header(const Db *db, uint64_t k, char *out, size_t outlen);

// ---------------------------------------------------------------- sw_scalar.c / sw_simd.c
// Reference kernel: Gotoh affine-gap Smith–Waterman, 32-bit scores, score only.
// work must hold 2*ql ints.
int sw_scalar(const unsigned char *q, int ql, const unsigned char *d, int dl,
              const Matrix *m, Gaps g, int *work);

// Batch kernel: aligns the query against the 16 sequences of batch b at once (one per SIMD lane),
// 16-bit scores. out[l] receives lane l's score; the return value is a bit mask of lanes whose score
// came within m->max of the 16-bit limit (they must be re-scored with sw_scalar).
#ifndef HYSW_COLBLOCK
#define HYSW_COLBLOCK 16        // tiled kernel: columns per block (profile 16 x 24 x 32 B = 12 KB, column state 1 KB)
#endif
typedef struct {
    int16_t *H, *E; int cap;
    unsigned char codes[HYSW_NCODES]; int ncodes;   // profile rows to build for each column
    int untiled;     // 1: original column-by-column loop order; 0 (default): column blocks (see sw_simd.c)
} SimdWork;
int simd_work_init(SimdWork *w, int ql);           // profile rows: all 24 residue codes
// only_query = 1: build profile rows only for residue codes that occur in the query (the others are never read)
void simd_work_profile(SimdWork *w, const unsigned char *q, int ql, int only_query);
void simd_work_free(SimdWork *w);
unsigned sw_simd_batch(const Db *db, uint64_t b, const unsigned char *q, int ql,
                       const Matrix *m, Gaps g, SimdWork *w, int out[HYSW_LANES]);
// Same tiled kernel on one batch, also recording H of every cell in the window rows [r0, r0+nr) x columns
// [c0, c0+nc): tr[((i-r0)*nc + (j-c0))*16 + lane]. Returns the overflow-risk mask (as sw_simd_batch), -1 on error.
int sw_simd_trace(const Db *db, uint64_t b, const unsigned char *q, int ql, const Matrix *m, Gaps g,
                  int r0, int nr, uint32_t c0, int nc, int16_t *tr, int out[HYSW_LANES]);

// ---------------------------------------------------------------- align.c
typedef struct {
    int score;
    int q_start, q_end, t_start, t_end;   // 1-based, inclusive
    int length, identities, positives, gaps;
    char *q, *mid, *t;                    // aligned strings ('-' for gaps); caller frees with align_free
} Alignment;

int sw_align(const unsigned char *q, int ql, const unsigned char *d, int dl,
             const Matrix *m, Gaps g, Alignment *a);
int alignment_rescore(const Alignment *a, const Matrix *m, Gaps g);   // score recomputed from the strings
void align_free(Alignment *a);

// ---------------------------------------------------------------- topology.c
#define HYSW_MAXCPU 256
typedef struct {
    int ncpu;                        // online logical CPUs
    int cpu[HYSW_MAXCPU];            // logical CPU ids
    char type[HYSW_MAXCPU];          // 'P', 'E' or '?'
    int core[HYSW_MAXCPU];           // physical core id
    int first_sibling[HYSW_MAXCPU];  // 1 if this is the lowest-numbered thread of its core
    char model[128];
    int hybrid;
} Topology;

void topology_read(Topology *t);
// spec: 1 | p | p1 | e | physical | all | auto | explicit list "0,2,4-7". Returns count, fills cpus.
int topology_set(const Topology *t, const char *spec, int *cpus, int maxn, char *err, size_t errlen);

// ---------------------------------------------------------------- mapping.c
// Batch-to-thread mapping policies behind one loop interface.
enum {
    MAP_STATIC, MAP_DYN1, MAP_DYNC, MAP_GUIDED,   // stock OpenMP schedules
    MAP_STEAL,                                    // equal contiguous blocks + work stealing
    MAP_COST_STEAL,                               // blocks of equal cost (cost hint) + stealing
    MAP_COSTCAP_STEAL,                            // blocks of cost proportional to measured thread speed + stealing
    MAP_NPOLICY
};
extern const char *map_names[MAP_NPOLICY];
int map_parse(const char *name);

typedef void (*loop_body)(long lo, long hi, int tid, void *ctx);

typedef struct {               // per-loop memory, kept between executions of the same loop
    int T;
    int cpus[HYSW_MAXCPU];     // thread t is pinned to cpus[t] (if pin)
    int pin;
    double speed[HYSW_MAXCPU]; // MAP_COSTCAP_STEAL: relative speed per thread (sums to 1)
    int init;
    long steals;
    double *ps; long ps_cap;   // prefix sums of the cost hint
} LoopSite;

void map_site_init(LoopSite *s, int T, const int *cpus, int pin);
void map_site_free(LoopSite *s);
void map_for(int policy, LoopSite *s, long n, loop_body body, void *ctx, const float *cost);

// ---------------------------------------------------------------- search.c
enum { KERNEL_SCALAR, KERNEL_SIMD };

typedef struct {
    int kernel, policy, top_k, align_k;
    int profile_query;       // SIMD: build profile rows only for residues present in the query
    int simd_untiled;        // SIMD: use the original untiled loop order (for comparison)
    int T, cpus[HYSW_MAXCPU];
    FILE *progress;          // JSON-lines progress (NULL = none)
    int trace;               // record per-thread busy intervals
} SearchOpts;

typedef struct { double t0, t1; } Interval;
typedef struct {
    double seconds;          // wall time of the score pass
    double align_seconds;    // traceback of the top hits
    double cells;            // query length x database residues (real cells, the GCUPS numerator)
    double padded_cells;     // query length x padded residue slots (work actually done by SIMD)
    long rescored;           // sequences re-scored because of 16-bit overflow risk
    long thread_items[HYSW_MAXCPU];
    double thread_busy[HYSW_MAXCPU];
    Interval *trace[HYSW_MAXCPU]; int ntrace[HYSW_MAXCPU];
} SearchStats;

typedef struct { uint64_t k; int score; Alignment aln; int has_aln; } Hit;

// Scores of every database sequence, in sorted order (scores[k] for sorted index k).
int search_scores(const Db *db, const unsigned char *q, int ql, const Matrix *m, Gaps g,
                  const SearchOpts *o, LoopSite *site, int *scores, SearchStats *st);
// Top-K by score (ties: lower original index first), with alignments for the first align_k.
int search_top(const Db *db, const unsigned char *q, int ql, const Matrix *m, Gaps g,
               const int *scores, int top_k, int align_k, int T, Hit *hits);
void search_stats_free(SearchStats *st);

// ---------------------------------------------------------------- calibrate.c
// Thread-set calibration (blueprint §4.5); see calibrate.c for the procedure and decision rule.
#define CALIB_MAXSET 6
#define CALIB_MAXROUND 32
typedef struct {
    const char *name;                    // thread-set name understood by topology_set
    int T, cpus[HYSW_MAXCPU];
    double gcups[CALIB_MAXROUND];        // one value per round
    double median;
} CalibSet;
typedef struct {
    CalibSet set[CALIB_MAXSET]; int nset;
    int def, chosen, best;               // indices: default set ("physical"), chosen set, fastest median
    int rounds, passes;                  // passes per trial
    double budget, margin, seconds, trial_target;
    int query_len;
    uint64_t stride, slice_batches, slice_residues;
} Calib;
// View of every stride-th full batch starting at batch `first` (shares db->res; free with calib_db_slice_free).
// Only len/blen/boff/res are valid in the slice (no headers, no original order).
int calib_db_slice(const Db *db, uint64_t stride, uint64_t first, Db *out);
void calib_db_slice_free(Db *s);
// Index of the set to use: the fastest median, unless the default's median is within `margin` of it.
int calib_choose(const double *median, int n, int def, double margin);
int calibrate(const Db *db, const Matrix *m, Gaps g, const Topology *t, double budget, double margin, int rounds,
              Calib *c, char *err, size_t errlen);
void calib_print(FILE *f, const Calib *c);

// ---------------------------------------------------------------- json.c
void json_str(FILE *f, const char *s);
void json_strn(FILE *f, const char *s, size_t n);

#endif
