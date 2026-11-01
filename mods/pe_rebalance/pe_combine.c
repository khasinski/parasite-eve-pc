/* Recomp medicine recipes (pe_native_menu.c), in two places:
 *   - F1 > Combat > Combine items: pick a recipe, press twice (an unchanged bag between presses);
 *   - Aya's item menu: a medicine with its partner in the bag gets a fourth action, "Combine",
 *     under Use / Move / Discard (PE_HOOK_ITEM_ACTIONS: the list gets a row, its own row drawer
 *     and its panel's input step). Accepting it combines at once and closes the list.
 * Bag-only and outside battle. The optional Junk service gates this economy
 * feature; there is no link-time dependency on pe_economy. */
#include <stdio.h>
#include <string.h>
#include "pe_game.h"
#include "pe_gametext.h"
#include "pe_mod_host.h"
#include "pe_mod_menu.h"
#include "pe_pages.h"
#include "pe_panacea.h"
#include "pe_services.h"
#include "pe1/draw_state.h"
#include "pe1/item_ids.h"
#include "pe1/menu_dialog.h"
#include "pe1/menu_draw_list.h"
#include "pe1/menu_equipment.h"
#include "pe1/menu_sound.h"
#include "pe1/menu_state.h"
#include "pe1/menu_inventory.h"
#include "pe1/menu_widget.h"

void Menu_ContextPick(int row);   /* lists.c: draws one of the game's action rows */
void Inv_SelectActiveList(int list);

static const struct { int first, second, result; const char *name; } s_recipes[] = {
    {ITEM_MEDICINE_1, ITEM_MEDICINE_1, ITEM_MEDICINE_2, "2 Medicine 1 into Medicine 2"},
    {ITEM_MEDICINE_2, ITEM_MEDICINE_2, ITEM_MEDICINE_3, "2 Medicine 2 into Medicine 3"},
    {ITEM_MEDICINE_3, ITEM_MEDICINE_3, ITEM_MEDICINE_4, "2 Medicine 3 into Medicine 4"},
    {ITEM_MEDICINE_4, ITEM_FULL_CURE, ITEM_ITEM25, "Medicine 4 + Full Cure into Panacea"},
};
static int s_recipe, s_armed = -1;
static s16 s_bag[50];
static char s_result[96];

static unsigned pe_recipe_count(void) { return g_pe_panacea ? 4 : 3; }
static const char *pe_recipe_name(unsigned i) { return i < pe_recipe_count() ? s_recipes[i].name : "None"; }

static int pe_pair(int *a, int *b) {
    int limit = Inv_GetAyaSlotLimit();
    *a = *b = -1;
    if (s_recipe < 0 || (unsigned)s_recipe >= pe_recipe_count()) return 0;
    if (limit > 50) limit = 50;
    for (int i = 0; i < limit; ++i) {
        int item = g_AyaInventoryItems[i];
        if (*a < 0 && item == s_recipes[s_recipe].first) *a = i;
        else if (*b < 0 && item == s_recipes[s_recipe].second) *b = i;
    }
    return *a >= 0 && *b >= 0;
}

static void pe_result(const char *text) {
    snprintf(s_result, sizeof s_result, "%s", text);
    pe_toast(text);
}

static void pe_combine(void) {
    const PeJunkApi *junk = pe_service_get("junk", 1);
    int a, b;
    if (!pe_game_loaded() || pe_in_battle() || (g_GameStateFlags & 0x2004u)) {
        s_armed = -1; pe_result("Combine in the field, outside battle and game menus"); return;
    }
    if (!junk || !junk->enabled()) { s_armed = -1; pe_result("Turn on Junk economy to combine items"); return; }
    if (!pe_pair(&a, &b)) { s_armed = -1; pe_result("The ingredients must be in Aya's bag"); return; }
    if (s_armed != s_recipe || memcmp(s_bag, g_AyaInventoryItems, sizeof s_bag)) {
        s_armed = s_recipe;
        memcpy(s_bag, g_AyaInventoryItems, sizeof s_bag);
        pe_result("Press Combine again to consume the two ingredients");
        return;
    }
    s_armed = -1;
    g_AyaInventoryItems[a] = s_recipes[s_recipe].result;
    g_AyaInventoryItems[b] = 0;
    pe_log("combine", "from=0x%02X into=0x%02X", s_recipes[s_recipe].first, s_recipes[s_recipe].result);
    pe_result(s_recipe == 3 ? "Combined into Panacea" : s_recipe == 2 ? "Combined into Medicine 4" :
              s_recipe == 1 ? "Combined into Medicine 3" : "Combined into Medicine 2");
}

