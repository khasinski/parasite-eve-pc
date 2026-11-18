/* The automatic import end to end, without the page: the job the worker runs at start-up
 * (mods/pe_import/pe_import_job.c), then the Wardrobe adding the result while the game runs.
 *   start-up   pe_src_re1, pe_src_re2 and pe_src_re3's own inits (games, readers, naming, rigs;
 *              Chris, Jill, Claire, Leon, Jill and Carlos automatic); the skins target as the Wardrobe registers it; packs/ empty
 *   run        the folder holds RE2 disc 2: it is read, Claire built into
 *              <prefs>/packs/re2-local/characters/claire.re2, Leon reported as needing disc 1
 *   folder     the folder of all the games: RE1, RE2 disc 1 and RE3 read; Chris, Jill (RE1), Leon,
 *              Jill (RE3) and Carlos built
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
#include "pe_wardrobe.h"
#include "pe_import.h"

extern const PeMod pe_mod_pe_src_re1, pe_mod_pe_src_re2, pe_mod_pe_src_re3;

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

/* Aya's data from the PE disc images, the game's 450 rooms (the game uses its open discs) */
static char s_pe1[1200];
static struct PeImpPe *test_source(char *why, size_t n) {
    char pe2[1200];
    snprintf(pe2, sizeof pe2, "%s", s_pe1);
    char *d1 = strstr(pe2, "Disc 1");
    if (d1) d1[5] = '2';
    PeImpPe *pe = pe_imp_pe_open(s_pe1, d1 ? pe2 : NULL, why, n);
    if (pe) pe_imp_pe_set_rooms(pe, 450);
    return pe;
}

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

int main(void) {
    const char *pe1 = getenv("PE_TEST_DISC"), *re2 = getenv("PE_TEST_RE2_DISC");
    if (!pe1 || !*pe1 || !re2 || !*re2) { printf("PE_TEST_DISC and PE_TEST_RE2_DISC not set: skipped\n"); return 77; }
    char store[700], packs[700];
    snprintf(s_dir, sizeof s_dir, "%s/pe-import-flow", getenv("PE_TEST_DIR") ? getenv("PE_TEST_DIR") : ".");
    snprintf(store, sizeof store, "%s/import/store", s_dir);
    snprintf(packs, sizeof packs, "%s/packs", s_dir);
    remove_tree(packs);

    /* start-up: RE1, RE2 and RE3's own mods register themselves, the Wardrobe its skins target, packs/ is empty */
    pe_imp_registry_reset();
    assert(pe_mod_pe_src_re1.init() == 0 && pe_mod_pe_src_re2.init() == 0 && pe_mod_pe_src_re3.init() == 0);
    assert(!pe_skin_target_register() && pe_imp_targets() == 1 && pe_imp_autos() == 6);
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
    int16_t normals[240][4];
    assert(!normal_table(normals));
    j.normals = (const int16_t (*)[4])normals;
    j.pe_source = test_source;
    j.progress = &k_job, j.step = &k_step;
    pe_import_job_run(&j);
    printf("  progress: %d discs read reported, %d builds, %d build steps, %d room-scan reports\n", s_reading, s_building, s_steps, s_rooms);
    assert(s_reading >= 1 && s_building == 1 && s_steps == 3 && s_rooms >= 90);   /* 900 rooms, every 10th */
    printf("first run: %d disc(s) (%s), %d models, built %d (%s), current %d, missing %d (%s)%s%s\n", j.discs_read, j.games,
           j.models, j.built, j.built_names, j.current, j.missing, j.missing_discs, j.failed ? ", problem: " : "", j.problem);
    assert(j.discs_read == 1 && !j.failed && j.built == 1 && j.current == 0 && j.missing == 5);
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
    assert(j.discs_read == 0 && j.unchanged == 1 && j.built == 0 && j.current == 1 && j.missing == 5 && !j.failed);
    assert(pe_packs_add_new(added, sizeof added) == 0);

    /* an added folder: the folder of games the disc came from (one game per subfolder, other
     * games' discs beside it). RE1, RE2 disc 1 and RE3 are there, so Chris, Jill, Leon, Jill and
     * Carlos are built;
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
    printf("  (%d unchanged)\n", j.unchanged);
    assert(!j.failed && j.missing == 0 && j.current == 1 && j.built == 5 && strstr(j.built_names, "Leon"));
    assert(strstr(j.built_names, "Jill") && strstr(j.built_names, "Carlos") && strstr(j.built_names, "Chris"));
    assert(j.discs_read == 4 && j.unknown == 5);
    assert(!strncmp(j.games, "Resident Evil, ", 15) || strstr(j.games, ", Resident Evil,") || strstr(j.games, "Resident Evil 3, Resident Evil"));           /* RE1, RE2 disc 1, the Dual Shock disc 1 and RE3 are new */
    assert(pe_packs_add_new(added, sizeof added) == 5 && strstr(added, "Chris (RE1)") && strstr(added, "Jill (RE1)") &&
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
    assert(j.discs_read == 0 && j.unknown == 0 && j.unchanged == 11 && j.built == 0 && j.current == 6 && !j.failed);
    printf("import flow passed\n");
    return 0;
}
