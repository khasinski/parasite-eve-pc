/* The skins builder target against tools/re2-skin (PR #4): Claire from the RE2 PS1 disc, built in
 * C, file by file against the Python build of the same input. Needs the player's discs, so it is
 * skipped (77) unless these are set:
 *   PE_TEST_DISC             Parasite Eve (USA) disc 1 .cue (disc 2 beside it is used too)
 *   PE_TEST_RE2_DISC         Resident Evil 2 (USA) disc 2 .cue
 *   PE_TEST_SKIN_REFERENCE   optional: the Python build's character folder (re2-local/characters/claire.re2)
 * Every file must match the reference byte for byte except texture.bin (our colour reduction
 * replaces Pillow's); building twice must give the same bytes. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pe_imp.h"
#include "pe_imp_fit.h"
#include "pe_imp_internal.h"
#include "pe_imp_pe.h"
#include "pe_skin_build.h"
#include "pe_src_re2.h"

#ifndef PE_SOURCE_DIR
#define PE_SOURCE_DIR "."
#endif

static int s_fail;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #c); s_fail = 1; } } while (0)

/* g_RenderNormalTable from the game's source, as build_re2_skin.py reads it */
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
        p = strchr(p, '{');
        if (!p || sscanf(p, "{%d,%d,%d,%d}", &x, &y, &z, &w) != 4) return -1;
        out[i][0] = (int16_t)x, out[i][1] = (int16_t)y, out[i][2] = (int16_t)z, out[i][3] = (int16_t)w;
        p++;
    }
    return 0;
}

static int read_file(const char *path, PeImpBuf *b) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    b->b = malloc(n ? (size_t)n : 1), b->n = (uint32_t)n;
    size_t got = b->b ? fread(b->b, 1, (size_t)n, f) : 0;
    fclose(f);
    return got == (size_t)n ? 0 : -1;
}

static void compare(const char *ref, const char *name, const PeImpBuf *mine, int must_match) {
    char path[1200];
    PeImpBuf r = {0};
    snprintf(path, sizeof path, "%s/%s", ref, name);
    if (read_file(path, &r)) { printf("  %-12s no reference file\n", name); CHECK(!must_match); return; }
    uint32_t first = 0, diff = 0;
    for (uint32_t i = 0; i < (r.n < mine->n ? r.n : mine->n); i++) if (r.b[i] != mine->b[i]) { if (!diff) first = i; diff++; }
    if (r.n == mine->n && !diff) printf("  %-12s identical (%u bytes)\n", name, r.n);
    else printf("  %-12s DIFFERS: %u vs %u bytes, %u bytes differ, first at %u\n", name, mine->n, r.n, diff, first);
    if (must_match) CHECK(r.n == mine->n && !diff);
    pe_imp_buf_free(&r);
}

/* rooms.bin: the same rows in the same places; angles the same except where the Python's to_yxz
 * hit gimbal lock (a reference pitch of exactly +-90 degrees, 1024 / 3072: its y and z there are
 * rounding noise; ours are the real rotation), and root heights within 3 where a dress row's feet
 * moved with such a shin. */
