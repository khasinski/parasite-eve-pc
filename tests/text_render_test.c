/* The text renderer (src/main/draw/text_render.c) against the drawing code it replaced: for every
 * letter code, in dialogue boxes and menus, the same cell, rectangle and pen movement. The original
 * code is copied below as a reference, working on the same stand-in tables. Then stretches and
 * letters a pack provides. */
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "common.h"
#include "pe_hooks.h"
#include "pe1/field_bg_load.h"
#include "pe1/text_render.h"
#include "pe1/textbox.h"

/* ---- stand-ins for the game state the renderer reads ---- */
int g_TextRenderMode;
unsigned char g_TextboxKernGlyph0F;
static TextboxGlyphSpacing s_widths[241];
const TextboxGlyphSpacing *g_TextboxGlyphWidths = s_widths;
unsigned g_TextboxGlyphCount = 78;
FieldFontTextureState g_FieldFontTextureState;
static unsigned char s_metrics[2048];
int Draw_LookupGlyphMetrics(int glyph) { return s_metrics[glyph & 2047]; }
int g_DrawDigitFontBaseTexU, g_DrawDigitFontBaseTexV;

/* ---- a stand-in language pack ---- */
static const unsigned s_stretch[] = {'H', 'i', ' ', 0xE9, '!', 0x4E2D};
static int s_hooks = 1;
int pe_host_init(const PeHookInit *init) { (void)init; return 0; }
int pe_hook_dispatch(int id, void *ctx) {
    if (!s_hooks) return 0;
    if (id == PE_HOOK_TEXT_STRETCH) {
        PeHookTextStretch *s = ctx;
        if (s->id != 5 * 240 + 7) return 0;
        s->letters = s_stretch;
        s->count = sizeof s_stretch / sizeof *s_stretch;
        return 1;
    }
    if (id == PE_HOOK_TEXT_LETTER) {
        PeHookTextLetter *l = ctx;
        if (l->paged == 0x101) { l->code = 0x11; return 1; }           /* name letter 1 is a B */
        if (l->paged) return 0;
        if (l->letter == 0xE9) { l->code = 0x4D; return 1; }          /* a table letter */
        if (l->letter == 0x4E2D) { l->u = 36; l->v = 132; l->advance = 11; return 1; }
        if (l->letter == 0x100) { l->u = 26; l->v = 132; l->w = 7; l->advance = 8; return 1; }  /* cut */
        return 0;
    }
    if (id == PE_HOOK_TEXT_GLYPH) {
        PeHookTextGlyph *g = ctx;
        if (g->text[0] != 0xF1 || g->text[1] != 0xC5) return 0;
        g->bytes = 4; g->advance = 12; g->u = 24; g->v = 144;
        return 1;
    }
    return 0;
}

/* ---- what the old code drew: one sprite or quad, and the pen after ---- */
typedef struct Drawn { int x, y, u, v, w, h, page, sprite; } Drawn;

static unsigned s_seed = 12345;
static unsigned next_random(void) { s_seed = s_seed * 1103515245u + 12345u; return s_seed >> 16; }

/* Old Menu_DrawTextboxEntries, one dialogue letter at the cursor; returns 0 when the box stopped. */
static int old_dialogue_native(unsigned code, int kern, unsigned short *cx, Drawn *d) {
    if (kern && code == 0x0F && g_TextboxKernGlyph0F == 1) *cx = (unsigned short)(*cx - 7);
    if (code >= g_TextboxGlyphCount) return 0;
    *cx = (unsigned short)(*cx - g_TextboxGlyphWidths[code].left);
    d->x = (short)*cx; d->u = (unsigned char)(signed char)((code % 21) * 12);
    d->v = (unsigned char)(signed char)((code / 21) * 12); d->w = d->h = 12; d->page = 0;
    *cx = (unsigned short)(*cx + g_TextboxGlyphWidths[code].right);
    return 1;
}

static void check_item(const PeTextItem *item, int pen, int peny, const Drawn *want, int pen_after) {
    assert(item->count == 1);
    assert(pen + item->letter[0].x == want->x);
    assert(peny + item->letter[0].y == want->y);
    assert(item->letter[0].u == want->u && item->letter[0].v == want->v);
    assert(item->letter[0].w == want->w && item->letter[0].h == want->h);
    assert(item->letter[0].page == want->page);
    assert(pen + item->advance == pen_after);
}

