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
ItemDataRecord *Item_LookupBaseData(unsigned int index){(void)index;return NULL;}
int Inv_GetSlotHighlightState(int,int);
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
/* The game's own charge after PE_HOOK_SPELL_DONE (attack.c): PE Light takes a third off. */
static void charge(int drain){aya.exp_or_acc-=(aya.attributes&&(aya.attributes->effectFlags&0x200))?drain*2/3:drain;}
static void vanilla(void){
    *rules=0;tick();assert(!g_pe_half_enabled && !g_pe_panacea && !g_pe_quick_mask);
    for(unsigned power=0;power<20;++power){
        memset(&aya,0,sizeof aya);aya.curHP=17;aya.maxHP=999;aya.exp_or_acc=1000*65536;
        g_ActiveActor=&aya;Combatant before=aya;PeHookSpell spell={power,&player};
        hooks[PE_HOOK_SPELL](&spell,NULL);assert(!memcmp(&aya,&before,sizeof aya));
        {int drain=1234;PeHookSpellDone done={power,&player,&aya,17,&drain};hooks[PE_HOOK_SPELL_DONE](&done,NULL);
         assert(drain==1234&&!memcmp(&aya,&before,sizeof aya));}
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
        assert(aya.exp_or_acc==1000*65536);   /* nothing is refunded up front any more */
        int drain=(power==3?100:50)*65536;PeHookSpellDone done={power,&player,&aya,0,&drain};
        hooks[PE_HOOK_SPELL_DONE](&done,NULL);assert(drain==cost*65536);
        charge(drain);
        int expected=cost*65536;if(light)expected=expected*2/3;
        assert(aya.exp_or_acc==1000*65536-expected);
        int menu_cost=power==3?100:50;PeHookPowerCost query={(int)power,&menu_cost};
        hooks[PE_HOOK_POWER_COST](&query,NULL);assert(menu_cost==cost);
        /* Exercise the original menu lookup, including its armor discount. */
        s16 bag[1]={0x100};g_InvItemPtr=bag;g_InvSlotLimit=1;
        g_InventoryRuntime.tracked[2]=0;
        ItemDataRecord *armor=&g_InventoryRuntime.equipment[0];
        armor->tailCount=light?1:0;armor->tailData[0]=0x0E;
        spells[power].cost=power==3?100:50;
        assert(Inv_GetSlotHighlightState((int)power,0)==(light?cost*2/3:cost));
    }
    puts("PE costs: 124 cases match native charge rounding and menu affordability");
}
static void other_actor(void){
    Combatant other={0};*rules=1;tick();aya.curHP=17;aya.maxHP=999;aya.exp_or_acc=1000*65536;
    g_ActiveActor=&other;Combatant before=aya;
    for(unsigned power=0;power<=7;++power){PeHookSpell spell={power,&player};hooks[PE_HOOK_SPELL](&spell,NULL);assert(!memcmp(&aya,&before,sizeof aya));
        int drain=4321;PeHookSpellDone done={power,&player,&other,0,&drain};hooks[PE_HOOK_SPELL_DONE](&done,NULL);
        assert(drain==4321&&!memcmp(&aya,&before,sizeof aya));}
    /* Heals at SPELL_DONE: Rebalanced Heal 1 on Aya (999 max HP) adds 15% - 30 vanilla = 119. */
    g_ActiveActor=&aya;aya.curHP=47;{int drain=0x3C0000;PeHookSpellDone done={0,&player,&aya,17,&drain};
     hooks[PE_HOOK_SPELL_DONE](&done,NULL);assert(aya.curHP==47+119&&drain==0x3C0000);}
}
int main(int argc,char **argv){
    if(argc==2 && !strcmp(argv[1],"--saved-vanilla"))saved_rules=0;
    pe_pe_balance_register("pe_rebalance");assert(rules && detox && scan && tick);
    if(saved_rules==0){assert(*rules==0 && !g_pe_half_enabled && !g_pe_panacea);vanilla();return 0;}
    if(argc==2 && !strcmp(argv[1],"--vanilla"))vanilla();else if(argc==2 && !strcmp(argv[1],"--actor"))other_actor();else costs();return 0;
}
