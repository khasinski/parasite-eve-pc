#ifndef PE_STATE_MIRROR_H
#define PE_STATE_MIRROR_H
#include "pe1/pm_types.h"
#include <stddef.h>
#define PE_STATE_MIRROR_BYTES (22u*32u+48u)
int PeStateMirror_HeaderSupported(unsigned,unsigned,unsigned);
int PeStateMirror_CurrentSupported(unsigned,const PmSlotHeader *);
int PeStateMirror_Prepare(const unsigned char *,const unsigned char *);
int PeStateMirror_Walk(const unsigned char *,const unsigned char *,unsigned char *,int);
#endif
