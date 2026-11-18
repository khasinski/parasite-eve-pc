/* Resident Evil 3 music. Read only. Worked out from the disc:
 *   BGM      u32 offset of the bank header, u32 tempo (microseconds per quarter), u16 ticks per
 *            quarter, 2 bytes time signature; Sony sequence events from byte 12 (no delta time
 *            before the first) up to the bank header
 *   bank     a compact VAB: u32 samples' size, u32 bank size, u32 offset of the sample table,
 *            then a VAB header's second half (programs at 18, tones at 20, samples at 22, volume
 *            and pan at 24); `programs` 16-byte program records (not 128); 32-byte VAB tone records
 *            (each naming its program at 20, its sample at 22; sample 0 = unused slot) up to the
 *            table; the table holds each sample's start in 8-byte units, one more entry for the end
 *   samples  SOUND/MAINxx.VB beside its .BGM, or a room archive's second entry
 *   ARD      room archives (STAGEn/Rnnn.ARD): u32 ?, u32 entry count, 8-byte entries (u32 size,
 *            u32 attributes) from 8; entries from 0x800, each 2 KiB aligned. Entry 0 is the room's
 *            BGM, entry 1 its samples. 169 rooms share 42 tracks: a room track's ID is its content's
 *            hash, so rooms playing the same music are one catalog entry.
 * Each track is rebuilt as a standard SEQ and VAB (pBAV header, 128 program records, 16 tones per
 * program present, 256 sample sizes) so the same player runs it. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pe_imp_spu.h"
#include "pe_src_re3.h"

static unsigned le16(const uint8_t *p) { return p[0] | (unsigned)p[1] << 8; }
static uint32_t le32(const uint8_t *p) { return le16(p) | (uint32_t)le16(p + 2) << 16; }

static int refuse(char *why, size_t n, const char *m) {
    if (why && n) snprintf(why, n, "RE3 music: %s", m);
    return -1;
}

/* BGM + samples -> a Song in the standard layout */
static int convert(const uint8_t *bgm, uint32_t size, const uint8_t *vb, uint32_t vb_size, PeImpSong **out,
                   char *why, size_t n) {
    *out = NULL;
    if (size < 16 + 32) return refuse(why, n, "too short");
    uint32_t vh = le32(bgm);
    if (vh < 16 || vh > size - 32) return refuse(why, n, "bank header outside the file");
    const uint8_t *h = bgm + vh;
    uint32_t vbs = le32(h), vh_size = le32(h + 4), table = le32(h + 8);
    unsigned programs = le16(h + 18), samples = le16(h + 22);
    uint32_t tones_at = 32 + programs * 16u;
    if (vbs != vb_size || vh_size > size - vh || programs > 128 || !programs || samples > 254 || table < tones_at ||
        table + 2u * (samples + 1) > vh_size || (table - tones_at) % 32)
        return refuse(why, n, "bank sizes");
    if (le16(h + table + 2 * samples) * 8u != vb_size) return refuse(why, n, "sample table doesn't end with the samples");
    unsigned records = (table - tones_at) / 32, present = 0;
    for (unsigned p = 0; p < programs; p++) present += h[32 + 16 * p] != 0;
    uint32_t std = 32 + 2048 + present * 512u + 512, seq = 15 + 1 + (vh - 12);
    PeImpSong *song = pe_imp_song_alloc(PE_IMP_SONG_PSX_SEQ_VAB, seq, 0, std, vb_size);
    if (!song) return refuse(why, n, "too large for a Song");
    uint8_t *s = song->payload;
    /* the sequence: a standard header, a zero delta, the events */
    memcpy(s, "pQES\0\0\0\1", 8);
    s[8] = bgm[9], s[9] = bgm[8];                              /* ticks, big-endian */
    s[10] = bgm[6], s[11] = bgm[5], s[12] = bgm[4];            /* tempo, 24-bit big-endian */
    s[13] = bgm[10], s[14] = bgm[11];
    s[15] = 0;
    memcpy(s + 16, bgm + 12, vh - 12);
    /* the bank: header, 128 programs, tone blocks for the programs present, sample sizes */
    uint8_t *v = s + seq;
    memset(v, 0, std);
    memcpy(v, "pBAV", 4);
    v[4] = 7;
    uint32_t total = std + vb_size;
    v[12] = (uint8_t)total, v[13] = (uint8_t)(total >> 8), v[14] = (uint8_t)(total >> 16), v[15] = (uint8_t)(total >> 24);
    memcpy(v + 16, h + 16, 16);
    v[18] = (uint8_t)present, v[19] = 0;
    unsigned block = 0;
    for (unsigned p = 0; p < programs; p++) {
        const uint8_t *pr = h + 32 + 16 * p;
        if (!pr[0]) continue;
        uint8_t *dst = v + 32 + 16 * p, *tones = v + 32 + 2048 + 512u * block;
        memcpy(dst, pr, 16);
        unsigned t = 0;
        for (unsigned r = 0; r < records && t < 16; r++) {
            const uint8_t *rec = h + tones_at + 32 * r;
            if (le16(rec + 20) != p || !le16(rec + 22) || le16(rec + 22) > samples) continue;
            memcpy(tones + 32 * t++, rec, 32);
        }
        dst[0] = (uint8_t)t;
        block++;
    }
    uint8_t *sizes = v + 32 + 2048 + present * 512u;
    for (unsigned i = 1; i <= samples; i++) {
        unsigned a = le16(h + table + 2 * (i - 1)), b = le16(h + table + 2 * i);
        if (b < a) { free(song); return refuse(why, n, "sample table goes backwards"); }
        sizes[2 * i] = (uint8_t)(b - a), sizes[2 * i + 1] = (uint8_t)((b - a) >> 8);
    }
    memcpy(s + seq + std, vb, vb_size);
    *out = song;
    return 0;
}

