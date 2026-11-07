#ifndef PE_STATE_CHECKPOINT_OWNERS_H
#define PE_STATE_CHECKPOINT_OWNERS_H
#include "pe_state.h"
enum PeCheckpointBlocker {PE_CHECKPOINT_SUPPORTED_IDLE=0,PE_CHECKPOINT_NOT_AT_CPU_POINT=1,PE_CHECKPOINT_PM_ACTIVE=2,PE_CHECKPOINT_MOVIE_ACTIVE=3,PE_CHECKPOINT_NO_PLAYER=4};
int PeStateCheckpoint_Boundary(void);
int PeState_RegisterGeometry(PeStateRegistry *);
int PeState_RegisterGameGlobals(PeStateRegistry *);
struct PeWorldState;
int PeState_RegisterCheckpointOwnersForGraph(PeStateRegistry*,struct PeWorldState*);
int PeState_RegisterCheckpointOwners(PeStateRegistry *);
/* Observation of the last actual CPU-boundary PM rejection, not a new gate. */
typedef struct PeStatePmBlocker {unsigned slot,command,status,category;unsigned long long sequence;}PeStatePmBlocker;
/* category1 unsupportedclass/header;2 supportedowner rejected typed actor/resource/data. Modal previews do not erase the last actual CPU observation; scene changes invalidate it. */
int PeStateCheckpoint_PmBlocker(PeStatePmBlocker*);
#endif
