/* The automatic import: what runs by itself (on a worker thread) when the game starts, and when
 * the player asks for a scan. No UI here, so the flow test runs exactly this.
 *
 *   1. every disc image in the import folder that a source game knows is read into the store
 *      (once per file: unchanged files are skipped);
 *   2. every asset a source game marks automatic (pe_imp_add_auto) is built by its module, unless
 *      its pack folder already holds a build of the same source file by the same builder;
 *   3. what's still missing is listed by the disc to put in the folder.
 * The Wardrobe is told on the main thread afterwards (pe_import.c). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>

#include "pe_imp.h"
#include "pe_imp_enemy.h"
#include "pe_imp_json.h"
#include "pe_imp_logic.h"
#include "pe_imp_pictures.h"
#include "pe_import.h"
#include "pe_mod_host.h"

#define MAX_IMAGES 64

static void add_text(char *out, size_t size, const char *text) {
    size_t used = strlen(out);
    if (used + 3 < size) snprintf(out + used, size - used, "%s%s", used ? ", " : "", text);
}

/* add `text` to a ", " list unless it's already a whole item ("Resident Evil" isn't in
 * "Resident Evil 2") */
static void add_once(char *out, size_t size, const char *text) {
    size_t len = strlen(text);
    for (const char *p = out; (p = strstr(p, text)) != NULL; p++)
        if ((p == out || (p >= out + 2 && p[-2] == ',' && p[-1] == ' ')) && (p[len] == 0 || p[len] == ',')) return;
    add_text(out, size, text);
}

/* ---- the disc images in the folder, loose or one folder deep ---- */
typedef struct { char path[MAX_IMAGES][1100]; int n, depth; } Found;

static int ends(const char *s, const char *tail) {
    size_t a = strlen(s), b = strlen(tail);
    return a >= b && !SDL_strcasecmp(s + a - b, tail);
}

static SDL_EnumerationResult on_entry(void *user, const char *dir, const char *name) {
    Found *f = user;
    char full[1100];
    SDL_PathInfo info;
    snprintf(full, sizeof full, "%s%s", dir, name);
    for (char *c = full; *c; c++) if (*c == '\\') *c = '/';      /* one separator, so a .bin finds its .cue */
    if (!SDL_GetPathInfo(full, &info)) return SDL_ENUM_CONTINUE;
    if (info.type == SDL_PATHTYPE_DIRECTORY) {
        if (f->depth == 0) {
            Found *sub = calloc(1, sizeof *sub);
            char d[1100];
            if (!sub) return SDL_ENUM_CONTINUE;
            sub->depth = 1;
            snprintf(d, sizeof d, "%s/", full);
            SDL_EnumerateDirectory(d, on_entry, sub);
            for (int i = 0; i < sub->n && f->n < MAX_IMAGES; i++) memcpy(f->path[f->n++], sub->path[i], sizeof sub->path[i]);
            free(sub);
        }
        return SDL_ENUM_CONTINUE;
    }
    /* a PC game folder (Resident Evil 3's rofs*.dat archives) counts once, by its rofs1.dat */
    if ((ends(name, ".cue") || ends(name, ".iso") || ends(name, ".bin") || !SDL_strcasecmp(name, "rofs1.dat")) && f->n < MAX_IMAGES)
        snprintf(f->path[f->n++], sizeof f->path[0], "%s", full);
    return SDL_ENUM_CONTINUE;
}

/* a .bin belongs to the .cue beside it when there is one */
static int wanted(const Found *f, const char *path) {
    if (!ends(path, ".bin")) return 1;
    const char *slash = strrchr(path, '/');
    size_t dir = slash ? (size_t)(slash - path) : 0;
    for (int i = 0; i < f->n; i++) {
        const char *s = strrchr(f->path[i], '/');
        size_t d = s ? (size_t)(s - f->path[i]) : 0;
        if (ends(f->path[i], ".cue") && d == dir && !strncmp(f->path[i], path, dir)) return 0;
    }
    return 1;
}

/* ---- what was looked at: <store>/images.json ----
 * {"readers": "<every reader and its version>", "images": {"<path>": {"size", "mtime", "game"}}}
 * An image whose size and date are unchanged is skipped without opening it: read ones (game set)
 * as long as their game's catalog is still there, unsupported ones (game "") until the readers
 * change, which is when a new source game or reader version could read them. */
