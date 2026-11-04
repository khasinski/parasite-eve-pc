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

SaveRuntimeState g_SaveRuntimeState;
GameStateBlock g_GameStateBlock;
Pe1GameState g_GameState;
InventoryItemWorkspace g_InventoryItemWorkspace;
static int s_scavenger;
static ItemDataRecord s_armor;
ItemDataRecord *pe_equipped(int armor) { return armor ? &s_armor : NULL; }
int pe_rec_has_effect(const ItemDataRecord *rec, int effect) { (void)rec; return s_scavenger && effect == PE_FX_SCAVENGER; }
const void *pe_service_get(const char *name, int version) {
    (void)version;
    if (!strcmp(name, "wallet")) return &s_wallet_api;
    if (!strcmp(name, "junk")) return &s_junk_api;
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
int pe_free_field(void) { return 0; }
int pe_game_loaded(void) { return 0; }
int pe_in_battle(void) { return 0; }
unsigned pe_wayne_count(void) { return 0; }
int pe_hook_add(int id, PeHookFn fn, void *user, int priority, const char *mod_id) { (void)id; (void)fn; (void)user; (void)priority; (void)mod_id; return 0; }
int pe_service_publish(const char *name, int version, const void *api, const char *mod_id) { (void)name; (void)version; (void)api; (void)mod_id; return 0; }
void pe_mod_log(const char *mod_id, const char *fmt, ...) { (void)mod_id; (void)fmt; }
void pe_toast(const char *text) { (void)text; }
int pe_save_register(const PeSaveRecord *record) { (void)record; return 0; }
int pe_menu_register_toggle(const char *sub, const char *label, const char *description, int *flag, PeMenuPerFrame per_frame) { (void)sub; (void)label; (void)description; (void)flag; (void)per_frame; return 0; }
int pe_menu_register_action(const char *sub, const char *label, const char *description, PeMenuAction action) { (void)sub; (void)label; (void)description; (void)action; return 0; }
int pe_menu_register_choice(const char *sub, const char *label, const char *description, int *index, const char *const *names, unsigned count) { (void)sub; (void)label; (void)description; (void)index; (void)names; (void)count; return 0; }
int pe_menu_register_int(const char *sub, const char *label, const char *description, int *value, int minimum, int maximum, int step) { (void)sub; (void)label; (void)description; (void)value; (void)minimum; (void)maximum; (void)step; return 0; }
int pe_menu_register_live_int(const char *sub, const char *label, const char *description, int minimum, int maximum, int step, PeMenuGet get, PeMenuSet set) { (void)sub; (void)label; (void)description; (void)minimum; (void)maximum; (void)step; (void)get; (void)set; return 0; }
int pe_menu_register_live_choice(const char *sub, const char *label, const char *description, const char *const *names, unsigned count, PeMenuGet get, PeMenuSet set) { (void)sub; (void)label; (void)description; (void)names; (void)count; (void)get; (void)set; return 0; }
int pe_menu_register_tick(PeMenuPerFrame fn) { (void)fn; return 0; }
int pe_menu_register_info(const char *sub, const char *label, const char *description, PeMenuText text) { (void)sub; (void)label; (void)description; (void)text; return 0; }

#define PINNED 0xA31B4CD7B0A66B68ull   /* the economy mod before D10 step 1, and after it: identical */

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
    puts("Loot parity: 400 scripted battles match the pinned record, lists and Junk; wallet copy in / out, battle_junk and rewards-off passed");
    return 0;
}
