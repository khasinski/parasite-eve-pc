#ifndef PE_STATE_ROOM_OBJECT_H
#define PE_STATE_ROOM_OBJECT_H
#include "pe_state.h"
#include "pe1/render_object.h"
void PeStateRoom_Configured(RenderObjectEntity *,RenderObjectHeader *,unsigned char *,short,short,short,short,short,int);
int PeState_RegisterRoomObject(PeStateRegistry *);
#endif
