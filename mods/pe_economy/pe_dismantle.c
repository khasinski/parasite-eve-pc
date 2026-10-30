/*
 * Dismantle, Rebuild and Tool salvage: the ways gear feeds the Mod Catalog. Ported from the
 * psxrecomp build (src/mods/pe_loot_catalog.c; decisions 10-12, 42; loot cascade design section 6).
 *
 *   Dismantle  A carried gun or armor (never the equipped ones, never the last Club), press twice:
 *              its effects unlock in the Catalog, it pays Junk and goes on the Rebuild list.
 *              Payout: 5 + 2 per + bonus point + 3 per mod + 2 per mod already unlocked,
 *              Salvager +50%, at most 150.
 *   Rebuild    Buy dismantled gear back: same item, + bonuses and slot count, empty slots. Price:
 *              payout x the markup (150%), at least the payout, at most 150; Wayne's guns and gear
 *              with 100+ base attack / defense always cost the cap. Never below what the rebuilt
 *              item could pay back -- (5 + 2 x bonuses) x 1.5 + 1 -- so no Salvager / Rebuild Junk
 *              loop. A full list (30) drops its oldest entry without Mod Permit slots first.
 *   Tool       A plain Tool deletes the donor: whatever stays on it is treated as dismantled
 *   salvage    (unlocks, Junk, Rebuild entry). A target mod a same-group move overwrites is
 *              unlocked too. A Super Tool keeps the donor, so nothing else happens.
 */
#include <stdio.h>
#include <string.h>

#include "pe_catalog.h"
#include "pe_economy.h"
#include "pe_mod_host.h"
#include "pe_translate.h"
#include "pe1/inventory_slots.h"

#define PE_KIND_CLUB       8
#define PE_EQUIPMENT_COUNT 128
#define PE_ITEM_M9         0x45
#define PE_ITEM_N_PROTECTOR 0x97

/* ---- payout and the Rebuild list ---- */

static uint32_t pe_salvage_payout(int which, int bonus_sum, const uint8_t *mods, int n, int *unlocks_out) {
    uint32_t payout = 5u + 2u * (uint32_t)bonus_sum;
    int unlocks = 0, dupes = 0, m;
    for (m = 0; m < n; ++m) {
        const int e = mods[m] & 0x1F;
        if (!e) continue;
        payout += 3u;
        if (pe_catalog_is_unlocked(which, e)) ++dupes; else ++unlocks;
    }
    payout += 2u * (uint32_t)dupes;
    if (pe_loot_armor_has(PE_FX_SALVAGER)) payout += payout / 2u;
    if (payout > PE_PAYOUT_CAP) payout = PE_PAYOUT_CAP;
    if (unlocks_out) *unlocks_out = unlocks;
    return payout;
}

static int pe_read_mods(const ItemDataRecord *rec, uint8_t *mods) {
    const int n = pe_cat_mod_count(rec);
    memcpy(mods, rec->tailData, (size_t)n);
    return n;
}

static unsigned pe_base_slots(uint8_t id) {
    const ItemDataRecord *base = pe_base_item(id);
    return base ? base->tailCount : 0u;
}

/* Slots bought with Mod Permits: above the base item's own. */
static unsigned pe_bought_slots(const PeRebuildEntry *r) {
    const unsigned own = pe_base_slots(r->id);
    return r->slots > own ? r->slots - own : 0u;
}

