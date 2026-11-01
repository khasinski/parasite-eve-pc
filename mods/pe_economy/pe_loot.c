/*
 * Loot overhaul: the Junk counter, battle Junk and rare drops. Ported from the psxrecomp build
 * (src/mods/pe_loot.c; bridge findings pe-loot-overhaul-design-2026-10-11.md,
 * pe-loot-cascade-design-2026-10-14.md, pe-loot-overhaul-checklist.md).
 *
 * Junk never takes a bag slot: it lives in one counter.
 *   - Battle end (PE_HOOK_BATTLE_REWARDS, Aya_SetTotalExp entry, once per won battle): vanilla
 *     Junk leaves the reward list for the counter; the battle pays floor + EXP / divisor
 *     (capped, boss cap for "can't escape" fights), a no-hit bonus and Junk stolen this battle,
 *     scaled by the preset and Scavenger. It shows on the items-found screen as one Junk entry
 *     whose name reads "Junk xN" (PE_HOOK_SLOT_ROW); taken, left behind or with no room it is
 *     paid all the same (it needs no slot).
 *   - Rare drops: soft pity per won battle, then the cascade (economy mod, Super Tool, Mod
 *     Permit, Duper / Super Junk, Tool by Tools held, else a stat booster by EXP tier), appended
 *     to the reward list and drawn in gold. An economy mod shows as a Mod Permit entry named
 *     "<mod> mod"; taking it unlocks the mod in the Mod Catalog.
 *   - Inv_AddItem(Junk) (PE_HOOK_ITEM_ADD): the id becomes 0 (adds nothing, reports success)
 *     and the Junk is counted, or banked until the battle is won for a steal (PE_HOOK_STEAL);
 *     an empty steal banks a little Junk.
 *   - Wayne's "Discard junk" (PE_HOOK_ITEM_COUNT on the script's count, PE_HOOK_INV_COMMAND on
 *     each removal): the count reads a list of N Junk and each removal gets one Junk from the
 *     counter placed just in time, so only one free bag slot is needed.
 *   - A sweep (free field only) moves Junk, Super Junk (75) and Duper Junk (150) from the bag
 *     and Wayne's storage into the counter.
 *   - The stolen item's name in the steal message is wrapped in the textbox's red highlight.
 *   - Saved by the host as the "pe_economy.loot" record (host/pe_save.h); saves from before, with
 *     the recomp build's "PELT" block, still load. A new game resets it.
 *
 * Other mods: publishes the "junk" service (pe_economy.h); uses "catalog" (economy mod unlocks)
 * and "keys" (key item pocket) when those are present.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "pe_economy.h"
#include "pe_game.h"
#include "pe_mod_host.h"
#include "pe_mod_menu.h"
#include "pe_pages.h"
#include "pe_save.h"
#include "pe1/battle.h"
#include "pe1/draw_state.h"
#include "pe1/game_timers.h"
#include "pe1/inventory_state.h"
#include "pe1/text.h"

#define MOD_ID "pe_economy"
#define PE_INV_CMD_REMOVE_ITEM    1112
#define PE_ITEM_JUNK        0x14
#define PE_ITEM_SUPER_JUNK  0x0B
#define PE_ITEM_DUPER_JUNK  0x0C
#define PE_ITEM_MOD_PERMIT  0x37
#define PE_WAYNE_CRAFT_COST 300u
#define PE_BAG_SLOTS        50
#define PE_STORAGE_SLOTS    100
#define PE_REWARD_SLOTS     10
#define PE_RARE_COLOR       0x0030A8D0u /* gold (0xBBGGRR) */
#define PE_TAIL_MAGIC       0x544C4550u /* "PELT" */
#define PE_HANDIN_GUARD_FRAMES 120u
#define PE_HANDIN_MAX       999u
#define PE_ENEMY_SLOTS      7

static struct {
    uint32_t junk;
    uint16_t steal_bank;        /* Junk stolen this battle, paid on a win */
    uint16_t hits[PE_ENEMY_SLOTS];
    uint16_t rare_dry;          /* pity progress since the last rare drop */
    uint16_t rare_carry;        /* a rare drop that didn't fit the list */
    uint32_t rng;
    uint16_t charm_left;
    uint8_t preset;             /* 0 Relaxed, 1 Normal, 2 Grind */
    uint8_t disabled;           /* "Junk economy" switched off for this game */
    uint8_t econ_since, permit_since;
} s_st = { .preset = 1 };
/* s_st is the "pe_economy.loot" record as it is: a change of layout needs a new record version
 * and an upgrade() for v1, or every player's Junk resets on load. */
_Static_assert(sizeof s_st == 36, "pe_economy.loot v1 holds these 36 bytes: raise the version for a new layout");

static int s_enabled = 1;
static int s_handin_mode;       /* 0 = up to Wayne's next gun, 1 = all */
static int s_tune_floor = 1, s_tune_exp_per_junk = 8, s_tune_battle_cap = 12, s_tune_boss_cap = 25;
static int s_tune_nohit_bonus = 25, s_tune_rare_base = 4, s_tune_rare_step = 1, s_tune_rare_guarantee = 25;
static int s_tune_econ_guarantee = 8, s_tune_permit_guarantee = 8, s_tune_empty_steal = 2;

static const char *const s_preset_names[] = { "Relaxed", "Normal", "Grind" };
static const uint16_t s_preset_junk[3] = { 150, 100, 75 };
static const uint16_t s_preset_pity[3] = { 72, 100, 132 };
static const uint16_t s_preset_pity_base[3] = { 150, 100, 50 };
static const uint16_t s_preset_pity_step[3] = { 150, 100, 60 };
static const uint16_t s_preset_price[3] = { 75, 100, 125 };
static const char *const s_handin_names[] = { "Next gun", "All Junk" };

static int s_was_enabled = -1, s_wayne_check; /* checkpoint-owned tick latches */
static int s_in_battle, s_boss_battle;
static struct {
    uint16_t rare_shown[2];
    uint32_t rare_before[2];
    int rare_is_econ[2];
    int rare_list_seen;
    uint32_t junk_shown;        /* this battle's Junk, shown as "Junk xN" */
    int junk_list_seen;
    uint32_t bag_junk_before;
    int econ_shown;             /* economy mod index on the list (as a Mod Permit), or -1 */
    uint32_t permits_before;
    int econ_list_seen;
} s_found = { .econ_shown = -1 };
static uint32_t s_frames, s_last_count_frame, s_free_frames;
static int s_handin_active, s_list_redirected, s_wayne_waiting, s_wayne_dropped, s_next_add_is_steal;
static uint32_t s_handin_left;
static s16 s_scratch[PE_HANDIN_MAX];
static s32 s_saved_limit;
static uint16_t s_wayne_prev;
static int s_color_saved;
static unsigned s_saved_color, s_saved_color_shaded;
static u8 s_junk_label[32], s_econ_label[32];
static uint16_t s_steal_item;
static uint32_t s_steal_frame;

