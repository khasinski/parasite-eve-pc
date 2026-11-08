/* SPDX-License-Identifier: MIT. Exact fixed startup field-model ownership. */
#ifndef PE_STATE_STARTUP_MODELS_H
#define PE_STATE_STARTUP_MODELS_H
#include <stddef.h>
#include <stdint.h>
#include "pe_state_field_models.h"
#define PE_STATE_STARTUP_MODELS_BYTES (128u+5u*(64u+PE_STATE_FIELD_MODEL_BYTES))
#define PE_STATE_STARTUP_BYTES 0x10A50u
typedef struct PeStartupResource{uint32_t key,offset,available,extent;PeFieldModelShape shape;}PeStartupResource;
typedef struct PeStartupModelView{uint32_t directory,table,maskedCrc;PeStartupResource resources[8];}PeStartupModelView;
/* Pure bounded source parsing. Never mutates source and never accepts an unknown key. */
int PeStartupModels_Describe(const void*,size_t,PeStartupModelView*);
int PeStartupModels_MaskedCrc(uint32_t*);
int PeStartupModels_Ready(void);
const char*PeStartupModels_Blocker(void);
void PeStartupModels_ResetBlocker(void);
int PeStartupModels_Walk(const unsigned char*,unsigned char*,int);
#endif
