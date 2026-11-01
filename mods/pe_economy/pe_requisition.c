/*
 * Requisition: buying supplies and economy effects with Junk. Ported from the psxrecomp build
 * (src/mods/pe_loot_shop.c). This file holds the shop's rules and its save block; the F1 page
 * is pe_requisition_menu.c, so a future Wayne's Workshop menu can share the same backend.
 *
 * Goods and prices (before the difficulty preset and Bargain, pe_loot_price):
 *   - Medicine, cures, ammo, Smoke Grenades: fixed prices, scaled by "Shop price %".
 *   - Ammo goes into its pool (the special records 0x200..0x202), up to the pool's capacity;
 *     the pool's bag entry is added if missing.
 *   - Revive: one price on a first playthrough, another in the EX Game (script variable 80, the
 *     enemy power scale, is above 1.0 there).
 *   - Tool: starts at "Tool price start" and rises by "Tool price step" per Tool bought (counted
 *     in the save), capped. Hidden while a Tool Kit or Super Tool Kit is carried: Tools are free.
 *   - Economy effects (Bargain, Scavenger, Lucky Find, Salvager): unlock in the Mod Catalog;
 *     each disappears from the board once unlocked.
 *   - Rescue: with no usable weapon in the bag or Wayne's storage, Bullets x15 cost nothing and a
 *     free Spare Club (Club 1) can be issued, so a game can never get stuck unable to fight.
 *
 * Board rows are goods ids, so a row keeps its identity when the board changes; economy effects
 * use the pseudo ids 0xF000..0xF003, never real inventory values.
 *
 * Saved by the host as the "pe_economy.shop" record (host/pe_save.h); saves from before, with the
 * recomp's "PESH" v1 block (16-byte header, then the 4-byte payload), still load. A new game resets
 * it.
 */
#include <stdio.h>
#include <string.h>

#include "pe_economy.h"
#include "pe_game.h"
#include "pe_mod_host.h"
#include "pe_mod_menu.h"
#include "pe_pages.h"
#include "pe_requisition.h"
#include "pe_save.h"
#include "pe1/script_queue.h"

/* Items */
#define PE_ITEM_BULLETS_15     0x02
#define PE_ITEM_REVIVE         0x12
#define PE_ITEM_TOOL           0x16
#define PE_ITEM_TOOL_KIT       0x35
#define PE_ITEM_SUPER_TOOL_KIT 0x36
#define PE_ITEM_CLUB_1         0x3F

/* Inventory values */
#define PE_EQUIPMENT_FIRST 0x100   /* 0x100 + record: an equipment record */
#define PE_EQUIPMENT_COUNT 128
#define PE_POOL_FIRST      0x200   /* 0x200 + pool: an ammo pool's bag entry */
#define PE_POOL_COUNT      3
#define PE_BAG_SLOTS       50
#define PE_STORAGE_SLOTS   100
#define PE_KIND_CLUB       8
#define PE_KIND_POOL_FIRST 16      /* ammo kinds 16..18 fill pools 0..2 */

#define PE_EX_GAME_VAR     80      /* enemy power scale, 16.16; above 1.0 in the EX Game */
#define PE_ECON_ID_FIRST   0xF000

/* The "PESH" block of saves from before */
#define PE_SAVE_MAGIC   0x48534550u /* "PESH" */
#define PE_SAVE_VERSION 1u

static const struct { uint16_t id; const char *name; unsigned price; } s_goods[] = {
    { 0x06, "Medicine 1", 6 },     { 0x07, "Medicine 2", 12 },  { 0x08, "Medicine 3", 35 },
    { 0x09, "Medicine 4", 80 },    { 0x0D, "Cure-P", 5 },       { 0x0E, "Cure-D", 5 },
    { 0x0F, "Cure-C", 5 },         { 0x10, "Cure-M", 5 },       { 0x11, "Full Cure", 15 },
    { 0x02, "Bullets x15", 5 },    { 0x03, "Bullets x30", 9 },  { 0x05, "Rocket", 20 },
    { 0x18, "Smoke Grenade", 15 },
    { PE_ITEM_REVIVE, "Revive", 0 },   /* priced by playthrough */
    { PE_ITEM_TOOL, "Tool", 0 },       /* escalating price; must stay last (hidden with a kit) */
};
#define PE_GOODS_COUNT ((int)(sizeof s_goods / sizeof s_goods[0]))