static void test_dialogue(void) {
    unsigned code, n;
    for (int kern = 0; kern < 2; ++kern) for (unsigned count = 78; count <= 241; count += 163) {
        g_TextboxKernGlyph0F = (unsigned char)kern;
        g_TextboxGlyphCount = count;
        for (code = 0; code <= 0xFF; ++code) {
            unsigned char text[2] = {(unsigned char)code, 0xFF};
            PeTextItem item;
            unsigned short cx = 100;
            Drawn want = {0};
            unsigned bytes = PeText_Read(PE_TEXT_DIALOGUE, text, ~0u, 1, &item);
            if (code == 0xF7 || code == 0xF8 || code == 0xF9 || code == 0xFA || code == 0xFF) {
                assert(bytes == 0);
                continue;
            }
            if (code >= 0xFB && code <= 0xFD) continue;   /* two-byte forms, below */
            assert(bytes == 1);
            if (!old_dialogue_native(code, 1, &cx, &want)) { assert(item.stop); continue; }
            assert(!item.stop);
            want.y = 40;
            check_item(&item, 100, 40, &want, (short)cx);
            /* the same letter in the player's name: no kerning */
            cx = 100;
            PeText_Read(PE_TEXT_DIALOGUE_NAME, text, 1, 1, &item);
            if (!old_dialogue_native(code, 0, &cx, &want)) { assert(item.stop); continue; }
            check_item(&item, 100, 40, &want, (short)cx);
        }
    }
    g_TextboxGlyphCount = 78;
    g_TextboxFontPages[2].textureY = 0x1F4;
    for (n = 0; n <= 0xFF; ++n) {
        unsigned char fb[3] = {0xFB, (unsigned char)n, 0xFF}, fc[3] = {0xFC, (unsigned char)n, 0xFF},
                      fd[3] = {0xFD, (unsigned char)n, 0xFF};
        PeTextItem item;
        Drawn want = {100, 40, 0, 0, 12, 12, 0, 0};
        if (n <= 3) {
            want.u = (unsigned char)(signed char)(n * 12 + 0x40);
            want.v = (unsigned char)(signed char)g_TextboxFontPages[2].textureY;
            want.page = 2;
        } else if (n >= 10) {
            unsigned v = ((n + 0xED) / 21) * 12;
            want.u = (unsigned char)(signed char)(((n + 0xED) % 21) * 12);
            if ((v & 0xFFFF) >= 0xF1) { v -= 0xFC; want.page = 1; }
            want.v = (unsigned char)(signed char)v;
        }
        assert(PeText_Read(PE_TEXT_DIALOGUE, fb, ~0u, 1, &item) == (n <= 3 || n >= 10 ? 2u : 0u));
        if (n <= 3 || n >= 10) check_item(&item, 100, 40, &want, 112);
        want.page = 1;
        want.u = (unsigned char)(signed char)(((n + 0x34) % 21) * 12);
        want.v = (unsigned char)(signed char)(((n + 0x34) / 21) * 12);
        assert(PeText_Read(PE_TEXT_DIALOGUE, fc, ~0u, 1, &item) == 2);
        check_item(&item, 100, 40, &want, 112);
        want.u = (unsigned char)(signed char)(((n + 0x134) % 21) * 12);
        want.v = (unsigned char)(signed char)(((n + 0x134) / 21) * 12);
        assert(PeText_Read(PE_TEXT_DIALOGUE, fd, ~0u, 1, &item) == 2);
        check_item(&item, 100, 40, &want, 112);
    }
    for (n = 0; n < 10; ++n) {
        PeTextLetter l;
        PeText_DialogueDigit(n, &l);
        assert(l.x == 0 && l.y == 0 && l.u == n * 12 && l.v == 0 && l.w == 12 && l.page == 0);
    }
}

