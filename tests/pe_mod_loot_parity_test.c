/* Battle-rewards parity fixture (bridge findings D10 §8.3): 400 scripted battles through the
 * production battle-end handler, varying EXP, hits, bosses, Scavenger, Difficulty, steals, the
 * lucky charm and how full the reward list is. Everything they produce (the "pe_economy.loot"
 * record's bytes, each reward list, the Junk shown) goes into one hash, pinned below.
 *
 * Pinned on November 3, 2026 against the economy mod before the wallet / rewards split (D10 step
 * 1). Step 2 (Wallet + Rewards as two mods) must reproduce this exact hash: any difference means a
 * player's Junk, pity or drops would change. Also checks the "wallet" service's copy in and out. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../mods/pe_economy/pe_loot.c"

SaveRuntimeState g_SaveRuntimeState;
GameStateBlock g_GameStateBlock;
Pe1GameState g_GameState;
InventoryItemWorkspace g_InventoryItemWorkspace;
static int s_scavenger;
static ItemDataRecord s_armor;
ItemDataRecord *pe_equipped(int armor) { return armor ? &s_armor : NULL; }
int pe_rec_has_effect(const ItemDataRecord *rec, int effect) { (void)rec; return s_scavenger && effect == PE_FX_SCAVENGER; }
const void *pe_service_get(const char *name, int version) { (void)name; (void)version; return NULL; }
void pe_log(const char *tag, const char *fmt, ...) { (void)tag; (void)fmt; }

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
            const int base = (int)s_tune_floor;
            s_tune_rare_base = 0; s_tune_rare_step = 0; s_tune_rare_guarantee = 200;
            s_wallet_api.battle_junk(40);
            pe_on_rewards(&reward, NULL);
            assert(s_found.junk_shown == (uint32_t)base + 40u);                          /* shown with the rest */
            s_found.junk_shown = 0;
            memset(list, 0, sizeof list);                    /* the last list's Junk row isn't vanilla Junk */
            pe_on_rewards(&reward, NULL);
            assert(s_found.junk_shown == (uint32_t)base);                                /* this battle only */
        }
        s_tune_rare_base = 4; s_tune_rare_step = 1; s_tune_rare_guarantee = 25;
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
        s_boss_battle = script(12) == 0;
        s_scavenger = script(5) == 0;
        for (i = 0; i < PE_ENEMY_SLOTS; ++i) s_st.hits[i] = (uint16_t)(script(3) == 0 ? script(4) : 0);
        s_st.steal_bank = (uint16_t)(script(6) == 0 ? script(20) : 0);
        if (script(40) == 0) s_st.charm_left = (uint16_t)(5 + script(10));
        pe_on_rewards(&reward, NULL);
        mix(&s_st, sizeof s_st);
        mix(list, sizeof list);
        mix(&s_found.junk_shown, sizeof s_found.junk_shown);
        mix(s_found.rare_shown, sizeof s_found.rare_shown);
        mix(&s_found.econ_shown, sizeof s_found.econ_shown);
        if (s_found.junk_shown) { pe_add_junk(s_found.junk_shown); s_found.junk_shown = 0; }   /* left behind: paid */
        mix(&s_st.junk, sizeof s_st.junk);
    }
    printf("parity hash 0x%016llX (pinned 0x%016llX)\n", (unsigned long long)s_hash, (unsigned long long)PINNED);
    assert(s_hash == PINNED);
    puts("Loot parity: 400 scripted battles match the pinned record, lists and Junk; wallet copy in / out and battle_junk passed");
    return 0;
}