/* ---- small helpers ---- */
static int pe_bag_limit(void) {
    int limit = g_AyaSaveState.inventory_slot_count + g_InvBonusSlotCount;
    return limit > PE_BAG_SLOTS ? PE_BAG_SLOTS : limit;
}
static void pe_add_junk(uint32_t n) { s_st.junk = n >= 99999u - s_st.junk ? 99999u : s_st.junk + n; }
static uint32_t pe_bundle_value(int v) { return v == PE_ITEM_SUPER_JUNK ? 75u : v == PE_ITEM_DUPER_JUNK ? 150u : 0u; }

static uint32_t pe_bag_count_of(int v) {
    uint32_t n = 0;
    int i;
    for (i = 0; i < PE_BAG_SLOTS; ++i) n += g_AyaSaveState.inventory_items[i] == v;
    return n;
}
static uint32_t pe_held(int v) {
    uint32_t n = pe_bag_count_of(v);
    int i;
    for (i = 0; i < PE_STORAGE_SLOTS; ++i) n += g_WayneStorageItems[i] == v;
    return n;
}
static int pe_free_bag_slot(int limit) {
    int i;
    for (i = 0; i < limit && i < PE_BAG_SLOTS; ++i)
        if (g_AyaSaveState.inventory_items[i] == 0) return i;
    return -1;
}
static int pe_items_found_open(void) { return g_InvActiveListOverride && g_InvItemPtr == g_InvActiveListOverride; }
static const u8 *pe_item_name(int id) { return id > 0 ? (const u8 *)Text_LookupTable8((unsigned)id - 1u) : NULL; }

/* ---- the "junk" service ---- */
int pe_loot_enabled(void) { return s_enabled; }
uint32_t pe_loot_junk(void) { return s_st.junk; }
int pe_loot_spend_junk(uint32_t n) {
    if (s_st.junk < n) return 0;
    s_st.junk -= n;
    return 1;
}
void pe_loot_give_junk(uint32_t n) { pe_add_junk(n); }
int pe_loot_armor_has(int effect) {
    const ItemDataRecord *rec = pe_equipped(1);
    return rec && rec->kind == 9 && pe_rec_has_effect(rec, effect);
}
uint32_t pe_loot_price(uint32_t base) {
    uint32_t pct, p;
    if (!base) return 0;
    pct = s_preset_price[s_st.preset < 3 ? s_st.preset : 1];
    if (pe_loot_armor_has(PE_FX_BARGAIN)) pct = pct * 85u / 100u;
    p = (base * pct + 50u) / 100u;
    return p ? p : 1u;
}
int pe_loot_wayne_waiting(void) { return s_enabled && s_wayne_waiting && pe_wayne_count() >= PE_WAYNE_CRAFT_COST; }
int pe_loot_handin_blocked(void) {
    return s_enabled && pe_game_loaded() && s_st.junk && pe_free_bag_slot(pe_bag_limit()) < 0;
}
static const PeJunkApi s_junk_api = {
    pe_loot_enabled, pe_loot_junk, pe_loot_spend_junk, pe_loot_give_junk, pe_loot_armor_has, pe_loot_price,
    pe_loot_wayne_waiting, pe_loot_handin_blocked,
};

static const PeCatalogApi *pe_catalog(void) { return (const PeCatalogApi *)pe_service_get("catalog", 1); }

/* ---- rare drops ---- */
#define PE_RARE_BOOSTER 0u
#define PE_RARE_ECON    1u
#define PE_ECON_DROP    0x8000u
#define PE_ECON_COUNT   4
enum { RC_ECON, RC_SUPER_TOOL, RC_PERMIT, RC_CHARM, RC_DUPER, RC_SUPER, RC_TOOL, RC_BOOSTER, RC_COUNT };
static const struct { uint8_t chance; uint16_t item; const char *name; } s_cascade[RC_COUNT] = {
    [RC_ECON] = { 15, PE_RARE_ECON, "econ" },      [RC_SUPER_TOOL] = { 6, 0x17u, "supertool" },
    [RC_PERMIT] = { 15, 0x37u, "permit" },         [RC_CHARM] = { 8, 0x19u, "charm" },
    [RC_DUPER] = { 4, 0x0Cu, "duper" },            [RC_SUPER] = { 8, 0x0Bu, "super" },
    [RC_TOOL] = { 0, 0x16u, "tool" },              [RC_BOOSTER] = { 100, PE_RARE_BOOSTER, "booster" },
};
static const uint8_t s_tool_chance[5] = { 85, 65, 40, 20, 10 };
static const struct { int effect; const char *name; uint8_t weight; } s_econ_mods[PE_ECON_COUNT] = {
    { PE_FX_SCAVENGER, "Scavenger mod", 1 }, { PE_FX_LUCKY_FIND, "Lucky Find mod", 2 },
    { PE_FX_SALVAGER, "Salvager mod", 4 },   { PE_FX_BARGAIN, "Bargain mod", 3 },
};
static char s_cascade_path[96];
static const uint16_t s_booster_base[] = { 0x1Du, 0x21u, 0x25u, 0x29u, 0x2Du, 0x31u };

static uint32_t pe_rand(void) {
    uint32_t x = s_st.rng ? s_st.rng : (0x9E3779B9u ^ (s_frames * 2654435761u)) | 1u;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    s_st.rng = x;
    return x;
}

static int pe_econ_missing(int *out) {
    const PeCatalogApi *cat = pe_catalog();
    int n = 0, i;
    if (!cat) return 0; /* no Mod Catalog: economy mods can't drop */
    for (i = 0; i < PE_ECON_COUNT; ++i)
        if (!cat->armor_unlocked(s_econ_mods[i].effect)) out[n++] = i;
    return n;
}

static int pe_pick_econ(const int *missing, int n) {
    uint32_t total = 0, r;
    int i;
    for (i = 0; i < n; ++i) total += s_econ_mods[missing[i]].weight;
    r = pe_rand() % (total ? total : 1u);
    for (i = 0; i < n; ++i) {
        if (r < s_econ_mods[missing[i]].weight) return missing[i];
        r -= s_econ_mods[missing[i]].weight;
    }
    return missing[0];
}

static int pe_cascade_skip(int k, int econ_missing, uint32_t *pct) {
    uint32_t held;
    *pct = s_cascade[k].chance;
    switch (k) {
    case RC_ECON: return econ_missing == 0;
    case RC_SUPER_TOOL: return pe_held(0x36) > 0;                 /* Super Tool Kit */
    case RC_PERMIT: return pe_held(0x37) >= 2u;
    case RC_CHARM: return 1;                                      /* 0x19 is Panacea now */
    case RC_TOOL:
        if (pe_held(0x35) || pe_held(0x36)) return 1;             /* either kit: Tools are free */
        held = pe_held(0x16);
        if (held >= 5u) return 1;
        *pct = s_tool_chance[held];
        return 0;
    default: return 0;
    }
}

