/* Resident Evil (Director's Cut Dual Shock) music: <disc folder>/SOUND/SEPxx.HSB. Read only.
 * Worked out from the disc: one instrument bank and a package of sequences.
 *   VAB header  "pBAV" at 0: programs at 18, total size (header + samples) at 12; the header is
 *               32 + 128 x 16 + programs x 512 + 512 bytes
 *   package     right after the header: "pQES", u16 version (0), then per sequence (big-endian):
 *               u16 id, u16 ticks per quarter, u24 tempo, u16 time signature, u32 length, then
 *               that many bytes of events (a delta time first). A zero entry ends it.
 *   samples     from 4 bytes after the package to the end of the file: the bank's total counts
 *               4 bytes more than are there (a gap as in RE2's BGM files); the last 4 are zero,
 *               and the last block is silenced if that leaves it broken (it ends the bank)
 * Each package holds the room's track and its short cues; 14-byte placeholders are skipped. Each
 * sequence becomes a Song with the package's bank, its header made standard ("pQES", version 1,
 * ticks, tempo, signature) so the same player runs it. 216 sequences in 57 files on the US disc. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pe_imp_spu.h"
#include "pe_src_re1.h"

#define MIN_SEQUENCE 32                           /* shorter ones are placeholders */

static unsigned le16(const uint8_t *p) { return p[0] | (unsigned)p[1] << 8; }
static uint32_t le32(const uint8_t *p) { return le16(p) | (uint32_t)le16(p + 2) << 16; }
static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

static int refuse(char *why, size_t n, const char *m) {
    if (why && n) snprintf(why, n, "SEP: %s", m);
    return -1;
}

static int read_sep(const uint8_t *data, uint32_t size, const char *path, PeImpSink *sink, char *why, size_t n) {
    char stem[24];
    if (pe_imp_sound_bank_name(path, stem)) return refuse(why, n, "bad file name");
    if (size < 32 + 2048 + 512 + 6 || memcmp(data, "pBAV", 4)) return refuse(why, n, "no instrument bank");
    unsigned programs = le16(data + 18);
    uint32_t header = 32 + 2048 + programs * 512u + 512, total = le32(data + 12);
    if (!programs || programs > 128 || total <= header + 4 || total - header > size - header - 6)
        return refuse(why, n, "bank sizes");
    uint32_t samples = total - header, samples_at = size - samples + 4;   /* after the 4-byte gap */
    if (memcmp(data + header, "pQES", 4) && memcmp(data + header, "pQEP", 4)) return refuse(why, n, "no sequence package");
    int emitted = 0;
    for (uint32_t at = header + 6; at + 13 <= samples_at - 4;) {
        unsigned id = (unsigned)(data[at] << 8 | data[at + 1]);
        const uint8_t *timing = data + at + 2;    /* ticks (2), tempo (3), signature (2) */
        uint32_t len = be32(data + at + 9);
        if (!len) break;
        if (len > samples_at - 4 - at - 13) return refuse(why, n, "a sequence runs into the samples");
        if (len >= MIN_SEQUENCE && emitted < 32) {
            PeImpSong *song = pe_imp_song_alloc(PE_IMP_SONG_PSX_SEQ_VAB, 15 + len, 0, header, samples);
            if (!song) return refuse(why, n, "too large for a Song");
            uint8_t *s = song->payload;
            memcpy(s, "pQES\0\0\0\1", 8);
            memcpy(s + 8, timing, 7);
            memcpy(s + 15, data + at + 13, len);
            memcpy(s + 15 + len, data, header);
            memcpy(s + 15 + len + header, data + samples_at, samples - 4);
            memset(s + 15 + len + header + samples - 4, 0, 4);
            uint8_t *last = s + 15 + len + header + samples - 16;  /* cut short by the gap: silence it if broken */
            if ((last[0] >> 4) > 4 || (last[0] & 15) > 12) memset(last, 0, 16);
            char id_s[48], name[48], label[24];
            snprintf(label, sizeof label, "%s", stem);
            for (char *p = label; *p; ++p) if (*p >= 'a' && *p <= 'z') *p = (char)(*p - 'a' + 'A');
            snprintf(id_s, sizeof id_s, "music/%s_%u", stem, id);
            snprintf(name, sizeof name, "%s %u", label, id);
            if (pe_imp_emit(sink, PE_IMP_SONG, id_s, name, song)) return refuse(why, n, "couldn't catalog a sequence");
            emitted++;
        }
        at += 13 + len;
    }
    return 0;
}

const PeImpReader g_re1_music_ps1 = {
    .api = {PE_IMP_API_VERSION, sizeof(PeImpReader)},
    .game = "re1", .platform = "ps1", .format = "sep-vab",
    .version = 1, .produces = PE_IMP_SONG, .match = "*/SOUND/SEP??.HSB",
    .tags = "music sequence", .read = read_sep, .path_key = 1
};
