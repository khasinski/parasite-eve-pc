#ifndef PE_STATE_SESSION_H
#define PE_STATE_SESSION_H
#include "pe_state_world.h"
#include "pe_state_battle.h"
typedef struct PeCpuStateSession {
    PeStateIdentity identity;PeStateRegistry registry;PeWorldState world;PeBattleNumeric battle;
    int initialized,pending,result,boundaryReason;size_t used;
    unsigned char *output,*scratch;size_t outputBytes,scratchBytes;
    const unsigned char *input;size_t inputBytes;
    char stateDirectory[256],stateFile[35];
} PeCpuStateSession;
void PeCpuStateSession_Init(PeCpuStateSession *,const PeStateIdentity *);
/* Developer partial COMPONENT operations only. Caller buffers live until pending=0. */
int PeCpuStateSession_RequestCapture(PeCpuStateSession *,unsigned char *,size_t);
int PeCpuStateSession_RequestRestoreComponents(PeCpuStateSession *,const unsigned char *,size_t,unsigned char *,size_t);
/* Development-only partial-component files; no full-game availability implied.
 * Immutable commit-pair files, caller-owned buffers live until pending clears. */
int PeCpuStateSession_RequestCaptureFile(PeCpuStateSession *,unsigned char *,size_t,const char *);
int PeCpuStateSession_RequestRestoreComponentsFile(PeCpuStateSession *,unsigned char *,size_t,unsigned char *,size_t,const char *,const char *);
enum PeStateRoomBlocker{PE_STATE_ROOM_READY=0,PE_STATE_ROOM_HOST_OR_MODS=10,PE_STATE_ROOM_MOVIE_SCRIPT=11,PE_STATE_ROOM_NATIVE_OVERLAY=12,PE_STATE_ROOM_ASSETS=13,PE_STATE_ROOM_OWNERS=14};
/* Evaluated at queued CPU service, never as a promise from inside the modal.
 * Further owner/graph validators can still reject capture or load. */
int PeCpuStateSession_CurrentRoomBoundary(void);
const char *PeCpuStateSession_RoomBoundaryText(int);
int PeCpuStateSession_FullLoadAvailable(void);
#endif
