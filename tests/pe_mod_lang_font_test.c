/* pe_lang's Latin Extended glyphs: the C composer against the Python definition
 * (localization/petext/latin.py), pixel for pixel, on the synthetic test letters; and filling a
 * font page, which writes only empty cells. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "pe_lang_font.h"
#include "pe_lang_font_vectors.h"

/* latin.synthetic_cell, the stand-in game letter (no game art in tests). */
static void synthetic_cell(unsigned code, unsigned char *out) {
    unsigned char s[144] = {0};
    int top = code >= 0x10 && code <= 0x29 ? 1 : 3, stem = 1 + (int)code % 3, right = 5 + (int)code % 2;
    for (int y = top; y < 9; ++y) {
        s[y * 12 + stem] = 1;
        if ((y + (int)code) % 2) s[y * 12 + right] = 1;
    }
    for (int x = stem; x <= right; ++x) s[(top + 8) / 2 * 12 + x] = 1;
    if (code == 0x38) s[1 * 12 + stem] = 1;
    memcpy(out, s, 144);
    for (int i = 0; i < 144; ++i) {
        int x = i % 12, y = i / 12;
        if (s[i] == 1 && x + 1 < 12 && y + 1 < 12 && out[(y + 1) * 12 + x + 1] == 0) out[(y + 1) * 12 + x + 1] = 2;
    }
}

static void put(unsigned char *page, unsigned code, const unsigned char *cell) {
    unsigned cx = code % 21 * 12, cy = code / 21 * 12;
    for (unsigned i = 0; i < 144; ++i) {
        unsigned char *b = &page[(cy + i / 12) * 128 + (cx + i % 12) / 2];
        unsigned v = cell[i];
        *b = (cx + i % 12) & 1 ? (unsigned char)((*b & 0x0F) | v << 4) : (unsigned char)((*b & 0xF0) | v);
    }
}

static void get(const unsigned char *page, unsigned code, unsigned char *cell) {
    unsigned cx = code % 21 * 12, cy = code / 21 * 12;
    for (unsigned i = 0; i < 144; ++i) {
        unsigned char b = page[(cy + i / 12) * 128 + (cx + i % 12) / 2];
        cell[i] = (cx + i % 12) & 1 ? b >> 4 : b & 15;
    }
}

int main(void) {
    assert(sizeof kLatinVectors / sizeof *kLatinVectors == PE_LATIN_COUNT);
    for (unsigned k = 0; k < PE_LATIN_COUNT; ++k) {
        const PeLatinGlyph *g = &kPeLatinGlyphs[k];
        unsigned char base[144], out[144];
        if (g->base != 0xFF) synthetic_cell(g->base, base);
        pe_lang_font_compose(g, g->base != 0xFF ? base : NULL, out);
        for (unsigned i = 0; i < 144; ++i) {
            if (out[i] != kLatinVectors[k][i] - '0') {
                fprintf(stderr, "glyph %02X differs from latin.py at pixel %u (%u, %u): %u vs %c\n",
                        g->code, i, i % 12, i / 12, out[i], kLatinVectors[k][i]);
                return 1;
            }
        }
    }

    /* A page holding the base letters (codes 0-76) and one glyph already in a Latin cell. */
    static unsigned char page[128 * 256], before[128 * 256];
    unsigned char cell[144], other[144];
    for (unsigned code = 0; code < 77; ++code) {
        synthetic_cell(code, cell);
        put(page, code, cell);
    }
    synthetic_cell(0x10, other);
    put(page, kPeLatinGlyphs[5].code, other);
    memcpy(before, page, sizeof page);
    assert(pe_lang_font_fill(page, 128, 256) == PE_LATIN_COUNT - 1);
    get(page, kPeLatinGlyphs[5].code, cell);
    assert(!memcmp(cell, other, 144));                         /* never drawn over */
    for (unsigned k = 0; k < PE_LATIN_COUNT; ++k) {
        if (k == 5) continue;
        get(page, kPeLatinGlyphs[k].code, cell);
        for (unsigned i = 0; i < 144; ++i) assert(cell[i] == kLatinVectors[k][i] - '0');
    }
    for (unsigned code = 0; code < 441; ++code) {             /* every other cell as it was */
        int latin = 0;
        for (unsigned k = 0; k < PE_LATIN_COUNT; ++k) latin |= kPeLatinGlyphs[k].code == code;
        if (latin) continue;
        unsigned char a[144], b[144];
        get(page, code, a);
        get(before, code, b);
        assert(!memcmp(a, b, 144));
    }
    assert(pe_lang_font_fill(page, 128, 256) == 0);            /* a second upload adds nothing */

    /* Widths: a composed letter advances like its base; a drawn sign by its ink. */
    unsigned char advance[78], metrics[78];
    for (unsigned i = 0; i < 78; ++i) advance[i] = (unsigned char)(i % 9 + 3), metrics[i] = (unsigned char)(0x10 * (i % 12));
    for (unsigned k = 0; k < PE_LATIN_COUNT; ++k) {
        const PeLatinGlyph *g = &kPeLatinGlyphs[k];
        unsigned a = pe_lang_font_advance(g, advance);
        assert(g->base == 0xFF ? a >= 3 && a <= 12 : a == advance[g->base]);
        assert(g->base == 0xFF || pe_lang_font_metric(g, metrics) == metrics[g->base]);
    }
    printf("Passed Latin Extended checks: %d glyphs match latin.py, page fill writes only empty cells, widths.\n",
           PE_LATIN_COUNT);
    return 0;
}
