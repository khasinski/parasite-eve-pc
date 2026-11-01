#ifndef PE_STATE_ACTOR_PREPARE_H
#define PE_STATE_ACTOR_PREPARE_H
#include "pe_state.h"
int PeStateActor_ConsumeAllocation(uint32_t,void **);
int PeStateActor_Prepare(void *,const unsigned char *,size_t,int rollback);
#endif
