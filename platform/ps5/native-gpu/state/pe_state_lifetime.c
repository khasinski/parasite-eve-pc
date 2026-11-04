#include "pe_state_lifetime.h"
#include "pe_state_boundary.h"
#include "psyz/state_audio.h"
#include "psyz/state_producers.h"
static int rebuilding;
static uint64_t epoch=1;
uint64_t PeStateResource_Epoch(void){return epoch;}
void PeStateResource_Changed(void){if(rebuilding)return;if(++epoch==0)epoch=1;}

int PeStateResource_BeginEquivalentRebuild(void){if(rebuilding||!PeStateBoundary_AtCpuPoint()||!Psyz_StateAudioOwned()||!Psyz_StateKernelOwned())return 0;rebuilding=1;return 1;}
void PeStateResource_EndEquivalentRebuild(void){if(Psyz_StateAudioOwned()&&Psyz_StateKernelOwned())rebuilding=0;}
int PeStateResource_IsRebuilding(void){return rebuilding;}
