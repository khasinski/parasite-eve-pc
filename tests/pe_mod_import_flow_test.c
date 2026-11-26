/* The automatic import end to end, without the page: the job the worker runs at start-up
 * (mods/pe_import/pe_import_job.c), then the Wardrobe adding the result while the game runs.
 *   start-up   pe_src_re1, pe_src_re2, pe_src_re3 and pe_src_dc1's own inits (games, readers,
 *              naming, rigs; Chris, Jill, Wesker, Forrest, Claire, Leon, Jill, Carlos and Regina automatic); the skins target as the Wardrobe registers it; packs/ empty
 *   run        the folder holds RE2 disc 2: it is read, Claire built into
 *              <prefs>/packs/re2-local/characters/claire.re2, Leon reported as needing disc 1
 *   folder     the folder of all the games: RE1, RE2 disc 1, RE3 and Dino Crisis read; Chris, Jill
 *              (RE1), Leon, Jill (RE3), Carlos and Regina built
 *   Wardrobe   pe_packs_add_new: Claire is a costume at once (key re2-local/claire.re2), no restart
 *   run again  nothing rebuilt (same source file, same builder), nothing new for the Wardrobe
 * Needs PE_TEST_DISC (Parasite Eve disc 1 .cue) and PE_TEST_RE2_DISC (RE2 disc 2 .cue); else 77.
 * The shape check (host pe_model_check_skin) needs the game: model.bin is byte for byte
 * tools/re2-skin's (mod-skin-build), which passes it in the game. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>

#include "pe_imp.h"
#include "pe_imp_internal.h"
#include "pe_imp_pe.h"
#include "pe_mod_host.h"
#include "pe_mod_menu.h"
#include "pe_model.h"
#include "pe_skin_build.h"
#include "pe_src_re1.h"
#include "pe_src_re2.h"
#include "pe_src_re3.h"
#include "pe_src_dc1.h"
#include "pe_wardrobe.h"
#include "pe_import.h"
#include "pe_enemy_build.h"

extern const PeMod pe_mod_pe_src_re1, pe_mod_pe_src_re2, pe_mod_pe_src_re3, pe_mod_pe_src_dc1;

#ifndef PE_SOURCE_DIR
#define PE_SOURCE_DIR "."
#endif

static char s_dir[600];
int pe_mod_path(char *out, size_t size, const char *file) { snprintf(out, size, "%s/%s", s_dir, file); return 0; }
void pe_mod_log(const char *m, const char *f, ...) { (void)m; (void)f; }
int pe_model_check_skin(const PeSkinFiles *f, char *why, size_t size) {
    (void)why, (void)size;
    for (int i = 0; i < 8; ++i) assert(f->day[i] && f->day_size[i]);
    assert(f->texture && f->model);
    return 1;
}
static const PeCostume *s_added[64];
static int s_nadded;
static int add(const PeCostume *c) { s_added[s_nadded] = c; return s_nadded++; }
const PeCostumesApi g_pe_costumes_api = {add, NULL, NULL};
int pe_menu_register_dynamic_choice(const char *s, const char *l, const char *d, int *i, unsigned m, PeMenuChoiceCount c, PeMenuChoiceName n) {
    (void)s, (void)l, (void)d, (void)i, (void)m, (void)c, (void)n;
    return 0;
}
int pe_menu_register_info(const char *s, const char *l, const char *d, PeMenuText t) { (void)s, (void)l, (void)d, (void)t; return 0; }

static int normal_table(int16_t out[240][4]) {
    FILE *f = fopen(PE_SOURCE_DIR "/src/main/render/object.c", "rb");
    if (!f) return -1;
    static char text[1 << 21];
    size_t n = fread(text, 1, sizeof text - 1, f);
    fclose(f);
    text[n] = 0;
    char *p = strstr(text, "g_RenderNormalTable[240] = {");
    if (!p) return -1;
    p = strchr(p, '{') + 1;
    for (int i = 0; i < 240; i++) {
        int x, y, z, w;
        if (!(p = strchr(p, '{')) || sscanf(p, "{%d,%d,%d,%d}", &x, &y, &z, &w) != 4) return -1;
        out[i][0] = (int16_t)x, out[i][1] = (int16_t)y, out[i][2] = (int16_t)z, out[i][3] = (int16_t)w;
        p++;
    }
    return 0;
}

/* The job's host for this test (import_host.h): Aya's data from the PE disc images, the game's 450
 * rooms (the game uses its open discs), and the normals read from the game's source. */