/* Old Draw_AllocTexturedQuad and Draw_MeasureTextWidth, with their page state. */
static int s_old_mode;
static int old_menu_quad(int code, int *cx, int cy, Drawn *d) {
    int current = code & 0xFF, glyph, metrics, width, spacing, column, row;
    if (s_old_mode) { current += s_old_mode << 8; s_old_mode = 0; }
    if ((code & 0xFF) >= 0xFA) { s_old_mode = (code & 0xFF) - 0xFA; current = -1; }
    glyph = current;
    if (glyph == 0x100) { d->sprite = 1; d->x = *cx; d->u = 0x77; *cx += 12; return 1; }
    if (glyph < 0) return 0;
    if (glyph >= 0x101) glyph -= 0x13;
    glyph %= 0x1B9;
    row = glyph / 0x15; column = glyph - row * 0x15;
    metrics = Draw_LookupGlyphMetrics(glyph);
    width = (metrics >> 4) & 0xF;
    spacing = (glyph < 10 || glyph == 15) + 1;
    d->sprite = 0;
    d->u = (unsigned char)(column * 12 + (metrics & 0xF));
    d->v = (unsigned char)(row * 12);
    d->x = *cx + (spacing >> 1); d->y = cy + 1; d->w = width; d->h = 12; d->page = 0;
    *cx += width + spacing;
    return 1;
}
static int s_measure_mode;
static int old_measure_byte(int code) {
    int glyph = (unsigned char)code;
    if (s_measure_mode) { glyph += s_measure_mode << 8; s_measure_mode = 0; }
    if ((unsigned char)code >= 250) { s_measure_mode = (unsigned char)code - 250; glyph = -1; }
    if (glyph < 0) return 0;
    {
        int advance = (glyph < 10 || glyph == 15) + 1;
        if (glyph >= 256) glyph -= 19;
        return ((Draw_LookupGlyphMetrics(glyph) >> 4) & 15) + advance;
    }
}

static void test_menu(void) {
    /* page bytes FA-FE (FF ends the text) */
    for (int prefix = -1; prefix <= 4; ++prefix) for (int code = 0; code < 0xFF; ++code) {
        unsigned char text[3];
        unsigned at = 0, i = 0;
        int cx = 200, pen = 200, measured = 0;
        if (prefix >= 0) text[at++] = (unsigned char)(0xFA + prefix);
        text[at++] = (unsigned char)code;
        text[at] = 0xFF;
        s_old_mode = g_TextRenderMode = 0;
        s_measure_mode = 0;
        for (i = 0; i < at; ++i) {
            PeTextItem item;
            Drawn want = {0};
            int drew = old_menu_quad(text[i], &cx, 60, &want);
            assert(PeText_Read(PE_TEXT_MENU, text + i, ~0u, 1, &item) == 1);
            assert(g_TextRenderMode == s_old_mode);
            if (!drew) { assert(item.count == 0); continue; }
            if (want.sprite) {
                assert(item.count == 1 && item.letter[0].page == PE_TEXT_PAGE_SPRITE && item.letter[0].u == 0x77);
                assert(pen + item.advance == cx);
            } else check_item(&item, pen, 60, &want, cx);
            pen += item.advance;
        }
        for (i = 0; i < at; ++i) measured += old_measure_byte(text[i]);
        g_TextRenderMode = 0;
        assert(PeText_MeasureMenu(text) == measured);
    }
}

/* Old Draw_EmitDigitSprite: the small menu digits, both variants and the blank. */
static void test_menu_digits(void) {
    for (int variant = 0; variant < 2; ++variant) for (int digit = -1; digit < 100; ++digit) {
        PeTextLetter l;
        int u, v;
        g_DrawDigitFontBaseTexU = variant ? 0xCC : 0x84;
        g_DrawDigitFontBaseTexV = 0xA4;
        u = (unsigned char)(digit >= 0 ? g_DrawDigitFontBaseTexU + (digit % 10) * 5 : 88);
        v = digit >= 0 ? g_DrawDigitFontBaseTexV : 164;
        PeText_MenuDigit(digit, &l);
        assert(l.x == 0 && l.y == 0 && l.u == u && l.v == v && l.w == 5 && l.h == 7);
        assert(l.page == PE_TEXT_PAGE_DIGITS);
    }
}

