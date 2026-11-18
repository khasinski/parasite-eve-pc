/* RE2 PS1 BGM archives store one or two sequences, a VAB header and samples.
 * The footer lists these offsets in reverse order after one opaque word.
 * The serialized VAB header has an extra four-byte slot before the VB data.
 * The size in the VAB header counts the standard header and complete VB. */
#include <stdio.h>
#include <string.h>
#include "pe_imp_spu.h"
#include "pe_src_re2.h"

static unsigned le16(const uint8_t *p)
{
    return p[0] | (unsigned)p[1] << 8;
}

static uint32_t le32(const uint8_t *p)
{
    return le16(p) | (uint32_t)le16(p + 2) << 16;
}

typedef struct {
    uint32_t secondary, instruments, instrument_size, samples, end;
} BgmRanges;

static int ranges(const uint8_t *data, uint32_t size, unsigned seqs, BgmRanges *out)
{
    enum { SEQ_HEADER = 15, VAB_HEADER = 32, VAB_PROGRAMS = 128 * 16,
           VAB_TONES = 16 * 32, VAB_LENGTHS = 512, VB_GAP = 4 };
    uint32_t footer_size = (3 + seqs) * 4;
    if (size < footer_size || le32(data + size - 4)) return 0;

    uint32_t end = size - footer_size;
    uint32_t vh = le32(data + size - (seqs + 1) * 4);
    uint32_t vb = le32(data + size - (seqs + 2) * 4);
    uint32_t split = seqs == 2 ? le32(data + size - 8) : vh;
    if (split < SEQ_HEADER || split > vh || vh > vb || vb > end ||
        vb - vh < VAB_HEADER || memcmp(data, "pQES", 4) ||
        (seqs == 2 && (vh - split < SEQ_HEADER || memcmp(data + split, "pQES", 4))) ||
        memcmp(data + vh, "pBAV", 4))
        return 0;

    const uint8_t *header = data + vh;
    unsigned programs = le16(header + 18), waves = le16(header + 22);
    uint32_t lengths = VAB_HEADER + VAB_PROGRAMS + programs * VAB_TONES;
    if (programs > 128 || waves > 254 || lengths + VAB_LENGTHS + VB_GAP != vb - vh ||
        le32(header + 12) != end - vh - VB_GAP || le16(header + lengths))
        return 0;

    uint32_t total = 0;
    for (unsigned i = 1; i <= waves; ++i) {
        uint32_t bytes = le16(header + lengths + i * 2) * 8u;
        if (bytes % PE_IMP_SPU_BLOCK) return 0;
        total += bytes;
    }
    if (total != end - vb) return 0;

    *out = (BgmRanges){split, vh, lengths + VAB_LENGTHS, vb, end};
    return 1;
}

static int read_bgm(const uint8_t *data, uint32_t size, const char *path, PeImpSink *sink,
                    char *why, size_t n)
{
    BgmRanges parts;
    char stem[24];
    if (size > PE_IMP_SONG_MAX || pe_imp_sound_bank_name(path, stem) ||
        (!ranges(data, size, 1, &parts) && !ranges(data, size, 2, &parts))) {
        if (why && n) snprintf(why, n, "invalid RE2 BGM sequence or instrument ranges");
        return -1;
    }

    PeImpSong *song = pe_imp_song_alloc(PE_IMP_SONG_PSX_SEQ_VAB,
        parts.secondary, parts.instruments - parts.secondary,
        parts.instrument_size, parts.end - parts.samples);
    if (!song) {
        if (why && n) snprintf(why, n, "could not allocate RE2 Song");
        return -1;
    }
    size_t sample_offset = (size_t)parts.instruments + parts.instrument_size;
    memcpy(song->payload, data, sample_offset);
    memcpy(song->payload + sample_offset, data + parts.samples, song->sample_size);

    char id[48], name[48];
    snprintf(id, sizeof id, "music/%s", stem);
    for (char *p = stem; *p; ++p)
        if (*p >= 'a' && *p <= 'z') *p -= 'a' - 'A';
    snprintf(name, sizeof name, "%s", stem);
    if (!pe_imp_emit(sink, PE_IMP_SONG, id, name, song)) return 0;
    if (why && n) snprintf(why, n, "could not catalog RE2 Song");
    return -1;
}

const PeImpReader g_re2_music_ps1 = {
    .api = {PE_IMP_API_VERSION, sizeof(PeImpReader)},
    .game = "re2", .platform = "ps1", .format = "bgm-seq-vab",
    .version = 2, .produces = PE_IMP_SONG, .match = "COMMON/SOUND/*.BGM",
    .tags = "music sequence", .read = read_bgm, .path_key = 1
};
