#ifndef PE1_TEXT_RENDER_H
#define PE1_TEXT_RENDER_H

/* The game's text renderer: the one place that decides which letters a piece of text draws, where
 * each goes and how far the pen moves. The dialogue box (textbox.c) and the menus (text.c) only turn
 * the placed letters into their own packets; colour, dimming, palette and ordering stay theirs.
 *
 * Text is the game's own letter bytes, plus two 4-byte tokens a language pack owns (operands below
 * 240, so never a terminator or command):
 *   F1 C5 hi lo   one letter from the pack's letter list (older packs: Chinese, Japanese, Korean)
 *   F1 C6 hi lo   a stretch: a run of Unicode letters the pack holds, drawn whole, so a sentence
 *                 takes 4 bytes in the game's fixed text fields however many letters it has
 * Stretch letters pass through PeText_Layout (left to right today; the place right-to-left order
 * and joined letters go) and each is resolved by PeText_ResolveLetter (the game's own letter when
 * it has one, else the pack's, else '?'). */

#include "common.h"

typedef enum PeTextStyle {
    PE_TEXT_DIALOGUE, /* dialogue boxes and battle lines (Menu_DrawTextboxEntries) */
    PE_TEXT_MENU,     /* menus, lists, save and name screens (Draw_PrintRawText and the rest) */
    PE_TEXT_DIALOGUE_NAME, /* the player's name in a dialogue box (FA): letters only, no commands */
    PE_TEXT_STYLE_COUNT
} PeTextStyle;

/* Where a letter's pixels are. Dialogue: g_TextboxFontPages[page] for pages 0-2. Menu: page 0 is
 * the font page (tpage 0x140, CLUT 0x89C); PE_TEXT_PAGE_SPRITE is a menu sprite, Draw_AllocSprite(u). */
enum { PE_TEXT_PAGE_DIGITS = 14, PE_TEXT_PAGE_SPRITE = 15 }; /* menu digits: the small number art */

typedef struct PeTextLetter {
    s16 x, y;          /* the drawn rectangle's top left, from the pen */
    u8 u, v, w, h;     /* source in its page; drawn w x h */
    u8 page;
    u8 nibble;         /* menu: the cell's first column (Draw_LookupGlyphMetrics low nibble) */
    u8 dw;             /* menu: drawn this wide when the line is narrowed (0: w, as cut) */
    s16 advance;       /* how far the pen moves after it */
    u16 code;          /* the game's code for it (PS5 tagging), or PE_TEXT_NO_CODE */
    u32 letter;        /* the Unicode letter it shows when known (right-to-left order), else 0 */
} PeTextLetter;
#define PE_TEXT_NO_CODE 0xFFFFu
#define PE_TEXT_DRAWN_W(l) ((l)->dw ? (l)->dw : (l)->w)

/* Most letters one item gives (a stretch). The pack tool keeps stretches within it. */
#define PE_TEXT_ITEM_LETTERS 96

typedef struct PeTextItem {
    unsigned bytes;    /* text bytes the item takes; 0 = not a letter (the caller's command) */
    unsigned count;    /* letters in it, 0..PE_TEXT_ITEM_LETTERS */
    int advance;       /* how far the pen moves after the whole item */
    int width;         /* what measuring a line counts for it (menus: the original measure) */
    int stop;          /* dialogue: a code past the width table, which ends the box's drawing */
    PeTextLetter letter[PE_TEXT_ITEM_LETTERS];
} PeTextItem;

/* Reads one item at `text` (at most `avail` bytes; ~0u when the text ends in FF). `draw` 0 is for
 * measuring and never puts a letter in video memory. Menu items take any page byte (FA-FF) with
 * the letter after it. Returns item->bytes. */
unsigned PeText_Read(PeTextStyle style, const u8 *text, unsigned avail, int draw, PeTextItem *item);

/* A menu line's width as the menus measure it (to FF). */
int PeText_MeasureMenu(const u8 *text);

/* Digits the number printers place themselves: a dialogue FB 08 number's, and the menus' small
 * 5 x 7 digits (Draw_EmitDigitSprite; a negative digit is its blank). */
