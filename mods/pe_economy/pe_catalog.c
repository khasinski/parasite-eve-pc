/*
 * The Mod Catalog's rules, ported from the psxrecomp build (src/mods/pe_loot_catalog.c; bridge
 * findings pe-loot-overhaul-design-2026-10-11.md decisions 9-12, 25, 26, 36, 37).
 *
 *   Effects     Every gun and armor effect the game has, plus the four economy armor effects
 *               (Bargain, Scavenger, Lucky Find, Salvager in the game's unused numbers 17-20).
 *               An effect is unlocked by dismantling gear that carries it (pe_dismantle.c), by
 *               Requisition, or by a rare drop.
 *   Install     An unlocked effect goes into a slot of the equipped gun or armor for Junk:
 *               common / mid / premium price, premium also a Tool unless a Tool Kit is carried.
 *               The game's rule holds: one mod per group, so a same-group mod elsewhere on the
 *               item is replaced. Bag-capacity armor mods (8-10) are never removed or replaced
 *               here: a smaller bag would strand items.
 *   Remove      Empties a slot; the effect stays unlocked.
 *
 * Saved by the host as the "pe_economy.catalog" record (host/pe_save.h); saves from before, with
 * the recomp's "PECT" v1 block (16-byte header, then the 312-byte state), still load. A new game
 * clears it. Publishes the "catalog" service (armor unlocks, for loot and other mods).
 */
#include <stdio.h>
#include <string.h>

#include "pe_catalog.h"
#include "pe_economy.h"
#include "pe_item_table.h"
#include "pe_mod_host.h"
#include "pe_translate.h"
#include "pe_mod_menu.h"
#include "pe_pages.h"
#include "pe_save.h"
#include "pe_services.h"
#include "pe1/inventory_state.h"

#define PE_ITEM_TOOL           0x16
#define PE_ITEM_TOOL_KIT       0x35
#define PE_ITEM_SUPER_TOOL_KIT 0x36
#define PE_BAG_SLOTS           50
#define PE_STORAGE_SLOTS       100

#define PE_SAVE_MAGIC   0x54434550u   /* "PECT" */
#define PE_SAVE_VERSION 1u

static PeCatalogState s_cat;
_Static_assert(sizeof s_cat == 312, "the PECT block of older saves holds 312 bytes of this state");

PeCatalogState *pe_catalog_state(void) { return &s_cat; }
int pe_catalog_is_unlocked(int which, int effect) {
    return effect > 0 && effect < 32 && ((s_cat.bits[which] >> effect) & 1u);
}

/* ---- effects ---- */

/* The game's effects are named by the host (pe_mod_effect_name); these are the economy's own, in
 * armor effects 17-20. */
static const char *const s_economy_names[4] = { "Bargain", "Scavenger", "Lucky Find", "Salvager" };
/* Board tiles fit 7 characters. */
static const char *const s_gun_short[21] = {
    "", "ROF 2", "ROF 3", "ROF 5", "ROF 7", "ROF 10", "RND ROF", "BURST", "ATK ALL", "HEAT", "FROST",
    "CYANIDE", "ACID", "TRANQ", "COUNTER", "QUICK", "STEAL", "STL ATK", "CRIT UP", "2 CMDS", "3 CMDS",
};
static const char *const s_armor_short[21] = {
    "", "POISON", "STIFF", "DARK", "STL DN", "CONFUSE", "AUTOMED", "AUTOCUR", "BAG +1", "BAG +2", "BAG +4",
    "MAX HP", "ATK UP", "SLOW AT", "PE BRDN", "ATK DN", "", "BARGAIN", "SCAVENG", "LUCKY", "SALVAGE",
};
/* Price tier per effect (the design's value ranking): 0 common, 1 mid, 2 premium. */
static const uint8_t s_gun_tier[21] = { 0, 0, 1, 1, 1, 2, 1, 1, 2, 1, 1, 2, 1, 1, 1, 1, 0, 1, 1, 2, 2 };
static const uint8_t s_armor_tier[21] = { 0, 0, 0, 0, 0, 0, 1, 1, 0, 1, 1, 1, 1, 0, 1, 0, 0, 1, 1, 1, 1 };
/* Board order. */
static const uint8_t s_board_gun[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20 };
static const uint8_t s_board_armor[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
                                         PE_FX_SCAVENGER, PE_FX_LUCKY_FIND, PE_FX_SALVAGER, PE_FX_BARGAIN };

