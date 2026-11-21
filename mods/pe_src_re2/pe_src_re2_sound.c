#include <stdio.h>
#include <string.h>
#include "pe_imp_spu.h"

static unsigned read16(const uint8_t *p) { return p[0] | (unsigned)p[1] << 8; }

/* The first ENEMSE set is the single standard zombie bank. Its EDT precedes the
 * VAB at 0x80 and its sample body starts on the next sector after the VAB header. */
static int read_zombie_bank(const uint8_t *data, uint32_t size, const char *path, PeImpSink *sink,
                            char *why, size_t n) {
    (void)path;
    const unsigned offset = 128, fixed = 32 + 2048;
    if (size < offset + fixed || memcmp(data + offset, "pBAV", 4)) goto invalid;
    const uint8_t *header = data + offset;
    const unsigned programs = read16(header + 18), waves = read16(header + 22);
    const unsigned table = fixed + programs * 512, length = table + 512;
    const unsigned body = (offset + length + 2047) & ~2047u;
    if (!programs || programs > 128 || !waves || waves > 254 || body > size) goto invalid;
    unsigned samples = 0;
    for (unsigned i = 1; i <= waves; ++i) samples += 8u * read16(header + table + 2 * i);
    if (samples <= size - body)
        return pe_imp_vab_read(sink, "enemy00", header, length, data + body, samples, why, n);
invalid:
    snprintf(why, n, "invalid RE2 enemy sound bank");
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
    .format = "enemy-vbs", .version = 1, .produces = PE_IMP_SOUND, .match = "COMMON/SOUND/ENEMSE.VBS",
    .tags = "enemy sound", .read = read_zombie_bank
};
