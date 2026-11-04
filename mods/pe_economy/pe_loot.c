/*
 * The Junk wallet: the Junk counter and everything that routes Junk into it. Ported from the
 * psxrecomp build
 * (src/mods/pe_loot.c; bridge findings pe-loot-overhaul-design-2026-10-11.md,
 * pe-loot-cascade-design-2026-10-14.md, pe-loot-overhaul-checklist.md).
 *
 * Junk never takes a bag slot: it lives in one counter.
 *   - Battle end (PE_HOOK_BATTLE_REWARDS, Aya_SetTotalExp entry, once per won battle), in three
 *     steps (bridge findings D10): here, early, vanilla Junk leaves the reward list; the battle
 *     rewards mod (pe_rewards, if running) adds its Junk through wallet.battle_junk and appends
 *     its rares; here, late, that Junk, vanilla Junk and Junk stolen this battle show on the
 *     items-found screen as one Junk entry whose name reads "Junk xN" (PE_HOOK_SLOT_ROW); taken,
 *     left behind or with no room it is paid all the same (it needs no slot).
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
 * Other mods: publishes the "junk" service (pe_economy.h) and "wallet" (the battle rewards' fields
 * in this record, for pe_rewards: this mod owns the record whole and keeps them as loaded while
 * the rewards are off; bridge findings D10); uses "keys" (key item pocket) when present.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "pe_econ_store.h"
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
static int s_tune_empty_steal = 2;

static const char *const s_preset_names[] = { "Relaxed", "Normal", "Grind" };
static const uint16_t s_preset_price[3] = { 75, 100, 125 };
static const char *const s_handin_names[] = { "Next gun", "All Junk" };

static int s_was_enabled = -1, s_wayne_check; /* checkpoint-owned tick latches */
static int s_in_battle;
static uint32_t s_battle_extra;   /* Junk added through wallet.battle_junk for this battle */
static uint32_t s_battle_vanilla; /* vanilla Junk taken off this battle's list */
static struct {
    uint32_t junk_shown;        /* this battle's Junk, shown as "Junk xN" */
    int junk_list_seen;
    uint32_t bag_junk_before;
} s_found;
static uint32_t s_frames, s_last_count_frame, s_free_frames;
static int s_handin_active, s_list_redirected, s_wayne_waiting, s_wayne_dropped, s_next_add_is_steal;
static uint32_t s_handin_left;
static s16 s_scratch[PE_HANDIN_MAX];
static s32 s_saved_limit;
static uint16_t s_wayne_prev;
static u8 s_junk_label[32];
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
/* The opt-in path with native Junk kept as items. Enhanced is never this. */
static int pe_junk_stays_item(void) {
    const PeEconConfig *policy = pe_econ_store_active();
    return policy && policy->route == PE_ECON_ROUTE_ITEMS;
}

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
    if (pe_econ_store_active()) pct = pe_econ_store_active()->price_pct;
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

/* ---- battle end: vanilla Junk off the list (early); the rewards mod adds its Junk and rares
 * (pe_rewards, normal); every bit of this battle's Junk as one "Junk xN" entry, last (late) ---- */
static int pe_on_rewards_early(void *ctx, void *user) {
    const PeHookRewards *r = (const PeHookRewards *)ctx;
    uint32_t kept = 0, i;
    (void)user;
    s_battle_vanilla = 0;
    s_found.junk_shown = 0;
    s_found.junk_list_seen = 0;
    if (!s_enabled || !r->list || pe_junk_stays_item()) return 0;
    for (i = 0; i < PE_REWARD_SLOTS; ++i) {
        const BattleRewardSlot e = r->list[i];
        if (e.id == 0) break;
        if (e.id == PE_ITEM_JUNK) ++s_battle_vanilla;
        else r->list[kept++] = e;
    }
    for (i = kept; i < PE_REWARD_SLOTS; ++i) { r->list[i].id = 0; r->list[i].extra = -1; }
    return 0;
}

static int pe_on_rewards_late(void *ctx, void *user) {
    const PeHookRewards *r = (const PeHookRewards *)ctx;
    const uint32_t total = s_battle_extra + s_battle_vanilla + s_st.steal_bank;
    (void)user;
    s_battle_extra = 0;
    s_battle_vanilla = 0;
    if (!s_enabled) return 0;
    if (r->list && total) {
        uint32_t kept = 0;
        while (kept < PE_REWARD_SLOTS && r->list[kept].id != 0) ++kept;
        if (kept < PE_REWARD_SLOTS) {   /* Junk last: Take all fills free slots in order */
            r->list[kept].id = PE_ITEM_JUNK;
            r->list[kept].extra = -1;
            s_found.junk_shown = total;
            s_found.bag_junk_before = pe_bag_count_of(PE_ITEM_JUNK);
        }
    }
    s_st.steal_bank = 0;
    if (!s_found.junk_shown) pe_add_junk(total);
    return 0;
}

