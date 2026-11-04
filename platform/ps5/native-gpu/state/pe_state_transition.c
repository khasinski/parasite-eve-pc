/* SPDX-License-Identifier: MIT. No process pointers serialized or epoch rewind. */
#include "pe_state_transition.h"
#include "pe_state_actor_prepare.h"
#include <string.h>
static uint32_t word(const unsigned char*p){return(uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static int linked(const PeStateGraphContext*g){if(!g||!g->registry||!g->world||!g->battle||g->registry->count>PE_STATE_MAX_CHUNKS)return 0;unsigned found=0;for(unsigned i=0;i<g->registry->count;i++){const PeStateModule*m=&g->registry->modules[i];if(m->id==PE_STATE_SCENE){if(m->context!=g->world)return 0;found|=1;}if(m->id==PE_STATE_ACTORS){if(m->context!=g->world)return 0;found|=2;}if(m->id==PE_STATE_BATTLE){if(m->context!=g->battle)return 0;found|=4;}}return found==7;}
static int same_binding(const PeStateGraphContext*g){return g->world->session==g->battle->session&&g->world->scene==g->battle->scene&&g->world->assetGeneration==g->battle->assetGeneration&&g->world->serialCounter==g->battle->entitySerialCounter&&g->world->liveCount==g->battle->entityLiveCount;}
int PeStateGraph_RestorePair(PeStateGraphContext*target,PeStateGraphContext*current,const PeStateBarrier*barrier,const PeStateIdentity*identity,const unsigned char*input,size_t bytes,unsigned char*scratch,size_t scratchBytes){
    if(!linked(target)||!linked(current)||target==current||target->world==current->world||target->battle==current->battle||!same_binding(target)||!same_binding(current)||target->world->session!=current->world->session||target->world->scene!=current->world->scene||target->world->assetGeneration!=current->world->assetGeneration||target->world->geometryOwner!=current->world->geometryOwner||target->world->storyDay!=current->world->storyDay||!PeWorld_IsCurrent(current->world)||!PeBattleNumeric_IsCurrent(current->battle))return PE_STATE_INCOMPATIBLE;
    PeStateInfo info;int result=PeState_Inspect(input,bytes,identity,&info);if(result)return result;if(info.kind!=PE_STATE_PARTIAL)return PE_STATE_INCOMPLETE;
    const unsigned char*scene=0,*render=0,*battle=0;size_t sceneBytes=0,renderBytes=0,battleBytes=0,at=PE_STATE_HEADER_BYTES;
    /* Inspect verified bounds/CRC/duplicates before these chunk reads. */
    for(unsigned i=0;i<info.count;i++){uint32_t id=word(input+at),n=word(input+at+8);if(id==PE_STATE_SCENE){scene=input+at+16;sceneBytes=n;}else if(id==PE_STATE_ACTORS){render=input+at+16;renderBytes=n;}else if(id==PE_STATE_BATTLE){battle=input+at+16;battleBytes=n;}at+=16+n;}
    if(!scene||!render||!battle||!PeStateActor_PreflightGraph(target->world,scene,sceneBytes,render,renderBytes))return PE_STATE_INCOMPATIBLE;
    uint32_t targetRoles[14]={0},currentRoles[14]={0};for(unsigned i=0;i<14;i++){if(target->world->liveMask&(1u<<i))targetRoles[i]=target->world->actor[i].coreRole;if(current->world->liveMask&(1u<<i))currentRoles[i]=current->world->actor[i].coreRole;}
    PeBattleNumeric targetBefore=*target->battle,currentBefore=*current->battle;uint32_t targetMode=target->world->targetMode,currentMode=current->world->targetMode;
    if(!PeBattleNumeric_SetTargetGraph(target->battle,target->world->liveMask,targetRoles)||!PeBattleNumeric_SetTargetGraph(current->battle,current->world->liveMask,currentRoles)||!PeBattleNumeric_PreflightTarget(target->battle,battle,battleBytes,target->world->liveMask,targetRoles)){*target->battle=targetBefore;*current->battle=currentBefore;return PE_STATE_INCOMPATIBLE;}
    target->world->targetMode=current->world->targetMode=1;
    /* Generic engine captures CURRENT rollback payloads under its cooperative
     * barrier, validates every owner and BOTH constructor preflights, then writes.
     * Dirty target failure reconstructs/restores through CURRENT contexts. */
    result=PeState_RestorePairedComponents(target->registry,current->registry,barrier,identity,input,bytes,scratch,scratchBytes);
    target->world->targetMode=targetMode;current->world->targetMode=currentMode;*target->battle=targetBefore;*current->battle=currentBefore;return result;
}
