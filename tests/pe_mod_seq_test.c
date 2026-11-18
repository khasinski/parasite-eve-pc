/* PS1 SEQ + VAB playback (mods/host/import/pe_imp_seq.c).
 *   made-up   a one-tone bank (a looping square wave) and short sequences: a half-second note
 *             plays for half a second plus its release, then the song ends; a note an octave up
 *             steps twice as fast (half the period); a loop section plays as many times as asked;
 *             truncated sequences and banks never read past their ends
 *   disc      with PE_TEST_RE2_DISC: RE2's MAIN00 scanned into a store, rendered for 20 seconds:
 *             sound, voices in use, the sequence moving. PE_TEST_SEQ_WAV=<path> also writes those
 *             seconds as a WAV file to listen to (it stays on this computer).
 *   RE1       with PE_TEST_RE1_DISC: the 94 sequences in SOUND/SEPxx.HSB scanned; every one plays
 *             (three seconds each); none hangs; most make sound. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pe_imp.h"
#include "pe_imp_internal.h"
#include "pe_imp_seq.h"
#include "pe_src_re2.h"
#include "pe_src_re1.h"

static uint8_t vab[32 + 128 * 16 + 16 * 32 + 512], smp[48];

static void put16(uint8_t *p, unsigned v) { p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8); }

/* one program, one tone: centre note 60, every key, instant attack, full sustain, fast release;
 * sample 1: three blocks, each +4096 then -4096 (shift 0: nibble x 4096), looping from block 1 */
static void bank(void) {
    memset(vab, 0, sizeof vab), memset(smp, 0, sizeof smp);
    memcpy(vab, "pBAV", 4);
    put16(vab + 18, 1), put16(vab + 20, 1), put16(vab + 22, 1);
    vab[24] = 127;
    uint8_t *prog = vab + 32;
    prog[0] = 1, prog[1] = 0, prog[4] = 64;            /* volume 0: taken as full */
    uint8_t *tone = vab + 32 + 128 * 16;
    tone[2] = 127, tone[3] = 64, tone[4] = 60, tone[6] = 0, tone[7] = 127, tone[12] = 2, tone[13] = 2;
    put16(tone + 16, 0x00ff);                          /* attack rate 0 (fastest), sustain level max */
    put16(tone + 18, 0x1fc0 | 0x0a);                   /* sustain holds, release rate 10 */
    put16(tone + 22, 1);
    put16(vab + 32 + 128 * 16 + 16 * 32 + 2, 48 / 8);  /* sample 1: 48 bytes */
    for (int b = 0; b < 3; b++) {
        uint8_t *k = smp + 16 * b;
        k[0] = 0x00;                                   /* predictor 0, shift 0 */
        k[1] = b == 1 ? 4 : b == 2 ? 3 : 0;            /* loop start at block 1, loop end + repeat at 2 */
        for (int j = 0; j < 14; j++) k[2 + j] = j < 7 ? 0x11 : 0xff;   /* 14 x +1, 14 x -1 */
    }
}

static uint8_t seq[256];
static uint32_t seq_n;
static void seq_begin(void) {
    static const uint8_t h[15] = {'p', 'Q', 'E', 'S', 0, 0, 0, 1, 0, 48, 0x07, 0xa1, 0x20, 4, 2};
    memcpy(seq, h, 15), seq_n = 15;
}
static void ev(const uint8_t *b, unsigned n) { memcpy(seq + seq_n, b, n), seq_n += n; }

static uint32_t render_all(PeImpSeqPlayer *p, uint32_t cap, int16_t **pcm) {
    *pcm = malloc((size_t)cap * 2 * sizeof **pcm);
    uint32_t total = 0, got;
    while (total < cap && (got = pe_imp_seq_render(p, *pcm + 2 * total, cap - total < 4096 ? cap - total : 4096)) > 0) total += got;
    return total;
}

