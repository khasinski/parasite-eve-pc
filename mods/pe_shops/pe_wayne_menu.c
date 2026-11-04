/*
 * The Workshop in Wayne's own menu, ported from the psxrecomp build (src/mods/pe_wayne_menu.c;
 * bridge findings pe-loot-overhaul-design-2026-10-11.md decisions 27, 31, 47).
 *
 * Wayne's menu is a script choice box: a dialogue box (g_TextboxEntries) showing a room message
 * such as "<FB 09 04>    Store equipment<F7>    Store item<F7>    Discard junk<F7>    Cancel".
 * FB 09 n opens a choice of n options (3 bits, so at most 7); Menu_DrawTextboxEntries moves the
 * cursor (g_DialogueChoiceCursor) and, on Cross, hands the result to the script.
 *
 * At the entry of Menu_DrawTextboxEntries (PE_HOOK_TEXTBOXES), every frame:
 *   - a box showing Wayne's menu (a choice with "Discard junk", or "Hold equipment" on the ship)
 *     is pointed at our copy: "Workshop" above Cancel, and "Discard junk <your Junk>, <his
 *     count>/300" (plus "bag full" when a hand-in has no free slot, or "Day 4" when he holds 300
 *     but can't build yet);
 *   - Cross on Workshop is taken from the game and opens the Workshop screen
 *     (pe_workshop_screen.c: Requisition, Rebuild, Wardrobe, Mod Permit Card). His box stays open
 *     -- the script is waiting for its answer -- below the screen, out of sight, and comes back
 *     where it was when the screen closes;
 *   - Cross on our Cancel moves the cursor up first, so the script gets its own Cancel.
 */
#include <stdio.h>
#include <string.h>

#include "pe_shops.h"
#include "pe_game.h"
#include "pe_gametext.h"
#include "pe_translate.h"
#include "pe_mod_host.h"
#include "pe_requisition.h"
#include "pe1/pad_state.h"
#include "pe1/textbox.h"

#define PE_TEXTBOX_SHOWN    2      /* state: fully typed out, choices active */
#define PE_IN_CONFIRM       0x0100u
#define PE_CHOICE_MAX       7
#define PE_SCREEN_RIGHT     312    /* boxes stay inside the screen */
#define PE_OFF_SCREEN_Y     0x1F0  /* below the 240-line screen: his box out of sight, still open */

extern u8 g_DialogueChoiceCursor;

int pe_workshop_screen_open(void);    /* pe_workshop_screen.c */
int pe_workshop_screen_busy(void);

static struct {
    int shown;                    /* our copy is in his box */
    TextboxEntry *entry;          /* the box we drive */
    u8 *orig_msg;                 /* Wayne's own message */
    u16 orig_y, orig_h, orig_w;   /* his box, before ours grows it by the Workshop row */
    int junk_option;              /* identified from the original page, before translation */
    int wayne_count;              /* options in Wayne's own menu */
    int hidden;                   /* moved off screen while the Workshop screen is up */
    u16 shown_y;                  /* where it was */
} s_ui;

static u8 s_buf[PE_GAMETEXT_MAX];   /* our message */
static u8 s_orig[PE_GAMETEXT_MAX];  /* copy of Wayne's */
static unsigned s_orig_len;

static int pe_contains(const u8 *b, unsigned n, const char *ascii) {
    const unsigned m = (unsigned)strlen(ascii);
    unsigned i, k;
    for (i = 0; i + m <= n; ++i) {
        for (k = 0; k < m && b[i + k] == pe_glyph(ascii[k]); ++k) {}
        if (k == m) return 1;
    }
    return 0;
}

const u8 *pe_field_wayne_text(void);   /* pe_field_wayne.c: Warner's version of his menu */

/* Wayne's menu is recognised by its English page ("Discard junk", "Hold equipment"). A language
 * pack (pe_lang) shows another text for that page, so PE_HOOK_TEXT_PAGE notes the page while it
 * is still the game's (before pe_lang) and the text its box will draw (after every handler). */
static int s_page_is_wayne;
static int s_page_junk_option, s_wayne_junk_option = -1;
static const u8 *s_wayne_page;   /* what a box shows for Wayne's menu page, in whatever language */

static int pe_english_wayne(const u8 *msg) {
    unsigned n = 0;
    while (n < PE_GAMETEXT_MAX - 1 && msg[n] != G_END) ++n;
    return pe_contains(msg, n, "Discard junk") || pe_contains(msg, n, "Hold equipment");
}

/* Find the hand-in option by its original text. The translated label is never used
 * as an action identifier, so Junk totals survive any wording in a language pack. */
static int pe_junk_option(const u8 *msg) {
    unsigned i = 0, start;
    int row = 0;
    while (i + 2 < PE_GAMETEXT_MAX && msg[i] != G_END && !(msg[i] == G_CODE && msg[i + 1] == G_CHOICE)) ++i;
    if (i + 2 >= PE_GAMETEXT_MAX || msg[i] == G_END) return -1;
    i += 3;
    for (start = i; i < PE_GAMETEXT_MAX; ++i) {
        if (msg[i] != G_NEWLINE && msg[i] != G_END) continue;
        if (pe_contains(msg + start, i - start, "Discard junk")) return row;
        if (msg[i] == G_END) break;
        ++row; start = i + 1;
    }
    return -1;
}

