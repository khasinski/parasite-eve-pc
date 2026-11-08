/* SPDX-License-Identifier: MIT. No allocation, file access, constructors or raw heap images. */
#include "pe_state_startup_models.h"
#include "pe_state_boundary.h"
#include "pe_state_assets.h"
#include "pe1/scene_room_load.h"
#include "pe1/fieldfx_state.h"
#include "pe1/fieldfx_task.h"
#include "psyz/state_audio.h"
#include "psyz/state_producers.h"
#include <string.h>
#include <stdint.h>
static uint32_t get32(const unsigned char*p){return(uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void put32(unsigned char*p,uint32_t v){for(unsigned j=0;j<4;j++)p[j]=(unsigned char)(v>>(j*8));}
static int overlap(size_t a,size_t an,size_t b,size_t bn){return an&&bn&&a<b+bn&&b<a+an;}
static const uint32_t startup_keys[8]={0x57D40D84u,0x57D41D84u,0x6DEAE684u,0x8AB5B684u,0x5AB32504u,0x5AB32104u,0x83ACD504u,0x57E8EB04u};
int PeStartupModels_Describe(const void*source,size_t bytes,PeStartupModelView*out){
 if(!source||!out||bytes!=PE_STATE_STARTUP_BYTES)return 0;const unsigned char*b=source;PeStartupModelView v;memset(&v,0,sizeof v);v.directory=get32(b+4);if(v.directory%4||v.directory>bytes||52>bytes-v.directory)return 0;uint32_t descriptor=get32(b+v.directory+8),count=descriptor>>22;v.table=descriptor&0x3fffffu;if(count!=8||v.table%4||v.table>bytes||count>(bytes-v.table)/12)return 0;
 unsigned seen=0;for(unsigned j=0;j<8;j++){const unsigned char*r=b+v.table+j*12;uint32_t key=get32(r+8),off=get32(r+4)&0xffffffu;unsigned k;for(k=0;k<8;k++)if(key==startup_keys[k])break;if(k==8||(seen&(1u<<k))||off%4||off<8||off>bytes)return 0;seen|=1u<<k;v.resources[k].key=key;v.resources[k].offset=off;}if(seen!=255)return 0;
 for(unsigned k=0;k<8;k++){PeStartupResource*r=&v.resources[k];size_t available=bytes-r->offset;for(unsigned j=0;j<8;j++)if(j!=k){unsigned other=v.resources[j].offset;if(other==r->offset)return 0;if(other>r->offset&&other-r->offset<available)available=other-r->offset;}r->available=(uint32_t)available;if(k<2){r->extent=32768;if(r->extent>available)return 0;}else{if(!PeFieldModel_Describe(b+r->offset,available,&r->shape))return 0;r->extent=r->shape.extent;}if(!r->extent||overlap(r->offset,r->extent,0,8)||overlap(r->offset,r->extent,v.directory,52)||overlap(r->offset,r->extent,v.table,96))return 0;for(unsigned j=0;j<k;j++)if(overlap(r->offset,r->extent,v.resources[j].offset,v.resources[j].extent))return 0;}
 /* Only6DEAE684(P91),8AB5B684(P98/P99),5AB32504/5AB32104(P103),83ACD504(P106) mutate.
  * Every other resource remains immutable source identity. */
 uint32_t crc=~0u;for(size_t j=0;j<bytes;j++){unsigned value=b[j];for(unsigned k=2;k<=6;k++){PeStartupResource*m=&v.resources[k];if(j>=m->offset&&j-m->offset<m->extent&&PeFieldModel_MutableByte(&m->shape,j-m->offset))value=0;}crc^=value;for(unsigned bit=0;bit<8;bit++)crc=(crc>>1)^(0xedb88320u&-(crc&1u));}v.maskedCrc=~crc;*out=v;return 1;
}
extern unsigned char *g_FieldFxModel6DEAE684,*D_800F32D8,*D_800F3418,*g_FieldFxTwinModelB,*g_FieldFxModel83ACD504,*g_FieldFxGlintModel;
static unsigned startupReason;
void PeStartupModels_ResetBlocker(void){startupReason=0;}
const char*PeStartupModels_Blocker(void){switch(startupReason){case 1:return "Checkpoint must wait for the normal game boundary";case 2:return "Original effect models are not initialized; resume normal gameplay";case 3:return "Original startup model format is unsupported; keep normal memory-card saves";case 4:return "Original effect model sources changed; restart before using checkpoints";case 5:return "Actor/resource ownership for this effect model is unsupported at this boundary";default:return 0;}}
static int native_view(PeStartupModelView*v){startupReason=1;if(!PeStateBoundary_AtCpuPoint())return 0;startupReason=2;if(g_GameState.startup_resource_blob!=g_StartupResourceBlob)return 0;startupReason=3;if(!PeStartupModels_Describe(g_StartupResourceBlob,sizeof g_StartupResourceBlob,v))return 0;startupReason=4;void*roots[8]={g_FieldEffectTexturePage0,g_FieldEffectTexturePage1,g_FieldFxModel6DEAE684,D_800F32D8,D_800F3418,g_FieldFxTwinModelB,g_FieldFxModel83ACD504,g_FieldFxGlintModel};for(unsigned j=0;j<8;j++)if(roots[j]!=(void*)(g_StartupResourceBlob+v->resources[j].offset))return 0;startupReason=5;for(unsigned k=2;k<=6;k++)if(!PeFieldModels_SourceDisjoint(g_StartupResourceBlob+v->resources[k].offset,v->resources[k].extent))return 0;startupReason=0;return 1;}
int PeStartupModels_MaskedCrc(uint32_t*out){PeStartupModelView v;if(!out||!native_view(&v))return 0;*out=v.maskedCrc;return 1;}
int PeStartupModels_Ready(void){PeStartupModelView v;return native_view(&v);}
int PeStartupModels_Walk(const unsigned char*in,unsigned char*out,int apply){
 if((!in&&!out)||(out&&apply)||!PeStateBoundary_AtCpuPoint())return 0;
 /* Entire set is validated before any mutable model word is written. */
 if(apply&&(!Psyz_StateKernelOwned()||!Psyz_StateAudioOwned()||!PeStartupModels_Walk(in,0,0)))return 0;
 PeStartupModelView v;if(!native_view(&v))return 0;uint64_t identity[3]={PeStateBoundary_Session(),PeStateBoundary_SceneEpoch(),PeStateAssets_Generation()};
 if(out){memset(out,0,PE_STATE_STARTUP_MODELS_BYTES);memcpy(out,"PESTMD04",8);put32(out+8,PE_STATE_STARTUP_BYTES);put32(out+12,v.maskedCrc);for(unsigned j=0;j<3;j++){put32(out+16+j*8,(uint32_t)identity[j]);put32(out+20+j*8,(uint32_t)(identity[j]>>32));}put32(out+40,v.directory);put32(out+44,8);put32(out+48,5);}
 else{if(memcmp(in,"PESTMD04",8)||get32(in+8)!=PE_STATE_STARTUP_BYTES||get32(in+12)!=v.maskedCrc||get32(in+40)!=v.directory||get32(in+44)!=8||get32(in+48)!=5)return 0;for(unsigned j=0;j<3;j++)if(get32(in+16+j*8)!=(uint32_t)identity[j]||get32(in+20+j*8)!=(uint32_t)(identity[j]>>32))return 0;for(unsigned j=52;j<128;j++)if(in[j])return 0;}
 for(unsigned index=0;index<5;index++){unsigned k=2+index;PeStartupResource*m=&v.resources[k];size_t at=128u+index*(64u+PE_STATE_FIELD_MODEL_BYTES);uint32_t record[5]={m->key,m->offset,m->extent,m->available,k};for(unsigned j=0;j<5;j++){if(out)put32(out+at+j*4,record[j]);else if(get32(in+at+j*4)!=record[j])return 0;}if(!out)for(unsigned j=20;j<64;j++)if(in[at+j])return 0;void*model=g_StartupResourceBlob+m->offset;if(out){if(!PeFieldModel_Capture(model,m->available,out+at+64,PE_STATE_FIELD_MODEL_BYTES))return 0;}else if(!PeFieldModel_Validate(model,m->available,in+at+64,PE_STATE_FIELD_MODEL_BYTES))return 0;}
 if(apply)for(unsigned index=0;index<5;index++){PeStartupResource*m=&v.resources[index+2];size_t at=128u+index*(64u+PE_STATE_FIELD_MODEL_BYTES);if(!PeFieldModel_Restore(g_StartupResourceBlob+m->offset,m->available,in+at+64,PE_STATE_FIELD_MODEL_BYTES))return 0;}
 return 1;
}