/* ---- items-found rows: "Junk xN", "<mod> mod", gold names ---- */
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
    (void)user;
    if (!s_found.junk_shown || !pe_items_found_open() || !*text) return 0;
    if (*text == pe_item_name(PE_ITEM_JUNK)) {
        u8 *label = pe_junk_label(s_found.junk_shown);
        if (label) *text = label;
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
        const PeEconConfig *policy = pe_econ_store_active();
        const uint32_t amount = !policy ? (uint32_t)s_tune_empty_steal
                                : (policy->flags & PE_ECON_F_EMPTY_STEAL) ? policy->steal_junk : 0u;
        const uint32_t bank = (uint32_t)s_st.steal_bank + amount;
        s_st.steal_bank = bank > 0xFFFFu ? 0xFFFFu : (uint16_t)bank;
        return 0;
    }
    if (!s_enabled || !pe_game_loaded() || *item != PE_ITEM_JUNK || pe_junk_stays_item()) return 0;
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

/* ---- the "wallet" service: the rewards' fields, copied in and out ---- */
static void pe_wallet_get_rewards(PeRewardState *out) {
    memcpy(out->hits, s_st.hits, sizeof out->hits);
    out->rare_dry = s_st.rare_dry;
    out->rare_carry = s_st.rare_carry;
    out->rng = s_st.rng;
    out->charm_left = s_st.charm_left;
    out->econ_since = s_st.econ_since;
    out->permit_since = s_st.permit_since;
}
static void pe_wallet_put_rewards(const PeRewardState *in) {
    memcpy(s_st.hits, in->hits, sizeof s_st.hits);
    s_st.rare_dry = in->rare_dry;
    s_st.rare_carry = in->rare_carry;
    s_st.rng = in->rng;
    s_st.charm_left = in->charm_left;
    s_st.econ_since = in->econ_since;
    s_st.permit_since = in->permit_since;
}
static int pe_wallet_preset(void) { return s_st.preset < 3 ? s_st.preset : 1; }
static void pe_wallet_battle_junk(uint32_t n) { s_battle_extra = n >= 99999u - s_battle_extra ? 99999u : s_battle_extra + n; }
static const PeEconState *pe_wallet_policy(void) { return pe_econ_store_active() ? pe_econ_store_state() : NULL; }
static uint32_t pe_wallet_room(void) {
    const uint64_t taken = (uint64_t)s_st.junk + s_battle_extra + s_battle_vanilla + s_st.steal_bank;
    return taken >= 99999u ? 0u : 99999u - (uint32_t)taken;
}
static const PeWalletApi s_wallet_api = {
    sizeof(PeWalletApi), pe_wallet_get_rewards, pe_wallet_put_rewards, pe_wallet_preset, pe_wallet_battle_junk,
    pe_wallet_policy, pe_econ_store_commit, pe_wallet_room,
};

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
/* The opt-in path: routing and redemption follow its settings, and a slot is only emptied when
 * the wallet can take all of it, stacked copies included. */
static void pe_sweep_policy(const PeEconConfig *policy) {
    const PeStacksApi *stacks = (const PeStacksApi *)pe_service_get("stacks", 1);
    int i;
    for (i = 0; i < PE_BAG_SLOTS + PE_STORAGE_SLOTS; ++i) {
        const int list = i < PE_BAG_SLOTS ? 0 : 1, at = list ? i - PE_BAG_SLOTS : i;
        s16 *slot = list ? &g_WayneStorageItems[at] : &g_AyaSaveState.inventory_items[at];
        const int item = *slot;
        uint32_t value = 0, copies, total;
        if (item == PE_ITEM_JUNK && policy->route == PE_ECON_ROUTE_COUNTER) value = 1;
        else if (item == PE_ITEM_SUPER_JUNK && (policy->flags & PE_ECON_F_SUPER_REDEEM)) value = policy->super_value;
        else if (item == PE_ITEM_DUPER_JUNK && (policy->flags & PE_ECON_F_DUPER_REDEEM)) value = policy->duper_value;
        if (!value) continue;
        copies = stacks ? (uint32_t)stacks->count(list, at) : 1u;
        if (!copies) copies = 1;
        total = copies * value;
        if (total > 99999u - s_st.junk) continue;   /* stays until there's room */
        if (stacks) { uint32_t n; for (n = 0; n < copies; ++n) stacks->take(list, at); }
        *slot = 0;
        if (value > 1u) pe_log("bundle", "item=0x%02X junk=%u", (unsigned)item, (unsigned)total);
        pe_add_junk(total);
    }
}

