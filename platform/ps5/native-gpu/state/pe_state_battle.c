#include "pe_state_battle.h"
#include "pe_state_boundary.h"
#include "pe_state_assets.h"
#include "pe1/battle_saved_state.h"
#include "pe1/battle_modifiers.h"
#include "pe1/battle_runtime.h"
#include "pe1/battle_main.h"
#include "pe1/entity_allocation.h"
#include <string.h>
#include <limits.h>
#define PLAYER_VALUES 61u
#define ENEMY_VALUES 63u
#define ACTION_VALUES 11u
#define EVENT_VALUES 9u
#define GLOBAL_VALUES 13u
#define BATTLE_BYTES_MAX 32768u
PE1_STATIC_ASSERT(PE1_OFFSETOF(Combatant,panelC_mode)<PE1_OFFSETOF(AyaBattleState,action),numeric_player_prefix_bounds);
typedef struct Cursor {const unsigned char *in;unsigned char *out;size_t size,at;int ok;} Cursor;
static void put(Cursor *c,uint32_t v){if(c->at>c->size||c->size-c->at<4){c->ok=0;return;}for(unsigned i=0;i<4;i++)c->out[c->at++]=(unsigned char)(v>>(8*i));}
static uint32_t get(Cursor *c){uint32_t v=0;if(c->at>c->size||c->size-c->at<4){c->ok=0;return 0;}for(unsigned i=0;i<4;i++)v|=(uint32_t)c->in[c->at++]<<(8*i);return v;}
static void put64(Cursor *c,uint64_t v){put(c,(uint32_t)v);put(c,(uint32_t)(v>>32));}
static uint64_t get64(Cursor *c){uint64_t lo=get(c);uint64_t hi=get(c);return lo|(hi<<32);}
static int canonical(uint32_t v,unsigned kind){switch(kind){case 1:return v<=255;case 2:return v<=65535;case 3:return (int32_t)v>=INT8_MIN&&(int32_t)v<=INT8_MAX;case 4:return (int32_t)v>=INT16_MIN&&(int32_t)v<=INT16_MAX;default:return 1;}}
static Combatant *player(void){return (Combatant *)(void *)&g_AyaBattleState;}
static uint32_t actor_id(const BattleEntity *ptr){if(!ptr)return 0;uintptr_t v=(uintptr_t)ptr,b=(uintptr_t)g_EntityPool;if(v<b||v-b>=sizeof g_EntityPool||(v-b)%sizeof(FieldActor))return UINT32_MAX;return (uint32_t)((v-b)/sizeof(FieldActor))+1;}
static uint32_t core_id(const Combatant *ptr){if(!ptr)return 0;if((const void *)ptr==(const void *)&g_AyaBattleState)return 1;for(unsigned i=0;i<7;i++)if((const void *)ptr==(const void *)&g_BattleEnemySlots[i].combatant)return i+2;return UINT32_MAX;}
static uint32_t effect_id(const EnemyCombatant *core){if(!core->effect)return 0;uintptr_t v=(uintptr_t)core->effect,b=(uintptr_t)core->records;if(v<b||v-b>=sizeof core->records||(v-b)%sizeof(EnemyActionEffect))return UINT32_MAX;return (uint32_t)((v-b)/sizeof(EnemyActionEffect))+1;}
int PeBattleNumeric_Bind(PeBattleNumeric *ctx){static uint64_t sequence;if(!ctx||!PeStateBoundary_AtCpuPoint())return PE_STATE_BUSY;memset(ctx,0,sizeof *ctx);ctx->session=PeStateBoundary_Session();ctx->scene=PeStateBoundary_SceneEpoch();ctx->binding=++sequence;if(!ctx->binding)ctx->binding=++sequence;ctx->assetGeneration=PeStateAssets_Generation();ctx->entitySerialCounter=g_EntitySerialCounter;ctx->entityLiveCount=g_EntityFreePoolCount;ctx->playerAction=g_AyaBattleState.action;ctx->playerAttributes=g_AyaBattleState.attributes;for(unsigned i=0;i<7;i++){const EnemyCombatant *e=&g_BattleEnemySlots[i].combatant;ctx->active[i]=g_BattleEnemySlots[i].active;ctx->enemyAction[i]=e->action;ctx->enemyAttributes[i]=e->attributes;ctx->enemyField70[i]=e->field70;}return 0;}
static int bound(const PeBattleNumeric *ctx){if(!ctx||!ctx->binding||!PeStateBoundary_AtCpuPoint()||ctx->session!=PeStateBoundary_Session()||ctx->scene!=PeStateBoundary_SceneEpoch()||ctx->assetGeneration!=PeStateAssets_Generation()||ctx->entitySerialCounter!=g_EntitySerialCounter||ctx->entityLiveCount!=g_EntityFreePoolCount||ctx->playerAction!=g_AyaBattleState.action||ctx->playerAttributes!=g_AyaBattleState.attributes)return 0;for(unsigned i=0;i<7;i++){const EnemyCombatant *e=&g_BattleEnemySlots[i].combatant;if(ctx->active[i]!=g_BattleEnemySlots[i].active)return 0;if(ctx->active[i]&&(ctx->enemyAction[i]!=e->action||ctx->enemyAttributes[i]!=e->attributes||ctx->enemyField70[i]!=e->field70))return 0;}return 1;}
static void put_player(Cursor *c,const Combatant *value){
    put(c,(uint32_t)value->coreFlags);
    put(c,(uint32_t)value->fieldId06);
    put(c,(uint32_t)value->exp_or_acc);
    put(c,(uint32_t)value->curHP);
    put(c,(uint32_t)value->hpMirror);
    put(c,(uint32_t)value->hpAlive);
    put(c,(uint32_t)value->actionMode12);
    put(c,(uint32_t)value->actionMode13);
    put(c,(uint32_t)value->targetAngleActionModes[0]);
    put(c,(uint32_t)value->targetAngleActionModes[1]);
    put(c,(uint32_t)value->targetAngleActionModes[2]);
    put(c,(uint32_t)value->followUpAngleActionModes[0]);
    put(c,(uint32_t)value->followUpAngleActionModes[1]);
    put(c,(uint32_t)value->followUpAngleActionModes[2]);
    put(c,(uint32_t)value->maxHP);
    put(c,(uint32_t)value->stat1E);
    put(c,(uint32_t)value->stat20);
    put(c,(uint32_t)value->stat22);
    put(c,(uint32_t)value->scaledOffense);
    put(c,(uint32_t)value->stat26);
    put(c,(uint32_t)value->maxAtk);
    put(c,(uint32_t)value->atbStep);
    put(c,(uint32_t)value->atbRate);
    put(c,(uint32_t)value->atbGauge);
    put(c,(uint32_t)value->subActionStep);
    put(c,(uint32_t)value->subActionCounter);
    put(c,(uint32_t)value->subActionPeriod);
    put(c,(uint32_t)value->statusStep3C);
    put(c,(uint32_t)value->statusStep3E);
    put(c,(uint32_t)value->statusTimer40);
    put(c,(uint32_t)value->statusTimer42);
    put(c,(uint32_t)value->statusTimer44);
    put(c,(uint32_t)value->statusTimer46);
    put(c,(uint32_t)value->knockbackFrames);
    put(c,(uint32_t)value->knockbackDistance);
    put(c,(uint32_t)value->knockbackAngle);
    put(c,(uint32_t)value->stateFlags);
    put(c,(uint32_t)value->panelA_val);
    put(c,(uint32_t)value->panelA_x);
    put(c,(uint32_t)value->panelA_y);
    put(c,(uint32_t)value->panelA_timer);
    put(c,(uint32_t)value->panelA_scale);
    put(c,(uint32_t)value->panelB_val);
    put(c,(uint32_t)value->panelB_x);
    put(c,(uint32_t)value->panelB_y);
    put(c,(uint32_t)value->panelB_timer);
    put(c,(uint32_t)value->panelB_flag);
    put(c,(uint32_t)value->panelAux_val);
    put(c,(uint32_t)value->panelAux_x);
    put(c,(uint32_t)value->panelAux_y);
    put(c,(uint32_t)value->panelAux_timer);
    put(c,(uint32_t)value->panelAux_mode);
    put(c,(uint32_t)value->field88);
    put(c,(uint32_t)value->field8C);
    put(c,(uint32_t)value->statusFlags2);
    put(c,(uint32_t)value->panelC_val);
    put(c,(uint32_t)value->panelC_x);
    put(c,(uint32_t)value->panelC_y);
    put(c,(uint32_t)value->panelC_timer);
    put(c,(uint32_t)value->panelC_mode);
    put(c,(uint32_t)value->field04.fieldId04);
}
static int get_player(Cursor *c,uint32_t *value){
    value[0]=get(c);if(!canonical(value[0],0))c->ok=0;
    value[1]=get(c);if(!canonical(value[1],4))c->ok=0;
    value[2]=get(c);if(!canonical(value[2],0))c->ok=0;
    value[3]=get(c);if(!canonical(value[3],2))c->ok=0;
    value[4]=get(c);if(!canonical(value[4],2))c->ok=0;
    value[5]=get(c);if(!canonical(value[5],2))c->ok=0;
    value[6]=get(c);if(!canonical(value[6],1))c->ok=0;
    value[7]=get(c);if(!canonical(value[7],3))c->ok=0;
    value[8]=get(c);if(!canonical(value[8],1))c->ok=0;
    value[9]=get(c);if(!canonical(value[9],1))c->ok=0;
    value[10]=get(c);if(!canonical(value[10],1))c->ok=0;
    value[11]=get(c);if(!canonical(value[11],1))c->ok=0;
    value[12]=get(c);if(!canonical(value[12],1))c->ok=0;
    value[13]=get(c);if(!canonical(value[13],1))c->ok=0;
    value[14]=get(c);if(!canonical(value[14],2))c->ok=0;
    value[15]=get(c);if(!canonical(value[15],4))c->ok=0;
    value[16]=get(c);if(!canonical(value[16],4))c->ok=0;
    value[17]=get(c);if(!canonical(value[17],2))c->ok=0;
    value[18]=get(c);if(!canonical(value[18],2))c->ok=0;
    value[19]=get(c);if(!canonical(value[19],4))c->ok=0;
    value[20]=get(c);if(!canonical(value[20],0))c->ok=0;
    value[21]=get(c);if(!canonical(value[21],0))c->ok=0;
    value[22]=get(c);if(!canonical(value[22],0))c->ok=0;
    value[23]=get(c);if(!canonical(value[23],0))c->ok=0;
    value[24]=get(c);if(!canonical(value[24],2))c->ok=0;
    value[25]=get(c);if(!canonical(value[25],1))c->ok=0;
    value[26]=get(c);if(!canonical(value[26],1))c->ok=0;
    value[27]=get(c);if(!canonical(value[27],2))c->ok=0;
    value[28]=get(c);if(!canonical(value[28],2))c->ok=0;
    value[29]=get(c);if(!canonical(value[29],2))c->ok=0;
    value[30]=get(c);if(!canonical(value[30],2))c->ok=0;
    value[31]=get(c);if(!canonical(value[31],2))c->ok=0;
    value[32]=get(c);if(!canonical(value[32],2))c->ok=0;
    value[33]=get(c);if(!canonical(value[33],3))c->ok=0;
    value[34]=get(c);if(!canonical(value[34],1))c->ok=0;
    value[35]=get(c);if(!canonical(value[35],4))c->ok=0;
    value[36]=get(c);if(!canonical(value[36],0))c->ok=0;
    value[37]=get(c);if(!canonical(value[37],4))c->ok=0;
    value[38]=get(c);if(!canonical(value[38],2))c->ok=0;
    value[39]=get(c);if(!canonical(value[39],2))c->ok=0;
    value[40]=get(c);if(!canonical(value[40],1))c->ok=0;
    value[41]=get(c);if(!canonical(value[41],1))c->ok=0;
    value[42]=get(c);if(!canonical(value[42],4))c->ok=0;
    value[43]=get(c);if(!canonical(value[43],2))c->ok=0;
    value[44]=get(c);if(!canonical(value[44],2))c->ok=0;
    value[45]=get(c);if(!canonical(value[45],1))c->ok=0;
    value[46]=get(c);if(!canonical(value[46],3))c->ok=0;
    value[47]=get(c);if(!canonical(value[47],2))c->ok=0;
    value[48]=get(c);if(!canonical(value[48],2))c->ok=0;
    value[49]=get(c);if(!canonical(value[49],2))c->ok=0;
    value[50]=get(c);if(!canonical(value[50],1))c->ok=0;
    value[51]=get(c);if(!canonical(value[51],1))c->ok=0;
    value[52]=get(c);if(!canonical(value[52],0))c->ok=0;
    value[53]=get(c);if(!canonical(value[53],2))c->ok=0;
    value[54]=get(c);if(!canonical(value[54],0))c->ok=0;
    value[55]=get(c);if(!canonical(value[55],4))c->ok=0;
    value[56]=get(c);if(!canonical(value[56],2))c->ok=0;
    value[57]=get(c);if(!canonical(value[57],2))c->ok=0;
    value[58]=get(c);if(!canonical(value[58],1))c->ok=0;
    value[59]=get(c);if(!canonical(value[59],1))c->ok=0;
    value[60]=get(c);if(!canonical(value[60],4))c->ok=0;
    return c->ok;
}
static void restore_player(Combatant *value,const uint32_t *input){
    value->coreFlags=(unsigned int)input[0];
    value->fieldId06=(short)input[1];
    value->exp_or_acc=(int)input[2];
    value->curHP=(unsigned short)input[3];
    value->hpMirror=(unsigned short)input[4];
    value->hpAlive=(unsigned short)input[5];
    value->actionMode12=(unsigned char)input[6];
    value->actionMode13=(signed char)input[7];
    value->targetAngleActionModes[0]=(unsigned char)input[8];
    value->targetAngleActionModes[1]=(unsigned char)input[9];
    value->targetAngleActionModes[2]=(unsigned char)input[10];
    value->followUpAngleActionModes[0]=(unsigned char)input[11];
    value->followUpAngleActionModes[1]=(unsigned char)input[12];
    value->followUpAngleActionModes[2]=(unsigned char)input[13];
    value->maxHP=(unsigned short)input[14];
    value->stat1E=(short)input[15];
    value->stat20=(short)input[16];
    value->stat22=(unsigned short)input[17];
    value->scaledOffense=(unsigned short)input[18];
    value->stat26=(short)input[19];
    value->maxAtk=(int)input[20];
    value->atbStep=(int)input[21];
    value->atbRate=(int)input[22];
    value->atbGauge=(int)input[23];
    value->subActionStep=(unsigned short)input[24];
    value->subActionCounter=(unsigned char)input[25];
    value->subActionPeriod=(unsigned char)input[26];
    value->statusStep3C=(unsigned short)input[27];
    value->statusStep3E=(unsigned short)input[28];
    value->statusTimer40=(unsigned short)input[29];
    value->statusTimer42=(unsigned short)input[30];
    value->statusTimer44=(unsigned short)input[31];
    value->statusTimer46=(unsigned short)input[32];
    value->knockbackFrames=(signed char)input[33];
    value->knockbackDistance=(unsigned char)input[34];
    value->knockbackAngle=(short)input[35];
    value->stateFlags=(int)input[36];
    value->panelA_val=(short)input[37];
    value->panelA_x=(unsigned short)input[38];
    value->panelA_y=(unsigned short)input[39];
    value->panelA_timer=(unsigned char)input[40];
    value->panelA_scale=(unsigned char)input[41];
    value->panelB_val=(short)input[42];
    value->panelB_x=(unsigned short)input[43];
    value->panelB_y=(unsigned short)input[44];
    value->panelB_timer=(unsigned char)input[45];
    value->panelB_flag=(signed char)input[46];
    value->panelAux_val=(unsigned short)input[47];
    value->panelAux_x=(unsigned short)input[48];
    value->panelAux_y=(unsigned short)input[49];
    value->panelAux_timer=(unsigned char)input[50];
    value->panelAux_mode=(unsigned char)input[51];
    value->field88=(int)input[52];
    value->field8C=(unsigned short)input[53];
    value->statusFlags2=(unsigned int)input[54];
    value->panelC_val=(short)input[55];
    value->panelC_x=(unsigned short)input[56];
    value->panelC_y=(unsigned short)input[57];
    value->panelC_timer=(unsigned char)input[58];
    value->panelC_mode=(unsigned char)input[59];
    value->field04.fieldId04=(short)input[60];
}
static void put_enemy(Cursor *c,const EnemyCombatant *value){
    put(c,(uint32_t)value->coreFlags);
    put(c,(uint32_t)value->slotBit);
    put(c,(uint32_t)value->curHP);
    put(c,(uint32_t)value->hpMirror);
    put(c,(uint32_t)value->hpAlive);
    put(c,(uint32_t)value->previousHP);
    put(c,(uint32_t)value->contactActionIds[0]);
    put(c,(uint32_t)value->contactActionIds[1]);
    put(c,(uint32_t)value->contactActionIds[2]);
    put(c,(uint32_t)value->contactActionIds[3]);
    put(c,(uint32_t)value->contactActionIds[4]);
    put(c,(uint32_t)value->contactActionIds[5]);
    put(c,(uint32_t)value->contactActionIds[6]);
    put(c,(uint32_t)value->contactActionIds[7]);
    put(c,(uint32_t)value->contactActionIds[8]);
    put(c,(uint32_t)value->contactActionIds[9]);
    put(c,(uint32_t)value->contactActionIds[10]);
    put(c,(uint32_t)value->contactActionIds[11]);
    put(c,(uint32_t)value->hpAliveCap);
    put(c,(uint32_t)value->defense);
    put(c,(uint32_t)value->chargeStep);
    put(c,(uint32_t)value->effectChance);
    put(c,(uint32_t)value->deathAnimFrame);
    put(c,(uint32_t)value->contactDamage);
    put(c,(uint32_t)value->contactKnockback);
    put(c,(uint32_t)value->effectLevel);
    put(c,(uint32_t)value->effectDuration);
    put(c,(uint32_t)value->periodicDamage);
    put(c,(uint32_t)value->rewardBase);
    put(c,(uint32_t)value->rewardFactorA);
    put(c,(uint32_t)value->rewardFactorB);
    put(c,(uint32_t)value->rewardSlotId);
    put(c,(uint32_t)value->stealItemId);
    put(c,(uint32_t)value->lootItemId);
    put(c,(uint32_t)value->lootItemAux);
    put(c,(uint32_t)value->motionPhase);
    put(c,(uint32_t)value->motionFrames);
    put(c,(uint32_t)value->motionAmplitude);
    put(c,(uint32_t)value->motionAngle);
    put(c,(uint32_t)value->deathAnimPhase);
    put(c,(uint32_t)value->deathFadeStep);
    put(c,(uint32_t)value->deathAssetEnabled);
    put(c,(uint32_t)value->deathPersist);
    put(c,(uint32_t)value->attackAssetId);
    put(c,(uint32_t)value->eventAssetId);
    put(c,(uint32_t)value->deathAssetId);
    put(c,(uint32_t)value->contactAssetIds[0]);
    put(c,(uint32_t)value->contactAssetIds[1]);
    put(c,(uint32_t)value->contactAssetIds[2]);
    put(c,(uint32_t)value->resumePhase);
    put(c,(uint32_t)value->savedActionMode);
    put(c,(uint32_t)value->savedInterpolation);
    put(c,(uint32_t)value->savedAnimFrame);
    put(c,(uint32_t)value->savedAnimPrev);
    put(c,(uint32_t)value->savedAnimStep);
    put(c,(uint32_t)value->statusFlags2);
    put(c,(uint32_t)value->panelC_val);
    put(c,(uint32_t)value->panelC_x);
    put(c,(uint32_t)value->panelC_y);
    put(c,(uint32_t)value->panelC_timer);
    put(c,(uint32_t)value->panelC_mode);
    put(c,(uint32_t)value->field04.fieldId04);
    put(c,(uint32_t)value->lowAndEntityId.field06);
}
static int get_enemy(Cursor *c,uint32_t *value){
    value[0]=get(c);if(!canonical(value[0],0))c->ok=0;
    value[1]=get(c);if(!canonical(value[1],0))c->ok=0;
    value[2]=get(c);if(!canonical(value[2],2))c->ok=0;
    value[3]=get(c);if(!canonical(value[3],2))c->ok=0;
    value[4]=get(c);if(!canonical(value[4],0))c->ok=0;
    value[5]=get(c);if(!canonical(value[5],0))c->ok=0;
    value[6]=get(c);if(!canonical(value[6],3))c->ok=0;
    value[7]=get(c);if(!canonical(value[7],3))c->ok=0;
    value[8]=get(c);if(!canonical(value[8],3))c->ok=0;
    value[9]=get(c);if(!canonical(value[9],3))c->ok=0;
    value[10]=get(c);if(!canonical(value[10],3))c->ok=0;
    value[11]=get(c);if(!canonical(value[11],3))c->ok=0;
    value[12]=get(c);if(!canonical(value[12],3))c->ok=0;
    value[13]=get(c);if(!canonical(value[13],3))c->ok=0;
    value[14]=get(c);if(!canonical(value[14],3))c->ok=0;
    value[15]=get(c);if(!canonical(value[15],3))c->ok=0;
    value[16]=get(c);if(!canonical(value[16],3))c->ok=0;
    value[17]=get(c);if(!canonical(value[17],3))c->ok=0;
    value[18]=get(c);if(!canonical(value[18],0))c->ok=0;
    value[19]=get(c);if(!canonical(value[19],2))c->ok=0;
    value[20]=get(c);if(!canonical(value[20],2))c->ok=0;
    value[21]=get(c);if(!canonical(value[21],1))c->ok=0;
    value[22]=get(c);if(!canonical(value[22],1))c->ok=0;
    value[23]=get(c);if(!canonical(value[23],1))c->ok=0;
    value[24]=get(c);if(!canonical(value[24],1))c->ok=0;
    value[25]=get(c);if(!canonical(value[25],1))c->ok=0;
    value[26]=get(c);if(!canonical(value[26],1))c->ok=0;
    value[27]=get(c);if(!canonical(value[27],4))c->ok=0;
    value[28]=get(c);if(!canonical(value[28],2))c->ok=0;
    value[29]=get(c);if(!canonical(value[29],2))c->ok=0;
    value[30]=get(c);if(!canonical(value[30],2))c->ok=0;
    value[31]=get(c);if(!canonical(value[31],1))c->ok=0;
    value[32]=get(c);if(!canonical(value[32],1))c->ok=0;
    value[33]=get(c);if(!canonical(value[33],4))c->ok=0;
    value[34]=get(c);if(!canonical(value[34],4))c->ok=0;
    value[35]=get(c);if(!canonical(value[35],1))c->ok=0;
    value[36]=get(c);if(!canonical(value[36],1))c->ok=0;
    value[37]=get(c);if(!canonical(value[37],2))c->ok=0;
    value[38]=get(c);if(!canonical(value[38],4))c->ok=0;
    value[39]=get(c);if(!canonical(value[39],1))c->ok=0;
    value[40]=get(c);if(!canonical(value[40],1))c->ok=0;
    value[41]=get(c);if(!canonical(value[41],1))c->ok=0;
    value[42]=get(c);if(!canonical(value[42],1))c->ok=0;
    value[43]=get(c);if(!canonical(value[43],2))c->ok=0;
    value[44]=get(c);if(!canonical(value[44],2))c->ok=0;
    value[45]=get(c);if(!canonical(value[45],2))c->ok=0;
    value[46]=get(c);if(!canonical(value[46],2))c->ok=0;
    value[47]=get(c);if(!canonical(value[47],2))c->ok=0;
    value[48]=get(c);if(!canonical(value[48],2))c->ok=0;
    value[49]=get(c);if(!canonical(value[49],1))c->ok=0;
    value[50]=get(c);if(!canonical(value[50],1))c->ok=0;
    value[51]=get(c);if(!canonical(value[51],1))c->ok=0;
    value[52]=get(c);if(!canonical(value[52],0))c->ok=0;
    value[53]=get(c);if(!canonical(value[53],0))c->ok=0;
    value[54]=get(c);if(!canonical(value[54],0))c->ok=0;
    value[55]=get(c);if(!canonical(value[55],0))c->ok=0;
    value[56]=get(c);if(!canonical(value[56],4))c->ok=0;
    value[57]=get(c);if(!canonical(value[57],2))c->ok=0;
    value[58]=get(c);if(!canonical(value[58],2))c->ok=0;
    value[59]=get(c);if(!canonical(value[59],1))c->ok=0;
    value[60]=get(c);if(!canonical(value[60],1))c->ok=0;
    value[61]=get(c);if(!canonical(value[61],4))c->ok=0;
    value[62]=get(c);if(!canonical(value[62],4))c->ok=0;
    return c->ok;
}
static void restore_enemy(EnemyCombatant *value,const uint32_t *input){
    value->coreFlags=(unsigned int)input[0];
    value->slotBit=(int)input[1];
    value->curHP=(unsigned short)input[2];
    value->hpMirror=(unsigned short)input[3];
    value->hpAlive=(int)input[4];
    value->previousHP=(int)input[5];
    value->contactActionIds[0]=(signed char)input[6];
    value->contactActionIds[1]=(signed char)input[7];
    value->contactActionIds[2]=(signed char)input[8];
    value->contactActionIds[3]=(signed char)input[9];
    value->contactActionIds[4]=(signed char)input[10];
    value->contactActionIds[5]=(signed char)input[11];
    value->contactActionIds[6]=(signed char)input[12];
    value->contactActionIds[7]=(signed char)input[13];
    value->contactActionIds[8]=(signed char)input[14];
    value->contactActionIds[9]=(signed char)input[15];
    value->contactActionIds[10]=(signed char)input[16];
    value->contactActionIds[11]=(signed char)input[17];
    value->hpAliveCap=(int)input[18];
    value->defense=(unsigned short)input[19];
    value->chargeStep=(unsigned short)input[20];
    value->effectChance=(unsigned char)input[21];
    value->deathAnimFrame=(unsigned char)input[22];
    value->contactDamage=(unsigned char)input[23];
    value->contactKnockback=(unsigned char)input[24];
    value->effectLevel=(unsigned char)input[25];
    value->effectDuration=(unsigned char)input[26];
    value->periodicDamage=(short)input[27];
    value->rewardBase=(unsigned short)input[28];
    value->rewardFactorA=(unsigned short)input[29];
    value->rewardFactorB=(unsigned short)input[30];
    value->rewardSlotId=(unsigned char)input[31];
    value->stealItemId=(unsigned char)input[32];
    value->lootItemId=(short)input[33];
    value->lootItemAux=(short)input[34];
    value->motionPhase=(unsigned char)input[35];
    value->motionFrames=(unsigned char)input[36];
    value->motionAmplitude=(unsigned short)input[37];
    value->motionAngle=(short)input[38];
    value->deathAnimPhase=(unsigned char)input[39];
    value->deathFadeStep=(unsigned char)input[40];
    value->deathAssetEnabled=(unsigned char)input[41];
    value->deathPersist=(unsigned char)input[42];
    value->attackAssetId=(unsigned short)input[43];
    value->eventAssetId=(unsigned short)input[44];
    value->deathAssetId=(unsigned short)input[45];
    value->contactAssetIds[0]=(unsigned short)input[46];
    value->contactAssetIds[1]=(unsigned short)input[47];
    value->contactAssetIds[2]=(unsigned short)input[48];
    value->resumePhase=(unsigned char)input[49];
    value->savedActionMode=(unsigned char)input[50];
    value->savedInterpolation=(unsigned char)input[51];
    value->savedAnimFrame=(int)input[52];
    value->savedAnimPrev=(int)input[53];
    value->savedAnimStep=(int)input[54];
    value->statusFlags2=(unsigned int)input[55];
    value->panelC_val=(short)input[56];
    value->panelC_x=(unsigned short)input[57];
    value->panelC_y=(unsigned short)input[58];
    value->panelC_timer=(unsigned char)input[59];
    value->panelC_mode=(unsigned char)input[60];
    value->field04.fieldId04=(short)input[61];
    value->lowAndEntityId.field06=(short)input[62];
}
static void put_action(Cursor *c,const BattleAction *value){
    put(c,(uint32_t)value->powerBonus);
    put(c,(uint32_t)value->range);
    put(c,(uint32_t)value->field04);
    put(c,(uint32_t)value->actionCode.actionId);
    put(c,(uint32_t)value->hitTrigger);
    put(c,(uint32_t)value->attackWord);
    put(c,(uint32_t)value->turnWord);
    put(c,(uint32_t)value->animMode[0]);
    put(c,(uint32_t)value->animMode[1]);
    put(c,(uint32_t)value->animMode[2]);
    put(c,(uint32_t)value->animMode[3]);
}
static int get_action(Cursor *c,uint32_t *value){
    value[0]=get(c);if(!canonical(value[0],2))c->ok=0;
    value[1]=get(c);if(!canonical(value[1],4))c->ok=0;
    value[2]=get(c);if(!canonical(value[2],2))c->ok=0;
    value[3]=get(c);if(!canonical(value[3],4))c->ok=0;
    value[4]=get(c);if(!canonical(value[4],0))c->ok=0;
    value[5]=get(c);if(!canonical(value[5],0))c->ok=0;
    value[6]=get(c);if(!canonical(value[6],0))c->ok=0;
    value[7]=get(c);if(!canonical(value[7],1))c->ok=0;
    value[8]=get(c);if(!canonical(value[8],1))c->ok=0;
    value[9]=get(c);if(!canonical(value[9],1))c->ok=0;
    value[10]=get(c);if(!canonical(value[10],1))c->ok=0;
    return c->ok;
}
static void restore_action(BattleAction *value,const uint32_t *input){
    value->powerBonus=(unsigned short)input[0];
    value->range=(short)input[1];
    value->field04=(unsigned short)input[2];
    value->actionCode.actionId=(short)input[3];
    value->hitTrigger=(int)input[4];
    value->attackWord=(unsigned int)input[5];
    value->turnWord=(unsigned int)input[6];
    value->animMode[0]=(unsigned char)input[7];
    value->animMode[1]=(unsigned char)input[8];
    value->animMode[2]=(unsigned char)input[9];
    value->animMode[3]=(unsigned char)input[10];
}
static void put_event(Cursor *c,const EnemyActionEffect *value){
    put(c,(uint32_t)value->state);
    put(c,(uint32_t)value->effectType);
    put(c,(uint32_t)value->enterMode);
    put(c,(uint32_t)value->exitMode);
    put(c,(uint32_t)value->enterStep);
    put(c,(uint32_t)value->exitStep);
    put(c,(uint32_t)value->power);
    put(c,(uint32_t)value->category);
    put(c,(uint32_t)value->frame);
}
static int get_event(Cursor *c,uint32_t *value){
    value[0]=get(c);if(!canonical(value[0],1))c->ok=0;
    value[1]=get(c);if(!canonical(value[1],1))c->ok=0;
    value[2]=get(c);if(!canonical(value[2],1))c->ok=0;
    value[3]=get(c);if(!canonical(value[3],1))c->ok=0;
    value[4]=get(c);if(!canonical(value[4],0))c->ok=0;
    value[5]=get(c);if(!canonical(value[5],0))c->ok=0;
    value[6]=get(c);if(!canonical(value[6],2))c->ok=0;
    value[7]=get(c);if(!canonical(value[7],1))c->ok=0;
    value[8]=get(c);if(!canonical(value[8],1))c->ok=0;
    return c->ok;
}
static void restore_event(EnemyActionEffect *value,const uint32_t *input){
    value->state=(unsigned char)input[0];
    value->effectType=(unsigned char)input[1];
    value->enterMode=(unsigned char)input[2];
    value->exitMode=(unsigned char)input[3];
    value->enterStep=(int)input[4];
    value->exitStep=(int)input[5];
    value->power=(unsigned short)input[6];
    value->category=(unsigned char)input[7];
    value->frame=(unsigned char)input[8];
}
static void put_globals(Cursor *c){
    put(c,(uint32_t)g_BattleHitTimer);
    put(c,(uint32_t)g_BattleHitPending);
    put(c,(uint32_t)g_BattleActionIndex);
    put(c,(uint32_t)g_BattleActionQueueCount);
    put(c,(uint32_t)g_BattleModeState);
    put(c,(uint32_t)g_BattleTargetCount);
    put(c,(uint32_t)g_BattleLiveEnemyCount);
    put(c,(uint32_t)g_BattleStepIndex);
    put(c,(uint32_t)g_BattleEnemyEscapeFlag);
    put(c,(uint32_t)g_BattleHaltTimer);
    put(c,(uint32_t)g_BattleRemainingActions);
    put(c,(uint32_t)g_MenuFrameCommand);
    put(c,(uint32_t)g_BattleTargetCycleMode);
}
static int get_globals(Cursor *c,uint32_t *value){
    value[0]=get(c);if(!canonical(value[0],1))c->ok=0;
    value[1]=get(c);if(!canonical(value[1],1))c->ok=0;
    value[2]=get(c);if(!canonical(value[2],1))c->ok=0;
    value[3]=get(c);if(!canonical(value[3],1))c->ok=0;
    value[4]=get(c);if(!canonical(value[4],0))c->ok=0;
    value[5]=get(c);if(!canonical(value[5],3))c->ok=0;
    value[6]=get(c);if(!canonical(value[6],3))c->ok=0;
    value[7]=get(c);if(!canonical(value[7],1))c->ok=0;
    value[8]=get(c);if(!canonical(value[8],1))c->ok=0;
    value[9]=get(c);if(!canonical(value[9],3))c->ok=0;
    value[10]=get(c);if(!canonical(value[10],1))c->ok=0;
    value[11]=get(c);if(!canonical(value[11],4))c->ok=0;
    value[12]=get(c);if(!canonical(value[12],3))c->ok=0;
    return c->ok;
}
static void restore_globals(const uint32_t *value){
    g_BattleHitTimer=(unsigned char)value[0];
    g_BattleHitPending=(unsigned char)value[1];
    g_BattleActionIndex=(unsigned char)value[2];
    g_BattleActionQueueCount=(unsigned char)value[3];
    g_BattleModeState=(int)value[4];
    g_BattleTargetCount=(signed char)value[5];
    g_BattleLiveEnemyCount=(signed char)value[6];
    g_BattleStepIndex=(unsigned char)value[7];
    g_BattleEnemyEscapeFlag=(unsigned char)value[8];
    g_BattleHaltTimer=(signed char)value[9];
    g_BattleRemainingActions=(unsigned char)value[10];
    g_MenuFrameCommand=(short)value[11];
    g_BattleTargetCycleMode=(signed char)value[12];
}

