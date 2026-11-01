#include "pe_state_mirror.h"
#include "pe1/pm.h"
#include "pe1/entity_allocation.h"
#include "pe1/player_entity.h"
#include "pe1/room_fx_model.h"
#include <stdint.h>
#include "pe_state_boundary.h"
#include "psyz/state_audio.h"
#include "psyz/state_producers.h"
_Static_assert(sizeof(RoomFxMirror)<=sizeof(PmPrimarySlot),"mirror inline payload fits actual native primary slot");
static uint32_t get(const unsigned char *p){return(uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void put(unsigned char *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(v>>(8*i));}
static unsigned actor_id(const void *p){if(!p)return 0;for(unsigned i=0;i<14;i++)if(p==&g_EntityPool[i])return i+1;return UINT32_MAX;}
static int live_mask(uint32_t *out){uint32_t mask=0;unsigned n=0;for(FieldActor *p=g_FieldActorListHead;p;p=p->next){unsigned id=actor_id(p);if(!id||id>14||n++>=14||(mask&(1u<<(id-1))))return 0;mask|=1u<<(id-1);}*out=mask;return 1;}
static PmSlotHeader *slot(unsigned i){return i<11?&g_PmSlotTable[i].header:&g_PmSlotTable2[i-11].header;}
int PeStateMirror_HeaderSupported(unsigned index,unsigned state,unsigned command){return index<22&&(state==0||(index<11&&state<=5&&command==69&&g_PmCmdHandlerTable&&g_PmCmdHandlerTable[69]==&g_RoomFxMirrorHandlers));}
int PeStateMirror_CurrentSupported(unsigned index,const PmSlotHeader *h){if(!h||index>=22||!g_PmSlotTable||!g_PmSlotTable2||h!=slot(index))return 0;if(!h->state)return !h->owner;if(!PeStateMirror_HeaderSupported(index,h->state,h->command))return 0;const RoomFxMirror *m=(const RoomFxMirror *)h;uint32_t live;unsigned owner=actor_id(h->owner),target=actor_id(m->target);return live_mask(&live)&&owner&&owner<=14&&target&&target<=14&&(live&(1u<<(owner-1)))&&(live&(1u<<(target-1)))&&m->bound<=1&&(!m->active||(g_EntityPool[owner-1].render_object.header&&g_EntityPool[owner-1].render_object.primitive_buffer&&g_EntityPool[target-1].render_object.header));}
/* Header points at the complete existing PM chunk; typed data is its new suffix.
 * PM header remains five explicit words at24+i*20. No work-area bytes are dumped. */
int PeStateMirror_Walk(const unsigned char *header,const unsigned char *in,unsigned char *out,int apply){uint32_t live;if(apply&&(!PeStateBoundary_AtCpuPoint()||!Psyz_StateKernelOwned()||!Psyz_StateAudioOwned()))return 0;if((!in&&!out)||(!out&&!header))return 0;if(!g_PmSlotTable||!g_PmSlotTable2||!live_mask(&live))return 0;size_t at=0;
for(unsigned i=0;i<22;i++){PmSlotHeader *h=slot(i);uint32_t state=out?h->state:get(header+24+i*20),command=out?h->command:get(header+28+i*20);if(!PeStateMirror_HeaderSupported(i,state,command))return 0;uint32_t v[8]={0};if(out&&!PeStateMirror_CurrentSupported(i,h))return 0;if(out&&state){RoomFxMirror *m=(RoomFxMirror *)h;v[0]=actor_id(h->owner);v[1]=actor_id(m->target);v[2]=(uint32_t)(int32_t)m->lineX0;v[3]=(uint32_t)(int32_t)m->lineZ0;v[4]=(uint32_t)(int32_t)m->lineX1;v[5]=(uint32_t)(int32_t)m->lineZ1;v[6]=m->bound;v[7]=m->active;}
for(unsigned j=0;j<8;j++){if(out)put(out+at,v[j]);else v[j]=get(in+at);at+=4;}
if(state){if(!v[0]||v[0]>14||!v[1]||v[1]>14||!(live&(1u<<(v[0]-1)))||!(live&(1u<<(v[1]-1)))||v[6]>1||v[7]>255)return 0;for(unsigned j=2;j<6;j++)if((int32_t)v[j]< -32768||(int32_t)v[j]>32767)return 0;if(apply&&!out){RoomFxMirror *m=(RoomFxMirror *)h;h->owner=&g_EntityPool[v[0]-1];m->target=&g_EntityPool[v[1]-1];m->lineX0=(s16)v[2];m->lineZ0=(s16)v[3];m->lineX1=(s16)v[4];m->lineZ1=(s16)v[5];m->bound=(u8)v[6];m->active=(u8)v[7];}}else{for(unsigned j=0;j<8;j++)if(v[j])return 0;if(apply&&!out)h->owner=0;}
}
for(unsigned j=0;j<12;j++){int32_t value=out?(j<9?g_RoomFxMirrorAxes.m[j/3][j%3]:g_RoomFxMirrorAxes.t[j-9]):(int32_t)get(in+at);if(j<9&&(value< -32768||value>32767))return 0;if(out)put(out+at,(uint32_t)value);else if(apply){if(j<9)g_RoomFxMirrorAxes.m[j/3][j%3]=(s16)value;else g_RoomFxMirrorAxes.t[j-9]=value;}at+=4;}return at==PE_STATE_MIRROR_BYTES;}

/* Existing inline slots have no class heap to allocate. A whole typed preflight
 * precedes writes; rollback preparation can repair partial PM headers after
 * actor reconstruction, without invoking class callbacks. */
int PeStateMirror_Prepare(const unsigned char *header,const unsigned char *data){if(!PeStateBoundary_AtCpuPoint()||!Psyz_StateKernelOwned()||!Psyz_StateAudioOwned())return 0;if(!PeStateMirror_Walk(header,data,0,0))return 0;for(unsigned i=0;i<22;i++){PmSlotHeader *h=slot(i);const unsigned char *p=header+24+i*20;h->state=(u8)get(p);h->command=(u8)get(p+4);h->field02=(u8)get(p+8);h->field03=(u8)get(p+12);h->ticks=get(p+16);h->owner=0;}return PeStateMirror_Walk(header,data,0,1)?1:-1;}