/* Unlock the effects in `mods` and put the item on the Rebuild list (newest first). */
static void pe_record_salvage(int which, uint8_t id, uint8_t slots, const int16_t bonus[3], const uint8_t *mods, int n,
                              uint32_t payout) {
    PeCatalogState *cat = pe_catalog_state();
    int fresh = 0, count = 0, m, k;
    for (m = 0; m < n; ++m) {
        const int e = mods[m] & 0x1F;
        if (!e) continue;
        ++count;
        if (!(cat->bits[which] & (1u << e))) ++fresh;
        cat->bits[which] |= 1u << e;
    }
    pe_log("dismantle", "item=0x%02X %s plus=%d/%d/%d mods=%d new=%d payout=%u", id, which ? "armor" : "gun",
           bonus[0], bonus[1], bonus[2], count, fresh, (unsigned)payout);
    if (cat->rebuild_count == PE_REBUILD_MAX) {
        /* Full: drop the oldest entry without bought slots (a built-up item is never lost). */
        int drop = PE_REBUILD_MAX - 1;
        for (k = PE_REBUILD_MAX - 1; k >= 0; --k)
            if (!pe_bought_slots(&cat->rebuild[k])) { drop = k; break; }
        memmove(&cat->rebuild[drop], &cat->rebuild[drop + 1], sizeof(PeRebuildEntry) * (size_t)(PE_REBUILD_MAX - 1 - drop));
        --cat->rebuild_count;
    }
    memmove(&cat->rebuild[1], &cat->rebuild[0], sizeof(PeRebuildEntry) * cat->rebuild_count);
    cat->rebuild[0].id = id;
    cat->rebuild[0].slots = slots;
    memcpy(cat->rebuild[0].bonus, bonus, sizeof cat->rebuild[0].bonus);
    cat->rebuild[0].payout = (uint16_t)payout;
    ++cat->rebuild_count;
}

/* Salvage `rec` as it stands: unlocks, Rebuild entry and Junk. Returns the payout. */
static uint32_t pe_salvage(const ItemDataRecord *rec, int which, int *unlocks_out) {
    uint8_t mods[PE_CAT_MAX_MODS];
    const int n = pe_read_mods(rec, mods);
    const uint32_t payout = pe_salvage_payout(which, pe_cat_bonus_sum(rec), mods, n, unlocks_out);
    pe_record_salvage(which, rec->itemId, rec->tailCount, rec->bonusStats, mods, n, payout);
    pe_loot_give_junk(payout);
    return payout;
}

/* For the game's own Discard (a later native item-menu port): what dismantling pays, and the
 * reward itself, leaving the record for the game to free. */
uint32_t pe_catalog_dismantle_preview(const ItemDataRecord *rec) {
    const int which = pe_cat_record_type(rec);
    uint8_t mods[PE_CAT_MAX_MODS];
    int n;
    if (which < 0) return 0;
    n = pe_read_mods(rec, mods);
    return pe_salvage_payout(which, pe_cat_bonus_sum(rec), mods, n, NULL);
}
uint32_t pe_catalog_dismantle_reward(const ItemDataRecord *rec, int *unlocks_out) {
    const int which = pe_cat_record_type(rec);
    return which < 0 ? 0u : pe_salvage(rec, which, unlocks_out);
}

/* ---- Dismantle ---- */

static int s_dis_choice, s_dis_slots[50], s_dis_armed = -1;
static unsigned s_dis_count;
static char s_dis_names[50][28];

unsigned pe_dismantle_list_count(void) {
    const int gun = g_AyaSaveState.equipped_weapon_slot, armor = g_AyaSaveState.equipped_armor_slot;
    int i;
    s_dis_count = 0;
    if (!pe_game_loaded()) return 1;
    for (i = 0; i < pe_cat_bag_limit(); ++i) {
        const ItemDataRecord *rec = pe_cat_bag_record(i);
        int bonus;
        if (!rec || i == gun || i == armor || pe_cat_record_type(rec) < 0) continue;
        bonus = pe_cat_bonus_sum(rec);
        if (bonus > 0) snprintf(s_dis_names[s_dis_count], sizeof s_dis_names[0], "%s +%d", pe_cat_item_name(rec->itemId), bonus);
        else snprintf(s_dis_names[s_dis_count], sizeof s_dis_names[0], "%s", pe_cat_item_name(rec->itemId));
        s_dis_slots[s_dis_count++] = i;
    }
    return s_dis_count ? s_dis_count : 1;
}

const char *pe_dismantle_list_name(unsigned i) {
    if (!s_dis_count) return pe_l10n_ui("NO UNEQUIPPED GEAR");
    return i < s_dis_count ? s_dis_names[i] : "";
}

int *pe_dismantle_choice(void) { return &s_dis_choice; }

