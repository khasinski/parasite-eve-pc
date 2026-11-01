#ifndef PE_STATE_BOUNDARY_H
#define PE_STATE_BOUNDARY_H
#include "pe_state.h"
typedef void (*PeStateBoundaryWork)(void *context);
/* Game-thread only; request routes to an explicit completed-frame CPU point. */
int PeStateBoundary_Request(PeStateBoundaryWork,void *);
int PeStateBoundary_RequestWithCancel(PeStateBoundaryWork,PeStateBoundaryWork,void *);
enum PeStateInvalidationReason{PE_STATE_INVALIDATE_ROOM=1,PE_STATE_INVALIDATE_NORMAL_LOAD=2};
/* Invalidate pending graph work BEFORE replacing authoritative game state. */
void PeStateBoundary_Invalidate(unsigned reason);
unsigned PeStateBoundary_InvalidationReason(void);
void PeStateBoundary_SceneChanged(void);
void PeStateBoundary_FrameBegin(void);
void PeStateBoundary_Service(int safe);
int PeStateBoundary_AtCpuPoint(void);
uint64_t PeStateBoundary_Session(void);
uint64_t PeStateBoundary_SceneEpoch(void);
PeStateBarrier PeStateBoundary_CpuBarrier(void);
#endif
