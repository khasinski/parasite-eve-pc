#ifndef PE_STATE_RENDER_H
#define PE_STATE_RENDER_H
#include "pe_state.h"
#include "pe_state_world.h"
#include "pe1/render_object.h"
void PeStateRender_Configured(RenderObjectEntity *,RenderObjectHeader *,unsigned char *,short,short,short,short,short,int);
typedef struct PeActorConstruction {uint32_t alive,type,sub,full,slot,blocks,packetOffset;int32_t x,y,unused,palette,init;uint32_t generationLo,generationHi;} PeActorConstruction;
typedef struct PeActorConstructionPlan {uint32_t liveMask,usedBlockMask;PeActorConstruction actor[14];} PeActorConstructionPlan;
/* Pure same-room source/slot preflight; does NOT destructively recreate actors. */
int PeStateRender_GetConstructionPlan(const unsigned char *,size_t,PeActorConstructionPlan *);
uint64_t PeStateRender_Revision(void);
int PeStateRender_CurrentCandidateValid(const unsigned char *,size_t);
int PeStateRender_BeginConstruction(void);
void PeStateRender_EndConstruction(void);
int PeStateRender_InstallConstructionBindings(const PeActorConstructionPlan *);
int PeState_RegisterRenderGraph(PeStateRegistry *,PeWorldState *);
#endif