/* ---- the Combine row in Aya's item menu ---- */

#define PE_COMBINE_ROW 3          /* under the game's three */
#define PE_ROW_HEIGHT  16
#define PE_ACCEPT      0x10000u

static int s_menu_row = 1;        /* F1 toggle */
static int s_menu_slot, s_menu_partner, s_menu_result;
static u8 s_label[16];

/* The partner of the medicine in bag slot `slot`, by the recipes; sets the result. */
static int pe_partner(int slot, int *result) {
    int limit = Inv_GetAyaSlotLimit(), item, i, r;
    if (limit > 50) limit = 50;
    if (slot < 0 || slot >= limit) return -1;
    item = g_AyaInventoryItems[slot];
    for (r = 0; r < (int)pe_recipe_count(); ++r) {
        int partner;
        if (item == s_recipes[r].first) partner = s_recipes[r].second;
        else if (item == s_recipes[r].second) partner = s_recipes[r].first;
        else continue;
        for (i = 0; i < limit; ++i)
            if (i != slot && g_AyaInventoryItems[i] == partner) { *result = s_recipes[r].result; return i; }
    }
    return -1;
}

/* The row colour advertises either available action, not just healing.
 * This does not change the game's selection bits or make Use available. */
static int pe_combine_available(void) {
    const PeJunkApi *junk = pe_service_get("junk", 1);
    return s_menu_row && pe_game_loaded() && !pe_in_battle() && junk && junk->enabled();
}
static int pe_inventory_row(void *ctx, void *user) {
    PeHookInventoryRow *row = ctx;
    MenuWidgetNode *list = row->node;
    (void)user;
    if (!pe_combine_available() || !list || list->draw != Menu_DrawSelectableEquipSlotList ||
        g_MenuSelectionLocked || g_InvSelectedItemIndex >= 0 || list->appearance.cursorTargetX >= 0) return 0;
    MenuWidgetNode *panel = list->parent;
    if (!panel || panel->mode != 1 || panel->selected_base != 1 ||
        panel->update != Menu_InventoryInputHandler || !panel->parent ||
        panel->parent->draw != Menu_DrawItemList) return 0;
    int result = 0;
    if (pe_partner(row->slot, &result) >= 0) *row->dimmed = 0;
    return 0;
}
static void pe_action_row(int row) {
    if (row != PE_COMBINE_ROW) { Menu_ContextPick(row); return; }
    MenuWidget_DrawCenteredText(s_label);
}

static void pe_menu_draw(MenuWidgetNode *node) {
    Inv_SelectActiveList(g_MenuActiveItemList);
    MenuWidget_DrawList(node, pe_action_row);
}

static int pe_menu_step(MenuWidgetNode *panel, unsigned int flags) {
    int other, result = 0;
    const char *into;
    char msg[48];
    if (!(flags & PE_ACCEPT) || MenuWidget_GridCellIndex(MenuWidget_GetChild(panel, 0)) != PE_COMBINE_ROW)
        return Menu_StepEquipScreen(panel, flags);
    other = pe_partner(s_menu_slot, &result);
    if (other < 0 || other != s_menu_partner || result != s_menu_result) { Menu_PlayErrorSound(); return 1; }
    pe_log("combine", "from=0x%02X into=0x%02X menu=1", g_AyaInventoryItems[s_menu_slot], result);
    g_AyaInventoryItems[s_menu_slot] = (s16)result;
    g_AyaInventoryItems[other] = 0;
    into = result == ITEM_ITEM25 ? "Panacea" : result == ITEM_MEDICINE_4 ? "Medicine 4" :
           result == ITEM_MEDICINE_3 ? "Medicine 3" : "Medicine 2";
    snprintf(msg, sizeof msg, "Combined into %s", into);
    pe_toast(msg);
    MenuWidget_DestroyNode(panel);
    Menu_PlayConfirmSound();
    return 1;
}

