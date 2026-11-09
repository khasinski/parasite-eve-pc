/* The game's text renderer (pe1/text_render.h): which letters text draws, where, and how far the
 * pen moves. Every letter the dialogue boxes and menus put on screen is placed here; they only turn
 * the result into packets. The game's own letters keep exactly the cells, offsets and spacing the
 * original drawing code gave them. */
#include <string.h>

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

/* The game's own letters, codes 00-4C (USA font page). */
static const char kGameLetters[] =
    "0123456789+-\0\0% ABCDEFGHIJKLMNOPQRSTUVWXYZ&!?\"'.abcdefghijklmnopqrstuvwxyz:,/";

/* The Unicode letter a game code below 4D draws, else 0 (a pack's own codes are asked for only in
 * right-to-left lines, PeText_LineRead). */
static u32 GameLetter(unsigned code) {
    return code < sizeof kGameLetters - 1 ? (u8)kGameLetters[code] : 0;
}

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
    l->dw = 0;
    l->advance = (s16)(-kern - g_TextboxGlyphWidths[code].left + g_TextboxGlyphWidths[code].right);
    l->code = (u16)code;
    l->letter = GameLetter(code);
    return 1;
}

/* A fixed 12 px dialogue cell (FB, FC, FD letters and the button icons). */
static void DialogueFixed(PeTextLetter *l, unsigned page, unsigned u, unsigned v, unsigned code) {
    l->x = l->y = 0;
    l->u = (u8)u; l->v = (u8)v;
    l->w = l->h = 12;
    l->page = (u8)page;
    l->nibble = 0;
    l->dw = 0;
    l->advance = 12;
    l->code = (u16)code;
    l->letter = 0;
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
    l->dw = 0;
        l->advance = 12;
        l->code = PE_TEXT_NO_CODE;
        l->letter = 0;
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
    l->dw = 0;
    l->advance = (s16)(width + spacing);
    l->code = (u16)glyph;
    l->letter = GameLetter((unsigned)glyph);
}

/* A cell the pack put on font page 0 (an F1 C5 letter or a resolved stretch letter), `w` columns
 * of it (0: all 12). */
static void PackCell(PeTextStyle style, PeTextLetter *l, unsigned u, unsigned v, unsigned w, unsigned advance) {
    l->x = 0;
    l->y = style == PE_TEXT_MENU ? 1 : 0;
    l->u = (u8)u; l->v = (u8)v;
    l->w = (u8)(w ? w : 12);
    l->h = 12;
    l->page = 0;
    l->nibble = 0;
    l->dw = 0;
    l->advance = (s16)advance;
    l->code = PE_TEXT_NO_CODE;
    l->letter = 0;
}

void PeText_DialogueDigit(unsigned digit, PeTextLetter *out) {
    DialogueFixed(out, 0, digit * 12, 0, PE_TEXT_NO_CODE);
    out->letter = digit < 10 ? '0' + digit : 0;
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
    out->dw = 0;
    out->advance = 0;                           /* the number printers move the pen themselves */
    out->code = PE_TEXT_NO_CODE;
    out->letter = digit >= 0 ? '0' + (u32)(digit % 10) : 0;
}

/* ---- Unicode letters ---- */

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
            PackCell(style, out, ask.u, ask.v, ask.w, ask.advance);
            out->letter = letter;
            return 1;
        }
        if (NativeLetter(style, (unsigned)ask.code, out)) { out->letter = letter; return 1; }
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
    if (ask.code < 0) PackCell(style, out, ask.u, ask.v, ask.w, ask.advance);
    else if (!NativeLetter(style, (unsigned)ask.code, out)) return 0;
    out->letter = ask.letter;
    return 1;
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

/* Takes up to `need` pixels from `cap` (one count per letter), spread evenly along the line:
 * a pixel from each letter that has one to give, as many rounds as it takes. Returns what is left. */
static int Take(int *cap, int *cut, unsigned count, int need) {
    while (need > 0) {
        unsigned i, m = 0, j = 0;
        int take;
        for (i = 0; i < count; ++i) m += cap[i] > 0;
        if (!m) break;
        take = need < (int)m ? need : (int)m;
        for (i = 0; i < count; ++i) {
            if (cap[i] <= 0) continue;
            if ((int)(((j + 1) * (unsigned)take) / m) > (int)((j * (unsigned)take) / m)) { --cap[i]; ++cut[i]; }
            ++j;
        }
        need -= take;
    }
    return need;
}

