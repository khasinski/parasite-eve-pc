/* MDEC frames (mods/host/import/pe_imp_mdec.c) and room backgrounds (the ImageSet type and the
 * Resident Evil room readers).
 *   made-up   refused: not a frame, a wrong version, sizes not multiples of 16; an ImageSet
 *             round-trips through the store format, a bad one is refused
 *   disc      with PE_TEST_RE1_DISC / PE_TEST_RE2_DISC / PE_TEST_RE3_DISC: every room scanned into a
 *             store, every view decoded at 320 x 240 to a picture with detail (not flat); truncated
 *             frames fail or end cleanly. PE_TEST_ROOMS_DIR=<folder> writes every view there as
 *             <game>_<room>_<view>.ppm to look at. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pe_imp.h"
#include "pe_imp_internal.h"
#include "pe_imp_mdec.h"
#include "pe_imp_room.h"

void MdecDiag_ExpandDecoderTable(void *buffer);   /* the game's, linked in */
extern const PeImpReader g_re1_rooms_ps1, g_re2_rooms_ps1, g_re3_rooms_ps1;

#define STORE "pe-mdec-test/store"

static void made_up(void) {
    static uint8_t rgb[320 * 240 * 3];
    uint8_t bad[16] = {0x10, 0, 0, 0x38, 1, 0, 3, 0};
    char why[100];
    assert(pe_imp_mdec_decode(bad, 4, 320, 240, rgb, why, sizeof why) == -1);
    bad[6] = 1;
    assert(pe_imp_mdec_decode(bad, sizeof bad, 320, 240, rgb, why, sizeof why) == -1);
    bad[6] = 3;
    assert(pe_imp_mdec_decode(bad, sizeof bad, 321, 240, rgb, why, sizeof why) == -1);

    uint32_t sizes[2] = {16, 20};
    assert(!pe_imp_imageset_alloc(PE_IMP_IMAGESET_MDEC, 320, 241, 2, sizes));
    assert(!pe_imp_imageset_alloc(2, 320, 240, 2, sizes));
    assert(!pe_imp_imageset_alloc(PE_IMP_IMAGESET_MDEC, 320, 240, 0, sizes));
    PeImpImageSet *set = pe_imp_imageset_alloc(PE_IMP_IMAGESET_MDEC, 320, 240, 2, sizes);
    assert(set);
    memcpy(set->payload, bad, 16);
    memcpy(set->payload + 16, bad, 16);
    pe_imp_mkdirs("pe-mdec-test/made-up");
    assert(!pe_imp_imageset_save(set, "pe-mdec-test/made-up", why, sizeof why));
    PeImpImageSet *back;
    assert(!pe_imp_imageset_load("pe-mdec-test/made-up", &back, why, sizeof why));
    uint32_t size;
    assert(back->count == 2 && pe_imp_imageset_frame(back, 1, &size) == back->payload + 16 && size == 20);
    assert(!memcmp(back->payload, set->payload, 36));
    free(back);
    set->payload[16 + 3] = 0;                                 /* the second frame isn't MDEC */
    assert(pe_imp_imageset_save(set, "pe-mdec-test/made-up", why, sizeof why) == -1);
    free(set);
    /* An IMG1 store produced by the first room importer still loads. */
    uint8_t img1[40] = {'I', 'M', 'G', '1', 1, 0, 0, 0, 0x40, 1, 0, 0, 240, 0, 0, 0, 1, 0, 0, 0,
                        16, 0, 0, 0};
    memcpy(img1 + 24, bad, sizeof bad);
    FILE *legacy = fopen("pe-mdec-test/made-up/imageset.bin", "wb");
    assert(legacy && fwrite(img1, 1, sizeof img1, legacy) == sizeof img1 && !fclose(legacy));
    assert(!pe_imp_imageset_load("pe-mdec-test/made-up", &back, why, sizeof why));
    PeImpRoomView view;
    assert(!pe_imp_room_view(back, 0, &view) && !view.game && !view.slot && !view.mask_count);
    assert(pe_imp_room_view(back, 1, &view) == -1);
    free(back);
    printf("made-up MDEC: bad headers refused, ImageSet round-trips\n");
}

