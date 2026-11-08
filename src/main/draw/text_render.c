/* The game's text renderer (pe1/text_render.h): which letters text draws, where, and how far the
 * pen moves. Every letter the dialogue boxes and menus put on screen is placed here; they only turn
 * the result into packets. The game's own letters keep exactly the cells, offsets and spacing the
 * original drawing code gave them. */
#include "common.h"
#include "pe_hooks.h"
#include "pe1/draw_state.h"
#include "pe1/field_bg_load.h"
#include "pe1/menu_flags.h"
#include "pe1/mod_text.h"
#include "pe1/text_render.h"
#include "pe1/textbox.h"

extern int g_TextRenderMode;

#define TOKEN_STRETCH 0xC6u

static unsigned StretchId(const u8 *p, unsigned avail) {
    if (avail < 4 || p[0] != 0xF1 || p[1] != TOKEN_STRETCH || p[2] >= 240 || p[3] >= 240) return ~0u;
    return p[2] * 240u + p[3];
}

/* ---- The game's own letters ---- */

static void Cell(PeTextLetter *l, unsigned cell) {
    l->u = (u8)((cell % 21) * 12);
    l->v = (u8)((cell / 21) * 12);
}

/* A dialogue letter on font page 0: the box moves back by its left gap, draws the 12 x 12 cell,
 * then moves on by its right width. */
static int DialogueNative(unsigned code, int kerning, PeTextLetter *l) {
    int kern = kerning && code == 0x0F && g_TextboxKernGlyph0F == 1 ? 7 : 0;
    if (code >= g_TextboxGlyphCount) return 0;
    Cell(l, code);
    l->x = (s16)(-kern - g_TextboxGlyphWidths[code].left);
    l->y = 0;
    l->w = l->h = 12;
    l->page = 0;
    l->nibble = 0;
    l->advance = (s16)(-kern - g_TextboxGlyphWidths[code].left + g_TextboxGlyphWidths[code].right);
    l->code = (u16)code;
    return 1;
}

/* A fixed 12 px dialogue cell (FB, FC, FD letters and the button icons). */
static void DialogueFixed(PeTextLetter *l, unsigned page, unsigned u, unsigned v, unsigned code) {
    l->x = l->y = 0;
    l->u = (u8)u; l->v = (u8)v;
    l->w = l->h = 12;
    l->page = (u8)page;
    l->nibble = 0;
    l->advance = 12;
    l->code = (u16)code;
}

static int MenuNarrow(int glyph) { return glyph < 10 || glyph == 15; }

/* What the menus' measuring counts for a letter (Draw_MeasureTextWidth's sum). */
static int MenuMeasure(int glyph) {
    int spacing = MenuNarrow(glyph) + 1;
    if (glyph >= 256) glyph -= 19;
    return ((Draw_LookupGlyphMetrics(glyph) >> 4) & 15) + spacing;
}

/* A menu letter, its page already added: a quad cut from the font page at the cell's first column
 * (metrics low nibble) and width (high nibble), half the spacing in, one pixel down. */
static void MenuNative(int glyph, PeTextLetter *l) {
    int metrics, width, spacing;
    if (glyph == 0x100) {                       /* a sprite in the letter set */
        l->x = l->y = 0;
        l->u = 0x77; l->v = 0; l->w = l->h = 0;
        l->page = PE_TEXT_PAGE_SPRITE;
        l->nibble = 0;
        l->advance = 12;
        l->code = PE_TEXT_NO_CODE;
        return;
    }
    if (glyph >= 0x101) glyph -= 0x13;
    glyph %= 0x1B9;
    metrics = Draw_LookupGlyphMetrics(glyph);
    width = (metrics >> 4) & 15;
    spacing = MenuNarrow(glyph) + 1;
    l->x = (s16)(spacing >> 1);
    l->y = 1;
    l->u = (u8)((glyph % 21) * 12 + (metrics & 15));
    l->v = (u8)((glyph / 21) * 12);
    l->w = (u8)width;
    l->h = 12;
    l->page = 0;
    l->nibble = (u8)(metrics & 15);
    l->advance = (s16)(width + spacing);
    l->code = (u16)glyph;
}