/* A narrowed menu line (PE_TEXT_NARROW_FIRST): `pen` px of letters made (pen * narrow + 128) >> 8
 * wide. The room comes from the blank first, so letters keep their shape: spaces down to 4 px,
 * then the gaps between letters, then spaces down to 2 px; only what is still missing comes off
 * the letters themselves (drawn narrower). Returns the new width. */
static int Squeeze(PeTextLetter *letter, unsigned n, const u8 *space, int pen, int narrow) {
    int cap[PE_TEXT_LINE_LETTERS], cut[PE_TEXT_LINE_LETTERS] = {0}, art[PE_TEXT_LINE_LETTERS] = {0};
    int start[PE_TEXT_LINE_LETTERS];
    int need = pen - ((pen * narrow + 128) >> 8), at = 0;
    unsigned i;
    for (i = 0; i < n; ++i) { start[i] = at; at += letter[i].advance; }
    for (i = 0; i < n; ++i) cap[i] = space[i] && letter[i].advance > 4 ? letter[i].advance - 4 : 0;
    need = Take(cap, cut, n, need);
    for (i = 0; i < n; ++i) {
        const PeTextLetter *l = &letter[i];
        int gap = l->advance - (l->x - start[i]) - l->w;
        cap[i] = !space[i] && gap > 0 ? gap : 0;
    }
    need = Take(cap, cut, n, need);
    for (i = 0; i < n; ++i)
        cap[i] = space[i] && letter[i].advance - cut[i] > 2 ? letter[i].advance - cut[i] - 2 : 0;
    need = Take(cap, cut, n, need);
    for (i = 0; i < n; ++i) cap[i] = !space[i] && letter[i].w > 1 ? letter[i].w - 1 : 0;
    Take(cap, art, n, need);
    at = 0;
    for (i = 0; i < n; ++i) {
        PeTextLetter *l = &letter[i];
        l->x = (s16)(at + (l->x - start[i]));
        if (art[i]) l->dw = (u8)(l->w - art[i]);
        at += l->advance - cut[i] - art[i];
    }
    return at;
}

/* An F1 C6 stretch: its letters laid out and resolved, placed one after another. */
static int Stretch(PeTextStyle style, unsigned id, int draw, PeTextItem *item) {
    PeHookTextStretch ask = {0};
    u32 letters[PE_TEXT_ITEM_LETTERS];
    unsigned count, i;
    int pen = 0, narrow = 256;
    u8 space[PE_TEXT_ITEM_LETTERS];
    ask.id = id;
    if (!PE_HOOK(PE_HOOK_TEXT_STRETCH, &ask) || !ask.letters) return 0;
    count = ask.count < PE_TEXT_ITEM_LETTERS ? ask.count : PE_TEXT_ITEM_LETTERS;
    for (i = 0; i < count; ++i) letters[i] = ask.letters[i];
    count = PeText_Layout(letters, count);
    item->count = 0;
    for (i = 0; i < count; ++i) {
        PeTextLetter *l = &item->letter[item->count];
        if (letters[i] >= PE_TEXT_NARROW_FIRST && letters[i] <= PE_TEXT_NARROW_LAST) {
            if (style == PE_TEXT_MENU) narrow = (int)(letters[i] - PE_TEXT_NARROW_FIRST) + 128;
            continue;
        }
        if (!PeText_ResolveLetter(style, letters[i], draw, l)) continue;
        space[item->count] = letters[i] == ' ';
        l->x = (s16)(l->x + pen);
        pen += l->advance;
        ++item->count;
    }
    if (narrow != 256) pen = Squeeze(item->letter, item->count, space, pen, narrow);
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
    PackCell(style, &item->letter[0], g.u, g.v, 0, g.advance);
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
    if (PeText_RightToLeft()) {                 /* joined letters are narrower */
        static PeTextLine line;
        PeText_LineStart(&line);
        while (*text != 0xFF) {
            unsigned bytes = PeText_LineRead(&line, PE_TEXT_MENU, text, ~0u, 0, &item);
            text += bytes ? bytes : 1;
        }
        return PeText_LineFinish(&line, 0);
    }
    while (*text != 0xFF) {
        unsigned bytes = PeText_Read(PE_TEXT_MENU, text, ~0u, 0, &item);
        width += item.width;
        text += bytes ? bytes : 1;
    }
    return width;
}