static void compare_rooms(const char *ref, const PeImpBuf *mine) {
    char path[1200];
    PeImpBuf r = {0};
    snprintf(path, sizeof path, "%s/rooms.bin", ref);
    if (read_file(path, &r)) { printf("  rooms.bin    no reference file\n"); CHECK(0); return; }
#define U32(p) ((p)[0] | (p)[1] << 8 | (p)[2] << 16 | (uint32_t)(p)[3] << 24)
    CHECK(r.n >= 8 && mine->n >= 8 && !memcmp(r.b, mine->b, 8));               /* PERR, the same count */
    uint32_t n = U32(r.b + 4), rows_same = 0, gimbal = 0, heights = 0, other = 0;
    CHECK(8 + 12ull * n <= r.n && 8 + 12ull * n <= mine->n);
    for (uint32_t i = 0; i < n && 8 + 12ull * n <= r.n && 8 + 12ull * n <= mine->n; i++) {
        const uint8_t *ta = r.b + 8 + 12 * i, *tb = mine->b + 8 + 12 * i;
        uint32_t offa = U32(ta + 4), sizea = U32(ta + 8), offb = U32(tb + 4), sizeb = U32(tb + 8);
        if (U32(ta) != U32(tb)) { other++; continue; }                         /* the same row, in order */
        if (sizea == sizeb && !memcmp(r.b + offa, mine->b + offb, sizea)) { rows_same++; continue; }
        int frames = r.b[offa + 2] ? r.b[offa + 2] : 1;
        for (int f = 0; f < frames; f++) {
            PeImpFrame a, b;
            PeImpBuf ra = {r.b, r.n}, rb = {mine->b, mine->n};
            if (pe_imp_anim_frame(&ra, offa, f, &a) || pe_imp_anim_frame(&rb, offb, f, &b) || a.n != b.n) { other++; continue; }
            for (int c = 0; c < 3; c++)
                if (a.tr[c] != b.tr[c]) { if (c == 1 && abs(a.tr[c] - b.tr[c]) <= 3) heights++; else other++; }
            for (int p = 0; p < a.n; p++)
                if (memcmp(a.ang[p], b.ang[p], sizeof a.ang[p])) {
                    if ((a.ang[p][0] == 1024 || a.ang[p][0] == 3072) && a.ang[p][0] == b.ang[p][0]) gimbal++;
                    else other++;
                }
        }
    }
    printf("  rooms.bin    %u of %u rows identical; elsewhere %u gimbal-lock angles, %u root heights within 3, %u other differences\n",
           rows_same, n, gimbal, heights, other);
    CHECK(other == 0);
    pe_imp_buf_free(&r);
}