static void made_up_masks(void) {
    /* A four-pixel 16-bit TIM: red, transparent, green, red. */
    static const uint8_t tim[] = {
        0x10, 0, 0, 0, 2, 0, 0, 0, 20, 0, 0, 0, 0, 0, 0, 0, 4, 0, 1, 0,
        0x1f, 0, 0, 0, 0xe0, 3, 0x1f, 0
    };
    static uint8_t rgba[320 * 240 * 4];
    for (unsigned game = PE_IMP_RE1; game <= PE_IMP_RE3; game++) {
        uint32_t sizes[] = {16}, prefix = game == PE_IMP_RE1 ? 0 : game == PE_IMP_RE2 ? 8 : 5;
        uint32_t extra[] = {64 + prefix + sizeof tim};
        PeImpImageSet *set = pe_imp_imageset_alloc_extra(PE_IMP_IMAGESET_MDEC, 320, 240, 1, sizes, extra);
        assert(set);
        uint8_t *rec = set->payload + sizes[0];
        memcpy(rec, "RMV1", 4);
        rec[4] = 3;
        rec[5] = (uint8_t)game;
        rec[8] = 230;
        memset(rec + 12, 0xff, 4);  /* eye.x = -1 */
        rec[36] = 24;
        rec[40] = rec[42] = rec[44] = 1; /* one group, one rectangle */
        rec[48] = rec[49] = 0xff;        /* group x = -1 */
        rec[56] = 7;                    /* tile depth */
        rec[60] = 4;
        rec[62] = 1;
        uint8_t *atlas = rec + 64;
        if (game == PE_IMP_RE2) {
            atlas[0] = sizeof tim;
            atlas[6] = 0x10;             /* extended literal */
            atlas[7] = sizeof tim - 16;
        } else if (game == PE_IMP_RE3) {
            atlas[0] = 1;                /* one command */
            atlas[4] = 0x80 | sizeof tim;
        }
        memcpy(atlas + prefix, tim, sizeof tim);
        PeImpRoomView view;
        assert(!pe_imp_room_view(set, 0, &view));
        assert(view.game == game && view.slot == 3 && view.projection == 230 && view.eye[0] == -1);
        assert(view.mask_count == 1 && view.masks[0].x == -1 && view.masks[0].depth == 7);
        assert(!pe_imp_room_foreground(set, 0, &view, rgba));
        assert(!rgba[3] && rgba[5] == 255 && rgba[7] == 255 && rgba[8] == 255 && rgba[11] == 255 && !rgba[15]);
        if (game == PE_IMP_RE2) {
            /* Collision-preview stores insert an atlas length and append SCA after it. */
            uint32_t collision_extra[] = {extra[0] + 8};
            PeImpImageSet *stored = pe_imp_imageset_alloc_extra(PE_IMP_IMAGESET_MDEC, 320, 240, 1,
                                                                sizes, collision_extra);
            assert(stored);
            uint8_t *rmv2 = stored->payload + sizes[0];
            memcpy(rmv2, rec, 40);
            rmv2[3] = '2';
            rmv2[6] = PE_IMP_RE2;
            rmv2[40] = (uint8_t)(prefix + sizeof tim);
            memcpy(rmv2 + 44, rec + 40, extra[0] - 40);
            memset(rmv2 + extra[0] + 4, 0xff, 4);
            PeImpRoomView collision_view;
            assert(!pe_imp_room_view(stored, 0, &collision_view) && collision_view.game == PE_IMP_RE2 &&
                   collision_view.projection == 230);
            assert(!pe_imp_room_foreground(stored, 0, &collision_view, rgba));
            rmv2[40] = 255;
            assert(pe_imp_room_view(stored, 0, &collision_view) == -1);
            free(stored);
        }
        if (game != PE_IMP_RE1) {
            atlas[game == PE_IMP_RE2 ? 6 : 4] = 0;  /* backward reference before any pixels */
            assert(pe_imp_room_foreground(set, 0, &view, rgba) == -1);
        }
        rec[36] = 255;                   /* PRI size larger than the whole record */
        assert(pe_imp_room_view(set, 0, &view) == -1);
        free(set);
    }
}

static void write_ppm(const char *dir, const char *game, const char *id, unsigned view, const uint8_t *rgb) {
    char path[512];
    const char *room = strchr(id, '/');
    snprintf(path, sizeof path, "%s/%s_%s_%u.ppm", dir, game, room ? room + 1 : id, view);
    FILE *f = fopen(path, "wb");
    if (f) { fprintf(f, "P6\n320 240\n255\n"); fwrite(rgb, 1, 320 * 240 * 3, f); fclose(f); }
}

