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
/* a menu line, then the same line narrowed to 240/256 and to 128/256 (PE_TEXT_NARROW_FIRST) */
static const unsigned s_line[] = {'H', 'i', ' ', 'm', 'a', ' ', 'H', 'i', ' ', 'm', 'a'};
static unsigned s_marked[1 + sizeof s_line / sizeof *s_line];
static int s_hooks = 1;
/* a right-to-left pack: its stretch 7 * 240 holds s_rtl; Hebrew, Arabic and brackets are pack cells
 * 6 wide that move the pen 7 */
static int s_rtl_on;
static unsigned s_rtl[PE_TEXT_LINE_LETTERS];
static unsigned s_rtl_count;
static int rtl_pack_letter(unsigned c) {
    return c >= 0x80 || c == '(' || c == ')' || c == '[' || c == ']';
}
int pe_host_init(const PeHookInit *init) { (void)init; return 0; }
int pe_hook_dispatch(int id, void *ctx) {
    if (!s_hooks) return 0;
    if (id == PE_HOOK_TEXT_SCRIPT) {
        PeHookTextScript *t = ctx;
        t->rtl = s_rtl_on;
        t->letter = t->code == 0x4E ? 0x05D2u : 0;                      /* a table code: gimel */
        return 1;
    }
    if (id == PE_HOOK_TEXT_STRETCH) {
        PeHookTextStretch *s = ctx;
        if (s->id == 7 * 240) {
            s->letters = s_rtl;
            s->count = s_rtl_count;
            return 1;
        }
        if (s->id == 6 * 240) {
            s->letters = s_line;
            s->count = sizeof s_line / sizeof *s_line;
            return 1;
        }
        if (s->id == 6 * 240 + 1) {
            s->letters = s_marked;
            s->count = sizeof s_marked / sizeof *s_marked;
            return 1;
        }
        if (s->id != 5 * 240 + 7) return 0;
        s->letters = s_stretch;
        s->count = sizeof s_stretch / sizeof *s_stretch;
        return 1;
    }
    if (id == PE_HOOK_TEXT_LETTER) {
        PeHookTextLetter *l = ctx;
        if (l->paged == 0x101) { l->code = 0x11; return 1; }           /* name letter 1 is a B */
        if (l->paged == 0x104 || l->paged == 0x105) {                  /* alef, bet */
            l->letter = l->paged == 0x104 ? 0x05D0u : 0x05D1u;
            l->u = 60; l->v = 132; l->w = 6; l->advance = 7;
            return 1;
        }
        if (l->paged) return 0;
        if (s_rtl_on && rtl_pack_letter(l->letter)) {
            l->u = (unsigned char)(l->letter & 0xF0); l->v = 120; l->w = 6; l->advance = 7;
            return 1;
        }
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

/* A narrowed menu line: exactly the width the pack tool measured, the room taken from spaces and
 * gaps before any letter is drawn narrower; dialogue boxes ignore the mark. */
static void test_narrow(void) {
    unsigned char plain[] = {0xF1, 0xC6, 6, 0, 0xFF}, marked[] = {0xF1, 0xC6, 6, 1, 0xFF};
    PeTextItem a, b;
    int k;
    for (unsigned i = 0; i < sizeof s_line / sizeof *s_line; ++i) s_marked[i + 1] = s_line[i];
    /* 240/256: spaces alone give the room, every letter keeps its cell and width */
    s_marked[0] = PE_TEXT_NARROW_FIRST + 112;
    g_TextRenderMode = 0;
    PeText_Read(PE_TEXT_MENU, plain, ~0u, 1, &a);
    PeText_Read(PE_TEXT_MENU, marked, ~0u, 1, &b);
    assert(a.count == b.count && b.count == sizeof s_line / sizeof *s_line);
    assert(b.width == (a.width * 240 + 128) >> 8 && b.advance == b.width && b.width < a.width);
    assert(PeText_MeasureMenu(marked) == b.width);
    for (unsigned i = 0; i < b.count; ++i) {
        assert(b.letter[i].dw == 0 && b.letter[i].w == a.letter[i].w && b.letter[i].u == a.letter[i].u);
        assert(i == 0 || b.letter[i].x > b.letter[i - 1].x);
        if (s_line[i] != ' ' && i + 1 < b.count && s_line[i + 1] != ' ')   /* gaps inside words stay */
            assert(b.letter[i + 1].x - b.letter[i].x == a.letter[i + 1].x - a.letter[i].x);
    }
    /* 128/256: more than all the blank, so letters are drawn narrower too, still exactly to width */
    s_marked[0] = PE_TEXT_NARROW_FIRST;
    PeText_Read(PE_TEXT_MENU, marked, ~0u, 1, &b);
    assert(b.width == (a.width * 128 + 128) >> 8);
    for (unsigned i = 0, narrower = 0; i < b.count; ++i) {
        assert(PE_TEXT_DRAWN_W(&b.letter[i]) >= 1 && PE_TEXT_DRAWN_W(&b.letter[i]) <= b.letter[i].w);
        narrower += b.letter[i].dw != 0;
        if (i + 1 == b.count) assert(narrower > 0);
    }
    /* a dialogue box draws the letters as they are */
    PeText_Read(PE_TEXT_DIALOGUE, plain, ~0u, 1, &a);
    PeText_Read(PE_TEXT_DIALOGUE, marked, ~0u, 1, &b);
    assert(a.count == b.count && a.advance == b.advance);
    for (k = 0; k < (int)a.count; ++k) assert(a.letter[k].x == b.letter[k].x && b.letter[k].dw == 0);
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

/* ---- right-to-left lines ---- */

static unsigned utf8(const char **p) {
    const unsigned char *s = (const unsigned char *)*p;
    unsigned c = *s++, extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
    c &= extra ? 0x3Fu >> extra : 0x7Fu;
    while (extra--) c = c << 6 | (*s++ & 0x3Fu);
    *p = (const char *)s;
    return c;
}

static unsigned decode(const char *from, const char *to, unsigned *out) {
    unsigned n = 0;
    while (from < to) out[n++] = utf8(&from);
    return n;
}

static const unsigned char s_rtl_text[] = {0xF1, 0xC6, 7, 0, 0xFF};

/* One stretch holding `letters` read as a line of `style`. */
static int rtl_line(PeTextStyle style, const unsigned *letters, unsigned count, PeTextLine *line) {
    PeTextItem item;
    memcpy(s_rtl, letters, count * sizeof *letters);
    s_rtl_count = count;
    PeText_LineStart(line);
    assert(PeText_LineRead(line, style, s_rtl_text, ~0u, 0, &item) == 4);
    return PeText_LineFinish(line, 1);
}

/* Every case in text_render_rtl_cases.txt, in dialogue boxes and menus. */
static unsigned test_rtl_cases(const char *path) {
    static char text[16384];
    FILE *f = fopen(path, "rb");
    size_t size;
    unsigned cases = 0;
    char *line, *end;
    assert(f);
    size = fread(text, 1, sizeof text - 1, f);
    fclose(f);
    text[size] = 0;
    for (line = text; *line; line = end + (*end != 0)) {
        unsigned logical[PE_TEXT_LINE_LETTERS], visual[PE_TEXT_LINE_LETTERS], n, v, i;
        char *tab;
        int style;
        for (end = line; *end && *end != '\n'; ++end) {}
        if (*line == '#' || end == line) continue;
        tab = memchr(line, '\t', (size_t)(end - line));
        assert(tab);
        n = decode(line, tab, logical);
        v = decode(tab + 1, end, visual);
        for (style = PE_TEXT_DIALOGUE; style <= PE_TEXT_MENU; ++style) {
            static PeTextLine out;
            rtl_line((PeTextStyle)style, logical, n, &out);
            if (out.count != v) {
                fprintf(stderr, "rtl case %u: %u letters drawn, %u expected\n", cases + 1, out.count, v);
                assert(0);
            }
            for (i = 0; i < v; ++i) {
                if (out.letter[i].letter == visual[i]) continue;
                fprintf(stderr, "rtl case %u, letter %u: U+%04X drawn, U+%04X expected\n", cases + 1, i,
                        out.letter[i].letter, visual[i]);
                assert(0);
            }
        }
        ++cases;
    }
    return cases;
}

static void test_rtl_lines(void) {
    static PeTextLine line;
    PeTextItem item;
    PeTextLetter l;
    unsigned i;
    s_rtl_on = 1;
    g_TextboxKernGlyph0F = 0;
    assert(PeText_RightToLeft());
    /* two joining letters touch: the left one moves the pen by its width less the shadow column */
    {
        static const unsigned beh[] = {0x0628, 0x0628};
        assert(rtl_line(PE_TEXT_MENU, beh, 2, &line) == 12);
        assert(line.letter[0].letter == 0xFE90 && line.letter[1].letter == 0xFE91);
        assert(line.letter[0].x == 0 && line.letter[1].x == 5);
        assert(line.letter[0].y == 1 && line.letter[0].w == 6);
    }
    /* letters that don't join keep their gap */
    {
        static const unsigned dal[] = {0x062F, 0x062F};
        assert(rtl_line(PE_TEXT_DIALOGUE, dal, 2, &line) == 14);
        assert(line.letter[1].x == 7);
    }
    /* a dialogue line: a stretch, a button icon and the box's number (digits it places itself) */
    {
        static const unsigned shalom[] = {0x05E9, 0x05DC, 0x05D5, 0x05DD, ' '};
        static const unsigned char icon[] = {0xFB, 0x01};
        memcpy(s_rtl, shalom, sizeof shalom);
        s_rtl_count = 5;
        PeText_LineStart(&line);
        assert(PeText_LineRead(&line, PE_TEXT_DIALOGUE, s_rtl_text, ~0u, 1, &item) == 4);
        assert(PeText_LineRead(&line, PE_TEXT_DIALOGUE, icon, 2, 2, &item) == 2);
        for (i = 1; i <= 2; ++i) {
            PeText_DialogueDigit(i, &l);
            l.advance = 12;
            PeText_LineAdd(&line, PE_TEXT_DIALOGUE, &l, 3);
        }
        assert(PeText_LineFinish(&line, 1) == 4 * 7 + 12 * 3 + s_widths[0x0F].right - s_widths[0x0F].left);
        assert(line.count == 8);
        assert(line.letter[0].letter == '1' && line.letter[1].letter == '2' && line.letter[0].x == 0);
        assert(line.letter[1].x == 12 && line.letter[2].letter == 0 && line.letter[2].page == 2);
        assert(line.letter[2].x == 24);
        assert(line.tag[0] == 3 && line.tag[2] == 2 && line.tag[4] == 1);
        assert(line.letter[3].letter == ' ' && line.letter[7].letter == 0x05E9);
    }
    /* a name of two-byte letters and a pack's one-byte code, by their Unicode letters */
    {
        static const unsigned char name[] = {0xFB, 0x04, 0xFB, 0x05};
        static const unsigned char menu[] = {0x4E, 0x23, 0xFF};      /* gimel, then 'T' */
        PeText_LineStart(&line);
        assert(PeText_LineRead(&line, PE_TEXT_DIALOGUE_NAME, name, 4, 0, &item) == 2);
        assert(PeText_LineRead(&line, PE_TEXT_DIALOGUE_NAME, name + 2, 2, 0, &item) == 2);
        PeText_LineFinish(&line, 1);
        assert(line.count == 2 && line.letter[0].letter == 0x05D1 && line.letter[1].letter == 0x05D0);
        g_TextRenderMode = 0;
        PeText_LineStart(&line);
        for (i = 0; menu[i] != 0xFF; i += PeText_LineRead(&line, PE_TEXT_MENU, menu + i, ~0u, 0, &item)) {}
        PeText_LineFinish(&line, 1);
        assert(line.count == 2 && line.letter[0].letter == 'T' && line.letter[1].letter == 0x05D2);
        assert(line.letter[1].code == 0x4E);              /* drawn as the game draws the code */
        assert(PeText_MeasureMenu(menu) == line.width);
    }
    /* a narrowed menu line still fits */
    {
        unsigned letters[1 + 20];
        int full;
        for (i = 1; i <= 20; ++i) letters[i] = i % 5 ? 0x05D0 + i : ' ';
        full = rtl_line(PE_TEXT_MENU, letters + 1, 20, &line);
        letters[0] = PE_TEXT_NARROW_FIRST + 64;             /* 192 / 256 */
        assert(rtl_line(PE_TEXT_MENU, letters, 21, &line) <= (full * 192 + 128) >> 8);
        assert(line.count == 20);
    }
    s_rtl_on = 0;
    assert(!PeText_RightToLeft());
}

int main(int argc, char **argv) {
    unsigned i, cases;
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
    test_narrow();
    test_name_letters();
    test_credits();
    test_rtl_lines();
    s_rtl_on = 1;
    cases = test_rtl_cases(argc > 1 ? argv[1] : "tests/text_render_rtl_cases.txt");
    s_rtl_on = 0;
    puts("text renderer: every letter code matches the old drawing code, credits roll included; stretches place right, narrowed lines fit");
    printf("text renderer: %u right-to-left cases ordered and joined as written, in dialogue and menus\n", cases);
    return 0;
}
