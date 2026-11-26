#ifndef PE_IMP_JSON_H
#define PE_IMP_JSON_H
/* A small JSON reader and writer for the importer's own files (store metadata, catalogs, naming
 * tables, bindings, recipes). Self-contained: the PS5 build has no cJSON. The reader is bounded
 * (size, depth, node count) because shared recipes are outside input. Numbers are integers. */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

enum { PJ_NULL, PJ_BOOL, PJ_INT, PJ_STRING, PJ_ARRAY, PJ_OBJECT };

typedef struct PjNode {
    int kind;
    long long i;                 /* PJ_INT, PJ_BOOL */
    char *s;                     /* PJ_STRING; for object members, `key` too */
    char *key;
    struct PjNode *child, *next; /* PJ_ARRAY / PJ_OBJECT: first child, then siblings */
    int count;
} PjNode;

#define PJ_TEXT_MAX (32u << 20)   /* a game's catalog.json: RE2's three discs with room sounds pass 4.5 MB */
#define PJ_DEPTH_MAX 32
#define PJ_NODES_MAX 4000000

PjNode *pj_parse(const char *text, size_t len, char *why, size_t n);   /* NULL on error */
void pj_free(PjNode *root);
const PjNode *pj_get(const PjNode *obj, const char *key);             /* NULL if absent */
const PjNode *pj_at(const PjNode *arr, int i);
long long pj_int(const PjNode *n, long long fallback);
const char *pj_str(const PjNode *n, const char *fallback);

/* Writing: plain fprintf with escaped strings. */
void pj_write_string(FILE *f, const char *s);

/* Whole files. pj_load returns NULL (and a reason) if missing, too large or malformed. */
PjNode *pj_load(const char *path, char *why, size_t n);

#endif
