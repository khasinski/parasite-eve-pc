#include "pe_state_field_models.h"
#include "pe_state_theater_tasks.h"
#include "pe_state_checkpoint.h"
#include "pe_state_world.h"
#ifdef PE_PS5_GPU_NATIVE
#include "pe1/planar_shadow.h"
#endif
#include "pe1/particle_augment.h"
#include "pe_state_boundary.h"
#include "pe_state_assets.h"
#include "pe_state_theater.h"
#include "pe_state_script_sprites.h"
#include "pe_state_mod_contract.h"
#include "pe_state_mirror.h"
#include "pe1/room_m005.h"
#include "pe1/room_flare.h"
#include "pe1/room_m023_effects.h"
#include "pe1/pm.h"
#include "pe1/game_state.h"
#include "pe1/player_entity.h"
#include "pe1/save.h"
#include "pe1/scene_assets.h"
#include <string.h>
_Static_assert(sizeof(SaveRuntimeState)==0x12e4,"original pointer-free persistent image");
static uint32_t get(const unsigned char *p){return(uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void put(unsigned char *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(v>>(8*i));}
static int checkpoint_boundary_base(void){if(!PeStateBoundary_AtCpuPoint())return PE_CHECKPOINT_NOT_AT_CPU_POINT;if(!g_PlayerEntity)return PE_CHECKPOINT_NO_PLAYER;if(g_GameState.scene_audio.tracks.movie_active)return PE_CHECKPOINT_MOVIE_ACTIVE;if(!g_PmSlotTable||!g_PmSlotTable2)return PE_CHECKPOINT_PM_ACTIVE;return 0;}
int PeStateCheckpoint_Boundary(void){PeStateTasks_ResetBlocker();int base=checkpoint_boundary_base();if(base)return base;for(unsigned i=0;i<11;i++)if((!PeStateMirror_CurrentSupported(i,&g_PmSlotTable[i].header)&&!PeStateTasks_CurrentSupported(i))||!PeStateMirror_CurrentSupported(i+11,&g_PmSlotTable2[i].header))return PE_CHECKPOINT_PM_ACTIVE;return PE_CHECKPOINT_SUPPORTED_IDLE;}
#define PERSIST_BYTES (8u+sizeof(SaveRuntimeState))
static int persistent_validate(void *ctx,const unsigned char *in,size_t bytes){(void)ctx;if(!in||bytes!=PERSIST_BYTES||memcmp(in,"PEPERS01",8))return 0;SaveRuntimeState image;memcpy(&image,in+8,sizeof image);if(image.player.aya.inventory_slot_count>50||image.player.aya.level>98||image.player.aya.equipped_weapon_slot< -1||image.player.aya.equipped_weapon_slot>=50||image.player.aya.equipped_armor_slot< -1||image.player.aya.equipped_armor_slot>=50)return 0;return 1;}
static int persistent_capture(void *ctx,unsigned char *out,size_t cap,size_t *used){(void)ctx;if(used)*used=0;if(!out||!used||cap<PERSIST_BYTES||PeStateCheckpoint_Boundary())return 0;memcpy(out,"PEPERS01",8);memcpy(out+8,&g_SaveRuntimeState,sizeof g_SaveRuntimeState);if(!persistent_validate(0,out,PERSIST_BYTES))return 0;*used=PERSIST_BYTES;return 1;}
static int persistent_restore(void *ctx,const unsigned char *in,size_t bytes){if(PeStateCheckpoint_Boundary()||!persistent_validate(ctx,in,bytes))return 0;memcpy(&g_SaveRuntimeState,in+8,sizeof g_SaveRuntimeState);return 1;}
static PmPrimarySlot *primary;static PmSecondarySlot *secondary;static PmCommand **handlers;static uint64_t pmScene;
extern u8 g_MenuCardDriftGlowColorTrack[40];
extern u8 g_MenuCardRisingEmberColorTrack[48];
extern u8 g_MenuCardGlintBurstColorTrack[40];
extern u8 g_MenuCardSpinRingColorTrack[40];
extern u8 g_MenuCardPulseColorTrack[40];
extern u8 g_MenuCardTrailColorTrack[48];
extern u8 g_MenuCardSineTrailColorTrack[48];
static FieldFxTaskProgram *const nativePrograms[]={&g_FieldFxTaskProgramCmd85,&g_FieldFxTaskProgramCmd86,&g_FieldFxTaskProgramCmd87,&g_FieldFxTaskProgramCmd88,&g_FieldFxTaskProgramCmd89,&g_FieldFxTaskProgramCmd90,&g_FieldFxTaskProgramCmd91,&g_FieldFxTaskProgramCmd92,&g_FieldFxTaskProgramCmd93,&g_FieldFxTaskProgramCmd99,&g_FieldFxTaskProgramCmd100,&g_FieldFxTaskProgramCmd101,&g_FieldFxTaskProgramCmd102,&g_FieldFxTaskProgramCmd103,&g_FieldFxTaskProgramCmd105,&g_FieldFxTaskProgramCmd107,&g_FieldFxTaskProgramCmd94,&g_FieldFxTaskProgramCmd95,&g_FieldFxTaskProgramCmd96,&g_FieldFxTaskProgramCmd97,&g_FieldFxTaskProgramCmd98,&g_FieldFxTaskProgramCmd104,&g_FieldFxTaskProgramCmd106,&g_FieldFxTaskProgramCmd108,&g_FieldFxTaskProgramCmd109,&g_FieldFxTaskProgramCmd110,&g_FieldFxTaskProgramCmd111,&g_FieldFxTaskProgramCmd112,&g_FieldFxTaskProgramCmd113,&g_FieldFxTaskProgramCmd114,&g_RoomM005Effects,&g_RoomOrbitFlareEffects,&g_RoomM023Effects};
static uint32_t program_id(const void *p){if(!p)return 0;for(unsigned i=0;i<sizeof nativePrograms/sizeof *nativePrograms;i++)if(p==nativePrograms[i])return i+1;return UINT32_MAX;}
#define PM_MAGIC "PEPMID16"
#define PM_BYTES (24u+22u*20u+304u+104u*4u)
static int pm_walk(PeWorldState*world,const unsigned char *in,unsigned char *out,int apply){size_t at=24;for(unsigned i=0;i<22;i++){PmSlotHeader *p=i<11?&g_PmSlotTable[i].header:&g_PmSlotTable2[i-11].header;uint32_t values[5]={p->state,p->command,p->field02,p->field03,p->ticks};for(unsigned j=0;j<5;j++){uint32_t v=out?values[j]:get(in+at);if((j==0&&v>5)||(j>0&&j<4&&v>255))return 0;if(out)put(out+at,v);else values[j]=v;at+=4;}if(!PeStateMirror_HeaderSupported(i,values[0],values[1])&&!PeStateTasks_HeaderSupported(i,values[0],values[1]))return 0;if(apply){p->state=(u8)values[0];p->command=(u8)values[1];p->field02=(u8)values[2];p->field03=(u8)values[3];p->ticks=values[4];p->owner=0;}}struct {u8 *data;size_t bytes;} caches[]={{g_MenuCardDriftGlowColorTrack,40},{g_MenuCardRisingEmberColorTrack,48},{g_MenuCardGlintBurstColorTrack,40},{g_MenuCardSpinRingColorTrack,40},{g_MenuCardPulseColorTrack,40},{g_MenuCardTrailColorTrack,48},{g_MenuCardSineTrailColorTrack,48}};for(unsigned i=0;i<7;i++){if(out)memcpy(out+at,caches[i].data,caches[i].bytes);else if(apply)memcpy(caches[i].data,in+at,caches[i].bytes);at+=caches[i].bytes;}for(unsigned i=0;i<104;i++){uint32_t id=out?program_id(g_FieldFxTaskPrograms[i]):get(in+at);if(id==UINT32_MAX||id>sizeof nativePrograms/sizeof *nativePrograms)return 0;if(out)put(out+at,id);else if(apply)g_FieldFxTaskPrograms[i]=id?nativePrograms[id-1]:0;at+=4;}if(at!=PM_BYTES||!PeStateTheater_Walk(in?in+at:0,out?out+at:0,apply))return 0;at+=PeStateTheater_Bytes();if(!PeStateScriptSprites_Walk(in?in+at:0,out?out+at:0,apply))return 0;at+=PE_STATE_SCRIPT_SPRITES_BYTES;uint32_t exact[4];if(!pe_mod_state_options(exact))return 0;for(unsigned i=0;i<4;i++){if(out)put(out+at,exact[i]);else if(get(in+at)!=exact[i])return 0;at+=4;}if(!PeStateMirror_WalkForWorld(world,in,in?in+at:0,out?out+at:0,apply))return 0;at+=PE_STATE_MIRROR_BYTES;if(!PeStateTasks_WalkForWorld(world,in,in?in+PM_BYTES:0,in?in+at:0,out?out+at:0,apply))return 0;at+=PE_STATE_TASKS_BYTES;if(!PeFieldModels_Walk(in?in+at:0,out?out+at:0,apply))return 0;at+=PE_STATE_FIELD_MODELS_BYTES;if(!FieldFx_ParticleStateWalk(in?in+at:0,out?out+at:0))return 0;at+=16u;return pe_mod_state_walk(in?in+at:0,out?out+at:0,PE_MOD_STATE_BYTES,apply);}
/* Per-frame derived packets are copied/drained before GPU state capture;
 * no active stack/arena admission may cross the actual final-CPU owner point.
 * Do not put this frame-open witness in menu readiness/HostReasonBits. */
static int checkpoint_shadow_idle(void){
#ifdef PE_PS5_GPU_NATIVE
    return PeShadow_StateIdle();
#else
    return 1;
#endif
}
static int pm_identity(PeWorldState*world,const unsigned char *in,size_t bytes){return checkpoint_shadow_idle()&&in&&bytes==PM_BYTES+PeStateTheater_Bytes()+PE_STATE_SCRIPT_SPRITES_BYTES+16u+PE_STATE_MIRROR_BYTES+PE_STATE_TASKS_BYTES+PE_STATE_FIELD_MODELS_BYTES+16u+PE_MOD_STATE_BYTES&&!memcmp(in,PM_MAGIC,8)&&primary==g_PmSlotTable&&secondary==g_PmSlotTable2&&handlers==g_PmCmdHandlerTable&&get(in+8)==(uint32_t)PeStateBoundary_SceneEpoch()&&get(in+12)==(uint32_t)(PeStateBoundary_SceneEpoch()>>32)&&get(in+16)==(uint32_t)PeStateAssets_Generation()&&get(in+20)==(uint32_t)(PeStateAssets_Generation()>>32)&&pm_walk(world,in,0,0);}
static int pm_validate(void *ctx,const unsigned char *in,size_t bytes){PeWorldState*w=ctx;return !(w&&w->targetMode?checkpoint_boundary_base():PeStateCheckpoint_Boundary())&&pm_identity(w,in,bytes);}
static int pm_prepare(void *ctx,const unsigned char *in,size_t bytes,int mode){PeWorldState*w=ctx;if(mode< -1||mode>1)return 0;if(mode<0)return pm_validate(ctx,in,bytes)?1:0;if(mode==0&&!pm_validate(ctx,in,bytes))return 0;if(mode==1&&!pm_identity(w,in,bytes))return 0;size_t offset=PM_BYTES+PeStateTheater_Bytes()+PE_STATE_SCRIPT_SPRITES_BYTES+16u;if(!PeStateTasks_Constructed(w,in,in+offset+PE_STATE_MIRROR_BYTES))return 0;int result=PeStateMirror_PrepareForWorld(w,in,in+offset);if(result!=1)return result;if(!PeStateTheater_Walk(in+PM_BYTES,0,1))return -1;return PeStateTasks_WalkForWorld(w,in,in+PM_BYTES,in+offset+PE_STATE_MIRROR_BYTES,0,1)?1:-1;}
static int pm_capture(void *ctx,unsigned char *out,size_t cap,size_t *used){PeWorldState*w=ctx;if(used)*used=0;if(!out||!used||cap<PM_BYTES+PeStateTheater_Bytes()+PE_STATE_SCRIPT_SPRITES_BYTES+16u+PE_STATE_MIRROR_BYTES+PE_STATE_TASKS_BYTES+PE_STATE_FIELD_MODELS_BYTES+16u+PE_MOD_STATE_BYTES||PeStateCheckpoint_Boundary())return 0;if(!primary||pmScene!=PeStateBoundary_SceneEpoch()){pmScene=PeStateBoundary_SceneEpoch();primary=g_PmSlotTable;secondary=g_PmSlotTable2;handlers=g_PmCmdHandlerTable;}memcpy(out,PM_MAGIC,8);put(out+8,(uint32_t)PeStateBoundary_SceneEpoch());put(out+12,(uint32_t)(PeStateBoundary_SceneEpoch()>>32));put(out+16,(uint32_t)PeStateAssets_Generation());put(out+20,(uint32_t)(PeStateAssets_Generation()>>32));if(!pm_walk(w,0,out,0)||!pm_validate(ctx,out,PM_BYTES+PeStateTheater_Bytes()+PE_STATE_SCRIPT_SPRITES_BYTES+16u+PE_STATE_MIRROR_BYTES+PE_STATE_TASKS_BYTES+PE_STATE_FIELD_MODELS_BYTES+16u+PE_MOD_STATE_BYTES))return 0;*used=PM_BYTES+PeStateTheater_Bytes()+PE_STATE_SCRIPT_SPRITES_BYTES+16u+PE_STATE_MIRROR_BYTES+PE_STATE_TASKS_BYTES+PE_STATE_FIELD_MODELS_BYTES+16u+PE_MOD_STATE_BYTES;return 1;}
static int pm_restore(void *ctx,const unsigned char *in,size_t n){return pm_validate(ctx,in,n)&&pm_walk(ctx,in,0,1);}
int PeState_RegisterCheckpointOwnersForGraph(PeStateRegistry *r,PeWorldState*world){PeStateModule pm={PE_STATE_PM,16,PM_BYTES+620u+PE_STATE_SCRIPT_SPRITES_BYTES+16u+PE_STATE_MIRROR_BYTES+PE_STATE_TASKS_BYTES+PE_STATE_FIELD_MODELS_BYTES+16u+PE_MOD_STATE_BYTES,pm_capture,world,pm_validate,pm_restore,pm_prepare},persistent={PE_STATE_PERSISTENT,1,PERSIST_BYTES,persistent_capture,0,persistent_validate,persistent_restore,0};int result=PeState_Register(r,pm);return result?result:PeState_Register(r,persistent);}

int PeState_RegisterCheckpointOwners(PeStateRegistry*r){return PeState_RegisterCheckpointOwnersForGraph(r,0);}