static uint16_t pe_pick_rare(uint32_t exp) {
    int missing[PE_ECON_COUNT], hit = RC_BOOSTER, k;
    const int econ = pe_econ_missing(missing);
    unsigned o = 0;
    uint32_t tier;
    s_cascade_path[0] = '\0';
    for (k = 0; k < RC_COUNT; ++k) {
        uint32_t pct, guarantee = 0, since = 0;
        int certain;
        if (pe_cascade_skip(k, econ, &pct)) {
            if (o < sizeof s_cascade_path - 12u)
                o += (unsigned)snprintf(s_cascade_path + o, sizeof s_cascade_path - o, "-%s", s_cascade[k].name);
            continue;
        }
        if (k == RC_ECON) { guarantee = (uint32_t)s_tune_econ_guarantee; since = s_st.econ_since; }
        if (k == RC_PERMIT) { guarantee = (uint32_t)s_tune_permit_guarantee; since = s_st.permit_since; }
        certain = guarantee && since + 1u >= guarantee;
        if (k == RC_BOOSTER || certain || pe_rand() % 100u < pct) {
            hit = k;
            if (o < sizeof s_cascade_path - 16u)
                snprintf(s_cascade_path + o, sizeof s_cascade_path - o, "+%s%s", s_cascade[k].name, certain ? "!" : "");
            break;
        }
    }
    if (econ) s_st.econ_since = hit == RC_ECON ? 0 : (uint8_t)(s_st.econ_since < 250 ? s_st.econ_since + 1 : 250);
    if (pe_held(0x37) < 2u)
        s_st.permit_since = hit == RC_PERMIT ? 0 : (uint8_t)(s_st.permit_since < 250 ? s_st.permit_since + 1 : 250);
    if (hit == RC_ECON) return (uint16_t)(PE_ECON_DROP | (uint32_t)pe_pick_econ(missing, econ));
    if (hit != RC_BOOSTER) return s_cascade[hit].item;
    tier = exp < 20u ? 0u : exp < 60u ? 1u : exp < 150u ? 2u : 3u;
    return (uint16_t)(s_booster_base[pe_rand() % 6u] + tier);
}

static uint32_t pe_rare_guarantee(void) {
    const uint32_t g = (uint32_t)s_tune_rare_guarantee * s_preset_pity[s_st.preset < 3 ? s_st.preset : 1] / 100u;
    return g ? g : 1u;
}

static uint32_t pe_pity_step(void) {
    uint32_t step = pe_loot_armor_has(PE_FX_LUCKY_FIND) ? 2u : 1u;
    if (s_st.charm_left) step *= 2u;
    return step;
}

static uint16_t pe_roll_rare(uint32_t exp) {
    const uint32_t dry = s_st.rare_dry, preset = s_st.preset < 3 ? s_st.preset : 1u;
    const uint32_t chance = (uint32_t)s_tune_rare_base * 10u * s_preset_pity_base[preset] / 100u +
                            (uint32_t)s_tune_rare_step * 10u * s_preset_pity_step[preset] / 100u * dry;
    uint32_t next;
    if (dry + 1u >= pe_rare_guarantee() || pe_rand() % 1000u < chance) {
        s_st.rare_dry = 0;
        return pe_pick_rare(exp);
    }
    next = dry + pe_pity_step();
    s_st.rare_dry = next > 0xFFFFu ? 0xFFFFu : (uint16_t)next;
    return 0;
}

/* ---- battle end ---- */
static int pe_on_rewards(void *ctx, void *user) {
    const PeHookRewards *r = (const PeHookRewards *)ctx;
    const uint32_t exp = (uint32_t)r->exp, per = s_tune_exp_per_junk > 0 ? (uint32_t)s_tune_exp_per_junk : 1u;
    const uint32_t cap = (uint32_t)(s_boss_battle ? s_tune_boss_cap : s_tune_battle_cap);
    uint32_t earned = (uint32_t)s_tune_floor + exp / per, hits = 0, vanilla = 0, total;
    int s;
    (void)user;
    if (!s_enabled) return 0;
    if (earned > cap) earned = cap;
    for (s = 0; s < PE_ENEMY_SLOTS; ++s) hits += s_st.hits[s];
    if (hits < 4) earned += earned * (uint32_t)s_tune_nohit_bonus * (4u - hits) / 400u;
    earned = earned * s_preset_junk[s_st.preset < 3 ? s_st.preset : 1] / 100u;
    if (pe_loot_armor_has(PE_FX_SCAVENGER)) earned += earned / 2u;

    s_found.junk_shown = 0;
    s_found.junk_list_seen = 0;
    if (r->list) {
        BattleRewardSlot keep[PE_REWARD_SLOTS];
        uint32_t kept = 0, shown, i;
        unsigned dry_before;
        uint16_t rolled, adds[2], carried;
        int a;
        for (i = 0; i < PE_REWARD_SLOTS; ++i) {
            const BattleRewardSlot e = r->list[i];
            if (e.id == 0) break;
            if (e.id == PE_ITEM_JUNK) ++vanilla;
            else keep[kept++] = e;
        }
        shown = earned + vanilla + s_st.steal_bank;
        if (s_st.charm_left) --s_st.charm_left;
        dry_before = s_st.rare_dry;
        carried = s_st.rare_carry;
        rolled = pe_roll_rare(exp);
        pe_log("loot", "exp=%u junk=%u hits=%u vanilla=%u steal=%u shown=%u boss=%d preset=%u scav=%d rare_dry=%u rolled=0x%04X carried=0x%04X charm=%u path=%s",
               (unsigned)exp, (unsigned)earned, (unsigned)hits, (unsigned)vanilla, (unsigned)s_st.steal_bank,
               (unsigned)shown, s_boss_battle, (unsigned)s_st.preset, pe_loot_armor_has(PE_FX_SCAVENGER), dry_before,
               rolled, carried, (unsigned)s_st.charm_left, rolled ? s_cascade_path : "-");
        adds[0] = s_st.rare_carry;
        adds[1] = rolled;
        s_st.rare_carry = 0;
        s_found.rare_shown[0] = s_found.rare_shown[1] = 0;
        s_found.rare_list_seen = 0;
        s_found.econ_shown = -1;
        s_found.econ_list_seen = 0;
        for (a = 0; a < 2; ++a) {
            uint16_t id = adds[a];
            const PeCatalogApi *cat = pe_catalog();
            if (!id) continue;
            /* A carried-over economy mod unlocked since: a fresh rare instead. */
            if ((id & PE_ECON_DROP) && (!cat || cat->armor_unlocked(s_econ_mods[(id & 0xFFu) % PE_ECON_COUNT].effect)))
                id = pe_pick_rare(exp);
            if (id & PE_ECON_DROP) {   /* carried by a Mod Permit entry: never next to a real one, never two */
                if (s_found.econ_shown >= 0 || adds[1 - a] == PE_ITEM_MOD_PERMIT || kept >= PE_REWARD_SLOTS) {
                    if (!s_st.rare_carry) s_st.rare_carry = id;
                    continue;
                }
                s_found.econ_shown = (int)(id & 0xFFu) % PE_ECON_COUNT;
                s_found.permits_before = pe_bag_count_of(PE_ITEM_MOD_PERMIT);
                id = PE_ITEM_MOD_PERMIT;
            }
            if (kept < PE_REWARD_SLOTS) {
                keep[kept].id = (s16)id;
                keep[kept++].extra = -1;
                s_found.rare_shown[a] = id;
                s_found.rare_before[a] = pe_bag_count_of(id);
                s_found.rare_is_econ[a] = id == PE_ITEM_MOD_PERMIT && s_found.econ_shown >= 0;
            } else {
                s_st.rare_carry = id;
            }
        }
        if (shown && kept < PE_REWARD_SLOTS) {   /* Junk last: Take all fills free slots in order */
            keep[kept].id = PE_ITEM_JUNK;
            keep[kept++].extra = -1;
            s_found.junk_shown = shown;
            s_found.bag_junk_before = pe_bag_count_of(PE_ITEM_JUNK);
        }
        for (i = 0; i < PE_REWARD_SLOTS; ++i) {
            if (i < kept) r->list[i] = keep[i];
            else { r->list[i].id = 0; r->list[i].extra = -1; }
        }
    }
    total = earned + vanilla + s_st.steal_bank;
    s_st.steal_bank = 0;
    if (!s_found.junk_shown) pe_add_junk(total);
    return 0;
}

