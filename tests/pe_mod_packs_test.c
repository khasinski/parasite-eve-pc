/* Character packs fixture: the production pack reader (pe_wardrobe/pe_packs.c) over real zips and
 * folders written into a scratch <preferences>/packs/, with the registry and host isolated. Each
 * refusal rule of bridge findings D8 §3 gets its own pack. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>

#include "miniz.h"
#include "pe_mod_host.h"
#include "pe_mod_menu.h"
#include "pe_model.h"
#include "pe_wardrobe.h"

static char s_dir[512];
int pe_mod_path(char *out, size_t size, const char *file) { snprintf(out, size, "%s/%s", s_dir, file); return 0; }
void pe_mod_log(const char *m, const char *f, ...) { (void)m; (void)f; }

/* The host's skin shape check (pe_model.c, tested with the model fixture): here a model.bin
 * starting "BAD" isn't Aya's shape; every other file must have been handed over. */
int pe_model_check_skin(const PeSkinFiles *f, char *why, size_t size) {
    int i;
    for (i = 0; i < 8; ++i) assert(f->day[i] && f->day_size[i]);
    assert(f->texture && f->model);
    if (!memcmp(f->model, "BAD", 3)) { snprintf(why, size, "only 1 parts (not Aya's layout)"); return 0; }
    return 1;
}

/* The registry: what the packs added. */
static const PeCostume *s_added[64];
static int s_nadded;
static int add(const PeCostume *c) {
    int i;
    for (i = 0; i < s_nadded; ++i) if (strcmp(s_added[i]->key, c->key) == 0) return -1;
    s_added[s_nadded] = c;
    return s_nadded++;
}
const PeCostumesApi g_pe_costumes_api = { add, NULL, NULL };

/* F1 rows: the Packs page's Status line, for the selected pack. */
static int *s_sel;
static PeMenuChoiceName s_pack_name;
static PeMenuText s_status;
int pe_menu_register_dynamic_choice(const char *s, const char *l, const char *d, int *i, unsigned m, PeMenuChoiceCount c, PeMenuChoiceName n) {
    (void)s; (void)l; (void)d; (void)m; (void)c; s_sel = i; s_pack_name = n; return 0;
}
int pe_menu_register_info(const char *s, const char *l, const char *d, PeMenuText t) {
    (void)s; (void)d; if (!strcmp(l, "Status")) s_status = t; return 0;
}

/* ---- writing packs ---- */
static const char *const s_files[] = {"texture.bin", "model.bin", "day1.bin", "day2.bin", "day3.bin", "day4.bin",
                                      "day5.bin", "day6.bin", "day7.bin", "day8.bin"};
#define NFILES (sizeof s_files / sizeof s_files[0])
static const char s_blob[] = "PEBLOB-0123456789";   /* over 8 bytes, as the skin loader wants */

static void put_file(const char *path, const char *text, size_t n) {
    FILE *f = fopen(path, "wb");
    assert(f);
    fwrite(text, 1, n, f);
    fclose(f);
}

static void mkdirs(const char *path) { assert(SDL_CreateDirectory(path)); }

static const char *pack_json(const char *id, const char *kind) {
    static char j[256];
    snprintf(j, sizeof j, "{\"format\":1,\"id\":\"%s\",\"name\":\"Pack %s\",\"version\":\"1.0.0\",\"author\":\"T\",\"kind\":\"%s\"}", id, id, kind);
    return j;
}
static const char *char_json(const char *name, const char *tier) {
    static char j[160];
    snprintf(j, sizeof j, "{\"name\":\"%s\",\"group\":\"Test\",\"tier\":\"%s\"}", name, tier);
    return j;
}