typedef struct BattleDecoded {
    uint32_t activeCore;
    uint32_t player[PLAYER_VALUES],enemy[7][ENEMY_VALUES],effect[7],events[7][6][EVENT_VALUES];
    uint32_t menuAction[ACTION_VALUES],tail[6],equip[2],globals[GLOBAL_VALUES];
    uint32_t queueActor[45],queueCommand[45],queueGeneration[45];
} BattleDecoded;
static int decode(PeBattleNumeric *,const unsigned char *,size_t,BattleDecoded *);
static int capture(void *context,unsigned char *out,size_t capacity,size_t *used){
    PeBattleNumeric *ctx=context;Cursor c={0,out,capacity,0,1};if(used)*used=0;if(!out||!used||!bound(ctx))return 0;
    put(&c,0x324c5442u);put64(&c,ctx->session);put64(&c,ctx->scene);put64(&c,ctx->binding);put64(&c,ctx->assetGeneration);
    put(&c,ctx->entitySerialCounter);put(&c,ctx->entityLiveCount);
    for(unsigned i=0;i<7;i++)put(&c,(uint32_t)ctx->active[i]);
    uint32_t active=core_id(g_ActiveActor);if(active==UINT32_MAX)return 0;put(&c,active);
    put_player(&c,player());
    for(unsigned i=0;i<7;i++){
        const EnemyCombatant *e=&g_BattleEnemySlots[i].combatant;put_enemy(&c,e);
        uint32_t selected=ctx->active[i]?effect_id(e):0;if(selected==UINT32_MAX)return 0;put(&c,selected);
        for(unsigned j=0;j<6;j++)put_event(&c,&e->records[j]);
    }
    put_action(&c,&g_BattleMenuActionRecord);
    for(unsigned i=0;i<6;i++)put(&c,(uint32_t)g_SavedBattleStateTail.words[i]);
    put(&c,g_BattleEquipStateBlock.parameterWord.raw);put(&c,g_BattleEquipStateBlock.effectFlags);
    put_globals(&c);
    for(unsigned i=0;i<45;i++){
        uint32_t id=actor_id(g_BattleActionQueue[i].actor);if(id==UINT32_MAX)return 0;
        put(&c,id);put(&c,(uint32_t)g_BattleActionQueue[i].command);put(&c,(uint32_t)g_BattleActionQueue[i].actionGeneration);
    }
    if(!c.ok)return 0;BattleDecoded checked;if(!decode(ctx,out,c.at,&checked))return 0;*used=c.at;return 1;
}
static int decode(PeBattleNumeric *ctx,const unsigned char *input,size_t bytes,BattleDecoded *d){
    Cursor c={input,0,bytes,0,1};if(!input||!d||bytes>BATTLE_BYTES_MAX||!bound(ctx))return 0;memset(d,0,sizeof *d);
    if(get(&c)!=0x324c5442u||get64(&c)!=ctx->session||get64(&c)!=ctx->scene||get64(&c)!=ctx->binding||get64(&c)!=ctx->assetGeneration||get(&c)!=ctx->entitySerialCounter||get(&c)!=ctx->entityLiveCount)return 0;
    for(unsigned i=0;i<7;i++)if(get(&c)!=(uint32_t)ctx->active[i])return 0;
    d->activeCore=get(&c);if(d->activeCore>8)return 0;
    if(!get_player(&c,d->player))return 0;
    for(unsigned i=0;i<7;i++){
        if(!get_enemy(&c,d->enemy[i]))return 0;
        d->effect[i]=get(&c);if(d->effect[i]>6||(!ctx->active[i]&&d->effect[i]))return 0;
        for(unsigned j=0;j<6;j++)if(!get_event(&c,d->events[i][j]))return 0;
    }
    if(!get_action(&c,d->menuAction))return 0;
    for(unsigned i=0;i<6;i++)d->tail[i]=get(&c);
    d->equip[0]=get(&c);d->equip[1]=get(&c);
    if(!get_globals(&c,d->globals))return 0;
    /* Source owns a fixed45-entry action queue; preserve sentinel index255. */
    if(d->globals[3]>45||(d->globals[2]>44&&d->globals[2]!=255)||d->globals[1]>1)return 0;
    for(unsigned i=0;i<45;i++){
        d->queueActor[i]=get(&c);d->queueCommand[i]=get(&c);d->queueGeneration[i]=get(&c);
        if(d->queueActor[i]>14||!canonical(d->queueCommand[i],4)||!canonical(d->queueGeneration[i],4))return 0;
    }
    return c.ok&&c.at==bytes;
}
static int validate(void *context,const unsigned char *input,size_t bytes){BattleDecoded decoded;return decode(context,input,bytes,&decoded);}
static int restore(void *context,const unsigned char *input,size_t bytes){
    PeBattleNumeric *ctx=context;BattleDecoded d;if(!decode(ctx,input,bytes,&d))return 0;
    restore_player(player(),d.player);
    for(unsigned i=0;i<7;i++){
        EnemyCombatant *e=&g_BattleEnemySlots[i].combatant;restore_enemy(e,d.enemy[i]);
        for(unsigned j=0;j<6;j++)restore_event(&e->records[j],d.events[i][j]);
        e->effect=d.effect[i]?&e->records[d.effect[i]-1]:0;
    }
    restore_action(&g_BattleMenuActionRecord,d.menuAction);
    for(unsigned i=0;i<6;i++)g_SavedBattleStateTail.words[i]=(int32_t)d.tail[i];
    g_BattleEquipStateBlock.parameterWord.raw=d.equip[0];g_BattleEquipStateBlock.effectFlags=d.equip[1];
    restore_globals(d.globals);
    for(unsigned i=0;i<45;i++){
        g_BattleActionQueue[i].actor=d.queueActor[i]?(BattleEntity *)(void *)&g_EntityPool[d.queueActor[i]-1]:0;
        g_BattleActionQueue[i].command=(short)d.queueCommand[i];g_BattleActionQueue[i].actionGeneration=(short)d.queueGeneration[i];
    }
    g_ActiveActor=d.activeCore==0?0:d.activeCore==1?player():(Combatant *)(void *)&g_BattleEnemySlots[d.activeCore-2].combatant;
    return 1;
}
int PeState_RegisterBattleNumeric(PeStateRegistry *registry,PeBattleNumeric *context){PeStateModule module={PE_STATE_BATTLE,2,BATTLE_BYTES_MAX,capture,context,validate,restore,0};return PeState_Register(registry,module);}