static const struct { int effect; const char *name; unsigned price; } s_econ[] = {
    { PE_FX_BARGAIN, "Bargain mod", 125 },     { PE_FX_SCAVENGER, "Scavenger mod", 300 },
    { PE_FX_LUCKY_FIND, "Lucky Find mod", 225 }, { PE_FX_SALVAGER, "Salvager mod", 75 },
};
#define PE_ECON_COUNT ((int)(sizeof s_econ / sizeof s_econ[0]))

static struct { uint16_t tools_bought, pad; } s_shop;   /* the PESH payload */
_Static_assert(sizeof s_shop == 4, "recomp PESH v1 payload");

/* Tuning (Debug > Loot tuning) */
static int s_price_pct = 100, s_econ_pct = 100;
static int s_tool_start = 40, s_tool_step = 10, s_tool_cap = 100;
static int s_revive = 300, s_revive_ex = 500;

/* ---- inventory helpers ---- */

static int pe_bag_limit(void) {
    const int n = Inv_GetAyaSlotLimit();
    return n < 0 ? 0 : n > PE_BAG_SLOTS ? PE_BAG_SLOTS : n;
}

/* The bag slot holding `value` (0: a free slot), or -1. */
static int pe_bag_find(int value) {
    int i;
    for (i = 0; i < pe_bag_limit(); ++i)
        if (g_AyaInventoryItems[i] == value) return i;
    return -1;
}

static int pe_is_tool_kit(int value) { return value == PE_ITEM_TOOL_KIT || value == PE_ITEM_SUPER_TOOL_KIT; }

/* A Tool Kit or Super Tool Kit in the bag or Wayne's storage. */
static int pe_has_tool_kit(void) {
    int i;
    for (i = 0; i < PE_BAG_SLOTS; ++i) if (pe_is_tool_kit(g_AyaInventoryItems[i])) return 1;
    for (i = 0; i < PE_STORAGE_SLOTS; ++i) if (pe_is_tool_kit(g_WayneStorageItems[i])) return 1;
    return 0;
}

static int pe_goods_index(uint16_t id) {
    int i;
    for (i = 0; i < PE_GOODS_COUNT; ++i) if (s_goods[i].id == id) return i;
    return -1;
}

static int pe_econ_index(uint16_t id) {
    return id >= PE_ECON_ID_FIRST && id < PE_ECON_ID_FIRST + PE_ECON_COUNT ? id - PE_ECON_ID_FIRST : -1;
}

/* The ammo pool an item fills, or -1. */
static int pe_ammo_pool(uint16_t id) {
    const ItemDataRecord *base = pe_base_item(id);
    const int pool = base ? (int)base->kind - PE_KIND_POOL_FIRST : -1;
    return pool >= 0 && pool < PE_POOL_COUNT ? pool : -1;
}

static int pe_pool_capacity(int pool) {
    const ItemDataRecord *p = &g_KeyItemRecords[pool];
    const int n = p->baseStats[2] + p->bonusStats[2];
    return n < 0 ? 0 : n > 999 ? 999 : n;
}

/* Inventory value `value` is a weapon Aya can fight with: a club, or a gun with rounds loaded or
 * in its pool (guns of kinds 1-5 use pool 0, kind 6 pool 1, kind 7 pool 2). */
static int pe_usable_weapon(int value) {
    const ItemDataRecord *p;
    int pool;
    if (value < PE_EQUIPMENT_FIRST || value >= PE_EQUIPMENT_FIRST + PE_EQUIPMENT_COUNT) return 0;
    p = &g_InventoryRuntime.equipment[value - PE_EQUIPMENT_FIRST];
    if (!p->iconId) return 0;
    if (p->kind == PE_KIND_CLUB) return 1;
    pool = p->kind >= 1 && p->kind <= 5 ? 0 : p->kind == 6 ? 1 : p->kind == 7 ? 2 : -1;
    return pool >= 0 && (p->ammo || g_KeyItemRecords[pool].ammo);
}