typedef struct { char path[1100], game[16]; long long size, mtime; } IndexEntry;
typedef struct { IndexEntry *e; int n, cap; char readers[1024]; } Index;

static void readers_signature(char *out, size_t size) {
    out[0] = 0;
    for (int r = 0; r < pe_imp_readers(); r++) {
        const PeImpReader *rd = pe_imp_reader(r);
        size_t u = strlen(out);
        snprintf(out + u, size - u, "%s/%s/%s %u;", rd->game, rd->platform, rd->format, rd->version);
    }
    size_t u = strlen(out);
    snprintf(out + u, size - u, "model %d sound %d song %d imageset %d roomlogic %d door %d items %d movie %d pictures %d naming %08x", PE_IMP_MODEL_VERSION, PE_IMP_SOUND_VERSION,
             PE_IMP_SONG_VERSION, PE_IMP_IMAGESET_VERSION, PE_IMP_ROOMLOGIC_VERSION, PE_IMP_DOORSCENE_VERSION, PE_IMP_ITEMTABLE_VERSION, PE_IMP_MOVIE_VERSION, PE_IMP_PICTURES_VERSION, (unsigned)pe_imp_naming_hash());
}

static IndexEntry *index_find(Index *ix, const char *path) {
    for (int i = 0; i < ix->n; i++) if (!strcmp(ix->e[i].path, path)) return &ix->e[i];
    return NULL;
}

static void index_put(Index *ix, const char *path, long long size, long long mtime, const char *game) {
    IndexEntry *e = index_find(ix, path);
    if (!e) {
        if (ix->n == ix->cap) {
            int cap = ix->cap ? ix->cap * 2 : 32;
            IndexEntry *g = realloc(ix->e, (size_t)cap * sizeof *g);
            if (!g) return;
            ix->e = g, ix->cap = cap;
        }
        e = &ix->e[ix->n++];
        snprintf(e->path, sizeof e->path, "%s", path);
    }
    e->size = size, e->mtime = mtime;
    snprintf(e->game, sizeof e->game, "%s", game ? game : "");
}

static void index_load(const PeImportJob *j, Index *ix) {
    char path[1200], sig[1024];
    memset(ix, 0, sizeof *ix);
    readers_signature(sig, sizeof sig);
    snprintf(ix->readers, sizeof ix->readers, "%s", sig);
    snprintf(path, sizeof path, "%s/images.json", j->store);
    PjNode *root = pj_load(path, NULL, 0);
    const PjNode *images = pj_get(root, "images");
    if (root && !strcmp(pj_str(pj_get(root, "readers"), ""), sig) && images && images->kind == PJ_OBJECT)
        for (const PjNode *x = images->child; x; x = x->next)
            index_put(ix, x->key, pj_int(pj_get(x, "size"), -1), pj_int(pj_get(x, "mtime"), -1), pj_str(pj_get(x, "game"), ""));
    pj_free(root);                                                      /* readers changed: look at everything */
}

static int index_still_there(const PeImportJob *j, const IndexEntry *e) {
    if (!e->game[0]) return 1;
    char path[1200];
    snprintf(path, sizeof path, "%s/%s/catalog.json", j->store, e->game);
    FILE *f = fopen(path, "rb");
    if (f) fclose(f);
    return f != NULL;
}

static void index_save(const PeImportJob *j, const Index *ix) {
    char path[1200], tmp[1210];
    snprintf(path, sizeof path, "%s/images.json", j->store);
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    pe_imp_mkdirs(j->store);
    FILE *f = fopen(tmp, "wb");
    if (!f) return;
    fputs("{\"readers\": ", f), pj_write_string(f, ix->readers), fputs(",\n \"images\": {", f);
    for (int i = 0; i < ix->n; i++) {
        fputs(i ? ",\n  " : "\n  ", f), pj_write_string(f, ix->e[i].path);
        fprintf(f, ": {\"size\": %lld, \"mtime\": %lld, \"game\": ", ix->e[i].size, ix->e[i].mtime);
        pj_write_string(f, ix->e[i].game), fputs("}", f);
    }
    fputs("}}\n", f);
    if (fclose(f)) return;
    remove(path);
    rename(tmp, path);
}