static int pe_on_page_game(void *ctx, void *user) {
    PeHookTextPage *page = ctx;
    (void)user;
    s_page_is_wayne = page->message && *page->message && pe_english_wayne(*page->message);
    s_page_junk_option = s_page_is_wayne ? pe_junk_option(*page->message) : -1;
    return 0;
}

static int pe_on_page_drawn(void *ctx, void *user) {
    PeHookTextPage *page = ctx;
    (void)user;
    if (page->message && s_page_is_wayne) { s_wayne_page = *page->message; s_wayne_junk_option = s_page_junk_option; }
    else if (page->message && *page->message == s_wayne_page) s_wayne_page = NULL;   /* reused for another page */
    s_page_is_wayne = 0;
    return 0;
}

/* Is `msg` Wayne's menu (or Warner's)? Copies it into s_orig if so. */
static int pe_is_wayne_menu(const u8 *msg) {
    unsigned n = 0, choice_at = PE_GAMETEXT_MAX;
    int count;
    const int known = msg && (msg == pe_field_wayne_text() || msg == s_wayne_page);
    if (!msg || msg == s_buf) return 0;
    while (n < PE_GAMETEXT_MAX - 1 && msg[n] != G_END) {
        if (n >= 2 && msg[n - 2] == G_CODE && msg[n - 1] == G_CHOICE && choice_at == PE_GAMETEXT_MAX) choice_at = n;
        ++n;
    }
    if (choice_at == PE_GAMETEXT_MAX || n >= PE_GAMETEXT_MAX - 1) return 0;
    if (!known && !pe_english_wayne(msg)) return 0;
    count = msg[choice_at] & 7;
    if (count < 2 || count + 1 > PE_CHOICE_MAX) return 0;    /* room for Workshop */
    memcpy(s_orig, msg, n + 1);
    s_orig_len = n;
    s_ui.junk_option = pe_junk_option(msg);
    if (s_ui.junk_option < 0 && msg == s_wayne_page) s_ui.junk_option = s_wayne_junk_option;
    s_ui.wayne_count = count;
    return 1;
}

static void pe_build_wayne(PeText *t) {
    unsigned i = 0, k, start;
    int option = 0;
    while (i < s_orig_len && !(s_orig[i] == G_CODE && i + 1 < s_orig_len && s_orig[i + 1] == G_CHOICE)) pe_t_byte(t, s_orig[i++]);
    i += 3;                                              /* FB 09 n */
    pe_t_choice(t, s_ui.wayne_count + 1);
    for (start = i; i <= s_orig_len; ++i) {
        if (i < s_orig_len && s_orig[i] != G_NEWLINE) continue;
        if (option == s_ui.wayne_count - 1) {           /* his last option is Cancel */
            pe_t_ui(t, "    "); pe_t_tr(t, "mod/pe_economy/wayne/workshop", "Workshop", 24);
            pe_t_byte(t, G_NEWLINE);
        }
        for (k = start; k < i; ++k) pe_t_byte(t, s_orig[k]);
        if (option == s_ui.junk_option) {
            pe_t_ui(t, " ");
            pe_t_num(t, (unsigned)pe_shopw_junk());
            pe_t_ui(t, ", ");
            pe_t_num(t, pe_wayne_count());
            pe_t_ui(t, "/300");
            if (pe_shopw_handin_blocked()) pe_t_tr(t, "mod/pe_economy/wayne/bag-full", " bag full", 24);
            else if (pe_shopw_wayne_waiting()) pe_t_tr(t, "mod/pe_economy/wayne/day-four", " Day 4", 24);
        }
        if (i < s_orig_len) pe_t_byte(t, G_NEWLINE);
        ++option;
        start = i + 1;
    }
}

/* Put our copy of his menu in his box, sized to fit (growing upwards), the cursor on `cursor`. */
static void pe_show(int cursor) {
    PeText t;
    TextboxEntry *e = s_ui.entry;
    unsigned height, bottom, width;
    pe_t_init(&t);
    pe_build_wayne(&t);
    pe_t_byte(&t, G_END);
    memcpy(s_buf, t.b, t.n);
    height = (unsigned)t.lines * 12u + 10u;
    bottom = (unsigned)s_ui.orig_y + s_ui.orig_h;
    width = t.widest * 9u + 16u;
    if (width < s_ui.orig_w) width = s_ui.orig_w;
    if (e->x + width > PE_SCREEN_RIGHT) width = PE_SCREEN_RIGHT - e->x;
    e->message = s_buf;
    e->height = (u16)height;
    e->y = (u16)(bottom > height ? bottom - height : 0);
    e->width = (u16)width;
    if (s_ui.hidden) {                                    /* still behind the Workshop screen */
        s_ui.shown_y = e->y;
        e->y = PE_OFF_SCREEN_Y;
    }
    s_ui.shown = 1;
    g_DialogueChoiceCursor = (u8)cursor;
}

