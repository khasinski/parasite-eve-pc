/* Exercise the production reward and balance logic without rendering a menu. */
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
ItemDataRecord *pe_equipped(int armor) { (void)armor;return NULL; }
int pe_rec_has_effect(const ItemDataRecord *rec,int effect) { (void)rec;(void)effect;return 0; }
const void *pe_service_get(const char *name,int version) { (void)version; return !strcmp(name,"wallet")?(const void *)&s_wallet_api:!strcmp(name,"junk")?(const void *)&s_junk_api:NULL; }
/* The battle end as the host runs it: wallet early, rewards, wallet late. */
static void pe_on_rewards(PeHookRewards *r,void *u) { pe_on_rewards_early(r,u); pe_rw_on_rewards(r,u); pe_on_rewards_late(r,u); }
void pe_log(const char *tag,const char *fmt,...) { (void)tag;(void)fmt; }
void pe_toast(const char *text) { (void)text; }
int main(void) {
    s_st.junk=100;
    pe_loot_give_junk(UINT32_MAX);
    assert(pe_loot_junk()==99999);
    assert(!pe_loot_spend_junk(100000) && pe_loot_junk()==99999);
    assert(pe_loot_spend_junk(99999) && pe_loot_junk()==0);
    BattleRewardSlot list[10]={{.id=0x14},{.id=6},{0}};
    PeHookRewards reward={.exp=80,.list=list};
    s_enabled=0;
    BattleRewardSlot before[10];memcpy(before,list,sizeof list);
    pe_on_rewards(&reward,NULL);
    assert(!memcmp(before,list,sizeof list) && s_st.junk==0);
    s_enabled=1;s_st.preset=1;s_st.steal_bank=2;
    s_rw_tune_rare_base=s_rw_tune_rare_step=0;s_rw_tune_rare_guarantee=200;
    pe_on_rewards(&reward,NULL);
    assert(list[0].id==6 && list[1].id==0x14 && list[2].id==0);
    assert(s_found.junk_shown==16 && s_st.steal_bank==0 && s_st.junk==0);
    /* A full reward list still pays Junk immediately and never overwrites it. */
    for(int i=0;i<10;++i)list[i]=(BattleRewardSlot){.id=6};
    pe_on_rewards(&reward,NULL);
    assert(s_st.junk==13 && !s_found.junk_shown);
    for(int i=0;i<10;++i)assert(list[i].id==6);
    s_st.preset=2;s_st.junk=123;s_st.rare_dry=17;
    pe_save_reset(NULL);
    assert(s_st.junk==0 && s_st.preset==2 && s_st.rare_dry==0);
    puts("Loot: saturated balance, disabled parity, reward compaction, full list and new game passed");
    return 0;
}
