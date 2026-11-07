/* SPDX-License-Identifier: MIT */
#ifndef PE_STATE_LEGACY_FX_H
#define PE_STATE_LEGACY_FX_H
#include "pe_state_world.h"
#include "pe1/fieldfx_state.h"
#define PE_STATE_LEGACY_FX_BYTES (2048u+11u*8192u)
int PeStateLegacy_HeaderSupported(unsigned,unsigned,unsigned);
int PeStateLegacy_CurrentSupported(unsigned);
int PeStateLegacy_Constructed(const PeWorldState*,const unsigned char*,const unsigned char*);
int PeStateLegacy_WalkForWorld(const PeWorldState*,const unsigned char*,const unsigned char*,unsigned char*,int);
/* Actual bounded common VM codec; targets are seven typed pool IDs from the group, never addresses. */
int PeStateLegacy_Context(FieldFxObject*,const PeWorldState*,const uint32_t[7],const unsigned char*,unsigned char*,int);
#endif
