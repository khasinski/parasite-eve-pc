#ifndef PE_FONTS_NAMES_H
#define PE_FONTS_NAMES_H

#include <stddef.h>

/* Speaker names in colour. A dialogue page's "Daniel:" goes yellow and Aya's "{name}:" red, at the
 * start of a page, a line or a section.
 * Plain data, no game state. */

/* FB F0 in a page tints what follows until FB 05. The game's text never uses it. */
#define PE_TEXT_SPEAKER_COLOR 0xF0

/* Copy the page starting at `in` (up to and including its FF or F9) into `out` with the colour
 * codes added. Returns the bytes written, or 0 to keep the page as it is: no name in it, it does
 * not fit, it has no end within `in_max`, or it uses extended glyphs (a language pack's text). */
size_t pe_fonts_color_names(const unsigned char *in, size_t in_max, unsigned char *out, size_t out_size);

#endif
