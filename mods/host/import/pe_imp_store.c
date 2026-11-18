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
#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define MKDIR(p) mkdir(p, 0755)
#endif

#include "pe_imp_internal.h"

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
typedef struct { uint32_t type; char id[PE_IMP_ID_MAX], name[PE_IMP_NAME_MAX]; void *obj; } Item;
struct PeImpSink { Item item[MAX_ITEMS]; int items; };

static void free_obj(uint32_t type, void *obj) {
    if (!obj) return;
    if (type == PE_IMP_MODEL) pe_imp_model_free(obj);
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
    if (!s || s->items == MAX_ITEMS || !proposed || !local_id_ok(proposed) || type != PE_IMP_MODEL) {
        free_obj(type, obj);
        return -1;
    }
    Item *it = &s->item[s->items++];
    it->type = type, it->obj = obj;
    snprintf(it->id, sizeof it->id, "%s", proposed);
    snprintf(it->name, sizeof it->name, "%s", name ? name : proposed);
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
    Source src[MAX_SOURCES];
    int srcs;
} Entry;
typedef struct { Entry *e; int n, cap; } Catalog;

static Entry *cat_find(Catalog *c, const char *id) {
    for (int i = 0; i < c->n; i++) if (!strcmp(c->e[i].id, id)) return &c->e[i];
    return NULL;
}

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
            Source *d = &e->src[e->srcs++];
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

static int cat_save(Catalog *c, const char *path) {
    char tmp[PE_IMP_PATH_MAX];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f) return -1;
    qsort(c->e, (size_t)c->n, sizeof *c->e, entry_cmp);
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
    remove(path);
    return rename(tmp, path);
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
    if (e->srcs < MAX_SOURCES) e->src[e->srcs++] = *src;
}

/* ---- scanning a disc image ---- */
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
             pj_int(pj_get(s, "model_version"), -1) == PE_IMP_MODEL_VERSION;
    pj_free(s);
    if (!ok) return 0;
    snprintf(p, sizeof p, "%s/items.json", dir);
    PjNode *items = pj_load(p, NULL, 0);
    if (!items || items->kind != PJ_ARRAY) { pj_free(items); return 0; }
    for (const PjNode *it = items->child; it; it = it->next) {
        Source src = {{0}};
        snprintf(src.sha, sizeof src.sha, "%s", sha);
        snprintf(src.dir, sizeof src.dir, "%s", pj_str(pj_get(it, "dir"), ""));
        snprintf(src.platform, sizeof src.platform, "%s", rd->platform);
        snprintf(src.image, sizeof src.image, "%s", image);
        snprintf(src.path, sizeof src.path, "%s", path);
        const char *type = pj_str(pj_get(it, "type"), "");
        cat_note(c, rd->game, pj_str(pj_get(it, "id"), ""), pj_str(pj_get(it, "name"), ""),
                 !strcmp(type, "Model") ? PE_IMP_MODEL : 0, rd->tags, &src);
    }
    pj_free(items);
    return 1;
}