static void disc(const char *label, const char *cue, const char *game, const PeImpReader *reader) {
    char why[300] = "";
    PeImpScan sc;
    pe_imp_registry_reset();
    assert(!pe_imp_add_reader(reader));
    char catalog[sizeof STORE + 32];
    snprintf(catalog, sizeof catalog, "%s/%s/catalog.json", STORE, game);
    remove(catalog);  /* keep source selection independent of discs used in prior test runs */
    assert(!pe_imp_scan(STORE, cue, NULL, &sc));
    if (sc.refused) printf("  refused: %s\n", sc.last_error);
    const char *dir = getenv("PE_TEST_ROOMS_DIR");
    if (dir && *dir) pe_imp_mkdirs(dir);
    PeImpAsset *a;
    int n = pe_imp_catalog_assets(STORE, game, &a), rooms = 0, views = 0, flat = 0, failed = 0;
    static uint8_t rgb[320 * 240 * 3];
    static uint8_t rgba[320 * 240 * 4];
    PeImpRoomView view;
    int truncated_once = 0, cameras = 0, masks = 0, missing = 0, mask_failures = 0, empty_masks = 0;
    unsigned long long masked_pixels = 0, color_error = 0;
    for (int i = 0; i < n; i++) {
        if (strcmp(a[i].type, "ImageSet")) continue;
        PeImpImageSet *set;
        if (pe_imp_catalog_imageset(STORE, a[i].id, &set, why, sizeof why)) {
            printf("  %s: %s\n", a[i].id, why);
            failed++;
            continue;
        }
        rooms++;
        for (uint32_t v = 0, next_slot = 0; v < set->count; v++) {
            uint32_t size;
            const uint8_t *f = pe_imp_imageset_frame(set, v, &size);
            views++;
            if (pe_imp_mdec_decode(f, size, set->width, set->height, rgb, why, sizeof why)) {
                printf("  %s view %u: %s\n", a[i].id, v, why);
                failed++;
                continue;
            }
            long sum = 0, sum2 = 0;
            for (size_t k = 0; k < sizeof rgb; k++) sum += rgb[k], sum2 += (long)rgb[k] * rgb[k];
            double mean = (double)sum / sizeof rgb, var = (double)sum2 / sizeof rgb - mean * mean;
            if (var < 100) flat++;
            assert(!pe_imp_room_view(set, v, &view));
            assert(view.slot >= next_slot);
            next_slot = view.slot + 1;
            if (view.game) {
                assert(view.projection || !view.mask_count);
                assert(view.eye[0] != view.target[0] || view.eye[1] != view.target[1] || view.eye[2] != view.target[2] ||
                       !view.mask_count);
                cameras++;
            }
            int m = pe_imp_room_foreground(set, v, &view, rgba);
            if (m == 0) {
                masks++;
                unsigned present = 0;
                for (size_t k = 0; k < 320u * 240u; k++) {
                    if (!rgba[4*k+3]) continue;
                    present++;
                    for (size_t ch = 0; ch < 3; ch++) color_error += abs(rgba[4*k+ch] - rgb[3*k+ch]);
                }
                masked_pixels += present;
                empty_masks += !present;
            } else if (m < 0) {
                printf("  %s slot %u: mask texture failed\n", a[i].id, view.slot);
                mask_failures++;
            } else if (view.mask_count) {
                printf("  %s slot %u: mask texture absent\n", a[i].id, view.slot);
                missing++;
            }
            if (dir && *dir) write_ppm(dir, game, strchr(a[i].id, ':') ? strchr(a[i].id, ':') + 1 : a[i].id, v, rgb);
            if (!truncated_once) {                            /* cut short anywhere: fails or ends cleanly */
                for (uint32_t cut = 12; cut < size; cut += 397) pe_imp_mdec_decode(f, cut, 320, 240, rgb, why, sizeof why);
                truncated_once = 1;
            }
        }
        free(set);
    }
    free(a);
    printf("%s: %d rooms, %d views from %d files; %d flat, %d failed\n", label, rooms, views, sc.files_read, flat, failed);
    printf("%s: %d cameras, %d foreground textures (%d blank), %d absent, %d failed, %.2f mean RGB error\n",
           label, cameras, masks, empty_masks, missing, mask_failures,
           masked_pixels ? color_error / (3.0 * masked_pixels) : 0);
    assert(rooms > 20 && !failed && !sc.refused && flat * 20 < views);
    assert(cameras > views * 3 / 4 && masks > views / 4 && !mask_failures && empty_masks < masks / 10);
    assert(masked_pixels && color_error < 15 * 3 * masked_pixels);
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    pe_imp_mdec_set_table(MdecDiag_ExpandDecoderTable);
    made_up();
    made_up_masks();
    static const struct { const char *env, *label, *game; const PeImpReader *reader; } k_games[] = {
        {"PE_TEST_RE1_DISC", "RE1", "re1", &g_re1_rooms_ps1},
        {"PE_TEST_RE2_DISC", "RE2", "re2", &g_re2_rooms_ps1},
        {"PE_TEST_RE3_DISC", "RE3", "re3", &g_re3_rooms_ps1},
    };
    for (unsigned g = 0; g < sizeof k_games / sizeof k_games[0]; g++) {
        const char *cue = getenv(k_games[g].env);
        if (cue && *cue) disc(k_games[g].label, cue, k_games[g].game, k_games[g].reader);
        else printf("%s not set: %s rooms skipped\n", k_games[g].env, k_games[g].label);
    }
    printf("MDEC passed\n");
    return 0;
}
