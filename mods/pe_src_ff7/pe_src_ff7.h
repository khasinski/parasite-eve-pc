#ifndef PE_SRC_FF7_H
#define PE_SRC_FF7_H
#include "pe_imp.h"

/* FF7's LZS: *out is malloc'd. 0, or -1 when the data isn't LZS (or decompresses past 2 MiB). */
int ff7_lzs(const uint8_t *data, uint32_t size, uint8_t **out, uint32_t *out_size);
extern const PeImpReader g_ff7_instruments_ps1, g_ff7_field_music_ps1, g_ff7_cue_music_ps1;

#endif