int pe_shop_rescue_needed(void) {
    int i;
    if (!pe_game_loaded()) return 0;
    for (i = 0; i < pe_bag_limit(); ++i) if (pe_usable_weapon(g_AyaInventoryItems[i])) return 0;
    for (i = 0; i < PE_STORAGE_SLOTS; ++i) if (pe_usable_weapon(g_WayneStorageItems[i])) return 0;
    return 1;
}

/* ---- the board ---- */

/* Goods rows shown: all of them, minus the Tool while a kit is carried. */
static int pe_goods_shown(void) { return PE_GOODS_COUNT - (pe_game_loaded() && pe_has_tool_kit()); }

int pe_shop_count(void) {
    int n = pe_goods_shown(), i;
    if (pe_game_loaded())
        for (i = 0; i < PE_ECON_COUNT; ++i) n += !pe_catalog_armor_unlocked(s_econ[i].effect);
    return n;
}

uint16_t pe_shop_item(int row) {
    const int goods = pe_goods_shown();
    int i;
    if (row < 0) return 0;
    if (row < goods) return s_goods[row].id;
    row -= goods;
    if (pe_game_loaded())
        for (i = 0; i < PE_ECON_COUNT; ++i)
            if (!pe_catalog_armor_unlocked(s_econ[i].effect) && row-- == 0) return (uint16_t)(PE_ECON_ID_FIRST + i);
    return 0;
}

const char *pe_shop_name(uint16_t id) {
    const int e = pe_econ_index(id), g = pe_goods_index(id);
    return e >= 0 ? s_econ[e].name : g >= 0 ? s_goods[g].name : "None";
}

uint32_t pe_shop_price(uint16_t id) {
    const int e = pe_econ_index(id), g = pe_goods_index(id);
    uint32_t price;
    if (e >= 0) return pe_loot_price(s_econ[e].price * (unsigned)s_econ_pct / 100u);
    if (g < 0) return 0;
    if (id == PE_ITEM_TOOL) {
        price = pe_loot_price((unsigned)s_tool_start + (unsigned)s_tool_step * s_shop.tools_bought);
        return price > (unsigned)s_tool_cap ? (unsigned)s_tool_cap : price;
    }
    if (id == PE_ITEM_REVIVE)
        return pe_loot_price((unsigned)(g_ScriptVariableBank[PE_EX_GAME_VAR] > 0x10000u ? s_revive_ex : s_revive));
    if (id == PE_ITEM_BULLETS_15 && pe_shop_rescue_needed()) return 0;
    price = (s_goods[g].price * (unsigned)s_price_pct + 50u) / 100u;
    return pe_loot_price(price ? price : 1u);
}

/* Wayne's Workshop is a counter: buying in his dialogue box is fine though the field is busy. */
static int s_at_counter;
void pe_shop_at_counter(int on) { s_at_counter = on; }

/* Why the shop is closed right now, or NULL. */
static const char *pe_shop_closed(void) {
    if (!pe_game_loaded()) return "Load a game first";
    if (!pe_loot_enabled()) return "Junk economy is off";
    if (!s_at_counter && !pe_free_field())
        return "Use Requisition in the field, outside battle and game menus";
    return NULL;
}

const char *pe_shop_blocked(uint16_t id) {
    const char *closed = pe_shop_closed();
    const int e = pe_econ_index(id), g = pe_goods_index(id);
    int pool;
    if (closed) return closed;
    if (e >= 0) {
        if (pe_catalog_armor_unlocked(s_econ[e].effect)) return "Already unlocked";
    } else {
        if (g < 0) return "No such goods";
        if (id == PE_ITEM_TOOL && pe_has_tool_kit()) return "A Tool Kit already covers Tools";
        if (!pe_base_item(id)) return "Item data unavailable";
        pool = pe_ammo_pool(id);
        if (pool >= 0) {
            if (g_KeyItemRecords[pool].ammo >= pe_pool_capacity(pool)) return "Ammo is full";
            if (pe_bag_find(PE_POOL_FIRST + pool) < 0 && pe_bag_find(0) < 0) return "Bag full";
        } else if (pe_bag_find(0) < 0) {
            return "Bag full";
        }
    }
    return pe_loot_junk() < pe_shop_price(id) ? "Not enough Junk" : NULL;
}

