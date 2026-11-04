#include "pe_state_theater_tasks.h"
#include "pe_state.h"
#include "pe_state_session.h"
#include "pe_state_assets.h"
#include "pe_state_render.h"
#include "pe_state_room_object.h"
#include "pe_state_theater.h"
#include "pe_state_checkpoint.h"
#include "pe_state_boundary.h"
#include <string.h>
#include <errno.h>
#include <SDL3/SDL.h>
#include "psyz/state_audio.h"
#include "psyz/state_producers.h"
#include "pe_state_files.h"
#include "pe_state_mod_contract.h"
extern int CPE_StateHostIdle(void);
extern unsigned CPE_StateHostReasonBits(void);
extern const char *CPE_StateHostReasonText(unsigned);
extern int PeStateScript_TransientIdle(void);
static PeStateBarrier cpu_barrier;
static int all_begin(void *ctx,uint64_t *epoch){(void)ctx;cpu_barrier=PeStateBoundary_CpuBarrier();if(!cpu_barrier.begin(cpu_barrier.context,epoch))return 0;if(!Psyz_StateKernelBegin()){cpu_barrier.end(cpu_barrier.context);return 0;}if(!Psyz_StateAudioBegin()){Psyz_StateKernelEnd();cpu_barrier.end(cpu_barrier.context);return 0;}return 1;}
static int all_unchanged(void *ctx,uint64_t epoch){(void)ctx;return Psyz_StateKernelOwned()&&Psyz_StateAudioOwned()&&cpu_barrier.unchanged(cpu_barrier.context,epoch);}
static void all_end(void *ctx){(void)ctx;Psyz_StateAudioEnd();Psyz_StateKernelEnd();cpu_barrier.end(cpu_barrier.context);}

int PeCpuStateSession_CurrentRoomBoundary(void){uint32_t native;if(!CPE_StateHostIdle())return PE_STATE_ROOM_HOST_OR_MODS;if(!PeStateScript_TransientIdle())return PE_STATE_ROOM_MOVIE_SCRIPT;int b=PeStateCheckpoint_Boundary();if(b)return b;if(!PeStateAssets_NativeDescriptors(&native))return PE_STATE_ROOM_ASSETS;PeScriptAsset asset;if(!PeStateAssets_Get(&asset))return PE_STATE_ROOM_ASSETS;if(!PeStateTheater_RoomSupported(asset.room,native))return PE_STATE_ROOM_NATIVE_OVERLAY;return 0;}
const char *PeCpuStateSession_RoomBoundaryText(int r){switch(r){case 0:
#if PE_NATIVE_STATE_CHANGED_COHORTS
return "Experimental checkpoint within this launch and room; supported actor changes, unchanged settings";
#else
return "Experimental same-room checkpoint; unchanged actors and settings only";
#endif
case 1:return "Resume gameplay to reach the checkpoint boundary";case 2:{const char*detail=PeStateTasks_Blocker();return detail?detail:"Wait for active effects or menu jobs to finish";}case 3:case 11:return "Finish the movie first";case 4:return "No player is active";case 10:return CPE_StateHostReasonText(CPE_StateHostReasonBits());case 12:return "This room has native event/effect code not yet serialized";case 16:return "Checkpoint file rejected or storage failed; inspect launch log";case 18:return "Saved actor layout unavailable or game resources changed; use a recent same-room capture from this launch";case 19:return "Normal save loaded; queued checkpoint cancelled and old layouts invalidated";case 20:return "Room changed; queued checkpoint cancelled and old layouts invalidated";case 17:return "Checkpoint rollback failed; restart before further checkpoint operations";case 13:return "Current room asset ownership is unavailable";default:return "A state owner or actor graph rejected this boundary";}}
int PeCpuStateSession_MetadataStatus(const PeCpuStateSession*s,PeStateMetadataStatus*out){if(!s||!out)return 0;memset(out,0,sizeof*out);
#if PE_NATIVE_STATE_CHANGED_COHORTS
    out->capacity=PE_STATE_GRAPH_ARCHIVE_SLOTS;out->reason=PeStateBoundary_InvalidationReason();uint64_t session=PeStateBoundary_Session(),scene=PeStateBoundary_SceneEpoch();out->stale=s->world.scene&&s->world.scene!=scene;
    for(unsigned i=0;i<PE_STATE_GRAPH_ARCHIVE_SLOTS;i++){const PeStateGraphRecord*r=&s->graphArchive.slots[i];if(r->serial&&r->world.session==session&&r->world.scene==scene)out->retained++;}
#endif
    return 1;}
