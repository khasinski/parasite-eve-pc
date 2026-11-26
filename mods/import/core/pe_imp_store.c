/* The extract-once store and the catalog.
 *
 *   <root>/<game>/<sha256>/source.json   the file it came from, the reader and its version
 *   <root>/<game>/<sha256>/items.json    [{"id": proposed, "name", "type", "dir": "0"}] (written last)
 *   <root>/<game>/<sha256>/<dir>/...     each item as its type stores itself
 *   <root>/<game>/catalog.json           {"<game>:<id>": {"name", "type", "tags", "sources": [...]}}
 *
 * The store holds another game's data: it lives in the player's preferences and is never shared.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#define MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define MKDIR(p) mkdir(p, 0755)
#endif

#include "pe_imp_internal.h"
#include "pe_imp_logic.h"
#include "pe_imp_pictures.h"

#define MAX_ITEMS 4096
#define MAX_ENTRIES 20000
#define MAX_SOURCES 16

int pe_imp_mkdirs(const char *path) {
    char p[PE_IMP_PATH_MAX];
    if (snprintf(p, sizeof p, "%s", path) >= (int)sizeof p) return -1;
    for (char *s = p + 1; *s; s++) {
        if (*s != '/' && *s != '\\') continue;
        char c = *s;
        *s = 0;
        if (!(s > p && s[-1] == ':')) MKDIR(p);
        *s = c;
    }
    MKDIR(p);
    FILE *probe;
    char test[PE_IMP_PATH_MAX];
    snprintf(test, sizeof test, "%s/.probe", p);
    if (!(probe = fopen(test, "wb"))) return -1;
    fclose(probe);
    remove(test);
    return 0;
}

/* ---- the sink a reader emits into ---- */
typedef struct { uint32_t type; char id[PE_IMP_ID_MAX], name[PE_IMP_NAME_MAX]; void *obj; int storage; } Item;
struct PeImpSink { Item item[MAX_ITEMS]; int items, objects; };

static unsigned schema_version(uint32_t type) {
    switch (type) {
    case PE_IMP_MODEL: return PE_IMP_MODEL_VERSION;
    case PE_IMP_SOUND: return PE_IMP_SOUND_VERSION;
    case PE_IMP_SONG: return PE_IMP_SONG_VERSION;
    case PE_IMP_IMAGESET: return PE_IMP_IMAGESET_VERSION;
    case PE_IMP_ANIMATION: return PE_IMP_ANIMATION_VERSION;
    case PE_IMP_ROOMLOGIC: return PE_IMP_ROOMLOGIC_VERSION;
    case PE_IMP_DOORSCENE: return PE_IMP_DOORSCENE_VERSION;
    case PE_IMP_ITEMTABLE: return PE_IMP_ITEMTABLE_VERSION;
    case PE_IMP_MOVIE: return PE_IMP_MOVIE_VERSION;
    case PE_IMP_PICTURES: return PE_IMP_PICTURES_VERSION;
    default: return 0;
    }
}

/* Keep the original Model stamp so existing costume catalogs are still current. */
static const char *schema_key(uint32_t types) {
    if (types == PE_IMP_MODEL) return "model_version";
    if (types == PE_IMP_SOUND) return "sound_version";
    if (types == PE_IMP_SONG) return "song_version";
    if (types == PE_IMP_IMAGESET) return "imageset_version";
    return "schema_versions";
}

static unsigned schema_stamp(uint32_t types) {
    if (types == PE_IMP_MODEL || types == PE_IMP_SOUND || types == PE_IMP_SONG || types == PE_IMP_IMAGESET) return schema_version(types);
    unsigned stamp = 0;
    for (uint32_t type = PE_IMP_MODEL; type <= PE_IMP_PICTURES; type <<= 1)
        if (types & type) stamp = (stamp * 31 + type + schema_version(type)) & 0x7fffffff;
    return stamp;
}

static uint32_t stored_type(const char *name) {
    for (uint32_t type = PE_IMP_MODEL; type <= PE_IMP_PICTURES; type <<= 1)
        if (schema_version(type) && !strcmp(name, pe_imp_type_name(type))) return type;
    return 0;
}

static int save_obj(const Item *it, const char *folder, char *why, size_t n) {
    switch (it->type) {
    case PE_IMP_MODEL: return pe_imp_model_save(it->obj, folder, why, n);
    case PE_IMP_SOUND: return pe_imp_sound_save(it->obj, folder, why, n);
    case PE_IMP_SONG: return pe_imp_song_save(it->obj, folder, why, n);
    case PE_IMP_IMAGESET: return pe_imp_imageset_save(it->obj, folder, why, n);
    case PE_IMP_ANIMATION: return pe_imp_animation_save(it->obj, folder, why, n);
    case PE_IMP_ROOMLOGIC: return pe_imp_logic_save(it->obj, folder, why, n);
    case PE_IMP_DOORSCENE: return pe_imp_door_save(it->obj, folder, why, n);
    case PE_IMP_ITEMTABLE: return pe_imp_items_save(it->obj, folder, why, n);
    case PE_IMP_MOVIE: return pe_imp_movie_save(it->obj, folder, why, n);
    case PE_IMP_PICTURES: return pe_imp_pictures_save(it->obj, folder, why, n);
    default: return -1;
    }
}

