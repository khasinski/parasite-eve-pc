/* The enemies module's builder target, registered with the importer (F1 > Import): a catalogued
 * enemy model, fitted as an enemy (pe_imp_fit.h), becomes an enemy pack (pe_enemy_build.c) with the
 * moves and sounds its profile names. The profile is the source game's hand-made one when it has
 * one, else guessed from the moves (pe_imp_guess.c, the importer). Either way it is written into the pack
 * folder as profile.json; once the player changes that file, the next build uses it instead (and
 * the pack counts as out of date until then). Import mode only. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pe_enemy_battle.h"
#include "pe_imp_guess.h"
#include "pe_imp_fit.h"
#include "pe_enemy_build.h"
#include "pe_mod_host.h"

#define PROFILE_NOTE "Which moves this enemy uses (numbers count the source's moves from 0), its size, "                      "attacks and behaviour. Change it and build again in F1 > Import to use your changes."

/* a file's SHA-256 (hex), or "" when it can't be read */
static void file_sha(const char *path, char out[65]) {
    FILE *f = fopen(path, "rb");
    out[0] = 0;
    if (!f) return;
    char buf[65536];
    size_t got = fread(buf, 1, sizeof buf, f);
    const int whole = feof(f) != 0;
    fclose(f);
    if (whole) pe_imp_sha256_hex((const uint8_t *)buf, got, out);
}

/* 1 if <folder>/profile.json is there and not what the builder last wrote (profile.made) */
static int profile_changed(const char *folder) {
    char path[1200], sha[65], made[65] = "";
    snprintf(path, sizeof path, "%s/profile.json", folder);
    file_sha(path, sha);
    if (!sha[0]) return 0;
    snprintf(path, sizeof path, "%s/profile.made", folder);
    FILE *f = fopen(path, "rb");
    if (f) {
        size_t got = fread(made, 1, 64, f);
        made[got] = 0;
        fclose(f);
    }
    return strcmp(sha, made) != 0;
}

static int profile_write(const PeImpEnemyProfile *p, const char *folder, char *why, size_t n) {
    char path[1200], sha[65];
    snprintf(path, sizeof path, "%s/profile.json", folder);
    if (pe_imp_enemy_profile_save(p, PROFILE_NOTE, path, why, n)) return -1;
    file_sha(path, sha);
    snprintf(path, sizeof path, "%s/profile.made", folder);
    FILE *f = fopen(path, "wb");
    if (!f || fputs(sha, f) < 0 || fclose(f)) { snprintf(why, n, "can't write %s", path); return -1; }
    return 0;
}

/* "re2:enemy/giant_moth" -> "Giant moth", "re2:enemy/em10" -> "EM10" */
static void name_of(const char *asset_id, char *out, size_t n) {
    const char *last = strrchr(asset_id, '/');
    snprintf(out, n, "%s", last ? last + 1 : asset_id);
    const int em = out[0] == 'e' && out[1] == 'm' && out[2] >= '0' && out[2] <= '9';
    for (char *c = out; *c; c++) {
        if (*c == '_') *c = ' ';
        else if (*c >= 'a' && *c <= 'z' && (c == out || em)) *c = (char)(*c - 'a' + 'A');
    }
}

static int enemies_build(const PeImpBuild *ctx, const char *asset_id, const char *folder, char *log, size_t logn,
                         char *why, size_t n) {
    const PeImpEnemyProfile *prof = pe_imp_enemy_for(asset_id);
    const char *colon = strchr(asset_id, ':');
    char game[16], id[PE_IMP_ID_MAX], path[1200], name[48];
    const char *how = prof ? "made by hand" : "guessed";
    PeImpEnemyGuess *g = calloc(1, sizeof *g);
    if (!g) { snprintf(why, n, "out of memory"); return -1; }
    snprintf(game, sizeof game, "%.*s", (int)(colon - asset_id), asset_id);
    if (profile_changed(folder)) {                     /* the player's own profile.json */
        char err[160];
        snprintf(path, sizeof path, "%s/profile.json", folder);
        if (pe_imp_enemy_profile_load(path, g, err, sizeof err)) { snprintf(why, n, "profile.json: %s", err); free(g); return -1; }
        prof = &g->p, how = "yours (profile.json)";
    }
    PeImpModel m;
    PeImpAnimation a;
    if (pe_imp_fit(ctx->store, asset_id, PE_IMP_USE_ENEMY, &m, NULL, why, n)) { free(g); return -1; }   /* zero-area faces dropped */
    snprintf(id, sizeof id, "%s:%s", game, prof ? prof->moves : "");
    if (!prof) snprintf(id, sizeof id, "%s/moves", asset_id);   /* a model's moves sit beside it */
    if (pe_imp_catalog_animation(ctx->store, id, &a, why, n)) { pe_imp_model_free(&m); free(g); return -1; }
    if (!prof) {
        int yaw = 0, scale = 0;
        pe_imp_enemy_defaults(game, &yaw, &scale);
        name_of(asset_id, name, sizeof name);
        if (pe_imp_enemy_guess(&m, &a, name, id + (colon - asset_id) + 1, yaw, scale, g, why, n)) {
            pe_imp_model_free(&m), pe_imp_animation_free(&a), free(g);
            return -1;
        }
        if (pe_imp_enemy_known_strength(asset_id, &g->p)) how = "guessed, its game's own strength";
        prof = &g->p;
    }
    PeImpSound *sounds[PE_ENEMY_MAX_SOUNDS] = {0};
    int count = prof->sounds ? (prof->sound_count < PE_ENEMY_MAX_SOUNDS ? prof->sound_count : PE_ENEMY_MAX_SOUNDS) : 0;
    for (int i = 0; i < count; i++) {            /* a missing sound stays silent; the build goes on */
        char sid[PE_IMP_ID_MAX], ignore[160];
        snprintf(sid, sizeof sid, "%s:%s%02d", game, prof->sounds, i);
        pe_imp_catalog_sound(ctx->store, sid, &sounds[i], ignore, sizeof ignore);
    }
    PeEnemyPack p;
    PeEnemyBuildInfo info;
    int rc = pe_enemy_build(&m, &a, prof, sounds, count, asset_id, &p, &info, why, n);
    pe_imp_model_free(&m);
    pe_imp_animation_free(&a);
    for (int i = 0; i < count; i++) free(sounds[i]);
    if (!rc && strcmp(how, "yours (profile.json)")) rc = profile_write(prof, folder, why, n);
    free(g);
    if (rc) return -1;
    rc = pe_enemy_write(&p, folder, why, n);
    if (!rc) pe_enemy_packs_changed();
    if (!rc && log && logn)
        snprintf(log, logn, "%d bones, %d vertices, %d triangles, %d moves, %d sounds (%d sound frames), scale %.3f; profile %s",
                 p.bones, p.vertices, p.triangles, p.anims, info.sounds, info.sound_events, info.scale, how);
    pe_enemy_pack_free(&p);
    return rc;
}

