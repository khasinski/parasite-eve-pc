/* Resident Evil 3 enemy sound banks (DATA/SOUND/E_xx.VH + .VB), PC, read as tones: what an
 * enemy's moves play (a step's sound number is a tone). The Python prototype is tools/vab3.py.
 *   VH   a 128-byte table of RE3's own, then a VAB header without its first 8 bytes ("pBAV",
 *        version): at 0x80 + 18 u16 programs, tones, samples; 16 bytes per program from 0x80 + 32,
 *        then 32 bytes per tone (VAB ToneAtr: +2 volume, +3 pan, +4 centre note, +5 fine, +6 min
 *        note, +7 max note, +22 s16 sample), then samples + 1 u16 running ends / 8 (sample k spans
 *        ends[k-1] .. ends[k], sample 0 unused).
 *   VB   the samples back to back, PlayStation ADPCM.
 * Tone t plays note 60 + t on its sample: rate 44100 * 2^((60 + t - centre - fine / 128) / 12).
 * Each tone comes out as a Sound at 22050 Hz (resampled from that rate, the tone's volume applied),
 * "sound/e_34/tone00". */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pe_imp_spu.h"
#include "pe_src_re3.h"

#define HDR 0x80
#define OUT_RATE 22050
#define TONES_MAX 128

static unsigned u16le(const uint8_t *p) { return p[0] | p[1] << 8; }

int re3_tone_count(const uint8_t *vh, uint32_t vh_size) {
    if (vh_size < HDR + 32) return -1;
    unsigned programs = u16le(vh + HDR + 18), tones = u16le(vh + HDR + 20), samples = u16le(vh + HDR + 22);
    uint64_t need = HDR + 32 + 16ull * programs + 32ull * tones + 2ull * (samples + 1);
    if (!tones || tones > TONES_MAX || !samples || samples > 255 || programs > 128 || need > vh_size) return -1;
    return (int)tones;
}

double re3_tone_rate(int note, int centre, int fine) {
    return 44100.0 * pow(2.0, (note - centre - fine / 128.0) / 12.0);
}

/* tone t as 22050 Hz PCM; NULL with a reason */
PeImpSound *re3_tone_sound(const uint8_t *vh, uint32_t vh_size, const uint8_t *vb, uint32_t vb_size, int t,
                           char *why, size_t n) {
    int tones = re3_tone_count(vh, vh_size);
    if (tones < 0 || t < 0 || t >= tones) { snprintf(why, n, "VH: no tone %d", t); return NULL; }
    unsigned programs = u16le(vh + HDR + 18), samples = u16le(vh + HDR + 22);
    const uint8_t *tone = vh + HDR + 32 + 16 * programs + 32 * t, *ends = vh + HDR + 32 + 16 * programs + 32 * tones;
    int sample = (int16_t)u16le(tone + 22);
    if (sample < 1 || (unsigned)sample > samples) { snprintf(why, n, "VH: tone %d plays sample %d", t, sample); return NULL; }
    uint32_t from = 8u * u16le(ends + 2 * (sample - 1)), to = 8u * u16le(ends + 2 * sample);
    if (to <= from || to > vb_size || (to - from) % PE_IMP_SPU_BLOCK) { snprintf(why, n, "VH: tone %d's sample outside the VB", t); return NULL; }
    uint32_t frames = (to - from) / PE_IMP_SPU_BLOCK * PE_IMP_SPU_FRAMES;
    int16_t *pcm = malloc(frames * sizeof *pcm);
    if (!pcm) { snprintf(why, n, "out of memory"); return NULL; }
    if (pe_imp_spu_decode(vb + from, to - from, pcm)) { free(pcm); snprintf(why, n, "VB: bad ADPCM in tone %d", t); return NULL; }
    double rate = re3_tone_rate(60 + t, tone[4], tone[5]);
    if (rate < 1000 || rate > 96000) { free(pcm); snprintf(why, n, "VH: tone %d plays at %.0f Hz", t, rate); return NULL; }
    double step = rate / OUT_RATE, gain = tone[2] / 127.0;
    uint32_t out = (uint32_t)(frames / step);
    if (!out) out = 1;
    PeImpSound *s = pe_imp_sound_alloc(OUT_RATE, out);
    if (!s) { free(pcm); snprintf(why, n, "tone %d is too long for a Sound", t); return NULL; }
    for (uint32_t i = 0; i < out; i++) {
        double p = i * step;
        uint32_t a = (uint32_t)p;
        double f = p - a, v = a + 1 < frames ? pcm[a] * (1 - f) + pcm[a + 1] * f : pcm[a < frames ? a : frames - 1];
        v *= gain;
        s->pcm[i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
    }
    free(pcm);
    return s;
}

static int read_tones(const uint8_t *vh, uint32_t vh_size, const uint8_t *vb, uint32_t vb_size,
                      const char *path, PeImpSink *sink, char *why, size_t n) {
    char bank[24];
    int tones = re3_tone_count(vh, vh_size);
    if (pe_imp_sound_bank_name(path, bank) || tones < 0) { snprintf(why, n, "not an RE3 enemy sound bank"); return -1; }
    for (int t = 0; t < tones; t++) {
        PeImpSound *s = re3_tone_sound(vh, vh_size, vb, vb_size, t, why, n);
        if (!s) return -1;
        char id[48], name[48], label[24];
        snprintf(label, sizeof label, "%s", bank);
        for (char *p = label; *p; p++) if (*p >= 'a' && *p <= 'z') *p -= 'a' - 'A';
        snprintf(id, sizeof id, "sound/%s/tone%02d", bank, t);
        snprintf(name, sizeof name, "%s tone %02d", label, t);
        if (pe_imp_emit(sink, PE_IMP_SOUND, id, name, s)) { snprintf(why, n, "could not catalog %s", id); return -1; }
    }
    return 0;
}

const PeImpReader g_re3_tones_pc = {
    .api = {PE_IMP_API_VERSION, sizeof(PeImpReader)},
    .game = "re3", .platform = "pc", .format = "vh-tones",
    .version = 1, .produces = PE_IMP_SOUND, .match = "DATA/SOUND/E_??.VH",
    .tags = "enemy sound", .companion = ".VB", .read_pair = read_tones
};