static void free_obj(uint32_t type, void *obj) {
    if (!obj) return;
    if (type == PE_IMP_MODEL) pe_imp_model_free(obj);
    if (type == PE_IMP_ANIMATION) pe_imp_animation_free(obj);
    if (type == PE_IMP_MOVIE) { pe_imp_movie_free(obj); return; }
    free(obj);
}

static int local_id_ok(const char *s) {          /* "player/pl01": lower case, at least one '/' */
    int slash = 0;
    if (!*s || *s == '/' || strlen(s) >= PE_IMP_ID_MAX - 17) return 0;
    for (const char *c = s; *c; c++) {
        if (*c == '/') { if (c[1] == '/' || !c[1]) return 0; slash = 1; continue; }
        if (!((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '_' || *c == '-' || *c == '.' || *c == '$'))
            return 0;
        if (*c == '.' && c[1] == '.') return 0;
    }
    return slash;
}

int pe_imp_emit(PeImpSink *s, uint32_t type, const char *proposed, const char *name, void *obj) {
    if (!s || s->items == MAX_ITEMS || !proposed || !local_id_ok(proposed) || !obj || !schema_version(type)) {
        free_obj(type, obj);
        return -1;
    }
    Item *it = &s->item[s->items++];
    it->type = type, it->obj = obj;
    it->storage = s->objects++;
    snprintf(it->id, sizeof it->id, "%s", proposed);
    snprintf(it->name, sizeof it->name, "%s", name ? name : proposed);
    return 0;
}

int pe_imp_emit_alias(PeImpSink *s, const char *proposed, const char *name) {
    if (!s || !s->items || s->items == MAX_ITEMS || !proposed || !local_id_ok(proposed)) return -1;
    Item *previous = &s->item[s->items - 1];
    Item *alias = &s->item[s->items++];
    alias->type = previous->type;
    alias->storage = previous->storage;
    snprintf(alias->id, sizeof alias->id, "%s", proposed);
    snprintf(alias->name, sizeof alias->name, "%s", name ? name : proposed);
    return 0;
}

static void sink_free(PeImpSink *s) {
    for (int i = 0; i < s->items; i++) free_obj(s->item[i].type, s->item[i].obj);
    s->items = 0;
}

/* ---- the catalog, in memory ---- */
typedef struct { char sha[65], dir[8], platform[16], image[128], path[128]; } Source;
typedef struct {
    char id[PE_IMP_ID_MAX], name[PE_IMP_NAME_MAX], type[16], tags[64];
    Source *src;                                  /* srcs of them (up to MAX_SOURCES), grown as noted */
    int srcs, scap;
} Entry;

static Source *src_add(Entry *e) {
    if (e->srcs >= MAX_SOURCES) return NULL;
    if (e->srcs == e->scap) {
        int cap = e->scap ? e->scap * 2 : 2;
        Source *g = realloc(e->src, (size_t)cap * sizeof *g);
        if (!g) return NULL;
        e->src = g, e->scap = cap;
    }
    return &e->src[e->srcs++];
}
/* hash: indices + 1 into e by ID (0: empty), so finding one isn't a walk of every entry; a mixed
 * store's catalog has some 10,000, and loading it walked them once per entry (about a second) */
typedef struct { Entry *e; int n, cap; int *hash; int hcap; } Catalog;

static uint32_t id_hash(const char *id) {
    uint32_t h = 2166136261u;
    for (; *id; id++) h = (h ^ (uint8_t)*id) * 16777619u;
    return h;
}

static void hash_put(Catalog *c, int i) {
    for (uint32_t k = id_hash(c->e[i].id) & (uint32_t)(c->hcap - 1);; k = (k + 1) & (uint32_t)(c->hcap - 1))
        if (!c->hash[k]) { c->hash[k] = i + 1; return; }
}

/* (re)build the index for n entries: it stays at most half full */
static void hash_build(Catalog *c) {
    int cap = 1024;
    while (cap < c->n * 2 + 2) cap *= 2;
    int *h = calloc((size_t)cap, sizeof *h);
    free(c->hash);
    c->hash = h, c->hcap = h ? cap : 0;
    if (h) for (int i = 0; i < c->n; i++) hash_put(c, i);
}

static Entry *cat_find(Catalog *c, const char *id) {
    if (!c->hash) {                                   /* no index (out of memory): walk them */
        for (int i = 0; i < c->n; i++) if (!strcmp(c->e[i].id, id)) return &c->e[i];
        return NULL;
    }
    for (uint32_t k = id_hash(id) & (uint32_t)(c->hcap - 1); c->hash[k]; k = (k + 1) & (uint32_t)(c->hcap - 1))
        if (!strcmp(c->e[c->hash[k] - 1].id, id)) return &c->e[c->hash[k] - 1];
    return NULL;
}

static void cat_free(Catalog *c) {
    for (int i = 0; i < c->n; i++) free(c->e[i].src);
    free(c->e), free(c->hash);
    memset(c, 0, sizeof *c);
}

static void cat_sort(Catalog *c);

static Entry *cat_add(Catalog *c, const char *id) {
    Entry *e = cat_find(c, id);
    if (e) return e;
    if (c->n >= MAX_ENTRIES) return NULL;
    if (c->n == c->cap) {
        int cap = c->cap ? c->cap * 2 : 64;
        Entry *g = realloc(c->e, (size_t)cap * sizeof *g);
        if (!g) return NULL;
        c->e = g, c->cap = cap;
    }
    e = &c->e[c->n++];
    memset(e, 0, sizeof *e);
    snprintf(e->id, sizeof e->id, "%s", id);
    if (!c->hash || c->n * 2 + 2 > c->hcap) hash_build(c);
    else hash_put(c, c->n - 1);
    return e;
}

static void cat_load(Catalog *c, const char *path) {
    memset(c, 0, sizeof *c);
    PjNode *root = pj_load(path, NULL, 0);
    if (!root || root->kind != PJ_OBJECT) { pj_free(root); return; }
    for (const PjNode *x = root->child; x; x = x->next) {
        Entry *e = cat_add(c, x->key);
        if (!e) break;
        snprintf(e->name, sizeof e->name, "%s", pj_str(pj_get(x, "name"), ""));
        snprintf(e->type, sizeof e->type, "%s", pj_str(pj_get(x, "type"), ""));
        snprintf(e->tags, sizeof e->tags, "%s", pj_str(pj_get(x, "tags"), ""));
        const PjNode *ss = pj_get(x, "sources");
        for (int i = 0; i < MAX_SOURCES && pj_at(ss, i); i++) {
            const PjNode *s = pj_at(ss, i);
            Source *d = src_add(e);
            if (!d) break;
            snprintf(d->sha, sizeof d->sha, "%s", pj_str(pj_get(s, "sha256"), ""));
            snprintf(d->dir, sizeof d->dir, "%s", pj_str(pj_get(s, "item"), ""));
            snprintf(d->platform, sizeof d->platform, "%s", pj_str(pj_get(s, "platform"), ""));
            snprintf(d->image, sizeof d->image, "%s", pj_str(pj_get(s, "image"), ""));
            snprintf(d->path, sizeof d->path, "%s", pj_str(pj_get(s, "path"), ""));
        }
    }
    pj_free(root);
}

static int plat_rank(const char *p) { return !strcmp(p, "ps1") ? 0 : !strcmp(p, "pc") ? 1 : 2; }

static int src_cmp(const void *a, const void *b) {
    const Source *x = a, *y = b;
    int r = plat_rank(x->platform) - plat_rank(y->platform);
    if (!r) r = strcmp(x->image, y->image);
    if (!r) r = strcmp(x->path, y->path);
    if (!r) r = strcmp(x->dir, y->dir);
    return r;
}

static int entry_cmp(const void *a, const void *b) { return strcmp(((const Entry *)a)->id, ((const Entry *)b)->id); }

/* by ID; the index is rebuilt, as the entries have moved */
static void cat_sort(Catalog *c) {
    qsort(c->e, (size_t)c->n, sizeof *c->e, entry_cmp);
    if (c->hash) hash_build(c);
}

/* A finished file over the old one in one step. Removing the old file first lost the catalog on
 * Windows when something (a virus scanner) still held the new .tmp: the rename failed after the
 * delete, leaving only catalog.json.tmp ("re2:logic/100-0 is not in the catalog"). Now a holder only
 * delays the swap; if it never lets go, the old catalog stays. */
static int replace_file(const char *from, const char *to) {
#ifdef _WIN32
    for (int tries = 0; tries < 100; tries++) {
        if (MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return 0;
        Sleep(20);
    }
    return -1;
#else
    return rename(from, to);
#endif
}

static volatile unsigned s_catalog_saves;

static int cat_save(Catalog *c, const char *path) {
    char tmp[PE_IMP_PATH_MAX];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f) return -1;
    cat_sort(c);
    fputs("{\n", f);
    int first = 1;
    for (int i = 0; i < c->n; i++) {
        Entry *e = &c->e[i];
        if (!e->srcs) continue;                    /* renamed or gone: nothing points here now */
        fputs(first ? "" : ",\n", f), first = 0;
        qsort(e->src, (size_t)e->srcs, sizeof *e->src, src_cmp);
        fputs(" ", f), pj_write_string(f, e->id), fputs(": {\"name\": ", f), pj_write_string(f, e->name);
        fputs(", \"type\": ", f), pj_write_string(f, e->type), fputs(", \"tags\": ", f), pj_write_string(f, e->tags);
        fputs(", \"sources\": [", f);
        for (int s = 0; s < e->srcs; s++) {
            Source *d = &e->src[s];
            fputs(s ? ", {\"sha256\": " : "{\"sha256\": ", f), pj_write_string(f, d->sha);
            fputs(", \"item\": ", f), pj_write_string(f, d->dir);
            fputs(", \"platform\": ", f), pj_write_string(f, d->platform);
            fputs(", \"image\": ", f), pj_write_string(f, d->image);
            fputs(", \"path\": ", f), pj_write_string(f, d->path), fputs("}", f);
        }
        fputs("]}", f);
    }
    fputs(first ? "}\n" : "\n}\n", f);
    if (fclose(f)) return -1;
    if (replace_file(tmp, path)) return -1;
    s_catalog_saves++;                             /* lookups read it again (catalog_cached) */
    return 0;
}

static void cat_note(Catalog *c, const char *game, const char *proposed, const char *name, uint32_t type,
                     const char *tags, const Source *src) {
    const char *local = proposed, *nm = name;
    pe_imp_rename(game, proposed, &local, &nm);
    if (!local_id_ok(local)) local = proposed;
    char id[PE_IMP_ID_MAX];
    snprintf(id, sizeof id, "%s:%s", game, local);
    Entry *e = cat_add(c, id);
    if (!e) return;
    snprintf(e->name, sizeof e->name, "%s", nm);
    snprintf(e->type, sizeof e->type, "%s", pe_imp_type_name(type));
    snprintf(e->tags, sizeof e->tags, "%s", tags ? tags : "");
    for (int s = 0; s < e->srcs; s++)
        if (!strcmp(e->src[s].sha, src->sha) && !strcmp(e->src[s].dir, src->dir)) { e->src[s] = *src; return; }
    Source *d = src_add(e);
    if (d) *d = *src;
}

/* ---- scanning a disc image ---- */
static int store_file(Catalog *c, const char *root, const PeImpReader *rd, const char *image, const char *path,
                      const uint8_t *data, uint32_t size,
                      const uint8_t *companion, uint32_t companion_size, PeImpScan *out);

/* One file through one reader: read it (and its companion), store what the reader makes. */
static void scan_file(Catalog *cat, const char *root, const char *label, PeImpDisc *d, int i,
                      const PeImpReader *rd, PeImpScan *out) {
    char why[200] = "";
    const char *path = pe_imp_disc_name(d, i);
    uint8_t *data;
    uint32_t size;
    uint8_t *companion = NULL;
    uint32_t companion_size = 0;
    int read_error;
    if (rd->xa == PE_IMP_XA_BOTH)
        read_error = pe_imp_disc_str(d, i, &data, &size, &companion, &companion_size, why, sizeof why);
    else if (rd->xa == PE_IMP_XA_INDEXED) read_error = pe_imp_disc_xa_indexed(d, i, &data, &size, why, sizeof why);
    else if (rd->xa == 1) read_error = pe_imp_disc_xa(d, i, &data, &size, why, sizeof why);
    else read_error = pe_imp_disc_read(d, i, &data, &size, why, sizeof why);
    if (read_error) {
        out->refused++;
        snprintf(out->last_error, sizeof out->last_error, "%s: %s", path, why);
        return;
    }
    if (rd->xa != PE_IMP_XA_BOTH && rd->find_companion) {
        int index = rd->find_companion(d, path);
        if (index >= 0 && pe_imp_disc_read(d, index, &companion, &companion_size, why, sizeof why)) {
            out->refused++;
            snprintf(out->last_error, sizeof out->last_error, "%s: %s", path, why);
            free(data);
            return;
        }
    } else if (rd->xa != PE_IMP_XA_BOTH && rd->companion) {
        char sibling[PE_IMP_PATH_MAX];
        const char *name = rd->companion;
        if (name[0] == '.') {
            const char *dot = strrchr(path, '.');
            const char *slash = strrchr(path, '/');
            if (!dot || (slash && dot < slash) ||
                snprintf(sibling, sizeof sibling, "%.*s%s", (int)(dot - path), path, name) >= (int)sizeof sibling)
                sibling[0] = 0;
            name = sibling;
        }
        int index = pe_imp_disc_find(d, name);
        if (index < 0 || pe_imp_disc_size(d, index) == 0 ||
            pe_imp_disc_read(d, index, &companion, &companion_size, why, sizeof why)) {
            out->refused++;
            snprintf(out->last_error, sizeof out->last_error, "%s: can't read companion %s", path, name);
            free(data);
            return;
        }
    }
    out->files_read += companion ? 2 : 1;
    store_file(cat, root, rd, label, path, data, size, companion, companion_size, out);
    free(companion);
    free(data);
}

static int reader_for(const char *game, const char *path) {
    for (int r = 0; r < pe_imp_readers(); r++) {
        const PeImpReader *rd = pe_imp_reader(r);
        if ((!game || !strcmp(rd->game, game)) && pe_imp_match(rd->match, path)) return r;
    }
    return -1;
}

static const char *pick_game(PeImpDisc *d) {
    const char *best = NULL;
    int best_n = 0;
    for (int r = 0; r < pe_imp_readers(); r++) {
        const char *g = pe_imp_reader(r)->game;
        int seen = 0;
        for (int i = 0; i < r; i++)
            if (!strcmp(g, pe_imp_reader(i)->game)) { seen = 1; break; }
        if (seen) continue;
        int n = 0, ids = pe_imp_disc_id_count(g), have = !ids;
        for (int k = 0; k < ids && !have; k++)
            for (int i = 0; i < pe_imp_disc_files(d) && !have; i++) have = pe_imp_match(pe_imp_disc_id(g, k), pe_imp_disc_name(d, i));
        if (!have) continue;                         /* another game's disc with the same layout */
        for (int i = 0; i < pe_imp_disc_files(d); i++) n += reader_for(g, pe_imp_disc_name(d, i)) >= 0;
        if (n > best_n) best = g, best_n = n;
    }
    return best;
}

/* Is this file already in the store, read by this reader at this version? Re-adds its items to
 * the catalog if so. */
static int current(Catalog *c, const char *dir, const PeImpReader *rd, const char *image, const char *path, const char *sha) {
    char p[PE_IMP_PATH_MAX];
    snprintf(p, sizeof p, "%s/source.json", dir);
    PjNode *s = pj_load(p, NULL, 0);
    int ok = s && !strcmp(pj_str(pj_get(s, "reader"), ""), rd->format) &&
             !strcmp(pj_str(pj_get(s, "platform"), ""), rd->platform) &&
             pj_int(pj_get(s, "reader_version"), -1) == rd->version &&
             pj_int(pj_get(s, schema_key(rd->produces)), -1) == schema_stamp(rd->produces);
    pj_free(s);
    if (!ok) return 0;
    snprintf(p, sizeof p, "%s/items.json", dir);
    PjNode *items = pj_load(p, NULL, 0);
    if (!items || items->kind != PJ_ARRAY) { pj_free(items); return 0; }
    for (const PjNode *it = items->child; it; it = it->next) {
        uint32_t type = stored_type(pj_str(pj_get(it, "type"), ""));
        const char *id = pj_str(pj_get(it, "id"), "");
        if (!type || !(rd->produces & type) || !local_id_ok(id)) continue;
        Source src = {{0}};
        snprintf(src.sha, sizeof src.sha, "%s", sha);
        snprintf(src.dir, sizeof src.dir, "%s", pj_str(pj_get(it, "dir"), ""));
        snprintf(src.platform, sizeof src.platform, "%s", rd->platform);
        snprintf(src.image, sizeof src.image, "%s", image);
        snprintf(src.path, sizeof src.path, "%s", path);
        cat_note(c, rd->game, id, pj_str(pj_get(it, "name"), ""), type, rd->tags, &src);
    }
    pj_free(items);
    return 1;
}

static int store_file(Catalog *c, const char *root, const PeImpReader *rd, const char *image, const char *path,
                      const uint8_t *data, uint32_t size,
                      const uint8_t *companion, uint32_t companion_size, PeImpScan *out) {
    char sha[65], dir[PE_IMP_PATH_MAX], p[PE_IMP_PATH_MAX], why[200] = "";
    pe_imp_sha256_hex(data, size, sha);
    if (rd->read_pair) {
        char pair[192], secondary[65], logical_path[65];
        pe_imp_sha256_hex(companion ? companion : (const uint8_t *)"", companion_size, secondary);
        pe_imp_sha256_hex((const uint8_t *)path, strlen(path), logical_path);
        memcpy(pair, sha, 64);
        memcpy(pair + 64, secondary, 64);
        memcpy(pair + 128, logical_path, 64);
        pe_imp_sha256_hex((const uint8_t *)pair, sizeof pair, sha);
    } else if (rd->path_key) {
        char key[64 + PE_IMP_PATH_MAX];
        memcpy(key, sha, 64);
        int len = snprintf(key + 64, sizeof key - 64, "%s/%s/%s", rd->platform, rd->format, path);
        if (len < 0 || (size_t)len >= sizeof key - 64) return -1;
        pe_imp_sha256_hex((const uint8_t *)key, 64 + (size_t)len, sha);
    }
    snprintf(dir, sizeof dir, "%s/%s/%s", root, rd->game, sha);
    if (current(c, dir, rd, image, path, sha)) { out->skipped++; return 0; }
    PeImpSink *sink = calloc(1, sizeof *sink);
    if (!sink) return -1;
    int rc = rd->read_pair
        ? rd->read_pair(data, size, companion, companion_size, path, sink, why, sizeof why)
        : rd->read(data, size, path, sink, why, sizeof why);
    if (!rc && pe_imp_mkdirs(dir)) rc = -1, snprintf(why, sizeof why, "can't create the store folder");
    snprintf(p, sizeof p, "%s/items.json", dir);
    remove(p);                                         /* items.json last: absent = not committed */
    FILE *list = NULL;
    for (int i = 0; !rc && i < sink->items; i++) {
        Item *it = &sink->item[i];
        if (!it->obj) continue;
        char sub[PE_IMP_PATH_MAX];
        snprintf(sub, sizeof sub, "%s/%d", dir, it->storage);
        if (pe_imp_mkdirs(sub) || save_obj(it, sub, why, sizeof why)) rc = -1;
    }
    if (!rc) {
        snprintf(p, sizeof p, "%s/source.json", dir);
        FILE *f = fopen(p, "wb");
        if (!f) rc = -1;
        else {
            fputs("{\"game\": ", f), pj_write_string(f, rd->game);
            fputs(", \"platform\": ", f), pj_write_string(f, rd->platform);
            fputs(", \"reader\": ", f), pj_write_string(f, rd->format);
            fprintf(f, ", \"reader_version\": %u, \"%s\": %u, \"size\": %u, \"sha256\": \"%s\", \"path\": ",
                    rd->version, schema_key(rd->produces), schema_stamp(rd->produces), size, sha);
            pj_write_string(f, path), fputs(", \"image\": ", f), pj_write_string(f, image), fputs("}\n", f);
            rc = fclose(f) ? -1 : 0;
        }
    }
    if (!rc) {
        snprintf(p, sizeof p, "%s/items.json", dir);
        list = fopen(p, "wb");
        if (!list) rc = -1;
        else {
            fputs("[", list);
            for (int i = 0; i < sink->items; i++) {
                Item *it = &sink->item[i];
                fputs(i ? ",\n {\"id\": " : "{\"id\": ", list), pj_write_string(list, it->id);
                fputs(", \"name\": ", list), pj_write_string(list, it->name);
                fprintf(list, ", \"type\": \"%s\", \"dir\": \"%d\"}", pe_imp_type_name(it->type), it->storage);
            }
            fputs("]\n", list);
            rc = fclose(list) ? -1 : 0;
        }
    }
    if (!rc) {
        for (int i = 0; i < sink->items; i++) {
            Source src = {{0}};
            snprintf(src.sha, sizeof src.sha, "%s", sha);
            snprintf(src.dir, sizeof src.dir, "%d", sink->item[i].storage);
            snprintf(src.platform, sizeof src.platform, "%s", rd->platform);
            snprintf(src.image, sizeof src.image, "%s", image);
            snprintf(src.path, sizeof src.path, "%s", path);
            cat_note(c, rd->game, sink->item[i].id, sink->item[i].name, sink->item[i].type, rd->tags, &src);
        }
        out->items += sink->items;
    } else {
        out->refused++;
        snprintf(out->last_error, sizeof out->last_error, "%s: %s", path, why[0] ? why : "store write failed");
    }
    sink_free(sink);
    free(sink);
    return rc;
}

static int scan_disc(const char *root, const char *image, const char *game, const char *selected, PeImpScan *out) {
    memset(out, 0, sizeof *out);
    char why[200] = "";
    PeImpDisc *d = pe_imp_disc_open(image, why, sizeof why);
    if (!d) { snprintf(out->last_error, sizeof out->last_error, "%s", why); return -1; }
    if (!game) game = pick_game(d);
    if (!game) {
        snprintf(out->last_error, sizeof out->last_error, "%s: no reader recognises this disc", pe_imp_disc_label(d));
        pe_imp_disc_close(d);
        return -1;
    }
    if (selected && strcmp(game, selected)) {
        pe_imp_disc_close(d);
        return 1;
    }
    snprintf(out->game, sizeof out->game, "%s", game);
    char cat_path[PE_IMP_PATH_MAX], gdir[PE_IMP_PATH_MAX];
    snprintf(gdir, sizeof gdir, "%s/%s", root, game);
    snprintf(cat_path, sizeof cat_path, "%s/catalog.json", gdir);
    if (pe_imp_mkdirs(gdir)) {
        snprintf(out->last_error, sizeof out->last_error, "can't create %s", gdir);
        pe_imp_disc_close(d);
        return -1;
    }
    Catalog cat;
    cat_load(&cat, cat_path);
    /* this image's entries are noted again below, under today's names: forget the old ones */
    const char *label = pe_imp_disc_label(d);
    for (int k = 0; k < cat.n; k++) {
        Entry *e = &cat.e[k];
        int kept = 0;
        for (int s = 0; s < e->srcs; s++)
            if (strcmp(e->src[s].image, label)) e->src[kept++] = e->src[s];
        e->srcs = kept;
    }
    /* every reader that matches a file reads it (two readers of one file keep apart with path_key) */
    for (int i = 0; i < pe_imp_disc_files(d); i++)
        for (int r = 0; r < pe_imp_readers(); r++) {
            const PeImpReader *rd = pe_imp_reader(r);
            if ((!game || !strcmp(rd->game, game)) && pe_imp_match(rd->match, pe_imp_disc_name(d, i)))
                scan_file(&cat, root, label, d, i, rd, out);
        }
    int rc = cat_save(&cat, cat_path);
    cat_free(&cat);
    pe_imp_disc_close(d);
    if (rc) snprintf(out->last_error, sizeof out->last_error, "can't write %s", cat_path);
    return rc;
}

int pe_imp_scan(const char *root, const char *image, const char *game, PeImpScan *out) {
    return scan_disc(root, image, game, NULL, out);
}

int pe_imp_scan_selected(const char *root, const char *image, const char *game, PeImpScan *out) {
    return scan_disc(root, image, NULL, game, out);
}

int pe_imp_catalog_assets(const char *root, const char *game, PeImpAsset **out) {
    char path[PE_IMP_PATH_MAX];
    *out = NULL;
    snprintf(path, sizeof path, "%s/%s/catalog.json", root, game);
    Catalog cat;
    cat_load(&cat, path);
    PeImpAsset *a = calloc((size_t)(cat.n ? cat.n : 1), sizeof *a);
    if (!a) { cat_free(&cat); return -1; }
    cat_sort(&cat);
    for (int i = 0; i < cat.n; i++) {
        Entry *e = &cat.e[i];
        qsort(e->src, (size_t)e->srcs, sizeof *e->src, src_cmp);
        snprintf(a[i].id, sizeof a[i].id, "%s", e->id);
        snprintf(a[i].name, sizeof a[i].name, "%s", e->name);
        snprintf(a[i].type, sizeof a[i].type, "%s", e->type);
        snprintf(a[i].tags, sizeof a[i].tags, "%s", e->tags);
        if (e->srcs) {
            snprintf(a[i].sha256, sizeof a[i].sha256, "%s", e->src[0].sha);
            snprintf(a[i].platform, sizeof a[i].platform, "%s", e->src[0].platform);
            snprintf(a[i].path, sizeof a[i].path, "%s", e->src[0].path);
        }
    }
    int n = cat.n;
    cat_free(&cat);
    *out = a;
    return n;
}

/* The catalog a lookup reads, kept per thread while its file is unchanged (size, time, and no save
 * here since). Reading it each time cost a camera change some 80 ms of parsing for a mixed store's
 * 4 MB catalog, and a room entry several of those. */
static _Thread_local struct {
    char path[PE_IMP_PATH_MAX];
    long long size, mtime;
    unsigned saves;
    int kept;
    Catalog cat;
} s_cached;

static Catalog *catalog_cached(const char *path) {
    struct stat st;
    const int found = !stat(path, &st);
    const long long size = found ? (long long)st.st_size : -1, mtime = found ? (long long)st.st_mtime : -1;
    if (s_cached.kept && !strcmp(s_cached.path, path) && s_cached.size == size && s_cached.mtime == mtime &&
        s_cached.saves == s_catalog_saves)
        return &s_cached.cat;
    cat_free(&s_cached.cat);
    cat_load(&s_cached.cat, path);
    snprintf(s_cached.path, sizeof s_cached.path, "%s", path);
    s_cached.size = size, s_cached.mtime = mtime, s_cached.saves = s_catalog_saves, s_cached.kept = 1;
    return &s_cached.cat;
}

void pe_imp_catalog_release(void) {
    cat_free(&s_cached.cat);
    s_cached.kept = 0;
}

static int catalog_lookup(const char *root, const char *asset_id, uint32_t type,
                          char path[PE_IMP_PATH_MAX], PeImpMovie **movie, char *why, size_t n) {
    char game[16] = "";
    const char *colon = strchr(asset_id, ':');
    if (!colon || colon == asset_id || (size_t)(colon - asset_id) >= sizeof game) {
        if (why && n) snprintf(why, n, "%s is not a <game>:<id> asset ID", asset_id);
        return -1;
    }
    memcpy(game, asset_id, (size_t)(colon - asset_id));
    if (strspn(game, "abcdefghijklmnopqrstuvwxyz0123456789-_") != strlen(game) ||
        !local_id_ok(colon + 1) ||
        snprintf(path, PE_IMP_PATH_MAX, "%s/%s/catalog.json", root, game) >= PE_IMP_PATH_MAX) {
        if (why && n) snprintf(why, n, "invalid catalog asset ID or path");
        return -1;
    }
    Entry *e = cat_find(catalog_cached(path), asset_id);
    int rc = -1;
    if (!e || !e->srcs) {
        if (why && n) snprintf(why, n, "%s is not in the catalog: scan its disc first", asset_id);
    } else if (strcmp(e->type, pe_imp_type_name(type))) {
        if (why && n) snprintf(why, n, "%s is a %s, expected %s", asset_id, e->type, pe_imp_type_name(type));
    } else {
        qsort(e->src, (size_t)e->srcs, sizeof *e->src, src_cmp);
        const int count = movie ? e->srcs : 1;
        for (int i = 0; i < count; ++i) {
            const Source *src = &e->src[i];
            if (strlen(src->sha) != 64 || strspn(src->sha, "0123456789abcdef") != 64 ||
                !*src->dir || strspn(src->dir, "0123456789") != strlen(src->dir) ||
                snprintf(path, PE_IMP_PATH_MAX, "%s/%s/%s/%s", root, game, src->sha, src->dir) >= PE_IMP_PATH_MAX) {
                if (rc && why && n) snprintf(why, n, "%s has an invalid catalog location", asset_id);
                continue;
            }
            if (!movie) {
                rc = 0;
                break;
            }
            PeImpMovie *candidate;
            if (pe_imp_movie_load(path, &candidate, rc ? why : NULL, rc ? n : 0)) continue;
            /* Keep normal catalog order among equally capable copies. */
            if (!*movie || candidate->frame_number ||
                (candidate->frame_sector && !(*movie)->frame_sector)) {
                pe_imp_movie_free(*movie);
                *movie = candidate;
            } else {
                pe_imp_movie_free(candidate);
            }
            rc = 0;
            if ((*movie)->frame_number) break;
        }
    }
    return rc;
}

static int catalog_path(const char *root, const char *asset_id, uint32_t type,
                        char path[PE_IMP_PATH_MAX], char *why, size_t n) {
    return catalog_lookup(root, asset_id, type, path, NULL, why, n);
}

int pe_imp_catalog_model(const char *root, const char *id, PeImpModel *model, char *why, size_t n) {
    char path[PE_IMP_PATH_MAX];
    if (catalog_path(root, id, PE_IMP_MODEL, path, why, n)) return -1;
    return pe_imp_model_load(path, model, why, n);
}

int pe_imp_catalog_animation(const char *root, const char *id, PeImpAnimation *a, char *why, size_t n) {
    char path[PE_IMP_PATH_MAX];
    if (catalog_path(root, id, PE_IMP_ANIMATION, path, why, n)) return -1;
    return pe_imp_animation_load(path, a, why, n);
}

int pe_imp_catalog_sound(const char *root, const char *id, PeImpSound **out, char *why, size_t n) {
    char path[PE_IMP_PATH_MAX];
    *out = NULL;
    if (catalog_path(root, id, PE_IMP_SOUND, path, why, n)) return -1;
    return pe_imp_sound_load(path, out, why, n);
}

int pe_imp_catalog_song(const char *root, const char *id, PeImpSong **out, char *why, size_t n) {
    char path[PE_IMP_PATH_MAX];
    *out = NULL;
    if (catalog_path(root, id, PE_IMP_SONG, path, why, n)) return -1;
    return pe_imp_song_load(path, out, why, n);
}

int pe_imp_catalog_imageset(const char *root, const char *id, PeImpImageSet **out, char *why, size_t n) {
    char path[PE_IMP_PATH_MAX];
    *out = NULL;
    if (catalog_path(root, id, PE_IMP_IMAGESET, path, why, n)) return -1;
    return pe_imp_imageset_load(path, out, why, n);
}

int pe_imp_catalog_roomlogic(const char *root, const char *id, PeImpRoomLogic **out, char *why, size_t n) {
    char path[PE_IMP_PATH_MAX];
    *out = NULL;
    if (catalog_path(root, id, PE_IMP_ROOMLOGIC, path, why, n)) return -1;
    return pe_imp_logic_load(path, out, why, n);
}

int pe_imp_catalog_doorscene(const char *root, const char *id, PeImpRoomLogic **out, char *why, size_t n) {
    char path[PE_IMP_PATH_MAX];
    *out = NULL;
    if (catalog_path(root, id, PE_IMP_DOORSCENE, path, why, n)) return -1;
    return pe_imp_door_load(path, out, why, n);
}

int pe_imp_catalog_itemtable(const char *root, const char *id, PeImpRoomLogic **out, char *why, size_t n) {
    char path[PE_IMP_PATH_MAX];
    *out = NULL;
    if (catalog_path(root, id, PE_IMP_ITEMTABLE, path, why, n)) return -1;
    return pe_imp_items_load(path, out, why, n);
}

int pe_imp_catalog_movie(const char *root, const char *id, PeImpMovie **out, char *why, size_t n) {
    char path[PE_IMP_PATH_MAX];
    *out = NULL;
    return catalog_lookup(root, id, PE_IMP_MOVIE, path, out, why, n);
}

int pe_imp_catalog_pictures(const char *root, const char *id, PeImpRoomLogic **out, char *why, size_t n) {
    char path[PE_IMP_PATH_MAX];
    *out = NULL;
    if (catalog_path(root, id, PE_IMP_PICTURES, path, why, n)) return -1;
    return pe_imp_pictures_load(path, out, why, n);
}
