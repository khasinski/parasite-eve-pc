/* AKAO playback (mods/import/core/pe_imp_akao.c) and FF7's music readers (pe_src_ff7).
 *   made-up   a one-instrument bank (a looping square wave, pitch 0x1000 at octave 6) and short
 *             sequences: a quarter note at 120 bpm lasts half a second plus its release; an octave
 *             up steps twice as fast; an endless loop plays as many times as asked
 *   disc      with PE_TEST_FF7_DISC (FF7 CD1 .cue): the instrument bank and the songs reach the
 *             catalog; every song plays (three seconds each) with almost no unreadable commands;
 *             most make sound. PE_TEST_AKAO_WAV=<path> writes 30 s of the longest song. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pe_imp.h"
#include "pe_imp_internal.h"
#include "pe_imp_akao.h"
#include "pe_src_ff7.h"

static uint8_t table[128 * 64], samples[48], seq[128];
static uint32_t seq_n;

static void put32(uint8_t *p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> 8 * i); }

static void bank(void) {
    memset(table, 0, sizeof table), memset(samples, 0, sizeof samples);
    uint8_t *r = table;                           /* instrument 0 */
    put32(r, 0x1000), put32(r + 4, 0x1010);       /* sample at the bank's start, loops from block 1 */
    r[8] = 0, r[9] = 0, r[10] = 15, r[11] = 0x7f, r[12] = 10, r[13] = 1, r[14] = 3, r[15] = 3;
    for (int k = 0; k < 12; k++) put32(r + 16 + 4 * k, 0x1000);
    for (int b = 0; b < 3; b++) {
        uint8_t *blk = samples + 16 * b;
        blk[1] = b == 2 ? 3 : b == 1 ? 4 : 0;
        for (int j = 0; j < 14; j++) blk[2 + j] = j < 7 ? 0x11 : 0xff;
    }
}

static void begin(void) {
    memset(seq, 0, sizeof seq);
    memcpy(seq, "AKAO", 4);
    put32(seq + 16, 1);                           /* channel 0 */
    seq[20] = 0, seq[21] = 0;                     /* its commands: right after the offset field */
    seq_n = 22;
}
static void cmd(const uint8_t *b, unsigned n) { memcpy(seq + seq_n, b, n), seq_n += n; }
static void finish(void) { seq[6] = (uint8_t)(seq_n - 16), seq[7] = (uint8_t)((seq_n - 16) >> 8); }

static uint32_t render_all(PeImpAkaoPlayer *p, uint32_t cap, int16_t **pcm) {
    *pcm = malloc((size_t)cap * 2 * sizeof **pcm);
    uint32_t total = 0, got;
    while (total < cap && (got = pe_imp_akao_render(p, *pcm + 2 * total, cap - total < 4096 ? cap - total : 4096)) > 0) total += got;
    return total;
}

static int flips(const int16_t *pcm, uint32_t from) {
    int n = 0;
    for (uint32_t i = from + 1; i < from + 281; i++) n += (pcm[2 * i] > 0) != (pcm[2 * i - 2] > 0);
    return n;
}

static void made_up(void) {
    char why[200];
    int16_t *pcm;
    bank();
    /* tempo 25800 (120 bpm), instrument 0, octave 6, a quarter note (key 0, length 2: 48 ticks), end */
    begin();
    cmd((const uint8_t[]){0xe8, 0xc8, 0x64, 0xa1, 0x00, 0xa5, 0x06, 0x02, 0xa0}, 9);
    finish();
    PeImpAkaoPlayer *p = pe_imp_akao_open(seq, seq_n, table, sizeof table, samples, sizeof samples, 0x1000, 0, why, sizeof why);
    if (!p) printf("refused: %s\n", why);
    assert(p);
    uint32_t n = render_all(p, 5 * PE_IMP_AKAO_RATE, &pcm);
    printf("a quarter note at 120 bpm: %u frames (%.2f s) until silence\n", n, n / (double)PE_IMP_AKAO_RATE);
    assert(n > PE_IMP_AKAO_RATE / 2 - 2000 && n < PE_IMP_AKAO_RATE);
    int f = flips(pcm, 1000);
    assert(f >= 19 && f <= 21 && pcm[2000] == pcm[2001] && abs(pcm[2000]) > 1000);
    assert(!pe_imp_akao_errors(p) && !pe_imp_akao_running(p));
    free(pcm), pe_imp_akao_close(p);
    /* an octave up: twice the steps */
    seq[22 + 6] = 0x07;
    p = pe_imp_akao_open(seq, seq_n, table, sizeof table, samples, sizeof samples, 0x1000, 0, why, sizeof why);
    render_all(p, PE_IMP_AKAO_RATE, &pcm);
    f = flips(pcm, 1000);
    assert(f >= 39 && f <= 41);
    free(pcm), pe_imp_akao_close(p);
    /* an endless loop of one quarter note: plays as many times as asked, or forever */
    begin();
    cmd((const uint8_t[]){0xe8, 0xc8, 0x64, 0xa1, 0x00, 0xa5, 0x06, 0xc8, 0x02, 0xca}, 10);
    finish();
    for (unsigned loops = 1; loops <= 3; loops++) {
        p = pe_imp_akao_open(seq, seq_n, table, sizeof table, samples, sizeof samples, 0x1000, loops, why, sizeof why);
        n = render_all(p, 10 * PE_IMP_AKAO_RATE, &pcm);
        printf("loop played %u time(s): %.2f s\n", loops, n / (double)PE_IMP_AKAO_RATE);
        assert(n > loops * (PE_IMP_AKAO_RATE / 2) - 3000 && n < (loops + 1) * (PE_IMP_AKAO_RATE / 2) + 3000);
        free(pcm), pe_imp_akao_close(p);
    }
    p = pe_imp_akao_open(seq, seq_n, table, sizeof table, samples, sizeof samples, 0x1000, 0, why, sizeof why);
    assert(render_all(p, 10 * PE_IMP_AKAO_RATE, &pcm) == 10 * PE_IMP_AKAO_RATE);
    free(pcm), pe_imp_akao_close(p);
    /* truncated sequences end or are refused, never read past */
    for (uint32_t cut = 0; cut < seq_n; cut++) {
        uint8_t *copy = malloc(cut ? cut : 1);
        memcpy(copy, seq, cut);
        p = pe_imp_akao_open(copy, cut, table, sizeof table, samples, sizeof samples, 0x1000, 1, why, sizeof why);
        if (p) { render_all(p, PE_IMP_AKAO_RATE, &pcm); free(pcm); pe_imp_akao_close(p); }
        free(copy);
    }
    printf("made-up AKAO: timing, pitch, loops, truncation\n");
}