/* ---- Right-to-left lines ---- */

/* Arabic letters' joined shapes: the alone shape's presentation form, and how many forms follow it
 * (alone, end, start, middle in that order; 2 for a letter that joins only the one before it). */
typedef struct ArabicForms { u16 letter, alone; u8 forms; } ArabicForms;
static const ArabicForms kArabicForms[] = {
    {0x0621, 0xFE80, 1}, {0x0622, 0xFE81, 2}, {0x0623, 0xFE83, 2}, {0x0624, 0xFE85, 2},
    {0x0625, 0xFE87, 2}, {0x0626, 0xFE89, 4}, {0x0627, 0xFE8D, 2}, {0x0628, 0xFE8F, 4},
    {0x0629, 0xFE93, 2}, {0x062A, 0xFE95, 4}, {0x062B, 0xFE99, 4}, {0x062C, 0xFE9D, 4},
    {0x062D, 0xFEA1, 4}, {0x062E, 0xFEA5, 4}, {0x062F, 0xFEA9, 2}, {0x0630, 0xFEAB, 2},
    {0x0631, 0xFEAD, 2}, {0x0632, 0xFEAF, 2}, {0x0633, 0xFEB1, 4}, {0x0634, 0xFEB5, 4},
    {0x0635, 0xFEB9, 4}, {0x0636, 0xFEBD, 4}, {0x0637, 0xFEC1, 4}, {0x0638, 0xFEC5, 4},
    {0x0639, 0xFEC9, 4}, {0x063A, 0xFECD, 4}, {0x0641, 0xFED1, 4}, {0x0642, 0xFED5, 4},
    {0x0643, 0xFED9, 4}, {0x0644, 0xFEDD, 4}, {0x0645, 0xFEE1, 4}, {0x0646, 0xFEE5, 4},
    {0x0647, 0xFEE9, 4}, {0x0648, 0xFEED, 2}, {0x0649, 0xFEEF, 2}, {0x064A, 0xFEF1, 4},
    /* Persian and Urdu letters */
    {0x067E, 0xFB56, 4}, {0x0686, 0xFB7A, 4}, {0x0698, 0xFB8A, 2}, {0x06A9, 0xFB8E, 4},
    {0x06AF, 0xFB92, 4}, {0x06CC, 0xFBFC, 4},
};

static const ArabicForms *FormsOf(u32 c) {
    unsigned i;
    for (i = 0; i < sizeof kArabicForms / sizeof *kArabicForms; ++i)
        if (kArabicForms[i].letter == c) return &kArabicForms[i];
    return NULL;
}

/* A vowel mark or other combining mark: left out of the line. */
static int IsMark(u32 c) {
    return (c >= 0x0300 && c <= 0x036F) || (c >= 0x0591 && c <= 0x05BD) || c == 0x05BF ||
           c == 0x05C1 || c == 0x05C2 || c == 0x05C4 || c == 0x05C5 || c == 0x05C7 ||
           (c >= 0x0610 && c <= 0x061A) || (c >= 0x064B && c <= 0x065F) || c == 0x0670 ||
           (c >= 0x06D6 && c <= 0x06DC) || (c >= 0x06DF && c <= 0x06E4) || c == 0x06E7 ||
           c == 0x06E8 || (c >= 0x06EA && c <= 0x06ED);
}

/* How a letter joins: 'D' both sides, 'R' only the letter before it, 'C' joins both and keeps its
 * shape (tatweel, zero width joiner), 'U' not at all. */
static int JoinType(u32 c) {
    const ArabicForms *f;
    if (c == 0x0640 || c == 0x200D) return 'C';
    f = FormsOf(c);
    if (!f || f->forms == 1) return 'U';
    return f->forms == 4 ? 'D' : 'R';
}

/* Lam then this alef is one letter: its alone form (the end form follows), else 0. */
static u32 LamAlef(u32 alef) {
    switch (alef) {
    case 0x0622: return 0xFEF5;
    case 0x0623: return 0xFEF7;
    case 0x0625: return 0xFEF9;
    case 0x0627: return 0xFEFB;
    default: return 0;
    }
}

/* Joins in place: keep[i] 0 for a mark and the alef of lam-alef; joins_next[i] 1 when letter i
 * touches the next kept letter. */
