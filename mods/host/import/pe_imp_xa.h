#ifndef PE_IMP_XA_H
#define PE_IMP_XA_H
/* CD-XA ADPCM streams (the PS1's streamed music and voice): decoding and playback as 44.1 kHz
 * stereo 16-bit PCM, pulled a block at a time. Plain C, no platform calls.
 * A sector's audio is 18 sound groups of 128 bytes: 16 header bytes (each sound unit's
 * filter << 4 | range), then 112 bytes of samples. 4-bit: 8 units of 28 samples per group,
 * 8-bit: 4. Stereo alternates units left, right. Coding byte (subheader): bit 0 stereo, bit 2
 * 18.9 kHz (else 37.8 kHz), bit 4 8-bit. */
#include <stddef.h>
#include <stdint.h>

#define PE_IMP_XA_GROUPS 2304u                    /* sound-group bytes per sector */
#define PE_IMP_XA_RATE 44100

/* One sector: interleaved frames (L, R; mono gives one value per frame) into out, which holds
 * 4032 values. `state` carries the two previous samples per channel across sectors. */
unsigned pe_imp_xa_decode(const uint8_t *groups, unsigned coding, int32_t state[2][2], int16_t *out);

typedef struct PeImpXaPlayer PeImpXaPlayer;
/* header: a Song's 16-byte XA header ("XAS1", coding, ...); audio: whole sectors of sound groups.
 * loops: how many times the stream plays (0: forever). The inputs are copied. */
PeImpXaPlayer *pe_imp_xa_open(const uint8_t *header, uint32_t header_size, const uint8_t *audio, uint32_t audio_size,
                              unsigned loops, char *why, size_t n);
/* Up to `frames` stereo frames at 44.1 kHz; fewer only at the end. 0: finished. */
uint32_t pe_imp_xa_render(PeImpXaPlayer *p, int16_t *out, uint32_t frames);
void pe_imp_xa_close(PeImpXaPlayer *p);

#endif
