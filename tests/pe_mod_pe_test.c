#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "pe_game.h"
#include "pe_mod_host.h"
#include "pe_mod_menu.h"
#include "pe_atb_items.h"
#include "pe1/inventory.h"
#include "pe1/pe_cost.h"

SaveRuntimeState g_SaveRuntimeState;
Combatant *g_ActiveActor;
int g_pe_half_enabled,g_pe_half_allow[5],g_pe_panacea;
uint32_t g_pe_quick_mask;
InventoryItemWorkspace g_InventoryItemWorkspace;
s16 *g_InvItemPtr;
s32 g_InvSlotLimit;
static ParasiteSpellEntry spells[20];
void *Aya_GetParasiteSpellUnlockTable(void){return spells;}
int BattleCmd_GetRemainingAmmo(int *out){if(out)*out=120;return 120;}
static ItemDataRecord base_armor;
ItemDataRecord *Item_LookupBaseData(unsigned int index){return index==8?&base_armor:NULL;}
static Combatant aya;
static BattleEntity player;
static PeHookFn hooks[PE_HOOK_COUNT];
int pe_hook_dispatch(int id,void *ctx){return hooks[id]?hooks[id](ctx,NULL):0;}
static PeMenuPerFrame tick;
static int *rules,*detox,*scan;
static unsigned toasts;
static int saved_rules=-1;
int pe_game_loaded(void){return 1;}
int pe_in_battle(void){return 1;}
Combatant *pe_aya_core(void){return &aya;}
BattleEntity *pe_player(void){return &player;}
void pe_log(const char *t,const char *f,...){(void)t;(void)f;}
void pe_toast(const char *t){(void)t;++toasts;}
int pe_hook_add(int id,PeHookFn fn,void *u,int p,const char *m){(void)u;(void)p;(void)m;assert(!hooks[id]);hooks[id]=fn;return 0;}
int pe_menu_register_toggle(const char *p,const char *l,const char *d,int *v,PeMenuPerFrame f){(void)p;(void)l;(void)d;(void)v;(void)f;return 0;}
int pe_menu_register_int(const char *p,const char *l,const char *d,int *v,int lo,int hi,int step){
    (void)p;(void)d;(void)lo;(void)hi;(void)step;if(!strcmp(l,"Detox PE cost"))detox=v;if(!strcmp(l,"Scan PE cost"))scan=v;return 0;
}
int pe_menu_register_choice(const char *p,const char *l,const char *d,int *v,const char *const *n,unsigned c){
    (void)p;(void)d;(void)n;(void)c;if(!strcmp(l,"Rules")){rules=v;if(saved_rules>=0)*v=saved_rules;}return 0;
}
int pe_menu_register_tick(PeMenuPerFrame fn){tick=fn;return 0;}
void pe_pe_balance_register(const char *);
static void vanilla(void){
    *rules=0;tick();assert(!g_pe_half_enabled && !g_pe_panacea && !g_pe_quick_mask);
    for(unsigned power=0;power<20;++power){
        memset(&aya,0,sizeof aya);aya.curHP=17;aya.maxHP=999;aya.exp_or_acc=1000*65536;
        g_ActiveActor=&aya;Combatant before=aya;PeHookSpell spell={power,&player};
        hooks[PE_HOOK_SPELL](&spell,NULL);assert(!memcmp(&aya,&before,sizeof aya));
        if(hooks[PE_HOOK_POWER_COST]){int cost=123;PeHookPowerCost c={(int)power,&cost};hooks[PE_HOOK_POWER_COST](&c,NULL);assert(cost==123);}
    }
    assert(!toasts);puts("PE Vanilla: every power preserves HP, PE, status and original cost");
}
static void costs(void){
    assert(hooks[PE_HOOK_POWER_COST]);
    BattleAttributes attributes={0};
    for(unsigned light=0;light<2;++light)for(int cost=0;cost<=300;cost+=10)for(unsigned power=3;power<=7;power+=4){
        *rules=1;tick();*(power==3?detox:scan)=cost;tick();
        memset(&aya,0,sizeof aya);aya.attributes=&attributes;attributes.effectFlags=light?0x200:0;
        aya.exp_or_acc=1000*65536;g_ActiveActor=&aya;
        PeHookSpell spell={power,&player};hooks[PE_HOOK_SPELL](&spell,NULL);
        int native=(power==3?100:50)*65536;
        if(light)native=native*2/3;
        int expected=cost*65536;if(light)expected=expected*2/3;
        assert(aya.exp_or_acc-native==1000*65536-expected);
        int menu_cost=power==3?100:50;PeHookPowerCost query={(int)power,&menu_cost};
        hooks[PE_HOOK_POWER_COST](&query,NULL);assert(menu_cost==cost);
        /* Exercise the cost calculation used by the native menu, including its armor discount. */
        s16 bag[1]={0x100};g_InvItemPtr=bag;g_InvSlotLimit=1;
        g_InventoryRuntime.tracked[2]=0;
        ItemDataRecord *armor=&g_InventoryRuntime.equipment[0];
        armor->tailCount=light?1:0;armor->tailData[0]=0x0E;
        spells[power].cost=power==3?100:50;
        assert(Inv_ParasitePowerCost((int)power,0)==(light?cost*2/3:cost));
    }
    puts("PE costs: 124 cases match native charge rounding and menu affordability");
}
static void menu_cost_inputs(void){
    *rules=0;tick();
    spells[3].cost=101;
    s16 bag[1]={0x100};g_InvItemPtr=bag;g_InvSlotLimit=1;g_InventoryRuntime.tracked[2]=0;
    ItemDataRecord *armor=&g_InventoryRuntime.equipment[0];
    memset(armor,0,sizeof *armor);
    assert(Inv_ParasitePowerCost(3,0)==101);
    armor->tailCount=1;armor->tailData[0]=0x0E;
    assert(Inv_ParasitePowerCost(3,0)==67);

    memset(&base_armor,0,sizeof base_armor);
    base_armor.tailCount=1;base_armor.tailData[0]=0x0E;bag[0]=9;
    assert(Inv_ParasitePowerCost(3,0)==67);
    for(int i=0;i<9;i++){
        memset(&g_KeyItemRecords[i],0,sizeof g_KeyItemRecords[i]);
        g_KeyItemRecords[i].tailCount=1;g_KeyItemRecords[i].tailData[0]=0x0E;
        bag[0]=0x200+i;
        assert(Inv_ParasitePowerCost(3,0)==67);
    }
    const int invalid[]={0,0x180,0x1ff,0x209,-1};
    for(unsigned i=0;i<sizeof invalid/sizeof invalid[0];i++){
        bag[0]=(s16)invalid[i];
        assert(Inv_ParasitePowerCost(3,0)==101);
    }
    bag[0]=0x100;
    g_InventoryRuntime.tracked[2]=-1;
    assert(Inv_ParasitePowerCost(3,0)==101);
    g_InventoryRuntime.tracked[2]=1;
    assert(Inv_ParasitePowerCost(3,0)==101);
    g_InventoryRuntime.tracked[2]=0;g_InvSlotLimit=0;
    assert(Inv_ParasitePowerCost(3,0)==101);
    assert(Inv_ParasitePowerCost(6,37)==37 && Inv_ParasitePowerCost(19,23)==23);
    assert(Inv_ParasitePowerCost(5,0)==40);
    puts("PE menu cost: equipment, plain and special items, invalid slots, armor rounding and ammo powers");
}
static void other_actor(void){
    Combatant other={0};*rules=1;tick();aya.curHP=17;aya.maxHP=999;aya.exp_or_acc=1000*65536;
    g_ActiveActor=&other;Combatant before=aya;
    for(unsigned power=0;power<=7;++power){PeHookSpell spell={power,&player};hooks[PE_HOOK_SPELL](&spell,NULL);assert(!memcmp(&aya,&before,sizeof aya));}
}
int main(int argc,char **argv){
    if(argc==2 && !strcmp(argv[1],"--saved-vanilla"))saved_rules=0;
    pe_pe_balance_register("pe_rebalance");assert(rules && detox && scan && tick);
    if(saved_rules==0){assert(*rules==0 && !g_pe_half_enabled && !g_pe_panacea);vanilla();return 0;}
    if(argc==2 && !strcmp(argv[1],"--vanilla"))vanilla();else if(argc==2 && !strcmp(argv[1],"--actor"))other_actor();else {costs();menu_cost_inputs();}return 0;
}
