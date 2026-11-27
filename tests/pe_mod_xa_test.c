/* CD-XA streamed audio (mods/host/import/pe_imp_xa.c, pe_src_dc1_music.c).
 *   made-up   sectors built here: stereo 4-bit (left units +1, right -1 at range 0: +-4096), a
 *             range shift, the first filter carrying the previous sample; mono 18.9 kHz played at
 *             44.1 kHz for 44100 / 18900 times its frames; a stream played twice is twice as long
 *   disc      with PE_TEST_DC1_DISC: Dino Crisis's DATA_XA scanned; the seven stereo music streams
 *             of STAGE00 become Songs, the mono speech doesn't; each plays with sound.
 *             PE_TEST_XA_WAV=<path> writes the first one out to listen to. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pe_imp.h"
#include "pe_imp_internal.h"
#include "pe_imp_xa.h"
#include "pe_src_dc1.h"

static uint8_t sector[2304];

/* every group: units' params, and every sample nibble the same per unit */
static void fill(const uint8_t params[8], const uint8_t nib[8]) {
    for (int g = 0; g < 18; g++) {
        uint8_t *grp = sector + 128 * g;
        memset(grp, 0, 128);
        for (int u = 0; u < 8; u++) grp[4 + u] = params[u];
        for (int j = 0; j < 28; j++)
            for (int u = 0; u < 8; u += 2) grp[16 + 4 * j + u / 2] = (uint8_t)(nib[u] | nib[u + 1] << 4);
    }
}

static void made_up(void) {
    int16_t out[4032];
    int32_t st[2][2] = {{0, 0}, {0, 0}};
    /* stereo: even units (left) +1, odd (right) -1, range 0, no filter */
    fill((const uint8_t[]){0, 0, 0, 0, 0, 0, 0, 0}, (const uint8_t[]){1, 15, 1, 15, 1, 15, 1, 15});
    unsigned frames = pe_imp_xa_decode(sector, 1, st, out);
    assert(frames == 2016 && out[0] == 4096 && out[1] == -4096 && out[2 * 2015] == 4096 && out[2 * 2015 + 1] == -4096);
    /* range 4: 4096 >> 4 = 256; filter 1 adds 60/64 of the previous sample */
    memset(st, 0, sizeof st);
    fill((const uint8_t[]){4, 4, 4, 4, 4, 4, 4, 4}, (const uint8_t[]){1, 1, 1, 1, 1, 1, 1, 1});
    frames = pe_imp_xa_decode(sector, 0, st, out);
    assert(frames == 4032 && out[0] == 256 && out[4031] == 256);
    memset(st, 0, sizeof st);
    fill((const uint8_t[]){0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14}, (const uint8_t[]){1, 1, 1, 1, 1, 1, 1, 1});
    pe_imp_xa_decode(sector, 0, st, out);
    assert(out[0] == 256 && out[1] == 256 + (256 * 60 + 32) / 64);
    /* a two-sector mono 18.9 kHz stream at 44.1 kHz, once and twice */
    uint8_t header[16] = {'X', 'A', 'S', '1', 4};
    static uint8_t two[2 * 2304];
    memcpy(two, sector, 2304), memcpy(two + 2304, sector, 2304);
    for (unsigned loops = 1; loops <= 2; loops++) {
        char why[100];
        PeImpXaPlayer *p = pe_imp_xa_open(header, 16, two, sizeof two, loops, why, sizeof why);
        assert(p);
        static int16_t pcm[2 * 50000];
        uint32_t n = 0, got;
        while ((got = pe_imp_xa_render(p, pcm + 2 * n, 1000)) > 0 && n < 49000) n += got;
        double want = 2.0 * 4032 * loops * 44100.0 / 18900.0;
        printf("mono 18.9 kHz, %u time(s): %u frames at 44.1 kHz (expected about %.0f)\n", loops, n, want);
        assert(fabs(n - want) < 4 && pcm[2 * 100] == pcm[2 * 100 + 1]);
        pe_imp_xa_close(p);
    }
    char why[100];
    assert(!pe_imp_xa_open(header, 16, two, 100, 0, why, sizeof why));
    header[0] = 'Y';
    assert(!pe_imp_xa_open(header, 16, two, sizeof two, 0, why, sizeof why));
    printf("made-up XA: stereo and mono, range, filter, resampling, loops\n");
}

