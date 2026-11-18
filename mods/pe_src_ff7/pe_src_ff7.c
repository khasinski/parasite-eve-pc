/* FFVII PS1's main instrument bank. These are raw sample auditions. */
#include <stdio.h>
#include "pe_imp_spu.h"
#include "pe_mod_host.h"
#include "pe_src_ff7.h"
#include <stdlib.h>
#include <string.h>

static uint32_t le32(const uint8_t *p)
{
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int read_instruments(const uint8_t *table, uint32_t table_size,
                            const uint8_t *bank, uint32_t bank_size, const char *path,
                            PeImpSink *sink, char *why, size_t n)
{
    enum { HEADER = 16, RECORD = 64, INSTRUMENTS = 128 };
    (void)path;
    if (table_size != RECORD * INSTRUMENTS || bank_size < HEADER) goto invalid;
    uint32_t base = le32(bank), size = le32(bank + 4);
    if (!size || size > bank_size - HEADER || size % PE_IMP_SPU_BLOCK) goto invalid;
    const uint8_t *body = bank + HEADER;
    unsigned count = 0;
    for (unsigned i = 0; i < INSTRUMENTS; ++i) {
        uint32_t address = le32(table + i * RECORD);
        if (!address) continue;
        if (address < base) goto invalid;
        uint32_t start = address - base;
        if (start >= size || start % PE_IMP_SPU_BLOCK) goto invalid;
        uint32_t end = start;
        while (end < size && !(body[end + 1] & 1)) end += PE_IMP_SPU_BLOCK;
        if (end == size) goto invalid;
        if (pe_imp_spu_emit(sink, "instr", i, body + start, end + PE_IMP_SPU_BLOCK - start, why, n))
            return -1;
        ++count;
    }
    if (count) {
        /* the bank as it is, for the songs (pe_src_ff7_music.c): header, table, samples */
        PeImpSong *song = pe_imp_song_alloc(PE_IMP_SONG_AKAO_BANK, 16, 0, RECORD * INSTRUMENTS, size);
        if (!song) goto invalid;
        memset(song->payload, 0, 16);
        memcpy(song->payload, "AKB1", 4);
        memcpy(song->payload + 4, bank, 4);
        memcpy(song->payload + 16, table, RECORD * INSTRUMENTS);
        memcpy(song->payload + 16 + RECORD * INSTRUMENTS, body, size);
        if (pe_imp_emit(sink, PE_IMP_SONG, "bank/instr", "Instruments", song)) goto invalid;
        return 0;
    }
invalid:
    if (why && n) snprintf(why, n, "invalid FFVII instrument bank or sample boundaries");
    return -1;
}

const PeImpReader g_ff7_instruments_ps1 = {
    .api = {PE_IMP_API_VERSION, sizeof(PeImpReader)},
    .game = "ff7", .platform = "ps1", .format = "akao-instr",
    .version = 2, .produces = PE_IMP_SOUND | PE_IMP_SONG, .match = "SOUND/INSTR.DAT",
    .tags = "sound sample instrument", .companion = ".ALL", .read_pair = read_instruments
};

static int init(void)
{
    return pe_imp_add_game("ff7", "Final Fantasy VII", "Final Fantasy") ||
           pe_imp_add_reader(&g_ff7_instruments_ps1) || pe_imp_add_reader(&g_ff7_field_music_ps1) ||
           pe_imp_add_reader(&g_ff7_cue_music_ps1);
}

const PeMod pe_mod_pe_src_ff7 = {"pe_src_ff7", "Import source: Final Fantasy VII", "0.1", NULL, init, NULL};