static void test_stretch(void) {
    unsigned char text[] = {0xF1, 0xC6, 5, 7, 0xFF};
    PeTextItem item, h;
    int pen = 0;
    unsigned char letter;
    g_TextboxGlyphCount = 241;
    assert(PeText_Read(PE_TEXT_DIALOGUE, text, ~0u, 1, &item) == 4);
    assert(item.count == 6);
    /* H, i, space and ! are the game's own letters; é is the pack's table code 4D; the last is a
     * cell the pack put on the page, with its own advance */
    /* (a dialogue stretch's space kerns as the dialogue's own does: g_TextboxKernGlyph0F is 1 here) */
    const int codes[] = {0x17, 0x38, 0x0F, 0x4D, 0x2B};
    assert(g_TextboxKernGlyph0F == 1);
    for (unsigned i = 0; i < 5; ++i) {
        letter = (unsigned char)codes[i];
        PeText_Read(PE_TEXT_DIALOGUE, &letter, 1, 1, &h);
        assert(item.letter[i].u == h.letter[0].u && item.letter[i].v == h.letter[0].v);
        assert(item.letter[i].x == pen + h.letter[0].x);
        pen += h.advance;
    }
    assert(item.letter[5].u == 36 && item.letter[5].v == 132 && item.letter[5].x == pen);
    assert(item.letter[5].w == 12 && item.advance == pen + 11);
    /* a provider's cell cut to its inked columns, as a menu cuts the game's own letters */
    {
        PeTextLetter cut;
        assert(PeText_ResolveLetter(PE_TEXT_MENU, 0x100, 1, &cut));
        assert(cut.u == 26 && cut.v == 132 && cut.w == 7 && cut.h == 12 && cut.x == 0 && cut.y == 1);
        assert(cut.advance == 8 && cut.code == PE_TEXT_NO_CODE);
    }
    /* menus measure a stretch by its letters */
    g_TextRenderMode = 0;
    assert(PeText_Read(PE_TEXT_MENU, text, ~0u, 0, &item) == 4);
    assert(PeText_MeasureMenu(text) == item.advance);
    /* an unknown stretch reads as the game's own F1, which ends the box, as it always did */
    text[2] = 9;
    assert(PeText_Read(PE_TEXT_DIALOGUE, text, ~0u, 1, &item) == 1 && item.stop == 1);
    /* a pack letter (F1 C5), with fewer than four bytes left it is the game's own */
    {
        unsigned char cjk[] = {0xF1, 0xC5, 0, 3, 0xFF};
        assert(PeText_Read(PE_TEXT_MENU, cjk, ~0u, 1, &item) == 4 && item.letter[0].u == 24);
        assert(item.letter[0].y == 1 && item.advance == 12);
        assert(PeText_Read(PE_TEXT_DIALOGUE_NAME, cjk, 3, 1, &item) == 1);
    }
    /* a letter no one has is '?' */
    s_hooks = 0;
    {
        PeTextLetter l, q;
        letter = 0x2C;
        PeText_ResolveLetter(PE_TEXT_MENU, 0x3B1, 1, &l);
        g_TextRenderMode = 0;
        PeText_Read(PE_TEXT_MENU, &letter, 1, 1, &item);
        q = item.letter[0];
        assert(l.u == q.u && l.v == q.v && l.w == q.w);
    }
    s_hooks = 1;
}

/* Two-byte name letters: the pack's in menus and dialogue names, '?' in a dialogue name when no
 * pack knows it, and the grid's cells counted by letters. */
static void test_name_letters(void) {
    const unsigned char menu[] = {0xFB, 0x01, 0xFF}, known[] = {0xFB, 0x01}, unknown[] = {0xFB, 0x02};
    const unsigned char grid[] = {0x10, 0xFB, 0x01, 0x11, 0xFE, 0xF9, 0xFF};
    unsigned char b = 0x11, q = 0x2C;
    PeTextItem item, want;
    g_TextRenderMode = 0;
    assert(PeText_Read(PE_TEXT_MENU, menu, ~0u, 1, &item) == 1 && item.count == 0 && g_TextRenderMode == 1);
    assert(PeText_Read(PE_TEXT_MENU, menu + 1, ~0u, 1, &item) == 1 && item.count == 1 && g_TextRenderMode == 0);
    PeText_Read(PE_TEXT_MENU, &b, 1, 1, &want);
    assert(item.letter[0].u == want.letter[0].u && item.advance == want.advance);
    assert(PeText_Read(PE_TEXT_DIALOGUE_NAME, known, 2, 1, &item) == 2 && !item.stop);
    PeText_Read(PE_TEXT_DIALOGUE_NAME, &b, 1, 1, &want);
    assert(item.letter[0].u == want.letter[0].u && item.advance == want.advance);
    assert(PeText_Read(PE_TEXT_DIALOGUE_NAME, unknown, 2, 1, &item) == 2 && !item.stop);
    PeText_Read(PE_TEXT_DIALOGUE_NAME, &q, 1, 1, &want);
    assert(item.letter[0].u == want.letter[0].u && item.letter[0].v == want.letter[0].v);
    assert(PeText_NthLetter(grid, 0) == grid && PeText_NthLetter(grid, 1) == grid + 1);
    assert(PeText_NthLetter(grid, 2) == grid + 3 && PeText_NthLetter(grid, 3) == grid + 4);
    assert(!PeText_NthLetter(grid, 4) && PeText_LetterBytes(grid + 1) == 2 && PeText_LetterBytes(grid + 4) == 2);
}