static void write_wav(const char *path, const int16_t *pcm, uint32_t frames) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    uint32_t data = frames * 4, rate = PE_IMP_XA_RATE, byterate = rate * 4, size = 36 + data, fmt = 16;
    uint16_t pcmfmt = 1, ch = 2, align = 4, bits = 16;
    fwrite("RIFF", 1, 4, f), fwrite(&size, 4, 1, f), fwrite("WAVEfmt ", 1, 8, f), fwrite(&fmt, 4, 1, f);
    fwrite(&pcmfmt, 2, 1, f), fwrite(&ch, 2, 1, f), fwrite(&rate, 4, 1, f), fwrite(&byterate, 4, 1, f);
    fwrite(&align, 2, 1, f), fwrite(&bits, 2, 1, f), fwrite("data", 1, 4, f), fwrite(&data, 4, 1, f);
    fwrite(pcm, 4, frames, f);
    fclose(f);
}

static void disc(const char *cue) {
    char why[300] = "";
    PeImpScan sc;
    pe_imp_registry_reset();
    assert(!pe_imp_add_reader(&g_dc1_music_ps1));
    assert(!pe_imp_scan("pe-xa-test/store", cue, NULL, &sc));
    if (sc.refused) fprintf(stderr, "  refused: %s\n", sc.last_error);
    PeImpAsset *a;
    int n = pe_imp_catalog_assets("pe-xa-test/store", "dc1", &a), songs = 0, loud = 0;
    static int16_t pcm[2 * 5 * PE_IMP_XA_RATE];
    for (int i = 0; i < n; i++) {
        if (strcmp(a[i].type, "Song")) continue;
        songs++;
        PeImpSong *song;
        assert(!pe_imp_catalog_song("pe-xa-test/store", a[i].id, &song, why, sizeof why));
        assert(song->format == PE_IMP_SONG_PSX_XA);
        PeImpXaPlayer *p = pe_imp_xa_open(song->payload, 16, song->payload + 16, song->sample_size, 1, why, sizeof why);
        assert(p);
        uint32_t got = 0, k;
        while (got < 5 * PE_IMP_XA_RATE && (k = pe_imp_xa_render(p, pcm + 2 * got, 4096 < 5 * PE_IMP_XA_RATE - got ? 4096 : 5 * PE_IMP_XA_RATE - got)) > 0) got += k;
        double sum = 0;
        for (uint32_t q = 0; q < 2 * got; q++) sum += (double)pcm[q] * pcm[q];
        double rms = got ? sqrt(sum / (2.0 * got)) : 0;
        loud += rms > 300;
        printf("  %s (%s): %.1f s of sectors, rms %.0f\n", a[i].id, a[i].name, song->sample_size / 2304 * 2016 / 37800.0, rms);
        const char *wav = getenv("PE_TEST_XA_WAV");
        if (wav && *wav && songs == 1) write_wav(wav, pcm, got);
        pe_imp_xa_close(p);
        free(song);
    }
    free(a);
    printf("Dino Crisis: %d music streams from %d files, %d with sound\n", songs, sc.files_read, loud);
    assert(songs == 7 && loud == 7 && !sc.refused);
}

int main(void) {
    made_up();
    const char *cue = getenv("PE_TEST_DC1_DISC");
    if (cue && *cue) disc(cue);
    else printf("PE_TEST_DC1_DISC not set: disc part skipped\n");
    printf("XA playback passed\n");
    return 0;
}
