/*
 * PE Enhancements: battle rewards (bridge findings D10). What a won battle pays beyond vanilla:
 * extra Junk (floor + EXP / divisor, capped, a no-hit bonus, scaled by Difficulty and Scavenger)
 * and rare drops (soft pity per won battle, then the cascade: economy mod, Super Tool, Mod
 * Permit, Duper / Super Junk, Tool by Tools held, else a stat booster by EXP tier), drawn in gold
 * on the items-found list; an economy mod shows as a Mod Permit entry named "<mod> mod" and
 * taking it unlocks the mod in the Mod Catalog. Moved out of the economy mod's pe_loot.c.
 *
 * The Junk wallet (pe_economy) owns the "pe_economy.loot" save record whole; this mod keeps no
 * copy of it. Its fields there (pity, carried rare, charm, RNG, hits, guarantees) are read and
 * written through the "wallet" service, extra Junk goes through wallet.battle_junk, prices and
 * Scavenger / Lucky Find through "junk". With this mod off, the wallet keeps them as loaded: pity
 * resumes when it is on again.
 *
 * Battle end (PE_HOOK_BATTLE_REWARDS), in three steps: the wallet (early) takes vanilla Junk off
 * the list; this mod (normal) adds its Junk and appends its rares; the wallet (late) shows all
 * the Junk as one "Junk xN" entry. tests/pe_mod_loot_parity_test.c checks the result is the same,
 * byte for byte, as when this was one handler.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../pe_economy/pe_econ_engine.h"
#include "pe_game.h"
#include "pe_mod_host.h"
#include "pe_mod_menu.h"
#include "pe_pages.h"
#include "pe_registry.h"
#include "pe_services.h"
#include "pe1/battle.h"
#include "pe1/draw_state.h"
#include "pe1/inventory_state.h"
#include "pe1/text.h"

#define PE_RW_ID              "pe_rewards"
#define PE_RW_ITEM_JUNK       0x14
#define PE_RW_ITEM_MOD_PERMIT 0x37
#define PE_RW_BAG_SLOTS       50
#define PE_RW_STORAGE_SLOTS   100
#define PE_RW_REWARD_SLOTS    10
#define PE_RW_ENEMY_SLOTS     7
#define PE_RW_RARE_COLOR      0x0030A8D0u /* gold (0xBBGGRR) */
#define PE_RW_TUNING_PAGE     PE_PAGE_DEBUG_LOOT

static int s_rw_tune_floor = 1, s_rw_tune_exp_per_junk = 8, s_rw_tune_battle_cap = 12, s_rw_tune_boss_cap = 25;
static int s_rw_tune_nohit_bonus = 25, s_rw_tune_rare_base = 4, s_rw_tune_rare_step = 1, s_rw_tune_rare_guarantee = 25;
static int s_rw_tune_econ_guarantee = 8, s_rw_tune_permit_guarantee = 8;

/* Difficulty (the wallet's): Relaxed, Normal, Grind. */
static const uint16_t s_rw_preset_junk[3] = { 150, 100, 75 };
static const uint16_t s_rw_preset_pity[3] = { 72, 100, 132 };
static const uint16_t s_rw_preset_pity_base[3] = { 150, 100, 50 };
static const uint16_t s_rw_preset_pity_step[3] = { 150, 100, 60 };

static PeRewardState s_rw;          /* the wallet's copy of our fields, while we work on it */
static uint32_t s_rw_frames;
static int s_rw_in_battle, s_rw_boss_battle;
static struct {
    uint16_t rare_shown[2];
    uint32_t rare_before[2];
    int rare_is_econ[2];
    int rare_list_seen;
    int econ_shown;                 /* economy mod index on the list (as a Mod Permit), or -1 */
    uint32_t permits_before;
    int econ_list_seen;
} s_rw_found = { .econ_shown = -1 };
static int s_rw_color_saved;
static unsigned s_rw_saved_color, s_rw_saved_color_shaded;
static u8 s_rw_econ_label[32];