static void scan(PeImportJob *j) {
    Found *f = calloc(1, sizeof *f);
    char dir[1200];
    if (!f) return;
    pe_imp_mkdirs(j->discs);
    snprintf(dir, sizeof dir, "%s/", j->discs);
    SDL_EnumerateDirectory(dir, on_entry, f);
    for (int e = 0; e < j->nextra && e < PE_IMPORT_FOLDERS; e++) {   /* folders the player added */
        size_t l = strlen(j->extra[e]);
        if (!l) continue;
        snprintf(dir, sizeof dir, "%s%s", j->extra[e], j->extra[e][l - 1] == '/' || j->extra[e][l - 1] == '\\' ? "" : "/");
        SDL_EnumerateDirectory(dir, on_entry, f);
    }
    Index ix;
    index_load(j, &ix);
    int todo = 0, done = 0;
    for (int i = 0; i < f->n; i++) todo += wanted(f, f->path[i]);
    for (int i = 0; i < f->n; i++) {
        if (!wanted(f, f->path[i])) continue;
        const char *name = strrchr(f->path[i], '/');
        name = name ? name + 1 : f->path[i];
        char what[200];
        snprintf(what, sizeof what, "reading %s", name);
        PE_IMP_PROGRESS(j->progress, what, ++done, todo);
        SDL_PathInfo info;
        long long size = 0, mtime = 0;
        if (SDL_GetPathInfo(f->path[i], &info)) size = (long long)info.size, mtime = (long long)info.modify_time;
        IndexEntry *e = index_find(&ix, f->path[i]);
        if (e && e->size == size && e->mtime == mtime && index_still_there(j, e)) {   /* nothing changed: not opened */
            if (j->game && strcmp(j->game, e->game)) continue;
            j->unchanged++;
            if (e->game[0]) add_once(j->games, sizeof j->games, pe_imp_game_title(e->game));
            continue;
        }
        PeImpScan sc;
        const int rc = j->game ? pe_imp_scan_selected(j->store, f->path[i], j->game, &sc)
                               : pe_imp_scan(j->store, f->path[i], NULL, &sc);
        if (rc == 1) continue;                              /* recognised as another game */
        const int unsupported = rc && strstr(sc.last_error, "no reader recognises");
        if ((rc && !unsupported) || (!rc && sc.refused)) {
            j->failed++;
            snprintf(j->problem, sizeof j->problem, "%.80s: %.151s", name, sc.last_error);
            pe_mod_log("pe_import", "disc scan failed: %s: %s", f->path[i], sc.last_error);
            continue;
        }
        index_put(&ix, f->path[i], size, mtime, rc ? "" : sc.game);     /* read, or looked at and not supported */
        if (unsupported) { j->unknown++; continue; }
        j->discs_read++, j->items += sc.items + sc.skipped;
        add_once(j->games, sizeof j->games, pe_imp_game_title(sc.game));
    }
    index_save(j, &ix);
    free(ix.e);
    free(f);
}

/* ---- the automatic builds ---- */
static int find_asset(const char *store, const char *id, PeImpAsset *out) {
    char game[16] = "";
    const char *colon = strchr(id, ':');
    if (!colon || (size_t)(colon - id) >= sizeof game) return 0;
    memcpy(game, id, (size_t)(colon - id));
    PeImpAsset *a;
    int n = pe_imp_catalog_assets(store, game, &a), found = 0;
    for (int i = 0; i < n && !found; i++) if (!strcmp(a[i].id, id)) *out = a[i], found = 1;
    free(a);
    return found;
}

static const PeImpTarget *target_for(const char *module) {
    for (int t = 0; t < pe_imp_targets(); t++) if (!strcmp(pe_imp_target(t)->module, module)) return pe_imp_target(t);
    return NULL;
}

/* Already built from this file by this builder, every file there? (character.json's source) */
static int current(const char *folder, const PeImpAsset *a, const char *builder) {
    char path[1200];
    snprintf(path, sizeof path, "%s/character.json", folder);
    PjNode *j = pj_load(path, NULL, 0);
    const PjNode *s = pj_get(j, "source");
    int ok = s && !strcmp(pj_str(pj_get(s, "sha256"), ""), a->sha256) && !strcmp(pj_str(pj_get(s, "builder"), ""), builder);
    pj_free(j);
    static const char *const files[] = {"model.bin", "texture.bin", "day1.bin", "day8.bin", "rooms.bin", "meta.bin"};
    for (unsigned i = 0; ok && i < sizeof files / sizeof *files; i++) {
        snprintf(path, sizeof path, "%s/%s", folder, files[i]);
        FILE *f = fopen(path, "rb");
        ok = f != NULL;
        if (f) fclose(f);
    }
    return ok;
}