static int pe_on_item_actions(void *ctx, void *user) {
    PeHookItemActions *a = ctx;
    const PeJunkApi *junk = pe_service_get("junk", 1);
    int result = 0, partner;
    (void)user;
    if (!pe_combine_available() || a->context != 0 || a->list_id != 0) return 0;
    if ((partner = pe_partner(a->slot, &result)) < 0) return 0;
    if (!s_label[0]) pe_text_from_ascii(s_label, sizeof s_label, "Combine");
    s_menu_slot = a->slot;
    s_menu_partner = partner;
    s_menu_result = result;
    a->list->y_limit = a->list->visible_rows = a->list->visible_rows_mirror = PE_COMBINE_ROW + 1;
    a->panel->visible_rows += PE_ROW_HEIGHT;             /* the panel's height */
    a->list->draw = pe_menu_draw;
    a->panel->update = pe_menu_step;
    return 0;
}

static void pe_ingredients(char *out, unsigned size) {
    int a, b;
    snprintf(out, size, "%s", pe_game_loaded() && pe_pair(&a, &b) ? "Ready" : "Missing ingredients");
}
static void pe_last_result(char *out, unsigned size) { snprintf(out, size, "%s", s_result[0] ? s_result : "Choose a recipe"); }
static void pe_disarm_tick(void) { s_armed = -1; }

void pe_combine_register(const char *mod_id) {
    pe_menu_register_toggle(PE_PAGE_COMBINE, "Combine in item menu",
        "A medicine with its partner in Aya's bag gets a Combine action.", &s_menu_row, NULL);
    pe_hook_add(PE_HOOK_INVENTORY_ROW, pe_inventory_row, NULL, PE_PRIORITY_NORMAL, mod_id);
    pe_hook_add(PE_HOOK_ITEM_ACTIONS, pe_on_item_actions, NULL, PE_PRIORITY_NORMAL, mod_id);
    pe_menu_register_dynamic_choice(PE_PAGE_COMBINE, "Recipe", "Combines two carried medicines, outside battle.",
        &s_recipe, 4, pe_recipe_count, pe_recipe_name);
    pe_menu_register_info(PE_PAGE_COMBINE, "Ingredients", "Both ingredients must be in Aya's bag.", pe_ingredients);
    pe_menu_register_info(PE_PAGE_COMBINE, "Result", "The last combine result or confirmation.", pe_last_result);
    pe_menu_register_action(PE_PAGE_COMBINE, "Combine", "Press twice: consumes two items and gives the result.", pe_combine);
    pe_menu_register_tick(pe_disarm_tick);
}

#if PE_NATIVE_STATE_CPU_COMPONENTS
#include "pe_checkpoint_rebalance.h"
static int pe_rb_combine_idle(void){MenuWidgetNode*n=g_MenuWidgetActiveListHead;unsigned visited=0;while(n&&n!=&g_MenuWidgetNodePoolSentinel){unsigned role=24;for(unsigned i=0;i<23;i++)if(n==&g_MenuWidgetNodePool[i]){role=i;break;}if(role==24||(visited&(1u<<role)))return 0;visited|=1u<<role;if(n->draw==pe_menu_draw||n->update==pe_menu_step)return 0;n=n->next;}return 1;}
int pe_rb_combine_walk(const unsigned char*in,unsigned char*out,int apply){if((!in&&!out)||(!pe_rb_combine_idle()))return 0;if(apply&&(!in||!pe_rb_combine_walk(in,0,0)))return 0;PeCheckpointWire w={in,out,0,apply,1};
PE_CK_S(w,s_menu_row,0,1,1);PE_CK_S(w,s_recipe,0,3,0);PE_CK_S(w,s_armed,-1,3,0);for(unsigned i=0;i<50;i++)PE_CK_S(w,s_bag[i],INT16_MIN,INT16_MAX,0);PE_CK_S(w,s_menu_slot,0,49,0);PE_CK_S(w,s_menu_partner,0,49,0);PE_CK_S(w,s_menu_result,0,255,0);if(apply&&w.ok){s_result[0]=0;s_label[0]=0;}
return pe_rb_finish(&w);}
#endif
