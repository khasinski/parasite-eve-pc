#ifndef PE_STATE_WORLD_H
#define PE_STATE_WORLD_H
#include "pe_state.h"
#include "pe1/field_actor.h"
#define PE_WORLD_REGIONS 32u
#define PE_WORLD_MAX_BYTES 65536u
typedef struct PeWorldRegion {uint32_t id,bytes,crc;const unsigned char *base;} PeWorldRegion;
typedef struct PeWorldBinding {
    const void *state,*model,*header,*action,*script;
    void (*frame)(FieldActor *);
    void (*contact)(FieldActor *,int,FieldActor *,int);
    uint16_t serial;
    uint32_t coreRole,frameRole,contactRole,type,sub;
} PeWorldBinding;
enum PeWorldReason {PE_WORLD_READY=0,PE_WORLD_UNBOUND=1,PE_WORLD_UNMAPPED_SCRIPT=2,PE_WORLD_UNCLASSIFIED_UNION=3,PE_WORLD_GRAPH_OR_COMPATIBILITY=4,PE_WORLD_BOUNDS=5};
typedef struct PeWorldSourceRole{uint32_t source,offset;uint64_t epoch;}PeWorldSourceRole;

typedef struct PeWorldState {
    uint64_t session,scene,binding,assetGeneration,resourceEpoch;
    uint32_t serialCounter,liveMask,lastError,constructorPlayerId,targetMode;uint16_t liveCount;
    PeWorldBinding actor[14];
    void *bankRows[10][48];
    const void *geometryOwner;
    PeWorldSourceRole bankSources[10],rowSources[10][48],collisionSource;
    const void*collisionBase;uint32_t collisionCount,collisionStride,collisionCrc,storyDay;
    PeWorldRegion regions[PE_WORLD_REGIONS];unsigned regionCount;size_t regionBytes;
} PeWorldState;
/* Explicit owner-supplied immutable SCRIPT asset spans; never guessed lengths. */
void PeWorld_Init(PeWorldState *);
int PeWorld_RegisterScriptRegion(PeWorldState *,uint32_t,const unsigned char *,size_t);
/* Called on the real CPU boundary; fixes same-live-binding compatibility. */
int PeWorld_BindCurrentScene(PeWorldState *);
int PeWorld_BindLoadedScene(PeWorldState *);
/* Source-known native role IDs, not addresses or inferred callback classes. */
int PeWorld_NativeActorRoles(const FieldActor*,uint32_t*,uint32_t*,uint32_t*);
int PeWorld_IsCurrent(const PeWorldState*);
int PeWorld_ValidateTargetConstruction(const PeWorldState*,const uint32_t types[14],uint32_t mask);
int PeWorld_ValidateEquivalentConstruction(const PeWorldState *,const uint32_t types[14],uint32_t mask);
int PeWorld_RestoreConstructorBindings(PeWorldState *);
/* Pure target graph parse against retained SAME room/source metadata; no live actor set required. */
int PeWorld_PreflightTarget(PeWorldState*,const unsigned char*,size_t);
int PeState_RegisterWorldSubset(PeStateRegistry *,PeWorldState *);
#endif