/* What one automatic entry needs: -1 nothing (no module, or not in the catalog: missing), 0 built
 * already, 1 to build; fills the asset, its folder and the builder's name. */
static int need(const PeImportJob *j, const PeImpAuto *au, const PeImpTarget **tg, PeImpAsset *a,
                char *folder, size_t fn, char *builder, size_t bn, char *game, size_t gn) {
    if (!(*tg = target_for(au->module)) || !find_asset(j->store, au->asset_id, a)) return -1;
    snprintf(builder, bn, "pe_import 1, %s %u", (*tg)->module, (*tg)->version);
    /* the folder pe_imp_pack_character writes: <packs>/<game>-local/characters/<name>.<game> */
    const char *colon = strchr(a->id, ':'), *last = strrchr(a->id, '/');
    snprintf(game, gn, "%.*s", (int)(colon - a->id), a->id);
    snprintf(folder, fn, "%s/%s-local/characters/%.30s.%s", j->packs, game, last ? last + 1 : colon + 1, game);
    if ((*tg)->current) return (*tg)->current(a, builder) ? 0 : 1;   /* a target with its own place */
    return current(folder, a, builder) ? 0 : 1;
}

static int auto_selected(const PeImportJob *j, const PeImpAuto *entry) {
    if (!j->game) return 1;
    const size_t len = strlen(j->game);
    return !strncmp(entry->asset_id, j->game, len) && entry->asset_id[len] == ':';
}

static void build_autos(PeImportJob *j) {
    const void *targets = NULL;                                     /* the game's data, opened once */
    int todo = 0, done = 0;
    for (int i = 0; i < pe_imp_autos(); i++) {                       /* how many builds, for the progress */
        const PeImpTarget *tg;
        PeImpAsset a;
        char folder[1100], builder[64], game[16];
        const PeImpAuto *entry = pe_imp_auto(i);
        if (auto_selected(j, entry))
            todo += need(j, entry, &tg, &a, folder, sizeof folder, builder, sizeof builder, game, sizeof game) == 1;
    }
    for (int i = 0; i < pe_imp_autos(); i++) {
        const PeImpAuto *au = pe_imp_auto(i);
        if (!auto_selected(j, au)) continue;
        const PeImpTarget *tg;
        PeImpAsset a;
        char builder[64], folder[1100], game[16], why[240] = "", log[160] = "", label[96], what[160];
        int k = need(j, au, &tg, &a, folder, sizeof folder, builder, sizeof builder, game, sizeof game);
        if (k < 0) {
            if (tg) j->missing++, add_text(j->missing_discs, sizeof j->missing_discs, au->disc);   /* no module: not ours */
            continue;
        }
        if (k == 0) { j->current++; continue; }
        snprintf(label, sizeof label, "%s (%s)", a.name, pe_imp_game_title(game));
        snprintf(what, sizeof what, "building %s", label);
        PE_IMP_PROGRESS(j->progress, what, ++done, todo);
        if (!targets && !(targets = j->host->open_targets(why, sizeof why))) {
            j->failed++, snprintf(j->problem, sizeof j->problem, "%s", why);
            break;
        }
        if (pe_imp_target_place(tg, j->packs, &a, builder, folder, sizeof folder, why, sizeof why) ||
            tg->build(&(PeImpBuild){j->store, targets, j->step}, a.id, folder, log, sizeof log, why, sizeof why)) {
            j->failed++, snprintf(j->problem, sizeof j->problem, "%s: %s", a.name, why);
            continue;
        }
        j->built++;
        add_text(j->built_names, sizeof j->built_names, label);
    }
    if (targets) j->host->close_targets(targets);
}

static int compare_asset_id(const void *id, const void *item) {
    const PeImpAsset *asset = item;
    return strcmp(id, asset->id);
}

