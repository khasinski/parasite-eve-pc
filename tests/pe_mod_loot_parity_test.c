/* Battle-rewards parity fixture (bridge findings D10 §8.3): 400 scripted battles through the
 * production battle-end handler, varying EXP, hits, bosses, Scavenger, Difficulty, steals, the
 * lucky charm and how full the reward list is. Everything they produce (the "pe_economy.loot"
 * record's bytes, each reward list, the Junk shown) goes into one hash, pinned below.
 *
 * Pinned on November 3, 2026 against the economy mod before the wallet / rewards split (D10 step
 * 1), when one handler did it all. Since step 2 the battle runs in three steps across two mods,
 * the Junk wallet (pe_economy, early and late) and the battle rewards (pe_rewards, between), and
 * must still reproduce this exact hash: any difference means a player's Junk, pity or drops
 * would change. Also checks the "wallet" service's copy in and out. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../mods/pe_economy/pe_loot.c"
#include "../mods/pe_rewards/pe_rewards.c"
#include "pe_econ_test_host.inc"

SaveRuntimeState g_SaveRuntimeState;
GameStateBlock g_GameStateBlock;
Pe1GameState g_GameState;
InventoryItemWorkspace g_InventoryItemWorkspace;
static int s_scavenger;
static ItemDataRecord s_armor;
ItemDataRecord *pe_equipped(int armor) { return armor ? &s_armor : NULL; }
int pe_rec_has_effect(const ItemDataRecord *rec, int effect) { (void)rec; return s_scavenger && effect == PE_FX_SCAVENGER; }
static int s_test_free_field, s_test_loaded, s_test_toasts;
static const PeCatalogApi *s_test_catalog;   /* the parity run has none */
const void *pe_service_get(const char *name, int version) {
    (void)version;
    if (!strcmp(name, "wallet")) return &s_wallet_api;
    if (!strcmp(name, "junk")) return &s_junk_api;
    if (!strcmp(name, "catalog")) return s_test_catalog;
    return NULL;                                  /* no Mod Catalog: economy mods can't drop */
}

/* The battle end as the host dispatches it: wallet early, rewards, wallet late. */
static void battle_end(PeHookRewards *reward) {
    pe_on_rewards_early(reward, NULL);
    pe_rw_on_rewards(reward, NULL);
    pe_on_rewards_late(reward, NULL);
}
void pe_log(const char *tag, const char *fmt, ...) { (void)tag; (void)fmt; }

/* The whole modules are included, so their menus and hooks need these to link, on Windows above
 * all. The battles below run the handlers directly, with no host behind them. */
BattleEnemySlot g_BattleEnemySlots[7];
u8 *g_CurItemEffectData;
unsigned int g_DrawColorShaded, g_DrawPrimColor;
int g_InvActiveListCount, g_InvBonusSlotCount, g_InvSlotLimit;
s16 g_InvActiveListItems[10];
s16 *g_InvActiveListOverride, *g_InvItemPtr;
u8 g_InvItemMessageBuffer[32];
void *Text_LookupTable8(unsigned int index) { (void)index; return NULL; }
int pe_free_field(void) { return s_test_free_field; }
int pe_game_loaded(void) { return s_test_loaded; }
int pe_in_battle(void) { return 0; }
unsigned pe_wayne_count(void) { return 0; }
int pe_hook_add(int id, PeHookFn fn, void *user, int priority, const char *mod_id) { (void)id; (void)fn; (void)user; (void)priority; (void)mod_id; return 0; }
int pe_service_publish(const char *name, int version, const void *api, const char *mod_id) { (void)name; (void)version; (void)api; (void)mod_id; return 0; }
void pe_mod_log(const char *mod_id, const char *fmt, ...) { (void)mod_id; (void)fmt; }
void pe_toast(const char *text) { (void)text; ++s_test_toasts; }
int pe_save_register(const PeSaveRecord *record) { (void)record; return 0; }
int pe_menu_register_toggle(const char *sub, const char *label, const char *description, int *flag, PeMenuPerFrame per_frame) { (void)sub; (void)label; (void)description; (void)flag; (void)per_frame; return 0; }
int pe_menu_register_action(const char *sub, const char *label, const char *description, PeMenuAction action) { (void)sub; (void)label; (void)description; (void)action; return 0; }
int pe_menu_register_choice(const char *sub, const char *label, const char *description, int *index, const char *const *names, unsigned count) { (void)sub; (void)label; (void)description; (void)index; (void)names; (void)count; return 0; }
int pe_menu_register_int(const char *sub, const char *label, const char *description, int *value, int minimum, int maximum, int step) { (void)sub; (void)label; (void)description; (void)value; (void)minimum; (void)maximum; (void)step; return 0; }
int pe_menu_register_live_int(const char *sub, const char *label, const char *description, int minimum, int maximum, int step, PeMenuGet get, PeMenuSet set) { (void)sub; (void)label; (void)description; (void)minimum; (void)maximum; (void)step; (void)get; (void)set; return 0; }
int pe_menu_register_live_choice(const char *sub, const char *label, const char *description, const char *const *names, unsigned count, PeMenuGet get, PeMenuSet set) { (void)sub; (void)label; (void)description; (void)names; (void)count; (void)get; (void)set; return 0; }
int pe_menu_register_tick(PeMenuPerFrame fn) { (void)fn; return 0; }
int pe_menu_register_info(const char *sub, const char *label, const char *description, PeMenuText text) { (void)sub; (void)label; (void)description; (void)text; return 0; }