/* ---- the credits roll: the old text_screen.c code, copied as the reference ---- */
static unsigned short s_old_style;

#define OLD_PUNCTUATION_INDEX(base) { \
    index = -1; \
    switch (saved) { \
    case ':': ++index; \
    case '?': ++index; \
    case '/': ++index; \
    case '.': ++index; \
    case ',': ++index; \
    case '\'': ++index; \
    case '-': ++index; \
    case ')': ++index; \
    case '(': ++index; \
    case '&': ++index; \
    case '#': ++index; \
    case '@': ++index; \
    case '!': ++index; \
    case '~': ++index; \
    default: index += (base); \
    } \
}
static int old_credit_index(unsigned char character) {
    unsigned short style = s_old_style;
    unsigned char saved = character;
    int index;
    if (style) {
        if ((unsigned char)(character - 'a') < 26) index = saved - 32;
        else if ((unsigned char)(character - 'A') < 26) index = saved - 65;
        else if ((unsigned char)(character - '0') < 10) index = saved + 58;
        else OLD_PUNCTUATION_INDEX(26);
    } else {
        if ((unsigned char)(character - 'A') < 26) index = saved - 25;
        else if ((unsigned char)(character - 'a') < 26) index = saved - 31;
        else if ((unsigned char)(character - '0') < 10) index = saved + 58;
        else OLD_PUNCTUATION_INDEX(92);
    }
    return (unsigned char)index;
}
#undef OLD_PUNCTUATION_INDEX

static unsigned short old_credit_width(unsigned char character) {
    unsigned short style = s_old_style;
    unsigned char saved;
    int width;
    if (style > 3) return 0;
    if (style == 2) {
        saved = character;
        if ((unsigned char)(character - 'a') < 26) saved = character - 32;
        if ((unsigned char)(saved - 'A') < 26) width = g_BootDisplayUppercaseWidths[saved - 'A'][2];
        else switch (saved) {
        case '!': case '\'': case '.': case ':': width = 3; break;
        case '#': width = 7; break;
        case '&': width = 11; break;
        case '(': case ')': case ',': case '-': case '/': width = 4; break;
        case '?': width = 6; break;
        case '@': width = 9; break;
        case '~': width = 8; break;
        default: width = 8; break;
        }
    } else {
        if ((unsigned char)(character - 'A') < 26) width = g_BootDisplayUppercaseWidths[character - 'A'][style];
        else if ((unsigned char)(character - 'a') < 26) width = g_BootDisplayUppercaseWidths[character - 'a'][style | 1];
        else switch (character) {
        case '!': case '.': case ':': width = 2; break;
        case '\'': case ',': width = 3; break;
        case '(': case ')': case '-': case '/': case '?': width = 4; break;
        case '&': case '@': width = 9; break;
        case '~': width = 6; break;
        default: width = 6; break;
        }
    }
    return (unsigned short)width;
}

typedef struct CreditDrawn { short x; unsigned char c; } CreditDrawn;

/* One line as BootDisplay_DrawTextBlock drew it: each letter's column and byte. */
static unsigned old_credit_line(const unsigned char *line, CreditDrawn *out) {
    unsigned x = 24, width, n = 0;
    const unsigned char *scan;
    for (; *line; ++line) {
        switch (*line) {
        case ' ':
            break;
        case '\\':
            width = 0;
            if (line[1]) {
                scan = line + 1;
                do { width += old_credit_width(*scan++); } while (*scan);
            }
            x = (320 - width) >> 1;
            continue;
        case '\t':
            x += old_credit_width(' ') * 14;
            continue;
        default:
            out[n].x = (short)x; out[n].c = *line; ++n;
            break;
        }
        x += old_credit_width(*line);
    }
    return n;
}