int main(void) {
    const char *pe1 = getenv("PE_TEST_DISC"), *re2 = getenv("PE_TEST_RE2_DISC"), *ref = getenv("PE_TEST_SKIN_REFERENCE");
    if (!pe1 || !*pe1 || !re2 || !*re2) { printf("PE_TEST_DISC and PE_TEST_RE2_DISC not set: skipped\n"); return 77; }
    char pe2[1200], why[300] = "";
    snprintf(pe2, sizeof pe2, "%s", pe1);
    char *d1 = strstr(pe2, "Disc 1");
    if (d1) d1[5] = '2';

    int16_t normals[240][4];
    CHECK(!normal_table(normals));

    pe_imp_registry_reset();
    CHECK(!pe_imp_add_reader(&g_re2_pld_ps1));
    CHECK(!pe_imp_add_reader(&g_re2_emd_ps1));                         /* the people among the enemies */
    CHECK(!pe_imp_set_naming("re2", g_re2_naming));
    PeImpScan sc;
    CHECK(!pe_imp_scan("pe-skin-build-test/store", re2, NULL, &sc));
    PeImpModel claire;
    CHECK(!pe_imp_catalog_model("pe-skin-build-test/store", "re2:player/claire", &claire, why, sizeof why));

    PeImpPe *pe = pe_imp_pe_open(pe1, d1 ? pe2 : NULL, why, sizeof why);
    if (!pe) { fprintf(stderr, "PE disc: %s\n", why); return 1; }
    if (getenv("PE_TEST_ROOMS")) pe_imp_pe_set_rooms(pe, atoi(getenv("PE_TEST_ROOMS")));
    PeSkinOut a, b;
    int rc = pe_skin_build(pe, &claire, &g_re2_human_rig, (const int16_t (*)[4])normals, NULL, &a, why, sizeof why);
    if (rc) fprintf(stderr, "build: %s\n", why);
    CHECK(!rc);
    if (!rc) {
        printf("claire.re2: %d vertices, %d quads, %d triangles, %d texture groups, %d room rows; legs slimmed: %s\n",
               a.vertices, a.quads, a.tris, a.groups, a.room_rows, a.log[0] ? a.log : "none");
        CHECK(!pe_imp_mkdirs("pe-skin-build-test/claire.re2"));
        CHECK(!pe_skin_write(&a, "pe-skin-build-test/claire.re2", why, sizeof why));
        if (ref && *ref) {
            static const char *days[8] = {"day1.bin", "day2.bin", "day3.bin", "day4.bin", "day5.bin", "day6.bin", "day7.bin", "day8.bin"};
            compare(ref, "model.bin", &a.model, 1);
            for (int d = 0; d < 8; d++) compare(ref, days[d], &a.day[d], 1);
            compare(ref, "meta.bin", &a.meta, 1);
            compare(ref, "rooms.bin", &a.rooms, 0);
            compare_rooms(ref, &a.rooms);
            compare(ref, "texture.bin", &a.texture, 0);
        }
        CHECK(!pe_skin_build(pe, &claire, &g_re2_human_rig, (const int16_t (*)[4])normals, NULL, &b, why, sizeof why));
        CHECK(a.model.n == b.model.n && !memcmp(a.model.b, b.model.b, a.model.n));
        CHECK(a.texture.n == b.texture.n && !memcmp(a.texture.b, b.texture.b, a.texture.n));
        CHECK(a.rooms.n == b.rooms.n && !memcmp(a.rooms.b, b.rooms.b, a.rooms.n));
        pe_skin_files_free(&b);
        pe_skin_files_free(&a);
        /* a face moved two pages over: inside a 384-wide picture it is repacked; past the picture it is
         * refused with a reason, never built from the wrong place */
        PeImpPart *pt = &claire.part[0];
        PeImpFace *f = pt->quads ? &pt->quad[0] : &pt->tri[0];
        uint16_t page = f->page;
        f->page = (uint16_t)((page & ~15u) | (((page & 15u) + 2) & 15u));
        why[0] = 0;
        int moved = pe_skin_build(pe, &claire, &g_re2_human_rig, (const int16_t (*)[4])normals, NULL, &b, why, sizeof why);
        printf("  a face two pages over: %s%s%s\n", moved ? "refused (" : "repacked", moved ? why : "", moved ? ")" : "");
        CHECK(!moved || strstr(why, "outside") != NULL);
        if (!moved) pe_skin_files_free(&b);
        f->page = page;
    }
    /* a person among the enemies (Ada, EM41), through the fit layer and the game's human rig, as the
     * costume target builds it */
    PeImpModel ada;
    PeImpFitInfo fit;
    if (pe_imp_fit("pe-skin-build-test/store", "re2:enemy/em41", PE_IMP_USE_COSTUME, &ada, &fit, why, sizeof why)) {
        printf("  enemy/em41: not fitted (%s); skipped\n", why);
    } else {
        PeSkinOut c;
        int rc2 = pe_skin_build(pe, &ada, &g_re2_human_rig, (const int16_t (*)[4])normals, NULL, &c, why, sizeof why);
        if (rc2) fprintf(stderr, "enemy/em41 build: %s\n", why);
        CHECK(!rc2);
        if (!rc2) {
            printf("  enemy/em41 as a costume: %d vertices, %d texture groups (fit: height %d, %u faces, %u dropped)\n", c.vertices,
                   c.groups, fit.height, fit.faces, fit.dropped_degenerate + fit.dropped_stub);
            pe_skin_files_free(&c);
        }
        pe_imp_model_free(&ada);
    }
    /* a person on three texture pages (Irons, Ben, Marvin, some Leon and Claire outfits): repacked
     * into the costume's 256 x 255 */
    static const char *const three[] = {"re2:enemy/em4a", "re2:enemy/em40", "re2:enemy/em44", "re2:enemy/em46",
                                        "re2:enemy/em59", "re2:enemy/em5a", "re2:enemy/em59-cdemd3", "re2:enemy/em5a-cdemd3"};
    int tried = 0;
    for (size_t k = 0; k < sizeof three / sizeof *three && !tried; k++) {
        PeImpModel wide;
        if (pe_imp_fit("pe-skin-build-test/store", three[k], PE_IMP_USE_COSTUME, &wide, &fit, why, sizeof why)) continue;
        if (fit.pages < 3) { pe_imp_model_free(&wide); continue; }
        tried = 1;
        PeSkinOut c;
        int rc3 = pe_skin_build(pe, &wide, &g_re2_human_rig, (const int16_t (*)[4])normals, NULL, &c, why, sizeof why);
        if (rc3) fprintf(stderr, "%s build: %s\n", three[k], why);
        CHECK(!rc3);
        if (!rc3) {
            printf("  %s (pages %u to %u) repacked as a costume: %d vertices, %d texture groups\n", three[k], fit.page_first,
                   fit.page_first + fit.pages - 1, c.vertices, c.groups);
            pe_skin_files_free(&c);
        }
        pe_imp_model_free(&wide);
    }
    if (!tried) printf("  no three-page person in this store; repack skipped\n");
    pe_imp_pe_close(pe);
    pe_imp_model_free(&claire);
    if (!s_fail) printf("skins builder passed\n");
    return s_fail;
}
