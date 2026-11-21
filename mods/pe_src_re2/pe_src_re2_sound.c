#include <stdio.h>
#include <string.h>
#include "pe_imp_spu.h"

static unsigned read16(const uint8_t *p) { return p[0] | (unsigned)p[1] << 8; }

/* ENEMSE stores each sound set as sector-aligned EDT/VAB header and sample blocks.
 * Consulted bio2-ds SND.C for the section layout and checked it against both US discs. */
static int read_enemy_banks(const uint8_t *data, uint32_t size, const char *path, PeImpSink *sink,
                            char *why, size_t n) {
    (void)path;
    enum { SECTOR = 2048, EDT = 128, VAB_FIXED = 32 + 2048 };
    unsigned bank = 0;
    for (uint32_t at = 0; at < size; bank++) {
        if (size - at < EDT + VAB_FIXED) break;
        const uint8_t *header = data + at + EDT;
        if (memcmp(header, "pBAV", 4)) break;
        const unsigned programs = read16(header + 18), waves = read16(header + 22);
        if (!programs || programs > 128 || !waves || waves > 254) break;
        const unsigned table = VAB_FIXED + programs * 512, length = table + 512;
        const unsigned body = (EDT + length + SECTOR - 1) & ~(SECTOR - 1u);
        if (body > size - at) break;
        unsigned samples = 0;
        for (unsigned i = 1; i <= waves; ++i) samples += 8u * read16(header + table + 2 * i);
        if (!samples || samples > size - at - body) break;
        const unsigned next = body + ((samples + SECTOR - 1) & ~(SECTOR - 1u));
        if (next > size - at) break;
        char name[24];
        snprintf(name, sizeof name, "enemy%02u", bank);
        if (pe_imp_vab_read(sink, name, header, length, data + at + body, samples, why, n)) return -1;
        at += next;
        if (at == size) return 0;
    }
    snprintf(why, n, "invalid RE2 enemy sound bank %u", bank);
    return -1;
}

const PeImpReader g_re2_sound_ps1 = {
    .api = {PE_IMP_API_VERSION, sizeof(PeImpReader)},
    .game = "re2", .platform = "ps1", .format = "vab",
    .version = 1, .produces = PE_IMP_SOUND, .match = "COMMON/SOUND/*.EDH",
    .tags = "sound sample", .companion = ".VB", .read_pair = pe_imp_vab_pair
};

const PeImpReader g_re2_enemy_sound_ps1 = {
    .api = {PE_IMP_API_VERSION, sizeof(PeImpReader)}, .game = "re2", .platform = "ps1",
    .format = "enemy-vbs", .version = 2, .produces = PE_IMP_SOUND, .match = "COMMON/SOUND/ENEMSE.VBS",
    .tags = "enemy sound", .read = read_enemy_banks
};
