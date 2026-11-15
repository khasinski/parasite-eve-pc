#ifndef PE_LANG_CYRILLIC_H
#define PE_LANG_CYRILLIC_H
#include "pe_lang_cyrillic_data.h"
/* Authored Cyrillic cells only; Latin aliases require no replacement pixels. */
void pe_lang_cyrillic_compose(const PeCyrillicGlyph *g, unsigned char out[144]);
int pe_lang_cyrillic_fill(unsigned char *pixels, unsigned stride, unsigned height);
#endif
