/* RE3's .VH uses a custom index. In the paired VB files, each waveform ends
 * at the dedicated SPU flag-7 block, including the short waveform at offset 0. */
#include <stdio.h>
#include "pe_imp_spu.h"

static int read_bank(const uint8_t *vh, uint32_t vh_size, const uint8_t *vb, uint32_t vb_size,
                     const char *path, PeImpSink *sink, char *why, size_t n)
{
    (void)vh;
    char bank[24];
    if (vh_size < 64 || vh_size % 16 || vb_size % PE_IMP_SPU_BLOCK ||
        pe_imp_sound_bank_name(path, bank)) goto invalid;
    unsigned wave = 0;
    uint32_t start = 0;
    for (uint32_t at = 0; at < vb_size; at += PE_IMP_SPU_BLOCK) {
        if (vb[at + 1] != 7) continue;
        uint32_t end = at + PE_IMP_SPU_BLOCK;
        if (++wave > 254) goto invalid;
        if (pe_imp_spu_emit(sink, bank, wave, vb + start, end - start, why, n)) return -1;
        start = end;
    }
    if (wave && start == vb_size) return 0;
invalid:
    if (why && n) snprintf(why, n, "invalid RE3 VH/VB waveform boundaries");
    return -1;
}

const PeImpReader g_re3_sound_ps1 = {
    .api = {PE_IMP_API_VERSION, sizeof(PeImpReader)},
    .game = "re3", .platform = "ps1", .format = "vh-spu",
    .version = 1, .produces = PE_IMP_SOUND, .match = "CD_DATA/SOUND/*.VH",
    .tags = "sound sample", .companion = ".VB", .read_pair = read_bank
};
