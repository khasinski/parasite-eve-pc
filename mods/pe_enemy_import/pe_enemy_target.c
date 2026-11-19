/* The enemies module's builder target, registered with the importer (F1 > Import): a catalogued
 * model with an enemy profile becomes an enemy pack (pe_enemy_build.c), with the moves and sounds
 * its profile names. Import mode only. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pe_enemy_battle.h"
#include "pe_enemy_build.h"
#include "pe_mod_host.h"

static int enemies_build(const PeImpBuild *ctx, const char *asset_id, const char *folder, char *log, size_t logn,
                         char *why, size_t n) {
    const PeImpEnemyProfile *prof = pe_imp_enemy_for(asset_id);
    if (!prof) { snprintf(why, n, "no enemy profile for %s", asset_id); return -1; }
    const char *colon = strchr(asset_id, ':');
    char game[16], id[PE_IMP_ID_MAX];
    snprintf(game, sizeof game, "%.*s", (int)(colon - asset_id), asset_id);
    PeImpModel m;
    PeImpAnimation a;
    if (pe_imp_catalog_model(ctx->store, asset_id, &m, why, n)) return -1;
    snprintf(id, sizeof id, "%s:%s", game, prof->moves);
    if (pe_imp_catalog_animation(ctx->store, id, &a, why, n)) { pe_imp_model_free(&m); return -1; }
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
    if (rc) return -1;
    rc = pe_enemy_write(&p, folder, why, n);
    if (!rc) pe_enemy_packs_changed();
    if (!rc && log && logn)
        snprintf(log, logn, "%d bones, %d vertices, %d triangles, %d moves, %d sounds (%d sound frames), scale %.3f",
                 p.bones, p.vertices, p.triangles, p.anims, info.sounds, info.sound_events, info.scale);
    pe_enemy_pack_free(&p);
    return rc;
}

static int enemies_accepts(const char *asset_id) { return pe_imp_enemy_for(asset_id) != NULL; }

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
    if (enemy_folder(a, folder, sizeof folder)) return 0;
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
    {PE_IMP_API_VERSION, sizeof(PeImpTarget)}, "enemies", 1, PE_IMP_MODEL, "Build as an enemy", 0, enemies_build,
    enemies_accepts, enemies_place, enemies_current};

int pe_enemy_target_register(void) { return pe_imp_add_target(&g_pe_enemy_target); }
