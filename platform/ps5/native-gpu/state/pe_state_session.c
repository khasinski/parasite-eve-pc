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
const char *PeCpuStateSession_RoomBoundaryText(int r){switch(r){case 0:return "Experimental same-room checkpoint; unchanged actors and settings only";case 1:return "Resume gameplay to reach the checkpoint boundary";case 2:return "Wait for active effects or menu jobs to finish";case 3:case 11:return "Finish the movie first";case 4:return "No player is active";case 10:return CPE_StateHostReasonText(CPE_StateHostReasonBits());case 12:return "This room has native event/effect code not yet serialized";case 16:return "Checkpoint file rejected or storage failed; inspect launch log";case 13:return "Current room asset ownership is unavailable";default:return "A state owner or actor graph rejected this boundary";}}
void PeCpuStateSession_Init(PeCpuStateSession *s,const PeStateIdentity *identity){if(!s)return;memset(s,0,sizeof *s);if(identity)s->identity=*identity;s->result=PE_STATE_INCOMPLETE;}
static int initialize(PeCpuStateSession *s){
    int r;if(s->initialized&&s->world.scene==PeStateBoundary_SceneEpoch())return 0;s->initialized=0;
    PeWorld_Init(&s->world);r=PeWorld_BindLoadedScene(&s->world);if(r)return r;
    r=PeBattleNumeric_Bind(&s->battle);if(r)return r;
    memset(&s->registry,0,sizeof s->registry);
    r=PeState_RegisterGameGlobals(&s->registry);if(r)return r;
    r=PeState_RegisterScriptAssetIdentity(&s->registry);if(r)return r;
    r=PeState_RegisterWorldSubset(&s->registry,&s->world);if(r)return r;
    r=PeState_RegisterRenderGraph(&s->registry,&s->world);if(r)return r;
    /* Inline PM/mirror graph prepare follows typed actor reconstruction; rollback same order. */
    r=PeState_RegisterCheckpointOwners(&s->registry);if(r)return r;
    r=PeState_RegisterBattleNumeric(&s->registry,&s->battle);if(r)return r;
    r=PeState_RegisterBiosRng(&s->registry);if(r)return r;
    r=PeState_RegisterAkaoTracks(&s->registry);if(r)return r;
    r=PeState_RegisterProducerState(&s->registry);if(r)return r;
    r=PeState_RegisterAudioSubset(&s->registry);if(r)return r;
    r=PeState_RegisterRoomObject(&s->registry);if(r)return r;
    r=PeState_RegisterGeometry(&s->registry);if(r)return r;
    r=PeState_RegisterNativeVram(&s->registry);if(r)return r;
    r=PeState_RegisterNumericGte(&s->registry);if(r)return r;
    s->initialized=1;return 0;
}
static void service(void *context){
    PeCpuStateSession *s=context;int mode=s->pending,attemptedRestore=0;PeStateBarrier barrier={0,all_begin,all_unchanged,all_end};
    s->used=0;s->boundaryReason=PeCpuStateSession_CurrentRoomBoundary();s->result=s->boundaryReason?PE_STATE_INCOMPLETE:initialize(s);
    if(s->result&&!s->boundaryReason)s->boundaryReason=PE_STATE_ROOM_OWNERS;
    if(!s->result){
        if(mode==1||mode==3)s->result=PeState_CapturePartial(&s->registry,&barrier,&s->identity,s->output,s->outputBytes,&s->used);
        else if(mode==4){errno=0;s->result=PeStateFiles_Read(s->stateDirectory,s->stateFile,s->output,s->outputBytes,&s->used,&s->identity);if(s->result){int error=errno;s->boundaryReason=16;SDL_Log("[native checkpoint] file_stage=read-validate result=%d errno=%d",s->result,error);}if(!s->result){attemptedRestore=1;s->result=PeState_RestoreComponents(&s->registry,&barrier,&s->identity,s->output,s->used,s->scratch,s->scratchBytes);}}
        else if(mode==2){attemptedRestore=1;s->result=PeState_RestoreComponents(&s->registry,&barrier,&s->identity,s->input,s->inputBytes,s->scratch,s->scratchBytes);}
        else s->result=PE_STATE_INVALID;
    }
    if(attemptedRestore)pe_mod_state_after_restore();
    if(!s->result&&mode==3){errno=0;s->result=PeStateFiles_WriteUnique(s->stateDirectory,s->output,s->used,&s->identity,s->stateFile,sizeof s->stateFile);if(s->result){int error=errno;s->boundaryReason=16;SDL_Log("[native checkpoint] file_stage=write-data-and-commit result=%d errno=%d",s->result,error);}}
    if(s->result&&!s->boundaryReason)s->boundaryReason=PE_STATE_ROOM_OWNERS;
    s->output=0;s->input=0;s->scratch=0;s->pending=0;
}
static void cancelled(void *context){PeCpuStateSession *s=context;s->used=0;s->result=PE_STATE_INCOMPATIBLE;s->pending=0;s->output=0;s->input=0;s->scratch=0;}
int PeCpuStateSession_RequestCapture(PeCpuStateSession *s,unsigned char *out,size_t bytes){
    if(!s||s->pending||!out||!bytes||bytes>PE_STATE_MAX_BYTES)return PE_STATE_INVALID;
    s->pending=1;s->output=out;s->outputBytes=bytes;s->result=PE_STATE_BUSY;s->used=0;
    int result=PeStateBoundary_RequestWithCancel(service,cancelled,s);if(result){s->pending=0;s->output=0;s->result=result;}return result;
}
int PeCpuStateSession_RequestRestoreComponents(PeCpuStateSession *s,const unsigned char *input,size_t bytes,unsigned char *scratch,size_t scratchBytes){
    if(!s||s->pending||!input||!bytes||bytes>PE_STATE_MAX_BYTES||!scratch||!scratchBytes||scratchBytes>PE_STATE_MAX_BYTES)return PE_STATE_INVALID;
    s->pending=2;s->input=input;s->inputBytes=bytes;s->scratch=scratch;s->scratchBytes=scratchBytes;s->result=PE_STATE_BUSY;s->used=0;
    int result=PeStateBoundary_RequestWithCancel(service,cancelled,s);if(result){s->pending=0;s->input=0;s->scratch=0;s->result=result;}return result;
}
int PeCpuStateSession_FullLoadAvailable(void){return 0;}

int PeCpuStateSession_RequestCaptureFile(PeCpuStateSession *s,unsigned char *out,size_t bytes,const char *directory){if(!s||!directory||!directory[0]||strlen(directory)>=sizeof s->stateDirectory)return PE_STATE_INVALID;int result=PeCpuStateSession_RequestCapture(s,out,bytes);if(result)return result;memcpy(s->stateDirectory,directory,strlen(directory)+1);s->stateFile[0]=0;s->pending=3;return 0;}
int PeCpuStateSession_RequestRestoreComponentsFile(PeCpuStateSession *s,unsigned char *out,size_t bytes,unsigned char *scratch,size_t scratchBytes,const char *directory,const char *name){if(!s||!directory||!directory[0]||strlen(directory)>=sizeof s->stateDirectory||!name||strlen(name)!=34||!scratch||!scratchBytes||scratchBytes>PE_STATE_MAX_BYTES)return PE_STATE_INVALID;int result=PeCpuStateSession_RequestCapture(s,out,bytes);if(result)return result;memcpy(s->stateDirectory,directory,strlen(directory)+1);memcpy(s->stateFile,name,35);s->scratch=scratch;s->scratchBytes=scratchBytes;s->pending=4;return 0;}
