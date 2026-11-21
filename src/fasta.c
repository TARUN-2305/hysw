// FASTA reading and residue encoding.
// Letters are upper-cased and mapped to codes in HYSW_ALPHABET order; letters outside the alphabet
// (U, O, J) become X. Non-letters inside sequence lines (digits, spaces, '*', '-') are ignored.
#include "hysw.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static signed char code_of[256];
static int code_init;

static void init_codes(void) {
    for (int c = 0; c < 256; c++) code_of[c] = -1;
    for (int k = 0; HYSW_ALPHABET[k]; k++) code_of[(unsigned char)HYSW_ALPHABET[k]] = (signed char)k;
    code_init = 1;
}

unsigned char hysw_encode(int c) {
    if (!code_init) init_codes();
    c = toupper(c & 0xff);
    int k = code_of[c];
    return k < 0 ? HYSW_X : (unsigned char)k;
}

char hysw_decode(unsigned char code) { return code < HYSW_NCODES ? HYSW_ALPHABET[code] : '?'; }

static int in_alphabet(int c) {
    if (!code_init) init_codes();
    return code_of[toupper(c & 0xff)] >= 0;
}

void seqset_free(SeqSet *s) {
    for (int k = 0; k < s->n; k++) { free(s->seq[k]); free(s->hdr[k]); }
    free(s->seq); free(s->hdr); free(s->len); free(s->nunknown);
    memset(s, 0, sizeof *s);
}

int fasta_read(const char *path, SeqSet *out, char *err, size_t errlen) {
    memset(out, 0, sizeof *out);
    FILE *f = fopen(path, "r");
    if (!f) { snprintf(err, errlen, "cannot open %s", path); return -1; }
    int cap = 0, cur = -1, scap = 0;
    char *line = NULL; size_t lcap = 0; ssize_t n;
    long lineno = 0;
    while ((n = getline(&line, &lcap, f)) >= 0) {
        lineno++;
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;
        if (line[0] == '>') {
            if (out->n == cap) {
                cap = cap ? cap * 2 : 1024;
                out->seq = realloc(out->seq, sizeof(*out->seq) * cap);
                out->hdr = realloc(out->hdr, sizeof(*out->hdr) * cap);
                out->len = realloc(out->len, sizeof(int) * cap);
                out->nunknown = realloc(out->nunknown, sizeof(int) * cap);
            }
            cur = out->n++;
            out->hdr[cur] = strdup(line + 1);
            out->seq[cur] = NULL; out->len[cur] = 0; out->nunknown[cur] = 0; scap = 0;
            continue;
        }
        int has_letters = 0;
        for (char *p = line; *p; p++) if (isalpha((unsigned char)*p)) { has_letters = 1; break; }
        if (!has_letters) continue;
        if (cur < 0) {
            snprintf(err, errlen, "%s line %ld: sequence data before the first '>' header (not FASTA)", path, lineno);
            free(line); fclose(f); seqset_free(out); return -1;
        }
        for (char *p = line; *p; p++) {
            if (!isalpha((unsigned char)*p)) continue;
            if (out->len[cur] == scap) { scap = scap ? scap * 2 : 256; out->seq[cur] = realloc(out->seq[cur], scap); }
            if (!in_alphabet(*p)) out->nunknown[cur]++;
            out->seq[cur][out->len[cur]++] = hysw_encode(*p);
            out->nres++;
        }
    }
    free(line);
    fclose(f);
    if (out->n == 0) { snprintf(err, errlen, "%s: no FASTA records found", path); return -1; }
    return 0;
}
