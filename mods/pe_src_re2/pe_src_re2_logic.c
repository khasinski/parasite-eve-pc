/* Resident Evil 2 room logic: each room file (PL?/RDT/ROOM*.RDT) becomes a RoomLogic record (pe_imp_logic.h), what the
 * room runtime reads: doors, trigger zones, pickups, camera zones, enemy placements, and the room's
 * scripts, messages and collision as they are. Read only. */
#include <stdio.h>
#include <string.h>
#include "pe_imp_rdt.h"
#include "pe_imp_spu.h"
#include "pe_src_re2.h"

static const char *const k_players[2] = {"Leon", "Claire"};

static uint32_t read_le(const uint8_t *p, unsigned n)
{
    uint32_t result = 0;
    for (unsigned i = 0; i < n; ++i) result |= (uint32_t)p[i] << (8 * i);
    return result;
}

/* Room bank 2. EDT entries select the VAB program/tone, which selects a waveform. */
static int read_room_sounds(const uint8_t *data, uint32_t size, const char *path,
                            PeImpSink *sink, char *why, size_t n)
{
    if (size < 20) return 0;
    const uint32_t edt = read_le(data + 8, 4), vh = read_le(data + 12, 4), vb = read_le(data + 16, 4);
    if (!edt || edt >= vh || vh >= vb || vb > size || vb - vh < 32 || memcmp(data + vh, "pBAV", 4)) return 0;
    const uint8_t *header = data + vh;
    const unsigned programs = read_le(header + 18, 2), waves = read_le(header + 22, 2);
    if (!programs || programs > 128 || waves > 254) return 0;
    const unsigned table = 0x820 + programs * 512;
    if (table + 2 * (waves + 1) > vb - vh) return 0;
    const char *room = strrchr(path, '/');
    if (!room || strlen(room) < 13) return 0;
    for (unsigned cue = 0; cue < 256 && cue * 4 + 4 <= vh - edt; ++cue) {
        const uint8_t *entry = data + edt + cue * 4;
        if (read_le(entry, 4) == UINT32_MAX || (entry[0] & 0x80)) continue;
        const unsigned program = entry[1] & 0x7f, tone = entry[2] >> 4;
        if (program >= programs) continue;
        const unsigned wave = read_le(header + 0x820 + program * 512 + tone * 32 + 22, 2);
        if (!wave || wave > waves) continue;
        uint32_t start = vb;
        for (unsigned j = 1; j < wave; ++j) start += read_le(header + table + 2 * j, 2) * 8;
        const uint32_t bytes = read_le(header + table + 2 * wave, 2) * 8;
        if (!bytes || bytes > size || start > size - bytes) continue;
        char bank[24];
        snprintf(bank, sizeof bank, "room%.4s", room + 5);
        for (char *c = bank; *c; ++c) if (*c >= 'A' && *c <= 'Z') *c += 'a' - 'A';   /* catalog ids are lower case:
                                                                                         * the Dual Shock disc's ROOMG040 too */
        if (pe_imp_spu_emit(sink, bank, cue, data + start, bytes, why, n)) return -1;
    }
    return 0;
}

static int read_logic(const uint8_t *data, uint32_t size, const char *path, PeImpSink *sink, char *why, size_t n) {
    const int result = pe_imp_emit_re_logic(sink, PE_IMP_RE2, path, data, size, k_players, why, n);
    if (result) return result;
    return read_room_sounds(data, size, path, sink, why, n);
}

const PeImpReader g_re2_logic_ps1 = {
    .api = {PE_IMP_API_VERSION, sizeof(PeImpReader)},
    .game = "re2", .platform = "ps1", .format = "rdt-logic",
    .version = 8, .produces = PE_IMP_ROOMLOGIC | PE_IMP_SOUND, .match = "PL?/RDT/ROOM*.RDT",
    .tags = "room logic", .read = read_logic, .path_key = 1
};
