// Database preparation: sort sequences by length (longest first), group them into batches of 16 and
// store each batch "interleaved" (residue j of all 16 lanes side by side) so the SIMD kernel reads one
// 16-byte column per step. Written once to a binary file and opened later with mmap.
//
// File layout (native little-endian), every section starts on a 64-byte boundary:
//   FileHeader | len[nseq] u32 | orig[nseq] u32 | blen[nbatch] u32 | boff[nbatch] u64 |
//   hoff[nseq+1] u64 | res[cells] u8 | hdr[hdr_bytes]
#include "hysw.h"
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAGIC "HYSWDB1"

typedef struct {
    char magic[8];
    uint32_t version, lanes;
    uint64_t nseq, nres, nbatch, cells;
    uint32_t maxlen, reserved;
    uint64_t hdr_bytes;
} FileHeader;

static uint64_t up64(uint64_t x) { return (x + 63) & ~(uint64_t)63; }

typedef struct { uint64_t len, orig, blen, boff, hoff, res, hdr, end; } Layout;

static Layout layout(uint64_t nseq, uint64_t nbatch, uint64_t cells, uint64_t hdr_bytes) {
    Layout L;
    L.len = up64(sizeof(FileHeader));
    L.orig = up64(L.len + 4 * nseq);
    L.blen = up64(L.orig + 4 * nseq);
    L.boff = up64(L.blen + 4 * nbatch);
    L.hoff = up64(L.boff + 8 * nbatch);
    L.res = up64(L.hoff + 8 * (nseq + 1));
    L.hdr = up64(L.res + cells);
    L.end = L.hdr + hdr_bytes;
    return L;
}

static void set_pointers(Db *db, unsigned char *base, const Layout *L) {
    db->len = (uint32_t *)(base + L->len);
    db->orig = (uint32_t *)(base + L->orig);
    db->blen = (uint32_t *)(base + L->blen);
    db->boff = (uint64_t *)(base + L->boff);
    db->hoff = (uint64_t *)(base + L->hoff);
    db->res = base + L->res;
    db->hdr = (char *)(base + L->hdr);
}

static const SeqSet *sort_set;
static int by_length_desc(const void *a, const void *b) {
    int x = *(const int *)a, y = *(const int *)b;
    int lx = sort_set->len[x], ly = sort_set->len[y];
    if (lx != ly) return ly - lx;
    return x - y;                       // ties: keep FASTA order (deterministic)
}

int db_build(const SeqSet *s, Db *db, char *err, size_t errlen) {
    memset(db, 0, sizeof *db);
    uint64_t nseq = (uint64_t)s->n, nbatch = (nseq + HYSW_LANES - 1) / HYSW_LANES;
    int *ord = malloc(sizeof(int) * nseq);
    for (uint64_t k = 0; k < nseq; k++) ord[k] = (int)k;
    sort_set = s;
    qsort(ord, nseq, sizeof(int), by_length_desc);
    uint64_t cells = 0, hdr_bytes = 0;
    for (uint64_t b = 0; b < nbatch; b++) cells += (uint64_t)s->len[ord[b * HYSW_LANES]] * HYSW_LANES;
    for (uint64_t k = 0; k < nseq; k++) hdr_bytes += strlen(s->hdr[k]);
    Layout L = layout(nseq, nbatch, cells, hdr_bytes);
    unsigned char *base = aligned_alloc(64, up64(L.end));
    if (!base) { free(ord); snprintf(err, errlen, "out of memory building database (%lu bytes)", (unsigned long)L.end); return -1; }
    memset(base, 0, sizeof(FileHeader));
    FileHeader *h = (FileHeader *)base;
    memcpy(h->magic, MAGIC, 8);
    h->version = 1; h->lanes = HYSW_LANES; h->nseq = nseq; h->nbatch = nbatch; h->cells = cells; h->hdr_bytes = hdr_bytes;
    set_pointers(db, base, &L);
    uint64_t off = 0, hpos = 0, nres = 0;
    for (uint64_t k = 0; k < nseq; k++) {
        int o = ord[k];
        db->len[k] = (uint32_t)s->len[o];
        db->orig[k] = (uint32_t)o;
        size_t hl = strlen(s->hdr[o]);
        db->hoff[k] = hpos; memcpy(db->hdr + hpos, s->hdr[o], hl); hpos += hl;
        nres += (uint64_t)s->len[o];
    }
    db->hoff[nseq] = hpos;
    for (uint64_t b = 0; b < nbatch; b++) {
        uint32_t bl = db->len[b * HYSW_LANES];
        db->blen[b] = bl; db->boff[b] = off;
        unsigned char *col = db->res + off;
        memset(col, HYSW_PAD, (size_t)bl * HYSW_LANES);
        for (int l = 0; l < HYSW_LANES; l++) {
            uint64_t k = b * HYSW_LANES + l;
            if (k >= nseq) break;
            const unsigned char *src = s->seq[ord[k]];
            for (uint32_t j = 0; j < db->len[k]; j++) col[(size_t)j * HYSW_LANES + l] = src[j];
        }
        off += (uint64_t)bl * HYSW_LANES;
    }
    h->nres = nres; h->maxlen = nseq ? db->len[0] : 0;
    db->nseq = nseq; db->nres = nres; db->nbatch = nbatch; db->cells = cells; db->maxlen = h->maxlen;
    db->own = base;
    free(ord);
    return 0;
}

