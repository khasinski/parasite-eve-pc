#include "pe_state_assets.h"
#include "pe_state_boundary.h"
#include "pe1/game_state.h"
#include <string.h>
static PeScriptAsset asset={.generation=1};
static uint32_t nativeDescriptors=UINT32_MAX;
static uint32_t read32(const unsigned char *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void write32(unsigned char *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(v>>(8*i));}
static uint32_t checksum(const unsigned char *p,size_t n){uint32_t v=~0u;for(size_t i=0;i<n;i++){v^=p[i];for(unsigned j=0;j<8;j++)v=(v>>1)^(0xedb88320u&-(int32_t)(v&1));}return ~v;}
static void generation(void){if(++asset.generation==0)asset.generation=1;}
void PeStateAssets_Invalidate(void){generation();nativeDescriptors=UINT32_MAX;asset.valid=0;asset.data=0;asset.view=0;}
uint64_t PeStateAssets_Generation(void){return asset.generation;}
static int shape(const unsigned char *data,size_t bytes,const RelocBlock *view){
    if(!data||!view||bytes<8||bytes>PE_STATE_MAX_BYTES)return 0;
    uint32_t offset=read32(data),count=read32(data+4);
    if(offset>=bytes||count>4096||count>(bytes-8)/4||view->count!=count||view->u0.base!=data+offset)return 0;
    for(uint32_t i=0;i<count;i++){uint32_t at=read32(data+8+(size_t)i*4);if(at>=bytes||view->offsets[i]!=data+at)return 0;}
    return 1;
}
void PeStateAssets_Commit(uint32_t room,uint32_t sourceOffset,const unsigned char *data,size_t bytes,const RelocBlock *view){
    PeStateAssets_Invalidate();
    if(!room||!shape(data,bytes,view))return;
    asset.room=room;asset.sourceOffset=sourceOffset;asset.bytes=(uint32_t)bytes;asset.crc=checksum(data,bytes);asset.count=(uint32_t)view->count;asset.data=data;asset.view=view;asset.valid=1;
}
int PeStateAssets_Get(PeScriptAsset *out){
    if(!out||!PeStateBoundary_AtCpuPoint()||!asset.valid||(const RelocBlock *)g_GameState.bank_reset_944[0]!=asset.view||g_SceneDataTable1!=asset.view||checksum(asset.data,asset.bytes)!=asset.crc||!shape(asset.data,asset.bytes,asset.view))return 0;
    *out=asset;return 1;
}
void PeStateAssets_SetNativeDescriptors(uint32_t n){if(asset.valid&&n<=1023)nativeDescriptors=n;}
int PeStateAssets_NativeDescriptors(uint32_t *n){PeScriptAsset current;if(!n||nativeDescriptors==UINT32_MAX||!PeStateAssets_Get(&current))return 0;*n=nativeDescriptors;return 1;}
static int capture(void *context,unsigned char *out,size_t cap,size_t *used){
    (void)context;PeScriptAsset current;if(used)*used=0;
    if(!out||!used||cap<32||!PeStateAssets_Get(&current))return 0;
    memcpy(out,"PETASK02",8);write32(out+8,current.room);write32(out+12,current.sourceOffset);write32(out+16,current.bytes);write32(out+20,current.crc);write32(out+24,current.count);write32(out+28,nativeDescriptors);*used=32;return 1;
}
static int validate(void *context,const unsigned char *data,size_t bytes){
    (void)context;PeScriptAsset current;
    if(!data||bytes!=32||memcmp(data,"PETASK02",8)||!PeStateAssets_Get(&current))return 0;
    return read32(data+8)==current.room&&read32(data+12)==current.sourceOffset&&read32(data+16)==current.bytes&&read32(data+20)==current.crc&&read32(data+24)==current.count&&read32(data+28)==nativeDescriptors;
}
int PeStateAssets_RebuildCurrentView(const unsigned char *descriptor,size_t bytes){
    if(!validate(0,descriptor,bytes))return PE_STATE_INCOMPATIBLE;
    if((const RelocBlock *)g_GameState.bank_reset_944[0]!=asset.view)return PE_STATE_INCOMPLETE;
    /* Existing constructor validates offsets, allocates new table first, then frees old. */
    RelocBlock *next=Script_RelocBlock((void *)asset.data,asset.bytes);
    if(!next)return PE_STATE_INCOMPLETE;
    g_GameState.bank_reset_944[0]=(void *)next;asset.view=next;generation();
    return 0;
}
/* Identity module restores nothing: it prevents applying CPU state to different script assets.
 * Actual CURRENT task-table reconstruction is the explicit API above, not a hidden no-op. */
int PeState_RegisterScriptAssetIdentity(PeStateRegistry *registry){PeStateModule module={PE_STATE_ASSETS,2,32,capture,0,validate,validate,0};return PeState_Register(registry,module);}