void PeCpuStateSession_Init(PeCpuStateSession *s,const PeStateIdentity *identity){if(!s)return;memset(s,0,sizeof *s);if(identity)s->identity=*identity;s->result=PE_STATE_INCOMPLETE;}
static int register_owners(PeStateRegistry*registry,PeWorldState*world,PeBattleNumeric*battle){int r;
    memset(registry,0,sizeof *registry);
    r=PeState_RegisterGameGlobals(registry);if(r)return r;
    r=PeState_RegisterScriptAssetIdentity(registry);if(r)return r;
    r=PeState_RegisterWorldSubset(registry,world);if(r)return r;
    r=PeState_RegisterRenderGraph(registry,world);if(r)return r;
    /* Inline PM/mirror graph prepare follows typed actor reconstruction; rollback same order. */
#if PE_NATIVE_STATE_CHANGED_COHORTS
    r=PeState_RegisterCheckpointOwnersForGraph(registry,world);if(r)return r;
#else
    r=PeState_RegisterCheckpointOwners(registry);if(r)return r;
#endif
    r=PeState_RegisterBattleNumeric(registry,battle);if(r)return r;
    r=PeState_RegisterBiosRng(registry);if(r)return r;
    r=PeState_RegisterAkaoTracks(registry);if(r)return r;
    r=PeState_RegisterProducerState(registry);if(r)return r;
    r=PeState_RegisterAudioSubset(registry);if(r)return r;
    r=PeState_RegisterRoomObject(registry);if(r)return r;
    r=PeState_RegisterGeometry(registry);if(r)return r;
    r=PeState_RegisterNativeVram(registry);if(r)return r;
    r=PeState_RegisterNumericGte(registry);if(r)return r;
    return 0;
}
static int initialize(PeCpuStateSession *s){
    int r;
#if PE_NATIVE_STATE_CHANGED_COHORTS
    if(s->topologyFailed){s->boundaryReason=17;return PE_STATE_ROLLBACK_FAILED;}
    if(s->initialized&&s->world.scene==PeStateBoundary_SceneEpoch()&&PeWorld_IsCurrent(&s->world)&&PeBattleNumeric_IsCurrent(&s->battle))return 0;
    if(s->world.scene!=PeStateBoundary_SceneEpoch())PeStateGraphArchive_Init(&s->graphArchive);
#else
    if(s->initialized&&s->world.scene==PeStateBoundary_SceneEpoch())return 0;
#endif
    s->initialized=0;
    PeWorld_Init(&s->world);r=PeWorld_BindLoadedScene(&s->world);if(r)return r;
    r=PeBattleNumeric_Bind(&s->battle);if(r)return r;
    r=register_owners(&s->registry,&s->world,&s->battle);if(r)return r;
    s->initialized=1;return 0;
}
static int restore_components(PeCpuStateSession*s,const PeStateBarrier*barrier,const unsigned char*in,size_t bytes){
#if PE_NATIVE_STATE_CHANGED_COHORTS
    int r=PeStateGraphArchive_Select(&s->graphArchive,&s->identity,in,bytes,&s->targetWorld,&s->targetBattle);if(r){s->boundaryReason=18;SDL_Log("[native checkpoint] graph_archive=select result=%d same_run_descriptors=unavailable",r);return r;}
    r=register_owners(&s->targetRegistry,&s->targetWorld,&s->targetBattle);if(r)return r;
    PeStateGraphContext target={&s->targetRegistry,&s->targetWorld,&s->targetBattle},current={&s->registry,&s->world,&s->battle};
    r=PeStateGraph_RestorePair(&target,&current,barrier,&s->identity,in,bytes,s->scratch,s->scratchBytes);
    if(!r)s->initialized=0;else if(r==PE_STATE_ROLLBACK_FAILED){s->topologyFailed=1;s->boundaryReason=17;}return r;
#else
    return PeState_RestoreComponents(&s->registry,barrier,&s->identity,in,bytes,s->scratch,s->scratchBytes);
#endif
}
static void service(void *context){
    PeCpuStateSession *s=context;int mode=s->pending,attemptedRestore=0;PeStateBarrier barrier={0,all_begin,all_unchanged,all_end};
    PeState_ClearFailure();s->used=0;s->boundaryReason=PeCpuStateSession_CurrentRoomBoundary();s->result=s->boundaryReason?PE_STATE_INCOMPLETE:initialize(s);
    if(s->result&&!s->boundaryReason)s->boundaryReason=PE_STATE_ROOM_OWNERS;
    if(!s->result){
        if(mode==1||mode==3)s->result=PeState_CapturePartial(&s->registry,&barrier,&s->identity,s->output,s->outputBytes,&s->used);
        else if(mode==4){errno=0;s->result=PeStateFiles_Read(s->stateDirectory,s->stateFile,s->output,s->outputBytes,&s->used,&s->identity);if(s->result){int error=errno;s->boundaryReason=16;SDL_Log("[native checkpoint] file_stage=read-validate result=%d errno=%d",s->result,error);}if(!s->result){attemptedRestore=1;s->result=restore_components(s,&barrier,s->output,s->used);}}
        else if(mode==2){attemptedRestore=1;s->result=restore_components(s,&barrier,s->input,s->inputBytes);}
        else s->result=PE_STATE_INVALID;
    }
#if PE_NATIVE_STATE_CHANGED_COHORTS
    if(!s->result&&mode==1)s->result=PeStateGraphArchive_Remember(&s->graphArchive,&s->world,&s->battle,&s->identity,s->output,s->used);
#endif
    if(s->result){PeStateFailure f=PeState_LastFailure();if(f.stage)SDL_Log("[native checkpoint] component-failure module=%u owner=%s stage=%s rollback-module=%u rollback-owner=%s rollback-stage=%s result=%d",f.module,PeState_FailureOwnerText(f.module),PeState_FailureStageText(f.stage),f.rollbackModule,PeState_FailureOwnerText(f.rollbackModule),PeState_FailureStageText(f.rollbackStage),s->result);}
    if(attemptedRestore)pe_mod_state_after_restore();
    if(!s->result&&mode==3){errno=0;s->result=PeStateFiles_WriteUnique(s->stateDirectory,s->output,s->used,&s->identity,s->stateFile,sizeof s->stateFile);if(s->result){int error=errno;s->boundaryReason=16;SDL_Log("[native checkpoint] file_stage=write-data-and-commit result=%d errno=%d",s->result,error);}}
#if PE_NATIVE_STATE_CHANGED_COHORTS
    /* Publish retained descriptors only AFTER unique data/commit pair succeeds.
     * Failed file save cannot evict metadata for an older valid checkpoint. */
    if(!s->result&&mode==3){s->result=PeStateGraphArchive_Remember(&s->graphArchive,&s->world,&s->battle,&s->identity,s->output,s->used);if(s->result){s->boundaryReason=18;SDL_Log("[native checkpoint] graph_archive=publish-after-commit result=%d",s->result);}}
#endif
    if(s->result&&!s->boundaryReason)s->boundaryReason=PE_STATE_ROOM_OWNERS;
    s->output=0;s->input=0;s->scratch=0;s->pending=0;
}
static void cancelled(void *context){PeCpuStateSession *s=context;
#if PE_NATIVE_STATE_CHANGED_COHORTS
    PeStateGraphArchive_Init(&s->graphArchive);s->initialized=0;
#endif
    unsigned reason=PeStateBoundary_InvalidationReason();s->boundaryReason=reason==PE_STATE_INVALIDATE_NORMAL_LOAD?PE_STATE_ROOM_NORMAL_SAVE_REPLACED:PE_STATE_ROOM_SCENE_REPLACED;s->used=0;s->result=PE_STATE_INCOMPATIBLE;s->pending=0;s->output=0;s->input=0;s->scratch=0;}
