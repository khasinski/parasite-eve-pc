/* The Wardrobe's weapons on view: an imported Model turned into a viewer model (pe_model_imp.h) on a
 * made-up model, then, with the player's discs, Parasite Eve's held gun and Resident Evil 2's
 * weapons drawn the way the Wardrobe screen draws them. Each picture must show textured pixels.
 *   PE_TEST_DISC       Parasite Eve (USA) disc 1 .cue: the held gun
 *   PE_TEST_RE2_DISC   Resident Evil 2 (USA) .cue: every weapon listed, three drawn
 *   PE_TEST_DIR        optional: the pictures written there as .ppm, to look at
 * Without the discs only the made-up model is checked. No disc data is kept. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pe_disc_assets.h"
#include "pe_imp.h"
#include "pe_imp_fit.h"
#include "pe_imp_internal.h"
#include "pe_model.h"
#include "pe_model_imp.h"
#include "pe_model_view.h"
#include "pe_src_re2.h"
#include "pe_wardrobe.h"

static int s_fail;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); s_fail = 1; } } while (0)

/* ---- the host, as the game would give it ---- */
void pe_mod_log(const char *mod, const char *fmt, ...) { (void)mod; (void)fmt; }
int pe_disc_room(int map, PeRoomBlobs *out) { (void)map; memset(out, 0, sizeof *out); return 0; }
int pe_disc_player(int bank, PeRoomBlobs *out) { (void)bank; memset(out, 0, sizeof *out); return 0; }
void pe_disc_room_free(PeRoomBlobs *r) { free(r->scratch); free(r->texture); free(r->data); memset(r, 0, sizeof *r); }
int pe_model_skin_load(const PeModelSource *src, PeModel *out) { (void)src; (void)out; return 0; }
static const char *s_store = "pe-weapon-view-test/store";
int pe_mod_path(char *out, size_t size, const char *file) {
    if (strcmp(file, "import/store")) return -1;
    return snprintf(out, size, "%s", s_store) < (int)size ? 0 : -1;
}

/* PE.IMG's runs as the game's sector table has them: [12..13] the gun, [39..40] its texture. */
static const char *s_pe_disc;
static int s_gun_reads;
static int read_run(PeImpDisc *d, uint32_t base, uint32_t first, uint32_t end, uint8_t **buf, uint32_t *size) {
    char why[200];
    *size = (end - first) * 2048u;
    *buf = malloc(*size);
    return *buf && !pe_imp_disc_sectors(d, base + first, end - first, *buf, why, sizeof why);
}
int pe_disc_held_gun(PeRoomBlobs *out) {
    char why[200];
    memset(out, 0, sizeof *out);
    ++s_gun_reads;
    PeImpDisc *d = s_pe_disc ? pe_imp_disc_open(s_pe_disc, why, sizeof why) : NULL;
    if (!d) return 0;
    const int img = pe_imp_disc_find(d, "PE.IMG");
    const int ok = img >= 0 && read_run(d, pe_imp_disc_lba(d, img), 537, 539, &out->texture, &out->texture_size) &&
                   read_run(d, pe_imp_disc_lba(d, img), 200, 203, &out->data, &out->data_size);
    pe_imp_disc_close(d);
    if (!ok) pe_disc_room_free(out);
    return ok;
}

/* ---- drawing ---- */
typedef struct { int drawn, colours; } Picture;

