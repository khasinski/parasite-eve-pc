/*
 * The Mod Catalog in Aya's own menu (native rewrite of the psxrecomp build's pe_catalog_menu.c).
 *
 * Main menu: a "Mod Catalog" row after the game's last, through the host's extra rows
 * (pe_main_menu.h; the game's own spare icon).
 *
 * The Catalog is a host screen built from the game's own menu widgets: a panel node with our
 * input handler and a list node with our row drawer (MenuWidget_DrawList), so cursor movement,
 * scrolling and sounds are the game's. One wide panel whose list shows three steps in turn
 * (Circle goes back a step, each step remembers its row):
 *   Item    your equipped gun / armor
 *   Slot    that item's mod slots
 *   Effect  Remove, the unlocked effects with their price, locked ones dimmed
 * The title bar (PE_HOOK_HELP_TEXT) explains the highlighted row: the effect's full name and
 * price, why it can't go in, or what the last action did. Installing asks for a second press;
 * Circle goes back a step. All rules and prices are the Catalog's (pe_loot_catalog.c).
 */
#include <stdio.h>
#include <string.h>

#include "pe_economy.h"
#include "pe_game.h"
#include "pe_main_menu.h"
#include "pe_mod_host.h"
#include "pe_translate.h"
#include "pe1/draw_state.h"
#include "pe1/menu_draw_list.h"
#include "pe1/menu_sound.h"
#include "pe1/menu_widget.h"
#include "pe_catalog.h"
#include "pe_gametext.h"

#define PE_TEXT_MAX     64

enum { STEP_ITEM, STEP_SLOT, STEP_EFFECT, STEP_COUNT };

/* One panel and one list for the whole Catalog, where the game's own screens sit: the list's
 * rows switch with the step, so nothing stacks on screen. */
#define PE_LIST_ROWS    8      /* visible rows; longer lists scroll */

static PeScreen s_screen;

static struct {
    MenuWidgetNode *list;  /* s_screen.list while the Catalog is open */
    int step;
    int row[STEP_COUNT]; /* the row each step was left on */
    int which;           /* 0 gun, 1 armor */
    int slot;
    int armed;           /* effect row armed by the first press, or -1 */
    char result[PE_TEXT_MAX];
} s_cat = { .armed = -1 };

static u8 s_title[PE_TEXT_MAX + 1];
static u8 s_row_text[PE_TEXT_MAX + 1];

/* Menu text in the game's glyph code (pe_gametext.h), into a 0xFF-terminated buffer. */
static u8 *pe_game_text(u8 *out, const char *ascii) { return pe_tr_ascii(out, PE_TEXT_MAX + 1, ascii); }

/* Effect names: the Catalog's own (the game's from the host, the economy's 17-20). */
static const char *pe_effect_name(int which, int e) { return pe_l10n_ui(pe_cat_effect_name(which, e)); }

/* ---- rows ---- */
static int pe_row_count(int step) {
    if (step == STEP_ITEM) return 2;
    if (step == STEP_SLOT) return pe_cat_slots(s_cat.which) > 0 ? pe_cat_slots(s_cat.which) : 1;
    return 1 + pe_cat_board_count(s_cat.which);                 /* Remove, then the board */
}

static int pe_effect_of_row(int row) { return row <= 0 ? 0 : pe_cat_board_effect(s_cat.which, row - 1); }

static int pe_cursor(int step) { (void)step; return s_cat.list ? MenuWidget_GridCellIndex(s_cat.list) : 0; }

static void pe_item_row(int index) {
    char text[PE_TEXT_MAX];
    const ItemDataRecord *rec = pe_cat_record(index);
    snprintf(text, sizeof text, "%s: %s", index ? pe_l10n_ui("Armor") : pe_l10n_ui("Gun"), rec ? pe_l10n_ui(pe_cat_item_name(rec->itemId)) : pe_l10n_ui("None"));
    Draw_SetTextDimmed(rec == 0);
    Draw_PrintRawText(pe_game_text(s_row_text, text));
    Draw_SetTextDimmed(0);
}

static void pe_slot_row(int index) {
    char text[PE_TEXT_MAX];
    if (pe_cat_slots(s_cat.which) <= 0) snprintf(text, sizeof text, "%s", pe_l10n_ui("No mod slots"));
    else snprintf(text, sizeof text, "%d  %s", index + 1, pe_effect_name(s_cat.which, pe_cat_slot_effect(s_cat.which, index)));
    Draw_PrintRawText(pe_game_text(s_row_text, text));
}