static void JoinLetters(u32 *letters, unsigned n, u8 *keep, u8 *joins_next) {
    u8 type[PE_TEXT_LINE_LETTERS];
    unsigned i;
    if (n > PE_TEXT_LINE_LETTERS) n = PE_TEXT_LINE_LETTERS;
    for (i = 0; i < n; ++i) {
        keep[i] = !IsMark(letters[i]);
        type[i] = (u8)(keep[i] ? JoinType(letters[i]) : 'U');
        joins_next[i] = 0;
    }
    for (i = 0; i < n; ++i) {
        int p = (int)i - 1, t = type[i], before, after;
        unsigned q = i + 1;
        const ArabicForms *f;
        if (!keep[i]) continue;
        if (t == 'U') {                         /* hamza: its one shape */
            f = FormsOf(letters[i]);
            if (f) letters[i] = f->alone;
            continue;
        }
        while (p >= 0 && !keep[p]) --p;
        while (q < n && !keep[q]) ++q;
        before = p >= 0 && (type[p] == 'D' || type[p] == 'C');
        if (letters[i] == 0x0644 && q < n && LamAlef(letters[q])) {
            letters[i] = LamAlef(letters[q]) + (before ? 1u : 0u);
            keep[q] = 0;
            type[i] = 'R';                      /* the pair joins only the letter before it */
            continue;
        }
        after = (t == 'D' || t == 'C') && q < n && (type[q] == 'R' || type[q] == 'D' || type[q] == 'C');
        joins_next[i] = (u8)after;
        if (t == 'C') continue;
        f = FormsOf(letters[i]);
        if (before && after) letters[i] = f->alone + 3u;
        else if (before) letters[i] = f->alone + 1u;
        else if (after) letters[i] = f->alone + 2u;
        else letters[i] = f->alone;
    }
}

unsigned PeText_Join(u32 *letters, unsigned count, u8 *joins_next) {
    u8 keep[PE_TEXT_LINE_LETTERS], next[PE_TEXT_LINE_LETTERS];
    unsigned i, m = 0;
    if (count > PE_TEXT_LINE_LETTERS) count = PE_TEXT_LINE_LETTERS;
    JoinLetters(letters, count, keep, next);
    for (i = 0; i < count; ++i) {
        if (!keep[i]) continue;
        letters[m] = letters[i];
        if (joins_next) joins_next[m] = next[i];
        ++m;
    }
    return m;
}

/* The bidirectional classes this needs (Unicode Bidi_Class, simplified). */
enum { B_L, B_R, B_AL, B_EN, B_ES, B_ET, B_AN, B_CS, B_NSM, B_WS, B_ON };

static int BidiClass(u32 c) {
    if (!c) return B_ON;                        /* an icon or game symbol */
    if (c >= '0' && c <= '9') return B_EN;
    if (c < 0x80) {
        if ((c | 0x20) >= 'a' && (c | 0x20) <= 'z') return B_L;
        switch (c) {
        case '+': case '-': return B_ES;
        case '#': case '$': case '%': return B_ET;
        case ',': case '.': case ':': case '/': return B_CS;
        case ' ': case '\t': return B_WS;
        default: return B_ON;
        }
    }
    if (c == 0xA0) return B_CS;
    if ((c >= 0xA2 && c <= 0xA5) || c == 0xB0 || c == 0xB1 || c == 0x2030 || c == 0x066A ||
        (c >= 0x20A0 && c <= 0x20CF))
        return B_ET;
    if (c == 0xAA || c == 0xB5 || c == 0xBA) return B_L;
    if (c < 0xC0) return B_ON;
    if (c == 0xD7 || c == 0xF7) return B_ON;
    if (IsMark(c)) return B_NSM;
    if (c >= 0x0660 && c <= 0x0669) return B_AN;
    if (c == 0x066B || c == 0x066C) return B_AN;
    if (c >= 0x06F0 && c <= 0x06F9) return B_EN;
    if (c == 0x060C) return B_CS;
    if ((c >= 0x0590 && c <= 0x05FF) || (c >= 0xFB1D && c <= 0xFB4F) || c == 0x200F) return B_R;
    if ((c >= 0x0600 && c <= 0x06FF) || (c >= 0x0750 && c <= 0x077F) || (c >= 0x08A0 && c <= 0x08FF) ||
        (c >= 0xFB50 && c <= 0xFDFF) || (c >= 0xFE70 && c <= 0xFEFE))
        return B_AL;
    if (c >= 0x2000 && c <= 0x200A) return B_WS;
    if (c == 0x200E) return B_L;
    if ((c >= 0x2010 && c <= 0x2027) || (c >= 0x2030 && c <= 0x205E) || c == 0x2212) return c == 0x2212 ? B_ES : B_ON;
    if (c >= 0x3000 && c <= 0x303F) return c == 0x3000 ? B_WS : B_ON;
    if (c >= 0xFF01 && c <= 0xFF0F) return B_ON;
    return B_L;
}