static char s_pe1[1200];
static int16_t s_normals[240][4];
static PeImpPe1Build s_targets;
static void test_init(void) {}
static const void *test_open(char *why, size_t n) {
    char pe2[1200];
    snprintf(pe2, sizeof pe2, "%s", s_pe1);
    char *d1 = strstr(pe2, "Disc 1");
    if (d1) d1[5] = '2';
    PeImpPe *pe = pe_imp_pe_open(s_pe1, d1 ? pe2 : NULL, why, n);
    if (!pe) return NULL;
    pe_imp_pe_set_rooms(pe, 450);
    s_targets.pe = pe, s_targets.normals = (const int16_t (*)[4])s_normals;
    return &s_targets;
}
static void test_close(const void *targets) {
    if (targets == &s_targets) pe_imp_pe_close(s_targets.pe), s_targets.pe = NULL;
}
static void test_bind_frame(const char *mod_id, int (*frame)(void *event, void *user)) { (void)mod_id, (void)frame; }
static const ImportHost k_test_host = {sizeof k_test_host, test_init, test_open, test_close, test_bind_frame};

/* what the page would show: every job and step report */
static int s_reading, s_building, s_rooms, s_steps;
static void on_job(void *u, const char *what, int done, int total) {
    (void)u;
    assert(done >= 1 && done <= total);
    if (!strncmp(what, "reading ", 8)) s_reading++;
    if (!strncmp(what, "building ", 9)) s_building++, printf("  progress: %s, %d of %d\n", what, done, total);
}
static void on_step(void *u, const char *what, int done, int total) {
    (void)u;
    assert(done >= 1 && done <= total);
    if (!strcmp(what, "room animations")) s_rooms++;
    else s_steps++;
}
static const PeImpProgress k_job = {on_job, NULL}, k_step = {on_step, NULL};

static void remove_tree(const char *path) {
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(path, &info)) return;
    if (info.type == SDL_PATHTYPE_DIRECTORY) {
        int count = 0;
        char **list = SDL_GlobDirectory(path, NULL, 0, &count);
        for (int i = 0; list && i < count; ++i) {
            char child[900];
            snprintf(child, sizeof child, "%s/%s", path, list[i]);
            remove_tree(child);
        }
        SDL_free(list);
    }
    SDL_RemovePath(path);
}

static void test_enemy_preparation(PeImportJob *j) {
    char enemy_dir[900], profile[950], why[200] = "";
    snprintf(enemy_dir, sizeof enemy_dir, "%s/pe_enemies", s_dir);
    remove_tree(enemy_dir);
    pe_imp_registry_reset();
    assert(!pe_mod_pe_src_re2.init() && !pe_enemy_target_register());
    const PeImpTarget *target = pe_imp_target(0);
    assert(target->accepts("re2:enemy/em10-cdemd2"));
    assert(!target->accepts("re2:enemy/em40") && !target->accepts("re2:enemy/em40-cdemd0"));
    j->nextra = 0;
    j->prepare_enemies = 1;
    pe_import_job_run(j);
    printf("enemy preparation: %d built, %d current, %d failed\n", j->enemies_built, j->enemies_current, j->enemies_failed);
    assert(j->enemies_built > 30 && !j->enemies_failed && !j->enemies_current && !j->enemies_partial);
    const int enemies = j->enemies_built;
    SDL_PathInfo info;
    snprintf(profile, sizeof profile, "%s/re2-em40/enemy.json", enemy_dir);
    assert(!SDL_GetPathInfo(profile, &info));

    snprintf(enemy_dir, sizeof enemy_dir, "%s/pe_enemies/re2-em10", s_dir);
    snprintf(profile, sizeof profile, "%s/profile.json", enemy_dir);
    FILE *f = fopen(profile, "wb");
    assert(f);
    fputs("{\"format\":1,\"name\":\"Edited\",\"moves\":\"enemy/em10/moves\",\"hp\":123,\"height\":500,\"anims\":{\"idle\":0}}", f);
    assert(!fclose(f));
    pe_import_job_run(j);
    assert(j->enemies_built == 1 && j->enemies_current == enemies - 1 && !j->enemies_failed);
    PeEnemyPack edited;
    assert(!pe_enemy_pack_load(&edited, enemy_dir, why, sizeof why));
    assert(edited.hp == 123 && !strcmp(edited.name, "Edited"));
    pe_enemy_pack_free(&edited);
    pe_import_job_run(j);
    assert(!j->enemies_built && j->enemies_current == enemies && !j->enemies_failed);
    assert(j->enemy_report_saved && j->enemies_complete > 0 && j->enemies_partial > 0);
    snprintf(profile, sizeof profile, "%s/enemy-report.tsv", j->store);
    FILE *report = fopen(profile, "rb");
    assert(report);
    int edited_reported = 0, complete_reported = 0, scene_reported = 0;
    while (fgets(why, sizeof why, report)) {
        edited_reported |= !strcmp(why, "re2:enemy/em10\tIncomplete\tMissing walk, hurt, death, attack\thurt, death\n");
        complete_reported |= !strcmp(why, "re2:enemy/em20\tComplete\tAll animation roles covered\tattack, hurt, death\n");
        scene_reported |= !strcmp(why, "re2:enemy/em2c\tScene only\tScripted scene actor\tNone\n");
    }
    fclose(report);
    assert(edited_reported && complete_reported && scene_reported && j->enemies_partial == 1);
}

