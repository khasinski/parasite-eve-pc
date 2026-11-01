/* SPDX-License-Identifier: MIT. Source-owned immutable model/row container bounds. */
#ifndef PE_STATE_BANK_SOURCES_H
#define PE_STATE_BANK_SOURCES_H
#include <stddef.h>
#include <stdint.h>
enum {PE_STATE_BANK_ROOM=1,PE_STATE_BANK_PLAYER=2,PE_STATE_BANK_ENTITY=3};
/* Actual loader invalidates BEFORE reuse and commits AFTER successful input completion. */
void PeStateBankSource_Invalidate(unsigned);
int PeStateBankSource_Commit(unsigned,const void*,size_t);
/* Source-ID+offset+available byte count, not a process address in serialized data. */
int PeStateBankSource_Locate(const void*,size_t,unsigned*,uint32_t*,size_t*,uint64_t*);
#endif
