#ifndef PE_STATE_FILES_H
#define PE_STATE_FILES_H
#include "pe_state.h"
/* Caller supplies the project-owned save-states directory. No implicit user paths.
 * Basename identifies an immutable payload+commit pair; never overwrites old state. */
int PeStateFiles_WriteUnique(const char *,const unsigned char *,size_t,const PeStateIdentity *,char *,size_t);
int PeStateFiles_Read(const char *,const char *,unsigned char *,size_t,size_t *,const PeStateIdentity *);
#endif