int pe_shop_buy(uint16_t id, char *msg, size_t size) {
    const char *why = pe_shop_blocked(id);
    const uint32_t price = why ? 0 : pe_shop_price(id);
    const int e = pe_econ_index(id);
    if (why) { snprintf(msg, size, "%s", why); return 0; }
    if (price && !pe_loot_spend_junk(price)) { snprintf(msg, size, "Not enough Junk"); return 0; }
    if (e >= 0) {
        if (!pe_catalog_unlock_armor(s_econ[e].effect)) {   /* unlocked meanwhile: refund */
            if (price) pe_loot_give_junk(price);
            snprintf(msg, size, "Already unlocked");
            return 0;
        }
    } else {
        const int pool = pe_ammo_pool(id);
        if (pool >= 0) {
            int rounds;
            if (pe_bag_find(PE_POOL_FIRST + pool) < 0) g_AyaInventoryItems[pe_bag_find(0)] = (s16)(PE_POOL_FIRST + pool);
            rounds = g_KeyItemRecords[pool].ammo + pe_base_item(id)->ammo;
            g_KeyItemRecords[pool].ammo = (u16)(rounds > pe_pool_capacity(pool) ? pe_pool_capacity(pool) : rounds);
        } else {
            g_AyaInventoryItems[pe_bag_find(0)] = (s16)id;
        }
        if (id == PE_ITEM_TOOL && s_shop.tools_bought < 0xFFFF) ++s_shop.tools_bought;
    }
    pe_log("buy", "item=%s price=%u", pe_shop_name(id), (unsigned)price);
    snprintf(msg, size, "Bought %s: %u Junk", pe_shop_name(id), (unsigned)price);
    return 1;
}

int pe_shop_spare_club(char *msg, size_t size) {
    const char *why = pe_shop_closed();
    const ItemDataRecord *base = pe_base_item(PE_ITEM_CLUB_1);
    int slot, record = -1, i;
    if (!why && !pe_shop_rescue_needed()) why = "Aya can still fight: no spare Club";
    if (why) { snprintf(msg, size, "%s", why); return 0; }
    slot = pe_bag_find(0);
    for (i = 0; i < PE_EQUIPMENT_COUNT; ++i)
        if (!g_InventoryRuntime.equipment[i].iconId) { record = i; break; }
    if (slot < 0) why = "Bag full";
    else if (record < 0 || !base) why = "No free equipment record";
    if (why) { snprintf(msg, size, "%s", why); return 0; }
    g_InventoryRuntime.equipment[record] = *base;
    g_AyaInventoryItems[slot] = (s16)(PE_EQUIPMENT_FIRST + record);
    pe_log("spare_club", "item=0x%02X", PE_ITEM_CLUB_1);
    snprintf(msg, size, "Spare Club issued");
    return 1;
}

void pe_shop_details(uint16_t id, char *out, unsigned size) {
    const int e = pe_econ_index(id), pool = pe_ammo_pool(id);
    int used = 0, held = 0, i;
    if (!pe_game_loaded()) { snprintf(out, size, "Load a game first"); return; }
    for (i = 0; i < pe_bag_limit(); ++i) {
        used += g_AyaInventoryItems[i] != 0;
        held += g_AyaInventoryItems[i] == id;
    }
    if (e >= 0)
        snprintf(out, size, "%u Junk: unlocks in Mod Catalog", (unsigned)pe_shop_price(id));
    else if (pool >= 0)
        snprintf(out, size, "%u Junk; ammo %u/%d; bag %d/%d", (unsigned)pe_shop_price(id),
                 (unsigned)g_KeyItemRecords[pool].ammo, pe_pool_capacity(pool), used, pe_bag_limit());
    else
        snprintf(out, size, "%u Junk; carrying %d; bag %d/%d", (unsigned)pe_shop_price(id), held, used, pe_bag_limit());
}

/* ---- save block (recomp PESH v1) ---- */

