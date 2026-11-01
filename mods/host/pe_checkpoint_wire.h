/* SPDX-License-Identifier: MIT. Source-bound typed checkpoint scalar encoding. */
#ifndef PE_CHECKPOINT_WIRE_H
#define PE_CHECKPOINT_WIRE_H
#include <stdint.h>
#include <stddef.h>
#include <limits.h>
typedef struct {const unsigned char *in;unsigned char *out;size_t at;int apply,ok;}PeCheckpointWire;
static uint32_t pe_checkpoint_u32(PeCheckpointWire *w,uint32_t current,uint32_t lo,uint32_t hi,int exact){
 uint32_t v=current;if(w->in){const unsigned char *p=w->in+w->at;v=(uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
 if(v<lo||v>hi||(w->in&&exact&&v!=current))w->ok=0;
 if(w->out)for(unsigned i=0;i<4;i++)w->out[w->at+i]=(unsigned char)(v>>(i*8));w->at+=4;return v;
}
static int32_t pe_checkpoint_s32(PeCheckpointWire *w,int32_t current,int32_t lo,int32_t hi,int exact){size_t at=w->at;uint32_t bits=pe_checkpoint_u32(w,(uint32_t)current,0,UINT32_MAX,exact);int32_t v=(int32_t)bits;if(v<lo||v>hi)w->ok=0;(void)at;return v;}
#define PE_CK_U(w,value,lo,hi,exact) do{uint32_t ck=pe_checkpoint_u32(&(w),(uint32_t)(value),(lo),(hi),(exact));if((w).apply&&(w).ok)(value)=(__typeof__(value))ck;}while(0)
#define PE_CK_S(w,value,lo,hi,exact) do{int32_t ck=pe_checkpoint_s32(&(w),(int32_t)(value),(lo),(hi),(exact));if((w).apply&&(w).ok)(value)=(__typeof__(value))ck;}while(0)
#endif