u32 PeText_Mirror(u32 c) {
    static const u16 pairs[][2] = {
        {'(', ')'}, {'[', ']'}, {'{', '}'}, {'<', '>'}, {0xAB, 0xBB}, {0x2039, 0x203A},
    };
    unsigned i;
    for (i = 0; i < sizeof pairs / sizeof *pairs; ++i) {
        if (c == pairs[i][0]) return pairs[i][1];
        if (c == pairs[i][1]) return pairs[i][0];
    }
    return c;
}

/* A neutral's neighbour as N1 sees it: numbers count as right to left. */
static int StrongOf(int cls) { return cls == B_L ? B_L : B_R; }

void PeText_Order(const u32 *letters, unsigned n, unsigned *order, u8 *mirrored) {
    u8 cls[PE_TEXT_LINE_LETTERS], level[PE_TEXT_LINE_LETTERS];
    unsigned i, j;
    int last;
    if (n > PE_TEXT_LINE_LETTERS) n = PE_TEXT_LINE_LETTERS;
    for (i = 0; i < n; ++i) cls[i] = (u8)BidiClass(letters[i]);
    /* W1: a mark takes the class before it (the line starts right to left) */
    for (i = 0; i < n; ++i)
        if (cls[i] == B_NSM) cls[i] = i ? cls[i - 1] : B_R;
    /* W2, W3: a number after Arabic letters is an Arabic number; Arabic letters are R */
    for (i = 0, last = B_R; i < n; ++i) {
        if (cls[i] == B_L || cls[i] == B_R || cls[i] == B_AL) last = cls[i];
        else if (cls[i] == B_EN && last == B_AL) cls[i] = B_AN;
    }
    for (i = 0; i < n; ++i)
        if (cls[i] == B_AL) cls[i] = B_R;
    /* W4: one separator between two numbers of a kind joins them */
    for (i = 1; i + 1 < n; ++i) {
        if (cls[i] == B_ES && cls[i - 1] == B_EN && cls[i + 1] == B_EN) cls[i] = B_EN;
        else if (cls[i] == B_CS && cls[i - 1] == cls[i + 1] && (cls[i - 1] == B_EN || cls[i - 1] == B_AN))
            cls[i] = cls[i - 1];
    }
    /* W5: currency and percent signs next to a number go with it */
    for (i = 0; i < n; i = j) {
        if (cls[i] != B_ET) { j = i + 1; continue; }
        for (j = i; j < n && cls[j] == B_ET; ++j) {}
        if ((i && cls[i - 1] == B_EN) || (j < n && cls[j] == B_EN))
            for (; i < j; ++i) cls[i] = B_EN;
    }
    /* W6: other separators are neutral */
    for (i = 0; i < n; ++i)
        if (cls[i] == B_ES || cls[i] == B_ET || cls[i] == B_CS) cls[i] = B_ON;
    /* W7: a number after English letters reads as English */
    for (i = 0, last = B_R; i < n; ++i) {
        if (cls[i] == B_L || cls[i] == B_R) last = cls[i];
        else if (cls[i] == B_EN && last == B_L) cls[i] = B_L;
    }
    /* N1, N2: neutrals take the direction on both sides when it is the same, else right to left */
    for (i = 0; i < n; i = j) {
        int before, after, dir;
        if (cls[i] != B_ON && cls[i] != B_WS) { j = i + 1; continue; }
        for (j = i; j < n && (cls[j] == B_ON || cls[j] == B_WS); ++j) {}
        before = i ? StrongOf(cls[i - 1]) : B_R;
        after = j < n ? StrongOf(cls[j]) : B_R;
        dir = before == after ? before : B_R;
        for (; i < j; ++i) cls[i] = (u8)dir;
    }
    /* I2: right to left at level 1; English and numbers at level 2 */
    for (i = 0; i < n; ++i) level[i] = (u8)(cls[i] == B_R ? 1 : 2);
    /* L1: spaces at the end of the line are at the line's level */
    for (i = n; i > 0 && BidiClass(letters[i - 1]) == B_WS; --i) level[i - 1] = 1;
    /* L2: reverse each level 2 run, then the whole line */
    for (i = 0; i < n; ++i) order[i] = i;
    for (i = 0; i < n; i = j) {
        if (level[i] != 2) { j = i + 1; continue; }
        for (j = i; j < n && level[j] == 2; ++j) {}
        {
            unsigned a = i, b = j - 1;
            while (a < b) { unsigned t = order[a]; order[a++] = order[b]; order[b--] = t; }
        }
    }
    for (i = 0, j = n ? n - 1 : 0; i < j; ++i, --j) { unsigned t = order[i]; order[i] = order[j]; order[j] = t; }
    if (mirrored)
        for (i = 0; i < n; ++i)
            mirrored[i] = (u8)(level[order[i]] == 1 && PeText_Mirror(letters[order[i]]) != letters[order[i]]);
}