static int s_price[3] = { 10, 30, 80 };   /* per tier, tunable (Debug > Mod Catalog) */
static int s_rebuild_markup = 150;

static int pe_valid(int which, int e) { return e >= 0 && e <= (which == PE_GUN ? PE_GUN_EFFECTS : PE_ARMOR_EFFECTS); }

const char *pe_cat_effect_name(int which, int e) {
    const char *name;
    if (!pe_valid(which, e)) return "?";
    if (which == PE_ARMOR && e >= PE_FX_BARGAIN) return pe_l10n_ui(s_economy_names[e - PE_FX_BARGAIN]);
    name = pe_mod_effect_name(which == PE_ARMOR, e);
    return name ? pe_l10n_ui(name) : "";
}
const char *pe_cat_effect_short(int which, int e) {
    if (!pe_valid(which, e)) return "?";
    return pe_l10n_ui(which == PE_GUN ? s_gun_short[e] : s_armor_short[e]);
}
int pe_cat_effect_tier(int which, int e) { return pe_valid(which, e) ? (which == PE_GUN ? s_gun_tier[e] : s_armor_tier[e]) : 0; }
int pe_cat_tier_price(int tier) { return (int)pe_loot_price((uint32_t)s_price[tier]); }
int pe_cat_rebuild_markup(void) { return s_rebuild_markup; }

static int pe_is_bag_mod(int which, int e) { return which == PE_ARMOR && e >= 8 && e <= 10; }

/* ---- gear ---- */

const char *pe_cat_item_name(int id) {
    unsigned i;
    for (i = 0; i < PE_ITEM_COUNT; i++)
        if (pe_item_ids[i] == id) return pe_l10n_item((unsigned)id,pe_item_names[i]);
    return pe_l10n_ui("UNKNOWN");
}

int pe_cat_bag_limit(void) {
    const int n = g_AyaSaveState.inventory_slot_count + g_InvBonusSlotCount;
    return n > PE_BAG_SLOTS ? PE_BAG_SLOTS : n;
}

ItemDataRecord *pe_cat_bag_record(int slot) {
    int value;
    if (slot < 0 || slot >= PE_BAG_SLOTS) return NULL;
    value = g_AyaSaveState.inventory_items[slot];
    return value >= 0x100 && value < 0x180 ? &g_InventoryRuntime.equipment[value - 0x100] : NULL;
}

int pe_cat_record_type(const ItemDataRecord *rec) {
    if (!rec) return -1;
    if (rec->kind >= 1 && rec->kind <= 8) return PE_GUN;
    if (rec->kind == 9) return PE_ARMOR;
    return -1;
}

int pe_cat_mod_count(const ItemDataRecord *rec) { return rec->tailCount > PE_CAT_MAX_MODS ? PE_CAT_MAX_MODS : rec->tailCount; }

int pe_cat_bonus_sum(const ItemDataRecord *rec) {
    int s = 0, i;
    for (i = 0; i < 3; ++i) if (rec->bonusStats[i] > 0) s += rec->bonusStats[i];
    return s;
}

int pe_cat_carried(int id) {
    int i;
    for (i = 0; i < PE_BAG_SLOTS; ++i) if (g_AyaSaveState.inventory_items[i] == id) return 1;
    for (i = 0; i < PE_STORAGE_SLOTS; ++i) if (g_WayneStorageItems[i] == id) return 1;
    return 0;
}