int db_write(const Db *db, const char *path, char *err, size_t errlen) {
    // In-memory databases are one contiguous block in file layout, so writing is a single copy.
    if (!db->own) { snprintf(err, errlen, "db_write: database is not built in memory"); return -1; }
    Layout L = layout(db->nseq, db->nbatch, db->cells, db->hoff[db->nseq]);
    char tmp[4096];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f) { snprintf(err, errlen, "cannot write %s", tmp); return -1; }
    size_t w = fwrite(db->own, 1, L.end, f);
    if (fclose(f) != 0 || w != L.end) { snprintf(err, errlen, "write failed for %s", tmp); remove(tmp); return -1; }
    if (rename(tmp, path) != 0) { snprintf(err, errlen, "cannot rename %s to %s", tmp, path); return -1; }
    return 0;
}

static int ends_with(const char *s, const char *suf) {
    size_t a = strlen(s), b = strlen(suf);
    return a >= b && strcmp(s + a - b, suf) == 0;
}

static void set_name(Db *db, const char *path) {
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    snprintf(db->name, sizeof db->name, "%s", base);
    char *dot = strrchr(db->name, '.');
    if (dot && dot != db->name) *dot = 0;
}

int db_open(const char *path, Db *db, char *err, size_t errlen) {
    memset(db, 0, sizeof *db);
    if (!ends_with(path, ".bin")) {                  // FASTA: build in memory
        SeqSet s;
        if (fasta_read(path, &s, err, errlen)) return -1;
        int r = db_build(&s, db, err, errlen);
        seqset_free(&s);
        if (!r) set_name(db, path);
        return r;
    }
    int fd = open(path, O_RDONLY);
    if (fd < 0) { snprintf(err, errlen, "cannot open database %s", path); return -1; }
    struct stat st;
    if (fstat(fd, &st) || (size_t)st.st_size < sizeof(FileHeader)) { close(fd); snprintf(err, errlen, "%s: too small", path); return -1; }
    void *map = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (map == MAP_FAILED) { snprintf(err, errlen, "cannot mmap %s", path); return -1; }
    const FileHeader *h = map;
    if (memcmp(h->magic, MAGIC, 8) || h->version != 1 || h->lanes != HYSW_LANES) {
        munmap(map, st.st_size); snprintf(err, errlen, "%s: not a HySW database (or wrong version)", path); return -1;
    }
    Layout L = layout(h->nseq, h->nbatch, h->cells, h->hdr_bytes);
    if (L.end != (uint64_t)st.st_size) { munmap(map, st.st_size); snprintf(err, errlen, "%s: size mismatch (truncated?)", path); return -1; }
    set_pointers(db, map, &L);
    db->nseq = h->nseq; db->nres = h->nres; db->nbatch = h->nbatch; db->cells = h->cells; db->maxlen = h->maxlen;
    db->map = map; db->maplen = st.st_size;
    madvise(map, st.st_size, MADV_WILLNEED);
    set_name(db, path);
    return 0;
}

void db_close(Db *db) {
    if (db->map) munmap(db->map, db->maplen);
    free(db->own);
    memset(db, 0, sizeof *db);
}

void db_get_seq(const Db *db, uint64_t k, unsigned char *out) {
    uint64_t b = k / HYSW_LANES, l = k % HYSW_LANES;
    const unsigned char *col = db->res + db->boff[b] + l;
    for (uint32_t j = 0; j < db->len[k]; j++) out[j] = col[(size_t)j * HYSW_LANES];
}

int db_header(const Db *db, uint64_t k, char *out, size_t outlen) {
    size_t n = db->hoff[k + 1] - db->hoff[k];
    if (n >= outlen) n = outlen - 1;
    memcpy(out, db->hdr + db->hoff[k], n);
    out[n] = 0;
    return (int)n;
}