static void pe_effect_row(int index) {
    char text[PE_TEXT_MAX];
    const int e = pe_effect_of_row(index);
    int dim = 0;
    if (index == 0) {
        snprintf(text, sizeof text, "%s", pe_l10n_ui("Remove"));
        dim = pe_cat_slot_effect(s_cat.which, s_cat.slot) == 0;
    } else if (!pe_cat_unlocked(s_cat.which, e)) {
        snprintf(text, sizeof text, "%s", pe_l10n_ui("Locked"));
        dim = 1;
    } else {
        snprintf(text, sizeof text, "%-18s %u%s", pe_effect_name(s_cat.which, e), (unsigned)pe_cat_price(s_cat.which, e),
                 pe_cat_needs_tool(s_cat.which, e) ? pe_l10n_ui("+Tool") : "");
        dim = pe_cat_install(s_cat.which, s_cat.slot, e, 1) != PE_CAT_OK;
    }
    Draw_SetTextDimmed(dim);
    Draw_PrintRawText(pe_game_text(s_row_text, text));
    Draw_SetTextDimmed(0);
}

static void pe_draw_list(MenuWidgetNode *node) {
    static void (*const rows[STEP_COUNT])(int) = { pe_item_row, pe_slot_row, pe_effect_row };
    MenuWidget_DrawList(node, rows[s_cat.step]);
}

/* ---- title bar ---- */
static const char *pe_install_problem(int code) {
    switch (code) {
    case PE_CAT_NO_ITEM: return pe_l10n_ui("Equip it first");
    case PE_CAT_NO_SLOTS: return pe_l10n_ui("This item has no mod slots");
    case PE_CAT_LOCKED: return pe_l10n_ui("Locked: dismantle gear with this effect");
    case PE_CAT_ALREADY: return pe_l10n_ui("Already in this slot");
    case PE_CAT_BAG: return pe_l10n_ui("Bag mods can't be replaced here");
    case PE_CAT_JUNK: return pe_l10n_ui("Not enough Junk");
    case PE_CAT_TOOL: return pe_l10n_ui("Needs a Tool, or a Tool Kit");
    case PE_CAT_EMPTY: return pe_l10n_ui("This slot is empty");
    case PE_CAT_OFF: return pe_l10n_ui("Junk economy is off");
    default: return pe_l10n_ui("Can't do that now");
    }
}

static void pe_title_for(int step, char *out, size_t size) {
    const int row = pe_cursor(step);
    if (s_cat.result[0]) { snprintf(out, size, "%s", s_cat.result); return; }
    if (step == STEP_ITEM) { snprintf(out, size, pe_l10n_ui("Mod Catalog   Junk %u"), (unsigned)pe_loot_junk()); return; }
    if (step == STEP_SLOT) { snprintf(out, size, pe_l10n_ui("Pick a slot   Junk %u"), (unsigned)pe_loot_junk()); return; }
    if (row == 0) {
        const int code = pe_cat_remove(s_cat.which, s_cat.slot, 1);
        snprintf(out, size, "%s", code == PE_CAT_OK ? pe_l10n_ui("Empty the slot. The effect stays unlocked") : pe_install_problem(code));
        return;
    }
    {
        const int e = pe_effect_of_row(row), code = pe_cat_install(s_cat.which, s_cat.slot, e, 1);
        if (s_cat.armed == row) snprintf(out, size, pe_l10n_ui("Press again to install %s"), pe_effect_name(s_cat.which, e));
        else if (code == PE_CAT_OK || code == PE_CAT_JUNK)
            snprintf(out, size, pe_l10n_ui("%s  %u Junk%s  (have %u)"), pe_effect_name(s_cat.which, e), (unsigned)pe_cat_price(s_cat.which, e),
                     pe_cat_needs_tool(s_cat.which, e) ? pe_l10n_ui(" + Tool") : "", (unsigned)pe_loot_junk());
        else snprintf(out, size, "%s", pe_install_problem(code));
    }
}

static int pe_step_of(const MenuWidgetNode *node) { return node && node == s_cat.list ? s_cat.step : -1; }

static int pe_on_help_text(void *ctx, void *user) {
    u8 **text = (u8 **)ctx;
    const int step = pe_step_of(MenuWidget_GetCurrentNode());
    char title[PE_TEXT_MAX];
    (void)user;
    if (step >= 0) {
        pe_title_for(step, title, sizeof title);
        *text = pe_game_text(s_title, title);
    }
    return 0;
}

