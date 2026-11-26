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

typedef struct { uint16_t channel, start, count; } XaCue;
typedef struct { const XaCue *cue; unsigned count; } XaBank;
#include "pe_re2_voice_tables.inc"

static int read_voice(const uint8_t *data, uint32_t size, const char *path, PeImpSink *sink,
                      char *why, size_t n)
{
    const unsigned player = (unsigned)(path[2] - '0');
    const char *name = strrchr(path, '/');
    if (!name || player >= 2 || name[6] != path[2] || name[7] < '0' || name[7] > '7') return 0;
    const unsigned stage = (unsigned)(name[7] - '0');
    const XaBank *bank = &k_voice[player][stage];
    const unsigned sectors = size / PE_IMP_XA_RECORD;
    for (unsigned i = 0; i < bank->count; ++i) {
        const XaCue *cue = &bank->cue[i];
        unsigned first = (unsigned)cue->start * 16 + cue->channel;
        if (cue->channel >= 16 || first >= sectors || !cue->count) continue;
        unsigned count = 0;
        uint8_t coding = 0;
        for (unsigned k = 0; k < cue->count && first + k * 16 < sectors; ++k) {
            const uint8_t *record = data + (size_t)(first + k * 16) * PE_IMP_XA_RECORD;
            if (record[0] != 1 || record[1] != cue->channel || (record[2] & 0x24) != 0x24) break;
            if (k && record[3] != coding) break;
            coding = record[3];
            ++count;
        }
        if (!count) continue;
        PeImpSong *song = pe_imp_song_alloc(PE_IMP_SONG_PSX_XA, 16, 0, 0, count * 2304u);
        if (!song) { snprintf(why, n, "XA cue exceeds asset budget"); return -1; }
        memcpy(song->payload, "XAS1", 4);
        song->payload[4] = coding;
        song->payload[5] = 1;
        song->payload[6] = (uint8_t)cue->channel;
        song->payload[8] = (uint8_t)count;
        song->payload[9] = (uint8_t)(count >> 8);
        for (unsigned k = 0; k < count; ++k)
            memcpy(song->payload + 16 + 2304u * k,
                   data + (size_t)(first + k * 16) * PE_IMP_XA_RECORD + 4, 2304);
        char id[32];
        snprintf(id, sizeof id, "voice/%u/%u/%u", player, stage, i);
        if (pe_imp_emit(sink, PE_IMP_SONG, id, id, song)) return -1;
    }
    return 0;
}

const PeImpReader g_re2_voice_ps1 = {
    .api = {PE_IMP_API_VERSION, sizeof(PeImpReader)}, .game = "re2", .platform = "ps1",
    .format = "xa-voice", .version = 1, .produces = PE_IMP_SONG,
    .match = "PL?/VOICE/VOICE??.XAS", .tags = "dialogue", .read = read_voice, .xa = PE_IMP_XA_INDEXED
};