void PeText_DialogueDigit(unsigned digit, PeTextLetter *out);
void PeText_MenuDigit(int digit, PeTextLetter *out);

/* ---- Stretches: the seams later work plugs into ---- */
#define PE_TEXT_NO_LETTER 0xFFFFFFFFu

/* A stretch may start with a narrowing mark, U+10FE80 + k (k 0-127): the line is made
 * (width * (128 + k) + 128) >> 8 px wide, so a line the pack tool found too wide for its room fits.
 * The room comes from spaces and the gaps between letters first; letters are drawn narrower only
 * for what is still missing. Menus only (their letters are quads); a dialogue box ignores it. */
#define PE_TEXT_NARROW_FIRST 0x10FE80u
#define PE_TEXT_NARROW_LAST 0x10FEFFu

/* A stretch whose only letter is U+10FE7F marks where a message box (Menu_CreateTwoLineDialog,
 * Menu_CreateNotificationDialog) starts a new line, so a line too wide for the screen goes on two
 * at full size; the box grows a line taller. Anywhere else it draws nothing. */
#define PE_TEXT_LINE_BREAK 0x10FE7Fu

/* 4 when `text` is a line break stretch (above), else 0. */
unsigned PeText_LineBreakAt(const u8 *text, unsigned avail);

/* Puts a stretch's letters (Unicode, in reading order) in drawing order, in place; returns the
 * count. Left to right: a pack that reads right to left draws whole lines instead (below), since
 * one line may be several stretches, a name and numbers. */
unsigned PeText_Layout(u32 *letters, unsigned count);

/* ---- Right-to-left lines ----
 * When the pack in use reads right to left (PE_HOOK_TEXT_SCRIPT), the dialogue box and the menus
 * read a whole line into a PeTextLine, in reading order, and draw it once it is laid out:
 *  - Arabic letters take their joined shapes (alone, start, middle, end; the Unicode presentation
 *    forms, U+FE70-FEFF, which the pack draws), and lam then alef becomes one letter. Letters that
 *    join touch: the gap between them is closed, so a pack draws joining letters cut to their
 *    columns. Vowel marks (Hebrew points, Arabic harakat) are left out.
 *  - Letters go in drawing order by the Unicode bidirectional rules, simplified for one line of
 *    right-to-left text: Hebrew and Arabic run right to left; English words and names, and numbers
 *    (with their signs, separators, %, and the box's number command) keep left to right; spaces and
 *    punctuation between take the direction of what is on both sides, else right to left; brackets
 *    turn round in right-to-left text. Button icons and game symbols count as punctuation.
 * English and left-to-right packs never come here, so they draw exactly as before. */
#define PE_TEXT_LINE_LETTERS 160

typedef struct PeTextLine {
    unsigned count;
    int width;                              /* PeText_LineFinish: the laid-out line's width */
    int narrow;                             /* a menu line's narrowing, 256 = none */
    PeTextLetter letter[PE_TEXT_LINE_LETTERS]; /* finished: drawing order, x from the left edge */
    u32 tag[PE_TEXT_LINE_LETTERS];          /* the caller's, per letter (the dialogue colour) */
    /* reading order, until finished */
    u32 uni[PE_TEXT_LINE_LETTERS];          /* the letter; 0 for an icon or symbol */
    u8 style[PE_TEXT_LINE_LETTERS];
    u8 again[PE_TEXT_LINE_LETTERS];         /* resolved by its Unicode letter when finished */
} PeTextLine;

/* Nonzero when the pack in use reads right to left. */
int PeText_RightToLeft(void);
void PeText_LineStart(PeTextLine *line);
/* Reads one item at `text` as PeText_Read does, its letters going into the line with `tag`.
 * Returns the bytes taken; 0 for the caller's command; item->stop as PeText_Read. */
unsigned PeText_LineRead(PeTextLine *line, PeTextStyle style, const u8 *text, unsigned avail, u32 tag,
                         PeTextItem *item);