static void pe_sweep(void) {
    uint32_t moved = 0;
    int i;
    if (s_found.junk_shown) return;
    if (pe_econ_store_active()) { pe_sweep_policy(pe_econ_store_active()); return; }
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
    s_found.junk_shown = 0;
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

/* With the bag full, the "Junk xN" line (and key items) are taken where they stand; an economy
 * mod is the rewards mod's. */
static void pe_junk_without_room(void) {
    const PeKeysApi *keys = (const PeKeysApi *)pe_service_get("keys", 1);
    int n = g_InvActiveListCount > 10 ? 10 : g_InvActiveListCount, i;
    if (g_InvActiveListOverride != g_InvActiveListItems || pe_free_bag_slot(pe_bag_limit()) >= 0) return;
    for (i = 0; i < n && keys; ++i)
        if (g_InvActiveListItems[i] && keys->take((uint16_t)g_InvActiveListItems[i])) g_InvActiveListItems[i] = 0;
    if (pe_junk_stays_item()) return;
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
    int battle, i;
    ++s_frames;
    if (s_was_enabled != s_enabled) {
        if (s_was_enabled >= 0) pe_switched(s_enabled);
        s_was_enabled = s_enabled;
        s_st.disabled = (uint8_t)!s_enabled;
    }
    if (pe_game_loaded()) pe_highlight_steal_message();
    if (!s_enabled || !pe_game_loaded()) { s_in_battle = 0; return; }

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
    battle = pe_in_battle();
    if (battle && !s_in_battle) {       /* a battle starts (the rewards mod clears its hits) */
        s_found.junk_shown = 0;
        s_st.steal_bank = 0;
    } else if (!battle && s_in_battle) {
        s_st.steal_bank = 0;            /* fled: stolen Junk is forfeited (a win already paid) */
    }
    s_in_battle = battle;
    if (battle) return;

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

#define PE_TUNING_PAGE PE_PAGE_DEBUG_LOOT

void pe_loot_register(const char *mod_id) {
    pe_menu_register_info(PE_PAGE_ECONOMY, "Junk", "Earned in battles and chests; spent in the Workshop.", pe_junk_text);
    pe_menu_register_live_choice(PE_PAGE_ECONOMY, "Difficulty", "Junk earned, rare luck and prices. Change any time.",
                                 s_preset_names, 3, pe_get_preset, pe_set_preset);
    pe_menu_register_choice(PE_PAGE_ECONOMY, "Wayne hand-in", "How much Junk Wayne's Discard junk gives him.",
                            &s_handin_mode, s_handin_names, 2);
    pe_menu_register_toggle(PE_PAGE_ECONOMY, "Junk economy", "Off: Junk goes back in the bag, as in the original game.",
                            &s_enabled, pe_loot_tick);
    pe_menu_register_action(PE_PAGE_ECONOMY, "Take out Junk", "With the economy off: move counter Junk to the bag and trunk.",
                            pe_take_out_junk);
    pe_menu_register_int(PE_TUNING_PAGE, "Empty steal Junk", "Junk for stealing from an empty enemy.", &s_tune_empty_steal, 0, 20, 1);
    pe_menu_register_action(PE_TUNING_PAGE, "Give 100 Junk", "Debug: add 100 Junk.", pe_give_100);
    pe_menu_register_action(PE_TUNING_PAGE, "Give 1000 Junk", "Debug: add 1000 Junk.", pe_give_1000);
    pe_menu_register_live_int(PE_TUNING_PAGE, "Set Junk", "Debug: change the Junk counter.", 0, 99999, 10, pe_get_junk, pe_set_junk_debug);

    pe_service_publish("junk", 1, &s_junk_api, mod_id);
    pe_service_publish("wallet", 1, &s_wallet_api, mod_id);
    pe_hook_add(PE_HOOK_BATTLE_REWARDS, pe_on_rewards_early, NULL, PE_PRIORITY_EARLY, mod_id);
    pe_hook_add(PE_HOOK_BATTLE_REWARDS, pe_on_rewards_late, NULL, PE_PRIORITY_LATE, mod_id);
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
