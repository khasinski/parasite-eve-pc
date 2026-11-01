/* Inventory menu presentation only. Sorting policy lives in pe_bag_sort.c.
 * Tips (Inventory > Inventory tips): the title bar on the main menu's Items row and on an empty bag
 * slot says Square sorts, and whether Auto is on; the first time the bag opens each session, a
 * message explains both. */
#include "pe_gametext.h"
#include "pe_inventory.h"
#include "pe_mod_host.h"
#include "pe_translate.h"
#include "pe_mod_menu.h"
#include "pe_pages.h"
#include "pe1/field_pad.h"
#include "pe1/menu_equipment.h"
#include "pe1/menu_inventory.h"
#include "pe1/menu_state.h"
#include "pe1/menu_widget.h"

#define PE_ACTION_ITEMS 0   /* the main menu's Use Item row (Menu_CreateEquipScreen) */

static int s_square, s_hidden, s_hidden_mask;
static int s_tips = 1, s_tip_shown;
static u8 s_tip[64];

static void pe_tip_once(MenuWidgetNode *current);

void pe_inventory_menu_shutdown(void) {
    /* A script may have replaced the menu mask since we hid the row. */
    if (s_hidden && g_MenuItemContextFlag == s_hidden_mask)
        g_MenuItemContextFlag |= 0x40;
    s_hidden = s_square = 0;
}
static int pe_menu_restore(void *ctx, void *user) {
    (void)ctx; (void)user;
    pe_inventory_menu_shutdown();
    return 0;
}
static int pe_menu_command(void *ctx, void *user) {
    (void)user;
    (void)ctx;
    pe_inventory_menu_shutdown();
    return 0;
}
static int pe_menu_rows(void *ctx, void *user) {
    PeHookListRows *c = ctx;
    (void)user;
    if (c->node && c->node->draw == Menu_DrawItemList && pe_game_loaded() && !pe_in_battle() &&
        (g_MenuItemContextFlag & 0x40)) {
        g_MenuItemContextFlag &= ~0x40;
        s_hidden_mask = g_MenuItemContextFlag;
        s_hidden = 1;
        if (*c->count > 0) --*c->count;
    }
    return 0;
}
static int pe_menu_frame(void *ctx, void *user) {
    (void)ctx; (void)user;
    int square = (g_FieldPadBits & 0x80000000u) != 0;
    int pressed = square && !s_square;
    s_square = square;
    if (!pe_game_loaded() || !(g_GameStateFlags & 4u)) {
        /* Preserve the held-button edge across menu closing. */
        pe_inventory_menu_shutdown(); s_square = square;
        return 0;
    }
    if (pe_in_battle()) return 0;
    /* Saving restores the original mask. Rebuild through the normal row hook
     * afterwards, so Catalog and other row extensions retain their positions. */
    if (g_MenuItemContextFlag & 0x40) {
        for (MenuWidgetNode *n = g_MenuWidgetActiveListHead; n; n = n->next) {
            if (n->draw != Menu_DrawItemList) continue;
            unsigned mask = g_MenuItemContextFlag & 0x1EF;
            int count = 0;
            for (; mask; mask >>= 1) count += mask & 1;
            Draw_SetPrimCallback(n, count);
            break;
        }
    }
    pe_tip_once(MenuWidget_GetCurrentNode());
    if (!pressed) return 0;
    MenuWidgetNode *list = MenuWidget_GetCurrentNode();
    if (!list || list->draw != Menu_DrawSelectableEquipSlotList) return 0;
    MenuWidgetNode *panel = list->parent;
    if (!panel || panel->mode != 1 || panel->selected_base != 1 ||
        panel->update != Menu_InventoryInputHandler) return 0;
    MenuWidgetNode *main_list = panel->parent;
    if (!main_list || main_list->draw != Menu_DrawItemList || g_MenuActionSubmenuOpen ||
        g_InvSelectedItemIndex >= 0 || list->appearance.cursorTargetX >= 0 ||
        g_MenuEquipMode || Menu_GetBattleEquipMode()) return 0;

    /* Native destruction returns focus to main_list. The stock Sort view
     * returns to Use Item after sorting and to the main menu on cancel. */
    MenuWidget_DestroyNode(panel);
    MenuWidget_NavScrollTo(0x1B);
    MenuWidget_SetCurrentNode(main_list);
    Menu_CreateInvSwapView(main_list, 0);
    return 0;
}
/* ---- tips ---- */

/* The bag list while it is Aya's Use Item screen (not picking a target or moving an item). */
static int pe_bag_open(MenuWidgetNode *list) {
    MenuWidgetNode *panel;
    if (!list || list->draw != Menu_DrawSelectableEquipSlotList || !(panel = list->parent)) return 0;
    return panel->mode == 1 && panel->selected_base == 1 && panel->update == Menu_InventoryInputHandler &&
           !g_MenuEquipMode && !Menu_GetBattleEquipMode();
}

/* The main menu's action at list row `row`: the row-th set bit. */
static int pe_main_action(int row) {
    unsigned mask = g_MenuItemContextFlag & 0x1EF;
    int action = 0;
    for (; mask; mask >>= 1, ++action)
        if ((mask & 1) && row-- == 0) return action;
    return -1;
}

