#ifndef PE_STATE_LIFETIME_H
#define PE_STATE_LIFETIME_H
#include <stdint.h>
uint64_t PeStateResource_Epoch(void);
int PeStateResource_BeginEquivalentRebuild(void);
void PeStateResource_EndEquivalentRebuild(void);
int PeStateResource_IsRebuilding(void);
void PeStateResource_Changed(void);
#endif