int PeCpuStateSession_RequestCapture(PeCpuStateSession *s,unsigned char *out,size_t bytes){
    if(!s||s->pending||!out||!bytes||bytes>PE_STATE_MAX_BYTES)return PE_STATE_INVALID;
#if PE_NATIVE_STATE_CHANGED_COHORTS
    if(s->topologyFailed){s->boundaryReason=17;return PE_STATE_ROLLBACK_FAILED;}
#endif
    s->pending=1;s->output=out;s->outputBytes=bytes;s->result=PE_STATE_BUSY;s->used=0;
    int result=PeStateBoundary_RequestWithCancel(service,cancelled,s);if(result){s->pending=0;s->output=0;s->result=result;}return result;
}
int PeCpuStateSession_RequestRestoreComponents(PeCpuStateSession *s,const unsigned char *input,size_t bytes,unsigned char *scratch,size_t scratchBytes){
    if(!s||s->pending||!input||!bytes||bytes>PE_STATE_MAX_BYTES||!scratch||!scratchBytes||scratchBytes>PE_STATE_MAX_BYTES)return PE_STATE_INVALID;
#if PE_NATIVE_STATE_CHANGED_COHORTS
    if(s->topologyFailed){s->boundaryReason=17;return PE_STATE_ROLLBACK_FAILED;}
#endif
    s->pending=2;s->input=input;s->inputBytes=bytes;s->scratch=scratch;s->scratchBytes=scratchBytes;s->result=PE_STATE_BUSY;s->used=0;
    int result=PeStateBoundary_RequestWithCancel(service,cancelled,s);if(result){s->pending=0;s->input=0;s->scratch=0;s->result=result;}return result;
}
int PeCpuStateSession_FullLoadAvailable(void){return 0;}

int PeCpuStateSession_RequestCaptureFile(PeCpuStateSession *s,unsigned char *out,size_t bytes,const char *directory){if(!s||!directory||!directory[0]||strlen(directory)>=sizeof s->stateDirectory)return PE_STATE_INVALID;int result=PeCpuStateSession_RequestCapture(s,out,bytes);if(result)return result;memcpy(s->stateDirectory,directory,strlen(directory)+1);s->stateFile[0]=0;s->pending=3;return 0;}
int PeCpuStateSession_RequestRestoreComponentsFile(PeCpuStateSession *s,unsigned char *out,size_t bytes,unsigned char *scratch,size_t scratchBytes,const char *directory,const char *name){if(!s||!directory||!directory[0]||strlen(directory)>=sizeof s->stateDirectory||!name||strlen(name)!=34||!scratch||!scratchBytes||scratchBytes>PE_STATE_MAX_BYTES)return PE_STATE_INVALID;int result=PeCpuStateSession_RequestCapture(s,out,bytes);if(result)return result;memcpy(s->stateDirectory,directory,strlen(directory)+1);memcpy(s->stateFile,name,35);s->scratch=scratch;s->scratchBytes=scratchBytes;s->pending=4;return 0;}