/* ---- items-found rows: "Junk xN", "<mod> mod", gold names ---- */
static void pe_restore_text_color(void) {
    if (!s_color_saved) return;
    g_DrawPrimColor = s_saved_color;
    g_DrawColorShaded = s_saved_color_shaded;
    s_color_saved = 0;
}

static u8 *pe_ascii_label(u8 *out, const char *ascii) {
    unsigned o = 0;
    for (; *ascii && o < 30u; ++ascii, ++o) {
        const char c = *ascii;
        out[o] = (c >= 'A' && c <= 'Z') ? (u8)(0x10 + c - 'A') : (c >= 'a' && c <= 'z') ? (u8)(0x30 + c - 'a')
               : (c >= '0' && c <= '9') ? (u8)(c - '0') : 0x0Fu;
    }
    out[o] = 0xFF;
    return out;
}

static u8 *pe_junk_label(uint32_t n) {
    const u8 *name = pe_item_name(PE_ITEM_JUNK);
    char digits[12];
    unsigned o = 0, i;
    const char *d;
    if (!name) return NULL;
    for (i = 0; i < 16u && name[i] != 0xFF; ++i) s_junk_label[o++] = name[i];
    snprintf(digits, sizeof digits, "%u", (unsigned)n);
    s_junk_label[o++] = 0x0F;
    s_junk_label[o++] = (u8)(0x30 + ('x' - 'a'));
    for (d = digits; *d && o < 30u; ++d) s_junk_label[o++] = (u8)(*d - '0');
    s_junk_label[o] = 0xFF;
    return s_junk_label;
}

static int pe_on_slot_row(void *ctx, void *user) {
    u8 **text = (u8 **)ctx;
    int a;
    (void)user;
    pe_restore_text_color();
    if (!s_found.rare_shown[0] && !s_found.rare_shown[1] && !s_found.junk_shown) return 0;
    if (!pe_items_found_open() || !*text) return 0;
    if (s_found.junk_shown && *text == pe_item_name(PE_ITEM_JUNK)) {
        u8 *label = pe_junk_label(s_found.junk_shown);
        if (label) *text = label;
        return 0;
    }
    for (a = 0; a < 2; ++a) {
        if (!s_found.rare_shown[a] || pe_item_name(s_found.rare_shown[a]) != *text) continue;
        if (s_found.econ_shown >= 0 && s_found.rare_shown[a] == PE_ITEM_MOD_PERMIT)
            *text = pe_ascii_label(s_econ_label, s_econ_mods[s_found.econ_shown].name);
        s_saved_color = g_DrawPrimColor;
        s_saved_color_shaded = g_DrawColorShaded;
        g_DrawPrimColor = PE_RARE_COLOR;
        g_DrawColorShaded = (PE_RARE_COLOR >> 1) & 0x7F7F7Fu;
        s_color_saved = 1;
        return 0;
    }
    return 0;
}

/* ---- pickups and steals ---- */
static int pe_on_steal(void *ctx, void *user) { (void)ctx; (void)user; s_next_add_is_steal = 1; return 0; }

static int pe_on_item_add(void *ctx, void *user) {
    s32 *item = (s32 *)ctx;
    const int from_steal = s_next_add_is_steal;
    (void)user;
    s_next_add_is_steal = 0;
    if (from_steal && *item) {
        s_steal_item = (uint16_t)*item;
        s_steal_frame = s_frames;
    }
    if (from_steal && !*item && s_enabled && pe_game_loaded()) {   /* empty steal: a little Junk */
        const uint32_t bank = (uint32_t)s_st.steal_bank + (uint32_t)s_tune_empty_steal;
        s_st.steal_bank = bank > 0xFFFFu ? 0xFFFFu : (uint16_t)bank;
        return 0;
    }
    if (!s_enabled || !pe_game_loaded() || *item != PE_ITEM_JUNK) return 0;
    *item = 0;                                                   /* adds nothing, returns success */
    if (s_found.junk_shown && g_InvActiveListOverride) {
        pe_log("junk_take", "n=%u", (unsigned)s_found.junk_shown);
        pe_add_junk(s_found.junk_shown);
        s_found.junk_shown = 0;
        return 0;
    }
    if (from_steal) { if (s_st.steal_bank < 0xFFFFu) ++s_st.steal_bank; }
    else pe_add_junk(1);
    return 0;
}

/* ---- Wayne's hand-in ---- */
static void pe_restore_active_list(void) {
    if (!s_list_redirected) return;
    g_InvItemPtr = g_AyaSaveState.inventory_items;
    g_InvSlotLimit = s_saved_limit;
    s_list_redirected = 0;
}

static int pe_on_item_count(void *ctx, void *user) {
    int **args = (int **)ctx;
    int new_handin, limit, i;
    uint32_t n;
    unsigned wayne;
    (void)user;
    if (!s_enabled || !pe_game_loaded() || !args || !args[0] || *args[0] != PE_ITEM_JUNK) return 0;
    new_handin = !s_handin_active || (!s_handin_left && s_frames - s_last_count_frame > PE_HANDIN_GUARD_FRAMES);
    s_handin_active = 1;
    s_last_count_frame = s_frames;
    if (!new_handin) return 0;
    s_handin_left = 0;
    s_wayne_prev = (uint16_t)pe_wayne_count();
    s_wayne_dropped = 0;
    if (g_InvItemPtr != g_AyaSaveState.inventory_items) return 0;
    limit = g_InvSlotLimit;
    for (i = 0; i < limit && i < PE_BAG_SLOTS; ++i)
        if (g_AyaSaveState.inventory_items[i] == PE_ITEM_JUNK) return 0;   /* real Junk in the bag */
    if (s_st.junk == 0) return 0;
    wayne = pe_wayne_count();
    n = s_st.junk;
    if (s_handin_mode == 0) {
        const uint32_t needed = wayne < PE_WAYNE_CRAFT_COST ? PE_WAYNE_CRAFT_COST - wayne : 1u;
        if (n > needed) n = needed;
    }
    if (n > PE_HANDIN_MAX) n = PE_HANDIN_MAX;
    if (pe_free_bag_slot(limit) < 0) return 0;
    for (i = 0; i < (int)n; ++i) s_scratch[i] = PE_ITEM_JUNK;
    s_saved_limit = limit;
    g_InvItemPtr = s_scratch;
    g_InvSlotLimit = (s32)n;
    s_list_redirected = 1;
    s_handin_left = n;
    return 0;
}