int PeText_RightToLeft(void) {
    PeHookTextScript ask = {0};
    ask.code = ~0u;
    return PE_HOOK(PE_HOOK_TEXT_SCRIPT, &ask) && ask.rtl;
}

void PeText_LineStart(PeTextLine *line) {
    line->count = 0;
    line->width = 0;
    line->narrow = 256;
}

static void LinePush(PeTextLine *line, PeTextStyle style, const PeTextLetter *l, u32 letter, u32 tag, int again) {
    unsigned n = line->count;
    if (n >= PE_TEXT_LINE_LETTERS) return;
    if (l) line->letter[n] = *l;
    else memset(&line->letter[n], 0, sizeof line->letter[n]);
    line->letter[n].letter = letter;
    line->uni[n] = letter;
    line->tag[n] = tag;
    line->style[n] = (u8)style;
    line->again[n] = (u8)again;
    line->count = n + 1;
}

void PeText_LineAdd(PeTextLine *line, PeTextStyle style, const PeTextLetter *l, u32 tag) {
    LinePush(line, style, l, l->letter, tag, 0);
}

/* The Unicode letter a pack's one-byte code draws. */
static u32 PackCodeLetter(unsigned code) {
    PeHookTextScript ask = {0};
    ask.code = code;
    return PE_HOOK(PE_HOOK_TEXT_SCRIPT, &ask) ? ask.letter : 0;
}

/* The Unicode letter of a two-byte name letter, without drawing it. */
static u32 PagedUnicode(PeTextStyle style, unsigned paged) {
    PeHookTextLetter ask = {0};
    ask.style = (unsigned)style;
    ask.paged = paged;
    ask.code = -1;
    return PE_HOOK(PE_HOOK_TEXT_LETTER, &ask) ? ask.letter : 0;
}

unsigned PeText_LineRead(PeTextLine *line, PeTextStyle style, const u8 *text, unsigned avail, u32 tag,
                         PeTextItem *item) {
    unsigned i, bytes, paged = 0, id;
    item->stop = 0;
    item->count = 0;
    item->advance = item->width = 0;
    if (!text || !avail) return item->bytes = 0;
    /* a stretch: its letters, resolved once the line is joined */
    id = StretchId(text, avail);
    if (id != ~0u) {
        PeHookTextStretch ask = {0};
        ask.id = id;
        if (PE_HOOK(PE_HOOK_TEXT_STRETCH, &ask) && ask.letters) {
            for (i = 0; i < ask.count; ++i) {
                u32 c = ask.letters[i];
                if (c >= PE_TEXT_NARROW_FIRST && c <= PE_TEXT_NARROW_LAST) {
                    if (style == PE_TEXT_MENU) line->narrow = (int)(c - PE_TEXT_NARROW_FIRST) + 128;
                    continue;
                }
                LinePush(line, style, NULL, c, tag, 1);
            }
            if (style == PE_TEXT_MENU) g_TextRenderMode = 0;
            return item->bytes = 4;
        }
    }
    /* a two-byte name letter: by its Unicode letter too, so a name joins */
    bytes = 0;
    if (style == PE_TEXT_DIALOGUE_NAME && avail >= 2 && PE_TEXT_IS_PAGE(text[0]) && text[1] < 0xFA) {
        paged = (unsigned)(text[0] - 0xFA) << 8 | text[1];
        bytes = 2;
    } else if (style == PE_TEXT_MENU && g_TextRenderMode && text[0] < 0xFA) {
        paged = (unsigned)g_TextRenderMode << 8 | text[0];
        bytes = paged > 0x100 ? 1 : 0;
    }
    if (bytes) {
        u32 c = PagedUnicode(style, paged);
        if (c) {
            if (style == PE_TEXT_MENU) g_TextRenderMode = 0;
            LinePush(line, style, NULL, c, tag, 1);
            return item->bytes = bytes;
        }
    }
    bytes = PeText_Read(style, text, avail, 1, item);
    if (!bytes || item->stop) return bytes;
    for (i = 0; i < item->count; ++i) {
        const PeTextLetter *l = &item->letter[i];
        u32 c = l->letter;
        if (!c && l->code >= 0x4D && l->code < 0xF1) c = PackCodeLetter(l->code);
        LinePush(line, style, l, c, tag, 0);
    }
    return bytes;
}

