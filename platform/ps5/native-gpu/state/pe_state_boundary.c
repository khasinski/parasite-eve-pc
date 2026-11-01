#include "pe_state_boundary.h"
#include <SDL3/SDL.h>
static struct {SDL_ThreadID thread;uint64_t session,scene,frame;int servicing,locked,reentered;unsigned reason;PeStateBoundaryWork work,cancel;void *context;} boundary;
void PeStateBoundary_Invalidate(unsigned reason) {
    if(reason!=PE_STATE_INVALIDATE_ROOM&&reason!=PE_STATE_INVALIDATE_NORMAL_LOAD)return;
    SDL_ThreadID thread=SDL_GetCurrentThreadID();
    if(!boundary.thread){boundary.thread=thread;boundary.session=SDL_GetPerformanceCounter();if(!boundary.session)boundary.session=1;}
    if(thread!=boundary.thread)return;
    boundary.reason=reason;
    ++boundary.scene;if(!boundary.scene)++boundary.scene;
    ++boundary.frame;if(!boundary.frame)++boundary.frame;
    if(boundary.servicing)boundary.reentered=1;
    if(boundary.work&&boundary.cancel)boundary.cancel(boundary.context);
    boundary.work=0;boundary.cancel=0;boundary.context=0;
}
void PeStateBoundary_SceneChanged(void){PeStateBoundary_Invalidate(PE_STATE_INVALIDATE_ROOM);}
unsigned PeStateBoundary_InvalidationReason(void){return boundary.reason;}
int PeStateBoundary_RequestWithCancel(PeStateBoundaryWork work,PeStateBoundaryWork cancel,void *context) {
    if(!work||!boundary.thread||SDL_GetCurrentThreadID()!=boundary.thread||boundary.work||boundary.servicing)return PE_STATE_BUSY;
    boundary.work=work;boundary.cancel=cancel;boundary.context=context;return 0;
}
void PeStateBoundary_FrameBegin(void) {
    if(!boundary.thread||SDL_GetCurrentThreadID()!=boundary.thread)return;
    ++boundary.frame;if(!boundary.frame)++boundary.frame;
    if(boundary.servicing)boundary.reentered=1;
}
void PeStateBoundary_Service(int safe) {
    if(!safe||!boundary.thread||SDL_GetCurrentThreadID()!=boundary.thread||boundary.servicing)return;
    if(!boundary.work)return;
    PeStateBoundaryWork work=boundary.work;void *context=boundary.context;
    boundary.work=0;boundary.cancel=0;boundary.context=0;boundary.servicing=1;boundary.reentered=0;
    work(context);
    boundary.locked=0;boundary.servicing=0;
}
static int begin(void *context,uint64_t *epoch) {
    (void)context;
    if(!epoch||!boundary.servicing||boundary.reentered||boundary.locked||SDL_GetCurrentThreadID()!=boundary.thread)return 0;
    boundary.locked=1;*epoch=boundary.frame;return 1;
}
static int unchanged(void *context,uint64_t epoch) {
    (void)context;return boundary.servicing&&!boundary.reentered&&boundary.locked&&epoch==boundary.frame&&SDL_GetCurrentThreadID()==boundary.thread;
}
static void end(void *context){(void)context;boundary.locked=0;}
uint64_t PeStateBoundary_Session(void){return boundary.session;}
uint64_t PeStateBoundary_SceneEpoch(void){return boundary.scene;}
PeStateBarrier PeStateBoundary_CpuBarrier(void){PeStateBarrier result={0,begin,unchanged,end};return result;}

int PeStateBoundary_AtCpuPoint(void){return boundary.servicing&&!boundary.reentered&&SDL_GetCurrentThreadID()==boundary.thread;}

int PeStateBoundary_Request(PeStateBoundaryWork work,void *context){return PeStateBoundary_RequestWithCancel(work,0,context);}
