/* The import mod's Parasite Eve adapter (import_host.h). The importer's PE source in the game:
 * Aya's data and the rooms from the discs the game already has open (g_CdImageVolumes: CUE/BIN or
 * CHD, PC and PS5), through the game's own tables and its locked disc reads (safe beside streamed
 * music). Rooms 1-450, the game's room table; checked against the bridge's scan of 1-999 (same rows:
 * tests/pe_mod_skin_build_test.c, PE_TEST_ROOMS). Builder targets get a PeImpPe1Build: that source
 * and the game's 240 vertex normals. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "import_host.h"
#include "pe_hooks.h"
#include "pe_mod_host.h"
#include "pe_imp_mdec.h"
#include "pe_imp_pe.h"
#include "pe1/boot_display.h"
#include "pe1/cdrom.h"
#include "pe1/field_bg_load.h"
#include "pe1/render_object.h"
#include "pe1/scene_room.h"

#define SECTOR 2048u
#define BLOB_MAX (64u << 20)

static int archive(int disc, PeDisc **d, PeDiscFile *file) {
    *d = disc >= 1 && disc <= 2 ? g_CdImageVolumes[disc - 1] : NULL;
    return *d && pe_disc_find(*d, "\\PE.IMG;1", file) && !file->directory ? 0 : -1;
}

static int run(int disc, uint64_t start, uint64_t sectors, PeImpBuf *out) {
    PeDisc *d;
    PeDiscFile f;
    out->b = NULL, out->n = 0;
    if (archive(disc, &d, &f) || sectors * SECTOR > BLOB_MAX) return -1;
    uint64_t off = start * SECTOR, bytes = sectors * SECTOR;
    if (off > f.size || bytes > f.size - off) return -1;
    if (!(out->b = malloc(bytes ? (size_t)bytes : 1))) return -1;
    if (bytes && !pe_disc_read(d, &f, off, out->b, (size_t)bytes)) { pe_imp_buf_free(out); return -1; }
    out->n = (uint32_t)bytes;
    return 0;
}

static int game_image(void *user, int idx, PeImpBuf *out) {
    (void)user;
    if (idx < 0 || g_PeImageSectorTable[idx + 1] < g_PeImageSectorTable[idx]) { out->b = NULL, out->n = 0; return -1; }
    return run(1, g_PeImageSectorTable[idx], (uint64_t)g_PeImageSectorTable[idx + 1] - g_PeImageSectorTable[idx], out);
}

static int game_room(void *user, int map, int disc, PeImpBuf *out) {
    (void)user;
    out->b = NULL, out->n = 0;
    if (map < 1 || map > SCENE_ROOM_COUNT) return -1;
    const SceneRoomSectorRange *r = &g_RoomSectorTable[map - 1];
    return run(disc, (uint64_t)r->start + r->scratchSectors + r->textureSectors, r->roomSectors, out);
}

static void init(void) {
    pe_imp_mdec_set_table(MdecDiag_ExpandDecoderTable);      /* the game's own MDEC table, for rooms */
}

static const void *open_targets(char *why, size_t n) {
    PeDisc *d;
    PeDiscFile f;
    PeImpPe1Build *b;
    if (archive(1, &d, &f)) {
        snprintf(why, n, "Parasite Eve's disc 1 isn't open (set it in the launcher)");
        return NULL;
    }
    if (!(b = calloc(1, sizeof *b))) { snprintf(why, n, "out of memory"); return NULL; }
    PeImpPeOps ops = {game_image, game_room, NULL, SCENE_ROOM_COUNT};
    if (!(b->pe = pe_imp_pe_from(&ops, NULL))) {
        free(b);
        snprintf(why, n, "Parasite Eve's data couldn't be read");
        return NULL;
    }
    b->normals = (const int16_t (*)[4])g_RenderNormalTable;
    return b;
}

static void close_targets(const void *targets) {
    PeImpPe1Build *b = (PeImpPe1Build *)(uintptr_t)targets;
    if (!b) return;
    pe_imp_pe_close(b->pe);
    free(b);
}

static void bind_frame(const char *mod_id, int (*frame)(void *event, void *user)) {
    pe_hook_add(PE_HOOK_FRAME, frame, NULL, PE_PRIORITY_LATE, mod_id);
}

const ImportHost *import_host(void) {
    static const ImportHost k_host = {sizeof k_host, init, open_targets, close_targets, bind_frame};
    return &k_host;
}