static int pe_tip_title(void *ctx, void *user) {
    u8 **text = ctx;
    MenuWidgetNode *current;
    (void)user;
    if (!s_tips || !pe_game_loaded() || pe_in_battle() || !(current = MenuWidget_GetCurrentNode())) return 0;
    if (current->draw == Menu_DrawItemList && !Menu_GetBattleEquipMode() &&
        pe_main_action(MenuWidget_GridCellIndex(current)) == PE_ACTION_ITEMS) {
        *text = pe_tr_ascii(s_tip, sizeof s_tip, pe_bag_sort_auto() ? "Items   Square: Sort   Auto sort on" : "Items   Square in the bag: Sort");
    } else if (pe_bag_open(current)) {
        const int slot = MenuWidget_GridCellIndex(current);
        if (slot >= 0 && slot < pe_inventory_limit() && !g_AyaInventoryItems[slot])
            *text = pe_tr_ascii(s_tip, sizeof s_tip, pe_bag_sort_auto() ? "Square: Sort   Auto sort is on" : "Square: Sort the bag");
    }
    return 0;
}

static void pe_tip_once(MenuWidgetNode *current) {
    if (!s_tips || s_tip_shown || !pe_bag_open(current)) return;
    s_tip_shown = 1;
    pe_toast(pe_bag_sort_auto() ? pe_l10n_ui("Tip: Square sorts the bag. Auto sort is on: new items are sorted for you.")
                                : "Tip: Square sorts the bag. After a sort, new items are sorted the same way.");
}

void pe_inventory_menu_register(const char *mod_id) {
    pe_menu_register_toggle(PE_PAGE_INVENTORY, "Inventory tips",
        "Show that Square sorts the bag and new items sort automatically.", &s_tips, NULL);
    pe_hook_add(PE_HOOK_HELP_TEXT, pe_tip_title, NULL, PE_PRIORITY_NORMAL, mod_id);
    pe_hook_add(PE_HOOK_LIST_ROWS, pe_menu_rows, NULL, PE_PRIORITY_EARLY, mod_id);
    pe_hook_add(PE_HOOK_FRAME, pe_menu_frame, NULL, PE_PRIORITY_NORMAL, mod_id);
    pe_hook_add(PE_HOOK_SAVE_WRITE, pe_menu_restore, NULL, PE_PRIORITY_EARLY, mod_id);
    pe_hook_add(PE_HOOK_SAVE_READ, pe_menu_restore, NULL, PE_PRIORITY_EARLY, mod_id);
    pe_hook_add(PE_HOOK_SCENE_START, pe_menu_restore, NULL, PE_PRIORITY_EARLY, mod_id);
    pe_hook_add(PE_HOOK_NEW_GAME, pe_menu_command, NULL, PE_PRIORITY_EARLY, mod_id);
}

#if PE_NATIVE_STATE_CPU_COMPONENTS
#include "pe_checkpoint_wire.h"
#include "pe_checkpoint_qol.h"
#include "pe1/inventory_slots.h"
static s16 *pe_qol_item_role(unsigned role){switch(role){case 1:return g_AyaInventoryItems;case 2:return g_InventoryRuntime.slots;case 3:return g_InventoryRuntime.storage;case 4:return g_InventoryRuntime.specialStorage;case 5:return g_InvActiveListItems;default:return 0;}}
static unsigned pe_qol_item_capacity(unsigned role){static const unsigned n[]={0,50,50,100,82,10};return role<6?n[role]:0;}
static unsigned pe_qol_item_id(const s16 *p){if(!p)return 0;for(unsigned i=1;i<6;i++)if(p==pe_qol_item_role(i))return i;return UINT32_MAX;}
int pe_inventory_checkpoint_walk(const unsigned char *in,unsigned char *out,int apply){
 if((!in&&!out)||s_hidden)return 0;if(apply&&(!in||!pe_inventory_checkpoint_walk(in,0,0)))return 0;
 PeCheckpointWire w={in,out,0,apply,1};PE_CK_U(w,s_square,0,1,0);PE_CK_U(w,s_hidden,0,0,0);PE_CK_S(w,s_hidden_mask,INT32_MIN,INT32_MAX,0);PE_CK_U(w,s_tips,0,1,1);PE_CK_U(w,s_tip_shown,0,1,0);
 PE_CK_S(w,g_InvActiveListCount,0,10,0);for(unsigned i=0;i<10;i++){PE_CK_S(w,g_InvActiveListItems[i],-32768,32767,0);}
 unsigned override=pe_qol_item_id(g_InvActiveListOverride),items=pe_qol_item_id(g_InvItemPtr);if(override==UINT32_MAX||items==UINT32_MAX)return 0;
 override=pe_checkpoint_u32(&w,override,0,5,0);PE_CK_S(w,g_InvOverrideSlotLimit,0,65535,0);items=pe_checkpoint_u32(&w,items,0,5,0);PE_CK_S(w,g_InvSlotLimit,0,65535,0);
 unsigned overrideLimit=out?(unsigned)g_InvOverrideSlotLimit:(unsigned)in[68]|(unsigned)in[69]<<8|(unsigned)in[70]<<16|(unsigned)in[71]<<24;
 unsigned itemLimit=out?(unsigned)g_InvSlotLimit:(unsigned)in[76]|(unsigned)in[77]<<8|(unsigned)in[78]<<16|(unsigned)in[79]<<24;
 if(overrideLimit>pe_qol_item_capacity(override)||itemLimit>pe_qol_item_capacity(items))return 0;
 if(apply&&w.ok){g_InvActiveListOverride=pe_qol_item_role(override);g_InvItemPtr=pe_qol_item_role(items);memset(s_tip,0,sizeof s_tip);}return w.ok&&w.at==80u;
}
#endif