/* A folder pack: `skip` names a character file left out ("" for none). */
static void folder_pack(const char *dir, const char *id, const char *folder, const char *tier, const char *skip, int license) {
    char path[700], base[600];
    size_t i;
    snprintf(base, sizeof base, "%s/packs/%s", s_dir, dir);
    mkdirs(base);
    snprintf(path, sizeof path, "%s/pack.json", base);
    put_file(path, pack_json(id, "assets"), strlen(pack_json(id, "assets")));
    if (license) { snprintf(path, sizeof path, "%s/LICENSE", base); put_file(path, "CC-BY-4.0", 9); }
    snprintf(path, sizeof path, "%s/characters/%s", base, folder);
    mkdirs(path);
    snprintf(path, sizeof path, "%s/characters/%s/character.json", base, folder);
    put_file(path, char_json(folder, tier), strlen(char_json(folder, tier)));
    for (i = 0; i < NFILES; ++i) {
        if (!strcmp(s_files[i], skip)) continue;
        snprintf(path, sizeof path, "%s/characters/%s/%s", base, folder, s_files[i]);
        put_file(path, s_blob, sizeof s_blob);
    }
}

/* A zip pack with characters `a` and `b` (b may be NULL); `extra` adds one more entry by name. */
static void zip_pack(const char *file, const char *id, const char *kind, const char *a, const char *b, const char *extra,
                     const char *skip_b) {
    char path[700], name[160];
    mz_zip_archive z;
    const char *who[2] = {a, b};
    size_t i, k;
    snprintf(path, sizeof path, "%s/packs/%s", s_dir, file);
    memset(&z, 0, sizeof z);
    assert(mz_zip_writer_init_file(&z, path, 0));
    assert(mz_zip_writer_add_mem(&z, "pack.json", pack_json(id, kind), strlen(pack_json(id, kind)), MZ_DEFAULT_COMPRESSION));
    assert(mz_zip_writer_add_mem(&z, "LICENSE", "CC0", 3, MZ_DEFAULT_COMPRESSION));
    for (k = 0; k < 2; ++k) {
        if (!who[k]) continue;
        snprintf(name, sizeof name, "characters/%s/character.json", who[k]);
        assert(mz_zip_writer_add_mem(&z, name, char_json(who[k], k ? "extra" : "main"), strlen(char_json(who[k], k ? "extra" : "main")), MZ_DEFAULT_COMPRESSION));
        for (i = 0; i < NFILES; ++i) {
            if (k == 1 && skip_b && !strcmp(s_files[i], skip_b)) continue;
            snprintf(name, sizeof name, "characters/%s/%s", who[k], s_files[i]);
            assert(mz_zip_writer_add_mem(&z, name, s_blob, sizeof s_blob, MZ_NO_COMPRESSION));
        }
    }
    if (extra) assert(mz_zip_writer_add_mem(&z, extra, "x", 1, MZ_NO_COMPRESSION));
    assert(mz_zip_writer_finalize_archive(&z));
    mz_zip_writer_end(&z);
}

static const PeCostume *find(const char *key) {
    int i;
    for (i = 0; i < s_nadded; ++i) if (!strcmp(s_added[i]->key, key)) return s_added[i];
    return NULL;
}

/* The Status line of the pack listed with `part` in its name. */
static const char *status_of(const char *part) {
    static char out[200];
    int i;
    for (i = 0; i < 16; ++i) {
        *s_sel = i;
        if (strstr(s_pack_name((unsigned)i), part)) { s_status(out, sizeof out); return out; }
    }
    return "";
}

static void remove_tree(const char *path) {
    char **list;
    int n, i;
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(path, &info)) return;
    if (info.type == SDL_PATHTYPE_DIRECTORY && (list = SDL_GlobDirectory(path, "*", 0, &n))) {
        for (i = 0; i < n; ++i) {
            char child[900];
            snprintf(child, sizeof child, "%s/%s", path, list[i]);
            remove_tree(child);
        }
        SDL_free(list);
    }
    SDL_RemovePath(path);
}

