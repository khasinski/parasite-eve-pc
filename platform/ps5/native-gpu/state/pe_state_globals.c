#include "pe_state_checkpoint.h"
#include "pe_state_boundary.h"
#include "pe_state_assets.h"
#include "pe1/game_state.h"
#include "pe1/game_timers.h"
#include "pe1/boot.h"
#include "pe1/render_lighting.h"
#include "pe1/field_sfx.h"
#include "pe1/battle_runtime.h"
#include "pe1/battle_main.h"
#include "pe1/scene_dispatch.h"
#include "pe1/render_buffers.h"
#include "pe1/entity_allocation.h"
#include "pe1/fieldfx_state.h"
#include <string.h>
static uint64_t rootScene,rootAssets;static const void *roots[14];
static void put(unsigned char *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(v>>(8*i));}
static uint32_t get(const unsigned char *p){return(uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static int bound(int bind){const void *now[14]={(const void *)g_GameState.scene_object_model,(const void *)g_GameState.room_geometry_table,(const void *)g_GameState.bank_asset_table,(const void *)g_GameState.bank_work_base,(const void *)g_GameState.bank_work_end,(const void *)g_GameState.voice_bank_base,(const void *)g_GameState.voice_bank_base_1400,(const void *)g_GameState.entity_texture_blob,(const void *)g_GameState.scene_object_work,(const void *)g_GameState.scene_process_slots,(const void *)g_GameState.loaded_scene_assets,(const void *)g_GameState.str_file_directory,(const void *)g_GameState.scene_load_scratch,(const void *)g_GameState.geometry_state};if(bind&&(rootScene!=PeStateBoundary_SceneEpoch()||rootAssets!=PeStateAssets_Generation())){rootScene=PeStateBoundary_SceneEpoch();rootAssets=PeStateAssets_Generation();memcpy(roots,now,sizeof roots);}if(rootScene!=PeStateBoundary_SceneEpoch()||rootAssets!=PeStateAssets_Generation())return 0;for(unsigned i=0;i<14;i++)if(roots[i]!=now[i])return 0;return !PeStateCheckpoint_Boundary();}
static int canonical(uint32_t v,unsigned k){return k==1?v<=255:k==2?v<=65535:k==3?((int32_t)v>=-32768&&(int32_t)v<=32767):k==4?((int32_t)v>=-128&&(int32_t)v<=127):k==5?v<=1:k==6?v<=7:k==7?((int32_t)v>=-1&&(int32_t)v<=7):1;}
static int walk(const unsigned char *in,unsigned char *out,int apply){size_t at=24;
#define FIELD(value,kind) do{uint32_t v=out?(uint32_t)(value):get(in+at);if(!canonical(v,kind))return 0;if(out)put(out+at,v);else if(apply)(value)=(__typeof__(value))v;at+=4;}while(0)
FIELD(g_GameState.flags,0);
FIELD(g_GameState.saved_state_004,2);
FIELD(g_GameState.field_bg_cache_id,3);
FIELD(g_GameState.room_type,1);
FIELD(g_GameState.field_bg_cache_bank,4);
FIELD(g_GameState.requested_entity_bank,1);
FIELD(g_GameState.loaded_entity_bank,1);
FIELD(g_GameState.current_story_day,4);
FIELD(g_GameState.pending_story_day,4);
FIELD(g_GameState.story_day_flags,1);
FIELD(g_GameState.unk_00f,1);
FIELD(g_GameState.cd_range_read_mode,1);
FIELD(g_GameState.bank_state_11,1);
FIELD(g_GameState.bank_state_12,1);
FIELD(g_GameState.scene_area_type_backup,1);
FIELD(g_GameState.reserved_scene_object_tail[0],1);
FIELD(g_GameState.reserved_scene_object_tail[1],1);
FIELD(g_GameState.reserved_scene_object_tail[2],1);
FIELD(g_GameState.reserved_scene_object_tail[3],1);
FIELD(g_GameState.reserved_scene_object_tail[4],1);
FIELD(g_GameState.reserved_scene_object_tail[5],1);
FIELD(g_GameState.reserved_scene_object_tail[6],1);
FIELD(g_GameState.reserved_scene_object_tail[7],1);
FIELD(g_GameState.pending_sample_bank,3);
FIELD(g_GameState.pending_stream_banks[0],1);
FIELD(g_GameState.pending_stream_banks[1],1);
FIELD(g_GameState.entity_texture_phase,1);
FIELD(g_GameState.scene_init_phase,1);
FIELD(g_GameState.scene_init_subphase,1);
FIELD(g_GameState.tim_load_state,1);
FIELD(g_GameState.cd_read_phase,1);
FIELD(g_GameState.cd_track_phase,1);
FIELD(g_GameState.cd_transition_phase,1);
FIELD(g_GameState.cd_range_state,1);
FIELD(g_GameState.memcard_init_phase,1);
FIELD(g_GameState.display_list_modes,1);
FIELD(g_GameState.bank_value_f6,1);
FIELD(g_GameState.bank_value_f7,1);
FIELD(g_GameState.bank_value_f8,2);
FIELD(g_GameState.bank_value_fa,2);
FIELD(g_GameState.unk_0fc[0],1);
FIELD(g_GameState.unk_0fc[1],1);
FIELD(g_GameState.track_volumes[0],1);
FIELD(g_GameState.track_volumes[1],1);
FIELD(g_GameState.pe_image_base_lba,0);
FIELD(g_GameState.bank_reset_940[0],0);
FIELD(g_GameState.geometry_disk_bytes,0);
FIELD(g_GameState.geometry_disk_entry_offset,0);
FIELD(g_GameStateBlock.flags,0);
FIELD(g_GameStateBlock.reserved,0);
FIELD(g_GameStateBlock.reserved_tail[0],0);
FIELD(g_GameStateBlock.reserved_tail[1],0);
FIELD(g_ActiveDrawSlot,5);
FIELD(g_GameState.scene_audio.bytes[0],1);
FIELD(g_GameState.scene_audio.bytes[1],1);
FIELD(g_GameState.scene_audio.bytes[2],1);
FIELD(g_GameState.scene_audio.bytes[3],1);
FIELD(g_GameState.scene_audio.bytes[4],1);
FIELD(g_GameState.scene_audio.bytes[5],1);
FIELD(g_GameState.scene_audio.bytes[6],1);
FIELD(g_GameState.scene_audio.bytes[7],1);
FIELD(g_GameState.scene_audio.bytes[8],1);
FIELD(g_GameState.scene_audio.bytes[9],1);
FIELD(g_GameState.scene_audio.bytes[10],1);
FIELD(g_GameState.scene_audio.bytes[11],1);
FIELD(g_GameState.scene_audio.bytes[12],1);
FIELD(g_GameState.scene_audio.bytes[13],1);
FIELD(g_GameState.scene_audio.bytes[14],1);
FIELD(g_GameState.scene_audio.bytes[15],1);
FIELD(g_GameState.fade_tile.r,1);
FIELD(g_GameState.fade_tile.g,1);
FIELD(g_GameState.fade_tile.b,1);
FIELD(g_GameState.fade_tile.code,1);
FIELD(g_GameState.fade_tile.x,3);
FIELD(g_GameState.fade_tile.y,3);
FIELD(g_GameState.fade_tile.w,2);
FIELD(g_GameState.fade_tile.h,2);
FIELD(g_MenuFrameCommand,3);
FIELD(g_BattleAttackPmSlot,0);
FIELD(g_BattleHitPending,1);
FIELD(g_BattleImpactPmSlot,0);
FIELD(g_BattleEffectPmSlot,0);
FIELD(g_BattleTargetCycleMode,4);
FIELD(g_BattleRemainingActions,1);
FIELD(g_BattleActionAnimModes[0],1);
FIELD(g_BattleActionAnimModes[1],1);
FIELD(g_BattleActionAnimModes[2],1);
FIELD(g_BattleActionAnimModes[3],1);
FIELD(g_BattleSaveOverlayActive,1);
FIELD(g_ActorEffectFlag100Timer,2);
FIELD(g_BattleSlotCancelPending,1);
FIELD(g_BattleActionIndex,1);
FIELD(g_BattleActionQueueCount,1);
FIELD(g_BattleModeState,0);
FIELD(g_BattleTargetCount,6);
FIELD(g_EscapeFailedTexts[0],1);
FIELD(g_EscapeFailedTexts[1],1);
FIELD(g_EscapeFailedTexts[2],1);
FIELD(g_EscapeFailedTexts[3],1);
FIELD(g_EscapeFailedTexts[4],1);
FIELD(g_EscapeFailedTexts[5],1);
FIELD(g_EscapeFailedTexts[6],1);
FIELD(g_EscapeFailedTexts[7],1);
FIELD(g_EscapeFailedTexts[8],1);
FIELD(g_EscapeFailedTexts[9],1);
FIELD(g_EscapeFailedTexts[10],1);
FIELD(g_EscapeFailedTexts[11],1);
FIELD(g_EscapeFailedTexts[12],1);
FIELD(g_EscapeFailedTexts[13],1);
FIELD(g_EscapeFailedTexts[14],1);
FIELD(g_EscapeFailedTexts[15],1);
FIELD(g_EscapeFailedTexts[16],1);
FIELD(g_EscapeFailedTexts[17],1);
FIELD(g_EscapeFailedTexts[18],1);
FIELD(g_EscapeFailedTexts[19],1);
FIELD(g_EscapeFailedTexts[20],1);
FIELD(g_EscapeFailedTexts[21],1);
FIELD(g_EscapeFailedTexts[22],1);
FIELD(g_EscapeFailedTexts[23],1);
FIELD(g_EscapeFailedTexts[24],1);
FIELD(g_EscapeFailedTexts[25],1);
FIELD(g_EscapeFailedTexts[26],1);
FIELD(g_EscapeFailedTexts[27],1);
FIELD(g_EscapeFailedTexts[28],1);
FIELD(g_EscapeFailedTexts[29],1);
FIELD(g_EscapeFailedTexts[30],1);
FIELD(g_EscapeFailedTexts[31],1);
FIELD(g_EscapeFailedTexts[32],1);
FIELD(g_EscapeFailedTexts[33],1);
FIELD(g_EscapeBlockedTexts[0],1);
FIELD(g_EscapeBlockedTexts[1],1);
FIELD(g_EscapeBlockedTexts[2],1);
FIELD(g_EscapeBlockedTexts[3],1);
FIELD(g_EscapeBlockedTexts[4],1);
FIELD(g_EscapeBlockedTexts[5],1);
FIELD(g_EscapeBlockedTexts[6],1);
FIELD(g_EscapeBlockedTexts[7],1);
FIELD(g_EscapeBlockedTexts[8],1);
FIELD(g_EscapeBlockedTexts[9],1);
FIELD(g_EscapeBlockedTexts[10],1);
FIELD(g_EscapeBlockedTexts[11],1);
FIELD(g_EscapeBlockedTexts[12],1);
FIELD(g_EscapeBlockedTexts[13],1);
FIELD(g_EscapeBlockedTexts[14],1);
FIELD(g_EscapeBlockedTexts[15],1);
FIELD(g_EscapeBlockedTexts[16],1);
FIELD(g_EscapeBlockedTexts[17],1);
FIELD(g_EscapeBlockedTexts[18],1);
FIELD(g_EscapeBlockedTexts[19],1);
FIELD(g_EscapeBlockedTexts[20],1);
FIELD(g_EscapeBlockedTexts[21],1);
FIELD(g_EscapeBlockedTexts[22],1);
FIELD(g_EscapeBlockedTexts[23],1);
FIELD(g_EscapeBlockedTexts[24],1);
FIELD(g_EscapeBlockedTexts[25],1);
FIELD(g_EscapeBlockedTexts[26],1);
FIELD(g_EscapeBlockedTexts[27],1);
FIELD(g_EntityInstanceCounter,1);
FIELD(g_BattleLiveEnemyCount,4);
FIELD(g_BattleStepIndex,1);
FIELD(g_BattleEnemyEscapeFlag,1);
FIELD(g_BattleHaltTimer,4);
FIELD(g_BattleTargetIndex,7);
FIELD(g_BattleGaugeCapLatch,1);
FIELD(g_FieldPadPressed,0);
FIELD(g_AyaActionStep,1);
FIELD(g_AyaAttackStep,4);
FIELD(g_EvasionReaction,4);
FIELD(g_AyaActionTimer,3);
FIELD(g_BattleCrossFlashStop,2);
FIELD(g_BattleHitTimer,1);
FIELD(g_BattlePlayerAimHeight,3);
for(unsigned i=0;i<4;i++){FIELD(g_GameTimers[i].flags,0);FIELD(g_GameTimers[i].current,0);FIELD(g_GameTimers[i].limit,0);}
FIELD(g_ActorEffectFlag100Timer,2);FIELD(g_FieldMoveLock,0);FIELD(g_GlobalFrameCounter[0],0);
FIELD(g_FrameRngCounter,0);
for(unsigned k=0;k<2;k++){GteMatrix *m=k?&g_RenderLightingBlock.color.matrix:&g_RenderLightingBlock.light.matrix;for(unsigned y=0;y<3;y++)for(unsigned x=0;x<3;x++){FIELD(m->m[y][x],3);}for(unsigned x=0;x<3;x++){FIELD(m->t[x],0);}}
for(unsigned y=0;y<3;y++)for(unsigned x=0;x<4;x++){FIELD(g_RenderLightingBlock.light_colors[y][x],1);}
{uint32_t count=out?g_FieldSfxQueueCount:get(in+at);if(count>28)return 0;FIELD(g_FieldSfxQueueCount,1);}
for(unsigned i=0;i<28;i++){FIELD(g_FieldSfxQueue[i].typeId,2);FIELD(g_FieldSfxQueue[i].subId,1);FIELD(g_FieldSfxQueue[i].taskArgument,1);FIELD(g_FieldSfxQueue[i].actorSelector.word,0);FIELD(g_FieldSfxQueue[i].taskValue,0);}
FIELD(g_SceneDispatchCur,0);
FIELD(g_SceneDispatchToken,0);
FIELD(g_FieldFxRandomPhaseCounter,2);FIELD(g_FieldFxLoadedTexturePage,3);
#undef FIELD
for(unsigned i=0;i<8;i++){uintptr_t pointer=(uintptr_t)g_BattleTargetList[i].actor,base=(uintptr_t)g_EntityPool;uint32_t id=!pointer?0:pointer>=base&&pointer-base<sizeof g_EntityPool&&(pointer-base)%sizeof(FieldActor)==0?(uint32_t)((pointer-base)/sizeof(FieldActor))+1:UINT32_MAX;if(out){if(id==UINT32_MAX)return 0;put(out+at,id);}else{id=get(in+at);if(id>14)return 0;if(apply)g_BattleTargetList[i].actor=id?(BattleEntity *)(void *)&g_EntityPool[id-1]:0;}at+=4;
#define TARGET_FIELD(value,kind) do{uint32_t v=out?(uint32_t)(value):get(in+at);if(!canonical(v,kind))return 0;if(out)put(out+at,v);else if(apply)(value)=(__typeof__(value))v;at+=4;}while(0)
TARGET_FIELD(g_BattleTargetList[i].dist,0);TARGET_FIELD(g_BattleTargetList[i].angle,3);
#undef TARGET_FIELD
}return at==1596;}
static int validate(void *ctx,const unsigned char *in,size_t n){(void)ctx;return in&&n==1596&&!memcmp(in,"PEGLOB02",8)&&bound(0)&&get(in+8)==(uint32_t)rootScene&&get(in+12)==(uint32_t)(rootScene>>32)&&get(in+16)==(uint32_t)rootAssets&&get(in+20)==(uint32_t)(rootAssets>>32)&&walk(in,0,0);}
static int capture(void *ctx,unsigned char *out,size_t cap,size_t *used){(void)ctx;if(used)*used=0;if(!out||!used||cap<1596||!bound(1))return 0;memcpy(out,"PEGLOB02",8);put(out+8,(uint32_t)rootScene);put(out+12,(uint32_t)(rootScene>>32));put(out+16,(uint32_t)rootAssets);put(out+20,(uint32_t)(rootAssets>>32));if(!walk(0,out,0))return 0;*used=1596;return 1;}
static int restore(void *ctx,const unsigned char *in,size_t n){if(!validate(ctx,in,n)||!walk(in,0,1))return 0;g_FieldPadPressed=0;return 1;}
int PeState_RegisterGameGlobals(PeStateRegistry *r){PeStateModule m={PE_STATE_GLOBALS,2,1596,capture,0,validate,restore,0};return PeState_Register(r,m);}