static void pe_reset(void) {
    if (s_ui.hidden && s_ui.entry) s_ui.entry->y = s_ui.shown_y;   /* never leave a box below the screen */
    pe_shop_at_counter(0);
    memset(&s_ui, 0, sizeof s_ui);
}

static uint32_t pe_input(uint32_t pressed, int cursor) {
    const int workshop = s_ui.wayne_count - 1;        /* just above Cancel */
    if (!(pressed & PE_IN_CONFIRM)) return 0;
    if (cursor == workshop) { pe_workshop_screen_open(); return PE_IN_CONFIRM; }
    if (cursor == workshop + 1) g_DialogueChoiceCursor = (u8)(s_ui.wayne_count - 1);   /* the script's own Cancel */
    return 0;
}

/* ---- every frame, before the boxes draw ---- */

static int pe_on_textboxes(void *ctx, void *user) {
    uint32_t taken;
    int i;
    (void)ctx;
    (void)user;
    if (!pe_game_loaded()) return 0;
    if (s_ui.shown) {                                      /* still driving our box? */
        TextboxEntry *e = s_ui.entry;
        if (e->state != PE_TEXTBOX_SHOWN || (e->message != s_buf && e->message != s_ui.orig_msg)) pe_reset();
        else if (e->message == s_ui.orig_msg) pe_show(g_DialogueChoiceCursor);
    }
    if (!s_ui.shown) {
        if (!pe_shopw_enabled()) return 0;
        for (i = 0; i < 4; ++i) {
            TextboxEntry *e = &g_TextboxEntries[i];
            if (e->state != PE_TEXTBOX_SHOWN || !pe_is_wayne_menu(e->message)) continue;
            s_ui.entry = e;
            s_ui.orig_msg = e->message;
            s_ui.orig_y = e->y;
            s_ui.orig_h = e->height;
            s_ui.orig_w = e->width;
            pe_show(g_DialogueChoiceCursor);
            break;
        }
        if (!s_ui.shown) return 0;
    }
    pe_shop_at_counter(1);                                 /* his box is the shop counter */
    if (pe_workshop_screen_busy()) {                       /* the Workshop screen has the pad */
        /* and the screen: his box would show through it, so it waits below the screen, still
         * open (the script is waiting for its answer), and comes back where it was. */
        if (!s_ui.hidden) {
            s_ui.hidden = 1;
            s_ui.shown_y = s_ui.entry->y;
            s_ui.entry->y = PE_OFF_SCREEN_Y;
        }
        g_FieldPadPressed = 0;
        return 0;
    }
    if (s_ui.hidden) {
        s_ui.hidden = 0;
        s_ui.entry->y = s_ui.shown_y;
    }
    taken = pe_input(g_FieldPadPressed, (s8)g_DialogueChoiceCursor);
    if (taken) g_FieldPadPressed &= ~taken;                /* the game never sees it */
    return 0;
}

static int pe_on_reset(void *ctx, void *user) {
    (void)ctx;
    (void)user;
    pe_reset();
    s_wayne_page = NULL;          /* a new room's bank can load where the last one was */
    return 0;
}

void pe_wayne_menu_register(const char *mod_id) {
    pe_hook_add(PE_HOOK_TEXTBOXES, pe_on_textboxes, NULL, PE_PRIORITY_NORMAL, mod_id);
    /* around every other page handler (pe_lang's is EARLY): the game's page, then the drawn one */
    pe_hook_add(PE_HOOK_TEXT_PAGE, pe_on_page_game, NULL, PE_PRIORITY_EARLY - 100, mod_id);
    pe_hook_add(PE_HOOK_TEXT_PAGE, pe_on_page_drawn, NULL, PE_PRIORITY_LATE + 100, mod_id);
    pe_hook_add(PE_HOOK_SCENE_START, pe_on_reset, NULL, PE_PRIORITY_NORMAL, mod_id);
    pe_hook_add(PE_HOOK_SAVE_READ, pe_on_reset, NULL, PE_PRIORITY_NORMAL, mod_id);
}

#if PE_NATIVE_STATE_CPU_COMPONENTS
#include "pe_checkpoint_wire.h"
#include "pe_checkpoint_economy.h"
int pe_wayne_menu_checkpoint_walk(const unsigned char *in,unsigned char *out,int apply){if((!in&&!out)||s_ui.shown)return 0;if(apply&&(!in||!pe_wayne_menu_checkpoint_walk(in,0,0)))return 0;PeCheckpointWire w={in,out,0,apply,1};

if(apply&&w.ok){memset(&s_ui,0,sizeof s_ui);s_orig_len=0;memset(s_buf,0,sizeof s_buf);memset(s_orig,0,sizeof s_orig);}return w.ok&&w.at==0u;}
#endif
