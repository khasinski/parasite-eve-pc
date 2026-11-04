#ifndef PE_LANG_FONT_H
#define PE_LANG_FONT_H

/* The Latin Extended glyphs (é, ñ, ß, « ...), made from the game's own letters when the dialogue
 * font goes to VRAM: localization/petext/latin.py defines them, pe_lang_latin.h is its table. */

#include "pe_lang_latin.h"

#define PE_FONT_CELL 12
#define PE_FONT_COLUMNS 21

/* One glyph's 144 pixels (0 empty, 1 stroke, 2 shadow) from its base letter's cell (NULL for a
 * drawn sign); the same steps as latin.compose. */
void pe_lang_font_compose(const PeLatinGlyph *glyph, const unsigned char *base, unsigned char *out);

/* Write every Latin glyph into a 4-bit font page (`stride` bytes a row, 21 cells a row), each in
 * the cell of its code and only where that cell is empty, reading base letters from the same
 * page. Returns how many were written; nothing else in the page changes. */
int pe_lang_font_fill(unsigned char *pixels, unsigned stride, unsigned height);

/* The dialogue box's advance of a Latin code (the base letter's from `base_widths`, or for a drawn
 * sign its ink width + 2), and its menu metrics byte (the base letter's, or the width in the
 * high nibble for a drawn sign). */
unsigned pe_lang_font_advance(const PeLatinGlyph *glyph, const unsigned char *base_advance);
unsigned char pe_lang_font_metric(const PeLatinGlyph *glyph, const unsigned char *base_metrics);

#endif