int main(void) {
    char packs[600];
    unsigned char *data;
    unsigned size;
    snprintf(s_dir, sizeof s_dir, "%s", getenv("PE_TEST_DIR") ? getenv("PE_TEST_DIR") : ".");
    snprintf(packs, sizeof packs, "%s/packs", s_dir);
    remove_tree(packs);

    /* No packs folder: nothing, and the page says where packs go. */
    pe_packs_scan();
    pe_packs_register();
    pe_packs_page_register();
    assert(s_nadded == 0);

    mkdirs(packs);
    zip_pack("good-1.0.zip", "good", "assets", "claire.re2", "zombie.re2", NULL, "day5.bin");   /* zombie lacks a file */
    folder_pack("leon", "leon-pack", "leon.re2$rpd", "special", "", 1);
    folder_pack("shape", "shape-pack", "wrong.re2", "main", "", 1);
    {   /* a model that isn't shaped like Aya: refused before it is listed */
        char path[700];
        snprintf(path, sizeof path, "%s/packs/shape/characters/wrong.re2/model.bin", s_dir);
        put_file(path, "BAD-MODEL-BLOB", 14);
    }
    folder_pack("nolicense", "no-license", "x.re2", "main", "", 0);
    folder_pack("dup-folder", "twice", "a.re2", "main", "", 1);
    zip_pack("dup.zip", "twice", "assets", "b.re2", NULL, NULL, NULL);
    zip_pack("evil.zip", "evil", "assets", "e.re2", NULL, "characters/../../escape.bin", NULL);
    zip_pack("recipe.zip", "recipe-pack", "recipe", "r.re2", NULL, NULL, NULL);
    zip_pack("badid.zip", "Bad_Id", "assets", "q.re2", NULL, NULL, NULL);
    zip_pack("pe1.zip", "pe1", "assets", "fake", NULL, NULL, NULL);

    pe_packs_scan();
    pe_packs_register();

    /* Loaded: keys are "<pack id>/<folder>", the tier sets the price, the bundle points back. */
    assert(find("good/claire.re2") && find("good/claire.re2")->price == 250);
    assert(!find("good/zombie.re2"));                       /* missing day5.bin: refused alone */
    assert(find("leon-pack/leon.re2$rpd") && find("leon-pack/leon.re2$rpd")->price == 400);
    assert(strcmp(find("good/claire.re2")->bundle, "@good/claire.re2") == 0 && find("good/claire.re2")->bank == -1);
    assert(s_nadded == 2);

    /* The skin loader's view: files read from the zip and from the folder; missing ones aren't. */
    assert(pe_pack_has("@good/claire.re2", "model.bin") && !pe_pack_has("@good/zombie.re2", "model.bin"));
    assert(pe_pack_read("@good/claire.re2", "day8.bin", &data, &size, 1u << 20) && size == sizeof s_blob && !memcmp(data, s_blob, size));
    free(data);
    assert(pe_pack_read("@leon-pack/leon.re2$rpd", "texture.bin", &data, &size, 1u << 20) && size == sizeof s_blob);
    free(data);
    assert(!pe_pack_read("@good/claire.re2", "day8.bin", &data, &size, 4) && !data);   /* over the limit */
    assert(!pe_pack_read("@nobody/x", "model.bin", &data, &size, 1u << 20));

    /* Refused, each with its reason on the Packs page. */
    assert(strstr(status_of("Pack good"), "1 characters; 1 refused"));
    assert(strstr(status_of("Pack leon-pack"), "1 characters loaded"));
    assert(strstr(status_of("Pack shape-pack"), "0 characters; 1 refused") && !find("shape-pack/wrong.re2"));
    assert(strstr(status_of("Pack no-license"), "no LICENSE"));
    assert(strstr(status_of("Pack twice"), "installed twice"));
    assert(strstr(status_of("dup.zip"), "installed twice") || strstr(status_of("Pack twice"), "installed twice"));
    assert(!find("twice/a.re2") && !find("twice/b.re2"));
    assert(strstr(status_of("evil.zip"), "unsafe name"));
    assert(strstr(status_of("Pack recipe-pack"), "pe_pack_build.py"));
    assert(strstr(status_of("Pack Bad_Id"), "bad id"));
    assert(strstr(status_of("Pack pe1"), "bad id"));          /* the game's own namespace */

    remove_tree(packs);
    puts("Pack checks passed: zip and folder packs, tier prices, reads, missing files, duplicates, unsafe names, licence, recipe, ids.");
    return 0;
}