static void report_enemy(PeImportJob *j, FILE *report, const PeImpTarget *tg, const PeImpAsset *asset) {
    if (!tg->coverage) return;
    char detail[200] = "Pack could not be read";
    char audio[64] = "Unknown";
    const int coverage = tg->coverage(asset, detail, sizeof detail, audio, sizeof audio);
    const char *status = "Incomplete";
    if (coverage == PE_IMP_COVERAGE_COMPLETE) {
        j->enemies_complete++;
        status = "Complete";
    } else if (coverage == PE_IMP_COVERAGE_SCENE) {
        status = "Scene only";
    } else {
        j->enemies_partial++;
    }
    if (report) fprintf(report, "%s\t%s\t%s\t%s\n", asset->id, status, detail, audio);
}

static void prepare_enemies(PeImportJob *j) {
    const PeImpTarget *tg = target_for("enemies");
    if (!tg || !tg->place || !tg->accepts || !tg->current) return;
    char builder[64];
    snprintf(builder, sizeof builder, "pe_import 1, %s %u", tg->module, tg->version);
    char path[1200], temp[1232];
    snprintf(path, sizeof path, "%s/%s%s", j->store, j->game ? j->game : "",
             j->game ? "-enemy-report.tsv" : "enemy-report.tsv");
    snprintf(temp, sizeof temp, "%s.tmp", path);
    FILE *report = fopen(temp, "wb");
    if (report) fputs("Asset\tStatus\tAnimation coverage\tAudio coverage\n", report);
    for (int r = 0; r < pe_imp_readers(); r++) {
        const char *game = pe_imp_reader(r)->game;
        if (j->game && strcmp(j->game, game)) continue;
        int seen = 0;
        for (int earlier = 0; earlier < r; earlier++)
            if (!strcmp(game, pe_imp_reader(earlier)->game)) seen = 1;
        if (seen) continue;
        PeImpAsset *assets = NULL;
        const int count = pe_imp_catalog_assets(j->store, game, &assets);
        for (int i = 0; i < count; i++) {
            const PeImpAsset *a = &assets[i];
            if (strcmp(a->type, "Model") || !tg->accepts(a->id)) continue;
            const PeImpEnemyProfile *profile = pe_imp_enemy_for(a->id);
            char moves[PE_IMP_ID_MAX], folder[1100], why[240], log[160], what[160];
            if (profile) snprintf(moves, sizeof moves, "%s:%s", game, profile->moves);
            else snprintf(moves, sizeof moves, "%s/moves", a->id);
            const PeImpAsset *anim = bsearch(moves, assets, (size_t)count, sizeof *assets, compare_asset_id);
            if (tg->current(a, builder)) { j->enemies_current++; report_enemy(j, report, tg, a); continue; }
            if (!anim || strcmp(anim->type, "Animation")) {
                j->enemies_partial++;
                if (report) fprintf(report, "%s\tIncomplete\tMissing source animations\tUnavailable\n", a->id);
                continue;
            }
            snprintf(what, sizeof what, "preparing %s (%s)", a->name, pe_imp_game_title(game));
            PE_IMP_PROGRESS(j->progress, what, i + 1, count);
            const PeImpBuild ctx = {j->store, NULL, j->step}; /* enemy packs use only the import store */
            if (tg->place(a, builder, folder, sizeof folder, why, sizeof why) ||
                tg->build(&ctx, a->id, folder, log, sizeof log, why, sizeof why)) {
                j->enemies_failed++;
                pe_mod_log("pe_import", "enemy %s: %s", a->id, why);
                if (report) fprintf(report, "%s\tFailed\t%s\tUnavailable\n", a->id, why);
                continue;
            }
            j->enemies_built++;
            report_enemy(j, report, tg, a);
            pe_mod_log("pe_import", "enemy %s: %s", a->id, log);
        }
        free(assets);
    }
    if (report) {
        const int ok = !ferror(report);
        if (!fclose(report) && ok && SDL_RenamePath(temp, path)) j->enemy_report_saved = 1;
        else SDL_RemovePath(temp);
    }
}

void pe_import_job_run(PeImportJob *j) {
    j->discs_read = j->unknown = j->failed = j->items = j->built = j->current = j->missing = j->unchanged = 0;
    j->games[0] = j->problem[0] = j->built_names[0] = j->missing_discs[0] = 0;
    j->enemies_built = j->enemies_current = j->enemies_failed = 0;
    j->enemies_complete = j->enemies_partial = j->enemy_report_saved = 0;
    if (!j->enemies_only) {
        scan(j);
        build_autos(j);
    }
    if (j->prepare_enemies) prepare_enemies(j);
}