/* a model with a profile, or any enemy model (its profile guessed) */
static int enemies_accepts(const char *asset_id) {
    const char *colon = asset_id ? strchr(asset_id, ':') : NULL;
    return pe_imp_enemy_for(asset_id) != NULL || (colon && !strncmp(colon + 1, "enemy/", 6));
}

/* <prefs>/pe_enemies/<game>-<name>: where F1 > Fights lists packs ("re3-nemesis") */
static int enemy_folder(const PeImpAsset *a, char *folder, size_t fn) {
    const char *colon = strchr(a->id, ':'), *last = strrchr(a->id, '/');
    char sub[96];
    if (!colon) return -1;
    snprintf(sub, sizeof sub, "pe_enemies/%.*s-%.40s", (int)(colon - a->id), a->id, last ? last + 1 : colon + 1);
    for (char *c = sub + 11; *c; c++) if (!((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '-' || *c == '_')) *c = '_';
    return pe_mod_path(folder, fn, sub);
}

/* import.json: {"asset", "sha256", "builder"}, written before the build; enemy.json comes last, so a
 * build that stopped half way is not current */
static int enemies_place(const PeImpAsset *a, const char *builder, char *folder, size_t fn, char *why, size_t n) {
    char path[1200];
    if (enemy_folder(a, folder, fn) || pe_imp_mkdirs(folder)) { snprintf(why, n, "can't create the enemy pack folder"); return -1; }
    snprintf(path, sizeof path, "%s/enemy.json", folder);
    remove(path);
    snprintf(path, sizeof path, "%s/import.json", folder);
    FILE *f = fopen(path, "wb");
    if (!f) { snprintf(why, n, "can't write %s", path); return -1; }
    fprintf(f, "{\"asset\": \"%s\", \"sha256\": \"%s\", \"builder\": \"%s\"}\n", a->id, a->sha256, builder);
    return fclose(f) ? (snprintf(why, n, "can't write %s", path), -1) : 0;
}

static int enemies_current(const PeImpAsset *a, const char *builder) {
    char folder[1100], path[1200], text[512], want[512];
    if (enemy_folder(a, folder, sizeof folder) || profile_changed(folder)) return 0;
    snprintf(path, sizeof path, "%s/enemy.json", folder);
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    snprintf(path, sizeof path, "%s/import.json", folder);
    if (!(f = fopen(path, "rb"))) return 0;
    size_t got = fread(text, 1, sizeof text - 1, f);
    fclose(f);
    text[got] = 0;
    snprintf(want, sizeof want, "{\"asset\": \"%s\", \"sha256\": \"%s\", \"builder\": \"%s\"}\n", a->id, a->sha256, builder);
    return !strcmp(text, want);
}

const PeImpTarget g_pe_enemy_target = {
    {PE_IMP_API_VERSION, sizeof(PeImpTarget)}, "enemies", 3, PE_IMP_MODEL, "Build as an enemy", 0, enemies_build,
    enemies_accepts, enemies_place, enemies_current};

int pe_enemy_target_register(void) { return pe_imp_add_target(&g_pe_enemy_target); }

int pe_enemy_prepare_pack(const char *store, const char *game, const char *asset_id,
                          char *folder, size_t fn, char *why, size_t n) {
    PeImpAsset *assets = NULL;
    const int count = pe_imp_catalog_assets(store, game, &assets);
    int i = 0;
    while (i < count && strcmp(assets[i].id, asset_id)) i++;
    if (i >= count || strcmp(assets[i].type, pe_imp_type_name(PE_IMP_MODEL))) {
        snprintf(why, n, "%s has no imported model", asset_id);
        free(assets);
        return -1;
    }
    const PeImpAsset *a = &assets[i];
    if (enemy_folder(a, folder, fn)) {
        snprintf(why, n, "can't resolve the enemy pack folder");
        free(assets);
        return -1;
    }
    char path[1200];
    snprintf(path, sizeof path, "%s/enemy.json", folder);
    FILE *f = fopen(path, "rb");
    if (f) {
        fclose(f);
        free(assets);
        return 0;
    }
    char builder[64];
    snprintf(builder, sizeof builder, "pe_import 1, %s %u", g_pe_enemy_target.module, g_pe_enemy_target.version);
    const PeImpBuild ctx = {.store = store};
    const int rc = enemies_place(a, builder, folder, fn, why, n) ||
                   enemies_build(&ctx, asset_id, folder, NULL, 0, why, n);
    free(assets);
    return rc ? -1 : 0;
}
