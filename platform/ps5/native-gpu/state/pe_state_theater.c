#include "pe_state_theater.h"
#include "pe_state_assets.h"
#include "pe1/room_module.h"
#include "pe1/room_m005.h"
#include "pe1/room_m005_beacon.h"
#include "pe1/room_script_record.h"
#include "pe1/scene_archive.h"
#include "pe1/asset_table.h"
#include <string.h>
extern char RoomM005_Rec2[32];
static const void *sourceBlob;static uint64_t sourceGeneration;
static uint32_t get(const unsigned char *p){return(uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void put(unsigned char *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(v>>(i*8));}
int PeStateTheater_RoomSupported(uint32_t room,uint32_t descriptors){return !descriptors||(descriptors==1&&(room==5||room==12||room==13||room==27||room==30||room==17||room==18||room==21||room==45||room==102||room==151||room==319));}
size_t PeStateTheater_Bytes(void){PeScriptAsset a;return PeStateAssets_Get(&a)?12u+(a.room==5?608u:0u):0;}
int PeStateTheater_Walk(const unsigned char *in,unsigned char *out,int apply){PeScriptAsset a;if(!PeStateAssets_Get(&a))return 0;size_t at=0;
#define F(value,kind) do{uint32_t v=out?(uint32_t)(value):get(in+at);if(kind==1&&v>255)return 0;if(kind==2&&((int32_t)v< -32768||(int32_t)v>32767))return 0;if(out)put(out+at,v);else if(apply)(value)=(__typeof__(value))v;at+=4;}while(0)
for(unsigned i=0;i<3;i++){F(g_RoomScriptArgs[i],0);}
if(a.room==5){if(out&&sourceGeneration!=a.generation){sourceGeneration=a.generation;sourceBlob=g_LoadedSceneAssetBlock[0];}if(sourceGeneration!=a.generation||sourceBlob!=g_LoadedSceneAssetBlock[0]||g_RoomM005ScriptAnchorView!=(u8 *)g_RoomScriptRecordWords+4||g_RoomM005ScriptFadeView!=(u8 *)g_RoomScriptRecordWords+12)return 0;
for(unsigned i=0;i<10;i++){F(g_RoomScriptRecordWords[i],2);}
GteShortVector *vectors[6]={&g_RoomM005BeaconOffset,&g_RoomM005BeaconCenter,&g_RoomM005OrbiterJointAnchor.vector,&g_RoomM005CrossFlareFlashAnchor.vector,&g_RoomM005CrossFlareTwinAnchor.vector,&g_RoomM005JointBeaconAnchor.vector};
for(unsigned i=0;i<6;i++){F(vectors[i]->x,2);F(vectors[i]->y,2);F(vectors[i]->z,2);F(vectors[i]->pad,2);}
F(g_RoomM005BeaconTint.r,1);F(g_RoomM005BeaconTint.g,1);F(g_RoomM005BeaconTint.b,1);F(g_RoomM005BeaconTint.code,1);
for(unsigned i=0;i<32;i++){F(((u8 *)RoomM005_Rec2)[i],1);}for(unsigned i=0;i<80;i++){F(g_RoomM005CrossFlareColorTrack[i],1);}
u8 **models[2]={&g_RoomM005BeaconModel0,&g_RoomM005BeaconModel1};const uint32_t keys[2]={0xC5462704u,0xC5862704u};for(unsigned i=0;i<2;i++){void *expected=sourceBlob?Asset_FindTable08ByU32Key((void *)sourceBlob,(int)keys[i]):0;uint32_t id=out?(!*models[i]?0:*models[i]==expected?1:UINT32_MAX):get(in+at);if(id>1||(id&&!expected))return 0;if(out)put(out+at,id);else if(apply)*models[i]=id?expected:0;at+=4;}
}
#undef F
return at==PeStateTheater_Bytes();}
