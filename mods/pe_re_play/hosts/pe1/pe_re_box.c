/* Messages in the game's dialogue band. See pe_re_box.h. Same game side as the phone mod's box
 * (pe_phone_box.c), kept here so the two mods stay apart:
 *
 * Box: one of the game's four dialogue boxes, page id 0x7F02 (no room message uses it; the phone
 * uses 0x7F01), the band style at the bottom (x 20, y 174), typed out at once.
 * Field lock: field event 0xFE (0xFF unlocks), sent from Aya as a talking actor's script does.
 * Pad: taken right after it is read (PE_HOOK_FRAME, late), before any field logic, and handed back
 * when the boxes draw (PE_HOOK_TEXTBOXES) with no presses, so the Cross that turns a page doesn't
 * also reach the field (and the room's next zone). */
#include <stdio.h>
#include <string.h>

#include "pe_re_box.h"
#include "../../re_host.h"

#include "pe_game.h"
#include "pe_gametext.h"
#include "pe_hooks.h"
#include "pe_mod_host.h"
#include "pe1/field_movement.h"
#include "pe1/field_sfx.h"
#include "pe1/pad_state.h"
#include "pe1/textbox.h"
#include "pe1/textbox_open.h"

#define PAGE_ID      0x7F02
#define BOX_SHOWN    2
#define FIELD_LOCK   0xFE
#define FIELD_UNLOCK 0xFF
#define BAND_X       0x14
#define BAND_Y       0xAE
#define LINE_W       272                 /* widest line in pixels (the game's own run to about 275) */
#define LINES        3                   /* lines a page */
#define PAGES        8
#define PAGE_BYTES   192
#define PAD_CROSS    0x0100u             /* g_FieldPadPressed */

static unsigned char s_page[PAGES][PAGE_BYTES];
static int s_pages, s_at = -1, s_locked, s_taken, s_fresh;
static TextboxEntry *s_box;
static u32 s_pressed, s_bits;
static int s_choosing, s_count, s_cur, s_result = -1;
static char s_title[48], s_labels[RE_HOST_DIALOGUE_CHOICES][41];
static u16 s_raw, s_raw_new;                 /* the pad as PS1 reads it (active high), new presses */
#define RAW_RIGHT 0x0020u
#define RAW_LEFT 0x0080u
#define RAW_TRIANGLE 0x1000u
#define RAW_CIRCLE 0x2000u

static int font_ready(void) { return g_TextboxGlyphWidths && g_TextboxGlyphCount; }
static int advance(unsigned char g) {
    if (!font_ready() || g >= g_TextboxGlyphCount) return 12;
    return (int)g_TextboxGlyphWidths[g].right - (int)g_TextboxGlyphWidths[g].left;
}

/* Words onto lines no wider than LINE_W, LINES to a page. */
static void layout(const char *text) {
    int line = 0, width = 0, n = 0;
    s_pages = 0;
    memset(s_page, 0, sizeof s_page);
    while (*text && s_pages < PAGES) {
        while (*text == ' ') text++;
        if (!*text) break;
        const char *end = text;
        int w = 0;
        while (*end && *end != ' ') w += advance(pe_glyph(*end++));
        const int space = width ? advance(pe_glyph(' ')) : 0;
        if (width && width + space + w > LINE_W) {               /* the next line, or the next page */
            if (++line >= LINES) {
                s_page[s_pages][n] = G_END;
                if (++s_pages >= PAGES) break;
                line = 0, n = 0;
            } else if (n + 1 < PAGE_BYTES) {
                s_page[s_pages][n++] = G_NEWLINE;
            }
            width = 0;
        } else if (width && n + 1 < PAGE_BYTES) {
            s_page[s_pages][n++] = pe_glyph(' ');
            width += space;
        }
        for (; text < end; text++) if (n + 2 < PAGE_BYTES) s_page[s_pages][n++] = pe_glyph(*text);   /* a word longer than a line runs on */
        width += w;
    }
    if (s_pages < PAGES && n) s_page[s_pages++][n] = G_END;
}

static int box_open(void) { return s_box && s_box->state == BOX_SHOWN && s_box->page_id == PAGE_ID; }

static int show(int page) {
    TextboxEntry *e = box_open() ? s_box : NULL;
    for (int i = 0; !e && i < 4; ++i) if (g_TextboxEntries[i].state == 0) e = &g_TextboxEntries[i];
    if (!e) return 0;
    e->page_id = PAGE_ID;
    e->style = 0;
    e->background = 1;
    e->x = BAND_X, e->y = BAND_Y, e->width = 280, e->height = 54;
    e->control.flags &= ~(0x000F0000u | 0x00100000u | 0x00200000u | 0x01C00000u | 0x02000000u);
    e->control.parts.delay = 0;
    e->control.parts.elapsed = 0;
    e->message = s_page[page];
    e->state = BOX_SHOWN;
    g_DialogueChoiceCursor = 0;
    g_MenuEquipSlotIndex = -1;
    s_box = e, s_at = page;
    return 1;
}

static void field_event(int value) {
    BattleEntity *aya = pe_player();
    Script_QueueFieldSfx(value, 0, 0, aya ? aya->fieldSfxId : 0, 0);
}

