/* pe_fonts: reading a font picture, and colouring speaker names in a page. */
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "pe_fonts_names.h"
#include "pe_fonts_sheet.h"

enum { W = 252, H = 48 };
static unsigned char s_rgba[H * W * 4];

static void put(unsigned code, unsigned x, unsigned y, unsigned char r, unsigned char g, unsigned char b) {
    unsigned char *p = s_rgba + (code / PE_FONT_COLUMNS * PE_FONT_CELL + y) * W * 4 +
                       (code % PE_FONT_COLUMNS * PE_FONT_CELL + x) * 4;
    p[0] = r, p[1] = g, p[2] = b, p[3] = 255;
}

static unsigned page_get(const unsigned char *p, unsigned stride, unsigned x, unsigned y) {
    unsigned char b = p[y * stride + x / 2];
    return x & 1 ? b >> 4 : b & 15;
}

static void test_sheet(void) {
    static PeFontSheet sheet;
    static unsigned char page[128 * 256];
    /* 'A': stroke at columns 1-4, shadow at 5. */
    for (unsigned x = 1; x < 5; ++x) put(0x10, x, 2, 255, 255, 255);
    put(0x10, 5, 3, 0, 0, 0);
    /* 'B': one stroke, its advance set by a marker at column 8. */
    put(0x11, 0, 0, 200, 200, 200);
    put(0x11, 8, 11, 255, 0, 255);
    assert(pe_font_sheet_read(s_rgba, W, H, W * 4, &sheet) == 0);
    assert(sheet.advance[0x10] == 6);
    assert(sheet.metric[0x10] == (5 << 4 | 1));
    assert(sheet.cells[0x10][2 * 12 + 1] == 1 && sheet.cells[0x10][3 * 12 + 5] == 2);
    assert(sheet.advance[0x11] == 9);
    assert(sheet.metric[0x11] == (1 << 4 | 0));                  /* the marker is not ink */
    assert(sheet.cells[0x11][11 * 12 + 8] == 0);
    assert(sheet.advance[PE_FONT_SPACE] == PE_FONT_SPACE_ADVANCE);
    assert(sheet.advance[0x12] == 0 && sheet.metric[0x12] == 0);  /* no letter: the game's stays */

    memset(page, 0xEE, sizeof page);
    pe_font_sheet_write(&sheet, page, 128);
    assert(page_get(page, 128, (0x10 % 21) * 12 + 1, (0x10 / 21) * 12 + 2) == 1);
    assert(page_get(page, 128, (0x10 % 21) * 12 + 0, (0x10 / 21) * 12 + 0) == 0);
    assert(page_get(page, 128, 0, 200) == 0xE);                  /* past the letters: untouched */
    assert(page_get(page, 128, 12 * (77 % 21), 12 * (77 / 21)) == 0xE);

    assert(pe_font_sheet_read(s_rgba, 100, H, 400, &sheet) == -1);
    assert(pe_font_sheet_read(s_rgba, W, 12, W * 4, &sheet) == -1);
    memset(s_rgba, 0, sizeof s_rgba);
    assert(pe_font_sheet_read(s_rgba, W, H, W * 4, &sheet) == -1);
}

/* "Eve" etc. in the game's codes. */
#define E 0x14
#define v 0x45
#define e 0x34
#define COLON 0x4A
#define SP 0x0F
static const unsigned char kEve[] = {E, v, e};

static void test_names(void) {
    unsigned char out[64];
    size_t n;

    static const unsigned char plain[] = {E, v, e, SP, e, 0xFF};  /* no colon: not a speaker */
    assert(pe_fonts_color_names(plain, sizeof plain, out, sizeof out) == 0);

    static const unsigned char named[] = {E, v, e, COLON, SP, e, 0xF7, SP, e, 0xFF, 0xFE};
    n = pe_fonts_color_names(named, sizeof named, out, sizeof out);
    assert(n == 14);
    assert(out[0] == 0xFB && out[1] == PE_TEXT_SPEAKER_COLOR);
    assert(!memcmp(out + 2, kEve, 3) && out[5] == COLON);
    assert(out[6] == 0xFB && out[7] == 5);
    assert(out[n - 1] == 0xFF);

    static const unsigned char aya[] = {0xFA, COLON, SP, e, 0xFF};
    n = pe_fonts_color_names(aya, sizeof aya, out, sizeof out);
    assert(n == 9 && out[0] == 0xFB && out[1] == 4 && out[2] == 0xFA && out[3] == COLON);
    assert(out[4] == 0xFB && out[5] == 5);

    /* A second speaker on a later line, and after a wait; spaces before the name are fine. */
    static const unsigned char two[] = {e, 0xF7, SP, E, v, e, COLON, 0xF8, 0xFA, COLON, 0xF9};
    n = pe_fonts_color_names(two, sizeof two, out, sizeof out);
    assert(n == 19 && out[n - 1] == 0xF9);
    assert(out[3] == 0xFB && out[4] == PE_TEXT_SPEAKER_COLOR);

    /* Mid-line "Eve:" is someone being talked about, not a speaker. */
    static const unsigned char mid[] = {e, SP, E, v, e, COLON, 0xFF};
    assert(pe_fonts_color_names(mid, sizeof mid, out, sizeof out) == 0);

    /* A control code's operand is not a terminator. */
    static const unsigned char operand[] = {E, v, e, COLON, 0xFB, 7, 0xFF, e, 0xFF};
    assert(pe_fonts_color_names(operand, sizeof operand, out, sizeof out) == 13);

    /* Too small, no end, a pack's extended glyphs: the page stays as it is. */
    assert(pe_fonts_color_names(named, sizeof named, out, 8) == 0);
    assert(pe_fonts_color_names(named, 5, out, sizeof out) == 0);
    static const unsigned char pack[] = {E, v, e, COLON, 0xF1, 1, 2, 0xFF};
    assert(pe_fonts_color_names(pack, sizeof pack, out, sizeof out) == 0);
}

int main(void) {
    test_sheet();
    test_names();
    return 0;
}