static const PeWalletApi *pe_rw_wallet(void) {
    const PeWalletApi *w = (const PeWalletApi *)pe_service_get("wallet", 1);
    return w && w->struct_size >= sizeof(PeWalletApi) ? w : NULL;
}
static const PeJunkApi *pe_rw_junk(void) { return (const PeJunkApi *)pe_service_get("junk", 1); }
static const PeCatalogApi *pe_rw_catalog(void) { return (const PeCatalogApi *)pe_service_get("catalog", 1); }
static int pe_rw_enabled(void) { const PeJunkApi *j = pe_rw_junk(); return j && j->enabled() && pe_rw_wallet(); }
static int pe_rw_preset(void) { const PeWalletApi *w = pe_rw_wallet(); const int p = w ? w->preset() : 1; return p >= 0 && p < 3 ? p : 1; }
static int pe_rw_armor_has(int effect) { const PeJunkApi *j = pe_rw_junk(); return j && j->armor_has(effect); }
static void pe_rw_load(void) { const PeWalletApi *w = pe_rw_wallet(); if (w) w->get_rewards(&s_rw); }
static void pe_rw_store(void) { const PeWalletApi *w = pe_rw_wallet(); if (w) w->put_rewards(&s_rw); }

/* ---- small helpers ---- */
static int pe_rw_bag_limit(void) {
    int limit = g_AyaSaveState.inventory_slot_count + g_InvBonusSlotCount;
    return limit > PE_RW_BAG_SLOTS ? PE_RW_BAG_SLOTS : limit;
}
static uint32_t pe_rw_bag_count_of(int v) {
    uint32_t n = 0;
    int i;
    for (i = 0; i < PE_RW_BAG_SLOTS; ++i) n += g_AyaSaveState.inventory_items[i] == v;
    return n;
}
static uint32_t pe_rw_held(int v) {
    uint32_t n = pe_rw_bag_count_of(v);
    int i;
    for (i = 0; i < PE_RW_STORAGE_SLOTS; ++i) n += g_WayneStorageItems[i] == v;
    return n;
}
static int pe_rw_free_bag_slot(int limit) {
    int i;
    for (i = 0; i < limit && i < PE_RW_BAG_SLOTS; ++i)
        if (g_AyaSaveState.inventory_items[i] == 0) return i;
    return -1;
}
static int pe_rw_items_found_open(void) { return g_InvActiveListOverride && g_InvItemPtr == g_InvActiveListOverride; }
static const u8 *pe_rw_item_name(int id) { return id > 0 ? (const u8 *)Text_LookupTable8((unsigned)id - 1u) : NULL; }

/* ---- rare drops ---- */
#define PE_RARE_BOOSTER 0u
#define PE_RARE_ECON    1u
#define PE_ECON_DROP    0x8000u
#define PE_ECON_COUNT   4
enum { RC_ECON, RC_SUPER_TOOL, RC_PERMIT, RC_CHARM, RC_DUPER, RC_SUPER, RC_TOOL, RC_BOOSTER, RC_COUNT };
static const struct { uint8_t chance; uint16_t item; const char *name; } s_rw_cascade[RC_COUNT] = {
    [RC_ECON] = { 15, PE_RARE_ECON, "econ" },      [RC_SUPER_TOOL] = { 6, 0x17u, "supertool" },
    [RC_PERMIT] = { 15, 0x37u, "permit" },         [RC_CHARM] = { 8, 0x19u, "charm" },
    [RC_DUPER] = { 4, 0x0Cu, "duper" },            [RC_SUPER] = { 8, 0x0Bu, "super" },
    [RC_TOOL] = { 0, 0x16u, "tool" },              [RC_BOOSTER] = { 100, PE_RARE_BOOSTER, "booster" },
};
static const uint8_t s_rw_tool_chance[5] = { 85, 65, 40, 20, 10 };
static const struct { int effect; const char *name; uint8_t weight; } s_rw_econ_mods[PE_ECON_COUNT] = {
    { PE_FX_SCAVENGER, "Scavenger mod", 1 }, { PE_FX_LUCKY_FIND, "Lucky Find mod", 2 },
    { PE_FX_SALVAGER, "Salvager mod", 4 },   { PE_FX_BARGAIN, "Bargain mod", 3 },
};
static char s_rw_cascade_path[96];
static const uint16_t s_rw_booster_base[] = { 0x1Du, 0x21u, 0x25u, 0x29u, 0x2Du, 0x31u };

static uint32_t pe_rw_rand(void) {
    uint32_t x = s_rw.rng ? s_rw.rng : (0x9E3779B9u ^ (s_rw_frames * 2654435761u)) | 1u;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    s_rw.rng = x;
    return x;
}

static int pe_rw_econ_missing(int *out) {
    const PeCatalogApi *cat = pe_rw_catalog();
    int n = 0, i;
    if (!cat) return 0; /* no Mod Catalog: economy mods can't drop */
    for (i = 0; i < PE_ECON_COUNT; ++i)
        if (!cat->armor_unlocked(s_rw_econ_mods[i].effect)) out[n++] = i;
    return n;
}