/* The opt-in path through the same real handlers. Runs after the hash is checked. */
static int s_unlocked[32];
static int test_armor_unlocked(int effect) { return s_unlocked[effect]; }
static int test_unlock_armor(int effect) { const int was = s_unlocked[effect]; s_unlocked[effect] = 1; return !was; }
static const PeCatalogApi s_catalog = {test_armor_unlocked, test_unlock_armor};

static PeEconConfig builtin(int i) {
    PeEconConfig c;
    assert(pe_econ_builtin(i, 100, &c) == PE_ECON_OK);
    return c;
}
static void clear_list(BattleRewardSlot *list) {
    memset(list, 0, sizeof(BattleRewardSlot) * 10);
    for (int i = 0; i < 10; ++i) list[i].extra = -1;
}
static int on_list(const BattleRewardSlot *list, int id) {
    for (int i = 0; i < 10; ++i) if (list[i].id == id) return 1;
    return 0;
}

static void test_policy_path(void) {
    BattleRewardSlot list[10];
    PeHookRewards reward = {.exp = 500, .list = list};
    PeRewardState before, after;
    PeEconConfig c;
    const PeEconState *state = pe_econ_store_state();
    enum { ENHANCED, VANILLA, LIGHT, STEADY, TREASURE };

    pe_save_reset(NULL);
    s_found.junk_shown = 0;
    s_st.preset = 2;                      /* Grind: Enhanced's price is 125 */
    s_st.rng = 0xC0FFEEu; s_st.rare_dry = 7; s_st.rare_carry = 0x31; s_st.econ_since = 2; s_st.permit_since = 3;
    s_test_free_field = 1;
    s_test_loaded = 1;
    s_rw_boss_battle = 0;
    s_scavenger = 0;
    assert(pe_loot_price(100) == 125);
    assert(!s_wallet_api.policy());

    /* Steady: fixed income shown with native Junk; Enhanced's progress doesn't move. */
    c = builtin(STEADY);
    c.price_pct = 75;
    assert(pe_econ_store_apply(&c) == PE_ECON_OK);
    assert(s_wallet_api.policy() == state && state->progress.rng_payment && state->progress.next_sequence == 1);
    {   /* Apply seeds from the clock; a fixed seed makes every award below repeatable. */
        PeEconProgress fixed = state->progress;
        pe_econ_seed(&fixed, 0x5EED0001u);
        pe_econ_store_commit(&fixed);
    }
    assert(pe_loot_price(100) == 75);     /* the path's own price */
    memset(&before, 0, sizeof before); memset(&after, 0, sizeof after);
    s_wallet_api.get_rewards(&before);
    clear_list(list);
    list[0].id = PE_ITEM_JUNK; list[1].id = 6;
    s_test_notices = 0;
    battle_end(&reward);
    assert(s_found.junk_shown == 3u + 1u && list[0].id == 6 && list[1].id == PE_ITEM_JUNK && !list[2].id);
    assert(s_test_notices == 1);
    s_wallet_api.get_rewards(&after);
    assert(!memcmp(&before, &after, sizeof before));
    pe_add_junk(s_found.junk_shown); s_found.junk_shown = 0;

    /* Vanilla drops: nothing beyond what the battle dropped, and no bonus for empty steals. */
    c = builtin(VANILLA);
    assert(pe_econ_store_apply(&c) == PE_ECON_OK);
    clear_list(list);
    list[0].id = 6;
    s_st.junk = 0;
    battle_end(&reward);
    assert(!s_found.junk_shown && list[0].id == 6 && !list[1].id && !s_st.junk);
    {
        s32 item = 0;
        pe_on_steal(NULL, NULL);
        pe_on_item_add(&item, NULL);
        assert(!s_st.steal_bank);
    }

    /* Bundles stay as items; native Junk still goes to the counter. */
    memset(g_AyaSaveState.inventory_items, 0, sizeof g_AyaSaveState.inventory_items);
    memset(g_WayneStorageItems, 0, sizeof g_WayneStorageItems);
    g_AyaSaveState.inventory_items[0] = PE_ITEM_SUPER_JUNK;
    g_AyaSaveState.inventory_items[1] = PE_ITEM_DUPER_JUNK;
    g_AyaSaveState.inventory_items[2] = PE_ITEM_JUNK;
    g_WayneStorageItems[0] = PE_ITEM_DUPER_JUNK;
    pe_sweep();
    assert(s_st.junk == 1 && g_AyaSaveState.inventory_items[0] == PE_ITEM_SUPER_JUNK);
    assert(g_AyaSaveState.inventory_items[1] == PE_ITEM_DUPER_JUNK && !g_AyaSaveState.inventory_items[2]);

    /* Light redeems at its values, all or nothing. */
    c = builtin(LIGHT);
    assert(pe_econ_store_apply(&c) == PE_ECON_OK);
    s_st.junk = 99999u - 15u;             /* room for a Super (10), not a Duper (20) */
    pe_sweep();
    assert(s_st.junk == 99999u - 5u && !g_AyaSaveState.inventory_items[0]);
    assert(g_AyaSaveState.inventory_items[1] == PE_ITEM_DUPER_JUNK && g_WayneStorageItems[0] == PE_ITEM_DUPER_JUNK);
    s_st.junk = 0;
    pe_sweep();
    assert(s_st.junk == 40u && !g_AyaSaveState.inventory_items[1] && !g_WayneStorageItems[0]);

    /* Items routing: native Junk stays what it is, everywhere. */
    c = builtin(VANILLA);
    c.route = PE_ECON_ROUTE_ITEMS;
    assert(pe_econ_store_apply(&c) == PE_ECON_OK);
    s_st.junk = 0;
    clear_list(list);
    list[0].id = PE_ITEM_JUNK;
    battle_end(&reward);
    assert(list[0].id == PE_ITEM_JUNK && !s_found.junk_shown && !s_st.junk);
    {
        s32 item = PE_ITEM_JUNK;
        pe_on_item_add(&item, NULL);
        assert(item == PE_ITEM_JUNK && !s_st.junk);
    }
    g_AyaSaveState.inventory_items[3] = PE_ITEM_JUNK;
    pe_sweep();
    assert(g_AyaSaveState.inventory_items[3] == PE_ITEM_JUNK && !s_st.junk);
    g_AyaSaveState.inventory_items[3] = 0;

    /* A full wallet: the payment waits whole, with a message; room delivers it. */
    c = builtin(STEADY);
    assert(pe_econ_store_apply(&c) == PE_ECON_OK);
    s_st.junk = 99998u;
    clear_list(list);
    s_test_toasts = 0;
    battle_end(&reward);
    assert(state->progress.pending[PE_ECON_SLOT_CURRENCY].amount == 3 && s_test_toasts == 1);
    assert(s_st.junk == 99998u && !s_found.junk_shown);
    s_st.junk = 0;
    clear_list(list);
    battle_end(&reward);
    assert(s_found.junk_shown == 3 && !state->progress.pending[PE_ECON_SLOT_CURRENCY].kind);
    s_found.junk_shown = 0;

    /* Treasure: the award stays pending until its list closes, then it's finished either way. */
    c = builtin(TREASURE);
    c.item_chance = 10000;
    assert(pe_econ_store_apply(&c) == PE_ECON_OK);
    s_test_catalog = NULL;
    clear_list(list);
    battle_end(&reward);
    assert(state->progress.pending[PE_ECON_SLOT_ITEM].kind == PE_ECON_PENDING_ITEM);
    assert(on_list(list, state->progress.pending[PE_ECON_SLOT_ITEM].content));
    pe_rw_policy_settle();
    assert(!state->progress.pending[PE_ECON_SLOT_ITEM].kind);

    /* A full list: nothing is lost, and it comes next time, with no new roll. */
    clear_list(list);
    for (int i = 0; i < 10; ++i) list[i].id = 6;
    battle_end(&reward);
    {
        const uint32_t seq = state->progress.pending[PE_ECON_SLOT_ITEM].sequence;
        assert(seq);
        pe_rw_policy_settle();            /* it wasn't shown: settling leaves it */
        assert(state->progress.pending[PE_ECON_SLOT_ITEM].sequence == seq);
        clear_list(list);
        battle_end(&reward);
        assert(state->progress.pending[PE_ECON_SLOT_ITEM].sequence == seq);
        assert(on_list(list, state->progress.pending[PE_ECON_SLOT_ITEM].content));
        pe_rw_policy_settle();
    }

    /* Rule 1: a waiting economy mod unlocked another way counts as delivered: no reroll, no Junk. */
    s_test_catalog = &s_catalog;
    memset(s_unlocked, 0, sizeof s_unlocked);
    {
        PeEconProgress g = state->progress;
        PeEconConfig quiet = builtin(VANILLA);
        g.pending[PE_ECON_SLOT_ITEM] = (PeEconPending){PE_ECON_PENDING_EFFECT, 0, 0, 2, 0, 1, g.next_sequence++};
        assert(pe_econ_store_apply(&quiet) == PE_ECON_OK);
        pe_econ_store_commit(&g);
        s_unlocked[s_rw_econ_mods[2].effect] = 1;
        s_st.junk = 0;
        s_test_toasts = 0;
        clear_list(list);
        battle_end(&reward);
        assert(!state->progress.pending[PE_ECON_SLOT_ITEM].kind && s_test_toasts == 1);
        assert(!list[0].id && !s_found.junk_shown && !s_st.junk);
    }

    /* Still locked: shown as its mod on a Mod Permit row, unless a real one is on the list. */
    {
        PeEconProgress g = state->progress;
        memset(s_unlocked, 0, sizeof s_unlocked);
        g.pending[PE_ECON_SLOT_ITEM] = (PeEconPending){PE_ECON_PENDING_EFFECT, 0, 0, 2, 0, 1, g.next_sequence++};
        pe_econ_store_commit(&g);
        clear_list(list);
        list[0].id = PE_ITEM_MOD_PERMIT;
        battle_end(&reward);
        assert(s_rw_found.econ_shown < 0 && state->progress.pending[PE_ECON_SLOT_ITEM].kind == PE_ECON_PENDING_EFFECT);
        clear_list(list);
        battle_end(&reward);
        assert(s_rw_found.econ_shown == 2 && list[0].id == PE_ITEM_MOD_PERMIT);
        pe_rw_policy_settle();            /* Rule 2: left means declined, and it stays locked */
        assert(!state->progress.pending[PE_ECON_SLOT_ITEM].kind && !s_unlocked[s_rw_econ_mods[2].effect]);
        s_rw_found.econ_shown = -1;
        s_test_catalog = NULL;
    }

    /* Back on Enhanced: its progress resumes where it stopped, and its price with Difficulty. */
    c = builtin(ENHANCED);
    assert(pe_econ_store_apply(&c) == PE_ECON_OK);
    assert(!s_wallet_api.policy() && pe_loot_price(100) == 125);
    s_wallet_api.get_rewards(&after);
    assert(!memcmp(&before, &after, sizeof before));
    s_test_free_field = 0;
    s_test_loaded = 0;
}