/* A cell the pack put on font page 0 (an F1 C5 letter or a resolved stretch letter). */
static void PackCell(PeTextStyle style, PeTextLetter *l, unsigned u, unsigned v, unsigned advance) {
    l->x = 0;
    l->y = style == PE_TEXT_MENU ? 1 : 0;
    l->u = (u8)u; l->v = (u8)v;
    l->w = l->h = 12;
    l->page = 0;
    l->nibble = 0;
    l->advance = (s16)advance;
    l->code = PE_TEXT_NO_CODE;
}

void PeText_DialogueDigit(unsigned digit, PeTextLetter *out) {
    DialogueFixed(out, 0, digit * 12, 0, PE_TEXT_NO_CODE);
}

extern int g_DrawDigitFontBaseTexU, g_DrawDigitFontBaseTexV;

void PeText_MenuDigit(int digit, PeTextLetter *out) {
    out->x = out->y = 0;
    out->u = (u8)(digit >= 0 ? g_DrawDigitFontBaseTexU + (digit % 10) * 5 : 88);
    out->v = (u8)(digit >= 0 ? g_DrawDigitFontBaseTexV : 164);
    out->w = 5;
    out->h = 7;
    out->page = PE_TEXT_PAGE_DIGITS;
    out->nibble = 0;
    out->advance = 0;                           /* the number printers move the pen themselves */
    out->code = PE_TEXT_NO_CODE;
}

/* ---- Unicode letters ---- */

/* The game's own letters, codes 00-4C (USA font page). */
static const char kGameLetters[] =
    "0123456789+-\0\0% ABCDEFGHIJKLMNOPQRSTUVWXYZ&!?\"'.abcdefghijklmnopqrstuvwxyz:,/";

int PeText_GameCode(u32 c) {
    unsigned code;
    if (!c || c > 0x7E) return -1;
    for (code = 0; code < sizeof kGameLetters - 1; ++code)
        if ((u8)kGameLetters[code] == c) return (int)code;
    return -1;
}

/* A stretch letter that is one of the game's codes: as in the text itself, so a space in dialogue
 * kerns when the scene asks for it (g_TextboxKernGlyph0F) and a name's never does. */
static int NativeLetter(PeTextStyle style, unsigned code, PeTextLetter *out) {
    if (style != PE_TEXT_MENU) return DialogueNative(code, style == PE_TEXT_DIALOGUE, out);
    MenuNative((int)code, out);
    return 1;
}

int PeText_ResolveLetter(PeTextStyle style, u32 letter, int draw, PeTextLetter *out) {
    PeHookTextLetter ask = {0};
    int code;
    ask.style = (unsigned)style;
    ask.letter = letter;
    ask.draw = draw;
    ask.code = -1;
    if (PE_HOOK(PE_HOOK_TEXT_LETTER, &ask)) {
        if (ask.code < 0) {
            PackCell(style, out, ask.u, ask.v, ask.advance);
            return 1;
        }
        if (NativeLetter(style, (unsigned)ask.code, out)) return 1;
    }
    code = PeText_GameCode(letter);
    if (code >= 0 && NativeLetter(style, (unsigned)code, out)) return 1;
    return NativeLetter(style, 0x2C, out);      /* '?' */
}

/* A two-byte name letter through the provider; 0 when no pack names it. */
static int PagedLetter(PeTextStyle style, unsigned paged, int draw, PeTextLetter *out) {
    PeHookTextLetter ask = {0};
    ask.style = (unsigned)style;
    ask.paged = paged;
    ask.draw = draw;
    ask.code = -1;
    if (!PE_HOOK(PE_HOOK_TEXT_LETTER, &ask)) return 0;
    if (ask.code < 0) {
        PackCell(style, out, ask.u, ask.v, ask.advance);
        return 1;
    }
    return NativeLetter(style, (unsigned)ask.code, out);
}

unsigned PeText_LetterBytes(const u8 *text) {
    if (*text == 0xFF) return 0;
    return PE_TEXT_IS_PAGE(*text) && text[1] < 0xFA ? 2u : 1u;
}

