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
    printf("made-up MDEC: bad headers refused, ImageSet round-trips\n");
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
    assert(!pe_imp_scan(STORE, cue, NULL, &sc));
    if (sc.refused) printf("  refused: %s\n", sc.last_error);
    const char *dir = getenv("PE_TEST_ROOMS_DIR");
    if (dir && *dir) pe_imp_mkdirs(dir);
    PeImpAsset *a;
    int n = pe_imp_catalog_assets(STORE, game, &a), rooms = 0, views = 0, flat = 0, failed = 0;
    static uint8_t rgb[320 * 240 * 3];
    int truncated_once = 0;
    for (int i = 0; i < n; i++) {
        if (strcmp(a[i].type, "ImageSet")) continue;
        PeImpImageSet *set;
        if (pe_imp_catalog_imageset(STORE, a[i].id, &set, why, sizeof why)) {
            printf("  %s: %s\n", a[i].id, why);
            failed++;
            continue;
        }
        rooms++;
        for (uint32_t v = 0; v < set->count; v++) {
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
    assert(rooms > 20 && !failed && !sc.refused && flat * 20 < views);
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    pe_imp_mdec_set_table(MdecDiag_ExpandDecoderTable);
    made_up();
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
