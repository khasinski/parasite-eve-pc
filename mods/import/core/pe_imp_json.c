/* See pe_imp_json.h. Recursive descent with explicit limits; strings are UTF-8 with \uXXXX
 * escapes (surrogate pairs combined); numbers must be integers. */
#include <stdlib.h>
#include <string.h>

#include "pe_imp_json.h"

typedef struct {
    const char *p, *end;
    int nodes;
    char *why;
    size_t n;
    int failed;
} Parser;

static void err(Parser *ps, const char *what) {
    if (!ps->failed && ps->why && ps->n) snprintf(ps->why, ps->n, "JSON: %s", what);
    ps->failed = 1;
}

static void ws(Parser *ps) {
    while (ps->p < ps->end && (*ps->p == ' ' || *ps->p == '\t' || *ps->p == '\r' || *ps->p == '\n')) ps->p++;
}

static PjNode *node(Parser *ps, int kind) {
    if (++ps->nodes > PJ_NODES_MAX) { err(ps, "too many values"); return NULL; }
    PjNode *x = calloc(1, sizeof *x);
    if (!x) { err(ps, "out of memory"); return NULL; }
    x->kind = kind;
    return x;
}

static int hex4(const char *p, unsigned *out) {
    unsigned v = 0;
    for (int i = 0; i < 4; i++) {
        char c = p[i];
        v <<= 4;
        if (c >= '0' && c <= '9') v |= (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f') v |= (unsigned)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= (unsigned)(c - 'A' + 10);
        else return -1;
    }
    *out = v;
    return 0;
}

static char *string(Parser *ps) {
    if (ps->p >= ps->end || *ps->p != '"') { err(ps, "expected a string"); return NULL; }
    ps->p++;
    size_t cap = 16, len = 0;
    char *out = malloc(cap);
    if (!out) { err(ps, "out of memory"); return NULL; }
    while (ps->p < ps->end && *ps->p != '"') {
        unsigned c = (unsigned char)*ps->p++;
        char buf[4];
        int k = 1;
        if (c < 0x20) { err(ps, "control character in a string"); free(out); return NULL; }
        if (c == '\\') {
            if (ps->p >= ps->end) break;
            char e = *ps->p++;
            switch (e) {
            case '"': case '\\': case '/': c = (unsigned char)e; break;
            case 'b': c = 8; break;
            case 'f': c = 12; break;
            case 'n': c = 10; break;
            case 'r': c = 13; break;
            case 't': c = 9; break;
            case 'u': {
                unsigned u;
                if (ps->end - ps->p < 4 || hex4(ps->p, &u)) { err(ps, "bad \\u escape"); free(out); return NULL; }
                ps->p += 4;
                if (u >= 0xD800 && u < 0xDC00) {
                    unsigned lo;
                    if (ps->end - ps->p < 6 || ps->p[0] != '\\' || ps->p[1] != 'u' || hex4(ps->p + 2, &lo) ||
                        lo < 0xDC00 || lo > 0xDFFF) { err(ps, "lone surrogate"); free(out); return NULL; }
                    ps->p += 6;
                    u = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00);
                } else if (u >= 0xDC00 && u < 0xE000) { err(ps, "lone surrogate"); free(out); return NULL; }
                if (!u) { err(ps, "NUL in a string"); free(out); return NULL; }
                if (u < 0x80) buf[0] = (char)u;
                else if (u < 0x800) buf[0] = (char)(0xC0 | u >> 6), buf[1] = (char)(0x80 | (u & 63)), k = 2;
                else if (u < 0x10000) buf[0] = (char)(0xE0 | u >> 12), buf[1] = (char)(0x80 | ((u >> 6) & 63)),
                                      buf[2] = (char)(0x80 | (u & 63)), k = 3;
                else buf[0] = (char)(0xF0 | u >> 18), buf[1] = (char)(0x80 | ((u >> 12) & 63)),
                     buf[2] = (char)(0x80 | ((u >> 6) & 63)), buf[3] = (char)(0x80 | (u & 63)), k = 4;
                goto put;
            }
            default: err(ps, "bad escape"); free(out); return NULL;
            }
        }
        buf[0] = (char)c;
    put:
        if (len + (size_t)k + 1 > cap) {
            while (len + (size_t)k + 1 > cap) cap *= 2;
            char *g = realloc(out, cap);
            if (!g) { err(ps, "out of memory"); free(out); return NULL; }
            out = g;
        }
        memcpy(out + len, buf, (size_t)k);
        len += (size_t)k;
    }
    if (ps->p >= ps->end) { err(ps, "unterminated string"); free(out); return NULL; }
    ps->p++;
    out[len] = 0;
    return out;
}

static PjNode *value(Parser *ps, int depth);

static PjNode *container(Parser *ps, int depth, int obj) {
    PjNode *x = node(ps, obj ? PJ_OBJECT : PJ_ARRAY), **tail;
    if (!x) return NULL;
    tail = &x->child;
    ps->p++;
    ws(ps);
    if (ps->p < ps->end && *ps->p == (obj ? '}' : ']')) { ps->p++; return x; }
    for (;;) {
        char *key = NULL;
        ws(ps);
        if (obj) {
            if (!(key = string(ps))) break;
            ws(ps);
            if (ps->p >= ps->end || *ps->p != ':') { err(ps, "expected ':'"); free(key); break; }
            ps->p++;
        }
        PjNode *v = value(ps, depth + 1);
        if (!v) { free(key); break; }
        v->key = key;
        *tail = v, tail = &v->next, x->count++;
        ws(ps);
        if (ps->p < ps->end && *ps->p == ',') { ps->p++; continue; }
        if (ps->p < ps->end && *ps->p == (obj ? '}' : ']')) { ps->p++; return x; }
        err(ps, obj ? "expected ',' or '}'" : "expected ',' or ']'");
        break;
    }
    pj_free(x);
    return NULL;
}