static int pe_rw_pick_econ(const int *missing, int n) {
    uint32_t total = 0, r;
    int i;
    for (i = 0; i < n; ++i) total += s_rw_econ_mods[missing[i]].weight;
    r = pe_rw_rand() % (total ? total : 1u);
    for (i = 0; i < n; ++i) {
        if (r < s_rw_econ_mods[missing[i]].weight) return missing[i];
        r -= s_rw_econ_mods[missing[i]].weight;
    }
    return missing[0];
}

static int pe_rw_cascade_skip(int k, int econ_missing, uint32_t *pct) {
    uint32_t held;
    *pct = s_rw_cascade[k].chance;
    switch (k) {
    case RC_ECON: return econ_missing == 0;
    case RC_SUPER_TOOL: return pe_rw_held(0x36) > 0;              /* Super Tool Kit */
    case RC_PERMIT: return pe_rw_held(0x37) >= 2u;
    case RC_CHARM: return 1;                                      /* 0x19 is Panacea now */
    case RC_TOOL:
        if (pe_rw_held(0x35) || pe_rw_held(0x36)) return 1;       /* either kit: Tools are free */
        held = pe_rw_held(0x16);
        if (held >= 5u) return 1;
        *pct = s_rw_tool_chance[held];
        return 0;
    default: return 0;
    }
}

static uint16_t pe_rw_pick_rare(uint32_t exp) {
    int missing[PE_ECON_COUNT], hit = RC_BOOSTER, k;
    const int econ = pe_rw_econ_missing(missing);
    unsigned o = 0;
    uint32_t tier;
    s_rw_cascade_path[0] = '\0';
    for (k = 0; k < RC_COUNT; ++k) {
        uint32_t pct, guarantee = 0, since = 0;
        int certain;
        if (pe_rw_cascade_skip(k, econ, &pct)) {
            if (o < sizeof s_rw_cascade_path - 12u)
                o += (unsigned)snprintf(s_rw_cascade_path + o, sizeof s_rw_cascade_path - o, "-%s", s_rw_cascade[k].name);
            continue;
        }
        if (k == RC_ECON) { guarantee = (uint32_t)s_rw_tune_econ_guarantee; since = s_rw.econ_since; }
        if (k == RC_PERMIT) { guarantee = (uint32_t)s_rw_tune_permit_guarantee; since = s_rw.permit_since; }
        certain = guarantee && since + 1u >= guarantee;
        if (k == RC_BOOSTER || certain || pe_rw_rand() % 100u < pct) {
            hit = k;
            if (o < sizeof s_rw_cascade_path - 16u)
                snprintf(s_rw_cascade_path + o, sizeof s_rw_cascade_path - o, "+%s%s", s_rw_cascade[k].name, certain ? "!" : "");
            break;
        }
    }
    if (econ) s_rw.econ_since = hit == RC_ECON ? 0 : (uint8_t)(s_rw.econ_since < 250 ? s_rw.econ_since + 1 : 250);
    if (pe_rw_held(0x37) < 2u)
        s_rw.permit_since = hit == RC_PERMIT ? 0 : (uint8_t)(s_rw.permit_since < 250 ? s_rw.permit_since + 1 : 250);
    if (hit == RC_ECON) return (uint16_t)(PE_ECON_DROP | (uint32_t)pe_rw_pick_econ(missing, econ));
    if (hit != RC_BOOSTER) return s_rw_cascade[hit].item;
    tier = exp < 20u ? 0u : exp < 60u ? 1u : exp < 150u ? 2u : 3u;
    return (uint16_t)(s_rw_booster_base[pe_rw_rand() % 6u] + tier);
}

static uint32_t pe_rw_rare_guarantee(void) {
    const uint32_t g = (uint32_t)s_rw_tune_rare_guarantee * s_rw_preset_pity[pe_rw_preset()] / 100u;
    return g ? g : 1u;
}

static uint32_t pe_rw_pity_step(void) {
    uint32_t step = pe_rw_armor_has(PE_FX_LUCKY_FIND) ? 2u : 1u;
    if (s_rw.charm_left) step *= 2u;
    return step;
}

static uint16_t pe_rw_roll_rare(uint32_t exp) {
    const uint32_t dry = s_rw.rare_dry, preset = (uint32_t)pe_rw_preset();
    const uint32_t chance = (uint32_t)s_rw_tune_rare_base * 10u * s_rw_preset_pity_base[preset] / 100u +
                            (uint32_t)s_rw_tune_rare_step * 10u * s_rw_preset_pity_step[preset] / 100u * dry;
    uint32_t next;
    if (dry + 1u >= pe_rw_rare_guarantee() || pe_rw_rand() % 1000u < chance) {
        s_rw.rare_dry = 0;
        return pe_rw_pick_rare(exp);
    }
    next = dry + pe_rw_pity_step();
    s_rw.rare_dry = next > 0xFFFFu ? 0xFFFFu : (uint16_t)next;
    return 0;
}