static void close_box(void) {
    if (box_open()) s_box->state = 0;
    s_box = NULL, s_at = -1, s_choosing = 0;
    if (s_locked) field_event(FIELD_UNLOCK), s_locked = 0;
}

int pe_re_box_busy(void) { return s_at >= 0; }

int pe_re_box_say(const char *text) {
    if (!font_ready() || !text || !*text) {
        pe_mod_log(NULL, "re play: no box: %s", !font_ready() ? "the font isn't loaded" : "nothing to say");
        return 0;
    }
    close_box();
    layout(text);
    if (!s_pages || !show(0)) {
        int used = 0;
        for (int i = 0; i < 4; ++i) used += g_TextboxEntries[i].state != 0;
        pe_mod_log(NULL, "re play: no box: %d pages, %d of 4 boxes in use", s_pages, used);
        s_at = -1;
        return 0;
    }
    if (!s_locked) field_event(FIELD_LOCK), s_locked = 1;
    s_fresh = 1;                                            /* the Cross that opened it isn't a page turn */
    return 1;
}

/* the choice's one page: title, the label between arrows, the hint */
static void compose(void) {
    char line[3][96];
    int n = 0;
    snprintf(line[0], sizeof line[0], "%s  %d/%d", s_title, s_cur + 1, s_count);
    snprintf(line[1], sizeof line[1], "  - %s -", s_labels[s_cur]);
    snprintf(line[2], sizeof line[2], "Left/Right, Cross: choose, Triangle: back");
    memset(s_page, 0, sizeof s_page);
    for (int l = 0; l < 3; l++) {
        if (l && n + 1 < PAGE_BYTES) s_page[0][n++] = G_NEWLINE;
        for (const char *p = line[l]; *p && n + 2 < PAGE_BYTES; p++) s_page[0][n++] = pe_glyph(*p);
    }
    s_page[0][n] = G_END;
    s_pages = 1;
}

int pe_re_box_choose(const char *title, const char *const labels[], int n, int at) {
    if (!font_ready() || n <= 0) return 0;
    close_box();
    s_count = n > RE_HOST_DIALOGUE_CHOICES ? RE_HOST_DIALOGUE_CHOICES : n;
    s_cur = at >= 0 && at < s_count ? at : 0;
    snprintf(s_title, sizeof s_title, "%s", title);
    for (int i = 0; i < s_count; i++) snprintf(s_labels[i], sizeof s_labels[i], "%s", labels[i]);
    compose();
    if (!show(0)) { s_at = -1; return 0; }
    s_choosing = 1, s_result = -1;
    if (!s_locked) field_event(FIELD_LOCK), s_locked = 1;
    s_fresh = 1;
    return 1;
}

int pe_re_box_choice(void) {
    const int r = s_result;
    s_result = -1;
    return r;
}

void pe_re_box_forget(void) {
    if (box_open()) s_box->state = 0;
    s_box = NULL, s_at = -1, s_locked = 0, s_taken = 0, s_choosing = 0;    /* a room load starts the field afresh */
}

/* Right after the pad is read, before any field logic: while a box is up, the pad is ours. */
static int on_frame(void *ctx, void *user) {
    (void)ctx, (void)user;
    const u16 raw = (u16)~g_PadPackets[0].buttons;
    s_raw_new = (u16)(raw & ~s_raw), s_raw = raw;
    if (s_at < 0) return 0;
    if (s_fresh) s_raw_new = 0;
    s_pressed = s_fresh ? 0 : g_FieldPadPressed, s_bits = g_FieldPadBits;
    s_fresh = 0;
    g_FieldPadPressed = 0;
    g_FieldPadBits = 0;
    s_taken = 1;
    return 0;
}

/* Before the boxes draw (after the field logic): turn the page, close, hand the pad back. */
static int on_textboxes(void *ctx, void *user) {
    (void)ctx, (void)user;
    if (s_at < 0) return 0;
    const u32 pressed = s_taken ? s_pressed : 0;
    if (!box_open()) close_box();                           /* someone else took the box */
    else if (s_choosing) {
        const u16 raw = s_taken ? s_raw_new : 0;
        if (pressed & PAD_CROSS) s_result = s_cur, close_box();
        else if (raw & (RAW_TRIANGLE | RAW_CIRCLE)) s_result = -2, close_box();
        else if (raw & (RAW_LEFT | RAW_RIGHT)) {
            s_cur = (s_cur + (raw & RAW_RIGHT ? 1 : s_count - 1)) % s_count;
            compose();
            show(0);
        }
    } else if (pressed & PAD_CROSS) {
        if (s_at + 1 < s_pages && show(s_at + 1)) {}
        else close_box();
    }
    if (s_taken) g_FieldPadBits = s_bits, g_FieldPadPressed = 0, s_taken = 0;   /* held as they were, no presses */
    return 0;
}

void pe_re_box_init(const char *mod_id) {
    pe_hook_add(PE_HOOK_FRAME, on_frame, NULL, PE_PRIORITY_LATE, mod_id);
    pe_hook_add(PE_HOOK_TEXTBOXES, on_textboxes, NULL, PE_PRIORITY_NORMAL, mod_id);
}
