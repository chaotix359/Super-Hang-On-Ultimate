/* Settings file (.ini) for the macOS frontend; portable C.
 * Same rules as the Windows profile functions the Windows build uses (GetPrivateProfileInt/String,
 * WritePrivateProfileString), so both builds read and write shangon.ini the same way. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "ini_file.h"

static char g_ini[1024];
void ini_set_path(const char *path) { snprintf(g_ini, sizeof g_ini, "%s", path); }

/* A small .ini reader/writer with the same rules as the Windows profile functions the Windows build uses:
   [section] and key=value lines, case-insensitive names, "; comment" after a value ignored. Writing a key
   replaces its line (or adds it at the end of its section, or adds the section). */
typedef struct { char **line; int n, cap; } ini_doc;
static void ini_push(ini_doc *d, int at, const char *s) {
    if (d->n == d->cap) { d->cap = d->cap ? d->cap * 2 : 64; d->line = realloc(d->line, sizeof(char *) * (size_t)d->cap); }
    memmove(d->line + at + 1, d->line + at, sizeof(char *) * (size_t)(d->n - at));
    d->line[at] = strdup(s); d->n++;
}
static void ini_read(ini_doc *d) {
    memset(d, 0, sizeof *d);
    FILE *f = fopen(g_ini, "r"); if (!f) return;
    char b[2048];
    while (fgets(b, sizeof b, f)) { size_t l = strlen(b); while (l && (b[l - 1] == '\n' || b[l - 1] == '\r')) b[--l] = 0; ini_push(d, d->n, b); }
    fclose(f);
}
static void ini_free(ini_doc *d) { for (int i = 0; i < d->n; i++) free(d->line[i]); free(d->line); memset(d, 0, sizeof *d); }
static const char *skip_ws(const char *s) { while (*s == ' ' || *s == '\t') s++; return s; }
static int line_section(const char *l, char *name, size_t n) {   /* 1 = "[name]" */
    l = skip_ws(l); if (*l != '[') return 0;
    const char *e = strchr(l, ']'); if (!e) return 0;
    size_t k = (size_t)(e - l - 1); if (k >= n) k = n - 1; memcpy(name, l + 1, k); name[k] = 0; return 1;
}
static const char *line_key(const char *l, const char *key) {   /* the value text if this is key=..., else NULL */
    l = skip_ws(l); if (*l == ';' || *l == '#' || *l == '[') return NULL;
    const char *eq = strchr(l, '='); if (!eq) return NULL;
    const char *ke = eq; while (ke > l && (ke[-1] == ' ' || ke[-1] == '\t')) ke--;
    size_t kl = (size_t)(ke - l);
    if (kl != strlen(key) || strncasecmp(l, key, kl) != 0) return NULL;
    return eq + 1;
}
/* finds the key: returns its line index (or -1); *sec_end = index after the section's last line (-1 = no section) */
static int ini_find(const ini_doc *d, const char *sec, const char *key, int *sec_end) {
    int in = 0, found = -1; char name[128]; *sec_end = -1;
    for (int i = 0; i < d->n; i++) {
        if (line_section(d->line[i], name, sizeof name)) { in = strcasecmp(name, sec) == 0; if (in) *sec_end = i + 1; continue; }
        if (!in) continue;
        if (d->line[i][0]) *sec_end = i + 1;
        if (found < 0 && line_key(d->line[i], key)) found = i;
    }
    return found;
}
static int ini_get(const char *sec, const char *key, char *out, size_t n) {
    ini_doc d; ini_read(&d); int se, i = ini_find(&d, sec, key, &se), ok = 0;
    if (i >= 0) {
        const char *v = skip_ws(line_key(d.line[i], key));
        snprintf(out, n, "%s", v);
        char *c = strchr(out, ';'); if (c) *c = 0;
        size_t l = strlen(out); while (l && (out[l - 1] == ' ' || out[l - 1] == '\t')) out[--l] = 0;
        ok = 1;
    }
    ini_free(&d);
    return ok;
}
int ini_int(const char *sec, const char *key, int def) { char b[64]; return ini_get(sec, key, b, sizeof b) ? (int)strtol(b, NULL, 10) : def; }
void ini_str(const char *sec, const char *key, const char *def, char *out, int n) { if (!ini_get(sec, key, out, (size_t)n)) snprintf(out, (size_t)n, "%s", def); }
void ini_set(const char *sec, const char *key, const char *val) {
    ini_doc d; ini_read(&d); int se, i = ini_find(&d, sec, key, &se);
    char l[512]; snprintf(l, sizeof l, "%s=%s", key, val);
    if (i >= 0) { free(d.line[i]); d.line[i] = strdup(l); }
    else if (se >= 0) ini_push(&d, se, l);
    else { char h[160]; snprintf(h, sizeof h, "[%s]", sec); ini_push(&d, d.n, h); ini_push(&d, d.n, l); }
    FILE *f = fopen(g_ini, "w");
    if (f) { for (int k = 0; k < d.n; k++) { fputs(d.line[k], f); fputc('\n', f); } fclose(f); }
    ini_free(&d);
}
void ini_set_int(const char *sec, const char *key, int v) { char b[16]; snprintf(b, sizeof b, "%d", v); ini_set(sec, key, b); }