int PeText_LineFinish(PeTextLine *line, int draw) {
    PeTextLetter out[PE_TEXT_LINE_LETTERS];
    u32 tags[PE_TEXT_LINE_LETTERS], was[PE_TEXT_LINE_LETTERS];
    unsigned order[PE_TEXT_LINE_LETTERS];
    u8 keep[PE_TEXT_LINE_LETTERS], next[PE_TEXT_LINE_LETTERS], mirrored[PE_TEXT_LINE_LETTERS];
    u8 space[PE_TEXT_LINE_LETTERS];
    unsigned n = line->count, i, k, m = 0;
    int pen = 0;
    memcpy(was, line->uni, n * sizeof *was);
    JoinLetters(line->uni, n, keep, next);
    /* each letter as it now draws; marks and the alef of lam-alef go */
    for (i = 0; i < n; ++i) {
        PeTextLetter l;
        if (!keep[i]) continue;
        if (line->again[i] || line->uni[i] != was[i]) {
            if (!PeText_ResolveLetter((PeTextStyle)line->style[i], line->uni[i], draw, &l)) continue;
            line->letter[m] = l;
        } else line->letter[m] = line->letter[i];
        line->uni[m] = line->uni[i];
        line->tag[m] = line->tag[i];
        line->style[m] = line->style[i];
        next[m] = next[i];
        ++m;
    }
    PeText_Order(line->uni, m, order, mirrored);
    for (k = 0; k < m; ++k) {
        PeTextLetter l;
        i = order[k];
        if (mirrored[k] &&
            PeText_ResolveLetter((PeTextStyle)line->style[i], PeText_Mirror(line->uni[i]), draw, &l))
            line->letter[i] = l;
    }
    /* left to right; a letter that joins the one on its right touches it */
    for (k = 0; k < m; ++k) {
        PeTextLetter l = line->letter[order[k]];
        int advance = l.advance;
        i = order[k];
        if (k + 1 < m) {
            unsigned r = order[k + 1];
            const PeTextLetter *right = &line->letter[r];
            if (r + 1 == i && next[r] && l.code == PE_TEXT_NO_CODE && right->code == PE_TEXT_NO_CODE) {
                int touch = l.x + l.w - 1 - right->x;   /* the last column is the shadow */
                if (touch < advance) advance = touch;
            }
        }
        out[k] = l;
        out[k].x = (s16)(l.x + pen);
        out[k].advance = (s16)advance;
        tags[k] = line->tag[i];
        space[k] = line->uni[i] == ' ';
        pen += advance;
    }
    if (line->narrow != 256) pen = Squeeze(out, m, space, pen, line->narrow);
    memcpy(line->letter, out, m * sizeof *out);
    memcpy(line->tag, tags, m * sizeof *tags);
    line->count = m;
    line->width = pen;
    return pen;
}

/* ---- Credits roll ---- */

/* USA alphabet widths: columns 0/1 for regular upper/lowercase and column 2 for style 2; odd
 * style 3 uses the authored zero column. A lowercase letter reads its own row (letter - 'a') at
 * column style | 1: the game's lowercase table sat 0x80 bytes before this one, at the same rows.
 * (boot_display.h declares it; the credits disc test checks it against the disc's copy.) */
