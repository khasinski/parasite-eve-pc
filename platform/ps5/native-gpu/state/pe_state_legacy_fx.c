#include "pe1/point_sprite_owner_data.h"
/* SPDX-License-Identifier: MIT. Typed original FieldFxObject VM, no raw object/heap image. */
#include "pe_state_legacy_fx.h"
#include "pe_state_boundary.h"
#include "pe_state_assets.h"
#include "pe1/pm.h"
#include "pe1/entity_allocation.h"
#include "pe1/battle_update.h"
#include "pe1/field_effect_modules.h"
#include "pe1/fieldfx_particle.h"
#include "pe1/player_entity.h"
#include "pe1/fieldfx_task.h"
#include "pe_state_bank_sources.h"
#include "pe1/battle.h"
#include "pe1/battle_saved_state.h"
#include "pe1/field_glow_sprite.h"
#include "psyz/state_audio.h"
#include "psyz/state_producers.h"
#include <stdint.h>
#include <string.h>
#include <limits.h>
extern int g_GlowEffectScript[],g_GlowEffectBScript[],g_GlowEffectCScript[],g_FourLayerGlowScript[],g_BurstEffectBScript[],g_BurstEffectScript[],g_BurstEffectCScript[],g_PointSpriteScript[];
extern FieldFxHandler g_BurstEffectCInitList[3],g_BurstEffectCDrawList[3],g_BurstEffectCUpdateList[3];
extern u16 g_BurstEffectCSlotSizes[3];
int BurstEffectC_InitTarget(void*,void*,void*);int BurstEffectC_InitTimedSpark(void*,void*,void*);
int BurstEffectC_DrawNop(void*,void*,void*);int BurstEffectC_DrawTimedSpark(void*,void*,void*);
int BurstEffectC_UpdateEnd(void*,void*,void*);int BurstEffectC_UpdateTimedSpark(void*,void*,void*);
static uint32_t rd(const unsigned char*p){return(uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void wr(unsigned char*p,uint32_t v){for(unsigned j=0;j<4;j++)p[j]=(unsigned char)(v>>(j*8));}
static int zero(const unsigned char*p,size_t n){for(size_t j=0;j<n;j++)if(p[j])return 0;return 1;}
static unsigned actor_id(const void*p){if(!p)return 0;for(unsigned j=0;j<14;j++)if(p==&g_EntityPool[j])return j+1;return UINT32_MAX;}
static int mask_for(const PeWorldState*w,int apply,uint32_t*mask){if(w&&w->targetMode&&!apply){uint32_t types[14];for(unsigned j=0;j<14;j++)types[j]=w->actor[j].type;if(w->targetMode!=1||!PeWorld_ValidateTargetConstruction(w,types,w->liveMask))return 0;*mask=w->liveMask;return 1;}*mask=0;unsigned count=0;for(FieldActor*p=g_FieldActorListHead;p;p=p->next){unsigned id=actor_id(p);if(!id||id>14||count++>=14||(*mask&(1u<<(id-1))))return 0;*mask|=1u<<(id-1);}return !w||!w->targetMode||*mask==w->liveMask;}
static int owner_role(const PeWorldState*w,unsigned id,int apply){uint32_t mask;if(!id||id>14||!mask_for(w,apply,&mask)||!(mask&(1u<<(id-1))))return 0;if(w&&w->targetMode&&!apply)return w->actor[id-1].state&&w->actor[id-1].coreRole>0&&w->actor[id-1].coreRole<=8;FieldActor*a=&g_EntityPool[id-1];uint32_t core,frame,contact;if(!a->state||!PeWorld_NativeActorRoles(a,&core,&frame,&contact)||!core)return 0;return !w||!w->targetMode||(a->type_id==w->actor[id-1].type&&a->sub_id==w->actor[id-1].sub&&a->model_state.model_header==w->actor[id-1].model&&a->render_object.header==w->actor[id-1].header&&a->state==w->actor[id-1].state&&core==w->actor[id-1].coreRole);}
typedef struct LegacyDesc{unsigned command,words,maxId,unit,limit,idle;int*script;u16*sizes;}LegacyDesc;
extern FieldFxHandler g_BurstEffectBInitList[4];
int BurstEffectB_InitTarget(void*,void*,void*);
int BurstEffectB_InitSparkCloud(void*,void*,void*);
int BurstEffectB_InitSparkPoint(void*,void*,void*);
extern FieldFxHandler g_BurstEffectBDrawList[4];
int BurstEffectB_DrawNop(void*,void*,void*);
int BurstEffectB_DrawSparkCloud(void*,void*,void*);
int BurstEffectB_DrawSparkPoint(void*,void*,void*);
extern FieldFxHandler g_BurstEffectBUpdateList[4];
int BurstEffectB_UpdateEnd(void*,void*,void*);
int BurstEffectB_UpdateSparkCloud(void*,void*,void*);
int BurstEffectB_UpdateSparkPoint(void*,void*,void*);
extern u16 g_BurstEffectBSlotSizes[];
extern FieldFxHandler g_BurstEffectInitList[6];
int BurstEffect_InitTarget(void*,void*,void*);
int BurstEffect_InitGroundGlow(void*,void*,void*);
int FieldFx_InitRandomOffsetPairs(void*,void*,void*);
int BurstEffect_InitSparkPoint(void*,void*,void*);
int BurstEffect_InitNop4(void*,void*,void*);
extern FieldFxHandler g_BurstEffectDrawList[6];
int BurstEffect_DrawNop0(void*,void*,void*);
int BurstEffect_DrawGroundGlow(void*,void*,void*);
int BurstEffect_DrawSparkPairs(void*,void*,void*);
int BurstEffect_DrawSparkPoint(void*,void*,void*);
int BurstEffect_DrawNop4(void*,void*,void*);
extern FieldFxHandler g_BurstEffectUpdateList[6];
int BurstEffect_UpdateEnd(void*,void*,void*);
int BurstEffect_UpdateGroundGlow(void*,void*,void*);
int BurstEffect_UpdateSparkPairs(void*,void*,void*);
int BurstEffect_UpdateSparkPoint(void*,void*,void*);
int BurstEffect_UpdateEnd4(void*,void*,void*);
extern u16 g_BurstEffectSlotSizes[];
extern FieldFxHandler g_BurstEffectCInitList[3];
int BurstEffectC_InitTarget(void*,void*,void*);
int BurstEffectC_InitTimedSpark(void*,void*,void*);
extern FieldFxHandler g_BurstEffectCDrawList[3];
int BurstEffectC_DrawNop(void*,void*,void*);
int BurstEffectC_DrawTimedSpark(void*,void*,void*);
extern FieldFxHandler g_BurstEffectCUpdateList[3];
int BurstEffectC_UpdateEnd(void*,void*,void*);
int BurstEffectC_UpdateTimedSpark(void*,void*,void*);
extern u16 g_BurstEffectCSlotSizes[];
extern FieldFxHandler g_GlowEffectInitList[7];
int GlowEffect_InitOwner(void*,void*,void*);
int GlowEffect_InitParticle(void*,void*,void*);
int GlowEffect_InitFlareLayers(void*,void*,void*);
int GlowEffect_InitCoreGlow(void*,void*,void*);
int GlowEffect_InitGroundGlow(void*,void*,void*);
int GlowEffect_InitHaloGlow(void*,void*,void*);
extern FieldFxHandler g_GlowEffectDrawList[7];
int GlowEffect_DrawNop(void*,void*,void*);
int GlowEffect_DrawParticle(void*,void*,void*);
int GlowEffect_DrawFlareLayers(void*,void*,void*);
int GlowEffect_DrawPointGlow(void*,void*,void*);
int GlowEffect_DrawGroundGlow(void*,void*,void*);
int GlowEffect_DrawHaloGlow(void*,void*,void*);
extern FieldFxHandler g_GlowEffectUpdateList[7];
int GlowEffect_UpdateEnd(void*,void*,void*);
int GlowEffect_UpdateParticle(void*,void*,void*);
int GlowEffect_UpdateFlareLayers(void*,void*,void*);
int GlowEffect_UpdateCoreGlow(void*,void*,void*);
int GlowEffect_UpdateGroundGlow(void*,void*,void*);
int GlowEffect_UpdateHaloGlow(void*,void*,void*);
extern u16 g_GlowEffectSlotSizes[];
extern FieldFxHandler g_GlowEffectBInitList[5];
int GlowEffectB_InitOwner(void*,void*,void*);
int GlowEffectB_InitParticle(void*,void*,void*);
int GlowEffectB_InitFlareLayers(void*,void*,void*);
int GlowEffectB_InitPointGlow(void*,void*,void*);
extern FieldFxHandler g_GlowEffectBDrawList[5];
int GlowEffectB_DrawNop(void*,void*,void*);
int GlowEffectB_DrawParticle(void*,void*,void*);
int GlowEffectB_DrawFlareLayers(void*,void*,void*);
int GlowEffectB_DrawPointGlow(void*,void*,void*);
extern FieldFxHandler g_GlowEffectBUpdateList[5];
int GlowEffectB_UpdateEnd(void*,void*,void*);
int GlowEffectB_UpdateParticle(void*,void*,void*);
int GlowEffectB_UpdateFlareLayers(void*,void*,void*);
int GlowEffectB_UpdatePointGlow(void*,void*,void*);
extern u16 g_GlowEffectBSlotSizes[];
extern FieldFxHandler g_GlowEffectCInitList[4];
int GlowEffectC_InitOwner(void*,void*,void*);
int GlowEffectC_InitParticle(void*,void*,void*);
int GlowEffectC_InitFlareLayers(void*,void*,void*);
extern FieldFxHandler g_GlowEffectCDrawList[4];
int GlowEffectC_DrawNop(void*,void*,void*);
int GlowEffectC_DrawParticle(void*,void*,void*);
int GlowEffectC_DrawFlareLayers(void*,void*,void*);
extern FieldFxHandler g_GlowEffectCUpdateList[4];
int GlowEffectC_UpdateEnd(void*,void*,void*);
int GlowEffectC_UpdateParticle(void*,void*,void*);
int GlowEffectC_UpdateFlareLayers(void*,void*,void*);
extern u16 g_GlowEffectCSlotSizes[];
extern FieldFxHandler g_FourLayerGlowInitList[7];
int FourLayerGlow_InitOwner(void*,void*,void*);
int FourLayerGlow_InitParticle(void*,void*,void*);
int FourLayerGlow_InitFlareLayers(void*,void*,void*);
int FourLayerGlow_InitTwinGlow(void*,void*,void*);
int FourLayerGlow_InitGroundGlow(void*,void*,void*);
int FourLayerGlow_InitHaloGlow(void*,void*,void*);
extern FieldFxHandler g_FourLayerGlowDrawList[7];
int FourLayerGlow_DrawNop(void*,void*,void*);
int FourLayerGlow_DrawParticle(void*,void*,void*);
int FourLayerGlow_DrawFlareLayers(void*,void*,void*);
int FourLayerGlow_DrawTwinGlow(void*,void*,void*);
int FourLayerGlow_DrawGroundGlow(void*,void*,void*);
int FourLayerGlow_DrawHaloGlow(void*,void*,void*);
extern FieldFxHandler g_FourLayerGlowUpdateList[7];
int FourLayerGlow_UpdateEnd(void*,void*,void*);
int FourLayerGlow_UpdateParticle(void*,void*,void*);
int FourLayerGlow_UpdateFlareLayers(void*,void*,void*);
int FourLayerGlow_UpdateTwinGlow(void*,void*,void*);
int FourLayerGlow_UpdateGroundGlow(void*,void*,void*);
int FourLayerGlow_UpdateHaloGlow(void*,void*,void*);
extern u16 g_FourLayerGlowSlotSizes[];
extern FieldFxHandler g_PointSpriteInitList[7];
int PointSprite_InitRisingStreak(void*,void*,void*);
int FieldFx_InitRadialEntries(void*,void*,void*);
int PointSprite_InitGroundRing(void*,void*,void*);
int PointSprite_InitBillboard(void*,void*,void*);
int FieldFx_InitScatteredParticles(void*,void*,void*);
int PointSprite_InitOrientedFlash(void*,void*,void*);
extern FieldFxHandler g_PointSpriteDrawList[7];
int PointSprite_DrawRisingStreak(void*,void*,void*);
int FieldFx_ProcessInterleavedPoints(void*,void*,void*);
int PointSprite_DrawGroundRing(void*,void*,void*);
int PointSprite_DrawBillboardFlash(void*,void*,void*);
int FieldFx_ProcessPointTriples(void*,void*,void*);
int PointSprite_DrawOrientedFlash(void*,void*,void*);
extern FieldFxHandler g_PointSpriteUpdateList[7];
int PointSprite_UpdateRisingStreak(void*,void*,void*);
int PointSprite_UpdateRadialParticles(void*,void*,void*);
int PointSprite_UpdateGroundRing(void*,void*,void*);
int PointSprite_UpdateBillboardFlash(void*,void*,void*);
int PointSprite_UpdateScatteredParticles(void*,void*,void*);
int PointSprite_UpdateOrientedFlash(void*,void*,void*);
extern u16 g_PointSpriteSlotSizes[];
static int descriptor(unsigned command,LegacyDesc*out){LegacyDesc d;memset(&d,0,sizeof d);if(!g_PmCmdHandlerTable)return 0;switch(command){
case 0:{static const int exact[25]={1703936,1769472,1835008,5177344,521338882,520552449,131072,721092608,2818049,737738762,1703937,1769472,821428224,2031616,6225920,65536,65537,65538,65539,436338688,65540,65541,1703936,820707328,-1};if(g_PmCmdHandlerTable[0]!=&g_GlowEffectClass||memcmp(exact,g_GlowEffectScript,sizeof exact))return 0;
if(g_GlowEffectInitList[0]!=GlowEffect_InitOwner)return 0;
if(g_GlowEffectInitList[1]!=GlowEffect_InitParticle)return 0;
if(g_GlowEffectInitList[2]!=GlowEffect_InitFlareLayers)return 0;
if(g_GlowEffectInitList[3]!=GlowEffect_InitCoreGlow)return 0;
if(g_GlowEffectInitList[4]!=GlowEffect_InitGroundGlow)return 0;
if(g_GlowEffectInitList[5]!=GlowEffect_InitHaloGlow)return 0;
if(g_GlowEffectInitList[6]!=(FieldFxHandler)(intptr_t)-1)return 0;
if(g_GlowEffectDrawList[0]!=GlowEffect_DrawNop)return 0;
if(g_GlowEffectDrawList[1]!=GlowEffect_DrawParticle)return 0;
if(g_GlowEffectDrawList[2]!=GlowEffect_DrawFlareLayers)return 0;
if(g_GlowEffectDrawList[3]!=GlowEffect_DrawPointGlow)return 0;
if(g_GlowEffectDrawList[4]!=GlowEffect_DrawGroundGlow)return 0;
if(g_GlowEffectDrawList[5]!=GlowEffect_DrawHaloGlow)return 0;
if(g_GlowEffectDrawList[6]!=(FieldFxHandler)(intptr_t)-1)return 0;
if(g_GlowEffectUpdateList[0]!=GlowEffect_UpdateEnd)return 0;
if(g_GlowEffectUpdateList[1]!=GlowEffect_UpdateParticle)return 0;
if(g_GlowEffectUpdateList[2]!=GlowEffect_UpdateFlareLayers)return 0;
if(g_GlowEffectUpdateList[3]!=GlowEffect_UpdateCoreGlow)return 0;
if(g_GlowEffectUpdateList[4]!=GlowEffect_UpdateGroundGlow)return 0;
if(g_GlowEffectUpdateList[5]!=GlowEffect_UpdateHaloGlow)return 0;
if(g_GlowEffectUpdateList[6]!=(FieldFxHandler)(intptr_t)-1)return 0;
if(g_GlowEffectSlotSizes[0]!=0)return 0;
if(g_GlowEffectSlotSizes[1]!=24)return 0;
if(g_GlowEffectSlotSizes[2]!=48)return 0;
if(g_GlowEffectSlotSizes[3]!=16)return 0;
if(g_GlowEffectSlotSizes[4]!=16)return 0;
if(g_GlowEffectSlotSizes[5]!=48)return 0;
if(g_GlowEffectSlotSizes[6]!=65535)return 0;
d=(LegacyDesc){0,25,5,8,2056,7,g_GlowEffectScript,g_GlowEffectSlotSizes};break;}
case 1:{static const int exact[13]={5177344,520749058,520224769,131072,821755904,2031616,6225920,65536,65537,65538,65539,821297152,-1};if(g_PmCmdHandlerTable[1]!=&g_GlowEffectBClass||memcmp(exact,g_GlowEffectBScript,sizeof exact))return 0;
if(g_GlowEffectBInitList[0]!=GlowEffectB_InitOwner)return 0;
if(g_GlowEffectBInitList[1]!=GlowEffectB_InitParticle)return 0;
if(g_GlowEffectBInitList[2]!=GlowEffectB_InitFlareLayers)return 0;
if(g_GlowEffectBInitList[3]!=GlowEffectB_InitPointGlow)return 0;
if(g_GlowEffectBInitList[4]!=(FieldFxHandler)(intptr_t)-1)return 0;
if(g_GlowEffectBDrawList[0]!=GlowEffectB_DrawNop)return 0;
if(g_GlowEffectBDrawList[1]!=GlowEffectB_DrawParticle)return 0;
if(g_GlowEffectBDrawList[2]!=GlowEffectB_DrawFlareLayers)return 0;
if(g_GlowEffectBDrawList[3]!=GlowEffectB_DrawPointGlow)return 0;
if(g_GlowEffectBDrawList[4]!=(FieldFxHandler)(intptr_t)-1)return 0;
if(g_GlowEffectBUpdateList[0]!=GlowEffectB_UpdateEnd)return 0;
if(g_GlowEffectBUpdateList[1]!=GlowEffectB_UpdateParticle)return 0;
if(g_GlowEffectBUpdateList[2]!=GlowEffectB_UpdateFlareLayers)return 0;
if(g_GlowEffectBUpdateList[3]!=GlowEffectB_UpdatePointGlow)return 0;
if(g_GlowEffectBUpdateList[4]!=(FieldFxHandler)(intptr_t)-1)return 0;
if(g_GlowEffectBSlotSizes[0]!=0)return 0;
if(g_GlowEffectBSlotSizes[1]!=24)return 0;
if(g_GlowEffectBSlotSizes[2]!=48)return 0;
if(g_GlowEffectBSlotSizes[3]!=16)return 0;
if(g_GlowEffectBSlotSizes[4]!=65535)return 0;
d=(LegacyDesc){1,13,3,8,2056,4,g_GlowEffectBScript,g_GlowEffectBSlotSizes};break;}
case 2:{static const int exact[12]={5177344,520683522,520224769,131072,821755904,2031616,6225920,65536,65537,65538,821362688,-1};if(g_PmCmdHandlerTable[2]!=&g_GlowEffectCClass||memcmp(exact,g_GlowEffectCScript,sizeof exact))return 0;
if(g_GlowEffectCInitList[0]!=GlowEffectC_InitOwner)return 0;
if(g_GlowEffectCInitList[1]!=GlowEffectC_InitParticle)return 0;
if(g_GlowEffectCInitList[2]!=GlowEffectC_InitFlareLayers)return 0;
if(g_GlowEffectCInitList[3]!=(FieldFxHandler)(intptr_t)-1)return 0;
if(g_GlowEffectCDrawList[0]!=GlowEffectC_DrawNop)return 0;
if(g_GlowEffectCDrawList[1]!=GlowEffectC_DrawParticle)return 0;
if(g_GlowEffectCDrawList[2]!=GlowEffectC_DrawFlareLayers)return 0;
if(g_GlowEffectCDrawList[3]!=(FieldFxHandler)(intptr_t)-1)return 0;
if(g_GlowEffectCUpdateList[0]!=GlowEffectC_UpdateEnd)return 0;
if(g_GlowEffectCUpdateList[1]!=GlowEffectC_UpdateParticle)return 0;
if(g_GlowEffectCUpdateList[2]!=GlowEffectC_UpdateFlareLayers)return 0;
if(g_GlowEffectCUpdateList[3]!=(FieldFxHandler)(intptr_t)-1)return 0;
if(g_GlowEffectCSlotSizes[0]!=0)return 0;
if(g_GlowEffectCSlotSizes[1]!=24)return 0;
if(g_GlowEffectCSlotSizes[2]!=48)return 0;
if(g_GlowEffectCSlotSizes[3]!=65535)return 0;
d=(LegacyDesc){2,12,2,8,2056,4,g_GlowEffectCScript,g_GlowEffectCSlotSizes};break;}
case 3:{static const int exact[25]={1703936,1769472,1835008,5177344,521338882,520552449,131072,721092608,2818049,737738762,1703937,1769472,821428224,2031616,6225920,65536,65537,65538,65539,436338688,65540,65541,1703936,820707328,-1};if(g_PmCmdHandlerTable[3]!=&g_FourLayerGlowClass||memcmp(exact,g_FourLayerGlowScript,sizeof exact))return 0;
if(g_FourLayerGlowInitList[0]!=FourLayerGlow_InitOwner)return 0;
if(g_FourLayerGlowInitList[1]!=FourLayerGlow_InitParticle)return 0;
if(g_FourLayerGlowInitList[2]!=FourLayerGlow_InitFlareLayers)return 0;
if(g_FourLayerGlowInitList[3]!=FourLayerGlow_InitTwinGlow)return 0;
if(g_FourLayerGlowInitList[4]!=FourLayerGlow_InitGroundGlow)return 0;
if(g_FourLayerGlowInitList[5]!=FourLayerGlow_InitHaloGlow)return 0;
if(g_FourLayerGlowInitList[6]!=(FieldFxHandler)(intptr_t)-1)return 0;
if(g_FourLayerGlowDrawList[0]!=FourLayerGlow_DrawNop)return 0;
if(g_FourLayerGlowDrawList[1]!=FourLayerGlow_DrawParticle)return 0;
if(g_FourLayerGlowDrawList[2]!=FourLayerGlow_DrawFlareLayers)return 0;
if(g_FourLayerGlowDrawList[3]!=FourLayerGlow_DrawTwinGlow)return 0;
if(g_FourLayerGlowDrawList[4]!=FourLayerGlow_DrawGroundGlow)return 0;
if(g_FourLayerGlowDrawList[5]!=FourLayerGlow_DrawHaloGlow)return 0;
if(g_FourLayerGlowDrawList[6]!=(FieldFxHandler)(intptr_t)-1)return 0;
if(g_FourLayerGlowUpdateList[0]!=FourLayerGlow_UpdateEnd)return 0;
if(g_FourLayerGlowUpdateList[1]!=FourLayerGlow_UpdateParticle)return 0;
if(g_FourLayerGlowUpdateList[2]!=FourLayerGlow_UpdateFlareLayers)return 0;
if(g_FourLayerGlowUpdateList[3]!=FourLayerGlow_UpdateTwinGlow)return 0;
if(g_FourLayerGlowUpdateList[4]!=FourLayerGlow_UpdateGroundGlow)return 0;
if(g_FourLayerGlowUpdateList[5]!=FourLayerGlow_UpdateHaloGlow)return 0;
if(g_FourLayerGlowUpdateList[6]!=(FieldFxHandler)(intptr_t)-1)return 0;
if(g_FourLayerGlowSlotSizes[0]!=0)return 0;
if(g_FourLayerGlowSlotSizes[1]!=24)return 0;
if(g_FourLayerGlowSlotSizes[2]!=48)return 0;
if(g_FourLayerGlowSlotSizes[3]!=24)return 0;
if(g_FourLayerGlowSlotSizes[4]!=16)return 0;
if(g_FourLayerGlowSlotSizes[5]!=48)return 0;
if(g_FourLayerGlowSlotSizes[6]!=65535)return 0;
d=(LegacyDesc){3,25,5,8,2056,7,g_FourLayerGlowScript,g_FourLayerGlowSlotSizes};break;}
case 7:{static const int exact[54]=PE_POINT_SPRITE_SCRIPT_INITIALIZER;if(g_PmCmdHandlerTable[7]!=&g_PointSpriteClass||memcmp(exact,g_PointSpriteScript,sizeof exact))return 0;
if(g_PointSpriteInitList[0]!=PointSprite_InitRisingStreak)return 0;
if(g_PointSpriteInitList[1]!=FieldFx_InitRadialEntries)return 0;
if(g_PointSpriteInitList[2]!=PointSprite_InitGroundRing)return 0;
if(g_PointSpriteInitList[3]!=PointSprite_InitBillboard)return 0;
if(g_PointSpriteInitList[4]!=FieldFx_InitScatteredParticles)return 0;
if(g_PointSpriteInitList[5]!=PointSprite_InitOrientedFlash)return 0;
if(g_PointSpriteInitList[6]!=(FieldFxHandler)(intptr_t)-1)return 0;
if(g_PointSpriteDrawList[0]!=PointSprite_DrawRisingStreak)return 0;
if(g_PointSpriteDrawList[1]!=FieldFx_ProcessInterleavedPoints)return 0;
if(g_PointSpriteDrawList[2]!=PointSprite_DrawGroundRing)return 0;
if(g_PointSpriteDrawList[3]!=PointSprite_DrawBillboardFlash)return 0;
if(g_PointSpriteDrawList[4]!=FieldFx_ProcessPointTriples)return 0;
if(g_PointSpriteDrawList[5]!=PointSprite_DrawOrientedFlash)return 0;
if(g_PointSpriteDrawList[6]!=(FieldFxHandler)(intptr_t)-1)return 0;
if(g_PointSpriteUpdateList[0]!=PointSprite_UpdateRisingStreak)return 0;
if(g_PointSpriteUpdateList[1]!=PointSprite_UpdateRadialParticles)return 0;
if(g_PointSpriteUpdateList[2]!=PointSprite_UpdateGroundRing)return 0;
if(g_PointSpriteUpdateList[3]!=PointSprite_UpdateBillboardFlash)return 0;
if(g_PointSpriteUpdateList[4]!=PointSprite_UpdateScatteredParticles)return 0;
if(g_PointSpriteUpdateList[5]!=PointSprite_UpdateOrientedFlash)return 0;
if(g_PointSpriteUpdateList[6]!=(FieldFxHandler)(intptr_t)-1)return 0;
if(g_PointSpriteSlotSizes[0]!=12)return 0;
if(g_PointSpriteSlotSizes[1]!=294)return 0;
if(g_PointSpriteSlotSizes[2]!=12)return 0;
if(g_PointSpriteSlotSizes[3]!=12)return 0;
if(g_PointSpriteSlotSizes[4]!=200)return 0;
if(g_PointSpriteSlotSizes[5]!=12)return 0;
if(g_PointSpriteSlotSizes[6]!=65535)return 0;
d=(LegacyDesc){7,54,5,2,2058,6,g_PointSpriteScript,g_PointSpriteSlotSizes};break;}
case 4:{static const int exact[18]={5177344,520945667,521011202,520224769,131072,821690368,2031616,6225920,1966080,1900544,65536,65537,65538,519831552,821100544,-3,820969472,-1};if(g_PmCmdHandlerTable[4]!=&g_BurstEffectBClass||memcmp(exact,g_BurstEffectBScript,sizeof exact))return 0;
if(g_BurstEffectBInitList[0]!=BurstEffectB_InitTarget)return 0;
if(g_BurstEffectBInitList[1]!=BurstEffectB_InitSparkCloud)return 0;
if(g_BurstEffectBInitList[2]!=BurstEffectB_InitSparkPoint)return 0;
if(g_BurstEffectBInitList[3]!=(FieldFxHandler)(intptr_t)-1)return 0;
if(g_BurstEffectBDrawList[0]!=BurstEffectB_DrawNop)return 0;
if(g_BurstEffectBDrawList[1]!=BurstEffectB_DrawSparkCloud)return 0;
if(g_BurstEffectBDrawList[2]!=BurstEffectB_DrawSparkPoint)return 0;
if(g_BurstEffectBDrawList[3]!=(FieldFxHandler)(intptr_t)-1)return 0;
if(g_BurstEffectBUpdateList[0]!=BurstEffectB_UpdateEnd)return 0;
if(g_BurstEffectBUpdateList[1]!=BurstEffectB_UpdateSparkCloud)return 0;
if(g_BurstEffectBUpdateList[2]!=BurstEffectB_UpdateSparkPoint)return 0;
if(g_BurstEffectBUpdateList[3]!=(FieldFxHandler)(intptr_t)-1)return 0;
if(g_BurstEffectBSlotSizes[0]!=0)return 0;
if(g_BurstEffectBSlotSizes[1]!=112)return 0;
if(g_BurstEffectBSlotSizes[2]!=16)return 0;
if(g_BurstEffectBSlotSizes[3]!=65535)return 0;
d=(LegacyDesc){4,18,2,16,2048,5,g_BurstEffectBScript,g_BurstEffectBSlotSizes};break;}
case 5:{static const int exact[24]={1835008,5177344,521273347,521338882,520224769,131072,821690368,2031616,6225920,1966080,1900544,65536,738328576,65537,65539,65538,2883585,738263044,1835008,519503872,820772864,-3,820641792,-1};if(g_PmCmdHandlerTable[5]!=&g_BurstEffectClass||memcmp(exact,g_BurstEffectScript,sizeof exact))return 0;
if(g_BurstEffectInitList[0]!=BurstEffect_InitTarget)return 0;
if(g_BurstEffectInitList[1]!=BurstEffect_InitGroundGlow)return 0;
if(g_BurstEffectInitList[2]!=FieldFx_InitRandomOffsetPairs)return 0;
if(g_BurstEffectInitList[3]!=BurstEffect_InitSparkPoint)return 0;
if(g_BurstEffectInitList[4]!=BurstEffect_InitNop4)return 0;
if(g_BurstEffectInitList[5]!=(FieldFxHandler)(intptr_t)-1)return 0;
if(g_BurstEffectDrawList[0]!=BurstEffect_DrawNop0)return 0;
if(g_BurstEffectDrawList[1]!=BurstEffect_DrawGroundGlow)return 0;
if(g_BurstEffectDrawList[2]!=BurstEffect_DrawSparkPairs)return 0;
if(g_BurstEffectDrawList[3]!=BurstEffect_DrawSparkPoint)return 0;
if(g_BurstEffectDrawList[4]!=BurstEffect_DrawNop4)return 0;
if(g_BurstEffectDrawList[5]!=(FieldFxHandler)(intptr_t)-1)return 0;
if(g_BurstEffectUpdateList[0]!=BurstEffect_UpdateEnd)return 0;
if(g_BurstEffectUpdateList[1]!=BurstEffect_UpdateGroundGlow)return 0;
if(g_BurstEffectUpdateList[2]!=BurstEffect_UpdateSparkPairs)return 0;
if(g_BurstEffectUpdateList[3]!=BurstEffect_UpdateSparkPoint)return 0;
if(g_BurstEffectUpdateList[4]!=BurstEffect_UpdateEnd4)return 0;
if(g_BurstEffectUpdateList[5]!=(FieldFxHandler)(intptr_t)-1)return 0;
if(g_BurstEffectSlotSizes[0]!=0)return 0;
if(g_BurstEffectSlotSizes[1]!=16)return 0;
if(g_BurstEffectSlotSizes[2]!=48)return 0;
if(g_BurstEffectSlotSizes[3]!=16)return 0;
if(g_BurstEffectSlotSizes[4]!=65535)return 0;
d=(LegacyDesc){5,24,3,16,2048,6,g_BurstEffectScript,g_BurstEffectSlotSizes};break;}
case 6:{static const int exact[18]={5177344,521076738,520224769,131072,821755904,2031616,6225920,1966080,1900544,65536,65537,131074,65537,131074,65537,519634944,820969472,-1};if(g_PmCmdHandlerTable[6]!=&g_BurstEffectCClass||memcmp(exact,g_BurstEffectCScript,sizeof exact))return 0;
if(g_BurstEffectCInitList[0]!=BurstEffectC_InitTarget)return 0;
if(g_BurstEffectCInitList[1]!=BurstEffectC_InitTimedSpark)return 0;
if(g_BurstEffectCInitList[2]!=(FieldFxHandler)(intptr_t)-1)return 0;
if(g_BurstEffectCDrawList[0]!=BurstEffectC_DrawNop)return 0;
if(g_BurstEffectCDrawList[1]!=BurstEffectC_DrawTimedSpark)return 0;
if(g_BurstEffectCDrawList[2]!=(FieldFxHandler)(intptr_t)-1)return 0;
if(g_BurstEffectCUpdateList[0]!=BurstEffectC_UpdateEnd)return 0;
if(g_BurstEffectCUpdateList[1]!=BurstEffectC_UpdateTimedSpark)return 0;
if(g_BurstEffectCUpdateList[2]!=(FieldFxHandler)(intptr_t)-1)return 0;
if(g_BurstEffectCSlotSizes[0]!=0)return 0;
if(g_BurstEffectCSlotSizes[1]!=12)return 0;
if(g_BurstEffectCSlotSizes[2]!=65535)return 0;
d=(LegacyDesc){6,18,1,12,2052,4,g_BurstEffectCScript,g_BurstEffectCSlotSizes};break;}
default:return 0;}if(out)*out=d;return 1;}
int PeStateLegacy_HeaderSupported(unsigned i,unsigned status,unsigned command){return i<11&&status>=1&&status<=5&&command<=7&&descriptor(command,0);}
static int player_role(const PeWorldState*w,int apply){uint32_t mask;if(!mask_for(w,apply,&mask))return 0;unsigned id=w&&w->targetMode?w->constructorPlayerId:actor_id(g_PlayerEntity);if(!id||id>14||!(mask&(1u<<(id-1))))return 0;if(w&&w->targetMode&&apply){FieldActor*a=&g_EntityPool[id-1];const PeWorldBinding*b=&w->actor[id-1];if(g_PlayerEntity!=a||a->type_id!=b->type||a->sub_id!=b->sub||a->model_state.model_header!=b->model||a->render_object.header!=b->header||a->state!=b->state)return 0;}return 1;}
/* One source-declared field, never packed heap/padding. kind0u8/1u16/2s16. */
static int field(const unsigned char*in,unsigned char*out,size_t*at,void*data,unsigned offset,unsigned kind,int apply){if(*at>8192-4)return 0;uint32_t v=0;if(out){if(kind==0)v=((unsigned char*)data)[offset];else if(kind==1){u16 x;memcpy(&x,(char*)data+offset,2);v=x;}else{s16 x;memcpy(&x,(char*)data+offset,2);v=(uint32_t)(int32_t)x;}wr(out+*at,v);}else{v=rd(in+*at);if((kind==0&&v>255)||(kind==1&&v>65535)||(kind==2&&((int32_t)v<INT16_MIN||(int32_t)v>INT16_MAX)))return 0;if(apply){if(kind==0)((unsigned char*)data)[offset]=(u8)v;else if(kind==1){u16 x=(u16)v;memcpy((char*)data+offset,&x,2);}else{s16 x=(s16)v;memcpy((char*)data+offset,&x,2);}}}*at+=4;return 1;}
static int payload(unsigned command,unsigned id,void*data,const unsigned char*in,unsigned char*out,size_t*at,int apply){if(!id)return 1;unsigned bytes=command==4?(id==1?112:16):command==5?(id==2?48:16):12;if(apply)memset(data,0,bytes);size_t first=*at;
 if(command==6){if(!field(in,out,at,data,1,0,apply)||!field(in,out,at,data,3,0,apply))return 0;uint32_t age=out?((FieldFxTimedSpark*)data)->age:rd(in+first+4);if(age>6)return 0;for(unsigned j=0;j<3;j++)if(!field(in,out,at,data,6+j*2,1,apply))return 0;return 1;}
 if(command==4){if(!field(in,out,at,data,1,0,apply)||!field(in,out,at,data,3,0,apply))return 0;for(unsigned j=0;j<6;j++)if(!field(in,out,at,data,4+j*2,1,apply))return 0;if(id==1)for(unsigned j=0;j<48;j++)if(!field(in,out,at,data,16+j*2,1,apply))return 0;return 1;}
 if(id==2){if(!field(in,out,at,data,3,0,apply))return 0;uint32_t age=out?((FieldFxTwoPointMotion*)data)->age:rd(in+first);if(age>8)return 0;}
 if(!field(in,out,at,data,4,1,apply)||!field(in,out,at,data,6,2,apply))return 0;for(unsigned j=0;j<3;j++)if(!field(in,out,at,data,8+j*2,1,apply))return 0;if(id==2)for(unsigned j=0;j<4;j++)for(unsigned k=0;k<3;k++)if(!field(in,out,at,data,16+j*8+k*2,1,apply))return 0;return 1;
}
static int extended_context(FieldFxObject*,const PeWorldState*,const unsigned char*,unsigned char*,int);
int PeStateLegacy_Context(FieldFxObject*object,const PeWorldState*w,const uint32_t targets[7],const unsigned char*in,unsigned char*out,int apply){
 if(!object||!targets||(!in&&!out)||(apply&&out)||!PeStateBoundary_AtCpuPoint())return 0;unsigned command=out?object->prefix.asset_type:rd(in+172);LegacyDesc d;if(!descriptor(command,&d))return 0;if(command<4||command==7)return extended_context(object,w,in,out,apply);
 if(apply&&!PeStateLegacy_Context(object,w,targets,in,0,0))return 0;
 if(out){memset(out,0,8192);memcpy(out,"PEFXVM04",8);}else if(memcmp(in,"PEFXVM04",8))return 0;
 uint32_t owner=out?actor_id(object->prefix.actor):rd(in+8),wait=out?(uint32_t)(int32_t)object->state.wait:rd(in+12),pc=out?(uint32_t)(int32_t)object->state.pc:rd(in+16),next=out?(uint32_t)(int32_t)object->state.data_next:rd(in+20),count=out?(uint32_t)(int32_t)object->state.slot_count:rd(in+24),halted=out?(uint32_t)(int32_t)object->state.halted:rd(in+28);
 unsigned pcAllowed=pc==0||pc==d.idle||pc==d.words-1||(command==6&&(pc==12||pc==14));if(!owner_role(w,owner,apply)||!pcAllowed||wait>(command==6?2u:0u)||next>d.limit||next%d.unit||count>64||halted>1||((pc==d.words-1)!=(halted==1))||(wait&&pc!=12&&pc!=14))return 0;
 if(command==5&&!player_role(w,apply))return 0;
 if(out&&(object->state.commands!=d.script||object->state.current_context))return 0;
 int32_t regs[16];for(unsigned j=0;j<16;j++){regs[j]=out?object->state.regs[j]:(int32_t)rd(in+32+j*4);if((j<13&&regs[j])||(j==13&&(regs[j]<0||regs[j]>6))||(j==14&&(regs[j]<0||regs[j]>1))||(j==15&&(regs[j]<0||regs[j]>2)))return 0;}
 uint32_t control=out?(uint32_t)object->state.vars[0]:rd(in+100),root=out?(object->state.vars[1]==(intptr_t)g_BattleActionActors?1u:object->state.vars[1]?UINT32_MAX:0u):rd(in+108);if(control>2||root>1||(out&&(intptr_t)control!=object->state.vars[0]))return 0;
 for(unsigned j=0;j<9;j++){uint32_t role=j==1?1:0,value=j==0?control:j==1?root:0;if(out){if(j>1&&object->state.vars[j])return 0;wr(out+96+j*8,role);wr(out+100+j*8,value);}else if(rd(in+96+j*8)!=role||rd(in+100+j*8)!=value)return 0;}
 if(!root&&(count||control==1||(pc!=0&&pc!=d.idle&&pc!=d.words-1)))return 0;
 unsigned length=0;while(length<7&&targets[length])length++;if(length==7)return 0;for(unsigned j=length;j<7;j++)if(targets[j])return 0;uint32_t live;if(!mask_for(w,apply,&live))return 0;
 if(root&&control==1&&(pc==0||pc==d.idle)){if(!length)return 0;for(unsigned j=0;j<length;j++)if(targets[j]>14||!(live&(1u<<(targets[j]-1))))return 0;}
 if(root&&command==6&&(pc==12||pc==14)&&!regs[14]){if((unsigned)regs[13]>=length)return 0;for(unsigned j=(unsigned)regs[13];j<length;j++)if(targets[j]>14||!(live&(1u<<(targets[j]-1))))return 0;}
 if(out){wr(out+8,owner);wr(out+12,wait);wr(out+16,pc);wr(out+20,next);wr(out+24,count);wr(out+28,halted);for(unsigned j=0;j<16;j++)wr(out+32+j*4,(uint32_t)regs[j]);wr(out+168,command+1);wr(out+172,command);}else if(rd(in+168)!=command+1||!zero(in+176,80))return 0;
 unsigned active=0;size_t fields=1280;unsigned char occupied[FIELDFX_DATA_SIZE];memset(occupied,0,sizeof occupied);
 for(unsigned j=0;j<64;j++){FieldFxSlot*s=&object->slots[j];size_t at=256+j*16;uint32_t flag=out?(uint32_t)(int32_t)s->flag:rd(in+at),id=out?s->handler_id:rd(in+at+4),counter=out?s->counter:rd(in+at+8),offset=out?(uint32_t)(int32_t)s->data_offset:rd(in+at+12);if(flag>2)return 0;if(!flag){if(!out&&!zero(in+at,16))return 0;if(apply)memset(s,0,sizeof*s);continue;}active++;if(id>d.maxId||counter>65535||offset>d.limit||offset%d.unit)return 0;unsigned bytes=d.sizes[id];if(bytes>FIELDFX_DATA_SIZE-offset)return 0;for(unsigned k=0;k<bytes;k++){if(occupied[offset+k])return 0;occupied[offset+k]=1;}
  if(out){wr(out+at,flag);wr(out+at+4,id);wr(out+at+8,counter);wr(out+at+12,offset);}if(!payload(command,id,object->data+offset,in,out,&fields,apply))return 0;if(apply){s->flag=(s8)flag;s->handler_id=(u8)id;s->counter=(u16)counter;s->data_offset=(s16)offset;}
 }
 if(active!=count||fields>8192||(!out&&!zero(in+fields,8192-fields)))return 0;
 if(apply){object->prefix.actor=&g_EntityPool[owner-1];object->prefix.asset_type=(u8)command;object->state.wait=(s16)wait;object->state.pc=(s16)pc;object->state.data_next=(s16)next;object->state.slot_count=(s8)count;object->state.halted=(s8)halted;memcpy(object->state.regs,regs,sizeof regs);memset(object->state.vars,0,sizeof object->state.vars);object->state.vars[0]=(intptr_t)control;object->state.vars[1]=root?(intptr_t)g_BattleActionActors:0;object->state.commands=d.script;object->state.current_context=0;}return 1;
}
static unsigned object_role(const void*p,unsigned kind){if(!p)return 0;for(unsigned j=0;j<11;j++){FieldFxObject*o=&g_PmSlotTable[j].effect;if(p==(kind==0?(void*)o:kind==1?(void*)&o->state:kind==2?(void*)o->data:(void*)o->slots))return j+1;}return UINT32_MAX;}
static int*script_root(unsigned id){switch(id){case 1:return g_GlowEffectScript;case 2:return g_GlowEffectBScript;case 3:return g_GlowEffectCScript;case 4:return g_FourLayerGlowScript;case 5:return g_BurstEffectBScript;case 6:return g_BurstEffectScript;case 7:return g_BurstEffectCScript;case 8:return g_PointSpriteScript;default:return 0;}}
static unsigned command_role(const void*p){if(!p)return 0;for(unsigned j=1;j<=8;j++)if(p==script_root(j))return j;return UINT32_MAX;}
static int sprite_walk(FieldFxSpriteState*state,const unsigned char*in,unsigned char*out,int apply){FieldOrientedSprite*s=&state->oriented;uint32_t values[20];unsigned at=0;for(unsigned j=0;j<8;j++){s16 v;memcpy(&v,(char*)s+j*2,2);values[at++]=(uint32_t)(int32_t)v;}for(unsigned j=0;j<4;j++){uint32_t v;memcpy(&v,(char*)s+16+j*4,4);values[at++]=v;}for(unsigned j=0;j<4;j++)values[at++]=s->rgb[j];values[at++]=s->cell;values[at++]=s->clut;values[at++]=(uint32_t)(int32_t)s->depth;values[at++]=s->brightness;
 for(unsigned j=0;j<20;j++){uint32_t v=out?values[j]:rd(in+j*4);if((j<8||j==18)&&((int32_t)v<INT16_MIN||(int32_t)v>INT16_MAX))return 0;if(j>=12&&j<18&&v>255)return 0;if(j==19&&v>65535)return 0;values[j]=v;if(out)wr(out+j*4,v);}
 if(apply){for(unsigned j=0;j<8;j++){s16 v=(s16)values[j];memcpy((char*)s+j*2,&v,2);}for(unsigned j=0;j<4;j++)memcpy((char*)s+16+j*4,&values[8+j],4);for(unsigned j=0;j<4;j++)s->rgb[j]=(u8)values[12+j];s->cell=(u8)values[16];s->clut=(u8)values[17];s->depth=(s16)values[18];s->brightness=(u16)values[19];}return 1;}

extern GteMatrix g_BurstEffectScaleMatrix0,g_BurstEffectScaleMatrix1,g_BurstEffectScaleMatrix2;
extern GteVector g_BurstEffectScaleA,g_BurstEffectScaleB,g_BurstEffectScaleC;
extern GteShortVector g_BurstEffectSpriteRotation;
static int glow_walk(FieldGlowSprite*s,const unsigned char*in,unsigned char*out,int apply){unsigned off[8]={0,1,2,4,5,6,8,10};size_t at=0;for(unsigned j=0;j<8;j++)if(!field(in,out,&at,s,off[j],j<6?0:j==6?2:1,apply))return 0;return 1;}
static int matrix_walk(GteMatrix*m,const unsigned char*in,unsigned char*out,int apply){uint32_t v[12];for(unsigned j=0;j<12;j++){v[j]=out?(j<9?(uint32_t)(int32_t)m->m[j/3][j%3]:(uint32_t)m->t[j-9]):rd(in+j*4);if(j<9&&((int32_t)v[j]<INT16_MIN||(int32_t)v[j]>INT16_MAX))return 0;if(out)wr(out+j*4,v[j]);}if(apply){memset(m,0,sizeof*m);for(unsigned j=0;j<9;j++)m->m[j/3][j%3]=(s16)v[j];for(unsigned j=0;j<3;j++)m->t[j]=(int32_t)v[j+9];}return 1;}
static uint32_t constants_crc(void){unsigned char raw[56];memcpy(raw,&g_BurstEffectScaleA,16);memcpy(raw+16,&g_BurstEffectScaleB,16);memcpy(raw+32,&g_BurstEffectScaleC,16);memcpy(raw+48,&g_BurstEffectSpriteRotation,8);uint32_t c=~0u;for(unsigned j=0;j<56;j++){c^=raw[j];for(unsigned k=0;k<8;k++)c=(c>>1)^(0xedb88320u&-(c&1u));}return ~c;}
static int burst_globals(const unsigned char*in,unsigned char*out,int apply){FieldActor**targets[2]={&g_BurstEffectBTarget,&g_BurstEffectTarget};FieldFxPointTriple*positions[2]={(FieldFxPointTriple*)&g_BurstEffectBTargetPosition,&g_BurstEffectTargetPosition};for(unsigned family=0;family<2;family++){size_t at=196+family*16;unsigned target=out?actor_id(*targets[family]):rd(in+at);if(target>14)return 0;if(out)wr(out+at,target);for(unsigned j=0;j<3;j++){uint32_t v=out?((u16*)positions[family])[j]:rd(in+at+4+j*4);if(v>65535)return 0;if(out)wr(out+at+4+j*4,v);}if(apply){*targets[family]=target?&g_EntityPool[target-1]:0;for(unsigned j=0;j<3;j++)((u16*)positions[family])[j]=(u16)rd(in+at+4+j*4);}}
 FieldFxSpriteState*sprites[3]={&g_BurstEffectBState0,&g_BurstEffectBState1,&g_BurstEffectBState2};for(unsigned j=0;j<3;j++)if(!sprite_walk(sprites[j],in?in+228+j*80:0,out?out+228+j*80:0,apply))return 0;
 FieldGlowSprite*glows[4]={&g_BurstEffectSprite0,&g_BurstEffectSprite1,&g_BurstEffectSprite2,&g_BurstEffectSprite3};for(unsigned j=0;j<4;j++)if(!glow_walk(glows[j],in?in+468+j*32:0,out?out+468+j*32:0,apply))return 0;
 GteMatrix*matrices[3]={&g_BurstEffectScaleMatrix0,&g_BurstEffectScaleMatrix1,&g_BurstEffectScaleMatrix2};for(unsigned j=0;j<3;j++)if(!matrix_walk(matrices[j],in?in+596+j*48:0,out?out+596+j*48:0,apply))return 0;
 uint32_t crc=constants_crc();if(out)wr(out+740,crc);else if(rd(in+740)!=crc)return 0;return 1;}

static int extended_globals(const PeWorldState*,const unsigned char*,const unsigned char*,unsigned char*,int);
int PeStateLegacy_WalkForWorld(const PeWorldState*w,const unsigned char*header,const unsigned char*in,unsigned char*out,int apply){
 if((!in&&!out)||(!out&&!header)||(out&&apply)||!g_PmSlotTable||!PeStateBoundary_AtCpuPoint())return 0;
 if(apply&&(!Psyz_StateAudioOwned()||!Psyz_StateKernelOwned()||!PeStateLegacy_WalkForWorld(w,header,in,0,0)||!PeStateLegacy_Constructed(w,header,in)))return 0;
 if(out){memset(out,0,PE_STATE_LEGACY_FX_BYTES);memcpy(out,"PELEGFX4",8);}else if(memcmp(in,"PELEGFX4",8))return 0;
 uint64_t ids[3]={PeStateBoundary_Session(),PeStateBoundary_SceneEpoch(),PeStateAssets_Generation()};for(unsigned j=0;j<3;j++){if(out){wr(out+8+j*8,(uint32_t)ids[j]);wr(out+12+j*8,(uint32_t)(ids[j]>>32));}else if(rd(in+8+j*8)!=(uint32_t)ids[j]||rd(in+12+j*8)!=(uint32_t)(ids[j]>>32))return 0;}
 uint32_t targets[7];unsigned count=out?g_BattleActionActorCount:rd(in+76);if(count>7)return 0;for(unsigned j=0;j<7;j++){targets[j]=out?actor_id(g_BattleActionActors[j]):rd(in+48+j*4);if(targets[j]>14)return 0;if(out)wr(out+48+j*4,targets[j]);}unsigned length=0;while(length<7&&targets[length])length++;if(length==7||count>length)return 0;for(unsigned j=length;j<7;j++)if(targets[j])return 0;uint32_t live;if(!mask_for(w,apply,&live))return 0;for(unsigned j=0;j<count;j++)if(!(live&(1u<<(targets[j]-1))))return 0;if(out)wr(out+76,count);unsigned target=out?actor_id(g_BurstEffectCTarget):rd(in+80);if(target>14)return 0;if(out)wr(out+80,target);for(unsigned j=0;j<3;j++){uint32_t v=out?((u16*)&g_BurstEffectCTargetPosition)[j]:rd(in+84+j*4);if(v>65535)return 0;if(out)wr(out+84+j*4,v);}
 if(!sprite_walk(&g_BurstEffectCState,in?in+96:0,out?out+96:0,apply))return 0;
 unsigned roles[5]={out?object_role(g_FieldFxScriptObject,0):rd(in+176),out?object_role(g_FieldFxScriptState,1):rd(in+180),out?object_role(g_FieldFxScriptData,2):rd(in+184),out?object_role(g_FieldFxSlotTable,3):rd(in+188),out?command_role(g_FieldFxCommandTable):rd(in+192)};for(unsigned j=0;j<5;j++){if(roles[j]>(j==4?8u:11u))return 0;if(out)wr(out+176+j*4,roles[j]);}
 if(!burst_globals(in,out,apply)||!extended_globals(w,header,in,out,apply))return 0;if(!out&&(!zero(in+32,16)||!zero(in+1724,324)))return 0;
 for(unsigned j=0;j<11;j++){unsigned state=out?g_PmSlotTable[j].header.state:rd(header+24+j*20),cmd=out?g_PmSlotTable[j].header.command:rd(header+28+j*20);size_t at=2048+j*8192;if(!state||cmd>7){if(!out&&!zero(in+at,8192))return 0;continue;}unsigned kind=out?g_PmSlotTable[j].header.field02:rd(header+32+j*20),cached=out?g_PmSlotTable[j].header.field03:rd(header+36+j*20);if((!out&&rd(in+at+172)!=cmd)||kind||cached>64||!PeStateLegacy_HeaderSupported(j,state,cmd)||!PeStateLegacy_Context(&g_PmSlotTable[j].effect,w,targets,in?in+at:0,out?out+at:0,apply))return 0;}
 if(apply){for(unsigned j=0;j<7;j++)g_BattleActionActors[j]=targets[j]?(BattleEntity*)&g_EntityPool[targets[j]-1]:0;g_BattleActionActorCount=(u8)count;g_BurstEffectCTarget=target?&g_EntityPool[target-1]:0;for(unsigned j=0;j<3;j++)((u16*)&g_BurstEffectCTargetPosition)[j]=(u16)rd(in+84+j*4);g_FieldFxScriptObject=roles[0]?&g_PmSlotTable[roles[0]-1].effect:0;g_FieldFxScriptState=roles[1]?&g_PmSlotTable[roles[1]-1].effect.state:0;g_FieldFxScriptData=roles[2]?g_PmSlotTable[roles[2]-1].effect.data:0;g_FieldFxSlotTable=roles[3]?g_PmSlotTable[roles[3]-1].effect.slots:0;g_FieldFxCommandTable=script_root(roles[4]);}return 1;
}
int PeStateLegacy_CurrentSupported(unsigned index){if(index>=11||!g_PmSlotTable)return 0;PmSlotHeader*h=&g_PmSlotTable[index].header;if(!PeStateLegacy_HeaderSupported(index,h->state,h->command)||h->field02||h->field03>64)return 0;unsigned char wire[PE_STATE_LEGACY_FX_BYTES];return PeStateLegacy_WalkForWorld(0,0,0,wire,0);}
static int extended_constructed(const PeWorldState*,const unsigned char*,const unsigned char*);
int PeStateLegacy_Constructed(const PeWorldState*w,const unsigned char*h,const unsigned char*in){if(!h||!in||!g_PmSlotTable)return 0;uint32_t actual;if(!mask_for(w,1,&actual))return 0;for(unsigned j=0;j<11;j++)if(rd(h+24+j*20)&&rd(h+28+j*20)<=7){unsigned cmd=rd(h+28+j*20);if(!PeStateLegacy_HeaderSupported(j,rd(h+24+j*20),cmd)||!owner_role(w,rd(in+2048+j*8192+8),1)||(cmd==5&&!player_role(w,1)))return 0;}return extended_constructed(w,h,in);}
/* Original Glow0..3 and PointSprite7: named payload fields only. */
static int joint_owner(const PeWorldState*w,unsigned id,int apply){
 if(!owner_role(w,id,apply))return 0;const void*header=w&&w->targetMode?w->actor[id-1].header:g_EntityPool[id-1].render_object.header;
 unsigned source;uint32_t offset;size_t available;uint64_t epoch;
 if(!PeStateBankSource_Locate(header,sizeof(RenderObjectHeader),&source,&offset,&available,&epoch)||!epoch||((const RenderObjectHeader*)header)->part_count<20)return 0;
 return w&&w->targetMode&&!apply?1:g_EntityPool[id-1].render_object.matrices!=0;
}
static int action_player(const PeWorldState*w,int apply){
 if(!player_role(w,apply))return 0;unsigned id=w&&w->targetMode?w->constructorPlayerId:actor_id(g_PlayerEntity);
 if(w&&w->targetMode&&!apply)return w->actor[id-1].coreRole==1;
 uint32_t core,frame,contact;FieldActor*a=&g_EntityPool[id-1];
 return PeWorld_NativeActorRoles(a,&core,&frame,&contact)&&core==1;
}
static int extended_payload(unsigned command,unsigned id,void*data,const unsigned char*in,unsigned char*out,size_t*at,int apply){
 LegacyDesc d;if(!descriptor(command,&d)||id>d.maxId)return 0;unsigned bytes=d.sizes[id];if(apply&&bytes)memset(data,0,bytes);
 if(command<4){if(!id)return 1;
  if(id==1){if(!field(in,out,at,data,1,0,apply)||!field(in,out,at,data,2,0,apply))return 0;for(unsigned j=0;j<3;j++)if(!field(in,out,at,data,8+j*2,1,apply)||!field(in,out,at,data,16+j*2,2,apply))return 0;return 1;}
  if(!field(in,out,at,data,4,1,apply)||!field(in,out,at,data,6,2,apply))return 0;
  for(unsigned j=0;j<3;j++)if(!field(in,out,at,data,8+j*2,1,apply))return 0;
  if(command==3&&id==3)for(unsigned j=0;j<3;j++)if(!field(in,out,at,data,16+j*2,1,apply))return 0;
  if(id==2||id==5){if(*at>8192-48||!matrix_walk((GteMatrix*)((char*)data+16),in?in+*at:0,out?out+*at:0,apply))return 0;*at+=48;}
  return 1;
 }
 if(command!=7)return 0;
 if(id!=1&&id!=4){if(!field(in,out,at,data,3,0,apply)||!field(in,out,at,data,4,2,apply))return 0;for(unsigned j=0;j<3;j++)if(!field(in,out,at,data,6+j*2,1,apply))return 0;return 1;}
 if(*at>8192-8)return 0;unsigned count=out?(id==1?(unsigned)((FieldFxRadialParticles*)data)->count:(unsigned)((FieldFxScatteredParticles*)data)->points.count):rd(in+*at+4);
 if((id==1&&count!=8&&count!=16)||(id==4&&count!=4&&count!=16))return 0;
 if(!field(in,out,at,data,id==1?1:3,0,apply)||!field(in,out,at,data,id==1?2:4,id==1?0:2,apply))return 0;
 for(unsigned j=0;j<count;j++){
  if(id==1){if(!field(in,out,at,data,6+j*2,1,apply))return 0;for(unsigned k=0;k<3;k++)if(!field(in,out,at,data,38+j*8+k*2,1,apply)||!field(in,out,at,data,166+j*8+k*2,2,apply))return 0;}
  else for(unsigned k=0;k<3;k++)if(!field(in,out,at,data,8+k*32+j*2,1,apply)||!field(in,out,at,data,104+k*32+j*2,2,apply))return 0;
 }
 return 1;
}
/* Exact quiescent VM states from the complete original PointSprite script.
 * Each wait stores the NEXT PC; end commands retain their own PC.  No pointer
 * or additional payload is introduced by either original conditional tail. */
static int point_vm_boundary(unsigned control,unsigned pc,unsigned wait,unsigned halted,const int32_t regs[16]){
 static const unsigned stops[3][5]={{6,12,17,20,21},{25,31,33,36,37},{41,47,49,52,53}};
 static const unsigned waits[5]={4,4,2,8,0};
 if(control>2||halted>1)return 0;
 unsigned stage=0;int32_t exact[16]={0};
 if(pc==0){if(wait||halted)return 0;}
 else {
  while(stage<5&&stops[control][stage]!=pc)stage++;
  if(stage==5||wait>(control&&stage==1?3u:waits[stage])||halted!=(stage==4))return 0;
  exact[0]=(int32_t)control;
  if(!control){if(stage>=1)exact[3]=exact[4]=exact[5]=32;if(stage>=2)exact[0]=exact[1]=exact[2]=128;}
  else if(stage>=1){exact[0]=64;exact[control==1?1:2]=64;}
 }
 for(unsigned i=0;i<16;i++)if(regs[i]!=exact[i])return 0;
 return 1;
}
static int extended_context(FieldFxObject*object,const PeWorldState*w,const unsigned char*in,unsigned char*out,int apply){
 unsigned command=out?object->prefix.asset_type:rd(in+172);LegacyDesc d;if(!descriptor(command,&d)||(command>3&&command!=7))return 0;
 if(apply&&!extended_context(object,w,in,0,0))return 0;
 if(out){memset(out,0,8192);memcpy(out,"PEFXVM04",8);}else if(memcmp(in,"PEFXVM04",8))return 0;
 unsigned owner=out?actor_id(object->prefix.actor):rd(in+8),wait=out?(unsigned)object->state.wait:rd(in+12),pc=out?(unsigned)object->state.pc:rd(in+16),next=out?(unsigned)object->state.data_next:rd(in+20),count=out?(unsigned)object->state.slot_count:rd(in+24),halted=out?(unsigned)object->state.halted:rd(in+28);
 unsigned pcAllowed=pc==0||pc==d.words-1||pc==d.idle||(command==7&&(pc==12||pc==17||pc==20));
 unsigned maxWait=command!=7?0:pc==6||pc==12?4:pc==17?2:pc==20?8:0;
 if(!owner_role(w,owner,apply)||!action_player(w,apply)||(command<4&&!joint_owner(w,owner,apply))||(command!=7&&(!pcAllowed||wait>maxWait||((pc==d.words-1)!=(halted==1))))||next>d.limit||next%d.unit||count>64||halted>1)return 0;
 if(out&&(object->state.commands!=d.script||object->state.current_context))return 0;
 int32_t regs[16];for(unsigned j=0;j<16;j++)regs[j]=out?object->state.regs[j]:(int32_t)rd(in+32+j*4);
 if(command!=7) for(unsigned j=0;j<16;j++){if(j==15){if(regs[j]<0||regs[j]>2)return 0;}else if((command==0||command==3)&&j==10){if(regs[j]<0||regs[j]>1)return 0;}else if((command==0||command==3)&&j==11){if(regs[j]<0||regs[j]>9)return 0;}else if(regs[j])return 0;}
 unsigned control=out?(unsigned)object->state.vars[0]:rd(in+100);if(control>2||(out&&(intptr_t)control!=object->state.vars[0])||(command==7&&!point_vm_boundary(control,pc,wait,halted,regs)))return 0;
 for(unsigned j=0;j<9;j++){if(out){if(j&&object->state.vars[j])return 0;wr(out+96+j*8,0);wr(out+100+j*8,j?0:control);}else if(rd(in+96+j*8)||rd(in+100+j*8)!=(j?0:control))return 0;}
 if(out){wr(out+8,owner);wr(out+12,wait);wr(out+16,pc);wr(out+20,next);wr(out+24,count);wr(out+28,halted);for(unsigned j=0;j<16;j++)wr(out+32+j*4,(uint32_t)regs[j]);wr(out+168,command+1);wr(out+172,command);}else if(rd(in+168)!=command+1||!zero(in+176,80))return 0;
 unsigned active=0;size_t fields=1280;unsigned char occupied[FIELDFX_DATA_SIZE];memset(occupied,0,sizeof occupied);
 for(unsigned j=0;j<64;j++){FieldFxSlot*s=&object->slots[j];size_t at=256+j*16;unsigned flag=out?(unsigned)s->flag:rd(in+at),id=out?s->handler_id:rd(in+at+4),counter=out?s->counter:rd(in+at+8),offset=out?(unsigned)s->data_offset:rd(in+at+12);
  if(flag>2)return 0;if(!flag){if(!out&&!zero(in+at,16))return 0;if(apply)memset(s,0,sizeof*s);continue;}active++;
  if(id>d.maxId||counter>65535||offset>d.limit||offset%d.unit)return 0;unsigned bytes=d.sizes[id];if(bytes>FIELDFX_DATA_SIZE-offset||offset+bytes>=FIELDFX_DATA_SIZE)return 0;
  for(unsigned k=0;k<bytes;k++){if(occupied[offset+k])return 0;occupied[offset+k]=1;}
  if(out){wr(out+at,flag);wr(out+at+4,id);wr(out+at+8,counter);wr(out+at+12,offset);}
  if(!extended_payload(command,id,object->data+offset,in,out,&fields,apply))return 0;
  if(apply){s->flag=(s8)flag;s->handler_id=(u8)id;s->counter=(u16)counter;s->data_offset=(s16)offset;}
 }
 if(active!=count||fields>8192||(!out&&!zero(in+fields,8192-fields)))return 0;
 if(apply){object->prefix.actor=&g_EntityPool[owner-1];object->prefix.asset_type=(u8)command;object->state.wait=(s16)wait;object->state.pc=(s16)pc;object->state.data_next=(s16)next;object->state.slot_count=(s8)count;object->state.halted=(s8)halted;memcpy(object->state.regs,regs,sizeof regs);memset(object->state.vars,0,sizeof object->state.vars);object->state.vars[0]=(intptr_t)control;object->state.commands=d.script;object->state.current_context=0;}
 return 1;
}

extern GteShortVector g_GlowEffectLayersSpinA;
extern GteShortVector g_GlowLayers1SpinB;
extern GteVector g_GlowLayers1Scale;
extern GteShortVector g_GlowEffectBLayersSpinA;
extern GteShortVector g_GlowEffectBSpinB;
extern GteVector g_GlowEffectBLayersScale;
extern GteShortVector g_GlowEffectCLayersSpinA;
extern GteShortVector g_GlowEffectCLayersSpinB;
extern GteVector g_GlowLayers3Scale;
extern GteShortVector g_FourLayerGlowSpinA;
extern GteShortVector g_FourLayerGlowSpinB;
extern GteShortVector g_FourLayerGlowSpinC;
extern GteShortVector g_FourLayerGlowSpinD;
extern GteVector g_FourLayerGlowScale;
extern GteShortVector g_GlowEffectPointOffset;
extern GteShortVector g_GlowEffectSpriteRotation;
extern s16 g_GlowEffectScaleByAction[16];
extern GteShortVector g_GlowEffectFlareOffsets[8];
extern GteShortVector g_GlowEffectHaloOffsets[8];
extern s16 g_GlowEffectBScaleByAction[16];
extern GteShortVector g_GlowEffectBFlareOffsets[8];
extern s16 g_GlowEffectCScaleByAction[16];
extern GteShortVector g_GlowEffectCFlareOffsets[8];
extern GteShortVector g_GlowEffectBPointOffset;
extern GteShortVector g_FourLayerGlowPointOffsetA;
extern GteShortVector g_FourLayerGlowPointOffsetB;
extern GteShortVector g_FourLayerGlowSpriteRotation;
extern s16 g_FourLayerGlowScaleByAction[16];
extern GteShortVector g_FourLayerGlowFlareOffsets[8];
extern GteShortVector g_FourLayerGlowHaloOffsets[8];
static uint32_t glow_constants_crc(void){uint32_t c=~0u;const unsigned char*p;size_t n;
p=(const unsigned char*)&g_GlowEffectLayersSpinA;n=sizeof g_GlowEffectLayersSpinA;for(size_t j=0;j<n;j++){c^=p[j];for(unsigned k=0;k<8;k++)c=(c>>1)^(0xedb88320u&-(c&1u));}
p=(const unsigned char*)&g_GlowLayers1SpinB;n=sizeof g_GlowLayers1SpinB;for(size_t j=0;j<n;j++){c^=p[j];for(unsigned k=0;k<8;k++)c=(c>>1)^(0xedb88320u&-(c&1u));}
p=(const unsigned char*)&g_GlowLayers1Scale;n=sizeof g_GlowLayers1Scale;for(size_t j=0;j<n;j++){c^=p[j];for(unsigned k=0;k<8;k++)c=(c>>1)^(0xedb88320u&-(c&1u));}
p=(const unsigned char*)&g_GlowEffectBLayersSpinA;n=sizeof g_GlowEffectBLayersSpinA;for(size_t j=0;j<n;j++){c^=p[j];for(unsigned k=0;k<8;k++)c=(c>>1)^(0xedb88320u&-(c&1u));}
p=(const unsigned char*)&g_GlowEffectBSpinB;n=sizeof g_GlowEffectBSpinB;for(size_t j=0;j<n;j++){c^=p[j];for(unsigned k=0;k<8;k++)c=(c>>1)^(0xedb88320u&-(c&1u));}
p=(const unsigned char*)&g_GlowEffectBLayersScale;n=sizeof g_GlowEffectBLayersScale;for(size_t j=0;j<n;j++){c^=p[j];for(unsigned k=0;k<8;k++)c=(c>>1)^(0xedb88320u&-(c&1u));}
p=(const unsigned char*)&g_GlowEffectCLayersSpinA;n=sizeof g_GlowEffectCLayersSpinA;for(size_t j=0;j<n;j++){c^=p[j];for(unsigned k=0;k<8;k++)c=(c>>1)^(0xedb88320u&-(c&1u));}
p=(const unsigned char*)&g_GlowEffectCLayersSpinB;n=sizeof g_GlowEffectCLayersSpinB;for(size_t j=0;j<n;j++){c^=p[j];for(unsigned k=0;k<8;k++)c=(c>>1)^(0xedb88320u&-(c&1u));}
p=(const unsigned char*)&g_GlowLayers3Scale;n=sizeof g_GlowLayers3Scale;for(size_t j=0;j<n;j++){c^=p[j];for(unsigned k=0;k<8;k++)c=(c>>1)^(0xedb88320u&-(c&1u));}
p=(const unsigned char*)&g_FourLayerGlowSpinA;n=sizeof g_FourLayerGlowSpinA;for(size_t j=0;j<n;j++){c^=p[j];for(unsigned k=0;k<8;k++)c=(c>>1)^(0xedb88320u&-(c&1u));}
p=(const unsigned char*)&g_FourLayerGlowSpinB;n=sizeof g_FourLayerGlowSpinB;for(size_t j=0;j<n;j++){c^=p[j];for(unsigned k=0;k<8;k++)c=(c>>1)^(0xedb88320u&-(c&1u));}
p=(const unsigned char*)&g_FourLayerGlowSpinC;n=sizeof g_FourLayerGlowSpinC;for(size_t j=0;j<n;j++){c^=p[j];for(unsigned k=0;k<8;k++)c=(c>>1)^(0xedb88320u&-(c&1u));}
p=(const unsigned char*)&g_FourLayerGlowSpinD;n=sizeof g_FourLayerGlowSpinD;for(size_t j=0;j<n;j++){c^=p[j];for(unsigned k=0;k<8;k++)c=(c>>1)^(0xedb88320u&-(c&1u));}
p=(const unsigned char*)&g_FourLayerGlowScale;n=sizeof g_FourLayerGlowScale;for(size_t j=0;j<n;j++){c^=p[j];for(unsigned k=0;k<8;k++)c=(c>>1)^(0xedb88320u&-(c&1u));}
p=(const unsigned char*)&g_GlowEffectPointOffset;n=sizeof g_GlowEffectPointOffset;for(size_t j=0;j<n;j++){c^=p[j];for(unsigned k=0;k<8;k++)c=(c>>1)^(0xedb88320u&-(c&1u));}
p=(const unsigned char*)&g_GlowEffectSpriteRotation;n=sizeof g_GlowEffectSpriteRotation;for(size_t j=0;j<n;j++){c^=p[j];for(unsigned k=0;k<8;k++)c=(c>>1)^(0xedb88320u&-(c&1u));}
p=(const unsigned char*)&g_GlowEffectScaleByAction;n=sizeof g_GlowEffectScaleByAction;for(size_t j=0;j<n;j++){c^=p[j];for(unsigned k=0;k<8;k++)c=(c>>1)^(0xedb88320u&-(c&1u));}
p=(const unsigned char*)&g_GlowEffectFlareOffsets;n=sizeof g_GlowEffectFlareOffsets;for(size_t j=0;j<n;j++){c^=p[j];for(unsigned k=0;k<8;k++)c=(c>>1)^(0xedb88320u&-(c&1u));}
p=(const unsigned char*)&g_GlowEffectHaloOffsets;n=sizeof g_GlowEffectHaloOffsets;for(size_t j=0;j<n;j++){c^=p[j];for(unsigned k=0;k<8;k++)c=(c>>1)^(0xedb88320u&-(c&1u));}
p=(const unsigned char*)&g_GlowEffectBScaleByAction;n=sizeof g_GlowEffectBScaleByAction;for(size_t j=0;j<n;j++){c^=p[j];for(unsigned k=0;k<8;k++)c=(c>>1)^(0xedb88320u&-(c&1u));}
p=(const unsigned char*)&g_GlowEffectBFlareOffsets;n=sizeof g_GlowEffectBFlareOffsets;for(size_t j=0;j<n;j++){c^=p[j];for(unsigned k=0;k<8;k++)c=(c>>1)^(0xedb88320u&-(c&1u));}
p=(const unsigned char*)&g_GlowEffectCScaleByAction;n=sizeof g_GlowEffectCScaleByAction;for(size_t j=0;j<n;j++){c^=p[j];for(unsigned k=0;k<8;k++)c=(c>>1)^(0xedb88320u&-(c&1u));}
p=(const unsigned char*)&g_GlowEffectCFlareOffsets;n=sizeof g_GlowEffectCFlareOffsets;for(size_t j=0;j<n;j++){c^=p[j];for(unsigned k=0;k<8;k++)c=(c>>1)^(0xedb88320u&-(c&1u));}
p=(const unsigned char*)&g_GlowEffectBPointOffset;n=sizeof g_GlowEffectBPointOffset;for(size_t j=0;j<n;j++){c^=p[j];for(unsigned k=0;k<8;k++)c=(c>>1)^(0xedb88320u&-(c&1u));}
p=(const unsigned char*)&g_FourLayerGlowPointOffsetA;n=sizeof g_FourLayerGlowPointOffsetA;for(size_t j=0;j<n;j++){c^=p[j];for(unsigned k=0;k<8;k++)c=(c>>1)^(0xedb88320u&-(c&1u));}
p=(const unsigned char*)&g_FourLayerGlowPointOffsetB;n=sizeof g_FourLayerGlowPointOffsetB;for(size_t j=0;j<n;j++){c^=p[j];for(unsigned k=0;k<8;k++)c=(c>>1)^(0xedb88320u&-(c&1u));}
p=(const unsigned char*)&g_FourLayerGlowSpriteRotation;n=sizeof g_FourLayerGlowSpriteRotation;for(size_t j=0;j<n;j++){c^=p[j];for(unsigned k=0;k<8;k++)c=(c>>1)^(0xedb88320u&-(c&1u));}
p=(const unsigned char*)&g_FourLayerGlowScaleByAction;n=sizeof g_FourLayerGlowScaleByAction;for(size_t j=0;j<n;j++){c^=p[j];for(unsigned k=0;k<8;k++)c=(c>>1)^(0xedb88320u&-(c&1u));}
p=(const unsigned char*)&g_FourLayerGlowFlareOffsets;n=sizeof g_FourLayerGlowFlareOffsets;for(size_t j=0;j<n;j++){c^=p[j];for(unsigned k=0;k<8;k++)c=(c>>1)^(0xedb88320u&-(c&1u));}
p=(const unsigned char*)&g_FourLayerGlowHaloOffsets;n=sizeof g_FourLayerGlowHaloOffsets;for(size_t j=0;j<n;j++){c^=p[j];for(unsigned k=0;k<8;k++)c=(c>>1)^(0xedb88320u&-(c&1u));}
return ~c;}
extern FieldGlowSprite g_GlowEffectBSprite2;
static int extended_globals(const PeWorldState*w,const unsigned char*header,const unsigned char*in,unsigned char*out,int apply){
 FieldActor**owners[5]={&g_GlowEffectOwner,&g_GlowEffectBOwner,&g_GlowEffectCOwner,&g_FourLayerGlowOwner,&g_PointSpriteOwner};
 FieldFxPointTriple*positions[5]={(FieldFxPointTriple*)&g_GlowEffectPosition,(FieldFxPointTriple*)&g_GlowEffectBPosition,(FieldFxPointTriple*)&g_GlowEffectCPosition,(FieldFxPointTriple*)&g_FourLayerGlowPosition,&g_PointSpriteActorPosition};
 unsigned active[4]={0};for(unsigned j=0;j<11;j++){unsigned state=out?g_PmSlotTable[j].header.state:rd(header+24+j*20),cmd=out?g_PmSlotTable[j].header.command:rd(header+28+j*20);if(state&&cmd<4)active[cmd]=1;}
 for(unsigned family=0;family<5;family++){size_t at=744+family*16;unsigned id=out?actor_id(*owners[family]):rd(in+at);if(id>14||(family<4&&active[family]&&!joint_owner(w,id,apply)))return 0;if(out)wr(out+at,id);for(unsigned j=0;j<3;j++){unsigned v=out?((u16*)positions[family])[j]:rd(in+at+4+j*4);if(v>65535)return 0;if(out)wr(out+at+4+j*4,v);}if(apply){*owners[family]=id?&g_EntityPool[id-1]:0;for(unsigned j=0;j<3;j++)((u16*)positions[family])[j]=(u16)rd(in+at+4+j*4);}}
 FieldGlowSprite*sprites[13]={&g_GlowEffectSprite0,&g_GlowEffectSprite1,&g_GlowEffectSprite2,&g_GlowEffectSprite3,&g_GlowEffectBSprite0,&g_GlowEffectBSprite1,&g_GlowEffectBSprite2,&g_GlowEffectCSprite0,&g_GlowEffectCSprite1,&g_FourLayerGlowSprite0,&g_FourLayerGlowSprite1,&g_FourLayerGlowSprite2,&g_FourLayerGlowSprite3};
 for(unsigned j=0;j<13;j++)if(!glow_walk(sprites[j],in?in+824+j*32:0,out?out+824+j*32:0,apply))return 0;
 FieldFxSpriteState*points[6]={&g_PointSpriteGroundRingSprite,&g_PointSpriteState3,&g_PointSpriteState4,&g_PointSpriteStreakSprite,&g_PointSpriteState0,&g_PointSpriteState1};
 for(unsigned j=0;j<6;j++)if(!sprite_walk(points[j],in?in+1240+j*80:0,out?out+1240+j*80:0,apply))return 0;
 uint32_t c=glow_constants_crc();if(out)wr(out+1720,c);else if(rd(in+1720)!=c)return 0;
 return 1;
}
static int extended_constructed(const PeWorldState*w,const unsigned char*h,const unsigned char*in){
 unsigned active[4]={0};for(unsigned j=0;j<11;j++){unsigned state=rd(h+24+j*20),cmd=rd(h+28+j*20);if(!state||cmd>7||(cmd>=4&&cmd<=6))continue;unsigned owner=rd(in+2048+j*8192+8);if(!owner_role(w,owner,1)||!action_player(w,1)||(cmd<4&&!joint_owner(w,owner,1)))return 0;if(cmd<4)active[cmd]=1;}
 for(unsigned j=0;j<4;j++)if(active[j]&&!joint_owner(w,rd(in+744+j*16),1))return 0;return 1;
}



