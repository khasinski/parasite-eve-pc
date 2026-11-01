#ifndef PE_STATE_ACTOR_PREPARE_H
#define PE_STATE_ACTOR_PREPARE_H
#include "pe_state.h"
#include "pe_state_world.h"
#include "pe_state_world.h"
#include "pe_state_world.h"
#include "pe_state_world.h"
int PeStateActor_ConsumeAllocation(uint32_t,void **);
int PeStateActor_Prepare(void *,const unsigned char *,size_t,int rollback);
/* Future changed-cohort transaction preflight: both retained source-bound graphs,
 * target and current rollback images, validated BEFORE any constructor runs.
 * This does not bypass current equivalent-cohort guards or enable new loads. */
int PeStateActor_PreflightGraph(PeWorldState*,const unsigned char*,size_t,const unsigned char*,size_t);
int PeStateActor_PreflightTransition(PeWorldState*,const unsigned char*,size_t,const unsigned char*,size_t,PeWorldState*,const unsigned char*,size_t,const unsigned char*,size_t);
#endif
