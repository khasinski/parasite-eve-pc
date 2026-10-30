/*
 * Discard dismantles: in Aya's item menu (and Wayne's storage), Discard on a gun or armor reads
 * "Dismantle" and does what F1 > Economy > Dismantle does -- its effects unlock in the Mod
 * Catalog, it pays Junk and goes on the Rebuild list -- before the game removes it as a discard.
 * Ported from the psxrecomp build (src/mods/pe_native_menu.c; decisions 25, 44, 47).
 *
 * PE_HOOK_ITEM_ACTIONS, when the action list opens for a gun or armor:
 *   - the list draws its Discard row as "Dismantle", one cell 16 px wider (the popup's 56 px
 *     don't fit it), and the panel's input step is ours;
 *   - accepting Discard runs the game's own step, which opens its "<name> will be discarded"
 *     dialog; we reword it -- "<name>: N Junk, M mods recovered, rebuildable" (shorter when it
 *     doesn't fit the screen) -- and put our confirm in front of the game's.
 * The equipped gun and armor, and the last Club, keep the plain Discard.
 */
#include <stdio.h>
#include <string.h>

#include "pe_catalog.h"
#include "pe_economy.h"
#include "pe_game.h"
#include "pe_gametext.h"
#include "pe_mod_host.h"
#include "pe_translate.h"
#include "pe_mod_menu.h"
#include "pe_pages.h"
#include "pe1/draw_state.h"
#include "pe1/menu_dialog.h"
#include "pe1/menu_draw_list.h"
#include "pe1/menu_equipment.h"
#include "pe1/menu_item_rows.h"
#include "pe1/menu_widget.h"
#include "pe1/text.h"

void Menu_ContextPick(int row);   /* lists.c: draws one of the game's action rows */
void Inv_SelectActiveList(int list);

#define PE_ACTION_DISCARD 2       /* g_MenuItemActionStateTable value */
#define PE_ACCEPT         0x10000u
#define PE_WIDEN          16      /* "Dismantle" is 68 px, the popup's text 56 */
#define PE_KIND_CLUB      7
#define PE_MAX_TEXT       280     /* widest dialog line that stays on the 300 px layout */

static int s_enabled = 1;         /* F1 toggle */
static int s_row = -1;            /* the list's Discard row */
static const ItemDataRecord *s_rec;
static u8 s_label[16];

static int pe_is_last_club(const ItemDataRecord *rec) {
    int i;
    if (rec->kind != PE_KIND_CLUB) return 0;
    for (i = 0; i < 50; ++i) {
        const int v = g_AyaSaveState.inventory_items[i];
        if (v >= 0x100 && v < 0x180 && &g_InventoryRuntime.equipment[v - 0x100] != rec &&
            g_InventoryRuntime.equipment[v - 0x100].kind == PE_KIND_CLUB)
            return 0;
    }
    return 1;
}

/* The selected item, if Discard would dismantle it. */
static const ItemDataRecord *pe_dismantlable(int list_id, int slot) {
    const ItemDataRecord *rec;
    if (!s_enabled || !pe_loot_enabled()) return NULL;
    Inv_SelectActiveList(list_id);
    rec = Inv_LookupActiveListData(slot);
    if (!rec || rec->kind < 1 || rec->kind > 9 || pe_is_last_club(rec)) return NULL;
    if (list_id == 0 && (slot == g_AyaSaveState.equipped_weapon_slot || slot == g_AyaSaveState.equipped_armor_slot))
        return NULL;
    return rec;
}

static void pe_list_row(int row) {
    if (row != s_row) { Menu_ContextPick(row); return; }
    Draw_SetTextDimmed(Inv_IsSlotSelectable(g_MenuActiveItemSlot) == 0);   /* as Discard */
    MenuWidget_DrawCenteredText(s_label);
}

static void pe_list_draw(MenuWidgetNode *node) {
    Inv_SelectActiveList(g_MenuActiveItemList);
    MenuWidget_DrawList(node, pe_list_row);
}