static int pe_on_inv_command(void *ctx, void *user) {
    const PeHookInvCommand *c = (const PeHookInvCommand *)ctx;
    int slot;
    (void)user;
    if (c->command != PE_INV_CMD_REMOVE_ITEM || c->value != PE_ITEM_JUNK) return 0;
    pe_restore_active_list();
    if (!s_handin_left || !s_st.junk) return 0;
    slot = pe_free_bag_slot(s_saved_limit ? s_saved_limit : g_InvSlotLimit);
    if (slot < 0) return 0;
    g_AyaSaveState.inventory_items[slot] = PE_ITEM_JUNK;
    --s_st.junk;
    --s_handin_left;
    s_last_count_frame = s_frames;
    return 0;
}

/* ---- per-save data: the host's "pe_economy.loot" record (pe_save.h) holds s_st ---- */
static uint32_t pe_get32(const u8 *p) { uint32_t v; memcpy(&v, p, 4); return v; }

/* A save without Junk data (vanilla, or New Game): start at 0, the sweep converts; difficulty and
 * the switch stay. */
static void pe_save_reset(void *data) {
    const uint8_t preset = s_st.preset < 3 ? s_st.preset : 1;
    (void)data;
    memset(&s_st, 0, sizeof s_st);
    s_st.preset = preset;
    s_st.disabled = (uint8_t)!s_enabled;
}

/* Only the lasting fields carry over; what belongs to a battle starts fresh. */
static int pe_save_check(void *data) {
    (void)data;
    if (s_st.junk > 99999u) return 1;
    if (s_st.preset > 2) s_st.preset = 1;
    s_st.disabled &= 1u;
    s_st.steal_bank = 0;
    memset(s_st.hits, 0, sizeof s_st.hits);
    s_st.rng = 0;
    return 0;
}

/* Saves from before: the recomp build's "PELT" block, versions 1 to 4. */
static int pe_save_upgrade(void *data, const uint8_t *at, unsigned size, unsigned version) {
    uint32_t junk;
    (void)data;
    if (version != 0 || size < 32) return 1;
    pe_save_reset(NULL);
    if (pe_get32(at) != PE_TAIL_MAGIC || (junk = pe_get32(at + 8)) > 99999u) return 1;
    version = pe_get32(at + 4);
    if (version == 1u) {
        if ((junk ^ 0xA5A5A5A5u) == pe_get32(at + 12)) s_st.junk = junk;
    } else if (version == 2u) {
        const uint32_t packed = pe_get32(at + 12);
        if ((junk ^ packed ^ 0xA5A5A5A5u) == pe_get32(at + 16)) {
            s_st.junk = junk;
            s_st.rare_dry = (uint16_t)packed;
            s_st.rare_carry = (uint16_t)(packed >> 16);
        }
    } else if (version == 3u || version == 4u) {
        const uint32_t packed = pe_get32(at + 12), extra = pe_get32(at + 16);
        if ((junk ^ packed ^ extra ^ 0xA5A5A5A5u) == pe_get32(at + 20)) {
            const uint32_t since = pe_get32(at + 24);
            s_st.junk = junk;
            s_st.rare_dry = (uint16_t)packed;
            s_st.rare_carry = (uint16_t)(packed >> 16);
            s_st.charm_left = (uint16_t)extra;
            s_st.preset = (uint8_t)(extra >> 16) < 3 ? (uint8_t)(extra >> 16) : 1;
            s_st.disabled = (uint8_t)((extra >> 24) & 1u);
            if (version == 4u && (since ^ 0x5A5A5A5Au) == pe_get32(at + 28)) {
                s_st.econ_since = (uint8_t)since;
                s_st.permit_since = (uint8_t)(since >> 8);
            }
        }
    }
    return 0;
}

static const PeSaveRecord s_save_record = {
    "pe_economy.loot", 1, &s_st, sizeof s_st, pe_save_check, pe_save_upgrade, pe_save_reset,
};

/* After the host put the record back: the switch follows the save. */
static int pe_on_save_read(void *ctx, void *user) {
    (void)ctx;
    (void)user;
    s_enabled = !s_st.disabled;
    pe_mod_log(MOD_ID, "save loaded: Junk %u, preset %s, economy %s", (unsigned)s_st.junk,
               s_preset_names[s_st.preset], s_enabled ? "on" : "off");
    return 0;
}

/* ---- per frame ---- */
static void pe_sweep(void) {
    uint32_t moved = 0;
    int i;
    if (s_found.junk_shown) return;
    for (i = 0; i < PE_BAG_SLOTS + PE_STORAGE_SLOTS; ++i) {
        s16 *slot = i < PE_BAG_SLOTS ? &g_AyaSaveState.inventory_items[i] : &g_WayneStorageItems[i - PE_BAG_SLOTS];
        const uint32_t bundle = pe_bundle_value(*slot);
        if (*slot == PE_ITEM_JUNK) { *slot = 0; ++moved; continue; }
        if (bundle) {
            pe_log("bundle", "item=0x%02X junk=%u", (unsigned)*slot, (unsigned)bundle);
            *slot = 0;
            moved += bundle;
        }
    }
    if (moved) pe_add_junk(moved);
}

/* The stolen item's name in the steal message, in the textbox's red highlight (FB 04 .. FB 05). */
static void pe_highlight_steal_message(void) {
    enum { MSG_MAX = (int)sizeof g_InvItemMessageBuffer };
    u8 msg[MSG_MAX];
    const u8 *name;
    unsigned msg_len = 0, name_len = 0, at, o, i;
    if (!s_steal_item) return;
    if (s_frames - s_steal_frame > 30u) { s_steal_item = 0; return; }
    if (g_CurItemEffectData != g_InvItemMessageBuffer) return;
    while (msg_len < MSG_MAX && (msg[msg_len] = g_InvItemMessageBuffer[msg_len]) != 0xFF) ++msg_len;
    name = pe_item_name(s_steal_item);
    s_steal_item = 0;
    if (msg_len >= (unsigned)MSG_MAX || !name) return;
    while (name_len < (unsigned)MSG_MAX && name[name_len] != 0xFF) ++name_len;
    if (!name_len || name_len > msg_len || msg_len + 5u > (unsigned)MSG_MAX) return;
    for (at = 0; at + name_len <= msg_len; ++at) {
        if (memcmp(msg + at, name, name_len) != 0) continue;
        o = 0;
        for (i = 0; i < at; ++i) g_InvItemMessageBuffer[o++] = msg[i];
        g_InvItemMessageBuffer[o++] = 0xFB; g_InvItemMessageBuffer[o++] = 0x04;
        for (i = 0; i < name_len; ++i) g_InvItemMessageBuffer[o++] = name[i];
        g_InvItemMessageBuffer[o++] = 0xFB; g_InvItemMessageBuffer[o++] = 0x05;
        for (i = at + name_len; i < msg_len; ++i) g_InvItemMessageBuffer[o++] = msg[i];
        g_InvItemMessageBuffer[o] = 0xFF;
        return;
    }
}

