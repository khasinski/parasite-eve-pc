/* The importer's PE source in the game: Aya's data and the rooms from the discs the game already
 * has open (g_CdImageVolumes: CUE/BIN or CHD, PC and PS5), through the game's own tables and its
 * locked disc reads (safe beside streamed music). Rooms 1-450, the game's room table; checked
 * against the bridge's scan of 1-999 (same rows: tests/pe_mod_skin_build_test.c, PE_TEST_ROOMS). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pe_imp_pe.h"
#include "pe_import.h"
#include "pe1/cdrom.h"
#include "pe1/field_bg_load.h"
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

PeImpPe *pe_import_game_source(char *why, size_t n) {
    PeDisc *d;
    PeDiscFile f;
    if (archive(1, &d, &f)) {
        snprintf(why, n, "Parasite Eve's disc 1 isn't open (set it in the launcher)");
        return NULL;
    }
    PeImpPeOps ops = {game_image, game_room, NULL, SCENE_ROOM_COUNT};
    return pe_imp_pe_from(&ops, NULL);
}

int pe_import_have_disc2(void) {
    PeDisc *d;
    PeDiscFile f;
    return !archive(2, &d, &f);
}