/* ---- the opt-in Economy path: one engine per battle, so Enhanced's fields above stay frozen ---- */
static uint32_t s_rw_policy_seq[2];   /* the pending entries on the open list, by sequence */

static uint32_t pe_rw_copies(int item) {
    const PeStacksApi *stacks = (const PeStacksApi *)pe_service_get("stacks", 1);
    return stacks ? (uint32_t)stacks->total(0, (uint16_t)item) + (uint32_t)stacks->total(1, (uint16_t)item) : pe_rw_held(item);
}

static int pe_rw_list_has(const PeHookRewards *r, uint32_t kept, int item) {
    uint32_t i;
    for (i = 0; i < kept; ++i) if (r->list[i].id == item) return 1;
    return 0;
}

/* Show the pending item and bundle at the end of the list; what doesn't fit waits. */
static void pe_rw_policy_show(const PeHookRewards *r, PeEconProgress *g) {
    static const int slots[2] = {PE_ECON_SLOT_ITEM, PE_ECON_SLOT_BUNDLE};
    const PeCatalogApi *cat = pe_rw_catalog();
    uint32_t kept = 0;
    int a;
    while (kept < PE_RW_REWARD_SLOTS && r->list[kept].id != 0) ++kept;
    s_rw_found.rare_shown[0] = s_rw_found.rare_shown[1] = 0;
    s_rw_found.rare_is_econ[0] = s_rw_found.rare_is_econ[1] = 0;
    s_rw_found.rare_list_seen = 0;
    s_rw_found.econ_shown = -1;
    s_rw_found.econ_list_seen = 0;
    s_rw_policy_seq[0] = s_rw_policy_seq[1] = 0;
    for (a = 0; a < 2; ++a) {
        PeEconPending *e = &g->pending[slots[a]];
        int id = e->content;
        if (!e->kind) continue;
        if (e->kind == PE_ECON_PENDING_EFFECT) {
            const int mod = e->content % PE_ECON_COUNT;
            if (!cat) continue;                 /* no Mod Catalog: it waits */
            if (cat->armor_unlocked(s_rw_econ_mods[mod].effect)) {
                /* Unlocked some other way: already satisfied, with no Junk and no reroll. */
                char msg[64];
                pe_log("econ_settled", "mod=%s", s_rw_econ_mods[mod].name);
                snprintf(msg, sizeof msg, "%s: already unlocked", s_rw_econ_mods[mod].name);
                pe_toast(msg);
                memset(e, 0, sizeof *e);
                continue;
            }
            /* Carried by a Mod Permit entry: never next to a real one. */
            if (pe_rw_list_has(r, kept, PE_RW_ITEM_MOD_PERMIT)) continue;
            s_rw_found.econ_shown = mod;
            s_rw_found.permits_before = pe_rw_bag_count_of(PE_RW_ITEM_MOD_PERMIT);
            id = PE_RW_ITEM_MOD_PERMIT;
        }
        if (kept >= PE_RW_REWARD_SLOTS) {
            if (e->kind == PE_ECON_PENDING_EFFECT) s_rw_found.econ_shown = -1;
            continue;
        }
        r->list[kept].id = (s16)id;
        r->list[kept++].extra = -1;
        s_rw_found.rare_shown[a] = (uint16_t)id;
        s_rw_found.rare_before[a] = pe_rw_bag_count_of(id);
        s_rw_found.rare_is_econ[a] = e->kind == PE_ECON_PENDING_EFFECT;
        s_rw_policy_seq[a] = e->sequence;
    }
}