/* Yes on the dialog: the reward, then the game's discard frees the item. */
static void pe_on_confirm(MenuWidgetNode *dialog, int yes) {
    if (yes) {
        const ItemDataRecord *rec;
        Inv_SelectActiveList(g_MenuActiveItemList);
        rec = Inv_LookupActiveListData(g_MenuActiveItemSlot);
        if (rec && rec == s_rec) {
            char msg[64];
            int unlocks = 0;
            const unsigned junk = (unsigned)pe_catalog_dismantle_reward(rec, &unlocks);
            snprintf(msg, sizeof msg, pe_l10n_ui("Dismantled %s: +%u Junk"), pe_cat_item_name(rec->itemId), junk);
            if (unlocks) snprintf(msg + strlen(msg), sizeof msg - strlen(msg), pe_l10n_ui(", %d new effect%s"), unlocks, unlocks == 1 ? "" : "s");
            pe_toast(msg);
            pe_log("dismantle", "item=0x%02X junk=%u unlocks=%d menu=1", rec->itemId, junk, unlocks);
        }
    }
    s_rec = NULL;
    Menu_OnItemDiscardConfirm(dialog, yes);
}

/* The game's discard dialog, reworded: what dismantling gives, on one line that fits. */
static void pe_reword(MenuWidgetNode *child) {
    MenuWidgetNode *panel = child->parent;
    const u8 *name = Inv_LookupActiveListDisplayData(g_MenuActiveItemSlot);
    const unsigned junk = (unsigned)pe_catalog_dismantle_preview(s_rec);
    const int mods = pe_cat_mod_count(s_rec);
    char text[4][48];
    u8 suffix[48];
    int i, width = 0;
    snprintf(text[0], sizeof text[0], pe_l10n_ui(": %u Junk, %d mod%s recovered, rebuildable"), junk, mods, mods == 1 ? "" : "s");
    snprintf(text[1], sizeof text[1], pe_l10n_ui(": %u Junk, %d mod%s recovered"), junk, mods, mods == 1 ? "" : "s");
    snprintf(text[2], sizeof text[2], pe_l10n_ui(": %u Junk, rebuildable"), junk);
    snprintf(text[3], sizeof text[3], pe_l10n_ui(": %u Junk"), junk);
    for (i = mods ? 0 : 2; i < 4; ++i) {
        if (name) Text_CopyFFTerminatedBytes(g_MenuItemLabelBuffer, (u8 *)name);
        else g_MenuItemLabelBuffer[0] = 0xFF;
        Text_AppendFFTerminatedBytes(g_MenuItemLabelBuffer, pe_tr_ascii(suffix, sizeof suffix, text[i]));
        width = Draw_MeasureTextWidth(g_MenuItemLabelBuffer);
        if (width <= PE_MAX_TEXT) break;
    }
    if (width < 120) width = 120;                        /* the game's layout */
    panel->grid_width = width + 20;
    panel->x = (300 - width) >> 1;
    child->x = (panel->grid_width - 128) >> 1;
    g_MenuConfirmCallback = pe_on_confirm;
}

static int pe_step(MenuWidgetNode *panel, unsigned int flags) {
    const int cell = MenuWidget_GridCellIndex(MenuWidget_GetChild(panel, 0));
    const int handled = Menu_StepEquipScreen(panel, flags);
    MenuWidgetNode *now = MenuWidget_GetCurrentNode();
    if ((flags & PE_ACCEPT) && cell == s_row && s_rec && g_MenuConfirmCallback == Menu_OnItemDiscardConfirm &&
        now && now->parent && now->parent->update == Menu_ConfirmDialogHandler)
        pe_reword(now);
    return handled;
}

static int pe_on_item_actions(void *ctx, void *user) {
    PeHookItemActions *a = ctx;
    int row;
    (void)user;
    s_row = -1;
    if (!(s_rec = pe_dismantlable(a->list_id, a->slot))) return 0;
    for (row = 0; row < 3 && g_MenuItemActionStateTable[a->context][row] != PE_ACTION_DISCARD; ++row) {}
    if (row == 3 || a->list->draw != Menu_DrawContextActionList) { s_rec = NULL; return 0; }
    pe_tr_ascii(s_label, sizeof s_label, "Dismantle");
    s_row = row;
    a->panel->grid_width += PE_WIDEN;
    a->list->draw_state += PE_WIDEN;                     /* the cell, so the text stays centred */
    a->list->draw = pe_list_draw;
    a->panel->update = pe_step;
    return 0;
}

void pe_discard_dismantle_register(const char *mod_id) {
    pe_menu_register_toggle(PE_PAGE_ECONOMY, "Discard dismantles",
        "Discard on gear in Aya's menu dismantles it: Junk, Catalog effects, Rebuild.", &s_enabled, NULL);
    pe_hook_add(PE_HOOK_ITEM_ACTIONS, pe_on_item_actions, NULL, PE_PRIORITY_NORMAL, mod_id);
}