static void made_up(void) {
    char why[200] = "";
    bank();
    /* a half-second note (48 ticks at 120 bpm), then the end */
    seq_begin();
    ev((const uint8_t[]){0x00, 0xc0, 0x00, 0x00, 0x90, 60, 127, 0x30, 0x80, 60, 0, 0x00, 0xff, 0x2f, 0x00}, 15);
    PeImpSeqPlayer *p = pe_imp_seq_open(seq, seq_n, vab, sizeof vab, smp, sizeof smp, 0, why, sizeof why);
    if (!p) printf("refused: %s\n", why);
    assert(p);
    int16_t *pcm;
    uint32_t n = render_all(p, 5 * PE_IMP_SEQ_RATE, &pcm);
    printf("half-second note: %u frames until silence (%.2f s)\n", n, n / (double)PE_IMP_SEQ_RATE);
    assert(n > PE_IMP_SEQ_RATE / 2 && n < PE_IMP_SEQ_RATE);       /* the note, then a short release */
    assert(pe_imp_seq_voices(p) == 0 && pe_imp_seq_tick(p) == 48);
    /* the square wave: 14 frames up, 14 down at the centre note; centred pan gives both sides */
    int peak = 0;
    for (uint32_t i = 1000; i < 2000; i++) peak = abs(pcm[2 * i]) > peak ? abs(pcm[2 * i]) : peak;
    assert(peak > 1500 && pcm[2 * 1000] == pcm[2 * 1000 + 1]);    /* 4096 at half gain */
    int flips = 0;
    for (uint32_t i = 1001; i < 1001 + 280; i++) flips += (pcm[2 * i] > 0) != (pcm[2 * i - 2] > 0);
    assert(flips >= 19 && flips <= 21);                            /* 28-frame period: 20 flips */
    free(pcm);
    pe_imp_seq_close(p);

    /* an octave up: twice the steps, so twice the flips */
    seq[15 + 5] = 72;
    p = pe_imp_seq_open(seq, seq_n, vab, sizeof vab, smp, sizeof smp, 0, why, sizeof why);
    render_all(p, PE_IMP_SEQ_RATE, &pcm);
    flips = 0;
    for (uint32_t i = 1001; i < 1001 + 280; i++) flips += (pcm[2 * i] > 0) != (pcm[2 * i - 2] > 0);
    assert(flips >= 39 && flips <= 41);
    free(pcm);
    pe_imp_seq_close(p);

    /* a loop section of 48 ticks (marked by controller 99 = 20 ... 30, count 127: forever) */
    seq_begin();
    ev((const uint8_t[]){0x00, 0xb0, 99, 20, 0x00, 0xb0, 6, 127, 0x00, 0x90, 60, 100, 0x30, 0x80, 60, 0,
                         0x00, 0xb0, 99, 30, 0x00, 0xff, 0x2f, 0x00}, 24);
    for (unsigned loops = 1; loops <= 3; loops++) {
        p = pe_imp_seq_open(seq, seq_n, vab, sizeof vab, smp, sizeof smp, loops, why, sizeof why);
        uint32_t frames = render_all(p, 10 * PE_IMP_SEQ_RATE, &pcm);
        printf("loop played %u time(s): %llu ticks, %.2f s\n", loops, (unsigned long long)pe_imp_seq_tick(p), frames / (double)PE_IMP_SEQ_RATE);
        assert(pe_imp_seq_tick(p) == 48ull * loops);
        free(pcm);
        pe_imp_seq_close(p);
    }
    p = pe_imp_seq_open(seq, seq_n, vab, sizeof vab, smp, sizeof smp, 0, why, sizeof why);   /* forever */
    uint32_t frames = render_all(p, 10 * PE_IMP_SEQ_RATE, &pcm);
    assert(frames == 10 * PE_IMP_SEQ_RATE && pe_imp_seq_tick(p) >= 48 * 19);
    free(pcm);
    pe_imp_seq_close(p);

    /* an empty loop (no time between the marks) can't hang: the song plays on and ends */
    seq_begin();
    ev((const uint8_t[]){0x00, 0xb0, 99, 20, 0x00, 0xb0, 99, 30, 0x00, 0xff, 0x2f, 0x00}, 12);
    p = pe_imp_seq_open(seq, seq_n, vab, sizeof vab, smp, sizeof smp, 0, why, sizeof why);
    assert(p && render_all(p, PE_IMP_SEQ_RATE, &pcm) < PE_IMP_SEQ_RATE);
    free(pcm);
    pe_imp_seq_close(p);

    /* truncated sequences end (or are refused); truncated banks are refused */
    seq_begin();
    ev((const uint8_t[]){0x00, 0xc0, 0x00, 0x00, 0x90, 60, 127, 0x30, 0x80, 60, 0, 0x00, 0xff, 0x2f, 0x00}, 15);
    for (uint32_t cut = 0; cut < seq_n; cut++) {
        uint8_t *copy = malloc(cut ? cut : 1);
        memcpy(copy, seq, cut);
        p = pe_imp_seq_open(copy, cut, vab, sizeof vab, smp, sizeof smp, 0, why, sizeof why);
        if (p) { render_all(p, 2 * PE_IMP_SEQ_RATE, &pcm); free(pcm); pe_imp_seq_close(p); }
        free(copy);
    }
    for (uint32_t cut = 0; cut < sizeof vab; cut += 97)
        assert(!pe_imp_seq_open(seq, seq_n, vab, cut, smp, sizeof smp, 0, why, sizeof why));
    assert(!pe_imp_seq_open(seq, seq_n, vab, sizeof vab, smp, 32, 0, why, sizeof why));
    printf("made-up songs: timing, pitch, loops, truncation\n");
}

