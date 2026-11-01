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
 *     is pointed at our copy: "Mod Permit Card" and "Workshop" above Cancel, and "Discard junk
 *     <your Junk>, <his count>/300" (plus "bag full" when a hand-in has no free slot, or "Day 4"
 *     when he holds 300 but can't build yet);
 *   - Cross on Workshop is taken from the game and the box shows our screens instead: Workshop
 *     (Requisition / Rebuild / Wardrobe while a mod offers one / Back), Requisition and Rebuild
 *     lists (5 a page, Left/Right turn pages; rows that can't be bought now are dimmed) and Mod
 *     Permit Card (the bag's guns and armor with their slots; Yes / No before a permit is spent
 *     for one more slot -- vanilla's Torres tune-up without the risk). The title line shows
 *     your Junk or what the last action did. Circle goes back a screen;
 *   - Cross on our Cancel moves the cursor up first, so the script gets its own Cancel.
 * Wayne's Chrysler menu already has 6 options, so there only Workshop is added and Mod Permit
 * moves onto the Workshop screen.
 *
 * Dimmed rows use the text box's red highlight codes, drawn in the menus' dimmed gray while our
 * screens are up (g_TextboxHighlightColor*, restored afterwards).
 */
#include <stdio.h>
#include <string.h>

#include "pe_economy.h"
#include "pe_game.h"
#include "pe_gametext.h"
#include "pe_translate.h"
#include "pe_mod_host.h"
#include "pe_requisition.h"
#include "pe1/game_timers.h"
#include "pe1/pad_state.h"
#include "pe1/textbox.h"

#define PE_TEXTBOX_SHOWN    2      /* state: fully typed out, choices active */
#define PE_IN_CONFIRM       0x0100u
#define PE_IN_CANCEL        0x0400u
#define PE_IN_LEFT          0x0040u
#define PE_IN_RIGHT         0x0010u
#define PE_PAGE_SIZE        5
#define PE_CHOICE_MAX       7
#define PE_ITEM_MOD_PERMIT  0x37
#define PE_BAG_SLOTS        50
#define PE_STORAGE_SLOTS    100
#define PE_MAX_MOD_SLOTS    10
#define PE_DIM_GRAY         0x40   /* the menus' dimmed text: 0x80 >> 1 */
#define PE_SCREEN_RIGHT     312    /* boxes stay inside the screen */

extern u8 g_DialogueChoiceCursor;

enum { SCR_NONE, SCR_WAYNE, SCR_ROOT, SCR_REQ, SCR_REB, SCR_PERMIT, SCR_PERMIT_ASK, SCR_WARD };
enum { ROOT_REQ, ROOT_REB, ROOT_WARD, ROOT_PERMIT, ROOT_BACK };

static struct {
    int screen;
    TextboxEntry *entry;          /* the box we drive */
    u8 *orig_msg;                 /* Wayne's own message */
    u16 orig_y, orig_h, orig_w;
    int junk_option;              /* identified from the original page, before translation */
    int wayne_count;              /* options in Wayne's own menu */
    int page;
    char note[48];                /* title line after an action */
    int pick;                     /* Mod Permit: the bag slot being asked about */
    int root[5], root_count;      /* the Workshop screen's options, ROOT_* */
} s_ui;

static u8 s_buf[PE_GAMETEXT_MAX];   /* our message */
static u8 s_orig[PE_GAMETEXT_MAX];  /* copy of Wayne's */
static unsigned s_orig_len;
static struct { int saved; u8 r, g, b; } s_highlight;

static const PeWardrobeApi *pe_wardrobe(void) { return (const PeWardrobeApi *)pe_service_get("wardrobe", 1); }

/* ---- Wayne's menu ---- */

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

/* With 6 options of his own there's room for Workshop only; Mod Permit moves inside. */
static int pe_compact(void) { return s_ui.wayne_count + 2 > PE_CHOICE_MAX; }

static void pe_build_wayne(PeText *t) {
    unsigned i = 0, k, start;
    int option = 0;
    while (i < s_orig_len && !(s_orig[i] == G_CODE && i + 1 < s_orig_len && s_orig[i + 1] == G_CHOICE)) pe_t_byte(t, s_orig[i++]);
    i += 3;                                              /* FB 09 n */
    pe_t_choice(t, s_ui.wayne_count + (pe_compact() ? 1 : 2));
    for (start = i; i <= s_orig_len; ++i) {
        if (i < s_orig_len && s_orig[i] != G_NEWLINE) continue;
        if (option == s_ui.wayne_count - 1) {           /* his last option is Cancel */
            if (!pe_compact()) { pe_t_ui(t, "    "); pe_t_tr(t, "mod/pe_economy/wayne/permit", "Mod Permit Card", 24); pe_t_byte(t, G_NEWLINE); }
            pe_t_ui(t, "    "); pe_t_tr(t, "mod/pe_economy/wayne/workshop", "Workshop", 24);
            pe_t_byte(t, G_NEWLINE);
        }
        for (k = start; k < i; ++k) pe_t_byte(t, s_orig[k]);
        if (option == s_ui.junk_option) {
            pe_t_ui(t, " ");
            pe_t_num(t, (unsigned)pe_loot_junk());
            pe_t_ui(t, ", ");
            pe_t_num(t, pe_wayne_count());
            pe_t_ui(t, "/300");
            if (pe_loot_handin_blocked()) pe_t_tr(t, "mod/pe_economy/wayne/bag-full", " bag full", 24);
            else if (pe_loot_wayne_waiting()) pe_t_tr(t, "mod/pe_economy/wayne/day-four", " Day 4", 24);
        }
        if (i < s_orig_len) pe_t_byte(t, G_NEWLINE);
        ++option;
        start = i + 1;
    }
}

/* ---- list paging ---- */

static int pe_pages(int count) { return count <= PE_PAGE_SIZE ? 1 : (count + PE_PAGE_SIZE - 1) / PE_PAGE_SIZE; }

/* Options on a list page: its items, Next page (if more than one page), Back. */
static int pe_list_options(int count, int *items_out) {
    int items = count - s_ui.page * PE_PAGE_SIZE;
    if (items > PE_PAGE_SIZE) items = PE_PAGE_SIZE;
    if (items < 0) items = 0;
    *items_out = items;
    return items + (pe_pages(count) > 1) + 1;
}

static void pe_list_tail(PeText *t, int count, int items) {
    if (pe_pages(count) > 1) { pe_t_option(t, items == 0); pe_t_tr(t, "mod/pe_economy/wayne/next-page", "Next page", 24); }
    pe_t_option(t, items == 0 && pe_pages(count) == 1);
    pe_t_tr(t, "mod/pe_economy/wayne/back", "Back", 24);
}

static void pe_title(PeText *t, const char *key, const char *name) {
    if (s_ui.note[0]) pe_t_ui(t, s_ui.note);
    else { pe_t_tr(t, key, name, 32); pe_t_tr(t, "mod/pe_economy/wayne/junk", "   Junk ", 24); pe_t_num(t, (unsigned)pe_loot_junk()); }
    pe_t_byte(t, G_NEWLINE);
}

/* ---- screens ---- */

static void pe_build_root(PeText *t) {
    static const char *const names[] = { "Requisition", "Rebuild", "Wardrobe", "Mod Permit Card", "Back" };
    static const char *const keys[] = {"mod/pe_economy/wayne/requisition", "mod/pe_economy/wayne/rebuild", "mod/pe_economy/wayne/wardrobe", "mod/pe_economy/wayne/permit", "mod/pe_economy/wayne/back"};
    int i;
    s_ui.root_count = 0;
    s_ui.root[s_ui.root_count++] = ROOT_REQ;
    s_ui.root[s_ui.root_count++] = ROOT_REB;
    if (pe_wardrobe()) s_ui.root[s_ui.root_count++] = ROOT_WARD;
    if (pe_compact()) s_ui.root[s_ui.root_count++] = ROOT_PERMIT;
    s_ui.root[s_ui.root_count++] = ROOT_BACK;
    pe_title(t, "mod/pe_economy/wayne/workshop", "Workshop");
    pe_t_choice(t, s_ui.root_count);
    for (i = 0; i < s_ui.root_count; ++i) { pe_t_option(t, i == 0); pe_t_tr(t, keys[s_ui.root[i]], names[s_ui.root[i]], 24); }
}

static int pe_root_row(int what) {
    int i;
    for (i = 0; i < s_ui.root_count; ++i) if (s_ui.root[i] == what) return i;
    return 0;
}

static void pe_build_list(PeText *t, int rebuild) {
    const int count = rebuild ? pe_rebuild_count() : pe_shop_count();
    int items, k;
    const int options = pe_list_options(count, &items);
    pe_title(t, rebuild ? (count ? "mod/pe_economy/wayne/rebuild" : "mod/pe_economy/wayne/rebuild-empty") : "mod/pe_economy/wayne/requisition",
             rebuild ? (count ? "Rebuild" : "Rebuild: nothing dismantled") : "Requisition");
    pe_t_choice(t, options);
    for (k = 0; k < items; ++k) {
        const int i = s_ui.page * PE_PAGE_SIZE + k;
        const uint16_t id = rebuild ? 0 : pe_shop_item(i);
        const int dim = rebuild ? pe_rebuild_price_index(i) > pe_loot_junk() : pe_shop_blocked(id) != NULL;
        pe_t_option(t, k == 0);
        if (dim) pe_t_red(t, 1);
        if (rebuild) pe_t_item(t, pe_rebuild_item(i));
        else if (id >= 0xF000) pe_t_ui(t, pe_l10n_ui(pe_shop_name(id)));   /* an economy effect */
        else pe_t_item(t, id);
        if (rebuild && pe_rebuild_bought_slots(i)) {            /* built up with Mod Permits */
            pe_t_ui(t, " +");
            pe_t_num(t, pe_rebuild_bought_slots(i));
            pe_t_tr(t, "mod/pe_economy/wayne/slots", " slots", 24);
        }
        pe_t_ui(t, "  ");
        pe_t_num(t, (unsigned)(rebuild ? pe_rebuild_price_index(i) : pe_shop_price(id)));
        if (dim) pe_t_red(t, 0);
    }
    pe_list_tail(t, count, items);
}

static void pe_build_ward(PeText *t) {
    const PeWardrobeApi *w = pe_wardrobe();
    const int count = w ? w->count() : 0;
    int items, k;
    const int options = pe_list_options(count, &items);
    pe_title(t, "mod/pe_economy/wayne/wardrobe", "Wardrobe");
    pe_t_choice(t, options);
    for (k = 0; k < items; ++k) {
        const int i = s_ui.page * PE_PAGE_SIZE + k, state = w->state(i);
        const int dim = state == 0 && w->price(i) > pe_loot_junk();
        pe_t_option(t, k == 0);
        if (dim) pe_t_red(t, 1);
        pe_t_ui(t, w->name(i));
        if (state == 2) pe_t_tr(t, "mod/pe_economy/wayne/worn", "  worn", 24);
        else if (state == 1) pe_t_tr(t, "mod/pe_economy/wayne/owned", "  owned", 24);
        else { pe_t_ui(t, "  "); pe_t_num(t, w->price(i)); }
        if (dim) pe_t_red(t, 0);
    }
    pe_list_tail(t, count, items);
}

/* ---- Mod Permit Card: one more slot on a gun or armor ---- */

static ItemDataRecord *pe_gear(int bag_slot) {
    int value;
    if (bag_slot < 0 || bag_slot >= Inv_GetAyaSlotLimit() || bag_slot >= PE_BAG_SLOTS) return NULL;
    value = g_AyaInventoryItems[bag_slot];
    if (value < 0x100 || value >= 0x180) return NULL;
    return g_InventoryRuntime.equipment[value - 0x100].kind >= 1 && g_InventoryRuntime.equipment[value - 0x100].kind <= 9
               ? &g_InventoryRuntime.equipment[value - 0x100] : NULL;
}

static int pe_gear_list(int *out) {
    int n = 0, i;
    for (i = 0; i < PE_BAG_SLOTS; ++i) if (pe_gear(i)) out[n++] = i;
    return n;
}

static unsigned pe_permits(void) {
    unsigned n = 0;
    int i;
    for (i = 0; i < PE_BAG_SLOTS; ++i) n += g_AyaInventoryItems[i] == PE_ITEM_MOD_PERMIT;
    for (i = 0; i < PE_STORAGE_SLOTS; ++i) n += g_WayneStorageItems[i] == PE_ITEM_MOD_PERMIT;
    return n;
}

static int pe_take_permit(void) {                       /* the bag's first, else storage */
    int i;
    for (i = 0; i < PE_BAG_SLOTS; ++i) if (g_AyaInventoryItems[i] == PE_ITEM_MOD_PERMIT) { g_AyaInventoryItems[i] = 0; return 1; }
    for (i = 0; i < PE_STORAGE_SLOTS; ++i) if (g_WayneStorageItems[i] == PE_ITEM_MOD_PERMIT) { g_WayneStorageItems[i] = 0; return 1; }
    return 0;
}

static unsigned pe_slot_max(const ItemDataRecord *rec) { return rec->tailCapacity < PE_MAX_MOD_SLOTS ? rec->tailCapacity : PE_MAX_MOD_SLOTS; }
static int pe_at_max(const ItemDataRecord *rec) { return rec->tailCount >= pe_slot_max(rec); }

static void pe_build_permit_list(PeText *t) {
    int gear[PE_BAG_SLOTS], items, k;
    const int count = pe_gear_list(gear), options = pe_list_options(count, &items), none = pe_permits() == 0;
    if (s_ui.note[0]) pe_t_ui(t, s_ui.note);
    else if (!count) pe_t_tr(t, "mod/pe_economy/wayne/permit-empty", "Mod Permit: no guns or armor", 32);
    else { pe_t_tr(t, "mod/pe_economy/wayne/permit-count", "Mod Permit   have ", 32); pe_t_num(t, pe_permits()); }
    pe_t_byte(t, G_NEWLINE);
    pe_t_choice(t, options);
    for (k = 0; k < items; ++k) {
        const ItemDataRecord *rec = pe_gear(gear[s_ui.page * PE_PAGE_SIZE + k]);
        const int dim = none || pe_at_max(rec);
        pe_t_option(t, k == 0);
        if (dim) pe_t_red(t, 1);
        pe_t_item(t, rec->itemId);
        pe_t_ui(t, "  ");
        pe_t_num(t, rec->tailCount);
        pe_t_ui(t, "/");
        pe_t_num(t, pe_slot_max(rec));
        if (dim) pe_t_red(t, 0);
    }
    pe_list_tail(t, count, items);
}

static void pe_build_permit_ask(PeText *t) {
    const ItemDataRecord *rec = pe_gear(s_ui.pick);
    pe_t_tr(t, "mod/pe_economy/wayne/add-slot", "Add a slot to ", 32);
    if (rec) pe_t_item(t, rec->itemId);
    pe_t_ui(t, "?");
    pe_t_byte(t, G_NEWLINE);
    pe_t_tr(t, "mod/pe_economy/wayne/permit-use", "Uses 1 Mod Permit, you have ", 32);
    pe_t_num(t, pe_permits());
    pe_t_byte(t, G_NEWLINE);
    pe_t_choice(t, 2);
    pe_t_option(t, 1); pe_t_tr(t, "mod/pe_economy/wayne/yes", "Yes", 24);
    pe_t_option(t, 0); pe_t_tr(t, "mod/pe_economy/wayne/no", "No", 24);
}

/* ---- the box ---- */

static void pe_dim_gray(int on) {
    if (on && !s_highlight.saved) {
        s_highlight.saved = 1;
        s_highlight.r = g_TextboxHighlightColorR;
        s_highlight.g = g_TextboxHighlightColorG;
        s_highlight.b = g_TextboxHighlightColorB;
    }
    if (on) {
        g_TextboxHighlightColorR = g_TextboxHighlightColorG = g_TextboxHighlightColorB = PE_DIM_GRAY;
    } else if (s_highlight.saved) {
        g_TextboxHighlightColorR = s_highlight.r;
        g_TextboxHighlightColorG = s_highlight.g;
        g_TextboxHighlightColorB = s_highlight.b;
        s_highlight.saved = 0;
    }
}

/* Write screen `screen` into the box, sized to fit, the cursor on `cursor`. */
static void pe_show(int screen, int cursor) {
    PeText t;
    TextboxEntry *e = s_ui.entry;
    unsigned height, bottom, width;
    pe_t_init(&t);
    s_ui.screen = screen;
    switch (screen) {
    case SCR_WAYNE: pe_build_wayne(&t); break;
    case SCR_ROOT: pe_build_root(&t); break;
    case SCR_PERMIT: pe_build_permit_list(&t); break;
    case SCR_PERMIT_ASK: pe_build_permit_ask(&t); break;
    case SCR_WARD: pe_build_ward(&t); break;
    default: pe_build_list(&t, screen == SCR_REB); break;
    }
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
    g_DialogueChoiceCursor = (u8)cursor;
    pe_dim_gray(screen != SCR_WAYNE);
}

static void pe_reset(void) {
    pe_dim_gray(0);
    pe_shop_at_counter(0);
    memset(&s_ui, 0, sizeof s_ui);
}

/* Drop the details after a colon or parenthesis, for a short title line. */
static void pe_short_note(void) {
    char *cut = strchr(s_ui.note, ':');
    if (cut) *cut = '\0';
    if ((cut = strchr(s_ui.note, '('))) *cut = '\0';
}

/* ---- input ---- */

static void pe_turn_page(int screen, int count, uint32_t pressed) {
    const int pages = pe_pages(count);
    s_ui.page = (s_ui.page + ((pressed & PE_IN_RIGHT) ? 1 : pages - 1)) % pages;
    s_ui.note[0] = '\0';
    pe_show(screen, 0);
}

static void pe_list_confirm(int rebuild, int cursor) {
    const int count = rebuild ? pe_rebuild_count() : pe_shop_count();
    int items, left;
    const int options = pe_list_options(count, &items);
    if (cursor < items) {
        const int i = s_ui.page * PE_PAGE_SIZE + cursor;
        int keep = cursor;
        if (rebuild) pe_rebuild_index(i, s_ui.note, sizeof s_ui.note);
        else pe_shop_buy(pe_shop_item(i), s_ui.note, sizeof s_ui.note);
        pe_short_note();
        left = (rebuild ? pe_rebuild_count() : pe_shop_count()) - s_ui.page * PE_PAGE_SIZE;   /* the list may shrink */
        if (left <= 0 && s_ui.page > 0) { --s_ui.page; keep = 0; }
        else if (keep >= left) keep = left > 0 ? left - 1 : 0;
        pe_show(rebuild ? SCR_REB : SCR_REQ, keep);
    } else if (pe_pages(count) > 1 && cursor == items) {
        s_ui.page = (s_ui.page + 1) % pe_pages(count);
        s_ui.note[0] = '\0';
        pe_show(rebuild ? SCR_REB : SCR_REQ, 0);
    } else if (cursor == options - 1) {
        s_ui.note[0] = '\0';
        pe_show(SCR_ROOT, pe_root_row(rebuild ? ROOT_REB : ROOT_REQ));
    }
}

/* Leave the permit list: back where it was opened from. */
static void pe_permit_back(void) {
    const int permit = s_ui.wayne_count - 1;
    s_ui.note[0] = '\0';
    if (pe_compact()) pe_show(SCR_ROOT, pe_root_row(ROOT_PERMIT));
    else pe_show(SCR_WAYNE, permit);
}

static uint32_t pe_input(uint32_t pressed, int cursor) {
    const int permit = pe_compact() ? -1 : s_ui.wayne_count - 1;
    const int workshop = s_ui.wayne_count - (pe_compact() ? 1 : 0);
    const int his_cancel = s_ui.wayne_count - 1;
    switch (s_ui.screen) {
    case SCR_WAYNE:
        if (!(pressed & PE_IN_CONFIRM)) return 0;
        if (cursor == permit) { s_ui.note[0] = '\0'; s_ui.page = 0; pe_show(SCR_PERMIT, 0); return PE_IN_CONFIRM; }
        if (cursor == workshop) { s_ui.note[0] = '\0'; pe_show(SCR_ROOT, 0); return PE_IN_CONFIRM; }
        if (cursor == workshop + 1) g_DialogueChoiceCursor = (u8)his_cancel;   /* the script's own Cancel */
        return 0;
    case SCR_ROOT:
        if (pressed & PE_IN_CONFIRM) {
            const int what = cursor >= 0 && cursor < s_ui.root_count ? s_ui.root[cursor] : ROOT_BACK;
            s_ui.page = 0;
            s_ui.note[0] = '\0';
            if (what == ROOT_REQ) pe_show(SCR_REQ, 0);
            else if (what == ROOT_REB) pe_show(SCR_REB, 0);
            else if (what == ROOT_WARD) pe_show(SCR_WARD, 0);
            else if (what == ROOT_PERMIT) pe_show(SCR_PERMIT, 0);
            else pe_show(SCR_WAYNE, workshop);
            return PE_IN_CONFIRM;
        }
        if (pressed & PE_IN_CANCEL) { pe_show(SCR_WAYNE, workshop); return PE_IN_CANCEL; }
        return 0;
    case SCR_REQ:
    case SCR_REB: {
        const int rebuild = s_ui.screen == SCR_REB;
        const int count = rebuild ? pe_rebuild_count() : pe_shop_count();
        if (pressed & PE_IN_CONFIRM) { pe_list_confirm(rebuild, cursor); return PE_IN_CONFIRM; }
        if (pressed & PE_IN_CANCEL) {
            s_ui.note[0] = '\0';
            pe_show(SCR_ROOT, pe_root_row(rebuild ? ROOT_REB : ROOT_REQ));
            return PE_IN_CANCEL;
        }
        if ((pressed & (PE_IN_LEFT | PE_IN_RIGHT)) && pe_pages(count) > 1) pe_turn_page(s_ui.screen, count, pressed);
        return 0;
    }
    case SCR_WARD: {
        const PeWardrobeApi *w = pe_wardrobe();
        const int count = w ? w->count() : 0;
        int items;
        const int options = pe_list_options(count, &items);
        if (pressed & PE_IN_CONFIRM) {
            if (cursor < items && w) {
                w->pick(s_ui.page * PE_PAGE_SIZE + cursor, s_ui.note, sizeof s_ui.note);
                pe_show(SCR_WARD, cursor);
            } else if (pe_pages(count) > 1 && cursor == items) {
                s_ui.page = (s_ui.page + 1) % pe_pages(count);
                s_ui.note[0] = '\0';
                pe_show(SCR_WARD, 0);
            } else if (cursor == options - 1) {
                s_ui.note[0] = '\0';
                pe_show(SCR_ROOT, pe_root_row(ROOT_WARD));
            }
            return PE_IN_CONFIRM;
        }
        if (pressed & PE_IN_CANCEL) { s_ui.note[0] = '\0'; pe_show(SCR_ROOT, pe_root_row(ROOT_WARD)); return PE_IN_CANCEL; }
        if ((pressed & (PE_IN_LEFT | PE_IN_RIGHT)) && pe_pages(count) > 1) pe_turn_page(SCR_WARD, count, pressed);
        return 0;
    }
    case SCR_PERMIT: {
        int gear[PE_BAG_SLOTS], items;
        const int count = pe_gear_list(gear), options = pe_list_options(count, &items);
        if (pressed & PE_IN_CONFIRM) {
            if (cursor < items) {
                const int slot = gear[s_ui.page * PE_PAGE_SIZE + cursor];
                const ItemDataRecord *rec = pe_gear(slot);
                if (!pe_permits()) { snprintf(s_ui.note, sizeof s_ui.note, "%s", pe_l10n_ui("No Mod Permit")); pe_show(SCR_PERMIT, cursor); }
                else if (!rec || pe_at_max(rec)) { snprintf(s_ui.note, sizeof s_ui.note, "%s", pe_l10n_ui("That one has all its slots")); pe_show(SCR_PERMIT, cursor); }
                else { s_ui.pick = slot; s_ui.note[0] = '\0'; pe_show(SCR_PERMIT_ASK, 1); }   /* on No */
            } else if (pe_pages(count) > 1 && cursor == items) {
                s_ui.page = (s_ui.page + 1) % pe_pages(count);
                s_ui.note[0] = '\0';
                pe_show(SCR_PERMIT, 0);
            } else if (cursor == options - 1) {
                pe_permit_back();
            }
            return PE_IN_CONFIRM;
        }
        if (pressed & PE_IN_CANCEL) { pe_permit_back(); return PE_IN_CANCEL; }
        if ((pressed & (PE_IN_LEFT | PE_IN_RIGHT)) && pe_pages(count) > 1) pe_turn_page(SCR_PERMIT, count, pressed);
        return 0;
    }
    case SCR_PERMIT_ASK: {
        int gear[PE_BAG_SLOTS], back = 0, i;
        const int count = pe_gear_list(gear);
        for (i = 0; i < count; ++i) if (gear[i] == s_ui.pick) back = i - s_ui.page * PE_PAGE_SIZE;
        if (back < 0 || back >= PE_PAGE_SIZE) back = 0;
        if (pressed & PE_IN_CONFIRM) {
            ItemDataRecord *rec = pe_gear(s_ui.pick);
            s_ui.note[0] = '\0';
            if (cursor == 0 && rec && !pe_at_max(rec) && pe_take_permit()) {
                rec->tailData[rec->tailCount] = 0;                 /* the new slot starts empty */
                ++rec->tailCount;
                snprintf(s_ui.note, sizeof s_ui.note, pe_l10n_ui("Slot added, now %u"), (unsigned)rec->tailCount);
                pe_log("permit_slot", "item=0x%02X slots=%u permits_left=%u", rec->itemId, (unsigned)rec->tailCount, pe_permits());
            }
            pe_show(SCR_PERMIT, back);
            return PE_IN_CONFIRM;
        }
        if (pressed & PE_IN_CANCEL) { s_ui.note[0] = '\0'; pe_show(SCR_PERMIT, back); return PE_IN_CANCEL; }
        return 0;
    }
    default:
        return 0;
    }
}

/* ---- every frame, before the boxes draw ---- */

static int pe_on_textboxes(void *ctx, void *user) {
    uint32_t taken;
    int i;
    (void)ctx;
    (void)user;
    if (!pe_game_loaded()) return 0;
    if (s_ui.screen != SCR_NONE) {                         /* still driving our box? */
        TextboxEntry *e = s_ui.entry;
        if (e->state != PE_TEXTBOX_SHOWN || (e->message != s_buf && e->message != s_ui.orig_msg)) pe_reset();
        else if (e->message == s_ui.orig_msg) pe_show(SCR_WAYNE, g_DialogueChoiceCursor);
    }
    if (s_ui.screen == SCR_NONE) {
        if (!pe_loot_enabled()) return 0;
        for (i = 0; i < 4; ++i) {
            TextboxEntry *e = &g_TextboxEntries[i];
            if (e->state != PE_TEXTBOX_SHOWN || !pe_is_wayne_menu(e->message)) continue;
            s_ui.entry = e;
            s_ui.orig_msg = e->message;
            s_ui.orig_y = e->y;
            s_ui.orig_h = e->height;
            s_ui.orig_w = e->width;
            pe_show(SCR_WAYNE, g_DialogueChoiceCursor);
            break;
        }
        if (s_ui.screen == SCR_NONE) return 0;
    }
    pe_shop_at_counter(1);                                 /* his box is the shop counter */
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
int pe_wayne_menu_checkpoint_walk(const unsigned char *in,unsigned char *out,int apply){if((!in&&!out)||(s_ui.screen!=SCR_NONE||s_highlight.saved))return 0;if(apply&&(!in||!pe_wayne_menu_checkpoint_walk(in,0,0)))return 0;PeCheckpointWire w={in,out,0,apply,1};

if(apply&&w.ok){memset(&s_ui,0,sizeof s_ui);memset(&s_highlight,0,sizeof s_highlight);s_orig_len=0;memset(s_buf,0,sizeof s_buf);memset(s_orig,0,sizeof s_orig);}return w.ok&&w.at==0u;}
#endif
