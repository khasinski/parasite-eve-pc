/* Read-only disc access for host features. See pe_disc_assets.h. */
#include <stdlib.h>
#include <string.h>

#include "pe_disc_assets.h"
#include "pe_mod_host.h"
#include "pe1/cdrom.h"
#include "pe1/pe_image.h"
#include "pe1/scene_room.h"

#define PE_SECTOR 2048u

/* PE.IMG on the bound disc, looked up by name as the game does (game_state.c, g_PeImagePath). */
static int pe_archive(PeDiscFile *out) {
    static PeDisc *s_disc;
    static PeDiscFile s_file;
    if (!g_CdImage) return 0;
    if (s_disc != g_CdImage) {
        if (!pe_disc_find(g_CdImage, "\\PE.IMG;1", &s_file) || s_file.directory) return 0;
        s_disc = g_CdImage;
    }
    *out = s_file;
    return 1;
}

static int pe_run(const PeDiscFile *archive, uint32_t start, uint32_t sectors, uint8_t **buf, uint32_t *size) {
    const uint64_t offset = (uint64_t)start * PE_SECTOR, bytes = (uint64_t)sectors * PE_SECTOR;
    *buf = NULL;
    *size = 0;
    if (!sectors) return 1;
    if (offset > archive->size || bytes > archive->size - offset || !(*buf = malloc((size_t)bytes))) return 0;
    if (!pe_disc_read(g_CdImage, archive, offset, *buf, (size_t)bytes)) { free(*buf); *buf = NULL; return 0; }
    *size = (uint32_t)bytes;
    return 1;
}

int pe_disc_room(int map, PeRoomBlobs *out) {
    PeDiscFile archive;
    const SceneRoomSectorRange *r;
    memset(out, 0, sizeof *out);
    if (map < 1 || map > SCENE_ROOM_COUNT || !pe_archive(&archive)) {
        pe_mod_log(NULL, "disc: no room M%03d (no disc bound, or no such room)", map);
        return 0;
    }
    r = &g_RoomSectorTable[map - 1];
    if (!r->roomSectors ||
        !pe_run(&archive, r->start, r->scratchSectors, &out->scratch, &out->scratch_size) ||
        !pe_run(&archive, r->start + r->scratchSectors, r->textureSectors, &out->texture, &out->texture_size) ||
        !pe_run(&archive, r->start + r->scratchSectors + r->textureSectors, r->roomSectors, &out->data, &out->data_size)) {
        pe_mod_log(NULL, "disc: room M%03d could not be read", map);
        pe_disc_room_free(out);
        return 0;
    }
    return 1;
}

/* Player bundles use the same immutable containers, without a room or live actor. */
int pe_disc_player(int bank, PeRoomBlobs *out) {
    PeDiscFile archive;
    uint32_t t, m;
    memset(out, 0, sizeof *out);
    if ((bank != 10 && bank != 12 && bank != 13) || !pe_archive(&archive)) return 0;
    t = bank + 3; m = bank + 8;
    if (g_PeImageSectorTable[t + 1] <= g_PeImageSectorTable[t] ||
        g_PeImageSectorTable[m + 1] <= g_PeImageSectorTable[m] ||
        !pe_run(&archive, g_PeImageSectorTable[t], g_PeImageSectorTable[t + 1] - g_PeImageSectorTable[t], &out->texture, &out->texture_size) ||
        !pe_run(&archive, g_PeImageSectorTable[m], g_PeImageSectorTable[m + 1] - g_PeImageSectorTable[m], &out->data, &out->data_size)) {
        pe_disc_room_free(out);
        return 0;
    }
    return 1;
}

void pe_disc_room_free(PeRoomBlobs *room) {
    free(room->scratch);
    free(room->texture);
    free(room->data);
    memset(room, 0, sizeof *room);
}