const u8 *PeText_NthLetter(const u8 *text, unsigned n) {
    for (; *text != 0xFF; text += PeText_LetterBytes(text))
        if (!n--) return text;
    return NULL;
}

unsigned PeText_Layout(u32 *letters, unsigned count) {
    (void)letters;
    return count;                               /* left to right */
}

/* ---- Items ---- */

static void One(PeTextItem *item, unsigned bytes, int width) {
    item->bytes = bytes;
    item->count = 1;
    item->advance = item->letter[0].advance;
    item->width = width;
}

/* An F1 C6 stretch: its letters laid out and resolved, placed one after another. */
static int Stretch(PeTextStyle style, unsigned id, int draw, PeTextItem *item) {
    PeHookTextStretch ask = {0};
    u32 letters[PE_TEXT_ITEM_LETTERS];
    unsigned count, i;
    int pen = 0;
    ask.id = id;
    if (!PE_HOOK(PE_HOOK_TEXT_STRETCH, &ask) || !ask.letters) return 0;
    count = ask.count < PE_TEXT_ITEM_LETTERS ? ask.count : PE_TEXT_ITEM_LETTERS;
    for (i = 0; i < count; ++i) letters[i] = ask.letters[i];
    count = PeText_Layout(letters, count);
    item->count = 0;
    for (i = 0; i < count; ++i) {
        PeTextLetter *l = &item->letter[item->count];
        if (!PeText_ResolveLetter(style, letters[i], draw, l)) continue;
        l->x = (s16)(l->x + pen);
        pen += l->advance;
        ++item->count;
    }
    item->bytes = 4;
    item->advance = pen;
    item->width = pen;
    return 1;
}

/* An F1 C5 letter, through PE_HOOK_TEXT_GLYPH as before. */
static int PackLetter(PeTextStyle style, const u8 *text, int draw, PeTextItem *item) {
    PeHookTextGlyph g = {0};
    g.text = text;
    g.draw = draw;
    if (!PE_HOOK(PE_HOOK_TEXT_GLYPH, &g)) return 0;
    PackCell(style, &item->letter[0], g.u, g.v, g.advance);
    One(item, g.bytes ? g.bytes : 4, (int)g.advance);
    return 1;
}

static unsigned ReadDialogue(const u8 *text, unsigned avail, int draw, PeTextItem *item) {
    unsigned b = text[0], n;
    if (avail >= 4 && b == 0xF1) {
        unsigned id = StretchId(text, avail);
        if (id != ~0u && Stretch(PE_TEXT_DIALOGUE, id, draw, item)) return item->bytes;
        if (Pe_ExtendedGlyph(text, NULL) && PackLetter(PE_TEXT_DIALOGUE, text, draw, item))
            return item->bytes;
    }
    switch (b) {
    case 0xF7: case 0xF8: case 0xF9: case 0xFA: case 0xFF:
        return 0;                               /* the box's commands */
    case 0xFB:
        if (avail < 2) return 0;
        n = text[1];
        if (n <= 3) {                           /* a button icon from page 2 */
            DialogueFixed(&item->letter[0], 2, n * 12 + 0x40, g_TextboxFontPages[2].textureY, PE_TEXT_NO_CODE);
            One(item, 2, 12);
            return 2;
        }
        if (n <= 9) return 0;                   /* colours, sections, pauses, numbers, choices */
        {
            unsigned v = ((n + 0xED) / 21) * 12, page = 0;
            if ((v & 0xFFFF) >= 0xF1) { v -= 0xFC; page = 1; }
            DialogueFixed(&item->letter[0], page, ((n + 0xED) % 21) * 12, v, PE_TEXT_NO_CODE);
        }
        One(item, 2, 12);
        return 2;
    case 0xFC: case 0xFD:
        if (avail < 2) return 0;
        {
            unsigned cell = text[1] + (b == 0xFC ? 0x34u : 0x134u);
            DialogueFixed(&item->letter[0], 1, (cell % 21) * 12, (cell / 21) * 12, PE_TEXT_NO_CODE);
        }
        One(item, 2, 12);
        return 2;
    default:
        item->bytes = 1;
        item->count = 0;
        item->advance = item->width = 0;
        if (!DialogueNative(b, 1, &item->letter[0])) { item->stop = 1; return 1; }
        One(item, 1, item->letter[0].advance);
        return 1;
    }
}

