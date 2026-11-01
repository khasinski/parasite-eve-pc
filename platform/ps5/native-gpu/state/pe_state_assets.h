#ifndef PE_STATE_ASSETS_H
#define PE_STATE_ASSETS_H
#include "pe_state.h"
#include "pe1/reloc_block.h"
typedef struct PeScriptAsset {uint64_t generation;uint32_t room,sourceOffset,bytes,crc,count;const unsigned char *data;const RelocBlock *view;int valid;} PeScriptAsset;
void PeStateAssets_Invalidate(void);
void PeStateAssets_Commit(uint32_t,uint32_t,const unsigned char *,size_t,const RelocBlock *);
uint64_t PeStateAssets_Generation(void);
int PeStateAssets_Get(PeScriptAsset *);
void PeStateAssets_SetNativeDescriptors(uint32_t);
int PeStateAssets_NativeDescriptors(uint32_t *);
/* Rebuild only this CURRENT loaded task view; not a CD/room/actor reconstruction. */
int PeStateAssets_RebuildCurrentView(const unsigned char *,size_t);
int PeState_RegisterScriptAssetIdentity(PeStateRegistry *);
#endif