static void pe_rw_policy_battle(const PeHookRewards *r, const PeWalletApi *wallet, const PeEconState *state) {
    PeEconProgress g = state->progress;
    PeEconBattleIn in;
    PeEconBattleOut out;
    int missing[PE_ECON_COUNT], econ, s, i;
    memset(&in, 0, sizeof in);
    econ = pe_rw_econ_missing(missing);
    for (i = 0; i < econ; ++i) in.econ_missing |= (uint8_t)(1u << missing[i]);
    for (s = 0; s < PE_RW_ENEMY_SLOTS; ++s) in.hits += s_rw.hits[s];
    in.exp = (uint32_t)r->exp;
    in.wallet_room = wallet->room();
    in.generation = pe_reg_generation();
    in.held_super_tool = pe_rw_copies(0x36);
    in.held_permits = pe_rw_copies(PE_RW_ITEM_MOD_PERMIT);
    in.held_tools = pe_rw_copies(0x16);
    in.tool_kit = pe_rw_copies(0x35) || in.held_super_tool;
    in.boss = (uint8_t)s_rw_boss_battle;
    in.has_list = r->list != NULL;
    in.scavenger = (uint8_t)pe_rw_armor_has(PE_FX_SCAVENGER);
    in.lucky_find = (uint8_t)pe_rw_armor_has(PE_FX_LUCKY_FIND);
    in.econ_guarantee = (uint8_t)s_rw_tune_econ_guarantee;
    in.permit_guarantee = (uint8_t)s_rw_tune_permit_guarantee;
    in.preview = pe_economy_policy_preview;
    pe_econ_battle(&state->config, &g, &in, &out);
    if (r->list) pe_rw_policy_show(r, &g);
    wallet->policy_commit(&g);
    if (out.junk) wallet->battle_junk(out.junk);
    if (out.notify) (void)pe_economy_policy_notify(&out.context, &out.result);   /* after the commit */
    if (out.paused) pe_toast("Junk counter is full: extra Junk is waiting and income is paused");
    if (out.preview_failed) pe_log("policy", "payout preview refused: no extra Junk this battle");
    pe_log("loot", "policy exp=%u junk=%u hits=%u boss=%d dry=%u item=%u bundle=%u held=%u",
           (unsigned)in.exp, (unsigned)out.junk, (unsigned)in.hits, s_rw_boss_battle, (unsigned)g.dry,
           (unsigned)g.pending[PE_ECON_SLOT_ITEM].kind, (unsigned)g.pending[PE_ECON_SLOT_BUNDLE].kind,
           (unsigned)g.pending[PE_ECON_SLOT_CURRENCY].amount);
}

/* The list closed: taken or left, what it showed is finished (left means declined). */
static void pe_rw_policy_settle(void) {
    static const int slots[2] = {PE_ECON_SLOT_ITEM, PE_ECON_SLOT_BUNDLE};
    const PeWalletApi *wallet = pe_rw_wallet();
    const PeEconState *state = wallet ? wallet->policy() : NULL;
    int a, changed = 0;
    if (state) {
        PeEconProgress g = state->progress;
        for (a = 0; a < 2; ++a)
            if (s_rw_policy_seq[a] && g.pending[slots[a]].sequence == s_rw_policy_seq[a]) {
                memset(&g.pending[slots[a]], 0, sizeof g.pending[slots[a]]);
                changed = 1;
            }
        if (changed) wallet->policy_commit(&g);
    }
    s_rw_policy_seq[0] = s_rw_policy_seq[1] = 0;
}

/* ---- battle end: our Junk, then our rares at the end of the list (the wallet took vanilla Junk
 * off it first and puts the "Junk xN" entry last after us) ---- */
