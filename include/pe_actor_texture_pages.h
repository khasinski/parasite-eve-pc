/* SPDX-License-Identifier: MPL-2.0. Original native actor texture ownership adapter. */
#ifndef PE_ACTOR_TEXTURE_PAGES_H
#define PE_ACTOR_TEXTURE_PAGES_H
#if defined(PE_PS5) && defined(PE_PS5_GPU_NATIVE) && PE_NATIVE_STATE_CPU_COMPONENTS
#include "pe1/boot_display.h"
#include "pe1/render_object.h"
#include "pe_state_bank_sources.h"
extern int Psyz_GpuRegisterActorTexturePages(const unsigned short *,unsigned,unsigned,unsigned);
/* Only the imminent native actor packet set is admitted. No geometry, VRAM,
 * palette, shader, allocator, or gameplay values are changed. */
static void PeModelTextures_Actor(FieldActor *actor){
 unsigned slot=14,source;uint32_t offset;size_t available;uint64_t epoch;
 for(unsigned j=0;j<14;j++)if(actor==&g_EntityPool[j]){slot=j;break;}
 if(slot==14||actor==g_PlayerEntity||actor->type_id<1||actor->type_id>9||g_ActiveDrawSlot<0||g_ActiveDrawSlot>1)return;
 RenderObjectEntity *o=&actor->render_object;RenderObjectHeader *h=o->header;
 if(!h||!o->draw_count||!o->primitive_buffer||actor->model_state.model_header!=h||g_GameState.bank_slots[actor->type_id]!=h)return;
 if(!PeStateBankSource_Locate(h,sizeof(*h),&source,&offset,&available,&epoch)||!epoch||(source!=PE_STATE_BANK_ROOM&&source!=PE_STATE_BANK_ENTITY))return;
 unsigned counts[4]={h->packet34_count,h->packet28_count,h->packet24_count,h->packet1c_count},faces=0;
 if(!h->part_count||h->part_count>64||h->vertex_count>2048)return;
 for(unsigned j=0;j<4;j++){if(counts[j]>2048)return;faces+=counts[j];}if(faces>4096)return;
 /* Exact original Render_SetupEntityPrims sections, including rounded command
  * bytes and both textured source primitive kinds. */
 size_t sourceBytes=sizeof(*h)+(size_t)h->part_count*sizeof(RenderObjectPart)+(size_t)h->vertex_count*12+(size_t)faces*sizeof(RenderPrimitiveDescriptor)+(3u+2u*h->part_count)*8+((h->matrix_command_bytes+3u)&~3u)+(size_t)counts[0]*16+(size_t)counts[1]*12;
 if(sourceBytes>available)return;
 size_t packetBytes=2u*((size_t)counts[0]*sizeof(RenderPacket34)+(size_t)counts[1]*sizeof(RenderPacket28)+(size_t)counts[2]*sizeof(RenderPacket24)+(size_t)counts[3]*sizeof(RenderPacket1C));
 uintptr_t begin=actor->allocation_block,pack=(uintptr_t)o->primitive_buffer;unsigned owned=0;
 if(!begin||pack<begin||pack%_Alignof(RenderPacket34))return;
 for(unsigned j=0;j<16;j++){EntityAllocationBlock *b=&g_EntityAllocationTable[j];if(b->address!=begin)continue;unsigned n=b->blockCount;if((n!=1&&n!=2&&n!=4&&n!=8)||j+n>16)return;size_t bytes=(size_t)n*ENTITY_ALLOCATION_BLOCK_SIZE;if(pack-begin>bytes||packetBytes>bytes-(pack-begin))return;owned=1;break;}
 if(!owned)return;
 unsigned short pairs[128];unsigned count=0;
 RenderPacket34 *q=(RenderPacket34*)o->primitive_buffer;RenderPacket28 *t=(RenderPacket28*)(q+2u*counts[0]);
 for(unsigned kind=0;kind<2;kind++)for(unsigned j=0;j<counts[kind];j++){
  unsigned page=kind?t[2*j+g_ActiveDrawSlot].page_bits:q[2*j+g_ActiveDrawSlot].page_bits;
  unsigned clut=kind?t[2*j+g_ActiveDrawSlot].clut:q[2*j+g_ActiveDrawSlot].clut;
  page&=511;clut&=32767;
  /* Current verified subset is P4 only. Other texture depths stay original. */
  if(((page>>7)&3)!=0||((clut&63)*16+16>1024))continue;
  unsigned k;for(k=0;k<count;k++)if(pairs[2*k]==page&&pairs[2*k+1]==clut)break;
  if(k<count)continue;if(count==64)return;
  pairs[2*count]=(unsigned short)page;pairs[2*count+1]=(unsigned short)clut;count++;
 }
 if(count)Psyz_GpuRegisterActorTexturePages(pairs,count,actor->type_id,actor->sub_id);
}
#else
#define PeModelTextures_Actor(actor) ((void)0)
#endif
#endif

