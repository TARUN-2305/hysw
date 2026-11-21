// Minimal JSON string output (the engine only writes JSON, it never parses it).
#include "hysw.h"
#include <string.h>

void json_strn(FILE *f, const char *s, size_t n) {
    fputc('"', f);
    for (size_t k = 0; k < n && s[k]; k++) {
        unsigned char c = (unsigned char)s[k];
        if (c == '"' || c == '\\') { fputc('\\', f); fputc(c, f); }
        else if (c == '\n') fputs("\\n", f);
        else if (c == '\t') fputs("\\t", f);
        else if (c < 0x20) fprintf(f, "\\u%04x", c);
        else fputc(c, f);
    }
    fputc('"', f);
}

void json_str(FILE *f, const char *s) { json_strn(f, s ? s : "", s ? strlen(s) : 0); }