static void write_wav(const char *path, const int16_t *pcm, uint32_t frames) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    uint32_t data = frames * 4, rate = PE_IMP_AKAO_RATE, byterate = rate * 4, size = 36 + data, fmt = 16;
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
    assert(!pe_imp_add_reader(&g_ff7_instruments_ps1) && !pe_imp_add_reader(&g_ff7_field_music_ps1) &&
           !pe_imp_add_reader(&g_ff7_cue_music_ps1));
    assert(!pe_imp_scan("pe-akao-test/store", cue, NULL, &sc));
    if (sc.refused) fprintf(stderr, "  refused: %s\n", sc.last_error);
    PeImpSong *bank_song;
    assert(!pe_imp_catalog_song("pe-akao-test/store", "ff7:bank/instr", &bank_song, why, sizeof why));
    assert(bank_song->format == PE_IMP_SONG_AKAO_BANK);
    const uint8_t *bh = bank_song->payload;
    uint32_t base = (uint32_t)bh[4] | (uint32_t)bh[5] << 8 | (uint32_t)bh[6] << 16 | (uint32_t)bh[7] << 24;
    PeImpAsset *a;
    int n = pe_imp_catalog_assets("pe-akao-test/store", "ff7", &a), songs = 0, loud = 0, longest = -1;
    unsigned errors = 0, worst = 0;
    uint32_t longest_size = 0;
    for (int i = 0; i < n; i++) {
        if (strcmp(a[i].type, "Song") || strstr(a[i].id, ":bank/")) continue;
        songs++;
        PeImpSong *song;
        assert(!pe_imp_catalog_song("pe-akao-test/store", a[i].id, &song, why, sizeof why));
        assert(song->format == PE_IMP_SONG_AKAO);
        PeImpAkaoPlayer *p = pe_imp_akao_open(song->payload + 80, song->seq_size[0] - 80, bh + 16, 128 * 64,
                                              bh + 16 + 128 * 64, bank_song->sample_size, base, 1, why, sizeof why);
        if (!p) fprintf(stderr, "  %s refused: %s\n", a[i].id, why);
        assert(p);
        int16_t *pcm;
        uint32_t frames = render_all(p, 3 * PE_IMP_AKAO_RATE, &pcm);
        double sum = 0;
        for (uint32_t k = 0; k < 2 * frames; k++) sum += (double)pcm[k] * pcm[k];
        loud += frames && sqrt(sum / (2.0 * frames)) > 100;
        unsigned e = pe_imp_akao_errors(p);
        errors += e, worst = e > worst ? e : worst;
        if (song->seq_size[0] > longest_size) longest_size = song->seq_size[0], longest = i;
        free(pcm), pe_imp_akao_close(p), free(song);
    }
    const char *wav = getenv("PE_TEST_AKAO_WAV");
    if (wav && *wav && longest >= 0) {
        PeImpSong *song;
        assert(!pe_imp_catalog_song("pe-akao-test/store", a[longest].id, &song, why, sizeof why));
        PeImpAkaoPlayer *p = pe_imp_akao_open(song->payload + 80, song->seq_size[0] - 80, bh + 16, 128 * 64,
                                              bh + 16 + 128 * 64, bank_song->sample_size, base, 1, why, sizeof why);
        int16_t *pcm;
        uint32_t got = render_all(p, 30 * PE_IMP_AKAO_RATE, &pcm);
        write_wav(wav, pcm, got);
        printf("wrote %s (%s)\n", wav, a[longest].id);
        free(pcm), pe_imp_akao_close(p), free(song);
    }
    free(a), free(bank_song);
    printf("FF7 CD1: %d songs from %d files, %d with sound in 3 s, %u unreadable commands (worst song %u)\n", songs,
           sc.files_read, loud, errors, worst);
    assert(songs >= 40 && loud > songs * 3 / 4 && worst < 8 && !sc.refused);
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    made_up();
    const char *cue = getenv("PE_TEST_FF7_DISC");
    if (cue && *cue) disc(cue);
    else printf("PE_TEST_FF7_DISC not set: disc part skipped\n");
    printf("AKAO playback passed\n");
    return 0;
}