static Picture draw(const PeModel *m, const char *name, int yaw) {
    Picture p = {0, 0};
    PeViewFit fit;
    PeView v = {160, 160, yaw, 0, 0, 0};
    PeImage img = {160, 160, calloc(160 * 160, 4)};
    uint32_t seen[64];
    if (!img.rgba || !pe_model_fit(m, &fit) || !pe_model_render(m, &fit, &v, &img)) { free(img.rgba); return p; }
    for (int i = 0; i < 160 * 160; i++) {
        const uint8_t *px = img.rgba + 4 * i;
        if (px[3] != 255) continue;
        const uint32_t c = (uint32_t)px[0] << 16 | (uint32_t)px[1] << 8 | px[2];
        int k = 0;
        p.drawn++;
        while (k < p.colours && seen[k] != c) k++;
        if (k == p.colours && p.colours < 64) seen[p.colours++] = c;
    }
    const char *dir = getenv("PE_TEST_DIR");
    if (dir && *dir) {
        char path[1200];
        snprintf(path, sizeof path, "%s/weapon-view-%s.ppm", dir, name);
        FILE *f = fopen(path, "wb");
        if (f) {
            fprintf(f, "P6\n160 160\n255\n");
            for (int i = 0; i < 160 * 160; i++) {
                const uint8_t *px = img.rgba + 4 * i, bg[3] = {16, 24, 40};
                fwrite(px[3] ? px : bg, 1, 3, f);
            }
            fclose(f);
        }
    }
    free(img.rgba);
    printf("  %s: %d pixels, %d+ colours\n", name, p.drawn, p.colours);
    return p;
}

/* A two-bone "person": bone 1 holds a textured quad (the weapon), bone 0 a triangle. */
static void made_up(PeImpModel *m) {
    memset(m, 0, sizeof *m);
    m->bones = m->parts = 2;
    m->parent = malloc(2 * sizeof *m->parent), m->parent[0] = -1, m->parent[1] = 0;
    m->rel = calloc(2, sizeof *m->rel), m->pos = calloc(2, sizeof *m->pos);
    m->pos[1][0] = m->rel[1][0] = 500;
    m->part = calloc(2, sizeof *m->part);
    m->part[0].tris = 1, m->part[0].tri = calloc(1, sizeof(PeImpFace));
    PeImpFace *t = m->part[0].tri;
    t->v[1][1] = -100, t->v[2][2] = 100, t->page = 3, t->clut = 7 << 6;
    m->part[1].quads = 1, m->part[1].quad = calloc(1, sizeof(PeImpFace));
    PeImpFace *q = m->part[1].quad;
    static const int16_t corner[4][3] = {{0, -50, -50}, {0, -50, 50}, {0, 50, -50}, {0, 50, 50}};
    static const uint8_t uv[4][2] = {{0, 0}, {63, 0}, {0, 63}, {63, 63}};
    memcpy(q->v, corner, sizeof corner), memcpy(q->uv, uv, sizeof uv);
    q->page = 4, q->clut = 9 << 6;                                       /* page 4: texel x 128 + u */
    m->pal_rows = 2, m->pal = calloc(2, sizeof *m->pal);
    m->pal[1][5] = 31;                                                   /* row 9 (the second): red */
    m->w = 256, m->h = 64, m->image = calloc(256 * 64, 1);
    for (int y = 0; y < 64; y++) for (int x = 128; x < 192; x++) m->image[y * 256 + x] = 5;
}

