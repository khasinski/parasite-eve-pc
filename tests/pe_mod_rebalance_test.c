/* Exercise the public registration and hooks against the original damage module. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pe_game.h"
#include "pe_mod_host.h"
#include "pe_mod_menu.h"
#include "pe1/save_blob.h"
#include "pe1/entity_animation.h"

void pe_mod_effects_register(const char *);
void pe_mod_effects_shutdown(void);
SaveRuntimeState g_SaveRuntimeState;
Combatant *g_ActiveActor;
struct FieldActor *g_PlayerEntity;
struct FieldActor *g_FieldActorListHead;
BattleEnemySlot g_BattleEnemySlots[7];
u8 g_BattleActionAnimModes[4],g_BattleHitPending;
s8 g_BattleTargetCount=1;
const EnemyAnimTable g_BattleActionAnimRows={.rows={[1]={0,0,7,0,0}}};
static Combatant aya;
static BattleEntity player,target;
static BattleAction action;
static ItemDataRecord gun,base;
static int battle=1,loaded=1,dispatch=1;
static PeHookFn hooks[PE_HOOK_COUNT];
static PeMenuPerFrame tick;
static struct { const char *name; int *value; } rows[32];
static unsigned row_count;
static int saved_rules=-1;
int pe_menu_register_choice(const char *p,const char *n,const char *d,int *v,const char *const *c,unsigned count){
    (void)p;(void)d;(void)c;(void)count;if(!strcmp(n,"Rules") && saved_rules>=0)*v=saved_rules;assert(row_count<32);rows[row_count].name=n;rows[row_count++].value=v;return 0;
}
int pe_menu_register_int(const char *p,const char *n,const char *d,int *v,int lo,int hi,int step){
    (void)lo;(void)hi;(void)step;return pe_menu_register_choice(p,n,d,v,NULL,0);
}
int pe_menu_register_tick(PeMenuPerFrame fn){assert(!tick);tick=fn;return 0;}
int pe_hook_add(int id,PeHookFn fn,void *u,int priority,const char *mod){
    (void)u;(void)priority;(void)mod;assert(id>=0 && id<PE_HOOK_COUNT && !hooks[id]);hooks[id]=fn;return 0;
}
int pe_hook_dispatch(int id,void *ctx){return dispatch && hooks[id]?hooks[id](ctx,NULL):0;}
void pe_log(const char *tag,const char *fmt,...){(void)tag;(void)fmt;}
void pe_mod_log(const char *mod,const char *fmt,...){(void)mod;(void)fmt;}
int pe_game_loaded(void){return loaded;}
int pe_in_battle(void){return battle;}
BattleEntity *pe_player(void){return &player;}
Combatant *pe_aya_core(void){return &aya;}
ItemDataRecord *pe_equipped(int armor){return armor?NULL:&gun;}
ItemDataRecord *pe_base_item(unsigned id){(void)id;return &base;}
int pe_rec_has_effect(const ItemDataRecord *r,int effect){
    for(unsigned i=0;r && i<r->tailCount && i<10;++i)if((r->tailData[i]&31)==effect)return 1;return 0;
}
int Battle_CalcDistToPlayer(BattleEntity *e,BattleEntity *p){(void)e;(void)p;return 10;}
int Battle_CalcRelativeAngle(BattleEntity *e,BattleEntity *p){(void)e;(void)p;return 0;}
void Entity_SetActionMode(BattleEntity *e,int mode){e->actionMode=mode;}
static void setting(const char *name,int value){
    for(unsigned i=0;i<row_count;++i)if(!strcmp(rows[i].name,name)){*rows[i].value=value;return;}
    fprintf(stderr,"Missing row %s\n",name);abort();
}
static void setup(void){
    memset(&aya,0,sizeof aya);memset(&action,0,sizeof action);memset(&player,0,sizeof player);
    memset(&target,0,sizeof target);memset(g_BattleEnemySlots,0,sizeof g_BattleEnemySlots);
    memset(&gun,0,sizeof gun);memset(&base,0,sizeof base);
    player.core=&aya;g_PlayerEntity=(struct FieldActor *)&player;g_ActiveActor=&aya;aya.action=&action;
    aya.stat1E=100;aya.curHP=100;g_AyaSaveState.max_hp=100;
    action.powerBonus=100;action.range=100;action.turnWord=1;action.actionCode.bytes.actionIdByte=1;
    gun.kind=1;gun.itemId=0x44;target.core=&g_BattleEnemySlots[0].combatant;
    EnemyCombatant *e=target.core;e->hpAlive=e->hpAliveCap=1000;e->defense=10;
    e->field04.bytes.kind=1;e->coreFlags=0x10000;
    g_BattleEnemySlots[0].active=1;g_BattleHitPending=0;
}
static void vanilla(void){
    setting("Rules",0);setup();tick();
    /* Scripts can choose an action hold different from the factory table. */
    for(unsigned id=0;id<9;++id){action.actionCode.bytes.actionIdByte=id;action.animMode[2]=91;tick();assert(action.animMode[2]==91);}
    unsigned cases=0;const unsigned shots[]={1,2,3,5,7,10};
    const unsigned effects[]={0,0x100,0x200,0x300,0x800,0x1000,0x20000};
    for(unsigned s=0;s<6;++s)for(unsigned f=0;f<7;++f)for(unsigned r=0;r<4;++r){
        EnemyCombatant expected;BattleAction expected_action;unsigned random_after;
        setup();action.turnWord=shots[s]|effects[f];((EnemyCombatant *)target.core)->statusFlags2=r*0x4444u;
        srand(17);dispatch=0;Battle_StepEnemyMovement(&target);expected=*(EnemyCombatant *)target.core;expected_action=action;random_after=rand();
        setup();action.turnWord=shots[s]|effects[f];((EnemyCombatant *)target.core)->statusFlags2=r*0x4444u;
        srand(17);dispatch=1;Battle_StepEnemyMovement(&target);
        assert(!memcmp(target.core,&expected,sizeof expected));assert(!memcmp(&action,&expected_action,sizeof action));assert((unsigned)rand()==random_after);++cases;
    }
    printf("Vanilla: authored animation holds and %u original damage/status/RNG cases preserved\n",cases);
}
static void scoped(void){
    setting("Rules",1);setup();tick();action.turnWord|=0x100;
    BattleAction before=action;EnemyCombatant *e=target.core;e->statusFlags2=1u<<14;
    Battle_StepEnemyMovement(&target);
    assert(!memcmp(&action,&before,sizeof action));assert(e->defense==10);assert((e->statusFlags2&(15u<<14))==(1u<<14));
    setup();aya.stat1E=1000;action.powerBonus=0;action.turnWord=1|0x100;
    e=target.core;e->defense=0;e->statusFlags2=1u<<14;
    Battle_StepEnemyMovement(&target);assert(e->hpAlive==840 && action.powerBonus==0);
    /* Installed RoF can temporarily change the hold, but must restore what it
     * replaced and must leave a later change by the game alone. */
    setup();gun.tailCount=1;gun.tailData[0]=3;action.animMode[2]=91;tick();assert(action.animMode[2]==0);
    setting("Rules",0);tick();assert(action.animMode[2]==91);
    setting("Rules",1);tick();assert(action.animMode[2]==0);action.animMode[2]=42;
    setting("Rules",0);tick();assert(action.animMode[2]==42);
    battle=0;tick();battle=1;setting("Rules",1);setup();tick();
    e=target.core;e->hpAlive=e->hpAliveCap=4;
    g_BattleEnemySlots[1].active=1;g_BattleEnemySlots[1].combatant.hpAlive=4;
    action.powerBonus=500;action.turnWord=1|0xC0;
    for(unsigned i=0;i<8;++i){e->coreFlags=0x10000;Battle_StepEnemyMovement(&target);assert(e->hpAlive>=1);assert(action.powerBonus==500);}
    /* Secondary boss hitboxes (slot 1 is the boss, or entity has 0x2000) keep their status immunity. */
    setup();setting("Acid/Tranq vs resist %",100);action.turnWord=1|0x1000;
    e=target.core;e->statusFlags2=1u<<4;
    g_BattleEnemySlots[1].active=1;g_BattleEnemySlots[1].combatant.hpAlive=1000;
    g_BattleEnemySlots[1].combatant.statusFlags2=0x40000u;
    target.actionMode=2;Battle_StepEnemyMovement(&target);
    assert(!(e->coreFlags&0x1800u) && target.actionMode==2);
    g_BattleEnemySlots[1].active=0;target.entityFlags=0x2000u;
    Battle_StepEnemyMovement(&target);
    assert(!(e->coreFlags&0x1800u) && target.actionMode==2);
    puts("Rebalance: scoped hit inputs, owned animation restore and Attack All cap at minimum damage");
}
static void poison(void){
    setting("Rules",1);setup();tick();
    EnemyCombatant *e=target.core;e->periodicDamage=10;tick();assert(e->periodicDamage==12);
    tick();assert(e->periodicDamage==12);
    setting("Rules",0);tick();assert(e->periodicDamage==10);
    setting("Rules",1);tick();assert(e->periodicDamage==12);
    e->periodicDamage=7;setting("Rules",0);tick();assert(e->periodicDamage==7);
}
int main(int argc,char **argv){
    if(argc==2 && !strcmp(argv[1],"--saved-vanilla"))saved_rules=0;
    pe_mod_effects_register("pe_rebalance");assert(tick);
    if(saved_rules==0){assert(*rows[0].value==0);vanilla();return 0;}
    if(argc==2 && !strcmp(argv[1],"--vanilla"))vanilla();
    else if(argc==2 && !strcmp(argv[1],"--poison"))poison();else scoped();
    pe_mod_effects_shutdown();return 0;
}
