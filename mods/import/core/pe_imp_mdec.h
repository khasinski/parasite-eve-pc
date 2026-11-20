#ifndef PE_IMP_MDEC_H
#define PE_IMP_MDEC_H
/* Decodes one PS1 MDEC frame (the bitstream STR movies and Resident Evil's room backgrounds use:
 * u16 code count, 0x3800, u16 quantiser, u16 version 2 or 3, then the VLC bitstream) into 24-bit
 * RGB, w x h pixels (multiples of 16), macroblocks column by column as the PS1 lays them out.
 * Thread-safe: the bitstream state is local; the lookup table is the game's own (LIBPRESS's, built
 * by MdecDiag_ExpandDecoderTable), handed in once with pe_imp_mdec_set_table and built on first
 * use. Hostile input is bounded. */
#include <stddef.h>
#include <stdint.h>

/* The function that builds the lookup table (the game's MdecDiag_ExpandDecoderTable). Until it is
 * set, decoding refuses. */
void pe_imp_mdec_set_table(void (*build)(void *table));
/* rgb: w * h * 3 bytes. 0, or -1 with a reason. */
int pe_imp_mdec_decode(const uint8_t *frame, uint32_t size, unsigned w, unsigned h, uint8_t *rgb, char *why, size_t n);

#endif