static ItemDataRecord *pe_selected(int *slot_out) {
    if (!pe_game_loaded()) return NULL;
    pe_dismantle_list_count();
    if (!s_dis_count || s_dis_choice < 0 || (unsigned)s_dis_choice >= s_dis_count) return NULL;
    if (slot_out) *slot_out = s_dis_slots[s_dis_choice];
    return pe_cat_bag_record(s_dis_slots[s_dis_choice]);
}

uint32_t pe_dismantle_selected_payout(int *unlocks_out) {
    const ItemDataRecord *rec = pe_selected(NULL);
    uint8_t mods[PE_CAT_MAX_MODS];
    int n;
    if (unlocks_out) *unlocks_out = 0;
    if (pe_cat_record_type(rec) < 0) return 0;
    n = pe_read_mods(rec, mods);
    return pe_salvage_payout(pe_cat_record_type(rec), pe_cat_bonus_sum(rec), mods, n, unlocks_out);
}

static int pe_is_last_club(int slot) {
    const ItemDataRecord *rec = pe_cat_bag_record(slot);
    int i;
    if (!rec || rec->kind != PE_KIND_CLUB) return 0;
    for (i = 0; i < 50; ++i) {
        const ItemDataRecord *other = i == slot ? NULL : pe_cat_bag_record(i);
        if (other && other->kind == PE_KIND_CLUB) return 0;
    }
    return 1;
}

void pe_dismantle_run(void) {
    char msg[96];
    ItemDataRecord *rec;
    int slot = -1, unlocks = 0, which;
    uint32_t payout;
    const char *name;
    if (!pe_cat_ready()) return;
    if (!(rec = pe_selected(&slot)) || (which = pe_cat_record_type(rec)) < 0) { pe_cat_say("Nothing to dismantle"); return; }
    if (pe_is_last_club(slot)) { pe_cat_say("Keep your last Club"); s_dis_armed = -1; return; }
    name = pe_cat_item_name(rec->itemId);
    payout = pe_dismantle_selected_payout(&unlocks);
    if (s_dis_armed != slot) {                     /* first press: say what happens */
        s_dis_armed = slot;
        snprintf(msg, sizeof msg, pe_l10n_ui("Dismantle %s: +%u Junk, %d new effect(s). Press again"), name, (unsigned)payout, unlocks);
        pe_cat_say(msg);
        return;
    }
    s_dis_armed = -1;
    pe_salvage(rec, which, NULL);
    memset(rec, 0, sizeof *rec);                   /* free the record */
    g_AyaSaveState.inventory_items[slot] = 0;
    snprintf(msg, sizeof msg, pe_l10n_ui("Dismantled %s: +%u Junk (total %u)"), name, (unsigned)payout, (unsigned)pe_loot_junk());
    pe_cat_say(msg);
}

/* ---- Rebuild ---- */

static int s_rb_choice;
static char s_rb_names[PE_REBUILD_MAX][32];

static int pe_is_wayne_gun(uint8_t id) {
    static const uint8_t ids[] = { 0x62, 0x63, 0x64, 0x65, 0x66, 0x6E, 0x78, 0x82, 0x8F, 0x92 };
    size_t i;
    for (i = 0; i < sizeof ids; ++i) if (ids[i] == id) return 1;
    return 0;
}

/* Wayne's guns, or 100+ base attack / defense: their payout counts only bonuses and mods. */
static int pe_top_tier(uint8_t id) {
    const ItemDataRecord *base = pe_base_item(id);
    return pe_is_wayne_gun(id) || (base && base->baseStats[0] >= 100u);
}

/* The most dismantling the rebuilt item could pay back, plus 1. */
static uint32_t pe_rebuild_floor(const PeRebuildEntry *r) {
    uint32_t b = 0, back;
    int i;
    for (i = 0; i < 3; ++i) if (r->bonus[i] > 0) b += (uint32_t)r->bonus[i];
    back = 5u + 2u * b;
    return back + back / 2u + 1u;
}

