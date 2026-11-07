/* Smoke Grenade from recomp pe_custom.c; its purchase price comes from
 * pe_loot_shop.c. Uses existing item/text hooks and the shared F1 panel.
 * No new save state: item 0x18 lives in the normal inventory. */
#include <stdio.h>
#include "pe_economy.h"
#include "pe_game.h"
#include "pe_mod_host.h"
#include "pe_mod_menu.h"
#include "pe_pages.h"
#include "pe1/item_ids.h"

static u8 s_name[20], s_description[40];
static ItemDataRecord *s_record;
static u8 s_old_flags, s_our_flags;
static int s_armed_slot = -1;
static uint32_t s_armed_price;
static char s_result[80];

static void pe_encode(u8 *out, const char *text) {
    while (*text) {
        char c = *text++;
        *out++ = c >= 'A' && c <= 'Z' ? 0x10 + c - 'A' :
                 c >= 'a' && c <= 'z' ? 0x30 + c - 'a' :
                 c >= '0' && c <= '9' ? c - '0' : 0x0F;
    }
    *out = 0xFF;
}

static int pe_escape_blocked(void) {
    for (int i = 0; i < 7; ++i) {
        const BattleEnemySlot *enemy = &g_BattleEnemySlots[i];
        if (enemy->active && enemy->combatant.hpAlive > 0 &&
            (enemy->combatant.statusFlags2 & 0x40000u)) return 1;
    }
    return 0;
}

static int pe_empty_slot(void) {
    int limit = Inv_GetAyaSlotLimit();
    if (limit > 50) limit = 50;
    for (int i = 0; i < limit; ++i) if (!g_AyaInventoryItems[i]) return i;
    return -1;
}

static int pe_smoke_text(void *ctx, void *user) {
    PeHookTextLookup *text = ctx;
    (void)user;
    if (text->index == ITEM_ITEM24 - 1) {
        if (text->table == 8) *text->text = s_name;
        if (text->table == 12) *text->text = s_description;
    }
    return 0;
}

static int pe_smoke_use(void *ctx, void *user) {
    const int *item = ctx;
    (void)user;
    if (*item != ITEM_ITEM24 || !pe_game_loaded() || !pe_in_battle()) return 0;
    if (pe_escape_blocked()) {
        /* The game already consumed the item; return it to the freed slot. */
        int slot = pe_empty_slot();
        if (slot >= 0) g_AyaInventoryItems[slot] = ITEM_ITEM24;
        pe_toast(slot >= 0 ? "Cannot escape. Smoke Grenade returned" : "Cannot escape. No room to return Smoke Grenade");
        return 0;
    }
    g_BattleModeState = 4;
    pe_log("smoke", "escape=1");
    return 0;
}

static void pe_smoke_tick(void) {
    ItemDataRecord *smoke;
    s_armed_slot = -1; /* Returning to gameplay cancels purchase confirmation. */
    if (!pe_game_loaded() || !(smoke = pe_base_item(ITEM_ITEM24))) return;
    if (smoke != s_record) { s_record = smoke; s_old_flags = smoke->flags; }
    s_our_flags = (smoke->flags & ~3u) | (pe_in_battle() && pe_escape_blocked() ? 0u : 2u);
    smoke->flags = s_our_flags;
}

static void pe_result(const char *text) {
    snprintf(s_result, sizeof s_result, "%s", text);
    pe_toast(text);
}

static void pe_buy_smoke(void) {
    uint32_t price;
    int slot;
    if (!pe_game_loaded() || pe_in_battle() || (g_GameStateFlags & 0x2004u)) {
        s_armed_slot = -1; pe_result("Buy in the field, outside battle and game menus"); return;
    }
    if (!pe_loot_enabled()) { s_armed_slot = -1; pe_result("Turn on Junk economy first"); return; }
    if ((slot = pe_empty_slot()) < 0) { s_armed_slot = -1; pe_result("Bag full"); return; }
    price = pe_loot_price(15);
    if (pe_loot_junk() < price) { s_armed_slot = -1; pe_result("Not enough Junk"); return; }
    if (s_armed_slot != slot || s_armed_price != price) {
        s_armed_slot = slot; s_armed_price = price;
        pe_result("Press Buy again to spend Junk"); return;
    }
    s_armed_slot = -1;
    if (!pe_loot_spend_junk(price)) { pe_result("Not enough Junk"); return; }
    g_AyaInventoryItems[slot] = ITEM_ITEM24;
    pe_log("shop", "item=0x18 price=%u", (unsigned)price);
    pe_result("Bought Smoke Grenade");
}

static void pe_price_text(char *out, unsigned size) {
    snprintf(out, size, "%u Junk", (unsigned)pe_loot_price(15));
}
static void pe_result_text(char *out, unsigned size) {
    snprintf(out, size, "%s", s_result[0] ? s_result : "Escape item for ordinary fights");
}

void pe_smoke_shutdown(void) {
    if (s_record && s_record->flags == s_our_flags) s_record->flags = s_old_flags;
    s_record = NULL;
}

void pe_smoke_register(const char *mod_id) {
    pe_encode(s_name, "Smoke Grenade");
    pe_encode(s_description, "Sure Escape from Battle");
    pe_hook_add(PE_HOOK_ITEM_USE, pe_smoke_use, NULL, PE_PRIORITY_NORMAL, mod_id);
    pe_hook_add(PE_HOOK_TEXT_LOOKUP, pe_smoke_text, NULL, PE_PRIORITY_LATE, mod_id);
    pe_menu_register_tick(pe_smoke_tick);
    pe_menu_register_info(PE_PAGE_SMOKE, "Price", "15 Junk before difficulty and Bargain discounts.", pe_price_text);
    pe_menu_register_info(PE_PAGE_SMOKE, "Result", "The last purchase result or confirmation.", pe_result_text);
    pe_menu_register_action(PE_PAGE_SMOKE, "Buy Smoke Grenade", "Press twice. Uses Junk and one free bag slot.", pe_buy_smoke);
}

#if PE_NATIVE_STATE_CPU_COMPONENTS
#include "pe_checkpoint_wire.h"
#include "pe_checkpoint_economy.h"
int pe_smoke_checkpoint_walk(const unsigned char *in,unsigned char *out,int apply){if((!in&&!out)||(0))return 0;if(apply&&(!in||!pe_smoke_checkpoint_walk(in,0,0)))return 0;PeCheckpointWire w={in,out,0,apply,1};
ItemDataRecord *base=pe_base_item(ITEM_ITEM24);unsigned role=!s_record?0:s_record==base?1:UINT32_MAX;role=pe_checkpoint_u32(&w,role,0,1,0);if(role&&!base)return 0;
PE_CK_U(w,s_old_flags,0,255,0);
PE_CK_U(w,s_our_flags,0,255,0);
PE_CK_S(w,s_armed_slot,-1,49,0);
PE_CK_U(w,s_armed_price,0,UINT32_MAX,0);
unsigned flags=base?base->flags:0;flags=pe_checkpoint_u32(&w,flags,0,255,0);if(!base&&flags)return 0;if(apply&&w.ok){s_record=role?base:NULL;if(base)base->flags=(u8)flags;}

if(apply&&w.ok){memset(s_result,0,sizeof s_result);}return w.ok&&w.at==24u;}
#endif
