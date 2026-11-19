/* Resident Evil 3 room logic: each room archive (CD_DATA/STAGE?/R*.ARD) becomes a RoomLogic record
 * (pe_imp_logic.h), what the room runtime reads: doors, trigger zones, pickups, camera zones, enemy
 * placements, and the room's scripts, messages and collision as they are.
 * Its sound effects become Sounds in bank "room<stage><room>": the room file's section 1 holds a
 * compact bank header (after 0xFF padding; pe_src_re3_music.c has the layout) and the archive
 * entry whose size it names holds the samples. Read only. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pe_imp_rdt.h"
#include "pe_imp_spu.h"
#include "pe_src_re3.h"

static uint32_t le32(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

/* the archive entry of `size` bytes (not the music, 0 and 1, nor the room, 8) */
static const uint8_t *entry_of_size(const uint8_t *ard, uint32_t ard_size, uint32_t want) {
    if (ard_size < 2048 + 16) return NULL;
    uint32_t entries = le32(ard + 4), pos = 2048;
    if (entries > 255 || 8 + entries * 8 > 2048) return NULL;
    for (uint32_t i = 0; i < entries; i++) {
        uint32_t len = le32(ard + 8 + i * 8);
        if (pos > ard_size || len > ard_size - pos) return NULL;
        if (i >= 2 && i != 8 && len == want) return ard + pos;
        pos += (len + 2047) & ~2047u;
    }
    return NULL;
}

static int room_sounds(const uint8_t *ard, uint32_t size, const char *path, PeImpSink *sink, char *why, size_t n) {
    uint32_t stage, room, player, off, len;
    const uint8_t *rdt;
    uint32_t rdt_size;
    if (pe_imp_re_room_name(PE_IMP_RE3, path, &stage, &room, &player) || pe_imp_re3_ard_rdt(ard, size, &rdt, &rdt_size) ||
        pe_imp_rdt_section(rdt, rdt_size, 1, &off, &len))
        return 0;
    const uint8_t *h = rdt + off;
    while (len >= 32 && le32(h) == 0xFFFFFFFFu) h += 4, len -= 4;     /* padding before the header */
    if (len < 32) return 0;
    uint32_t samples = le32(h);
    const uint8_t *vb = entry_of_size(ard, size, samples);
    if (!vb) return 0;                                                  /* no sounds in this room */
    char quiet[8];
    uint32_t std = re3_bank_size(h, len, samples, quiet, sizeof quiet);
    if (!std) return 0;
    uint8_t *vh = malloc(std);
    if (!vh) { if (why && n) snprintf(why, n, "out of memory"); return -1; }
    int rc = re3_bank_rebuild(h, samples, vh, why, n);
    if (!rc) {
        char bank[24];
        snprintf(bank, sizeof bank, "room%x%02x", (unsigned)stage + 1, (unsigned)room);
        rc = pe_imp_vab_read(sink, bank, vh, std, vb, samples, why, n);
    }
    free(vh);
    return rc;
}

static int read_logic(const uint8_t *data, uint32_t size, const char *path, PeImpSink *sink, char *why, size_t n) {
    int rc = pe_imp_emit_re3_logic(sink, path, data, size, why, n);
    return rc ? rc : room_sounds(data, size, path, sink, why, n);
}

const PeImpReader g_re3_logic_ps1 = {
    .api = {PE_IMP_API_VERSION, sizeof(PeImpReader)},
    .game = "re3", .platform = "ps1", .format = "rdt-logic",
    .version = 5, .produces = PE_IMP_ROOMLOGIC | PE_IMP_SOUND, .match = "CD_DATA/STAGE?/R*.ARD",
    .tags = "room logic", .read = read_logic, .path_key = 1
};
