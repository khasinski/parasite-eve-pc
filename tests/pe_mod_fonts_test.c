/* pe_fonts: reading a font picture, writing it into the font page and putting the game's own
 * letters back, and the pictures shipped in mods/fonts/ (the folder is the first argument). */
#include <SDL3/SDL.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    assert(page_get(page, 128, (0x12 % 21) * 12 + 3, (0x12 / 21) * 12 + 3) == 0xE); /* empty: the page's */
    assert(page_get(page, 128, 12 * (77 % 21), 12 * (77 / 21)) == 0xE);

    assert(pe_font_sheet_read(s_rgba, 100, H, 400, &sheet) == -1);
    assert(pe_font_sheet_read(s_rgba, W, 12, W * 4, &sheet) == -1);
    memset(s_rgba, 0, sizeof s_rgba);
    assert(pe_font_sheet_read(s_rgba, W, H, W * 4, &sheet) == -1);
}

/* Original after a font: the page's letter cells come back exactly, whatever their pixel values,
 * and nothing past them changes on the way. */
static void test_round_trip(const PeFontSheet *font) {
    static unsigned char page[128 * 256], before[128 * 256];
    static PeFontSheet game;
    srand(7);
    for (unsigned i = 0; i < sizeof page; ++i) page[i] = (unsigned char)rand();
    memcpy(before, page, sizeof page);
    pe_font_sheet_take(page, 128, &game);
    pe_font_sheet_write(font, page, 128);
    assert(memcmp(page, before, sizeof page) != 0);
    const unsigned past = (PE_FONT_CODES / PE_FONT_COLUMNS + 1) * PE_FONT_CELL * 128;
    assert(!memcmp(page + past, before + past, sizeof page - past));   /* rows below the letters */
    pe_font_sheet_write(&game, page, 128);
    assert(!memcmp(page, before, sizeof page));
}

/* The game's letter codes; '=' and '*' (0C, 0D) are cells the US font leaves empty. */
static int is_letter(unsigned code) { return code != 0x0C && code != 0x0D && code != PE_FONT_SPACE; }

static void test_shipped(const char *dir) {
    static PeFontSheet sheet;
    char path[4096];
    int count = 0;
    char **files = SDL_GlobDirectory(dir, "*.png", SDL_GLOB_CASEINSENSITIVE, &count);
    int shipped = 0;
    assert(files && count > 0);
    for (int i = 0; i < count; ++i) {
        snprintf(path, sizeof path, "%s/%s", dir, files[i]);
        SDL_Surface *loaded = SDL_LoadPNG(path);
        SDL_Surface *image = loaded ? SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32) : NULL;
        if (!image) fprintf(stderr, "%s: %s\n", path, SDL_GetError());
        assert(image);
        assert(pe_font_sheet_read(image->pixels, (unsigned)image->w, (unsigned)image->h,
                                  (unsigned)image->pitch, &sheet) == 0);
        for (unsigned code = 0; code < PE_FONT_CODES; ++code) {
            if (!is_letter(code)) continue;
            if (!sheet.advance[code] || !sheet.metric[code])
                fprintf(stderr, "%s: code %02X has no letter\n", files[i], code);
            assert(sheet.advance[code] > 0 && sheet.advance[code] <= PE_FONT_CELL);
            assert(sheet.metric[code] != 0);
        }
        assert(sheet.advance[PE_FONT_SPACE] > 0);
        if (!SDL_strcasecmp(files[i], "Pixel_Sans.png")) {
            shipped = 1;
            test_round_trip(&sheet);
        }
        SDL_DestroySurface(image);
        SDL_DestroySurface(loaded);
    }
    SDL_free(files);
    assert(shipped);
}

int main(int argc, char **argv) {
    test_sheet();
    if (argc > 1) test_shipped(argv[1]);
    puts("mod-fonts: ok");
    return 0;
}
