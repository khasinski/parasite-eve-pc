/*
 * The Wardrobe in Aya's own menu, picked like her armor: a "Wardrobe" row after the game's rows
 * (host extra rows, pe_main_menu.h), shown once a costume is owned, opens a list where the game's
 * screens sit -- Aya's own clothes, then every costume owned and wearable now, the worn one marked.
 * Cross wears the highlighted one (it shows when the menu closes: the room reloads), Circle goes
 * back. Buying stays in F1 and at Wayne's.
 *
 * The row's icon is the menu's own Equip Armor icon twice, the second a few pixels up and right:
 * a stack of clothes in the menu's own colours.
 */
#include <stdio.h>

#include "pe_collection.h"
#include "pe_gametext.h"
#include "pe_main_menu.h"
#include "pe_mod_host.h"
#include "pe_translate.h"
#include "pe1/draw_state.h"
#include "pe1/menu_draw_list.h"
#include "pe1/menu_sound.h"
#include "pe1/menu_widget.h"
#include "pe1/text.h"

#define PE_LIST_ROWS  8
#define PE_ICON_SHIFT_X 3
#define PE_ICON_SHIFT_Y 2

static PeScreen s_screen;
static u8 s_text[80];
static char s_note[64];   /* the title line after a pick */

/* Row 0 is Aya's own clothes; row k >= 1 is the (k-1)-th owned costume. */
static const PeCostume *pe_row_costume(int row) { return row > 0 ? pe_wardrobe_owned(row - 1) : NULL; }
static int pe_rows(void) { return 1 + pe_wardrobe_owned_count(); }

static void pe_row(int row) {
    const PeCostume *c = pe_row_costume(row);
    char line[80];
    if (row > 0 && !c) return;
    snprintf(line, sizeof line, "%s%s", c ? pe_l10n_ui(c->name) : pe_l10n_ui("Aya's own clothes"), pe_wardrobe_worn() == c ? pe_l10n_ui("   Worn") : "");
    Draw_PrintRawText(pe_tr_ascii(s_text, sizeof s_text, line));
}

static void pe_draw_list(MenuWidgetNode *node) { MenuWidget_DrawList(node, pe_row); }

static int pe_input(MenuWidgetNode *panel, unsigned int buttons) {
    if (panel != s_screen.panel) return 0;
    if (buttons & PE_MENU_CIRCLE) {
        pe_screen_close(&s_screen);
        Menu_PlayCancelSound();
        return 1;
    }
    if (buttons & PE_MENU_CROSS) {
        const int row = MenuWidget_GridCellIndex(s_screen.list);
        const PeCostume *c = pe_row_costume(row);
        if ((row > 0 && !c) || pe_wardrobe_worn() == c) { Menu_PlayErrorSound(); return 1; }
        pe_wardrobe_wear_later(c);
        snprintf(s_note, sizeof s_note, "%s when you close the menu", c ? pe_l10n_ui("Wearing it") : pe_l10n_ui("Back to her own clothes"));
        Menu_PlayConfirmSound();
        return 1;
    }
    return 0;
}

static int pe_on_help_text(void *ctx, void *user) {
    u8 **text = (u8 **)ctx;
    const PeCostume *c;
    char line[80];
    int row;
    (void)user;
    if (!s_screen.list || MenuWidget_GetCurrentNode() != s_screen.list) return 0;
    row = MenuWidget_GridCellIndex(s_screen.list);
    c = pe_row_costume(row);
    if (s_note[0]) snprintf(line, sizeof line, "%s", s_note);
    else if (c) snprintf(line, sizeof line, "%s   %s", pe_l10n_ui(c->name), c->group ? pe_l10n_ui(c->group) : "");
    else snprintf(line, sizeof line, "%s", pe_l10n_ui("The clothes the story gives Aya"));
    *text = pe_tr_ascii(s_text, sizeof s_text, line);
    return 0;
}

static void pe_open(MenuWidgetNode *main_list) {
    const PeCostume *worn = pe_wardrobe_worn();
    int row = 0, k;
    for (k = 0; worn && k < pe_wardrobe_owned_count(); ++k)
        if (pe_wardrobe_owned(k) == worn) row = k + 1;
    s_note[0] = 0;
    pe_screen_open_game(&s_screen, main_list, PE_LIST_ROWS, pe_draw_list, pe_input);
    pe_screen_rows(&s_screen, pe_rows(), row);
}

/* Shown once something can be worn besides her own clothes. */
static int pe_shown(void) { return pe_wardrobe_owned_count() > 0; }

/* The Equip Armor icon twice, the one behind up and right; the pair centred on the icon's spot. */
static void pe_icon(PeHookMainMenuIcon *icon) {
    const int armor = PE_MAIN_MENU_SPRITE + PE_ACTION_EQUIP_ARMOR;
    const int x = -(PE_ICON_SHIFT_X / 2), y = (PE_ICON_SHIFT_Y + 1) / 2;
    pe_icon_sprite(armor, x + PE_ICON_SHIFT_X, y - PE_ICON_SHIFT_Y);   /* the one behind */
    pe_icon_sprite(armor, x, y);
    icon->sprite = -1;
}

static const PeMainMenuRow s_row = { "Wardrobe", 0, pe_shown, pe_open, pe_icon };

void pe_wardrobe_menu_register(const char *mod_id) {
    pe_main_menu_add(&s_row, 30);                    /* after the Mod Catalog and Key Items */
    pe_hook_add(PE_HOOK_HELP_TEXT, pe_on_help_text, NULL, PE_PRIORITY_LATE, mod_id);
}
