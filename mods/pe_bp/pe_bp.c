/* BP overhaul (design: PE_BP_Overhaul_Revised_2026-11-12.md). This file is the game side:
 * the two save sections, a read-only snapshot of everything BP touches, and the writes the
 * transaction executor (bp_txn.c) commits. Rules and prices live in bp_rules.c.
 *
 * Writes, all after every check passed (bp_execute): g_AyaBonusPoints; one gear record's
 * bonusStats; Aya's stat-table entries 5-6 only (a purchase step, or the full reset); the
 * pe_bp sections. Earning is untouched. The ranks' battle effects are in pe_bp_battle.c. */
#include <stdio.h>
#include <string.h>

#include "pe_bp_api.h"
#include "pe_bp_battle.h"
#include "pe_extension_store.h"
#include "pe_game.h"
#include "pe_item_table.h"
#include "pe_mod_host.h"
#include "pe_services.h"
#include "pe1/aya.h"
#include "pe1/inventory.h"
#include "pe1/inventory_queries.h"
#include "pe1/inventory_slots.h"
#include "pe1/menu_bonus_stats.h"

#define MOD_ID "pe_bp"
enum { BAG_SLOTS = 50, FX_PE_BURDEN_DOWN = 14 };

extern int g_StatScaleBase[7];

static uint8_t s_ranks_bytes[BP_RANKS_BYTES], s_run_bytes[BP_RUN_BYTES];
static const PeExtensionSection s_ranks = {"pe_bp/ranks", 1, 1, BP_RANKS_BYTES, s_ranks_bytes};
static const PeExtensionSection s_run = {"pe_bp/run", 1, 1, BP_RUN_BYTES, s_run_bytes};
static BpRecord s_rec;
static int s_damaged;          /* this save's sections failed their checks: kept, not written */

/* ---- save sections ---- */
static int on_save_read(void *ctx, void *user) {
    (void)ctx; (void)user;
    s_damaged = 0;
    memset(&s_rec, 0, sizeof s_rec);
    if (bp_record_decode(s_ranks_bytes, s_run_bytes, &s_rec)) {
        memset(&s_rec, 0, sizeof s_rec);
        s_damaged = 1;
        pe_mod_log(MOD_ID, "save: BP sections refused (out of range); kept unchanged, purchases off");
    }
    return 0;
}
static int on_new_game(void *ctx, void *user) {
    (void)ctx; (void)user;
    memset(&s_rec, 0, sizeof s_rec);   /* the host already cleared the sections */
    s_damaged = 0;
    return 0;
}
static int store_ready(void *user) {
    (void)user;
    if (s_damaged) return 0;
    return pe_extension_enable(&s_ranks) == PE_REG_OK && pe_extension_enable(&s_run) == PE_REG_OK;
}
static void set_record(const BpRecord *rec, void *user) {
    (void)user;
    s_rec = *rec;
    bp_record_encode(&s_rec, s_ranks_bytes, s_run_bytes);
}