int main(void) {
    char why[300] = "";
    PeImpModel m;
    PeModel out;

    CHECK(pe_imp_weapon_part("re2:weapon/pl00w02") == 11 && pe_imp_weapon_part("re3:weapon/pl08w02") == 11);
    CHECK(pe_imp_weapon_part("re1:weapon/w02") == 14);
    CHECK(pe_imp_weapon_part("re2:weapon/pl00w02/moves") < 0 && pe_imp_weapon_part("re2:player/leon") < 0 &&
          pe_imp_weapon_part("dc1:weapon/x") < 0 && pe_imp_weapon_part(NULL) < 0);

    made_up(&m);                                                          /* the whole model */
    CHECK(!pe_model_from_imp(&m, -1, &out, why, sizeof why));
    CHECK(out.parts == 1 && out.faces == 2 && out.verts == 7 && out.commands == 1 && !out.anim);
    CHECK(out.vert[0][0] == 0 && out.vert[3][0] == 500);                  /* the quad at its bone */
    CHECK(out.face[1].quad && out.face[1].tpage == (0x80 | 1) && out.face[1].clut == (257 << 6));
    CHECK(out.face[0].tpage == 0x80 && out.face[0].clut == (256 << 6));
    pe_model_free(&out);
    CHECK(!pe_model_from_imp(&m, 1, &out, why, sizeof why));             /* the weapon alone */
    CHECK(out.faces == 1 && out.verts == 4);
    Picture p = draw(&out, "made-up", 0);                                /* it stands in the y-z plane */
    CHECK(p.drawn > 1000 && p.colours == 1);                             /* all red: the texture found */
    pe_model_free(&out);
    CHECK(pe_model_from_imp(&m, 5, &out, why, sizeof why) && strstr(why, "no part"));
    pe_imp_model_free(&m);

    const char *pe1 = getenv("PE_TEST_DISC"), *re2 = getenv("PE_TEST_RE2_DISC");
    s_pe_disc = pe1 && *pe1 ? pe1 : NULL;
    if (s_pe_disc) {                                                     /* the gun Aya holds */
        PeRoomBlobs blobs;
        CHECK(pe_disc_held_gun(&blobs));
        CHECK(pe_model_decode_held_gun(&blobs, &out));
        pe_disc_room_free(&blobs);
        CHECK(out.parts == 2 && out.verts == 39 && out.faces == 34);
        p = draw(&out, "pe-held-gun", 3072);
        CHECK(p.drawn > 500 && p.colours > 8);
        pe_model_free(&out);
    } else printf("PE_TEST_DISC not set: the held gun skipped\n");

    if (re2 && *re2) {                                                   /* imported weapons, as the screen lists them */
        PeImpScan sc;
        pe_imp_registry_reset();
        CHECK(!pe_imp_add_reader(&g_re2_weapons_ps1));
        CHECK(!pe_imp_set_naming("re2", g_re2_naming));
        CHECK(!pe_imp_scan(s_store, re2, NULL, &sc));
        pe_ward_weapons_refresh();
        int first = -1, listed = 0;
        for (int k = 0; k < pe_ward_weapons_count(); k++)
            if (!strcmp(pe_ward_weapon_group(k), "Resident Evil 2")) { if (first < 0) first = k; listed++; }
        printf("weapons listed: %d (Resident Evil 2: %d)\n", pe_ward_weapons_count(), listed);
        CHECK(!strcmp(pe_ward_weapon_name(0), "Aya's gun") && !strcmp(pe_ward_weapon_group(0), "Parasite Eve"));
        CHECK(listed >= 30 && first == 1);                               /* disc 2 has 32 */
        for (int k = first; k > 0 && k < first + 3; k++) {
            PeModelSource src;
            char name[64];
            CHECK(pe_ward_weapon_source(k, &src) && src.kind == PE_MODEL_CALLBACK);
            const PeModel *w = pe_model_load(&src);
            CHECK(w != NULL);
            if (!w) continue;
            CHECK(pe_model_load(&src) == w);                             /* cached */
            snprintf(name, sizeof name, "re2-%s", pe_ward_weapon_name(k));
            p = draw(w, name, 3072);
            CHECK(p.drawn > 100 && p.colours > 4);
        }
        PeModelSource gun;                                               /* the gun, through the list */
        CHECK(pe_ward_weapon_source(0, &gun));
        const int reads = s_gun_reads;
        const PeModel *g = pe_model_load(&gun);
        CHECK(s_pe_disc ? g && g->faces == 34 : g == NULL);
        pe_ward_weapons_release();                                       /* the screen closed */
        CHECK(pe_model_load(&gun) == g || !s_pe_disc);                   /* read again, same storage */
        CHECK(!s_pe_disc || s_gun_reads == reads + 2);
        pe_ward_weapons_release();
        pe_model_release_all();
    } else printf("PE_TEST_RE2_DISC not set: imported weapons skipped\n");

    printf(s_fail ? "weapon view FAILED\n" : "weapon view passed\n");
    return s_fail;
}
