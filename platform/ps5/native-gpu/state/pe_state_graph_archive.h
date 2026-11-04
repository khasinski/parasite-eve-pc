/* SPDX-License-Identifier: MIT. Same-process typed owner descriptors only. */
#ifndef PE_STATE_GRAPH_ARCHIVE_H
#define PE_STATE_GRAPH_ARCHIVE_H
#include "pe_state_transition.h"
#define PE_STATE_GRAPH_ARCHIVE_SLOTS 8u
typedef struct PeStateGraphRecord{uint64_t serial;PeWorldState world;PeBattleNumeric battle;}PeStateGraphRecord;
typedef struct PeStateGraphArchive{uint64_t serial;PeStateGraphRecord slots[PE_STATE_GRAPH_ARCHIVE_SLOTS];}PeStateGraphArchive;
/* These descriptors are NOT serialized and cannot survive process/source reuse.
 * Remember only after actual complete component capture. An evicted/foreign
 * file has no retained target graph and is explicitly incompatible. */
void PeStateGraphArchive_Init(PeStateGraphArchive*);
int PeStateGraphArchive_Remember(PeStateGraphArchive*,const PeWorldState*,const PeBattleNumeric*,const PeStateIdentity*,const unsigned char*,size_t);
int PeStateGraphArchive_Select(const PeStateGraphArchive*,const PeStateIdentity*,const unsigned char*,size_t,PeWorldState*,PeBattleNumeric*);
#endif
