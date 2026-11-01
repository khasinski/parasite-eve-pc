/* SPDX-License-Identifier: MIT. Typed retained graph metadata, no heap snapshots. */
#ifndef PE_STATE_TRANSITION_H
#define PE_STATE_TRANSITION_H
#include "pe_state_world.h"
#include "pe_state_battle.h"
typedef struct PeStateGraphContext{PeStateRegistry*registry;PeWorldState*world;PeBattleNumeric*battle;}PeStateGraphContext;
/* Internal opt-in transaction, NOT wired to gameplay/session availability.
 * Target metadata must be retained from its capture; current metadata must be
 * freshly bound to the live cohort. No native/source lifetime can change.
 * Every other registered owner still validates; there is no exclusion mask.
 * Caller adopts/rebinds a successful target after return, never before it.
 * Contexts and buffers must remain alive until this synchronous call returns. */
int PeStateGraph_RestorePair(PeStateGraphContext*,PeStateGraphContext*,const PeStateBarrier*,const PeStateIdentity*,const unsigned char*,size_t,unsigned char*,size_t);
#endif
