/* Project-authored explicit serialization foundation; no process/heap dump. */
#ifndef PE_STATE_H
#define PE_STATE_H
#include <stddef.h>
#include <stdint.h>
#define PE_STATE_MAX_BYTES (8u*1024u*1024u)
#define PE_STATE_MAX_CHUNKS 16u
#define PE_STATE_HEADER_BYTES 100u
#define PE_STATE_CHECKPOINT 1u
#define PE_STATE_PARTIAL 2u
#define PE_STATE_ALL_RUNTIME 0xffffeu /* IDs2..20, explicit restore modules still absent */
enum PeStateId { PE_STATE_ORIGINAL_SAVE=1,PE_STATE_RNG,PE_STATE_SCENE,PE_STATE_SCRIPTS,PE_STATE_ACTORS,PE_STATE_BATTLE,PE_STATE_SCHEDULER,PE_STATE_AUDIO,PE_STATE_GPU,PE_STATE_EVENTS,PE_STATE_DISC,PE_STATE_MODS,PE_STATE_HOST,PE_STATE_GTE,PE_STATE_ASSETS,PE_STATE_AKAO,PE_STATE_PERSISTENT,PE_STATE_GLOBALS,PE_STATE_PM,PE_STATE_GEOMETRY };
enum PeStateResult { PE_STATE_OK=0,PE_STATE_INVALID=-1,PE_STATE_BOUNDS=-2,PE_STATE_INCOMPATIBLE=-3,PE_STATE_BUSY=-4,PE_STATE_INCOMPLETE=-5,PE_STATE_CHECKSUM=-6,PE_STATE_ROLLBACK_FAILED=-7 };
typedef struct PeStateChunk {uint32_t id,schema;const unsigned char *data;size_t bytes;} PeStateChunk;
typedef struct PeStateIdentity {unsigned char build[32],disc[32];} PeStateIdentity;
typedef struct PeStateInfo {uint32_t kind,count,presentMask,missingRuntimeMask;uint64_t epoch;int arbitraryFrameLoadAvailable;} PeStateInfo;
typedef int (*PeStateCapture)(void *context,unsigned char *out,size_t capacity,size_t *used);
typedef int (*PeStateValidate)(void *,const unsigned char *,size_t);
typedef int (*PeStateRestore)(void *,const unsigned char *,size_t);
/* Optional graph preparation runs only after every target/rollback image validates.
 * mode -1 is read-only preflight; 0 prepares target, 1 prepares rollback.
 * Return1 success,0 clean rejection,-1 failure after possible mutation.
 * Rollback preparation must not require current topology. */
typedef int (*PeStatePrepare)(void *,const unsigned char *,size_t,int mode);
typedef struct PeStateModule {uint32_t id,schema;size_t maximumBytes;PeStateCapture capture;void *context;PeStateValidate validate;PeStateRestore restore;PeStatePrepare prepare;} PeStateModule;
typedef struct PeStateRegistry {PeStateModule modules[PE_STATE_MAX_CHUNKS];unsigned count;} PeStateRegistry;
typedef struct PeStateBarrier {void *context;int (*begin)(void *,uint64_t *);int (*unchanged)(void *,uint64_t);void (*end)(void *);} PeStateBarrier;
/* Bounded diagnostic witness only: never used to authorize or change restoration. */
typedef struct PeStateFailure{uint32_t module,stage,rollbackModule,rollbackStage;}PeStateFailure;
void PeState_ClearFailure(void);
PeStateFailure PeState_LastFailure(void);
const char*PeState_FailureStageText(uint32_t);
const char*PeState_FailureOwnerText(uint32_t);
int PeState_Register(PeStateRegistry *,PeStateModule);
int PeState_RegisterBiosRng(PeStateRegistry *);
int PeState_RegisterAkaoTracks(PeStateRegistry *);
int PeState_RegisterProducerState(PeStateRegistry *);
int PeState_RegisterAudioSubset(PeStateRegistry *);
int PeState_RegisterNativeVram(PeStateRegistry *);
int PeState_RegisterNumericGte(PeStateRegistry *);
/* Explicit partial COMPONENT transaction only; never restores an arbitrary game frame. */
/* Distinct retained target and CURRENT rollback contexts. Exact module order,
 * schema, size and mutation callbacks must match. Both graphs preflight before
 * any write. Caller owns contexts/buffers for the entire cooperative barrier.
 * This generic primitive does not authorize a changed-cohort game load. */
int PeState_RestorePairedComponents(const PeStateRegistry *,const PeStateRegistry *,const PeStateBarrier *,const PeStateIdentity *,const unsigned char *,size_t,unsigned char *,size_t);
int PeState_RestoreComponents(const PeStateRegistry *,const PeStateBarrier *,const PeStateIdentity *,const unsigned char *,size_t,unsigned char *,size_t);
uint32_t PeState_MissingRuntime(const PeStateRegistry *);
int PeState_Encode(unsigned char *,size_t,size_t *,uint32_t,uint64_t,const PeStateIdentity *,const PeStateChunk *,unsigned);
int PeState_Inspect(const unsigned char *,size_t,const PeStateIdentity *,PeStateInfo *);
/* Caller supplies a real cooperative game/audio/GPU barrier. Not hooked to runtime. */
int PeState_CapturePartial(const PeStateRegistry *,const PeStateBarrier *,const PeStateIdentity *,unsigned char *,size_t,size_t *);
/* Only validates an existing original save block; never claims arbitrary-frame state. */
int PeState_OriginalCheckpoint(const unsigned char *,size_t,PeStateChunk *);
#endif