/* One Tool for a premium install: free with a Tool Kit, else one from the bag, then storage. */
int pe_cat_take_tool(int dry_run) {
    int i;
    if (pe_cat_carried(PE_ITEM_TOOL_KIT) || pe_cat_carried(PE_ITEM_SUPER_TOOL_KIT)) return 1;
    for (i = 0; i < PE_BAG_SLOTS; ++i)
        if (g_AyaSaveState.inventory_items[i] == PE_ITEM_TOOL) { if (!dry_run) g_AyaSaveState.inventory_items[i] = 0; return 1; }
    for (i = 0; i < PE_STORAGE_SLOTS; ++i)
        if (g_WayneStorageItems[i] == PE_ITEM_TOOL) { if (!dry_run) g_WayneStorageItems[i] = 0; return 1; }
    return 0;
}

/* ---- messages ---- */

static char s_said[64] = "-";
void pe_cat_say(const char *text) {
    pe_toast(text);
    snprintf(s_said, sizeof s_said, "%s", text);
}
void pe_cat_note(const char *text) { snprintf(s_said, sizeof s_said, "%s", text); }
const char *pe_cat_last_said(void) { return s_said; }

int pe_cat_ready(void) {
    if (!pe_game_loaded()) { pe_cat_say("Load a game first"); return 0; }
    if (!pe_loot_enabled()) { pe_cat_say("Junk economy is off (Economy page)"); return 0; }
    return 1;
}

/* ---- install / remove: the public calls (pe_economy.h) ---- */

static ItemDataRecord *pe_equipped_record(int which) {
    ItemDataRecord *rec = pe_game_loaded() ? pe_equipped(which) : NULL;
    return pe_cat_record_type(rec) == which ? rec : NULL;
}

ItemDataRecord *pe_cat_record(int which) { return pe_equipped_record(which); }
int pe_cat_slots(int which) {
    const ItemDataRecord *rec = pe_equipped_record(which);
    return rec ? pe_cat_mod_count(rec) : 0;
}
int pe_cat_slot_effect(int which, int slot) {
    const ItemDataRecord *rec = pe_equipped_record(which);
    if (!rec || slot < 0 || slot >= pe_cat_mod_count(rec)) return 0;
    return rec->tailData[slot] & 0x1F;
}
int pe_cat_board_count(int which) { return which == PE_GUN ? (int)sizeof s_board_gun : (int)sizeof s_board_armor; }
int pe_cat_board_effect(int which, int i) {
    if (i < 0 || i >= pe_cat_board_count(which)) return 0;
    return which == PE_GUN ? s_board_gun[i] : s_board_armor[i];
}
int pe_cat_unlocked(int which, int e) { return pe_catalog_is_unlocked(which, e); }
uint32_t pe_cat_price(int which, int e) { return (uint32_t)pe_cat_tier_price(pe_cat_effect_tier(which, e)); }
int pe_cat_needs_tool(int which, int e) { return pe_cat_effect_tier(which, e) == 2; }
int pe_cat_is_bag_mod(int which, int e) { return pe_is_bag_mod(which, e); }

/* Install effect e into `slot` of the equipped gun or armor, or with dry_run only say whether it
 * would work. Returns a PE_CAT_* result. */
int pe_cat_install(int which, int slot, int e, int dry_run) {
    ItemDataRecord *rec;
    int n, current, group, tier, m;
    uint32_t price;
    if (!pe_game_loaded()) return PE_CAT_NO_GAME;
    if (!pe_loot_enabled()) return PE_CAT_OFF;
    if (!(rec = pe_equipped_record(which))) return PE_CAT_NO_ITEM;
    n = pe_cat_mod_count(rec);
    if (n <= 0 || slot < 0 || slot >= n) return PE_CAT_NO_SLOTS;
    if (e <= 0 || !pe_catalog_is_unlocked(which, e)) return PE_CAT_LOCKED;
    current = rec->tailData[slot] & 0x1F;
    if (current == e) return PE_CAT_ALREADY;
    if (pe_is_bag_mod(which, current)) return PE_CAT_BAG;
    group = pe_mod_effect_group(which == PE_ARMOR, e);
    for (m = 0; group && m < n; ++m)       /* a bag mod in the group would be replaced */
        if (m != slot && (rec->tailData[m] & 0x1F) && (rec->tailData[m] >> 5) == group && pe_is_bag_mod(which, rec->tailData[m] & 0x1F))
            return PE_CAT_BAG;
    tier = pe_cat_effect_tier(which, e);
    price = (uint32_t)pe_cat_tier_price(tier);
    if (pe_loot_junk() < price) return PE_CAT_JUNK;
    if (tier == 2 && !pe_cat_take_tool(1)) return PE_CAT_TOOL;
    if (dry_run) return PE_CAT_OK;

    for (m = 0; group && m < n; ++m)       /* the game's rule: one per group */
        if (m != slot && (rec->tailData[m] & 0x1F) && (rec->tailData[m] >> 5) == group) rec->tailData[m] = 0;
    pe_log("install", "%s slot=%d effect=%d was=%d price=%u tool=%d", which ? "armor" : "gun", slot, e, current,
           (unsigned)price, tier == 2);
    rec->tailData[slot] = (u8)((group << 5) | e);
    pe_loot_spend_junk(price);
    if (tier == 2) pe_cat_take_tool(0);
    return PE_CAT_OK;
}