#define PINNED 0xA31B4CD7B0A66B68ull  /* the economy mod before D10 step 1, and after it: identical */

static uint64_t s_hash = 1469598103934665603ull;
static void mix(const void *p, size_t n) {
    const unsigned char *b = p;
    while (n--) { s_hash ^= *b++; s_hash *= 1099511628211ull; }
}

static uint32_t s_script = 12345u;
static uint32_t script(uint32_t n) { s_script = s_script * 1103515245u + 12345u; return (s_script >> 16) % n; }

int main(void) {
    int battle, i;
    s_enabled = 1;
    pe_save_reset(NULL);
    s_st.rng = 0xC0FFEEu;

    /* The wallet service: a copy out and back in changes nothing; battle_junk adds this battle. */
    {
        uint8_t before[sizeof s_st];
        PeRewardState r;
        s_st.rare_dry = 7; s_st.rare_carry = 0x31; s_st.charm_left = 3; s_st.econ_since = 2; s_st.permit_since = 5;
        s_st.hits[3] = 9;
        memcpy(before, &s_st, sizeof s_st);
        s_wallet_api.get_rewards(&r);
        assert(r.rare_dry == 7 && r.rare_carry == 0x31 && r.charm_left == 3 && r.econ_since == 2 && r.permit_since == 5 && r.hits[3] == 9);
        s_wallet_api.put_rewards(&r);
        assert(!memcmp(before, &s_st, sizeof s_st));
        r.rare_dry = 1;
        s_wallet_api.put_rewards(&r);
        assert(s_st.rare_dry == 1 && s_st.junk == before[0] + (before[1] << 8));   /* only its fields */
        assert(s_wallet_api.struct_size == sizeof(PeWalletApi) && s_wallet_api.preset() == 1);
        pe_save_reset(NULL);
        s_st.rng = 0xC0FFEEu;
        {
            BattleRewardSlot list[10] = {{0}};
            PeHookRewards reward = {.exp = 0, .list = list};
            const int base = (int)s_rw_tune_floor;
            s_rw_tune_rare_base = 0; s_rw_tune_rare_step = 0; s_rw_tune_rare_guarantee = 200;
            s_wallet_api.battle_junk(40);
            battle_end(&reward);
            assert(s_found.junk_shown == (uint32_t)base + 40u);                          /* shown with the rest */
            s_found.junk_shown = 0;
            memset(list, 0, sizeof list);                    /* the last list's Junk row isn't vanilla Junk */
            battle_end(&reward);
            assert(s_found.junk_shown == (uint32_t)base);                                /* this battle only */
        }
        s_rw_tune_rare_base = 4; s_rw_tune_rare_step = 1; s_rw_tune_rare_guarantee = 25;
        pe_save_reset(NULL);
        s_st.rng = 0xC0FFEEu;
        s_found.junk_shown = 0;
    }

    for (battle = 0; battle < 400; ++battle) {
        BattleRewardSlot list[10];
        PeHookRewards reward;
        const int filled = (int)script(11);                  /* 0..10 rows already on the list */
        memset(list, 0, sizeof list);
        for (i = 0; i < filled; ++i) list[i].id = (s16)(script(4) == 0 ? PE_ITEM_JUNK : 1 + script(0x30)), list[i].extra = -1;
        reward.exp = (int)script(900);
        reward.list = list;
        s_st.preset = (uint8_t)script(3);
        s_rw_boss_battle = script(12) == 0;
        s_scavenger = script(5) == 0;
        for (i = 0; i < PE_ENEMY_SLOTS; ++i) s_st.hits[i] = (uint16_t)(script(3) == 0 ? script(4) : 0);
        s_st.steal_bank = (uint16_t)(script(6) == 0 ? script(20) : 0);
        if (script(40) == 0) s_st.charm_left = (uint16_t)(5 + script(10));
        battle_end(&reward);
        mix(&s_st, sizeof s_st);
        mix(list, sizeof list);
        mix(&s_found.junk_shown, sizeof s_found.junk_shown);
        mix(s_rw_found.rare_shown, sizeof s_rw_found.rare_shown);
        mix(&s_rw_found.econ_shown, sizeof s_rw_found.econ_shown);
        if (s_found.junk_shown) { pe_add_junk(s_found.junk_shown); s_found.junk_shown = 0; }   /* left behind: paid */
        mix(&s_st.junk, sizeof s_st.junk);
    }
    /* Rewards off (D10 §8.2): the wallet alone runs the battle end. Rewards' fields stay as they
     * were, byte for byte; only vanilla Junk and the steal bank are paid, with no rares. */
    {
        PeRewardState before, after;
        BattleRewardSlot list[10] = {{.id = PE_ITEM_JUNK}, {.id = 6}, {.id = PE_ITEM_JUNK}};
        PeHookRewards reward = {.exp = 500, .list = list};
        const uint32_t junk = s_st.junk;
        const uint64_t h = s_hash;                           /* this block stays out of the hash */
        s_st.steal_bank = 3;
        memset(&before, 0, sizeof before);                   /* padding before rng: compare fields only */
        memset(&after, 0, sizeof after);
        s_wallet_api.get_rewards(&before);
        pe_on_rewards_early(&reward, NULL);
        pe_on_rewards_late(&reward, NULL);
        s_wallet_api.get_rewards(&after);
        assert(!memcmp(&before, &after, sizeof before));
        assert(list[0].id == 6 && list[1].id == PE_ITEM_JUNK && list[2].id == 0);
        assert(s_found.junk_shown == 2u + 3u && s_st.junk == junk && s_st.steal_bank == 0);
        s_hash = h;
    }
    printf("parity hash 0x%016llX (pinned 0x%016llX)\n", (unsigned long long)s_hash, (unsigned long long)PINNED);
    assert(s_hash == PINNED);
    test_policy_path();
    puts("Loot parity: 400 scripted battles match the pinned record, lists and Junk; wallet copy in / out, battle_junk, rewards-off and the opt-in path passed");
    return 0;
}
