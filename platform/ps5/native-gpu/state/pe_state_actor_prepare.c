#include "pe_state_actor_prepare.h"
#include "pe_state_render.h"
#include "pe_state_checkpoint.h"
#include "pe_state_lifetime.h"
#include "pe_state_boundary.h"
#include "psyz/state_audio.h"
#include "psyz/state_producers.h"
#include "pe1/entity_allocation.h"
#include "pe1/entity_frame_update.h"
#include "pe1/render_buffers.h"
#include "pe1/script_node.h"
static unsigned reservationSlot,reservationBlocks;
int PeStateActor_ConsumeAllocation(uint32_t bytes,void **out){if(!PeStateResource_IsRebuilding())return 0;if(!PeStateBoundary_AtCpuPoint()||!Psyz_StateAudioOwned()||!Psyz_StateKernelOwned())return -1;if(!out||!reservationSlot||reservationSlot>16||!g_RenderScratchBufferBase[0])return -1;unsigned blocks=bytes<=ENTITY_ALLOCATION_BLOCK_SIZE?1:bytes<=2*ENTITY_ALLOCATION_BLOCK_SIZE?2:bytes<=4*ENTITY_ALLOCATION_BLOCK_SIZE?4:8;if(bytes>8*ENTITY_ALLOCATION_BLOCK_SIZE||blocks!=reservationBlocks||reservationSlot-1+blocks>16)return -1;for(unsigned i=reservationSlot-1;i<reservationSlot-1+blocks;i++)if(g_EntityAllocationTable[i].address)return -1;uintptr_t address=(uintptr_t)(g_RenderScratchBufferBase[0]+(size_t)(reservationSlot-1)*ENTITY_ALLOCATION_BLOCK_SIZE);g_EntityAllocationTable[reservationSlot-1]=(EntityAllocationBlock){blocks,address};*out=(void *)address;reservationSlot=reservationBlocks=0;return 1;}
static int take_free(unsigned index){FieldActor *target=&g_EntityPool[index],*previous=0,*node=g_EntityFreeListHead;unsigned count=0;while(node&&node!=target){if(++count>14)return 0;previous=node;node=node->next;}if(!node)return 0;if(previous){previous->next=node->next;node->next=g_EntityFreeListHead;g_EntityFreeListHead=node;}return 1;}
int PeStateActor_Prepare(void *context,const unsigned char *input,size_t bytes,int rollback){PeWorldState*owner=context;if(!owner)return 0;if(rollback<=0&&PeStateCheckpoint_Boundary())return 0;if(rollback==0&&!owner->targetMode&&!PeStateRender_CurrentCandidateValid(input,bytes))return 0;PeWorldState *world=context;PeActorConstructionPlan plan;if(!world||!PeStateRender_GetConstructionPlan(input,bytes,&plan))return 0;uint32_t types[14];for(unsigned i=0;i<14;i++){types[i]=plan.actor[i].type;if((plan.liveMask&(1u<<i))&&plan.actor[i].sub!=world->actor[i].sub)return 0;}if(!PeWorld_ValidateTargetConstruction(world,types,plan.liveMask)||(!world->targetMode&&!PeWorld_ValidateEquivalentConstruction(world,types,plan.liveMask)))return 0;if(rollback<0)return 1;
/* Scope is explicitly same known actor set. Changed allocation/actor lineage is still rejected by initial module validators. */
if(!PeStateRender_BeginConstruction())return 0;if(!PeStateResource_BeginEquivalentRebuild()){PeStateRender_EndConstruction();return 0;}int ok=1;reservationSlot=reservationBlocks=0;
Entity_InitFreePool();Script_InitNodeFreeList();for(unsigned i=0;i<72;i++)g_ScriptNodes[i].flags|=0x10;
for(unsigned i=0;i<14&&ok;i++)if(plan.liveMask&(1u<<i)){const PeActorConstruction *p=&plan.actor[i];if(!take_free(i)){ok=0;break;}unsigned char descriptor[2]={(unsigned char)p->type,(unsigned char)p->sub};reservationSlot=p->slot;reservationBlocks=p->blocks;FieldActor *actor=Scene_LoadMap(descriptor,0,(int)p->full);if(actor!=&g_EntityPool[i]||reservationSlot){ok=0;break;}}
if(ok)ok=PeStateRender_InstallConstructionBindings(&plan)&&PeWorld_RestoreConstructorBindings(world);
reservationSlot=reservationBlocks=0;PeStateResource_EndEquivalentRebuild();PeStateRender_EndConstruction();return ok?1:-1;}

/* Read-only BOTH-graph prerequisite; no source clock rewind or live graph teardown. */
static int target_graph(PeWorldState*world,const unsigned char*numeric,size_t numericBytes,const unsigned char*render,size_t renderBytes){PeActorConstructionPlan plan;if(!PeWorld_PreflightTarget(world,numeric,numericBytes)||!PeStateRender_PreflightTarget(render,renderBytes)||!PeStateRender_GetConstructionPlan(render,renderBytes,&plan))return 0;uint32_t types[14];for(unsigned i=0;i<14;i++){types[i]=plan.actor[i].type;if((plan.liveMask&(1u<<i))&&plan.actor[i].sub!=world->actor[i].sub)return 0;}return PeWorld_ValidateTargetConstruction(world,types,plan.liveMask);}
int PeStateActor_PreflightGraph(PeWorldState*w,const unsigned char*n,size_t nb,const unsigned char*r,size_t rb){return target_graph(w,n,nb,r,rb);}
int PeStateActor_PreflightTransition(PeWorldState*target,const unsigned char*targetNumeric,size_t targetNumericBytes,const unsigned char*targetRender,size_t targetRenderBytes,PeWorldState*rollback,const unsigned char*rollbackNumeric,size_t rollbackNumericBytes,const unsigned char*rollbackRender,size_t rollbackRenderBytes){if(!target||!rollback||target->session!=rollback->session||target->scene!=rollback->scene||target->assetGeneration!=rollback->assetGeneration||target->geometryOwner!=rollback->geometryOwner||target->storyDay!=rollback->storyDay)return 0;return target_graph(target,targetNumeric,targetNumericBytes,targetRender,targetRenderBytes)&&target_graph(rollback,rollbackNumeric,rollbackNumericBytes,rollbackRender,rollbackRenderBytes);}