int pe_cat_remove(int which, int slot, int dry_run) {
    ItemDataRecord *rec;
    int e;
    if (!pe_game_loaded()) return PE_CAT_NO_GAME;
    if (!pe_loot_enabled()) return PE_CAT_OFF;
    if (!(rec = pe_equipped_record(which))) return PE_CAT_NO_ITEM;
    if (pe_cat_mod_count(rec) <= 0 || slot < 0 || slot >= pe_cat_mod_count(rec)) return PE_CAT_NO_SLOTS;
    e = rec->tailData[slot] & 0x1F;
    if (!e) return PE_CAT_EMPTY;
    if (pe_is_bag_mod(which, e)) return PE_CAT_BAG;
    if (!dry_run) {
        rec->tailData[slot] = 0;
        pe_log("remove", "%s slot=%d effect=%d", which ? "armor" : "gun", slot, e);
    }
    return PE_CAT_OK;
}

/* Effects installing e into `slot` would take out: the slot's own mod and a same-group mod. */
int pe_cat_replaces(int which, int slot, int e, int *out, int max) {
    const ItemDataRecord *rec = pe_equipped_record(which);
    const int n = rec ? pe_cat_mod_count(rec) : 0, group = pe_mod_effect_group(which == PE_ARMOR, e);
    int k = 0, current, m;
    if (slot < 0 || slot >= n) return 0;
    current = rec->tailData[slot] & 0x1F;
    if (current && current != e && k < max) out[k++] = current;
    for (m = 0; group && m < n && k < max; ++m)
        if (m != slot && (rec->tailData[m] & 0x1F) && (rec->tailData[m] >> 5) == group) out[k++] = rec->tailData[m] & 0x1F;
    return k;
}

/* ---- armor unlocks: the "catalog" service ---- */

int pe_catalog_armor_unlocked(int effect) {
    return effect > 0 && effect <= PE_ARMOR_EFFECTS && pe_catalog_is_unlocked(PE_ARMOR, effect);
}
int pe_catalog_unlock_armor(int effect) {
    if (effect <= 0 || effect > PE_ARMOR_EFFECTS || pe_catalog_is_unlocked(PE_ARMOR, effect)) return 0;
    s_cat.bits[PE_ARMOR] |= 1u << effect;
    return 1;
}
static const PeCatalogApi s_catalog_api = { pe_catalog_armor_unlocked, pe_catalog_unlock_armor };

/* ---- save block ---- */

static uint32_t pe_checksum(const uint8_t *p, uint32_t n) {
    uint32_t s = 0x5A17C0DEu, i;
    for (i = 0; i < n; ++i) s = (s << 5) + s + p[i];
    return s;
}

static int pe_save_check(void *data) {
    (void)data;
    if (s_cat.rebuild_count > PE_REBUILD_MAX) s_cat.rebuild_count = 0;
    pe_mod_log("pe_economy", "Mod Catalog loaded: %d gun / %d armor effects, %u to rebuild",
               __builtin_popcount(s_cat.bits[PE_GUN]), __builtin_popcount(s_cat.bits[PE_ARMOR]), (unsigned)s_cat.rebuild_count);
    return 0;
}