static void pe_switched(int on) {
    char msg[96];
    pe_restore_active_list();
    s_handin_active = 0;
    s_st.steal_bank = 0;
    s_found.rare_shown[0] = s_found.rare_shown[1] = 0;
    s_found.junk_shown = 0;
    s_found.econ_shown = -1;
    pe_restore_text_color();
    s_in_battle = on && pe_game_loaded() && pe_in_battle();
    if (!pe_game_loaded()) return;
    if (on) snprintf(msg, sizeof msg, "Junk economy on: Junk in the bag and trunk joins the counter");
    else snprintf(msg, sizeof msg, "Junk economy off: %u Junk kept (Workshop > Take out Junk)", (unsigned)s_st.junk);
    pe_toast(msg);
}

/* Junk last on the items-found list. */
static void pe_junk_entry_last(void) {
    int n = g_InvActiveListCount > 10 ? 10 : g_InvActiveListCount, last = 0, junk = -1, i;
    if (g_InvActiveListOverride != g_InvActiveListItems) return;
    for (i = 0; i < n; ++i) {
        if (g_InvActiveListItems[i]) last = i;
        if (g_InvActiveListItems[i] == PE_ITEM_JUNK && junk < 0) junk = i;
    }
    if (junk < 0 || junk >= last) return;
    for (i = junk; i < last; ++i) g_InvActiveListItems[i] = g_InvActiveListItems[i + 1];
    g_InvActiveListItems[last] = PE_ITEM_JUNK;
}

/* With the bag full, the "Junk xN" line (and an economy mod, and key items) are taken where they stand. */
static void pe_junk_without_room(void) {
    const PeCatalogApi *cat = pe_catalog();
    const PeKeysApi *keys = (const PeKeysApi *)pe_service_get("keys", 1);
    int n = g_InvActiveListCount > 10 ? 10 : g_InvActiveListCount, i;
    if (g_InvActiveListOverride != g_InvActiveListItems || pe_free_bag_slot(pe_bag_limit()) >= 0) return;
    for (i = 0; i < n && s_found.econ_shown >= 0; ++i) {
        if (g_InvActiveListItems[i] != PE_ITEM_MOD_PERMIT) continue;
        g_InvActiveListItems[i] = 0;
        if (cat) cat->unlock_armor(s_econ_mods[s_found.econ_shown].effect);
        pe_log("econ_unlock", "mod=%s source=drop no_room=1", s_econ_mods[s_found.econ_shown].name);
        s_found.econ_shown = -1;
        s_found.econ_list_seen = 0;
    }
    for (i = 0; i < n && keys; ++i)
        if (g_InvActiveListItems[i] && keys->take((uint16_t)g_InvActiveListItems[i])) g_InvActiveListItems[i] = 0;
    for (i = 0; i < n; ++i) {
        uint32_t pay;
        if (g_InvActiveListItems[i] != PE_ITEM_JUNK) continue;
        g_InvActiveListItems[i] = 0;
        pay = s_found.junk_shown ? s_found.junk_shown : 1u;
        pe_log("junk_take", "n=%u no_room=1", (unsigned)pay);
        pe_add_junk(pay);
        s_found.junk_shown = 0;
        s_found.junk_list_seen = 0;
        return;
    }
}

static void pe_loot_tick(void) {

    int battle, i, s;
    ++s_frames;
    if (s_was_enabled != s_enabled) {
        if (s_was_enabled >= 0) pe_switched(s_enabled);
        s_was_enabled = s_enabled;
        s_st.disabled = (uint8_t)!s_enabled;
    }
    if (pe_game_loaded()) pe_highlight_steal_message();
    if (!s_enabled || !pe_game_loaded()) { s_in_battle = 0; return; }

    pe_restore_text_color();
    pe_junk_entry_last();
    pe_junk_without_room();
    if (s_found.junk_shown) {           /* the "Junk xN" entry, not taken yet */
        uint32_t extra = pe_bag_count_of(PE_ITEM_JUNK);
        int taken = 0;
        extra = extra > s_found.bag_junk_before ? extra - s_found.bag_junk_before : 0u;
        for (i = 0; i < PE_BAG_SLOTS && extra; ++i)
            if (g_AyaSaveState.inventory_items[i] == PE_ITEM_JUNK) { g_AyaSaveState.inventory_items[i] = 0; --extra; taken = 1; }
        if (taken) {
            pe_log("junk_take", "n=%u", (unsigned)s_found.junk_shown);
            pe_add_junk(s_found.junk_shown);
            s_found.junk_shown = 0;
            s_found.junk_list_seen = 0;
        } else if (g_InvActiveListOverride) {
            s_found.junk_list_seen = 1;
        } else if (s_found.junk_list_seen) {
            pe_log("junk_left", "n=%u paid=1", (unsigned)s_found.junk_shown);
            pe_add_junk(s_found.junk_shown);
            s_found.junk_shown = 0;
            s_found.junk_list_seen = 0;
        }
    }
    if (s_found.econ_shown >= 0) {      /* the "<mod> mod" entry, not taken yet */
        if (pe_bag_count_of(PE_ITEM_MOD_PERMIT) > s_found.permits_before) {
            const PeCatalogApi *cat = pe_catalog();
            for (i = 0; i < PE_BAG_SLOTS; ++i)
                if (g_AyaSaveState.inventory_items[i] == PE_ITEM_MOD_PERMIT) { g_AyaSaveState.inventory_items[i] = 0; break; }
            if (cat) cat->unlock_armor(s_econ_mods[s_found.econ_shown].effect);
            pe_log("econ_unlock", "mod=%s source=drop", s_econ_mods[s_found.econ_shown].name);
            s_found.econ_shown = -1;
        } else if (g_InvActiveListOverride) {
            s_found.econ_list_seen = 1;
        } else if (s_found.econ_list_seen) {
            pe_log("econ_left", "mod=%s", s_econ_mods[s_found.econ_shown].name);
            s_found.econ_shown = -1;
        }
    }
    if (s_found.rare_shown[0] || s_found.rare_shown[1]) {
        if (g_InvActiveListOverride) s_found.rare_list_seen = 1;
        else if (s_found.rare_list_seen) {
            int a;
            for (a = 0; a < 2; ++a) {
                if (!s_found.rare_shown[a] || s_found.rare_is_econ[a]) continue;
                pe_log(pe_bag_count_of(s_found.rare_shown[a]) > s_found.rare_before[a] ? "rare_take" : "rare_left",
                       "item=0x%02X", (unsigned)s_found.rare_shown[a]);
            }
            s_found.rare_shown[0] = s_found.rare_shown[1] = 0;
        }
    }

    battle = pe_in_battle();
    if (battle && !s_in_battle) {       /* a battle starts */
        s_found.rare_shown[0] = s_found.rare_shown[1] = 0;
        s_found.junk_shown = 0;
        s_found.econ_shown = -1;
        s_boss_battle = 0;
        s_st.steal_bank = 0;
        memset(s_st.hits, 0, sizeof s_st.hits);
    } else if (!battle && s_in_battle) {
        s_st.steal_bank = 0;            /* fled: stolen Junk is forfeited (a win already paid) */
    }
    s_in_battle = battle;
    if (battle) {
        for (s = 0; s < PE_ENEMY_SLOTS; ++s) {
            const EnemyCombatant *core = &g_BattleEnemySlots[s].combatant;
            if (!g_BattleEnemySlots[s].active) continue;
            if (core->rewardFactorB > s_st.hits[s]) s_st.hits[s] = core->rewardFactorB; /* +0x9C: hits on Aya */
            if (core->statusFlags2 & 0x40000u) s_boss_battle = 1;
        }
        return;
    }

    if (s_wayne_waiting && pe_wayne_count() < PE_WAYNE_CRAFT_COST) s_wayne_waiting = 0;
    if (s_list_redirected && s_frames - s_last_count_frame > 2u) pe_restore_active_list();
    s_free_frames = pe_free_field() ? s_free_frames + 1u : 0u;
    if (s_handin_left && s_free_frames > 60u) s_handin_left = 0;
    if (s_handin_active || s_wayne_check) {   /* a craft takes 300 off his count */
        const uint16_t w = (uint16_t)pe_wayne_count();
        if (w < s_wayne_prev) s_wayne_dropped = 1;
        s_wayne_prev = w;
    }
    if (s_handin_active && !s_handin_left && s_frames - s_last_count_frame > PE_HANDIN_GUARD_FRAMES) {
        s_handin_active = 0;
        s_wayne_check = 1;
    }
    if (s_wayne_check && !s_handin_active && s_free_frames > 60u) {
        s_wayne_check = 0;
        if (!s_wayne_dropped && s_wayne_prev >= PE_WAYNE_CRAFT_COST) s_wayne_waiting = 1;   /* before Day 4 */
    }
    if (!s_handin_active && pe_free_field()) pe_sweep();
}

