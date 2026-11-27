/* Dino Crisis music: the .XAS files in PSX/DATA_XA, streamed CD-XA audio. Read only.
 * Each file interleaves streams by channel (the subheader's file and channel); a stream ends at a
 * sector with the end-of-file submode bit. On the US disc STAGE00.XAS holds the music: stereo,
 * 37.8 kHz, seven channels of 14 to 30 seconds. The other files' mono 18.9 kHz channels are
 * spoken lines, left to a later voice reader. Each stereo stream of 8 sectors or more becomes a
 * Song in the XA format: "music/<file>_c<channel>" ("_<n>" for later clips in the channel). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pe_imp_spu.h"
#include "pe_src_dc1.h"

#define MIN_SECTORS 8
#define MAX_STREAMS 64

static int read_xas(const uint8_t *data, uint32_t size, const char *path, PeImpSink *sink, char *why, size_t n) {
    char stem[24];
    if (pe_imp_sound_bank_name(path, stem) || size % PE_IMP_XA_RECORD) {
        if (why && n) snprintf(why, n, "XAS: bad file name or records");
        return -1;
    }
    uint32_t records = size / PE_IMP_XA_RECORD;
    /* one pass per (file, channel) found; each clip is a run up to an end-of-file sector */
    unsigned seen[MAX_STREAMS][2], streams = 0;
    for (uint32_t r = 0; r < records; r++) {
        const uint8_t *h = data + (size_t)r * PE_IMP_XA_RECORD;
        unsigned k = 0;
        while (k < streams && (seen[k][0] != h[0] || seen[k][1] != h[1])) k++;
        if (k == streams && streams < MAX_STREAMS) seen[streams][0] = h[0], seen[streams][1] = h[1], streams++;
    }
    for (unsigned k = 0; k < streams; k++) {
        uint32_t clip_start = 0, count = 0, coding = 0, clip = 0;
        for (uint32_t r = 0; r <= records; r++) {
            const uint8_t *h = r < records ? data + (size_t)r * PE_IMP_XA_RECORD : NULL;
            int mine = h && h[0] == seen[k][0] && h[1] == seen[k][1];
            if (mine && !count) clip_start = r, coding = h[3];
            if (mine) count++;
            int end = (mine && (h[2] & 0x80)) || (!h && count);
            if (!end) continue;
            if (count >= MIN_SECTORS && (coding & 1)) {          /* stereo: music */
                PeImpSong *song = pe_imp_song_alloc(PE_IMP_SONG_PSX_XA, 16, 0, 0, count * 2304u);
                if (!song) { if (why && n) snprintf(why, n, "XAS: a stream is too long for a Song"); return -1; }
                uint8_t *s = song->payload;
                memset(s, 0, 16);
                memcpy(s, "XAS1", 4);
                s[4] = (uint8_t)coding, s[5] = (uint8_t)seen[k][0], s[6] = (uint8_t)seen[k][1];
                s[8] = (uint8_t)count, s[9] = (uint8_t)(count >> 8), s[10] = (uint8_t)(count >> 16);
                uint32_t w = 0;
                for (uint32_t q = clip_start; q <= r && q < records && w < count; q++) {
                    const uint8_t *g = data + (size_t)q * PE_IMP_XA_RECORD;
                    if (g[0] != seen[k][0] || g[1] != seen[k][1]) continue;
                    memcpy(s + 16 + (size_t)w * 2304, g + 4, 2304);
                    w++;
                }
                char id[48], name[48], label[24];
                snprintf(label, sizeof label, "%s", stem);
                for (char *p = label; *p; ++p) if (*p >= 'a' && *p <= 'z') *p = (char)(*p - 'a' + 'A');
                if (clip) snprintf(id, sizeof id, "music/%s_c%u_%u", stem, seen[k][1], clip);
                else snprintf(id, sizeof id, "music/%s_c%u", stem, seen[k][1]);
                snprintf(name, sizeof name, "%s %u%s", label, seen[k][1] + 1, clip ? "+" : "");
                if (pe_imp_emit(sink, PE_IMP_SONG, id, name, song)) {
                    if (why && n) snprintf(why, n, "XAS: couldn't catalog a stream");
                    return -1;
                }
            }
            if (count) clip++;
            count = 0;
        }
    }
    return 0;
}

const PeImpReader g_dc1_music_ps1 = {
    .api = {PE_IMP_API_VERSION, sizeof(PeImpReader)},
    .game = "dc1", .platform = "ps1", .format = "xas",
    .version = 1, .produces = PE_IMP_SONG, .match = "PSX/DATA_XA/*.XAS",
    .tags = "music stream", .read = read_xas, .path_key = 1, .xa = 1
};