static uint32_t pe_rebuild_price(const PeRebuildEntry *r) {
    uint32_t p, floor;
    if (pe_top_tier(r->id)) {
        p = pe_loot_price(PE_REBUILD_CAP);
        if (p > PE_REBUILD_CAP) p = PE_REBUILD_CAP;
    } else {
        p = pe_loot_price((uint32_t)r->payout * (uint32_t)pe_cat_rebuild_markup() / 100u);
        if (p < r->payout) p = r->payout;
        if (p > PE_REBUILD_CAP) p = PE_REBUILD_CAP;
    }
    floor = pe_rebuild_floor(r);                   /* above the cap if it must be */
    return p < floor ? floor : p;
}

/* A new copy of base item `id` in a free bag slot: bonuses, slot count and mods as given. */
static int pe_create_item(uint8_t id, uint8_t slots, const int16_t bonus[3], const uint8_t *mods, int nmods) {
    const ItemDataRecord *base = pe_base_item(id);
    ItemDataRecord *rec;
    int slot = -1, record = -1, i;
    if (slots > PE_CAT_MAX_MODS || nmods < 0 || nmods > slots) {
        pe_cat_say("Invalid saved equipment slots");
        return 0;
    }
    for (i = 0; i < pe_cat_bag_limit() && slot < 0; ++i) if (!g_AyaSaveState.inventory_items[i]) slot = i;
    if (slot < 0) { pe_cat_say("Bag full"); return 0; }
    for (i = 0; i < PE_EQUIPMENT_COUNT && record < 0; ++i) if (!g_InventoryRuntime.equipment[i].iconId) record = i;
    if (record < 0) { pe_cat_say("No free equipment record"); return 0; }
    if (!base) { pe_cat_say("Unknown item"); return 0; }
    rec = &g_InventoryRuntime.equipment[record];
    *rec = *base;
    memcpy(rec->bonusStats, bonus, sizeof rec->bonusStats);
    rec->tailCount = slots;
    for (i = 0; i < PE_CAT_MAX_MODS; ++i) rec->tailData[i] = i < nmods ? mods[i] : 0;
    g_AyaSaveState.inventory_items[slot] = (s16)(0x100 + record);
    return 1;
}

int pe_rebuild_count(void) { return pe_catalog_state()->rebuild_count; }
uint8_t pe_rebuild_item(int i) { return i >= 0 && i < pe_rebuild_count() ? pe_catalog_state()->rebuild[i].id : 0; }
unsigned pe_rebuild_bought_slots(int i) { return i >= 0 && i < pe_rebuild_count() ? pe_bought_slots(&pe_catalog_state()->rebuild[i]) : 0u; }
uint32_t pe_rebuild_price_index(int i) { return i >= 0 && i < pe_rebuild_count() ? pe_rebuild_price(&pe_catalog_state()->rebuild[i]) : 0; }

int pe_rebuild_index(int i, char *msg, size_t msg_size) {
    PeCatalogState *cat = pe_catalog_state();
    PeRebuildEntry r;
    uint32_t price;
    if (!pe_game_loaded()) { snprintf(msg, msg_size, "%s", pe_l10n_ui("Load a game first")); return 0; }
    if (!pe_loot_enabled()) { snprintf(msg, msg_size, "%s", pe_l10n_ui("Junk economy is off")); return 0; }
    if (i < 0 || i >= cat->rebuild_count) { snprintf(msg, msg_size, "%s", pe_l10n_ui("Nothing to rebuild")); return 0; }
    r = cat->rebuild[i];
    if (r.slots > PE_CAT_MAX_MODS) { snprintf(msg, msg_size, "%s", pe_l10n_ui("Saved equipment data is invalid")); return 0; }
    price = pe_rebuild_price(&r);
    if (pe_loot_junk() < price) { snprintf(msg, msg_size, pe_l10n_ui("Need %u Junk (have %u)"), (unsigned)price, (unsigned)pe_loot_junk()); return 0; }
    if (!pe_create_item(r.id, r.slots, r.bonus, NULL, 0)) { snprintf(msg, msg_size, "%s", pe_l10n_ui("Bag full")); return 0; }
    pe_loot_spend_junk(price);
    pe_log("rebuild", "item=0x%02X price=%u", r.id, (unsigned)price);
    memmove(&cat->rebuild[i], &cat->rebuild[i + 1], sizeof(PeRebuildEntry) * (size_t)(cat->rebuild_count - i - 1));
    --cat->rebuild_count;
    snprintf(msg, msg_size, pe_l10n_ui("Rebuilt %s (-%u Junk)"), pe_cat_item_name(r.id), (unsigned)price);
    return 1;
}

