/* Random encounters in room script bytecode. See pe_encounter.h. */
#include "pe_encounter.h"
#include <stdint.h>
#include <string.h>

/* Bytecode is disk data: read words without assuming alignment. */
static uint32_t pe_word(const unsigned char *p) {
    uint32_t value;
    memcpy(&value, p, sizeof value);
    return value;
}
static unsigned pe_argc(uint32_t h) { return (h >> 13) & 15u; }
static unsigned pe_mode(uint32_t h, unsigned arg) { return (h >> (17u + 3u * arg)) & 7u; }

int pe_encounter_roll(const unsigned char *code, size_t bytes) {
    uint32_t h, compare;
    size_t pos;
    if (bytes < 52) return 0;
    h = pe_word(code);
    if ((h & 0x1FFFu) != 0x1Au || pe_argc(h) != 3 ||
        pe_mode(h, 1) != 0 || pe_mode(h, 2) != 0 || pe_word(code + 12) != 0) return 0;
    compare = pe_word(code + 20);
    if ((compare & 0x1FFFu) != 9 || pe_argc(compare) != 4 ||
        pe_word(code + 28) != 10 || pe_word(code + 36) != pe_word(code + 8) ||
        pe_mode(compare, 2) != pe_mode(h, 0) ||
        (pe_word(code + 44) & 0x1FFFu) != 5) return 0;
    pos = 44;
    for (unsigned n = 0; n < 16 && pos <= bytes - 8; ++n) {
        uint32_t instruction = pe_word(code + pos);
        size_t length = 8 + 4 * pe_argc(instruction);
        if (length > bytes - pos) return 0;
        if ((instruction & 0x1FFFu) == 0x55u && pe_argc(instruction) == 1) return 1;
        pos += length;
    }
    return 0;
}

/* Opcode 119 with eleven arguments: eight immediate corners, then Aya's x and z and the result
 * (any modes). The modes of arguments 5..7 are in the tail word's low bits. */
#define PE_ZONE_SEARCH 0x1000u
static int pe_zone_at(const unsigned char *p) {
    uint32_t h = pe_word(p);
    return (h & 0x1FFFu) == 119 && pe_argc(h) == 11 && (h >> 17) == 0 && (pe_word(p + 4) & 0x1FFu) == 0;
}

int pe_encounter_find(const unsigned char *blob, size_t bytes, PeEncounterZone *out, int max) {
    int found = 0;
    size_t at;
    if (!blob || bytes < 52) return 0;
    for (at = 0; at + 52 <= bytes; at += 4) {
        size_t back, stop;
        if (!pe_encounter_roll(blob + at, bytes - at)) continue;
        stop = at > PE_ZONE_SEARCH ? at - PE_ZONE_SEARCH : 0;
        for (back = at; back >= stop + 4; ) {
            back -= 4;
            if (back + 8 + 11 * 4 > at || !pe_zone_at(blob + back)) continue;
            if (found < max && out) {
                out[found].roll = at;
                for (int i = 0; i < 8; ++i) out[found].corner[i] = (int)pe_word(blob + back + 8 + 4 * (size_t)i);
            }
            ++found;
            break;
        }
    }
    return found;
}

void pe_encounter_center(const PeEncounterZone *zone, int *x, int *z) {
    long long sx = 0, sz = 0;
    for (int i = 0; i < 4; ++i) { sx += zone->corner[2 * i]; sz += zone->corner[2 * i + 1]; }
    *x = (int)(sx / 4);
    *z = (int)(sz / 4);
}