/* One menu byte, with the game's page state (g_TextRenderMode) as Draw_AllocTexturedQuad keeps it:
 * a byte FA-FE sets the page for the next one and draws nothing. */
static unsigned ReadMenu(const u8 *text, unsigned avail, int draw, PeTextItem *item) {
    int glyph = text[0];
    if (avail >= 4 && text[0] == 0xF1) {
        unsigned id = StretchId(text, avail);
        if (id != ~0u && Stretch(PE_TEXT_MENU, id, draw, item)) { g_TextRenderMode = 0; return 4; }
        if (Pe_ExtendedGlyph(text, NULL) && PackLetter(PE_TEXT_MENU, text, draw, item)) {
            g_TextRenderMode = 0;
            return item->bytes;
        }
    }
    if (g_TextRenderMode) {
        glyph += g_TextRenderMode << 8;
        g_TextRenderMode = 0;
        /* a two-byte name letter a pack names */
        if (text[0] < 0xFA && glyph > 0x100 && PagedLetter(PE_TEXT_MENU, (unsigned)glyph, draw, &item->letter[0])) {
            One(item, 1, item->letter[0].advance);
            return 1;
        }
    }
    item->bytes = 1;
    item->count = 0;
    item->advance = item->width = 0;
    if (text[0] >= 0xFA) {
        g_TextRenderMode = text[0] - 0xFA;
        return 1;
    }
    MenuNative(glyph, &item->letter[0]);
    One(item, 1, MenuMeasure(glyph));
    return 1;
}

/* A player-name byte in a dialogue box: an F1 C5 / C6 token when four bytes are left, else a game
 * letter; a code past the width table ends the box's drawing. */
static unsigned ReadName(const u8 *text, unsigned avail, int draw, PeTextItem *item) {
    if (avail >= 4 && text[0] == 0xF1) {
        unsigned id = StretchId(text, avail);
        if (id != ~0u && Stretch(PE_TEXT_DIALOGUE_NAME, id, draw, item)) return item->bytes;
        if (Pe_ExtendedGlyph(text, NULL) && PackLetter(PE_TEXT_DIALOGUE, text, draw, item))
            return item->bytes;
    }
    if (avail >= 2 && PE_TEXT_IS_PAGE(text[0]) && text[1] < 0xFA) {
        /* a two-byte name letter: the pack's, else '?' (never the end of the box) */
        unsigned paged = (unsigned)(text[0] - 0xFA) << 8 | text[1];
        if (!PagedLetter(PE_TEXT_DIALOGUE_NAME, paged, draw, &item->letter[0]) &&
            !DialogueNative(0x2C, 0, &item->letter[0])) { item->stop = 1; return 2; }
        One(item, 2, item->letter[0].advance);
        return 2;
    }
    item->bytes = 1;
    item->advance = item->width = 0;
    if (!DialogueNative(text[0], 0, &item->letter[0])) { item->stop = 1; return 1; }
    One(item, 1, item->letter[0].advance);
    return 1;
}

unsigned PeText_Read(PeTextStyle style, const u8 *text, unsigned avail, int draw, PeTextItem *item) {
    item->stop = 0;
    item->count = 0;
    item->advance = item->width = 0;
    if (!text || !avail) return item->bytes = 0;
    switch (style) {
    case PE_TEXT_DIALOGUE: return ReadDialogue(text, avail, draw, item);
    case PE_TEXT_DIALOGUE_NAME: return ReadName(text, avail, draw, item);
    default: return ReadMenu(text, avail, draw, item);
    }
}

int PeText_MeasureMenu(const u8 *text) {
    PeTextItem item;
    int width = 0;
    while (*text != 0xFF) {
        unsigned bytes = PeText_Read(PE_TEXT_MENU, text, ~0u, 0, &item);
        width += item.width;
        text += bytes ? bytes : 1;
    }
    return width;
}