static int pe_rw_on_rewards(void *ctx, void *user) {
    const PeHookRewards *r = (const PeHookRewards *)ctx;
    const PeWalletApi *wallet = pe_rw_wallet();
    const uint32_t exp = (uint32_t)r->exp, per = s_rw_tune_exp_per_junk > 0 ? (uint32_t)s_rw_tune_exp_per_junk : 1u;
    const uint32_t cap = (uint32_t)(s_rw_boss_battle ? s_rw_tune_boss_cap : s_rw_tune_battle_cap);
    uint32_t earned = (uint32_t)s_rw_tune_floor + exp / per, hits = 0;
    int s;
    (void)user;
    if (!pe_rw_enabled()) return 0;
    pe_rw_load();
    if (wallet->policy()) {
        pe_rw_policy_battle(r, wallet, wallet->policy());
        return 0;
    }
    if (earned > cap) earned = cap;
    for (s = 0; s < PE_RW_ENEMY_SLOTS; ++s) hits += s_rw.hits[s];
    if (hits < 4) earned += earned * (uint32_t)s_rw_tune_nohit_bonus * (4u - hits) / 400u;
    earned = earned * s_rw_preset_junk[pe_rw_preset()] / 100u;
    if (pe_rw_armor_has(PE_FX_SCAVENGER)) earned += earned / 2u;
    wallet->battle_junk(earned);

    if (r->list) {
        uint32_t kept = 0;
        unsigned dry_before;
        uint16_t rolled, adds[2], carried;
        int a;
        while (kept < PE_RW_REWARD_SLOTS && r->list[kept].id != 0) ++kept;   /* the wallet compacted it */
        if (s_rw.charm_left) --s_rw.charm_left;
        dry_before = s_rw.rare_dry;
        carried = s_rw.rare_carry;
        rolled = pe_rw_roll_rare(exp);
        pe_log("loot", "exp=%u junk=%u hits=%u boss=%d preset=%d scav=%d rare_dry=%u rolled=0x%04X carried=0x%04X charm=%u path=%s",
               (unsigned)exp, (unsigned)earned, (unsigned)hits, s_rw_boss_battle, pe_rw_preset(), pe_rw_armor_has(PE_FX_SCAVENGER),
               dry_before, rolled, carried, (unsigned)s_rw.charm_left, rolled ? s_rw_cascade_path : "-");
        adds[0] = s_rw.rare_carry;
        adds[1] = rolled;
        s_rw.rare_carry = 0;
        s_rw_found.rare_shown[0] = s_rw_found.rare_shown[1] = 0;
        s_rw_found.rare_list_seen = 0;
        s_rw_found.econ_shown = -1;
        s_rw_found.econ_list_seen = 0;
        for (a = 0; a < 2; ++a) {
            uint16_t id = adds[a];
            const PeCatalogApi *cat = pe_rw_catalog();
            if (!id) continue;
            /* A carried-over economy mod unlocked since: a fresh rare instead. */
            if ((id & PE_ECON_DROP) && (!cat || cat->armor_unlocked(s_rw_econ_mods[(id & 0xFFu) % PE_ECON_COUNT].effect)))
                id = pe_rw_pick_rare(exp);
            if (id & PE_ECON_DROP) {   /* carried by a Mod Permit entry: never next to a real one, never two */
                if (s_rw_found.econ_shown >= 0 || adds[1 - a] == PE_RW_ITEM_MOD_PERMIT || kept >= PE_RW_REWARD_SLOTS) {
                    if (!s_rw.rare_carry) s_rw.rare_carry = id;
                    continue;
                }
                s_rw_found.econ_shown = (int)(id & 0xFFu) % PE_ECON_COUNT;
                s_rw_found.permits_before = pe_rw_bag_count_of(PE_RW_ITEM_MOD_PERMIT);
                id = PE_RW_ITEM_MOD_PERMIT;
            }
            if (kept < PE_RW_REWARD_SLOTS) {
                r->list[kept].id = (s16)id;
                r->list[kept++].extra = -1;
                s_rw_found.rare_shown[a] = id;
                s_rw_found.rare_before[a] = pe_rw_bag_count_of(id);
                s_rw_found.rare_is_econ[a] = id == PE_RW_ITEM_MOD_PERMIT && s_rw_found.econ_shown >= 0;
            } else {
                s_rw.rare_carry = id;
            }
        }
    }
    pe_rw_store();
    return 0;
}

/* ---- items-found rows: "<mod> mod", gold names ---- */
static void pe_rw_restore_text_color(void) {
    if (!s_rw_color_saved) return;
    g_DrawPrimColor = s_rw_saved_color;
    g_DrawColorShaded = s_rw_saved_color_shaded;
    s_rw_color_saved = 0;
}

static u8 *pe_rw_ascii_label(u8 *out, const char *ascii) {
    unsigned o = 0;
    for (; *ascii && o < 30u; ++ascii, ++o) {
        const char c = *ascii;
        out[o] = (c >= 'A' && c <= 'Z') ? (u8)(0x10 + c - 'A') : (c >= 'a' && c <= 'z') ? (u8)(0x30 + c - 'a')
               : (c >= '0' && c <= '9') ? (u8)(c - '0') : 0x0Fu;
    }
    out[o] = 0xFF;
    return out;
}

static int pe_rw_on_slot_row(void *ctx, void *user) {
    u8 **text = (u8 **)ctx;
    int a;
    (void)user;
    pe_rw_restore_text_color();
    if (!s_rw_found.rare_shown[0] && !s_rw_found.rare_shown[1]) return 0;
    if (!pe_rw_items_found_open() || !*text) return 0;
    for (a = 0; a < 2; ++a) {
        if (!s_rw_found.rare_shown[a] || pe_rw_item_name(s_rw_found.rare_shown[a]) != *text) continue;
        if (s_rw_found.econ_shown >= 0 && s_rw_found.rare_shown[a] == PE_RW_ITEM_MOD_PERMIT)
            *text = pe_rw_ascii_label(s_rw_econ_label, s_rw_econ_mods[s_rw_found.econ_shown].name);
        s_rw_saved_color = g_DrawPrimColor;
        s_rw_saved_color_shaded = g_DrawColorShaded;
        g_DrawPrimColor = PE_RW_RARE_COLOR;
        g_DrawColorShaded = (PE_RW_RARE_COLOR >> 1) & 0x7F7F7Fu;
        s_rw_color_saved = 1;
        return 0;
    }
    return 0;
}

