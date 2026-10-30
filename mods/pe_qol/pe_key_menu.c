/*
 * Key Items in Aya's own menu (native rewrite of the psxrecomp build's Key Items screen,
 * pe_catalog_menu.c, 2026-10-14): a "Key Items" row in the main menu, with the Dressing Room
 * Key's icon (host extra rows, pe_main_menu.h), opens a screen listing the key item pocket
 * (pe_key_items.c): each key with its inventory icon and name, the highlighted key's game
 * description in the title bar. View only; Circle or Cross goes back.
 */
#include <stdio.h>

#include "pe_gametext.h"
#include "pe_inventory.h"
#include "pe_main_menu.h"
#include "pe_mod_host.h"
#include "pe_translate.h"
#include "pe1/draw_state.h"
#include "pe1/inventory_slots.h"
#include "pe1/menu_context_help.h"
#include "pe1/menu_draw_list.h"
#include "pe1/menu_sound.h"
#include "pe1/menu_widget.h"
#include "pe1/text.h"

#define PE_ITEM_DRESSING_ROOM_KEY 0xC8
#define PE_LIST_ROWS   8
#define PE_ICON_WIDTH  0x12   /* as the bag's rows: icon, then the name */

static PeScreen s_screen;
static u8 s_text[64];

static void pe_key_row(int index) {
    const uint16_t id = pe_keys_item(index);
    const ItemDataRecord *rec = id ? Inv_LookupItemRecordScaled(id) : NULL;
    if (!id) {
        Draw_SetTextDimmed(1);
        Draw_PrintRawText(pe_tr_ascii(s_text, sizeof s_text, "No key items"));
        Draw_SetTextDimmed(0);
        return;
    }
    Draw_StatePush();
    if (rec && rec->iconId) Draw_AllocSprite(rec->iconId - 1);
    Draw_OffsetCursor(PE_ICON_WIDTH, 0);
    Draw_PrintRawText((u8 *)Text_LookupTable8(id - 1u));
    Draw_StatePop();
}

static void pe_draw_list(MenuWidgetNode *node) { MenuWidget_DrawList(node, pe_key_row); }

static int pe_input(MenuWidgetNode *panel, unsigned int buttons) {
    if (panel != s_screen.panel) return 0;
    if (buttons & (PE_MENU_CIRCLE | PE_MENU_CROSS)) {
        pe_screen_close(&s_screen);
        Menu_PlayCancelSound();
        return 1;
    }
    return 0;
}

/* The title bar: the highlighted key's description, or how many keys there are. */
static int pe_on_help_text(void *ctx, void *user) {
    u8 **text = (u8 **)ctx;
    uint16_t id;
    (void)user;
    if (!s_screen.list || MenuWidget_GetCurrentNode() != s_screen.list) return 0;
    if ((id = pe_keys_item(MenuWidget_GridCellIndex(s_screen.list)))) *text = Text_LookupTableC(id - 1u);   /* item descriptions */
    else *text = pe_tr_ascii(s_text, sizeof s_text, "The key item pocket is empty");
    return 0;
}

static void pe_open(MenuWidgetNode *main_list) {
    const int count = pe_keys_count();
    pe_screen_open_game(&s_screen, main_list, PE_LIST_ROWS, pe_draw_list, pe_input);   /* flush with the game's screens */
    pe_screen_rows(&s_screen, count ? count : 1, 0);
}

static const PeMainMenuRow s_row = { "Key Items", PE_ITEM_DRESSING_ROOM_KEY, pe_keys_enabled, pe_open };

void pe_key_menu_register(const char *mod_id) {
    pe_main_menu_add(&s_row, 20);                   /* after the Mod Catalog */
    pe_hook_add(PE_HOOK_HELP_TEXT, pe_on_help_text, NULL, PE_PRIORITY_LATE, mod_id);
}