const u8 g_BootDisplayUppercaseWidths[26][4]={
    {9,6,10,0},
    {7,6,8,0},
    {8,6,10,0},
    {9,7,10,0},
    {7,6,8,0},
    {7,5,8,0},
    {9,6,10,0},
    {10,7,11,0},
    {5,4,7,0},
    {7,5,7,0},
    {9,6,10,0},
    {8,4,9,0},
    {10,10,12,0},
    {9,7,11,0},
    {9,6,11,0},
    {7,7,8,0},
    {10,6,12,0},
    {8,5,10,0},
    {6,5,8,0},
    {9,4,10,0},
    {9,7,10,0},
    {9,6,10,0},
    {11,9,13,0},
    {9,7,10,0},
    {8,6,10,0},
    {7,5,8,0},
};

/* Punctuation sits in consecutive cells after the letters, '~' first; anything else takes the
 * cell before them, as the game's fallthrough switch gave it. */
static int CreditPunctuation(u8 c) {
    static const char order[] = "~!@#&()-',./?:";
    unsigned i;
    for (i = 0; i < sizeof order - 1; ++i)
        if ((u8)order[i] == c) return (int)i;
    return -1;
}

unsigned PeText_CreditCell(unsigned style, u8 c) {
    int index;
    if ((u16)style) {
        if ((u8)(c - 'a') < 26) index = c - 32;
        else if ((u8)(c - 'A') < 26) index = c - 65;
        else if ((u8)(c - '0') < 10) index = c + 58;
        else index = CreditPunctuation(c) + 26;
    } else {
        if ((u8)(c - 'A') < 26) index = c - 25;
        else if ((u8)(c - 'a') < 26) index = c - 31;
        else if ((u8)(c - '0') < 10) index = c + 58;
        else index = CreditPunctuation(c) + 92;
    }
    return (u8)index;
}

unsigned PeText_CreditWidth(unsigned style, u8 c) {
    style = (u16)style;
    if (style > 3) return 0;
    if (style == 2) {
        u8 upper = (u8)(c - 'a') < 26 ? (u8)(c - 32) : c;
        if ((u8)(upper - 'A') < 26) return g_BootDisplayUppercaseWidths[upper - 'A'][2];
        switch (upper) {
        case '!': case '\'': case '.': case ':': return 3;
        case '#': return 7;
        case '&': return 11;
        case '(': case ')': case ',': case '-': case '/': return 4;
        case '?': return 6;
        case '@': return 9;
        default: return 8;                      /* '~' and the rest */
        }
    }
    if ((u8)(c - 'A') < 26) return g_BootDisplayUppercaseWidths[c - 'A'][style];
    if ((u8)(c - 'a') < 26) return g_BootDisplayUppercaseWidths[c - 'a'][style | 1];
    switch (c) {
    case '!': case '.': case ':': return 2;
    case '\'': case ',': return 3;
    case '(': case ')': case '-': case '/': case '?': return 4;
    case '&': case '@': return 9;
    default: return 6;                          /* '~' and the rest */
    }
}

int PeText_ReadCredit(PeTextCreditPen *pen, const u8 *text, PeTextLetter *out) {
    unsigned c = *text, cell, width;
    const u8 *scan;
    switch (c) {
    case '\\':                                  /* centre what follows */
        width = 0;
        for (scan = text + 1; *scan; ++scan) width += PeText_CreditWidth(pen->style, *scan);
        pen->x = (320u - width) >> 1;
        return 0;
    case '\t':
        pen->x += PeText_CreditWidth(pen->style, ' ') * 14;
        return 0;
    case ' ':
        pen->x += PeText_CreditWidth(pen->style, ' ');
        return 0;
    }
    cell = PeText_CreditCell(pen->style, (u8)c);
    width = PeText_CreditWidth(pen->style, (u8)c);
    out->x = (s16)pen->x;
    out->y = 0;
    out->u = (u8)((cell & 15) << 4);
    out->v = (u8)(cell & 0xF0);
    out->w = out->h = 16;
    out->page = PE_TEXT_PAGE_CREDITS;
    out->nibble = 0;
    out->dw = 0;
    out->advance = (s16)width;
    out->code = (u16)c;
    out->letter = c;
    pen->x += width;
    return 1;
}
