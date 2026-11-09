/* Challenges: a notice in the game's own dialogue window over the field.
 *
 * Two lines and one option (a choice of one, so the game's own window takes Cross and closes the
 * way Wayne's menu does), typed out at once. The box is one of the game's four dialogue boxes
 * (g_TextboxEntries) filled the way pe_shops' Warner menu fills his: a window style, a page id no
 * room message uses, our text in the game's glyph code (pe_gametext.h). It opens from
 * PE_HOOK_TEXTBOXES, before the boxes draw, on the first frame the field is free and no other box
 * is up. When it closes (its state or page changes: Cross, a room change), `closed` runs once.
 * If it can't open within CH_NOTICE_WAIT frames, `closed` runs anyway so a caller never waits
 * forever.
 *
 * Aya isn't held while it shows; callers that need that hold the field themselves.
 */
#include <string.h>

#include "pe_challenges.h"
#include "pe_game.h"
#include "pe_gametext.h"
#include "pe_mod_host.h"
#include "pe_mod_menu.h"
#include "pe1/textbox.h"
#include "pe1/textbox_open.h"

#define CH_NOTICE_PAGE   0x7F10   /* no room message uses it (Warner's box is 0x7F00) */
#define CH_TEXTBOX_SHOWN 2
#define CH_NOTICE_WAIT   600      /* frames: ten seconds */
#define CH_BOX_X         40
#define CH_BOX_Y         80
#define CH_BOX_W         240
#define CH_BOX_H         66       /* three lines in the window style */

static u8 s_text[256];
static TextboxEntry *s_box;
static void (*s_closed)(void);
static int s_pending, s_wait;

int pe_ch_notice_busy(void) { return s_pending || s_box; }

int pe_ch_notice_show(const char *line1, const char *line2, const char *option, void (*closed)(void)) {
    PeText t;
    if (pe_ch_notice_busy()) return 0;
    pe_t_init(&t);
    pe_t_str(&t, line1);
    if (line2 && *line2) { pe_t_byte(&t, G_NEWLINE); pe_t_str(&t, line2); }
    pe_t_byte(&t, G_NEWLINE);
    pe_t_choice(&t, 1);
    pe_t_option(&t, 1);
    pe_t_str(&t, option);
    pe_t_byte(&t, G_END);
    memcpy(s_text, t.b, t.n < sizeof s_text ? t.n : sizeof s_text);
    s_text[sizeof s_text - 1] = G_END;
    s_closed = closed;
    s_pending = 1;
    s_wait = CH_NOTICE_WAIT;
    return 1;
}

static void pe_finish(void) {
    void (*closed)(void) = s_closed;
    s_box = NULL;
    s_pending = 0;
    s_closed = NULL;
    if (closed) closed();
}

static int pe_any_box_up(void) {
    for (int i = 0; i < 4; ++i) if (g_TextboxEntries[i].state != 0) return 1;
    return 0;
}

static void pe_open(void) {
    TextboxEntry *e = &g_TextboxEntries[0];
    e->page_id = CH_NOTICE_PAGE;
    e->style = 1;                                   /* the game's window, as Wayne's menu */
    e->background = 0;
    e->x = CH_BOX_X;
    e->y = CH_BOX_Y;
    e->width = CH_BOX_W;
    e->height = CH_BOX_H;
    e->control.flags &= ~0x000F0000u & ~0x00100000u & ~0x00200000u;
    e->control.parts.delay = 0;                     /* typed out at once */
    e->control.parts.elapsed = 0;
    e->message = s_text;
    e->state = CH_TEXTBOX_SHOWN;
    g_DialogueChoiceCursor = 0;
    g_MenuEquipSlotIndex = -1;
    s_box = e;
    s_pending = 0;
    pe_log("challenge_notice", "shown");
}

static int pe_notice_textboxes(void *ctx, void *user) {
    (void)ctx; (void)user;
    if (s_box) {
        if (s_box->state != CH_TEXTBOX_SHOWN || s_box->page_id != CH_NOTICE_PAGE) pe_finish();
        return 0;
    }
    if (!s_pending) return 0;
    if (pe_game_loaded() && !pe_in_battle() && pe_free_field() && !pe_any_box_up()) pe_open();
    return 0;
}

/* The wait for a moment to open counts field frames (the textbox hook may not run at all). */
static int pe_notice_frame(void *ctx, void *user) {
    (void)ctx; (void)user;
    if (s_pending && --s_wait <= 0) {
        pe_log("challenge_notice", "no moment to open: carried on without it");
        pe_finish();
    }
    return 0;
}

/* A load or a new game drops a box that never opened (its caller hears it closed). */
static int pe_notice_reset(void *ctx, void *user) {
    (void)ctx; (void)user;
    s_box = NULL;
    return 0;
}

/* F1 > Challenges > Debug: the Hardcore notice as it looks, closing to nothing (no title, no run
 * change). It opens once F1 is closed, on the first free field frame. */
static void pe_notice_preview(void) {
    if (!pe_game_loaded()) { pe_toast("Load a game first"); return; }
    if (!pe_ch_notice_show("This Hardcore run has ended.", "It fell after 12 battles, 1:02:03.", "Close (preview)", NULL))
        pe_toast("A notice is already waiting");
    else
        pe_toast("Close F1: the notice opens in the field");
}

void pe_ch_notice_register(const char *mod_id) {
    pe_hook_add(PE_HOOK_TEXTBOXES, pe_notice_textboxes, NULL, PE_PRIORITY_EARLY, mod_id);
    pe_hook_add(PE_HOOK_FRAME, pe_notice_frame, NULL, PE_PRIORITY_NORMAL, mod_id);
    pe_hook_add(PE_HOOK_NEW_GAME, pe_notice_reset, NULL, PE_PRIORITY_NORMAL, mod_id);
    pe_menu_register_action("Debug", "Preview the Hardcore notice",
        "Shows the fallen-run message in the field, closing to nothing. No run or save changes.", pe_notice_preview);
}