static void write_wav(const char *path, const int16_t *pcm, uint32_t frames) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    uint32_t data = frames * 4, rate = PE_IMP_SEQ_RATE, byterate = rate * 4, size = 36 + data, fmt = 16;
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
    assert(!pe_imp_add_reader(&g_re2_music_ps1));
    assert(!pe_imp_scan("pe-seq-test/store", cue, NULL, &sc));
    PeImpSong *song;
    assert(!pe_imp_catalog_song("pe-seq-test/store", "re2:music/main00", &song, why, sizeof why));
    const uint8_t *vh = song->payload + song->seq_size[0] + song->seq_size[1];
    PeImpSeqPlayer *p = pe_imp_seq_open(song->payload, song->seq_size[0], vh, song->instrument_size,
                                        vh + song->instrument_size, song->sample_size, 0, why, sizeof why);
    if (!p) printf("refused: %s\n", why);
    assert(p);
    int16_t *pcm;
    uint32_t frames = render_all(p, 20 * PE_IMP_SEQ_RATE, &pcm), clipped = 0;
    double sum = 0;
    unsigned most = 0;
    for (uint32_t i = 0; i < frames; i++) {
        for (int k = 0; k < 2; k++) {
            int s = pcm[2 * i + (uint32_t)k];
            sum += (double)s * s;
            clipped += s == 32767 || s == -32768;
        }
    }
    most = pe_imp_seq_voices(p);
    double rms = frames ? sqrt(sum / (2.0 * frames)) : 0;
    printf("RE2 MAIN00: %u frames, rms %.0f, %u clipped samples, %u voices at the end, tick %llu\n", frames, rms, clipped,
           most, (unsigned long long)pe_imp_seq_tick(p));
    assert(frames == 20 * PE_IMP_SEQ_RATE && rms > 200 && clipped < frames / 100 && pe_imp_seq_tick(p) > 48);
    const char *wav = getenv("PE_TEST_SEQ_WAV");
    if (wav && *wav) write_wav(wav, pcm, frames), printf("wrote %s\n", wav);
    free(pcm);
    pe_imp_seq_close(p);
    free(song);
}

static void re1(const char *cue) {
    char why[300] = "";
    PeImpScan sc;
    pe_imp_registry_reset();
    assert(!pe_imp_add_reader(&g_re1_music_ps1));
    assert(!pe_imp_scan("pe-seq-test/store", cue, NULL, &sc));
    if (sc.refused) printf("  refused: %s\n", sc.last_error);
    PeImpAsset *a;
    int n = pe_imp_catalog_assets("pe-seq-test/store", "re1", &a), songs = 0, loud = 0;
    for (int i = 0; i < n; i++) {
        if (strcmp(a[i].type, "Song")) continue;
        songs++;
        PeImpSong *song;
        assert(!pe_imp_catalog_song("pe-seq-test/store", a[i].id, &song, why, sizeof why));
        const uint8_t *vh = song->payload + song->seq_size[0];
        PeImpSeqPlayer *p = pe_imp_seq_open(song->payload, song->seq_size[0], vh, song->instrument_size,
                                            vh + song->instrument_size, song->sample_size, 1, why, sizeof why);
        if (!p) fprintf(stderr, "  %s refused: %s\n", a[i].id, why);
        assert(p);
        int16_t *pcm;
        uint32_t frames = render_all(p, 3 * PE_IMP_SEQ_RATE, &pcm);
        double sum = 0;
        for (uint32_t k = 0; k < 2 * frames; k++) sum += (double)pcm[k] * pcm[k];
        loud += frames && sqrt(sum / (2.0 * frames)) > 100;
        const char *wav = getenv("PE_TEST_SEQ_WAV_RE1");
        if (wav && *wav && strstr(a[i].id, "sep06_0")) {          /* a long track, to listen to */
            pe_imp_seq_close(p);
            p = pe_imp_seq_open(song->payload, song->seq_size[0], vh, song->instrument_size,
                                vh + song->instrument_size, song->sample_size, 1, why, sizeof why);
            free(pcm);
            uint32_t got = render_all(p, 30 * PE_IMP_SEQ_RATE, &pcm);
            write_wav(wav, pcm, got);
        }
        if (strstr(a[i].id, "sep28_0") || strstr(a[i].id, "sep30_0"))
            printf("  %s: %.1f s rendered, tick %llu\n", a[i].id, frames / (double)PE_IMP_SEQ_RATE, (unsigned long long)pe_imp_seq_tick(p));
        free(pcm);
        pe_imp_seq_close(p);
        free(song);
    }
    free(a);
    printf("RE1: %d songs from %d files read, %d with sound in their first 3 seconds\n", songs, sc.files_read, loud);
    assert(songs == 94 && !sc.refused && loud > songs / 2);
}

int main(void) {
    made_up();
    const char *r1 = getenv("PE_TEST_RE1_DISC");
    if (r1 && *r1) re1(r1);
    const char *cue = getenv("PE_TEST_RE2_DISC");
    if (cue && *cue) disc(cue);
    else printf("PE_TEST_RE2_DISC not set: disc part skipped\n");
    printf("sequence playback passed\n");
    return 0;
}