/* ---- per frame ---- */
static void pe_rw_forget_list(void) {
    s_rw_found.rare_shown[0] = s_rw_found.rare_shown[1] = 0;
    s_rw_found.econ_shown = -1;
    pe_rw_restore_text_color();
}

/* With the bag full, an economy mod on the list is taken where it stands. */
static void pe_rw_econ_without_room(void) {
    const PeCatalogApi *cat = pe_rw_catalog();
    int n = g_InvActiveListCount > 10 ? 10 : g_InvActiveListCount, i;
    if (g_InvActiveListOverride != g_InvActiveListItems || pe_rw_free_bag_slot(pe_rw_bag_limit()) >= 0) return;
    for (i = 0; i < n && s_rw_found.econ_shown >= 0; ++i) {
        if (g_InvActiveListItems[i] != PE_RW_ITEM_MOD_PERMIT) continue;
        g_InvActiveListItems[i] = 0;
        if (cat) cat->unlock_armor(s_rw_econ_mods[s_rw_found.econ_shown].effect);
        pe_log("econ_unlock", "mod=%s source=drop no_room=1", s_rw_econ_mods[s_rw_found.econ_shown].name);
        s_rw_found.econ_shown = -1;
        s_rw_found.econ_list_seen = 0;
    }
}

static void pe_rw_tick(void) {
    static int was_enabled = -1;
    const int enabled = pe_rw_enabled();
    int battle, i, s;
    ++s_rw_frames;
    if (was_enabled != enabled) {
        if (was_enabled >= 0) pe_rw_forget_list();
        s_rw_in_battle = enabled && pe_game_loaded() && pe_in_battle();
        was_enabled = enabled;
    }
    if (!enabled || !pe_game_loaded()) { s_rw_in_battle = 0; return; }

    pe_rw_restore_text_color();
    pe_rw_econ_without_room();
    if (s_rw_found.econ_shown >= 0) {      /* the "<mod> mod" entry, not taken yet */
        if (pe_rw_bag_count_of(PE_RW_ITEM_MOD_PERMIT) > s_rw_found.permits_before) {
            const PeCatalogApi *cat = pe_rw_catalog();
            for (i = 0; i < PE_RW_BAG_SLOTS; ++i)
                if (g_AyaSaveState.inventory_items[i] == PE_RW_ITEM_MOD_PERMIT) { g_AyaSaveState.inventory_items[i] = 0; break; }
            if (cat) cat->unlock_armor(s_rw_econ_mods[s_rw_found.econ_shown].effect);
            pe_log("econ_unlock", "mod=%s source=drop", s_rw_econ_mods[s_rw_found.econ_shown].name);
            s_rw_found.econ_shown = -1;
        } else if (g_InvActiveListOverride) {
            s_rw_found.econ_list_seen = 1;
        } else if (s_rw_found.econ_list_seen) {
            pe_log("econ_left", "mod=%s", s_rw_econ_mods[s_rw_found.econ_shown].name);
            s_rw_found.econ_shown = -1;
        }
    }
    if (s_rw_found.rare_shown[0] || s_rw_found.rare_shown[1]) {
        if (g_InvActiveListOverride) s_rw_found.rare_list_seen = 1;
        else if (s_rw_found.rare_list_seen) {
            int a;
            for (a = 0; a < 2; ++a) {
                if (!s_rw_found.rare_shown[a] || s_rw_found.rare_is_econ[a]) continue;
                pe_log(pe_rw_bag_count_of(s_rw_found.rare_shown[a]) > s_rw_found.rare_before[a] ? "rare_take" : "rare_left",
                       "item=0x%02X", (unsigned)s_rw_found.rare_shown[a]);
            }
            pe_rw_policy_settle();
            s_rw_found.rare_shown[0] = s_rw_found.rare_shown[1] = 0;
        }
    }

    battle = pe_in_battle();
    if (battle && !s_rw_in_battle) {       /* a battle starts */
        s_rw_policy_seq[0] = s_rw_policy_seq[1] = 0;   /* not closed: still pending */
        s_rw_found.rare_shown[0] = s_rw_found.rare_shown[1] = 0;
        s_rw_found.econ_shown = -1;
        s_rw_boss_battle = 0;
        pe_rw_load();
        memset(s_rw.hits, 0, sizeof s_rw.hits);
        pe_rw_store();
    }
    s_rw_in_battle = battle;
    if (battle) {
        int changed = 0;
        pe_rw_load();
        for (s = 0; s < PE_RW_ENEMY_SLOTS; ++s) {
            const EnemyCombatant *core = &g_BattleEnemySlots[s].combatant;
            if (!g_BattleEnemySlots[s].active) continue;
            if (core->rewardFactorB > s_rw.hits[s]) { s_rw.hits[s] = core->rewardFactorB; changed = 1; }   /* +0x9C: hits on Aya */
            if (core->statusFlags2 & 0x40000u) s_rw_boss_battle = 1;
        }
        if (changed) pe_rw_store();
    }
}