unsigned pe_rebuild_list_count(void) {
    const PeCatalogState *cat = pe_catalog_state();
    unsigned i;
    for (i = 0; i < cat->rebuild_count; ++i) {
        const unsigned bought = pe_bought_slots(&cat->rebuild[i]);
        if (bought)
            snprintf(s_rb_names[i], sizeof s_rb_names[0], pe_l10n_ui("%s +%u SLOTS %u"), pe_cat_item_name(cat->rebuild[i].id), bought,
                     (unsigned)pe_rebuild_price(&cat->rebuild[i]));
        else
            snprintf(s_rb_names[i], sizeof s_rb_names[0], "%s %u", pe_cat_item_name(cat->rebuild[i].id),
                     (unsigned)pe_rebuild_price(&cat->rebuild[i]));
    }
    return cat->rebuild_count ? cat->rebuild_count : 1;
}

const char *pe_rebuild_list_name(unsigned i) {
    if (!pe_rebuild_count()) return pe_l10n_ui("NOTHING DISMANTLED YET");
    return i < (unsigned)pe_rebuild_count() ? s_rb_names[i] : "";
}

int *pe_rebuild_choice(void) { return &s_rb_choice; }

void pe_rebuild_run(void) {
    char msg[80];
    if (!pe_cat_ready()) return;
    pe_rebuild_index(s_rb_choice, msg, sizeof msg);
    pe_cat_say(msg);
}

/* ---- Tool salvage (decision 42) ---- */

/* The record of saved selection `sel` (0/1): Aya's bag, or the override list it came from. */
static ItemDataRecord *pe_selection_record(unsigned sel) {
    const s16 *list = g_AyaSaveState.inventory_items;
    int limit = pe_cat_bag_limit(), index, value;
    if (sel >= 2u) return NULL;
    index = g_InvSavedSelectionIndex[sel];
    if (g_InvSavedSelectionFromStorage[sel] && g_InvActiveListOverride) {
        list = g_InvActiveListOverride;
        limit = g_InvOverrideSlotLimit;
    }
    if (index < 0 || index >= limit) return NULL;
    value = list[index];
    return value >= 0x100 && value < 0x180 ? &g_InventoryRuntime.equipment[value - 0x100] : NULL;
}

/* Would the game's transfer of mod byte `value` find room on `dest` (same group, else empty)? */
static int pe_tool_move_fits(const ItemDataRecord *dest, int value) {
    int i;
    if (value & 0xE0)
        for (i = 0; i < dest->tailCount; ++i) if ((dest->tailData[i] & 0xE0) == (value & 0xE0)) return 1;
    for (i = 0; i < dest->tailCount; ++i) if (!(dest->tailData[i] & 0x1F)) return 1;
    return 0;
}

/* The target mod a same-group move overwrites, or 0. */
static int pe_tool_overwritten(const ItemDataRecord *dest, int value) {
    int i;
    if (!(value & 0xE0)) return 0;
    for (i = 0; i < dest->tailCount; ++i)
        if ((dest->tailData[i] & 0xE0) == (value & 0xE0)) return dest->tailData[i] & 0x1F;
    return 0;
}