/* ---- menu ---- */
static int pe_get_junk(void) { return (int)s_st.junk; }
static int pe_get_rare_within(void) {
    const int left = (int)pe_rare_guarantee() - (int)s_st.rare_dry, step = (int)pe_pity_step();
    return left < 1 ? 1 : (left + step - 1) / step;
}
static void pe_take_out_junk(void) {
    char msg[80];
    uint32_t moved = 0;
    int i;
    if (!pe_game_loaded()) { pe_toast("Load a game first"); return; }
    if (s_enabled) { pe_toast("Turn Junk economy off first (it would go back)"); return; }
    for (i = 0; i < pe_bag_limit() && s_st.junk; ++i)
        if (!g_AyaSaveState.inventory_items[i]) { g_AyaSaveState.inventory_items[i] = PE_ITEM_JUNK; --s_st.junk; ++moved; }
    for (i = 0; i < PE_STORAGE_SLOTS && s_st.junk; ++i)
        if (!g_WayneStorageItems[i]) { g_WayneStorageItems[i] = PE_ITEM_JUNK; --s_st.junk; ++moved; }
    snprintf(msg, sizeof msg, "%u Junk to the bag and trunk, %u left in the counter", (unsigned)moved, (unsigned)s_st.junk);
    pe_toast(msg);
}
static int pe_get_preset(void) { return s_st.preset < 3 ? s_st.preset : 1; }
static int pe_set_preset(int v) { if (v < 0 || v > 2) return 0; s_st.preset = (uint8_t)v; return 1; }
static void pe_give_junk_toast(uint32_t n) {
    char msg[48];
    pe_add_junk(n);
    snprintf(msg, sizeof msg, "+%u Junk (total %u)", (unsigned)n, (unsigned)s_st.junk);
    pe_toast(msg);
}
static void pe_give_100(void) { pe_give_junk_toast(100); }
static void pe_give_1000(void) { pe_give_junk_toast(1000); }
static int pe_set_junk_debug(int v) { if (v < 0 || v > 99999) return 0; s_st.junk = (uint32_t)v; return 1; }

static void pe_junk_text(char *out, unsigned size) {
    if (!pe_game_loaded()) snprintf(out, size, "load a game");
    else snprintf(out, size, "%u", (unsigned)s_st.junk);
}
static void pe_rare_text(char *out, unsigned size) {
    if (!pe_game_loaded()) snprintf(out, size, "-");
    else snprintf(out, size, "%d battle%s", pe_get_rare_within(), pe_get_rare_within() == 1 ? "" : "s");
}

#define PE_TUNING_PAGE PE_PAGE_DEBUG_LOOT

void pe_loot_register(const char *mod_id) {
    pe_menu_register_info(PE_PAGE_ECONOMY, "Junk", "Earned in battles and chests; spent in the Workshop.", pe_junk_text);
    pe_menu_register_live_choice(PE_PAGE_ECONOMY, "Difficulty", "Junk earned, rare luck and prices. Change any time.",
                                 s_preset_names, 3, pe_get_preset, pe_set_preset);
    pe_menu_register_info(PE_PAGE_ECONOMY, "Rare drop within", "Won battles until a rare drop is certain.", pe_rare_text);
    pe_menu_register_choice(PE_PAGE_ECONOMY, "Wayne hand-in", "How much Junk Wayne's Discard junk gives him.",
                            &s_handin_mode, s_handin_names, 2);
    pe_menu_register_toggle(PE_PAGE_ECONOMY, "Junk economy", "Off: Junk goes back in the bag, as in the original game.",
                            &s_enabled, pe_loot_tick);
    pe_menu_register_action(PE_PAGE_ECONOMY, "Take out Junk", "With the economy off: move counter Junk to the bag and trunk.",
                            pe_take_out_junk);
    pe_menu_register_int(PE_TUNING_PAGE, "Junk floor", "Junk for every won battle.", &s_tune_floor, 0, 10, 1);
    pe_menu_register_int(PE_TUNING_PAGE, "EXP per Junk", "Battle EXP for each extra Junk.", &s_tune_exp_per_junk, 1, 100, 1);
    pe_menu_register_int(PE_TUNING_PAGE, "Battle Junk cap", "Most Junk from one battle.", &s_tune_battle_cap, 1, 200, 1);
    pe_menu_register_int(PE_TUNING_PAGE, "Boss Junk cap", "Most Junk from a boss fight.", &s_tune_boss_cap, 1, 200, 1);
    pe_menu_register_int(PE_TUNING_PAGE, "No-hit bonus %", "Extra Junk for a no-hit battle.", &s_tune_nohit_bonus, 0, 100, 5);
    pe_menu_register_int(PE_TUNING_PAGE, "Rare base %", "Rare drop chance after a drop.", &s_tune_rare_base, 0, 100, 1);
    pe_menu_register_int(PE_TUNING_PAGE, "Rare step %", "Added for each battle without one.", &s_tune_rare_step, 0, 50, 1);
    pe_menu_register_int(PE_TUNING_PAGE, "Rare guarantee", "Battles until a rare drop is certain.", &s_tune_rare_guarantee, 1, 200, 1);
    pe_menu_register_int(PE_TUNING_PAGE, "Economy mod guarantee", "Rares until an economy mod is certain.", &s_tune_econ_guarantee, 1, 50, 1);
    pe_menu_register_int(PE_TUNING_PAGE, "Mod Permit guarantee", "Rares until a Mod Permit is certain.", &s_tune_permit_guarantee, 1, 50, 1);
    pe_menu_register_int(PE_TUNING_PAGE, "Empty steal Junk", "Junk for stealing from an empty enemy.", &s_tune_empty_steal, 0, 20, 1);
    pe_menu_register_action(PE_TUNING_PAGE, "Give 100 Junk", "Debug: add 100 Junk.", pe_give_100);
    pe_menu_register_action(PE_TUNING_PAGE, "Give 1000 Junk", "Debug: add 1000 Junk.", pe_give_1000);
    pe_menu_register_live_int(PE_TUNING_PAGE, "Set Junk", "Debug: change the Junk counter.", 0, 99999, 10, pe_get_junk, pe_set_junk_debug);

    pe_service_publish("junk", 1, &s_junk_api, mod_id);
    pe_hook_add(PE_HOOK_BATTLE_REWARDS, pe_on_rewards, NULL, PE_PRIORITY_NORMAL, mod_id);
    pe_hook_add(PE_HOOK_SLOT_ROW, pe_on_slot_row, NULL, PE_PRIORITY_NORMAL, mod_id);
    pe_hook_add(PE_HOOK_STEAL, pe_on_steal, NULL, PE_PRIORITY_NORMAL, mod_id);
    pe_hook_add(PE_HOOK_ITEM_ADD, pe_on_item_add, NULL, PE_PRIORITY_LATE, mod_id);  /* after loggers see the real id */
    pe_hook_add(PE_HOOK_ITEM_COUNT, pe_on_item_count, NULL, PE_PRIORITY_NORMAL, mod_id);
    pe_hook_add(PE_HOOK_INV_COMMAND, pe_on_inv_command, NULL, PE_PRIORITY_NORMAL, mod_id);
    pe_save_register(&s_save_record);
    pe_hook_add(PE_HOOK_SAVE_READ, pe_on_save_read, NULL, PE_PRIORITY_NORMAL, mod_id);
}