/* Saves from before: the "PECT" block. */
static int pe_save_upgrade(void *data, const uint8_t *at, unsigned size, unsigned version) {
    uint32_t header[4];
    (void)data;
    if (version != 0 || size < sizeof header + sizeof s_cat) return 1;
    memcpy(header, at, sizeof header);
    if (header[0] != PE_SAVE_MAGIC || header[1] != PE_SAVE_VERSION || header[2] != sizeof s_cat ||
        header[3] != pe_checksum(at + sizeof header, sizeof s_cat))
        return 1;
    memcpy(&s_cat, at + sizeof header, sizeof s_cat);
    return 0;
}

static const PeSaveRecord s_save_record = {
    "pe_economy.catalog", 1, &s_cat, sizeof s_cat, pe_save_check, pe_save_upgrade, NULL,
};

/* ---- registration: rules, then the Dismantle feature and the pages ---- */

void pe_catalog_register(const char *mod_id) {
    pe_service_publish("catalog", 1, &s_catalog_api, mod_id);
    pe_save_register(&s_save_record);
    pe_menu_register_int(PE_PAGE_DEBUG_CATALOG, "Mod price common", "Junk for a common effect.", &s_price[0], 0, 500, 5);
    pe_menu_register_int(PE_PAGE_DEBUG_CATALOG, "Mod price mid", "Junk for a mid effect.", &s_price[1], 0, 500, 5);
    pe_menu_register_int(PE_PAGE_DEBUG_CATALOG, "Mod price premium", "Junk for a premium effect (+Tool).", &s_price[2], 0, 500, 5);
    pe_menu_register_int(PE_PAGE_DEBUG_CATALOG, "Rebuild markup %", "Rebuild price vs dismantle payout.", &s_rebuild_markup, 100, 300, 10);
    pe_dismantle_register(mod_id);
    pe_catalog_pages_register();
}

#if PE_NATIVE_STATE_CPU_COMPONENTS
#include "pe_checkpoint_wire.h"
#include "pe_checkpoint_economy.h"
int pe_catalog_checkpoint_walk(const unsigned char *in,unsigned char *out,int apply){if((!in&&!out)||(0))return 0;if(apply&&(!in||!pe_catalog_checkpoint_walk(in,0,0)))return 0;PeCheckpointWire w={in,out,0,apply,1};
for(unsigned i=0;i<2;i++){PE_CK_U(w,s_cat.bits[i],0,UINT32_MAX,0);}
PE_CK_U(w,s_cat.rebuild_count,0,30,0);
for(unsigned i=0;i<3;i++){PE_CK_U(w,s_cat.pad[i],0,255,0);}
unsigned count=out?s_cat.rebuild_count:pe_econ_word(in+8);for(unsigned i=0;i<30;i++){unsigned id=out?s_cat.rebuild[i].id:pe_econ_word(in+24+i*24);if(i<count){const ItemDataRecord *record=pe_base_item((int)id);unsigned slots=out?s_cat.rebuild[i].slots:pe_econ_word(in+28+i*24);if(!record||record->kind<1||record->kind>9||slots>10)return 0;}PE_CK_U(w,s_cat.rebuild[i].id,0,255,0);PE_CK_U(w,s_cat.rebuild[i].slots,0,255,0);for(unsigned j=0;j<3;j++){PE_CK_S(w,s_cat.rebuild[i].bonus[j],-32768,32767,0);}PE_CK_U(w,s_cat.rebuild[i].payout,0,65535,0);}
PE_CK_S(w,s_price[0],INT32_MIN,INT32_MAX,1);
PE_CK_S(w,s_price[1],INT32_MIN,INT32_MAX,1);
PE_CK_S(w,s_price[2],INT32_MIN,INT32_MAX,1);
PE_CK_S(w,s_rebuild_markup,INT32_MIN,INT32_MAX,1);

if(apply&&w.ok){memset(s_said,0,sizeof s_said);s_said[0]='-';}return w.ok&&w.at==760u;}
#endif
