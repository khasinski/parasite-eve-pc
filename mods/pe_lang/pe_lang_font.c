/* Latin Extended glyphs composed from the game's font: the C side of latin.compose. Plain data, no
 * game state, so tests check it against the Python definition pixel for pixel. */
#include "pe_lang_font.h"

#include <string.h>

#define CELL PE_FONT_CELL
#define CELL_PX (CELL * CELL)

static void ink_box(const unsigned char *s, int *x0, int *x1, int *y0, int *y1) {
    *x0 = *y0 = CELL;
    *x1 = *y1 = -1;
    for (int i = 0; i < CELL_PX; ++i) {
        if (s[i] != 1) continue;
        int x = i % CELL, y = i / CELL;
        if (x < *x0) *x0 = x;
        if (x > *x1) *x1 = x;
        if (y < *y0) *y0 = y;
        if (y > *y1) *y1 = y;
    }
}

static void add_shadow(const unsigned char *strokes, unsigned char *out) {
    memcpy(out, strokes, CELL_PX);
    for (int i = 0; i < CELL_PX; ++i) {
        int x = i % CELL, y = i / CELL;
        if (strokes[i] == 1 && x + 1 < CELL && y + 1 < CELL && out[(y + 1) * CELL + x + 1] == 0)
            out[(y + 1) * CELL + x + 1] = 2;
    }
}

/* Make room above a capital: drop a middle row equal to the one below it (a stem), moving the
 * rows above it down one, so the baseline stays. */
static void squash(unsigned char *s) {
    static const int order[] = {5, 4, 6, 3, 7, 2};
    int drop = 5;
    for (unsigned k = 0; k < sizeof order / sizeof *order; ++k) {
        if (!memcmp(s + order[k] * CELL, s + (order[k] + 1) * CELL, CELL)) {
            drop = order[k];
            break;
        }
    }
    memmove(s + CELL, s, (size_t)drop * CELL);
    memset(s, 0, CELL);
}

static int is_capital(unsigned char code) { return code >= 0x10 && code <= 0x29; }   /* A-Z */

void pe_lang_font_compose(const PeLatinGlyph *g, const unsigned char *base, unsigned char *out) {
    unsigned char s[CELL_PX] = {0};
    int x0, x1, y0, y1;
    if (g->base == 0xFF) {
        const unsigned short *rows = kPeLatinArt[g->mark];
        for (int y = 0; y < CELL; ++y)
            for (int x = 0; x + 1 < CELL; ++x)
                if (rows[y] >> x & 1) s[y * CELL + x + 1] = 1;
        add_shadow(s, out);
        return;
    }
    for (int i = 0; i < CELL_PX; ++i) s[i] = base[i] == 1;
    if (g->ops & PE_LATIN_OP_DOTLESS) memset(s, 0, 3 * CELL);
    if (g->ops & PE_LATIN_OP_ROTATE) {
        unsigned char turned[CELL_PX] = {0};
        ink_box(s, &x0, &x1, &y0, &y1);
        for (int i = 0; i < CELL_PX; ++i)
            if (s[i]) turned[(y0 + y1 - i / CELL) * CELL + (x0 + x1 - i % CELL)] = 1;
        memcpy(s, turned, CELL_PX);
    }
    int capital = is_capital(g->base);
    const PeLatinMark *mark = &kPeLatinMarks[g->mark];
    if (g->mark != PE_MARK_NONE && g->mark != PE_MARK_SLASH && !mark->fixed_rows && capital) squash(s);
    ink_box(s, &x0, &x1, &y0, &y1);
    if (x1 >= 0 && g->mark == PE_MARK_SLASH) {
        int span = y1 - y0 > 0 ? y1 - y0 : 1;
        for (int k = 0; k <= y1 - y0; ++k) s[(y1 - k) * CELL + x0 + (2 * k * (x1 - x0) + span) / (2 * span)] = 1;
    } else if (x1 >= 0 && g->mark != PE_MARK_NONE) {
        int anchor = (x0 + x1) / 2, top = capital ? 0 : 1;
        if (mark->anchor == 1) anchor = x0;
        else if (mark->anchor == 2) anchor = x1;
        else if (mark->anchor == 3) {
            int best = -1;
            for (int x = 0; x < CELL; ++x) {
                int n = 0;
                for (int y = 0; y < CELL; ++y) n += s[y * CELL + x];
                if (n > best) best = n, anchor = x;
            }
        }
        for (unsigned k = 0; k < mark->count; ++k) {
            int x = anchor + mark->pts[k].dx, y = mark->fixed_rows ? mark->pts[k].row : top + mark->pts[k].row;
            if (x >= 0 && x < CELL && y >= 0 && y < CELL) s[y * CELL + x] = 1;
        }
    }
    add_shadow(s, out);
}

static unsigned px_get(const unsigned char *p, unsigned stride, unsigned x, unsigned y) {
    unsigned char b = p[y * stride + x / 2];
    return x & 1 ? b >> 4 : b & 15;
}

static void px_set(unsigned char *p, unsigned stride, unsigned x, unsigned y, unsigned v) {
    unsigned char *b = &p[y * stride + x / 2];
    *b = x & 1 ? (unsigned char)((*b & 0x0F) | v << 4) : (unsigned char)((*b & 0xF0) | v);
}

static void read_cell(const unsigned char *p, unsigned stride, unsigned code, unsigned char *out) {
    unsigned cx = code % PE_FONT_COLUMNS * CELL, cy = code / PE_FONT_COLUMNS * CELL;
    for (unsigned i = 0; i < CELL_PX; ++i) out[i] = (unsigned char)px_get(p, stride, cx + i % CELL, cy + i / CELL);
}

int pe_lang_font_fill(unsigned char *pixels, unsigned stride, unsigned height) {
    int written = 0;
    for (unsigned k = 0; k < PE_LATIN_COUNT; ++k) {
        const PeLatinGlyph *g = &kPeLatinGlyphs[k];
        unsigned char base[CELL_PX], cell[CELL_PX], out[CELL_PX];
        unsigned cx = g->code % PE_FONT_COLUMNS * CELL, cy = g->code / PE_FONT_COLUMNS * CELL;
        if (cy + CELL > height || stride * 2 < cx + CELL) continue;
        read_cell(pixels, stride, g->code, cell);
        int empty = 1;
        for (unsigned i = 0; i < CELL_PX && empty; ++i) empty = !cell[i];
        if (!empty) continue;                             /* never draw over a glyph the font has */
        if (g->base != 0xFF) read_cell(pixels, stride, g->base, base);
        pe_lang_font_compose(g, g->base != 0xFF ? base : NULL, out);
        for (unsigned i = 0; i < CELL_PX; ++i) px_set(pixels, stride, cx + i % CELL, cy + i / CELL, out[i]);
        ++written;
    }
    return written;
}

unsigned pe_lang_font_advance(const PeLatinGlyph *g, const unsigned char *base_advance) {
    unsigned char out[CELL_PX];
    int x0, x1, y0, y1;
    if (g->base != 0xFF) return base_advance[g->base];
    pe_lang_font_compose(g, NULL, out);
    ink_box(out, &x0, &x1, &y0, &y1);
    return (unsigned)x1 + 2;
}

unsigned char pe_lang_font_metric(const PeLatinGlyph *g, const unsigned char *base_metrics) {
    if (g->base != 0xFF) return base_metrics[g->base];
    unsigned width = pe_lang_font_advance(g, NULL);
    return (unsigned char)((width > 15 ? 15 : width) << 4);
}