#if PE_NATIVE_STATE_CPU_COMPONENTS
#include "pe_checkpoint_wire.h"
#include "pe_checkpoint_economy.h"
int pe_loot_checkpoint_walk(const unsigned char *in,unsigned char *out,int apply){if((!in&&!out)||(s_list_redirected))return 0;if(apply&&(!in||!pe_loot_checkpoint_walk(in,0,0)))return 0;PeCheckpointWire w={in,out,0,apply,1};
PE_CK_U(w,s_st.junk,0,99999,0);
PE_CK_U(w,s_st.steal_bank,0,65535,0);
for(unsigned i=0;i<7;i++){PE_CK_U(w,s_st.hits[i],0,65535,0);}
PE_CK_U(w,s_st.rare_dry,0,65535,0);
PE_CK_U(w,s_st.rare_carry,0,65535,0);
PE_CK_U(w,s_st.rng,0,UINT32_MAX,0);
PE_CK_U(w,s_st.charm_left,0,65535,0);
PE_CK_U(w,s_st.preset,0,2,0);
PE_CK_U(w,s_st.disabled,0,1,0);
PE_CK_U(w,s_st.econ_since,0,255,0);
PE_CK_U(w,s_st.permit_since,0,255,0);
PE_CK_U(w,s_enabled,0,1,1);
PE_CK_U(w,s_handin_mode,0,1,1);
PE_CK_S(w,s_tune_floor,INT32_MIN,INT32_MAX,1);
PE_CK_S(w,s_tune_exp_per_junk,INT32_MIN,INT32_MAX,1);
PE_CK_S(w,s_tune_battle_cap,INT32_MIN,INT32_MAX,1);
PE_CK_S(w,s_tune_boss_cap,INT32_MIN,INT32_MAX,1);
PE_CK_S(w,s_tune_nohit_bonus,INT32_MIN,INT32_MAX,1);
PE_CK_S(w,s_tune_rare_base,INT32_MIN,INT32_MAX,1);
PE_CK_S(w,s_tune_rare_step,INT32_MIN,INT32_MAX,1);
PE_CK_S(w,s_tune_rare_guarantee,INT32_MIN,INT32_MAX,1);
PE_CK_S(w,s_tune_econ_guarantee,INT32_MIN,INT32_MAX,1);
PE_CK_S(w,s_tune_permit_guarantee,INT32_MIN,INT32_MAX,1);
PE_CK_S(w,s_tune_empty_steal,INT32_MIN,INT32_MAX,1);
PE_CK_U(w,s_in_battle,0,1,0);
PE_CK_U(w,s_boss_battle,0,1,0);
for(unsigned i=0;i<2;i++){PE_CK_U(w,s_found.rare_shown[i],0,65535,0);}
for(unsigned i=0;i<2;i++){PE_CK_U(w,s_found.rare_before[i],0,UINT32_MAX,0);}
for(unsigned i=0;i<2;i++){PE_CK_U(w,s_found.rare_is_econ[i],0,1,0);}
PE_CK_U(w,s_found.rare_list_seen,0,1,0);
PE_CK_U(w,s_found.junk_list_seen,0,1,0);
PE_CK_U(w,s_found.econ_list_seen,0,1,0);
PE_CK_U(w,s_found.junk_shown,0,UINT32_MAX,0);
PE_CK_U(w,s_found.bag_junk_before,0,UINT32_MAX,0);
PE_CK_U(w,s_found.permits_before,0,UINT32_MAX,0);
PE_CK_S(w,s_found.econ_shown,-1,PE_ECON_COUNT-1,0);
PE_CK_U(w,s_frames,0,UINT32_MAX,0);
PE_CK_U(w,s_last_count_frame,0,UINT32_MAX,0);
PE_CK_U(w,s_free_frames,0,UINT32_MAX,0);
PE_CK_U(w,s_handin_active,0,1,0);
PE_CK_U(w,s_list_redirected,0,0,0);
PE_CK_U(w,s_wayne_waiting,0,1,0);
PE_CK_U(w,s_wayne_dropped,0,1,0);
PE_CK_U(w,s_next_add_is_steal,0,1,0);
PE_CK_U(w,s_handin_left,0,UINT32_MAX,0);
PE_CK_S(w,s_saved_limit,INT32_MIN,INT32_MAX,0);
PE_CK_U(w,s_wayne_prev,0,65535,0);
PE_CK_U(w,s_color_saved,0,1,0);
PE_CK_U(w,s_saved_color,0,UINT32_MAX,0);
PE_CK_U(w,s_saved_color_shaded,0,UINT32_MAX,0);
PE_CK_U(w,s_steal_item,0,65535,0);
PE_CK_U(w,s_steal_frame,0,UINT32_MAX,0);
PE_CK_U(w,g_DrawPrimColor,0,UINT32_MAX,0);
PE_CK_U(w,g_DrawColorShaded,0,UINT32_MAX,0);

PE_CK_S(w,s_was_enabled,-1,1,0);PE_CK_S(w,s_wayne_check,0,1,0);
if(apply&&w.ok){memset(s_scratch,0,sizeof s_scratch);memset(s_junk_label,0,sizeof s_junk_label);memset(s_econ_label,0,sizeof s_econ_label);s_cascade_path[0]=0;}return w.ok&&w.at==260u;}
#endif
