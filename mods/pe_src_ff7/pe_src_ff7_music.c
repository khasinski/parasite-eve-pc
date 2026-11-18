/* Final Fantasy VII music. Read only.
 *   field files  FIELD/<name>.DAT, LZS-compressed: u32 compressed size, then LZSS (a flag byte,
 *                low bit first, 1 = a literal; a reference b0 | (b1 & 0xF0) << 4 into a 4 KiB ring
 *                that starts at 0xFEE, length (b1 & 15) + 3). A field's music is AKAO blocks inside
 *                it (pe_imp_akao.h has the layout); fields share songs, so a song's ID is its AKAO
 *                song number: "music/akao_<nn>", one catalog entry with every field it's in.
 *   cues         ENEMY6/<name>.SND: an AKAO block as it is (the battle fanfare, game over)
 *   instruments  SOUND/INSTR.DAT + INSTR.ALL become "bank/instr" (pe_src_ff7.c); every song names it.
 * On the US discs: 92 songs. Files that aren't LZS or hold no AKAO are passed over quietly. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pe_imp.h"
#include "pe_src_ff7.h"

#define LZS_MAX (2u << 20)

static unsigned le16(const uint8_t *p) { return p[0] | (unsigned)p[1] << 8; }
static uint32_t le32(const uint8_t *p) { return le16(p) | (uint32_t)le16(p + 2) << 16; }

int ff7_lzs(const uint8_t *data, uint32_t size, uint8_t **out, uint32_t *out_size) {
    *out = NULL, *out_size = 0;
    if (size < 4) return -1;
    uint32_t n = le32(data);
    if (n > size - 4) return -1;
    const uint8_t *src = data + 4;
    uint8_t ring[4096] = {0};
    unsigned r = 0xfee;
    uint32_t cap = n * 2 + 64, len = 0, i = 0;
    uint8_t *o = malloc(cap);
    if (!o) return -1;
    while (i < n) {
        unsigned fl = src[i++];
        for (int k = 0; k < 8 && i < n; k++) {
            if (fl >> k & 1) {
                if (len == cap) {
                    if (cap >= LZS_MAX) { free(o); return -1; }
                    uint8_t *g = realloc(o, cap *= 2);
                    if (!g) { free(o); return -1; }
                    o = g;
                }
                o[len++] = ring[r] = src[i++], r = (r + 1) & 0xfff;
                continue;
            }
            if (i + 1 >= n) break;
            unsigned off = src[i] | (src[i + 1] & 0xf0u) << 4, run = (src[i + 1] & 15u) + 3;
            i += 2;
            for (unsigned j = 0; j < run; j++) {
                if (len == cap) {
                    if (cap >= LZS_MAX) { free(o); return -1; }
                    uint8_t *g = realloc(o, cap *= 2);
                    if (!g) { free(o); return -1; }
                    o = g;
                }
                uint8_t c = ring[(off + j) & 0xfff];
                o[len++] = ring[r] = c, r = (r + 1) & 0xfff;
            }
        }
    }
    *out = o, *out_size = len;
    return 0;
}

/* a plausible AKAO block at `at`: its length inside the data, a channel mask, offsets inside */
static uint32_t akao_at(const uint8_t *d, uint32_t size, uint32_t at) {
    if (at + 22 > size || memcmp(d + at, "AKAO", 4)) return 0;
    uint32_t len = le16(d + at + 6), mask = le32(d + at + 16);
    if (!len || len > size - at - 16 || !(mask & 0xffffff) || (mask & 0xff000000u)) return 0;
    unsigned channels = 0;
    for (uint32_t m = mask & 0xffffff; m; m &= m - 1) channels++;
    if (20 + 2 * channels > 16 + len) return 0;
    for (unsigned k = 0; k < channels; k++)
        if (22 + 2 * k + le16(d + at + 20 + 2 * k) >= 16 + len) return 0;
    return 16 + len;
}

static int emit_blocks(const uint8_t *d, uint32_t size, PeImpSink *sink, char *why, size_t n) {
    int emitted = 0;
    for (uint32_t at = 0; at + 22 <= size && emitted < 32; at++) {
        uint32_t len = akao_at(d, size, at);
        if (!len) continue;
        PeImpSong *song = pe_imp_song_alloc(PE_IMP_SONG_AKAO, 80 + len, 0, 0, 0);
        if (!song) { if (why && n) snprintf(why, n, "FF7: a song too long"); return -1; }
        memset(song->payload, 0, 80);
        memcpy(song->payload, "AKS1", 4);
        memcpy(song->payload + 8, "bank/instr", 10);
        memcpy(song->payload + 80, d + at, len);
        unsigned id = le16(d + at + 4);
        char sid[48], name[48];
        snprintf(sid, sizeof sid, "music/akao_%02x", id);
        snprintf(name, sizeof name, "Track %02X", id);
        if (pe_imp_emit(sink, PE_IMP_SONG, sid, name, song)) { if (why && n) snprintf(why, n, "FF7: couldn't catalog"); return -1; }
        emitted++;
        at += len - 1;
    }
    return 0;
}

static int read_field(const uint8_t *data, uint32_t size, const char *path, PeImpSink *sink, char *why, size_t n) {
    (void)path;
    uint8_t *d;
    uint32_t dn;
    if (ff7_lzs(data, size, &d, &dn)) return 0;   /* not compressed: no music here */
    int rc = emit_blocks(d, dn, sink, why, n);
    free(d);
    return rc;
}

static int read_cue(const uint8_t *data, uint32_t size, const char *path, PeImpSink *sink, char *why, size_t n) {
    (void)path;
    return emit_blocks(data, size, sink, why, n);
}

const PeImpReader g_ff7_field_music_ps1 = {
    .api = {PE_IMP_API_VERSION, sizeof(PeImpReader)},
    .game = "ff7", .platform = "ps1", .format = "field-akao",
    .version = 1, .produces = PE_IMP_SONG, .match = "FIELD/*.DAT",
    .tags = "music sequence", .read = read_field
};
const PeImpReader g_ff7_cue_music_ps1 = {
    .api = {PE_IMP_API_VERSION, sizeof(PeImpReader)},
    .game = "ff7", .platform = "ps1", .format = "snd-akao",
    .version = 1, .produces = PE_IMP_SONG, .match = "ENEMY6/*.SND",
    .tags = "music sequence", .read = read_cue
};
