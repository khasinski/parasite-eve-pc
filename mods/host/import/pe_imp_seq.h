#ifndef PE_IMP_SEQ_H
#define PE_IMP_SEQ_H
/* Plays a PS1 Sony sequence (SEQ, "pQES") on its instrument bank (VAB header "pBAV" and its
 * ADPCM samples) as 44.1 kHz stereo 16-bit PCM, pulled a block at a time. Plain C, no platform
 * calls: the caller owns the audio device (the asset service on PC, a test, an offline render).
 * Hostile input: every offset and count is bounded; a bad song is refused with a reason. */
#include <stddef.h>
#include <stdint.h>

#define PE_IMP_SEQ_RATE 44100
#define PE_IMP_SEQ_VOICES 24                    /* the PS1 SPU's */

typedef struct PeImpSeqPlayer PeImpSeqPlayer;

/* The bank is the VAB header (programs, tones, sample sizes) and the samples back to back, as a
 * Song stores them. `loops`: how many times the sequence's loop section plays before the song goes
 * on to its end (0: forever). The player copies what it needs: the inputs may go afterwards. */
PeImpSeqPlayer *pe_imp_seq_open(const uint8_t *seq, uint32_t seq_size, const uint8_t *vab, uint32_t vab_size,
                                const uint8_t *samples, uint32_t sample_size, unsigned loops,
                                char *why, size_t n);
/* Up to `frames` stereo frames (L, R interleaved); fewer only once the song has ended and its
 * last notes have died away. 0: finished. */
uint32_t pe_imp_seq_render(PeImpSeqPlayer *p, int16_t *out, uint32_t frames);
/* Notes playing now, and the sequence position in ticks (for tests and status) */
unsigned pe_imp_seq_voices(const PeImpSeqPlayer *p);
uint64_t pe_imp_seq_tick(const PeImpSeqPlayer *p);
void pe_imp_seq_close(PeImpSeqPlayer *p);

#endif