/* Adds a letter the caller placed itself (a number's digit, an icon); its `letter` says how it
 * orders ('0'-'9' keep together left to right, 0 is punctuation). */
void PeText_LineAdd(PeTextLine *line, PeTextStyle style, const PeTextLetter *l, u32 tag);
/* Joins, orders and places the line's letters (draw 0: measuring, nothing uploaded); returns the
 * width. Afterwards letter[] and tag[] are in drawing order. */
int PeText_LineFinish(PeTextLine *line, int draw);

/* The joining step alone, on letters in reading order, in place: each Arabic letter becomes its
 * joined shape, lam then alef one letter, vowel marks are dropped. Returns the new count;
 * joins_next (may be NULL) says which letters touch the one after. localization/petext/rtl.py
 * follows the same rules, so the pack tool measures lines as they are drawn. */
unsigned PeText_Join(u32 *letters, unsigned count, u8 *joins_next);
/* The bidirectional step alone: `order` gets the reading-order index of each letter in drawing
 * order, left to right; `mirrored` (may be NULL) is set for letters drawn turned round. */
void PeText_Order(const u32 *letters, unsigned count, unsigned *order, u8 *mirrored);
/* A bracket's turned-round partner, or the letter itself. */
u32 PeText_Mirror(u32 letter);

/* One Unicode letter as the style draws it: the game's own letter when it has one, else the
 * language pack's (PE_HOOK_TEXT_LETTER), else '?'. Returns 0 only when not even '?' can be drawn. */
int PeText_ResolveLetter(PeTextStyle style, u32 letter, int draw, PeTextLetter *out);

/* The game letter code for a Unicode letter in the built-in English set, or -1. */
int PeText_GameCode(u32 letter);

/* ---- Names ----
 * A name (the player's, its default, the name entry grid) may hold two-byte letters: a page byte
 * FB-FE, then a byte below FA. The game counts them as one letter (Save_AppendMetadataChar), as
 * the Japanese game stored kana and kanji. A language pack says which Unicode letter each is
 * (PE_HOOK_TEXT_LETTER with `paged`); with none, a dialogue box shows '?'. */
#define PE_TEXT_IS_PAGE(b) ((b) >= 0xFB && (b) <= 0xFE)

/* Bytes in the letter at `text` (1 or 2; 0 at FF). */
unsigned PeText_LetterBytes(const u8 *text);
/* The `n`th letter of `text` (counting from 0), or NULL past its end. */
const u8 *PeText_NthLetter(const u8 *text, unsigned n);

/* ---- Credits roll ----
 * The staff roll (boot display) has its own 16 x 16 letter art and widths, in four width styles
 * (g_BootDisplayFontStyle; any other style measures 0). Its lines are plain ASCII from the disc,
 * ending in 0: '\' centres the rest of the line on the 320 px screen, a tab moves 14 spaces on, a
 * space moves one on without drawing. The renderer places every letter; the boot display's two
 * drawers (sprites, or letters brightened into the picture) only put them on screen. */
enum { PE_TEXT_PAGE_CREDITS = 13 };            /* the credits art: 16 cells a row, 16 px each */
#define PE_TEXT_CREDIT_LEFT 24                 /* where each credits line starts */

typedef struct PeTextCreditPen {
    unsigned style;    /* g_BootDisplayFontStyle */
    u32 x;             /* the screen column the next letter goes at */
} PeTextCreditPen;

/* The credits widths (boot_display.h names it too; the credits disc test checks it). */
extern const u8 g_BootDisplayUppercaseWidths[26][4];

/* The credits art cell for a letter: low nibble the column, high nibble the row. */
unsigned PeText_CreditCell(unsigned style, u8 letter);
/* How far the pen moves after a credits letter. */
unsigned PeText_CreditWidth(unsigned style, u8 letter);
/* Reads the byte at `text` (not the line's 0) and moves the pen. Returns 1 and fills `out` (x is
 * the screen column, code the letter) when the byte draws a letter, 0 for a space, '\' or tab. */
int PeText_ReadCredit(PeTextCreditPen *pen, const u8 *text, PeTextLetter *out);

#endif
