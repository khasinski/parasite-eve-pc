/* SPDX-License-Identifier: MIT. No file/network access, allocation or source-data writes. */
#include "pe_state_bank_sources.h"
#include <stdint.h>
#include <limits.h>
#include "pe1/game_state.h"
typedef struct{const unsigned char*base;size_t bytes;uint64_t epoch;}BankSource;
static BankSource sources[3];
static void invalidate(BankSource*s){s->base=0;s->bytes=0;if(s->epoch<UINT64_MAX)++s->epoch;}
void PeStateBankSource_Invalidate(unsigned id){if(id>=1&&id<=3)invalidate(&sources[id-1]);}
int PeStateBankSource_Commit(unsigned id,const void*base,size_t bytes){if(id<1||id>3)return 0;BankSource*s=&sources[id-1];invalidate(s);if(s->epoch==UINT64_MAX||!base||!bytes||bytes>UINT32_MAX||(uintptr_t)base>UINTPTR_MAX-bytes)return 0;if(id==PE_STATE_BANK_ROOM&&base!=(const void*)g_GameState.loaded_scene_assets)return 0;
if(id==PE_STATE_BANK_PLAYER){uintptr_t end=(uintptr_t)g_GameState.entity_texture_blob,start=(uintptr_t)base;if(base!=(const void*)g_GameState.voice_bank_base_1400||end<=start||bytes>end-start)return 0;}if(id==PE_STATE_BANK_ENTITY&&base!=(const void*)g_GameState.entity_texture_blob)return 0;s->base=base;s->bytes=bytes;return 1;}
int PeStateBankSource_Locate(const void*p,size_t needed,unsigned*id,uint32_t*offset,size_t*available,uint64_t*epoch){if(!p||!id||!offset||!available||!epoch)return 0;uintptr_t value=(uintptr_t)p;unsigned found=0;for(unsigned i=0;i<3;i++){BankSource*s=&sources[i];uintptr_t base=(uintptr_t)s->base;if((i==0&&s->base!=(const unsigned char*)g_GameState.loaded_scene_assets)||(i==1&&s->base!=(const unsigned char*)g_GameState.voice_bank_base_1400)||(i==2&&s->base!=(const unsigned char*)g_GameState.entity_texture_blob))continue;if(!base||s->epoch==UINT64_MAX||value<base||value-base>=s->bytes||needed>s->bytes-(value-base))continue;if(found)return 0;found=i+1;}if(!found)return 0;BankSource*s=&sources[found-1];*id=found;*offset=(uint32_t)(value-(uintptr_t)s->base);*available=s->bytes-*offset;*epoch=s->epoch;return 1;}