static void upper(const char *in, char *out, size_t n) {
    snprintf(out, n, "%s", in);
    for (char *p = out; *p; ++p) if (*p >= 'a' && *p <= 'z') *p = (char)(*p - 'a' + 'A');
}

/* SOUND/MAINxx.BGM with MAINxx.VB */
static int read_bgm(const uint8_t *bgm, uint32_t size, const uint8_t *vb, uint32_t vb_size, const char *path,
                    PeImpSink *sink, char *why, size_t n) {
    char stem[24], id[48], name[48];
    if (pe_imp_sound_bank_name(path, stem)) return refuse(why, n, "bad file name");
    PeImpSong *song;
    if (convert(bgm, size, vb, vb_size, &song, why, n)) return -1;
    snprintf(id, sizeof id, "music/%s", stem);
    upper(stem, name, sizeof name);
    return pe_imp_emit(sink, PE_IMP_SONG, id, name, song) ? refuse(why, n, "couldn't catalog") : 0;
}

/* a room archive's track: entries 0 and 1 */
static int read_ard(const uint8_t *data, uint32_t size, const char *path, PeImpSink *sink, char *why, size_t n) {
    char stem[24], id[48], name[48], hex[65];
    if (pe_imp_sound_bank_name(path, stem)) return refuse(why, n, "bad file name");
    if (size < 0x800 + 16) return 0;
    uint32_t count = le32(data + 4);
    if (count < 2 || count > 254) return 0;                       /* no music in this archive */
    uint32_t s0 = le32(data + 8), s1 = le32(data + 16);
    uint64_t at1 = 0x800 + ((uint64_t)s0 + 2047) / 2048 * 2048;
    if (s0 > size - 0x800 || at1 > size || s1 > size - at1) return refuse(why, n, "archive entries past the end");
    PeImpSong *song;
    if (convert(data + 0x800, s0, data + at1, s1, &song, why, n)) return 0;   /* not music: nothing to read */
    pe_imp_sha256_hex(song->payload, (size_t)song->seq_size[0] + song->instrument_size + song->sample_size, hex);
    hex[8] = 0;
    snprintf(id, sizeof id, "music/room_%s", hex);
    upper(stem, name, sizeof name);
    snprintf(name + strlen(name), sizeof name - strlen(name), " room track");
    return pe_imp_emit(sink, PE_IMP_SONG, id, name, song) ? refuse(why, n, "couldn't catalog") : 0;
}

const PeImpReader g_re3_bgm_ps1 = {
    .api = {PE_IMP_API_VERSION, sizeof(PeImpReader)},
    .game = "re3", .platform = "ps1", .format = "bgm-vb",
    .version = 1, .produces = PE_IMP_SONG, .match = "CD_DATA/SOUND/MAIN??.BGM",
    .tags = "music sequence", .companion = ".VB", .read_pair = read_bgm
};
const PeImpReader g_re3_room_music_ps1 = {
    .api = {PE_IMP_API_VERSION, sizeof(PeImpReader)},
    .game = "re3", .platform = "ps1", .format = "ard-music",
    .version = 1, .produces = PE_IMP_SONG, .match = "CD_DATA/STAGE?/R???.ARD",
    .tags = "music sequence", .read = read_ard
};