static int pe_on_tool_transfer(void *ctx, void *user) {
    const PeHookToolTransfer *t = (const PeHookToolTransfer *)ctx;
    const ItemDataRecord *src, *dest;
    PeCatalogState *cat = pe_catalog_state();
    uint8_t mods[PE_CAT_MAX_MODS];
    int16_t bonus[3];
    int which, dest_which, n, kept_new = 0, bonus_sum = 0, unlocks = 0, i;
    uint32_t payout;
    char msg[64];
    (void)user;
    if (!pe_loot_enabled() || !pe_game_loaded()) return 0;
    src = pe_selection_record(t->selection);
    dest = pe_selection_record(t->selection ^ 1u);
    if ((which = pe_cat_record_type(src)) < 0 || !dest) return 0;
    n = pe_read_mods(src, mods);
    dest_which = pe_cat_record_type(dest);
    if (t->modifier >= 0 && t->modifier < n && dest_which >= 0) {      /* any Tool: an overwritten mod is kept */
        const int old = pe_tool_overwritten(dest, mods[t->modifier]);
        if (old && old != (mods[t->modifier] & 0x1F) && !pe_catalog_is_unlocked(dest_which, old)) {
            cat->bits[dest_which] |= 1u << old;
            kept_new = 1;
            if (t->flags & 1) {
                snprintf(msg, sizeof msg, pe_l10n_ui("%s replaced: kept in the Mod Catalog"), pe_cat_effect_name(dest_which, old));
                pe_cat_note(msg);           /* the game's Tool screen is up: no toast */
            }
        }
    }
    if (t->flags & 1) return 0;                                         /* Super Tool: the donor survives */
    memcpy(bonus, src->bonusStats, sizeof bonus);
    if (t->modifier >= 0) {                                             /* that mod leaves, if it fits */
        if (t->modifier < n && pe_tool_move_fits(dest, mods[t->modifier])) mods[t->modifier] = 0;
    } else {                                                            /* the + bonuses leave */
        bonus[0] = bonus[1] = bonus[2] = 0;
    }
    for (i = 0; i < 3; ++i) if (bonus[i] > 0) bonus_sum += bonus[i];
    payout = pe_salvage_payout(which, bonus_sum, mods, n, &unlocks);
    pe_record_salvage(which, src->itemId, src->tailCount, bonus, mods, n, payout);
    pe_loot_give_junk(payout);
    snprintf(msg, sizeof msg, pe_l10n_ui("Salvaged %s: +%u Junk, %d new effect(s)"), pe_cat_item_name(src->itemId), (unsigned)payout,
             unlocks + kept_new);
    pe_cat_note(msg);           /* the game's Tool screen is up: no toast */
    return 0;
}

/* ---- debug (Debug > Mod Catalog) ---- */

void pe_dismantle_debug_unlock_all(void) {
    PeCatalogState *cat = pe_catalog_state();
    int which, i;
    for (which = 0; which < 2; ++which)
        for (i = 0; i < pe_cat_board_count(which); ++i) cat->bits[which] |= 1u << pe_cat_board_effect(which, i);
    pe_cat_say("Mod Catalog: all effects unlocked");
}

void pe_dismantle_debug_clear(void) {
    memset(pe_catalog_state(), 0, sizeof *pe_catalog_state());
    pe_cat_say("Mod Catalog and Rebuild list cleared");
}

/* Fixed test gear, one mod per rule: a group mod, an ungrouped one, a premium one, another group;
 * the armor carries the protected bag mod. */
void pe_dismantle_debug_test_gun(void) {
    static const uint8_t mods[4] = { 0x22u, 0x09u, 0x74u, 0x91u };   /* ROF 3, Heat, 3 Commands, Steal on attack */
    static const int16_t bonus[3] = { 5, 0, 0 };
    if (!pe_game_loaded()) { pe_cat_say("Load a game first"); return; }
    if (pe_create_item(PE_ITEM_M9, 6u, bonus, mods, 4)) pe_cat_say("Test gun added: M9 +5 with 4 mods");
}

void pe_dismantle_debug_test_armor(void) {
    static const uint8_t mods[4] = { 0x01u, 0x06u, 0xA8u, 0x0Bu };   /* Resist Poison, Auto Medicine, Bag +1, Max HP Up */
    static const int16_t bonus[3] = { 0, 0, 0 };
    if (!pe_game_loaded()) { pe_cat_say("Load a game first"); return; }
    if (pe_create_item(PE_ITEM_N_PROTECTOR, 4u, bonus, mods, 4)) pe_cat_say("Test armor added: N Protector with 4 mods");
}

void pe_dismantle_register(const char *mod_id) {
    pe_hook_add(PE_HOOK_TOOL_TRANSFER, pe_on_tool_transfer, NULL, PE_PRIORITY_NORMAL, mod_id);
}