static uint32_t pe_checksum(const void *payload) {
    const uint8_t *p = (const uint8_t *)payload;
    uint32_t sum = 0x5A17C0DEu;
    unsigned i;
    for (i = 0; i < sizeof s_shop; ++i) sum = (sum << 5) + sum + p[i];
    return sum;
}

/* Saves from before: the "PESH" block. */
static int pe_save_upgrade(void *data, const uint8_t *at, unsigned size, unsigned version) {
    uint32_t header[4];
    (void)data;
    if (version != 0 || size < sizeof header + sizeof s_shop) return 1;
    memcpy(header, at, sizeof header);
    if (header[0] != PE_SAVE_MAGIC || header[1] != PE_SAVE_VERSION || header[2] != sizeof s_shop ||
        header[3] != pe_checksum(at + sizeof header))
        return 1;
    memcpy(&s_shop, at + sizeof header, sizeof s_shop);
    return 0;
}

static const PeSaveRecord s_save_record = {
    "pe_economy.shop", 1, &s_shop, sizeof s_shop, NULL, pe_save_upgrade, NULL,
};

/* ---- registration ---- */

static int pe_get_tools_bought(void) { return s_shop.tools_bought; }
static int pe_set_tools_bought(int n) {
    if (n < 0 || n > 0xFFFF) return 0;
    s_shop.tools_bought = (uint16_t)n;
    return 1;
}

void pe_requisition_register(const char *mod_id) {
    pe_save_register(&s_save_record);

    pe_menu_register_int(PE_PAGE_DEBUG_LOOT, "Shop price %", "Goods prices, excluding Tools, Revive and economy mods.",
                         &s_price_pct, 10, 300, 5);
    pe_menu_register_int(PE_PAGE_DEBUG_LOOT, "Tool price start", "First Tool's price.", &s_tool_start, 0, 100, 5);
    pe_menu_register_int(PE_PAGE_DEBUG_LOOT, "Tool price step", "Added per Tool bought.", &s_tool_step, 0, 50, 1);
    pe_menu_register_int(PE_PAGE_DEBUG_LOOT, "Tool price cap", "Most a Tool costs, after discounts.", &s_tool_cap, 10, 100, 5);
    pe_menu_register_int(PE_PAGE_DEBUG_LOOT, "Revive price", "Revive on a first playthrough.", &s_revive, 0, 999, 25);
    pe_menu_register_int(PE_PAGE_DEBUG_LOOT, "Revive price (EX Game)", "Revive on later playthroughs.", &s_revive_ex, 0, 999, 25);
    pe_menu_register_int(PE_PAGE_DEBUG_LOOT, "Economy mod price %", "Of each effect's individual price.", &s_econ_pct, 0, 300, 10);
    pe_menu_register_live_int(PE_PAGE_DEBUG_LOOT, "Tools bought", "Sets the next Tool's price.", 0, 0xFFFF, 1,
                              pe_get_tools_bought, pe_set_tools_bought);
}

#if PE_NATIVE_STATE_CPU_COMPONENTS
#include "pe_checkpoint_wire.h"
#include "pe_checkpoint_economy.h"
int pe_requisition_checkpoint_walk(const unsigned char *in,unsigned char *out,int apply){if((!in&&!out)||(0))return 0;if(apply&&(!in||!pe_requisition_checkpoint_walk(in,0,0)))return 0;PeCheckpointWire w={in,out,0,apply,1};
PE_CK_U(w,s_shop.tools_bought,0,65535,0);
PE_CK_U(w,s_shop.pad,0,65535,0);
PE_CK_S(w,s_price_pct,INT32_MIN,INT32_MAX,1);
PE_CK_S(w,s_econ_pct,INT32_MIN,INT32_MAX,1);
PE_CK_S(w,s_tool_start,INT32_MIN,INT32_MAX,1);
PE_CK_S(w,s_tool_step,INT32_MIN,INT32_MAX,1);
PE_CK_S(w,s_tool_cap,INT32_MIN,INT32_MAX,1);
PE_CK_S(w,s_revive,INT32_MIN,INT32_MAX,1);
PE_CK_S(w,s_revive_ex,INT32_MIN,INT32_MAX,1);
PE_CK_U(w,s_at_counter,0,1,0);

if(apply&&w.ok){}return w.ok&&w.at==40u;}
#endif