/* ---- snapshot ---- */
static const char *item_name(unsigned id) {
    for (unsigned i = 0; i < PE_ITEM_COUNT; ++i) if (pe_item_ids[i] == id) return pe_item_names[i];
    return "Gear";
}
static const PeJunkApi *junk_api(void) { return pe_service_get("junk", 1); }
static int stat_dist(int stat, unsigned value) { int d = 0; Aya_StatQueryDistanceAndSubLevel(stat, (int)value, &d, NULL); return d; }
static int bag_slots_at(int level) { return Aya_LookupLevelStats(level)->inventoryCapacity; }
static void read_stat(int stat, BpAyaStat *a) {
    int d = stat_dist(stat, a->value);
    a->maxed = d <= 0;
    a->next_value = a->value + (d > 0 ? (unsigned)d : 0u);
    a->distance = d > 0 ? d : 0;
    a->next_distance = stat_dist(stat, a->next_value);
    Aya_StatQueryLevelAndSubLevel(stat, (int)a->value, &a->level, &a->sub);
    Aya_StatQueryLevelAndSubLevel(stat, (int)a->next_value, &a->next_level, &a->next_sub);
    a->shown = stat == 5 ? a->level + 1 : bag_slots_at(a->level);
    a->next_shown = stat == 5 ? a->next_level + 1 : bag_slots_at(a->next_level);
}
static int snapshot(BpSnapshot *s) {
    const PeJunkApi *junk = junk_api();
    const ItemDataRecord *armor;
    int lv, limit;
    memset(s, 0, sizeof *s);
    if (!pe_game_loaded() || pe_in_battle()) return 0;
    s->loaded = !s_damaged;
    s->bp = g_AyaSaveState.bonus_points;
    s->economy = junk && junk->enabled && junk->enabled();
    s->junk = s->economy && junk->junk ? junk->junk() : 0;
    s->level = g_AyaSaveState.level;
    s->learned = g_AyaSaveState.parasite_spell_flags;
    if ((armor = pe_equipped(1)) != NULL) {
        static const int fx[BP_RESIST_TRACKS] = {1, 5, 3, 2};   /* Poison, Confusion, Darkness, Stiffness */
        for (int t = 0; t < BP_RESIST_TRACKS; ++t) s->armor_resist[t] = pe_rec_has_effect(armor, fx[t]);
        s->pe_light = pe_rec_has_effect(armor, FX_PE_BURDEN_DOWN);
    }
    Aya_StatQueryLevelAndSubLevel(3, g_AyaSaveState.stats.levels[3], &lv, NULL);
    s->max_pe = Aya_LookupLevelStats(lv + g_StatScaleBase[3])->battleMaxAtk;

    s->active.value = g_AyaSaveState.stats.levels[5];
    s->capacity.value = g_AyaSaveState.stats.levels[6];
    read_stat(5, &s->active);
    read_stat(6, &s->capacity);
    s->active_base = (unsigned)((int *)Aya_GetStatGrowthTable(5))[0] + 10u * (unsigned)s->level;
    s->capacity_base = (unsigned)((int *)Aya_GetStatGrowthTable(6))[0] + 10u * (unsigned)s->level;
    /* Item Capacity's thresholds are 10 apart, like a level-up's +10, so its BP share replays
     * exactly from the save. Checked here rather than assumed. */
    if (s->capacity.value >= s->capacity_base) {
        unsigned v = s->capacity_base, n = 0;
        int d;
        while (v < s->capacity.value && (d = stat_dist(6, v)) > 0 && n < 999) { v += (unsigned)d; ++n; }
        s->capacity_reset_ok = v == s->capacity.value;
        s->capacity_steps = s->capacity_reset_ok ? n : 0;
    }
    {
        int after_lv;
        Aya_StatQueryLevelAndSubLevel(6, (int)s->capacity_base, &after_lv, NULL);
        s->capacity_after_reset = bag_slots_at(after_lv);
    }
    limit = Inv_GetAyaSlotLimit();
    if (limit > BAG_SLOTS) limit = BAG_SLOTS;
    for (int i = s->capacity_after_reset; i < limit; ++i) s->bag_items_beyond += g_AyaInventoryItems[i] != 0;

    for (int i = 0; i < limit && s->gear_count < BP_MAX_GEAR; ++i) {
        int value = g_AyaInventoryItems[i];
        const ItemDataRecord *rec;
        BpGear *g;
        if (value < 0x100 || value >= 0x180) continue;
        rec = &g_InventoryRuntime.equipment[value - 0x100];
        if (rec->kind < 1 || rec->kind > 9) continue;   /* guns 1-8, armor 9 */
        g = &s->gear[s->gear_count++];
        g->record = value - 0x100;
        g->armor = rec->kind == 9;
        g->equipped = i == (g->armor ? g_EquippedArmorSlot : g_EquippedWeaponSlot);
        g->item_id = rec->itemId;
        g->name = item_name(rec->itemId);
        for (int k = 0; k < 3; ++k) { g->base[k] = rec->baseStats[k]; g->bonus[k] = rec->bonusStats[k]; }
    }
    s->rec = s_rec;
    return 1;
}

/* ---- writes ---- */
static int read_op(BpSnapshot *out, void *user) { (void)user; return snapshot(out); }
static unsigned price(unsigned base) {
    const PeJunkApi *junk = junk_api();
    return junk && junk->price ? junk->price(base) : base;
}
static int junk_spend(unsigned n, void *user) {
    const PeJunkApi *junk = junk_api();
    (void)user;
    return junk && junk->spend && junk->spend(n);
}
static void set_bp(unsigned bp, void *user) {
    (void)user;
    g_AyaSaveState.bonus_points = bp;
    g_BonusPointDisplayValue = (int)bp;   /* the vanilla menu shows this cache */
}
static void set_gear_bonus(int record, int stat, int value, void *user) {
    (void)user;
    g_InventoryRuntime.equipment[record].bonusStats[stat] = (s16)value;   /* equip sync carries it */
}
static void set_aya_stat(int stat, unsigned value, void *user) {
    (void)user;
    if (stat != 5 && stat != 6) return;   /* the only entries this mod may write (design rule) */
    g_AyaSaveState.stats.levels[stat] = (u16)value;
    g_BonusPointStatDeltas[stat] = (int)value;
}
static void after(int stats, int gear, void *user) {
    (void)user; (void)gear;
    if (stats) Inv_RecalcSlotStats();
}
static const BpGameOps s_ops = {read_op, price, store_ready, junk_spend, set_bp, set_gear_bonus,
                                set_aya_stat, set_record, after, NULL};

static BpResult execute(const char *request) {
    BpResult r = bp_execute(&s_ops, request);
    char id[48];
    bp_request_format(&r.request, id, sizeof id);
    pe_mod_log(MOD_ID, "%s: %s bp=%u refund=%u junk=%u", id, r.error ? bp_error_text(r.error) : "ok",
               r.quote.bp_cost, r.quote.bp_refund, r.quote.junk_fee);
    return r;
}
static const PeBpApi s_api = {sizeof(PeBpApi), snapshot, price, execute};
static const BpRecord *current_record(void) { return &s_rec; }

static int bp_init(void) {
    if (pe_extension_register(&s_ranks) < 0 || pe_extension_register(&s_run) < 0)
        pe_mod_log(MOD_ID, "save sections not registered: rank purchases will be refused");
    pe_hook_add(PE_HOOK_SAVE_READ, on_save_read, NULL, PE_PRIORITY_NORMAL, MOD_ID);
    pe_hook_add(PE_HOOK_NEW_GAME, on_new_game, NULL, PE_PRIORITY_NORMAL, MOD_ID);
    pe_service_publish("bp", 1, &s_api, MOD_ID);
    pe_bp_battle_init(MOD_ID, current_record);
    return 0;
}

const PeMod pe_mod_pe_bp = {MOD_ID, "BP Overhaul (preview)", "0.1", NULL, bp_init, NULL};
