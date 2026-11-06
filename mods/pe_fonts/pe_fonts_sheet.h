#ifndef PE_FONTS_SHEET_H
#define PE_FONTS_SHEET_H

/* A font file's letters, turned into what the game's dialogue font page holds. Plain data, no game
 * state, so tests can check it directly.
 *
 * The picture is the game's own layout: 12 x 12 cells, 21 cells a row, codes in reading order
 * (0-9 + - = * % space A-Z & ! ? " ' . a-z : , /), 252 or 256 pixels wide and at least 48 tall.
 * Bright pixels are the letter, dark ones its shadow, see-through ones empty. A letter's advance
 * is measured from its pixels; one pure magenta pixel in its cell overrides that: the advance
 * becomes that pixel's column + 1 (and the pixel is not drawn). */

#define PE_FONT_CELL 12
#define PE_FONT_COLUMNS 21
#define PE_FONT_CODES 77                /* codes 0x00-0x4C are letters; the rest of the page stays */
#define PE_FONT_SPACE 0x0F
#define PE_FONT_SPACE_ADVANCE 12        /* the box takes 7 off it between words: 5 pixels */

typedef struct PeFontSheet {
    unsigned char cells[PE_FONT_CODES][PE_FONT_CELL * PE_FONT_CELL]; /* 0 empty, 1 stroke, 2 shadow */
    unsigned char advance[PE_FONT_CODES];                            /* dialogue box */
    unsigned char metric[PE_FONT_CODES];    /* menus: drawn width << 4 | first column; 0 keeps the game's */
} PeFontSheet;

/* From 32-bit pixels, R, G, B, A in byte order. Returns 0, or -1 for a picture of the wrong size
 * or one with no letters at all. */
int pe_font_sheet_read(const unsigned char *rgba, unsigned width, unsigned height, unsigned pitch,
                       PeFontSheet *out);

/* Write the letters into a 4-bit page (`stride` bytes a row); everything past them stays. */
void pe_font_sheet_write(const PeFontSheet *sheet, unsigned char *pixels, unsigned stride);

#endif