/* ---- the screen ---- */
static int pe_screen_input(MenuWidgetNode *panel, unsigned int buttons);

/* Show step `step` in the list, the cursor on `row`. */
static void pe_set_step(int step, int row) {
    s_cat.step = step;
    s_cat.armed = -1;
    s_cat.result[0] = '\0';
    pe_screen_rows(&s_screen, pe_row_count(step), row);
}

static void pe_open(MenuWidgetNode *from) {
    pe_screen_open_game(&s_screen, from, PE_LIST_ROWS, pe_draw_list, pe_screen_input);
    s_cat.list = s_screen.list;
    memset(s_cat.row, 0, sizeof s_cat.row);
    pe_set_step(STEP_ITEM, 0);
}

static void pe_close(void) {
    s_cat.list = NULL;
    pe_screen_close(&s_screen);
}

/* Circle: back a step, or out of the Catalog. */
static void pe_back(void) {
    if (s_cat.step == STEP_ITEM) { pe_close(); return; }
    pe_set_step(s_cat.step - 1, s_cat.row[s_cat.step - 1]);
}

/* Cross on a row that leads deeper: remember it and show the next step. */
static void pe_forward(int row) {
    s_cat.row[s_cat.step] = row;
    pe_set_step(s_cat.step + 1, 0);
}

static void pe_effect_confirm(int row) {
    char name[40];
    int code;
    if (row == 0) {
        code = pe_cat_remove(s_cat.which, s_cat.slot, 0);
        if (code != PE_CAT_OK) { Menu_PlayErrorSound(); return; }
        snprintf(s_cat.result, sizeof s_cat.result, "%s", pe_l10n_ui("Removed. It stays unlocked"));
        Menu_PlayConfirmSound();
        return;
    }
    {
        const int e = pe_effect_of_row(row);
        code = pe_cat_install(s_cat.which, s_cat.slot, e, 1);
        if (code != PE_CAT_OK) { s_cat.armed = -1; Menu_PlayErrorSound(); return; }
        if (s_cat.armed != row) { s_cat.armed = row; Menu_PlayConfirmSound(); return; }
        snprintf(name, sizeof name, "%s", pe_effect_name(s_cat.which, e));
        code = pe_cat_install(s_cat.which, s_cat.slot, e, 0);
        s_cat.armed = -1;
        if (code != PE_CAT_OK) { Menu_PlayErrorSound(); return; }
        snprintf(s_cat.result, sizeof s_cat.result, pe_l10n_ui("Installed %s"), name);
        pe_log("catalog_menu", "install which=%d slot=%d effect=%d", s_cat.which, s_cat.slot, e);
        Menu_PlayConfirmSound();
    }
}

static int pe_screen_input(MenuWidgetNode *panel, unsigned int buttons) {
    const int step = s_cat.step;
    if (panel != s_screen.panel) return 0;
    if (buttons & 0x40) {                                   /* Circle */
        pe_back();
        Menu_PlayCancelSound();
        return 1;
    }
    if (buttons & 0x10000) {                                /* Cross */
        const int row = pe_cursor(step);
        s_cat.result[0] = '\0';
        if (step == STEP_ITEM) {
            if (!pe_cat_record(row)) { Menu_PlayErrorSound(); return 1; }
            s_cat.which = row;
            pe_forward(row);
            Menu_PlayConfirmSound();
        } else if (step == STEP_SLOT) {
            if (pe_cat_slots(s_cat.which) <= 0) { Menu_PlayErrorSound(); return 1; }
            s_cat.slot = row;
            pe_forward(row);
            Menu_PlayConfirmSound();
        } else {
            pe_effect_confirm(row);
        }
        return 1;
    }
    if (step == STEP_EFFECT && s_cat.armed >= 0 && pe_cursor(step) != s_cat.armed) s_cat.armed = -1;
    return 0;
}

/* ---- main menu row ---- */
static const PeMainMenuRow s_row = { "Mod Catalog", 0, pe_loot_enabled, pe_open };

void pe_catalog_menu_register(const char *mod_id) {
    pe_main_menu_add(&s_row, 10);
    pe_hook_add(PE_HOOK_HELP_TEXT, pe_on_help_text, NULL, PE_PRIORITY_LATE, mod_id);
}