static int store_file(Catalog *c, const char *root, const PeImpReader *rd, const char *image, const char *path,
                      const uint8_t *data, uint32_t size, PeImpScan *out) {
    char sha[65], dir[PE_IMP_PATH_MAX], p[PE_IMP_PATH_MAX], why[200] = "";
    pe_imp_sha256_hex(data, size, sha);
    snprintf(dir, sizeof dir, "%s/%s/%s", root, rd->game, sha);
    if (current(c, dir, rd, image, path, sha)) { out->skipped++; return 0; }
    PeImpSink *sink = calloc(1, sizeof *sink);
    if (!sink) return -1;
    int rc = rd->read(data, size, path, sink, why, sizeof why);
    if (!rc && pe_imp_mkdirs(dir)) rc = -1, snprintf(why, sizeof why, "can't create the store folder");
    snprintf(p, sizeof p, "%s/items.json", dir);
    remove(p);                                         /* items.json last: absent = not committed */
    FILE *list = NULL;
    for (int i = 0; !rc && i < sink->items; i++) {
        Item *it = &sink->item[i];
        char sub[PE_IMP_PATH_MAX];
        snprintf(sub, sizeof sub, "%s/%d", dir, i);
        if (pe_imp_mkdirs(sub) || pe_imp_model_save(it->obj, sub, why, sizeof why)) rc = -1;
    }
    if (!rc) {
        snprintf(p, sizeof p, "%s/source.json", dir);
        FILE *f = fopen(p, "wb");
        if (!f) rc = -1;
        else {
            fputs("{\"game\": ", f), pj_write_string(f, rd->game);
            fputs(", \"platform\": ", f), pj_write_string(f, rd->platform);
            fputs(", \"reader\": ", f), pj_write_string(f, rd->format);
            fprintf(f, ", \"reader_version\": %u, \"model_version\": %d, \"size\": %u, \"sha256\": \"%s\", \"path\": ",
                    rd->version, PE_IMP_MODEL_VERSION, size, sha);
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
                fprintf(list, ", \"type\": \"%s\", \"dir\": \"%d\"}", pe_imp_type_name(it->type), i);
            }
            fputs("]\n", list);
            rc = fclose(list) ? -1 : 0;
        }
    }
    if (!rc) {
        for (int i = 0; i < sink->items; i++) {
            Source src = {{0}};
            snprintf(src.sha, sizeof src.sha, "%s", sha);
            snprintf(src.dir, sizeof src.dir, "%d", i);
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

int pe_imp_scan(const char *root, const char *image, const char *game, PeImpScan *out) {
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
    for (int i = 0; i < pe_imp_disc_files(d); i++) {
        const char *path = pe_imp_disc_name(d, i);
        int r = reader_for(game, path);
        if (r < 0) continue;
        uint8_t *data;
        uint32_t size;
        if (pe_imp_disc_read(d, i, &data, &size, why, sizeof why)) {
            out->refused++;
            snprintf(out->last_error, sizeof out->last_error, "%s: %s", path, why);
            continue;
        }
        out->files_read++;
        store_file(&cat, root, pe_imp_reader(r), pe_imp_disc_label(d), path, data, size, out);
        free(data);
    }
    int rc = cat_save(&cat, cat_path);
    free(cat.e);
    pe_imp_disc_close(d);
    if (rc) snprintf(out->last_error, sizeof out->last_error, "can't write %s", cat_path);
    return rc;
}

int pe_imp_catalog_assets(const char *root, const char *game, PeImpAsset **out) {
    char path[PE_IMP_PATH_MAX];
    *out = NULL;
    snprintf(path, sizeof path, "%s/%s/catalog.json", root, game);
    Catalog cat;
    cat_load(&cat, path);
    PeImpAsset *a = calloc((size_t)(cat.n ? cat.n : 1), sizeof *a);
    if (!a) { free(cat.e); return -1; }
    qsort(cat.e, (size_t)cat.n, sizeof *cat.e, entry_cmp);
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
    free(cat.e);
    *out = a;
    return n;
}

int pe_imp_catalog_model(const char *root, const char *asset_id, PeImpModel *m, char *why, size_t n) {
    char game[16] = "", path[PE_IMP_PATH_MAX];
    const char *colon = strchr(asset_id, ':');
    if (!colon || colon == asset_id || (size_t)(colon - asset_id) >= sizeof game) {
        if (why && n) snprintf(why, n, "%s is not a <game>:<id> asset ID", asset_id);
        return -1;
    }
    memcpy(game, asset_id, (size_t)(colon - asset_id));
    snprintf(path, sizeof path, "%s/%s/catalog.json", root, game);
    Catalog cat;
    cat_load(&cat, path);
    Entry *e = cat_find(&cat, asset_id);
    int rc = -1;
    if (!e || !e->srcs) {
        if (why && n) snprintf(why, n, "%s is not in the catalog: scan its disc first", asset_id);
    } else if (strcmp(e->type, "Model")) {
        if (why && n) snprintf(why, n, "%s is a %s, not a Model", asset_id, e->type);
    } else {
        qsort(e->src, (size_t)e->srcs, sizeof *e->src, src_cmp);
        snprintf(path, sizeof path, "%s/%s/%s/%s", root, game, e->src[0].sha, e->src[0].dir);
        rc = pe_imp_model_load(path, m, why, n);
    }
    free(cat.e);
    return rc;
}