/* Use an already imported fixture and add just the new RE2 bank. */
static void test_enemy_sounds(const char *store) {
    char why[256], folder[1100], summary[180], re2_store[700];
    const char *disc = getenv("PE_TEST_RE2_DISC");
    assert(disc && *disc);
    pe_imp_registry_reset();
    assert(!pe_imp_add_reader(&g_re2_enemy_sound_ps1) && !pe_imp_add_reader(&g_re2_emd_ps1));
    snprintf(re2_store, sizeof re2_store, "%s/store", s_dir);
    PeImpScan scan;
    assert(!pe_imp_scan(re2_store, disc, "re2", &scan));
    assert(!scan.refused && scan.files_read + scan.skipped > 1);
    for (unsigned bank = 0; bank < 72; bank++) {
        char id[64];
        PeImpSound *sample = NULL;
        snprintf(id, sizeof id, "re2:sound/enemy%02u/wave01", bank);
        if (pe_imp_catalog_sound(re2_store, id, &sample, why, sizeof why)) {
            fprintf(stderr, "%s: %s\n", id, why);
            assert(0);
        }
        assert(sample->frames > 0);
        free(sample);
    }
    assert(!pe_re2_enemies_register() && !pe_re1_enemy_sounds_register() && !pe_enemy_target_register());
    assert(!pe_imp_set_enemy_defaults("re2", PE_RE_ENEMY_YAW, PE_RE_ENEMY_SCALE));
    static const unsigned added_kinds[] = {0x23, 0x25, 0x27, 0x28, 0x2a, 0x2b, 0x2d, 0x2e, 0x2f, 0x30, 0x37, 0x39, 0x3b};
    for (unsigned i = 0; i < sizeof added_kinds / sizeof added_kinds[0]; ++i) {
        char asset[32], sample_id[64];
        snprintf(asset, sizeof asset, "re2:enemy/em%02x", added_kinds[i]);
        const PeImpEnemyCues *cues = pe_imp_enemy_cues(asset);
        assert(cues);
        for (int c = 0; c < PE_IMP_CUE_COUNT; ++c) {
            if (!cues->sound[c]) continue;
            PeImpSound *sample = NULL;
            snprintf(sample_id, sizeof sample_id, "re2:%s", cues->sound[c]);
            assert(!pe_imp_catalog_sound(re2_store, sample_id, &sample, why, sizeof why));
            assert(sample->frames > 1);
            free(sample);
        }
    }
    const PeImpTarget *target = pe_imp_target(0);
    static const char *const ids[] = {
        "re2:enemy/em10", "re2:enemy/em20", "re2:enemy/em22", "re2:enemy/em24",
        "re1:enemy/em1000", "re1:enemy/em1001", "re1:enemy/em1011",
        "re1:enemy/em1100", "re1:enemy/em1101", "re1:enemy/em1111"
    };
    for (unsigned i = 0; i < sizeof ids / sizeof ids[0]; i++) {
        const PeImpBuild ctx = {strncmp(ids[i], "re2:", 4) ? store : re2_store, NULL, NULL};
        snprintf(folder, sizeof folder, "%s/%s", s_dir, strrchr(ids[i], '/') + 1);
        assert(!pe_imp_mkdirs(folder));
        assert(!target->build(&ctx, ids[i], folder, summary, sizeof summary, why, sizeof why));
        PeEnemyPack pack;
        assert(!pe_enemy_pack_load(&pack, folder, why, sizeof why));
        assert(pack.sounds == PE_IMP_CUE_COUNT + 1);
        for (int cue = 1; cue < pack.sounds; cue++) assert(pack.sound[cue].frames > 1);
        assert(pack.attacks > 0);
        const PeEnemyAttack *attack = &pack.attack[0];
        assert((pack.anim[attack->anim].event[attack->hit] & PE_ENEMY_EVENT_SOUND) == PE_IMP_CUE_ATTACK + 1);
        assert(pack.role[PE_ANIM_HURT] >= 0 && pack.role[PE_ANIM_DIE] >= 0);
        assert((pack.anim[pack.role[PE_ANIM_HURT]].event[0] & PE_ENEMY_EVENT_SOUND) == PE_IMP_CUE_HURT + 1);
        assert((pack.anim[pack.role[PE_ANIM_DIE]].event[0] & PE_ENEMY_EVENT_SOUND) == PE_IMP_CUE_DIE + 1);
        assert(pe_enemy_pack_audio_roles(&pack) == (PE_AUDIO_ATTACK | PE_AUDIO_HURT | PE_AUDIO_DEATH));
        pe_enemy_pack_free(&pack);
        printf("%s: %s\n", ids[i], summary);
    }
    static const struct { const char *id; int flight, attack, cues; } more[] = {
        {"re2:enemy/em21", 1, 1, 3}, {"re2:enemy/em23", 0, 0, 2}, {"re2:enemy/em26", 0, 0, 0},
        {"re2:enemy/em27", 0, 1, 3}, {"re2:enemy/em28", 0, 1, 2},
        {"re2:enemy/em29", 0, 1, 3}, {"re2:enemy/em2b", 0, 1, 1}, {"re2:enemy/em2d", 0, 1, 3},
        {"re2:enemy/em2e", 0, 1, 3}, {"re2:enemy/em2f", 0, 1, 2}, {"re2:enemy/em31", 0, 1, 3},
        {"re2:enemy/em33", 0, 1, 3}, {"re2:enemy/em34", 0, 0, 1}, {"re2:enemy/em36", 0, 1, 3},
        {"re2:enemy/em37", 0, 1, 1}, {"re2:enemy/em39", 0, 1, 3},
        {"re2:enemy/em3a", 1, 1, 3}, {"re2:enemy/em3b", 0, 1, 2}
    };
    const PeImpBuild ctx = {re2_store, NULL, NULL};
    for (unsigned i = 0; i < sizeof more / sizeof more[0]; i++) {
        snprintf(folder, sizeof folder, "%s/%s", s_dir, strrchr(more[i].id, '/') + 1);
        assert(!pe_imp_mkdirs(folder));
        assert(!target->build(&ctx, more[i].id, folder, summary, sizeof summary, why, sizeof why));
        PeEnemyPack pack;
        assert(!pe_enemy_pack_load(&pack, folder, why, sizeof why));
        assert((pack.altitude > 0) == more[i].flight);
        int cues = 0;
        for (int cue = 1; cue < pack.sounds; cue++) cues += pack.sound[cue].frames > 1;
        assert(cues == more[i].cues);
        const int audio = pe_enemy_pack_audio_roles(&pack);
        if (!strcmp(more[i].id, "re2:enemy/em26")) {
            assert(pack.speed > 0 && pack.role[PE_ANIM_WALK] >= 0 && pack.role[PE_ANIM_HURT] < 0);
            assert(pack.attacks == 1 && pack.attack[0].hit == 8);
            const PeEnemyAnim *leap = &pack.anim[pack.attack[0].anim];
            const int16_t *apex = leap->data + pack.attack[0].hit * (3 + 3 * pack.bones);
            assert(leap->frames == 16 && apex[1] < leap->data[1] && apex[0] > leap->data[0]);
        }
        if (!strcmp(more[i].id, "re2:enemy/em23"))
            assert(pack.attacks > 0 && pack.role[PE_ANIM_WALK] >= 0 && pack.role[PE_ANIM_HURT] >= 0);
        if (!strcmp(more[i].id, "re2:enemy/em27"))
            assert(pack.role[PE_ANIM_WALK] >= 0 && pack.role[PE_ANIM_DIE] >= 0);
        if (!strcmp(more[i].id, "re2:enemy/em28")) {
            assert(pack.role[PE_ANIM_HURT] >= 0 && pack.role[PE_ANIM_DIE] >= 0);
            assert(audio == (PE_AUDIO_ATTACK | PE_AUDIO_DEATH));
        }
        if (!strcmp(more[i].id, "re2:enemy/em29"))
            assert(pack.speed > 0 && audio == (PE_AUDIO_ATTACK | PE_AUDIO_HURT | PE_AUDIO_DEATH));
        if (!strcmp(more[i].id, "re2:enemy/em2b"))
            assert(pack.role[PE_ANIM_WALK] >= 0 && pack.role[PE_ANIM_HURT] >= 0 && pack.role[PE_ANIM_DIE] >= 0);
        if (!strcmp(more[i].id, "re2:enemy/em2d"))
            assert(!pack.speed && pack.role[PE_ANIM_HURT] >= 0 &&
                   audio == (PE_AUDIO_ATTACK | PE_AUDIO_HURT | PE_AUDIO_DEATH));
        if (!strcmp(more[i].id, "re2:enemy/em2e") || !strcmp(more[i].id, "re2:enemy/em39"))
            assert(pack.role[PE_ANIM_HURT] >= 0);
        if (!strcmp(more[i].id, "re2:enemy/em2f")) {
            assert(!pack.speed && pack.role[PE_ANIM_WALK] < 0 && pack.role[PE_ANIM_HURT] >= 0 &&
                   pack.role[PE_ANIM_DIE] >= 0);
            assert(audio == (PE_AUDIO_ATTACK | PE_AUDIO_DEATH));
        }
        if (!strcmp(more[i].id, "re2:enemy/em36")) {
            assert(pack.speed > 0 && pack.role[PE_ANIM_WALK] >= 0 && pack.role[PE_ANIM_HURT] >= 0 &&
                   pack.role[PE_ANIM_DIE] >= 0);
            assert(audio == (PE_AUDIO_ATTACK | PE_AUDIO_HURT | PE_AUDIO_DEATH));
        }
        if (!strcmp(more[i].id, "re2:enemy/em37"))
            assert(!pack.speed && pack.role[PE_ANIM_WALK] < 0 && audio == PE_AUDIO_ATTACK);
        if (!strcmp(more[i].id, "re2:enemy/em3b"))
            assert(pack.speed > 0 && pack.role[PE_ANIM_WALK] >= 0 && pack.attacks == 1 &&
                   pack.attack[0].hit == 29 && audio == (PE_AUDIO_ATTACK | PE_AUDIO_DEATH));
        if (more[i].attack) {
            assert(pack.attacks > 0);
            const PeEnemyAttack *attack = &pack.attack[0];
            assert((pack.anim[attack->anim].event[attack->hit] & PE_ENEMY_EVENT_SOUND) == PE_IMP_CUE_ATTACK + 1);
        }
        pe_enemy_pack_free(&pack);
        printf("%s: %s\n", more[i].id, summary);
    }
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    const char *test_dir = getenv("PE_TEST_DIR") ? getenv("PE_TEST_DIR") : ".";
    if (argc == 3 && !strcmp(argv[1], "--enemy-sounds")) {
        snprintf(s_dir, sizeof s_dir, "%s/pe-import-flow-sounds", test_dir);
        test_enemy_sounds(argv[2]);
        return 0;
    }
    /* Repeat the enemy regression using a populated test catalog without rescanning the discs. */
    if (argc == 3 && !strcmp(argv[1], "--enemy-store")) {
        PeImportJob j = {.host = &k_test_host};
        snprintf(s_dir, sizeof s_dir, "%s/pe-import-flow-enemies", test_dir);
        snprintf(j.discs, sizeof j.discs, "%s/no-discs", s_dir);
        snprintf(j.store, sizeof j.store, "%s", argv[2]);
        test_enemy_preparation(&j);
        return 0;
    }
    const char *pe1 = getenv("PE_TEST_DISC"), *re2 = getenv("PE_TEST_RE2_DISC");
    if (!pe1 || !*pe1 || !re2 || !*re2) { printf("PE_TEST_DISC and PE_TEST_RE2_DISC not set: skipped\n"); return 77; }
    char store[700], packs[700];
    snprintf(s_dir, sizeof s_dir, "%s/pe-import-flow", test_dir);
    snprintf(store, sizeof store, "%s/import/store", s_dir);
    snprintf(packs, sizeof packs, "%s/packs", s_dir);
    remove_tree(packs);
    remove_tree(store);                                  /* the first run expects an empty store */

    /* start-up: RE1, RE2, RE3 and Dino Crisis's own mods register themselves, the Wardrobe its skins target, packs/ is empty */
    pe_imp_registry_reset();
    assert(pe_mod_pe_src_re1.init() == 0 && pe_mod_pe_src_re2.init() == 0 && pe_mod_pe_src_re3.init() == 0 && pe_mod_pe_src_dc1.init() == 0);
    assert(!pe_skin_target_register() && pe_imp_targets() == 1 && pe_imp_autos() == 15);   /* 9 costumes, and RE3 PC enemies: Nemesis, Zombie, Cerberus, Hunter, Giant Spider, Drain Deimos */
    pe_packs_scan();
    pe_packs_register();
    assert(s_nadded == 0);

    /* the automatic import, as the worker runs it: the folder holds RE2 disc 2 only */
    snprintf(s_pe1, sizeof s_pe1, "%s", pe1);
    PeImportJob j;
    memset(&j, 0, sizeof j);
    snprintf(j.discs, sizeof j.discs, "%s", re2);
    char *slash = strrchr(j.discs, '/'), *back = strrchr(j.discs, '\\');
    if (!slash || (back && back > slash)) slash = back;
    assert(slash);
    *slash = 0;                                                     /* the disc's folder */
    snprintf(j.store, sizeof j.store, "%s", store);
    snprintf(j.packs, sizeof j.packs, "%s", packs);
    assert(!normal_table(s_normals));
    j.host = &k_test_host;
    j.progress = &k_job, j.step = &k_step;
    pe_import_job_run(&j);
    printf("  progress: %d discs read reported, %d builds, %d build steps, %d room-scan reports\n", s_reading, s_building, s_steps, s_rooms);
    assert(s_reading >= 1 && s_building == 1 && s_steps == 3 && s_rooms >= 90);   /* 900 rooms, every 10th */
    printf("first run: %d disc(s) (%s), %d catalog items, built %d (%s), current %d, missing %d (%s)%s%s\n", j.discs_read, j.games,
           j.items, j.built, j.built_names, j.current, j.missing, j.missing_discs, j.failed ? ", problem: " : "", j.problem);
    assert(j.discs_read == 1 && !j.failed && j.built == 1 && j.current == 0 && j.missing == 8);
    assert(strstr(j.built_names, "Claire") && strstr(j.missing_discs, "disc 1"));

    /* the main thread then tells the Wardrobe: Claire is a costume at once, no restart */
    char added[200];
    assert(pe_packs_add_new(added, sizeof added) == 1 && !strcmp(added, "Claire (RE2)"));
    const PeCostume *c = NULL;
    for (int i = 0; i < s_nadded; i++) if (!strcmp(s_added[i]->key, "re2-local/claire.re2")) c = s_added[i];
    assert(c && !strcmp(c->name, "Claire (RE2)") && !strcmp(c->group, "Resident Evil") && c->bank == -1 &&
           !strcmp(c->bundle, "@re2-local/claire.re2"));
    unsigned char *data;
    unsigned size;
    assert(pe_pack_read("@re2-local/claire.re2", "model.bin", &data, &size, 4u << 20) && size > 1000);
    free(data);
    assert(pe_pack_has("@re2-local/claire.re2", "rooms.bin") && pe_pack_has("@re2-local/claire.re2", "meta.bin"));
    printf("the Wardrobe added %s, \"%s\" in %s, while running\n", c->key, c->name, c->group);

    /* the next start: nothing to read or build again, nothing new for the Wardrobe */
    pe_import_job_run(&j);
    printf("second run: %d discs read, %d unchanged, built %d, current %d, missing %d\n", j.discs_read, j.unchanged,
           j.built, j.current, j.missing);
    assert(j.discs_read == 0 && j.unchanged == 1 && j.built == 0 && j.current == 1 && j.missing == 8 && !j.failed);
    assert(pe_packs_add_new(added, sizeof added) == 0);

    /* an added folder: the folder of games the disc came from (one game per subfolder, other
     * games' discs beside it). RE1, RE2 disc 1, RE3 and Dino Crisis are there, so Chris, Jill, Leon,
     * Jill, Carlos and Regina are built;
     * the rest are read or reported as not supported; nothing fails. */
    snprintf(j.extra[0], sizeof j.extra[0], "%s", j.discs);
    slash = strrchr(j.extra[0], '/'), back = strrchr(j.extra[0], '\\');
    if (!slash || (back && back > slash)) slash = back;
    assert(slash);
    *slash = 0;
    j.nextra = 1;
    pe_import_job_run(&j);
    printf("with %s: %d disc(s) (%s), %d not supported, built %d (%s), current %d, missing %d%s%s\n", j.extra[0],
           j.discs_read, j.games, j.unknown, j.built, j.built_names, j.current, j.missing, j.failed ? ", problem: " : "", j.problem);
    printf("  (%d unchanged)%s%s\n", j.unchanged, j.missing ? "; missing: " : "", j.missing ? j.missing_discs : "");
    fflush(stdout);
    assert(!j.failed && j.missing == 0 && j.current == 1 && j.built == 8 && strstr(j.built_names, "Leon") && strstr(j.built_names, "Regina") &&
           strstr(j.built_names, "Wesker") && strstr(j.built_names, "Forrest"));
    assert(strstr(j.built_names, "Jill") && strstr(j.built_names, "Carlos") && strstr(j.built_names, "Chris"));
    assert(j.discs_read >= 5 && j.unknown >= 4);   /* at least the five discs it was written against; a folder may hold more */
    assert(!strncmp(j.games, "Resident Evil, ", 15) || strstr(j.games, ", Resident Evil,") || strstr(j.games, "Resident Evil 3, Resident Evil"));     /* RE1, RE2 disc 1, the Dual Shock disc 1, RE3, DC1 */
    assert(strstr(j.games, "Dino Crisis"));
    assert(pe_packs_add_new(added, sizeof added) == 8 && strstr(added, "Forrest (RE1)") && strstr(added, "Regina (DC1)") && strstr(added, "Chris (RE1)") && strstr(added, "Jill (RE1)") &&
           strstr(added, "Leon (RE2)") && strstr(added, "Jill (RE3)") &&
           strstr(added, "Carlos (RE3)"));
    for (int i = 0; i < s_nadded; i++)
        if (!strcmp(s_added[i]->key, "re3-local/jill.re3"))
            assert(!strcmp(s_added[i]->group, "Resident Evil") && pe_pack_has("@re3-local/jill.re3", "rooms.bin"));
    printf("the Wardrobe added \"%s\" while running\n", added);

    /* and again: every image unchanged, none opened, nothing built */
    pe_import_job_run(&j);
    printf("last run: %d read, %d not supported, %d unchanged, built %d, current %d\n", j.discs_read, j.unknown,
           j.unchanged, j.built, j.current);
    assert(j.discs_read == 0 && j.unknown == 0 && j.unchanged >= 11 && j.built == 0 && j.current == 9 && !j.failed);

    /* Prepare RE2's catalogued enemies through the real target, then check the incremental run. */
    test_enemy_preparation(&j);
    printf("import flow passed\n");
    return 0;
}