static PjNode *value(Parser *ps, int depth) {
    if (depth > PJ_DEPTH_MAX) { err(ps, "nested too deeply"); return NULL; }
    ws(ps);
    if (ps->p >= ps->end) { err(ps, "unexpected end"); return NULL; }
    char c = *ps->p;
    if (c == '{' || c == '[') return container(ps, depth, c == '{');
    if (c == '"') {
        char *s = string(ps);
        if (!s) return NULL;
        PjNode *x = node(ps, PJ_STRING);
        if (!x) { free(s); return NULL; }
        x->s = s;
        return x;
    }
    if (c == '-' || (c >= '0' && c <= '9')) {
        int neg = c == '-';
        if (neg) ps->p++;
        if (ps->p >= ps->end || *ps->p < '0' || *ps->p > '9') { err(ps, "bad number"); return NULL; }
        unsigned long long v = 0;
        while (ps->p < ps->end && *ps->p >= '0' && *ps->p <= '9') {
            if (v > 922337203685477580ULL) { err(ps, "number too large"); return NULL; }
            v = v * 10 + (unsigned)(*ps->p++ - '0');
        }
        if (ps->p < ps->end && (*ps->p == '.' || *ps->p == 'e' || *ps->p == 'E')) { err(ps, "only whole numbers"); return NULL; }
        PjNode *x = node(ps, PJ_INT);
        if (x) x->i = neg ? -(long long)v : (long long)v;
        return x;
    }
    static const struct { const char *word; int kind; long long i; } lit[] = {
        {"true", PJ_BOOL, 1}, {"false", PJ_BOOL, 0}, {"null", PJ_NULL, 0}};
    for (int k = 0; k < 3; k++) {
        size_t l = strlen(lit[k].word);
        if ((size_t)(ps->end - ps->p) >= l && !memcmp(ps->p, lit[k].word, l)) {
            ps->p += l;
            PjNode *x = node(ps, lit[k].kind);
            if (x) x->i = lit[k].i;
            return x;
        }
    }
    err(ps, "unexpected character");
    return NULL;
}

PjNode *pj_parse(const char *text, size_t len, char *why, size_t n) {
    if (len > PJ_TEXT_MAX) {
        if (why && n) snprintf(why, n, "JSON: too large");
        return NULL;
    }
    Parser ps = {text, text + len, 0, why, n, 0};
    if (len >= 3 && !memcmp(text, "\xEF\xBB\xBF", 3)) ps.p += 3;          /* UTF-8 BOM */
    PjNode *x = value(&ps, 0);
    if (x) {
        ws(&ps);
        if (ps.p != ps.end) { err(&ps, "text after the value"); pj_free(x); x = NULL; }
    }
    return x;
}

void pj_free(PjNode *x) {
    while (x) {
        PjNode *next = x->next;
        pj_free(x->child);
        free(x->s), free(x->key), free(x);
        x = next;
    }
}

const PjNode *pj_get(const PjNode *o, const char *key) {
    if (!o || o->kind != PJ_OBJECT) return NULL;
    for (const PjNode *c = o->child; c; c = c->next)
        if (c->key && !strcmp(c->key, key)) return c;
    return NULL;
}

const PjNode *pj_at(const PjNode *a, int i) {
    if (!a || a->kind != PJ_ARRAY || i < 0) return NULL;
    const PjNode *c = a->child;
    while (c && i--) c = c->next;
    return c;
}

long long pj_int(const PjNode *x, long long fallback) { return x && x->kind == PJ_INT ? x->i : fallback; }
const char *pj_str(const PjNode *x, const char *fallback) { return x && x->kind == PJ_STRING ? x->s : fallback; }

void pj_write_string(FILE *f, const char *s) {
    fputc('"', f);
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') fprintf(f, "\\%c", c);
        else if (c < 0x20) fprintf(f, "\\u%04x", c);
        else fputc(c, f);
    }
    fputc('"', f);
}

PjNode *pj_load(const char *path, char *why, size_t n) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        if (why && n) snprintf(why, n, "can't open %s", path);
        return NULL;
    }
    long size = fseek(f, 0, SEEK_END) ? -1 : ftell(f);   /* the file's own size, up to the limit (+1 tells "too large") */
    if (size < 0 || fseek(f, 0, SEEK_SET)) size = PJ_TEXT_MAX;
    const size_t want = (size_t)size > PJ_TEXT_MAX ? PJ_TEXT_MAX + 1 : (size_t)size + 1;
    char *buf = malloc(want);
    size_t len = buf ? fread(buf, 1, want, f) : 0;
    fclose(f);
    if (!buf) return NULL;
    PjNode *x = NULL;
    if (len > PJ_TEXT_MAX) {
        if (why && n) snprintf(why, n, "%s is too large", path);
    } else {
        x = pj_parse(buf, len, why, n);
    }
    free(buf);
    return x;
}
