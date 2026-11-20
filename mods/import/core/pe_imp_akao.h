#ifndef PE_IMP_AKAO_H
#define PE_IMP_AKAO_H
/* Plays a first-generation AKAO sequence (Square's PS1 driver, as Final Fantasy VII has it) on
 * its instrument bank, as 44.1 kHz stereo 16-bit PCM pulled a block at a time. Plain C, no
 * platform calls. Hostile input is bounded; a bad song is refused or ends where it breaks.
 *   sequence  "AKAO", u16 id, u16 length (after the 16-byte header), u16 reverb, 6 bytes time;
 *             u32 channel mask (bits 0-23), then per channel used a u16 offset from the end of that
 *             offset field to the channel's commands
 *   bank      the instrument table (128 records of 64 bytes: u32 sample address, u32 loop address
 *             (SPU addresses), attack, decay, sustain level, sustain rate, release, attack mode,
 *             sustain mode, release mode, 12 u32 pitches for the octave-6 semitones) and the
 *             samples (SPU ADPCM) with the SPU address of their first byte */
#include <stddef.h>
#include <stdint.h>

#define PE_IMP_AKAO_RATE 44100

typedef struct PeImpAkaoPlayer PeImpAkaoPlayer;
/* loops: how many times the song's endless loop (a jump back) plays before it stops (0: forever).
 * The inputs are copied. */
PeImpAkaoPlayer *pe_imp_akao_open(const uint8_t *seq, uint32_t seq_size, const uint8_t *table, uint32_t table_size,
                                  const uint8_t *samples, uint32_t sample_size, uint32_t sample_base, unsigned loops,
                                  char *why, size_t n);
/* Up to `frames` stereo frames; fewer only once every channel has finished and died away. */
uint32_t pe_imp_akao_render(PeImpAkaoPlayer *p, int16_t *out, uint32_t frames);
/* channels still running, and how many commands they couldn't read (for tests) */
unsigned pe_imp_akao_running(const PeImpAkaoPlayer *p);
unsigned pe_imp_akao_errors(const PeImpAkaoPlayer *p);
void pe_imp_akao_close(PeImpAkaoPlayer *p);

#endif