static unsigned new_credit_line(unsigned style, const unsigned char *line, PeTextLetter *out) {
    PeTextCreditPen pen;
    unsigned n = 0;
    pen.x = PE_TEXT_CREDIT_LEFT;
    for (; *line; ++line) {
        pen.style = style;
        if (PeText_ReadCredit(&pen, line, &out[n])) ++n;
    }
    return n;
}

/* Every byte in every style the game can hold gives the old cell and width; lines (the real
 * roll's shapes, odd bytes, a line too wide to centre) place every letter where the old code did,
 * with the cell the drawers cut. */
static void test_credits(void) {
    static const unsigned short styles[] = {0, 1, 2, 3, 4, 5, 7, 255, 256, 257, 0x8000, 65535};
    static const char *lines[] = {
        "PRODUCER", "\tHironobu Sakaguchi", "\\BASED ON A NOVEL BY", "\t'Parasite Eve'",
        "\t(Kadokawa Horror Bunko)", "", " ", "\\", "\\ ", "a\\b", "\t\tTwo tabs", "A B  C",
        "~!@#&()-',./?:", "0123456789", "MIXED case & Numbers 42!", "\\x\ty z",
        "\\WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
    };
    unsigned char all[256], odd[64];
    unsigned s, c, i;
    for (s = 0; s < sizeof styles / sizeof *styles; ++s) {
        s_old_style = styles[s];
        for (c = 0; c < 256; ++c) {
            assert(PeText_CreditCell(styles[s], (unsigned char)c) == (unsigned)old_credit_index((unsigned char)c));
            assert(PeText_CreditWidth(styles[s], (unsigned char)c) == old_credit_width((unsigned char)c));
        }
    }
    for (c = 1; c < 256; ++c) all[c - 1] = (unsigned char)c;
    all[255] = 0;
    for (s = 0; s < 4; ++s) {
        CreditDrawn want[256];
        PeTextLetter got[256];
        unsigned n, k;
        s_old_style = styles[s];
        for (i = 0; i < sizeof lines / sizeof *lines + 1 + 32; ++i) {
            const unsigned char *line;
            if (i < sizeof lines / sizeof *lines) line = (const unsigned char *)lines[i];
            else if (i == sizeof lines / sizeof *lines) line = all;
            else {                               /* random bytes, some spaces, tabs and '\' */
                for (k = 0; k + 1 < sizeof odd; ++k) {
                    unsigned r = next_random() % 8;
                    odd[k] = (unsigned char)(r == 0 ? ' ' : r == 1 ? '\t' : r == 2 ? '\\' : 1 + next_random() % 255);
                }
                odd[next_random() % (sizeof odd - 1)] = 0;
                odd[sizeof odd - 1] = 0;
                line = odd;
            }
            n = old_credit_line(line, want);
            assert(new_credit_line(styles[s], line, got) == n);
            for (k = 0; k < n; ++k) {
                unsigned cell = (unsigned)old_credit_index(want[k].c);
                assert(got[k].x == want[k].x && got[k].code == want[k].c && got[k].y == 0);
                assert(got[k].u == ((cell & 15) << 4) && got[k].v == (cell & 0xF0));
                assert(got[k].w == 16 && got[k].h == 16 && got[k].page == PE_TEXT_PAGE_CREDITS);
                assert(got[k].advance == old_credit_width(want[k].c));
            }
        }
    }
}

int main(void) {
    unsigned i;
    for (i = 0; i < sizeof s_metrics; ++i) s_metrics[i] = (unsigned char)next_random();
    for (i = 0; i < 241; ++i) {
        s_widths[i].left = (unsigned char)(next_random() % 4);
        s_widths[i].right = (unsigned char)(5 + next_random() % 8);
    }
    s_hooks = 0;                     /* the game's own text, no pack: as the old code drew it */
    test_dialogue();
    test_menu();
    s_hooks = 1;
    test_menu_digits();
    test_stretch();
    test_name_letters();
    test_credits();
    puts("text renderer: every letter code matches the old drawing code, credits roll included; stretches place right");
    return 0;
}