/* ---- menu ---- */
static int pe_rw_rare_within(void) {
    int left, step;
    pe_rw_load();
    left = (int)pe_rw_rare_guarantee() - (int)s_rw.rare_dry;
    step = (int)pe_rw_pity_step();
    return left < 1 ? 1 : (left + step - 1) / step;
}
static void pe_rw_rare_text(char *out, unsigned size) {
    const PeWalletApi *wallet = pe_rw_wallet();
    const PeEconState *policy = wallet ? wallet->policy() : NULL;
    if (!pe_game_loaded() || !wallet) snprintf(out, size, "-");
    else if (policy) {   /* the new path has its own guarantee, or none */
        const int guarantee = (policy->config.flags & PE_ECON_F_BONUS_ITEM) ? policy->config.item_guarantee : 0;
        const int left = guarantee - (int)policy->progress.dry;
        if (!guarantee) snprintf(out, size, "-");
        else snprintf(out, size, "%d battle%s", left < 1 ? 1 : left, left <= 1 ? "" : "s");
    } else snprintf(out, size, "%d battle%s", pe_rw_rare_within(), pe_rw_rare_within() == 1 ? "" : "s");
}

static int rewards_init(void) {
    const char *mod_id = PE_RW_ID;
    pe_menu_register_info(PE_PAGE_ECONOMY, "Rare drop within", "Won battles until a rare drop is certain.", pe_rw_rare_text);
    pe_menu_register_int(PE_RW_TUNING_PAGE, "Junk floor", "Junk for every won battle.", &s_rw_tune_floor, 0, 10, 1);
    pe_menu_register_int(PE_RW_TUNING_PAGE, "EXP per Junk", "Battle EXP for each extra Junk.", &s_rw_tune_exp_per_junk, 1, 100, 1);
    pe_menu_register_int(PE_RW_TUNING_PAGE, "Battle Junk cap", "Most Junk from one battle.", &s_rw_tune_battle_cap, 1, 200, 1);
    pe_menu_register_int(PE_RW_TUNING_PAGE, "Boss Junk cap", "Most Junk from a boss fight.", &s_rw_tune_boss_cap, 1, 200, 1);
    pe_menu_register_int(PE_RW_TUNING_PAGE, "No-hit bonus %", "Extra Junk for a no-hit battle.", &s_rw_tune_nohit_bonus, 0, 100, 5);
    pe_menu_register_int(PE_RW_TUNING_PAGE, "Rare base %", "Rare drop chance after a drop.", &s_rw_tune_rare_base, 0, 100, 1);
    pe_menu_register_int(PE_RW_TUNING_PAGE, "Rare step %", "Added for each battle without one.", &s_rw_tune_rare_step, 0, 50, 1);
    pe_menu_register_int(PE_RW_TUNING_PAGE, "Rare guarantee", "Battles until a rare drop is certain.", &s_rw_tune_rare_guarantee, 1, 200, 1);
    pe_menu_register_int(PE_RW_TUNING_PAGE, "Economy mod guarantee", "Rares until an economy mod is certain.", &s_rw_tune_econ_guarantee, 1, 50, 1);
    pe_menu_register_int(PE_RW_TUNING_PAGE, "Mod Permit guarantee", "Rares until a Mod Permit is certain.", &s_rw_tune_permit_guarantee, 1, 50, 1);
    pe_menu_register_tick(pe_rw_tick);
    pe_hook_add(PE_HOOK_BATTLE_REWARDS, pe_rw_on_rewards, NULL, PE_PRIORITY_NORMAL, mod_id);
    pe_hook_add(PE_HOOK_SLOT_ROW, pe_rw_on_slot_row, NULL, PE_PRIORITY_NORMAL, mod_id);
    return 0;
}

static const char *const s_rw_requires[] = { "pe_economy", NULL };   /* the Junk wallet */
const PeMod pe_mod_pe_rewards = {PE_RW_ID, "PE Enhancements: battle rewards", PE_ENHANCEMENTS_VERSION, s_rw_requires,
                                 rewards_init, NULL};
