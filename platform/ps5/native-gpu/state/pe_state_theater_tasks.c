#include "pe_state_field_models.h"
#include "pe_state_theater_tasks.h"
#include "pe1/room_m005.h"
#include "pe1/room_m023_effects.h"
#include "pe1/room_m023_beacon.h"
#include "pe1/room_flare.h"
#include "pe1/room_module.h"
#include "pe1/room_m005_beacon.h"
#include "pe1/room_script_record.h"
#include <string.h>
#include <limits.h>
/* Native pointers are reconstructed only from exact P117 table/script roles,
 * aligned bounded arena offsets and known callback-list schemas. Never call
 * a constructor: original initialization sends audio and consumes game RNG. */
static uint32_t rd(const unsigned char*p){return(uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void wr(unsigned char*p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(v>>(8*i));}
extern u16 g_FieldFxTaskScriptCmd85[22],g_FieldFxTaskScriptCmd86[18],g_FieldFxTaskScriptCmd87[18];
int FieldFx_ScatterBurstEmitter(int,void*,intptr_t);
int FieldFx_CosineSparkEmitter(int,void*,intptr_t);
int FieldFx_RisingSpriteEmitter(int,void*,intptr_t);
static int task_resident(unsigned p){return p>=85&&p<=90;}
static int task_recovery(unsigned p){return p>=85&&p<=87;}
/*0no children,1falling,2scatter,3cosine,4rising.*/
static unsigned resident_kind(unsigned p,unsigned id){if(task_recovery(p))return id==0?1:0;if(p==88)return 2;if(p==89)return id==0?3:4;return id==0?2:id==1?4:3;}
static unsigned resident_max(unsigned p){return p==88?0:p==90?2:1;}
static unsigned resident_payload(unsigned p,unsigned id){return task_recovery(p)?4:resident_kind(p,id)==3?16:12;}
static unsigned resident_entries(unsigned p,unsigned id){switch(resident_kind(p,id)){case 1:return 16;case 2:return 12;case 3:return 24;case 4:return 8;default:return 0;}}
static FieldFxTaskCallback resident_callback(unsigned p,unsigned id){switch(resident_kind(p,id)){case 1:return FieldFx_FallingSprite;case 2:return FieldFx_ScatterBurstSprite;case 3:return FieldFx_CosineEffectB;case 4:return FieldFx_RisingSprite;default:return 0;}}
extern u16 g_FieldFxTaskScriptCmd88[12],g_FieldFxTaskScriptCmd89[16],g_FieldFxTaskScriptCmd90[22];
static FieldFxTaskProgram*task_descriptor(unsigned p){switch(p){case 85:return &g_FieldFxTaskProgramCmd85;case 86:return &g_FieldFxTaskProgramCmd86;case 87:return &g_FieldFxTaskProgramCmd87;case 88:return &g_FieldFxTaskProgramCmd88;case 89:return &g_FieldFxTaskProgramCmd89;case 90:return &g_FieldFxTaskProgramCmd90;case 115:return &g_RoomOrbitFlareEffects;case 117:return &g_RoomM005Effects;case 118:return &g_RoomM023Effects;default:return 0;}}
static u16*task_script(unsigned p){switch(p){case 85:return g_FieldFxTaskScriptCmd85;case 86:return g_FieldFxTaskScriptCmd86;case 87:return g_FieldFxTaskScriptCmd87;case 88:return g_FieldFxTaskScriptCmd88;case 89:return g_FieldFxTaskScriptCmd89;case 90:return g_FieldFxTaskScriptCmd90;case 115:return g_RoomOrbitFlareScript;case 117:return g_RoomM005EffectScript;case 118:return g_RoomM023EffectScript;default:return 0;}}
static unsigned task_script_words(unsigned p){return p==85||p==90?22u:p==88?12u:p==89?16u:p==86||p==87?18u:p==118?63u:30u;}

/* Eight resident recovery/ailment tracks own only lazily-derived numeric timing caches.
 * Colour keys stay immutable source identity; no callback or RNG is executed. */
extern unsigned char g_FallingSpriteColorTrack0[40],g_FallingSpriteColorTrack1[40],g_BouncingSpriteColorTrack2[40],g_RotatingEffectColorTrack[40],g_PulsingStarFanColorTrack0[40],g_PulsingStarFanColorTrack1[40];
extern unsigned char g_CosineSparkColorTrack[40],g_RisingSpriteColorTrack[48];
static int recovery_tracks(const unsigned char*in,unsigned char*out,int apply){
 unsigned char*tracks[8]={g_FallingSpriteColorTrack0,g_FallingSpriteColorTrack1,g_BouncingSpriteColorTrack2,g_RotatingEffectColorTrack,g_PulsingStarFanColorTrack0,g_PulsingStarFanColorTrack1,g_CosineSparkColorTrack,g_RisingSpriteColorTrack};
 for(unsigned n=0;n<8;n++){unsigned char*t=tracks[n];unsigned keys=n==7?4:3;uint32_t crc=~0u,total=0;for(unsigned k=0;k<=keys;k++){for(unsigned j=0;j<4;j++){crc^=t[8+k*8+j];for(unsigned b=0;b<8;b++)crc=(crc>>1)^(0xedb88320u&-(crc&1u));}if(k<keys){if(!t[11+k*8])return 0;total+=t[11+k*8];}else if(t[11+keys*8])return 0;}
  crc=~crc;unsigned initialized=rd(t)!=0;if(rd(t)!=(initialized?total:0)||rd(t+4)!=(initialized?keys:0u))return 0;unsigned start=0;for(unsigned k=0;k<=keys;k++){unsigned len=k<keys?t[11+k*8]:0;uint32_t timing=initialized&&k<keys?len|(start<<16):0;if(rd(t+12+k*8)!=timing)return 0;start+=len;}
  size_t at=192+n*8;unsigned wanted=out?initialized:rd(in+at+4);if(out){wr(out+at,crc);wr(out+at+4,initialized);}else if(rd(in+at)!=crc||wanted>1)return 0;
  if(apply){wr(t,wanted?total:0);wr(t+4,wanted?keys:0);start=0;for(unsigned k=0;k<=keys;k++){unsigned len=k<keys?t[11+k*8]:0;wr(t+12+k*8,wanted&&k<keys?len|(start<<16):0);start+=len;}}
 }return 1;
}
static size_t aligned(size_t n){size_t a=_Alignof(void*);return(n+a-1)/a*a;}
static int scalar(const unsigned char*i,unsigned char*o,size_t*at,void*p,unsigned width,int apply){uint32_t v=0;if(o){if(width==2){int16_t s;memcpy(&s,p,2);v=(uint32_t)(int32_t)s;}else memcpy(&v,p,4);wr(o+*at,v);}else{v=rd(i+*at);if(width==2&&((int32_t)v<INT16_MIN||(int32_t)v>INT16_MAX))return 0;if(apply){if(width==2){int16_t s=(int16_t)v;memcpy(p,&s,2);}else memcpy(p,&v,4);}}*at+=4;return 1;}
static int state(const unsigned char*i,unsigned char*o,size_t*at,void*p,unsigned program,unsigned id,int apply){
 if(task_resident(program)){if(id>resident_max(program))return 0;if(task_recovery(program))return id?1:scalar(i,o,at,p,4,apply);for(unsigned k=0;k<3;k++)if(!scalar(i,o,at,(char*)p+k*2,2,apply))return 0;if(!scalar(i,o,at,(char*)p+8,4,apply))return 0;if(apply)memset((char*)p+6,0,2);return resident_kind(program,id)!=3||scalar(i,o,at,(char*)p+12,4,apply);}
 if(program==118){if(id>2)return 0;if(id==0){RoomM023ScatterState*s=p;uint32_t joint=o?(uint32_t)s->joint:0;if(!o)joint=rd(i+*at+12);if(joint!=8&&joint!=16)return 0;for(unsigned j=0;j<4;j++)if(!scalar(i,o,at,(char*)s+8+j*2,2,apply))return 0;return 1;}if(id==1){RoomM023JointGlow*s=p;uint32_t joint=o?(uint32_t)s->joint:rd(i+*at);if(joint!=8&&joint!=16)return 0;for(unsigned j=0;j<3;j++)if(!scalar(i,o,at,(char*)s+j*2,2,apply))return 0;return 1;}RoomM023Beacon*s=p;uint32_t joint=o?(uint32_t)s->joint:rd(i+*at+14*4);if(joint!=8&&joint!=16)return 0;GteShortVector*v[3]={&s->position,&s->target,&s->rotation};for(unsigned j=0;j<3;j++)for(unsigned k=0;k<4;k++)if(!scalar(i,o,at,(char*)v[j]+k*2,2,apply))return 0;int*ints[4]={&s->height,&s->width,&s->joint,&s->armed};for(unsigned j=0;j<4;j++)if(!scalar(i,o,at,ints[j],4,apply))return 0;for(unsigned j=0;j<4;j++)if(!scalar(i,o,at,(char*)&s->phase+j*2,2,apply))return 0;return 1;}
 if(program==115){if(id)return 0;for(unsigned j=0;j<10;j++)if(!scalar(i,o,at,(char*)p+j*2,2,apply))return 0;return 1;}
 if(id==0){RoomM005OrbiterState *s=p;/* first eight bytes are not consumed by the original callback */
  if(!scalar(i,o,at,&s->armed,2,apply)||!scalar(i,o,at,&s->frame,2,apply))return 0;
 }else if(id==1){/* CrossFlare never reads its historical four-byte payload. */}
 else if(id==2){RoomM005Beacon*s=p;GteShortVector*v[3]={&s->position,&s->target,&s->rotation};for(unsigned k=0;k<3;k++)for(unsigned j=0;j<4;j++)if(!scalar(i,o,at,(char*)v[k]+j*2,2,apply))return 0;if(!scalar(i,o,at,&s->height,4,apply)||!scalar(i,o,at,&s->width,4,apply)||!scalar(i,o,at,&s->phase,2,apply)||!scalar(i,o,at,&s->timer,2,apply))return 0;
 }else return 0;return 1;
}
/* A single walk provides the writer and strict reader. Input is completely
 * prevalidated before Restore touches the live context. */
static int walk(FieldFxTaskContext*c,const unsigned char*i,unsigned char*o,int apply,unsigned*needsModels){
 if(needsModels)*needsModels=0;const size_t arenaBytes=sizeof(c->arena);uint32_t pc,used,count,flags,delay,program=0;if(o){const unsigned allowed[9]={85,86,87,88,89,90,115,117,118};for(unsigned j=0;j<9;j++)if(c->table==&task_descriptor(allowed[j])->table){program=allowed[j];break;}}else program=rd(i+28);FieldFxTaskProgram*descriptor=task_descriptor(program);if(!descriptor)return 0;u16*script=task_script(program);if(descriptor->script!=script)return 0;size_t at=32;unsigned live=0;unsigned char occupied[sizeof(c->arena)];memset(occupied,0,sizeof occupied);
 if(o){if(!task_resident(program)&&c->argument!=(program==115?(intptr_t)g_RoomScriptArgs:(intptr_t)&g_RoomScriptRecord))return 0;uintptr_t a=(uintptr_t)c->pc.script,b=(uintptr_t)script;if(a<b||a-b>=(size_t)task_script_words(program)*2||(a-b)%6)return 0;pc=(uint32_t)((a-b)/2);used=c->used;count=c->count;flags=c->flags;delay=c->delay;if(used>arenaBytes||(uintptr_t)c->cursor!=(uintptr_t)c->arena+used)return 0;memset(o,0,PE_STATE_TASK_CONTEXT_BYTES);memcpy(o,"PETCTX05",8);wr(o+8,pc);wr(o+12,used);wr(o+16,count);wr(o+20,flags);wr(o+24,delay);wr(o+28,program);
 }else{if(memcmp(i,"PETCTX05",8))return 0;pc=rd(i+8);used=rd(i+12);count=rd(i+16);flags=rd(i+20);delay=rd(i+24);}
 if(pc+3u>task_script_words(program)||pc%3||used>arenaBytes||count>8||flags>1||(task_resident(program)&&flags)||delay>65535)return 0;
 for(unsigned j=0;j<7;j++){if(task_resident(program)){int32_t value=o?c->variables[j]:(int32_t)rd(i+at);int32_t expected=j==6&&pc?(int32_t)program-85:0;if(value!=expected)return 0;}if(j==0){int32_t variant=o?c->variables[j]:(int32_t)rd(i+at);if(variant<0||variant>(program==115?2:1))return 0;}if(!scalar(i,o,&at,&c->variables[j],2,apply))return 0;}
 for(unsigned k=0;k<8;k++){
  FieldFxTaskSlot*s=&c->slots[k];uint32_t id=o?s->id:rd(i+at),age=o?s->age:rd(i+at+4),offset=0;
  if(o&&id!=65535){uintptr_t p=(uintptr_t)s->start,b=(uintptr_t)c->arena;if(p<b||p-b>arenaBytes)return 0;offset=(uint32_t)(p-b);}else if(!o)offset=rd(i+at+8);
  if(o){wr(o+at,id);wr(o+at+4,id==65535?0:age);wr(o+at+8,offset);}at+=12;
  if(id==65535){if(!o&&(age||offset))return 0;if(apply){s->id=65535;s->age=0;s->start=s->end=0;}continue;}
  /* JointBeacon additionally mutates model UV source bytes; that owner is not yet included. */
  if(id>(task_resident(program)?resident_max(program):program==115?0:2)||(id==2&&!task_resident(program)&&!PeFieldModels_Ready(program,o!=0))||age>65535||offset%_Alignof(void*))return 0;live++;if(id==2&&!task_resident(program)&&needsModels)*needsModels=1;
  const size_t payload=aligned(task_resident(program)?resident_payload(program,id):program==115?sizeof(RoomOrbitFlareState):program==118?(id==0?sizeof(RoomM023ScatterState):id==1?8:sizeof(RoomM023Beacon)):id==0?sizeof(RoomM005OrbiterState):id==1?4:sizeof(RoomM005Beacon));const unsigned entries=task_resident(program)?resident_entries(program,id):program==115?16:program==118?(id==0?12:0):id==0?24:id==2?8:0;const size_t stride=aligned(sizeof(FieldFxCallbackListEntry)+(task_resident(program)&&resident_kind(program,id)==3?8:program==115?12:16)),listBytes=entries?sizeof(FieldFxCallbackList)+entries*stride:0,total=payload+listBytes;
  if(offset>used||total>used-offset)return 0;
  for(size_t z=offset;z<offset+total;z++){if(occupied[z])return 0;occupied[z]=1;}
  char*start=c->arena+offset;FieldFxCallbackList*l=(FieldFxCallbackList*)(start+payload);
  if(o&&(s->end!=(entries?(char*)l:0)||(entries&&(l->stride!=(int)stride||l->count!=(int)entries||l->callback!=(task_resident(program)?resident_callback(program,id):program==115?RoomEffect_OrbitFlareDrift:program==118?RoomM023_DriftPulseCallback:id==0?RoomM005_FxOrbiter:RoomM005_BeaconDriftCallback)))))return 0;
  if(!state(i,o,&at,start,program,id,apply))return 0;
  if(entries){for(unsigned q=0;q<entries;q++){FieldFxCallbackListEntry*e=(void*)(l->entries+q*stride);uint32_t active=o?(uint32_t)e->active:rd(i+at),elapsed=o?(uint32_t)(int32_t)e->age:rd(i+at+4);if(active>1||(int32_t)elapsed<INT16_MIN||(int32_t)elapsed>INT16_MAX)return 0;if(o){wr(o+at,active);wr(o+at+4,active?elapsed:0);}else if(!active&&elapsed)return 0;at+=8;if(apply){e->active=(int16_t)active;e->age=(int16_t)elapsed;}if(active){if(task_resident(program)){unsigned kind=resident_kind(program,id);if(kind!=4){unsigned off=kind==3?3:6;int32_t duration=o?(kind==3?((RenderCosineEffect*)e->state)->duration:((RenderBouncingSprite*)e->state)->duration):(int32_t)rd(i+at+off*4);int lo=kind==1?26:kind==2?17:12;if(duration<lo||duration>lo+3||elapsed>(uint32_t)duration)return 0;}else if(elapsed>24)return 0;}/* both known children contain eight original s16 fields, no pointers */if(program==115){for(unsigned z=0;z<4;z++)if(!scalar(i,o,&at,e->state+z*2,2,apply))return 0;if(!scalar(i,o,&at,e->state+8,4,apply))return 0;}else if(task_resident(program)&&resident_kind(program,id)==4){for(unsigned z=0;z<7;z++){uint32_t v;if(o){uint16_t word;memcpy(&word,e->state+z*2,2);wr(o+at,word);}else{v=rd(i+at);if(v>65535)return 0;if(apply){uint16_t word=v;memcpy(e->state+z*2,&word,2);}}at+=4;}}else for(unsigned z=0;z<(task_resident(program)&&resident_kind(program,id)==3?4:8);z++)if(!scalar(i,o,&at,e->state+z*2,2,apply))return 0;}}}
  if(apply){s->id=(u16)id;s->age=(u16)age;s->start=start;s->end=entries?(char*)l:0;if(entries){l->stride=(int)stride;l->count=(int)entries;l->callback=task_resident(program)?resident_callback(program,id):program==115?RoomEffect_OrbitFlareDrift:program==118?RoomM023_DriftPulseCallback:id==0?RoomM005_FxOrbiter:RoomM005_BeaconDriftCallback;}}
 }
 if(live!=count||at>PE_STATE_TASK_CONTEXT_BYTES)return 0;if(!o)for(size_t z=at;z<PE_STATE_TASK_CONTEXT_BYTES;z++)if(i[z])return 0;
 if(apply){c->table=&descriptor->table;c->argument=task_resident(program)?0:program==115?(intptr_t)g_RoomScriptArgs:(intptr_t)&g_RoomScriptRecord;c->pc.script=script+pc;c->cursor=c->arena+used;c->used=(u16)used;c->count=(u8)count;c->flags=(u8)flags;c->delay=(u16)delay;}return 1;
}
int PeStateTaskContext_Capture(const FieldFxTaskContext*c,unsigned char*o,size_t n){return c&&o&&n==PE_STATE_TASK_CONTEXT_BYTES&&walk((FieldFxTaskContext*)c,0,o,0,0)&&PeStateTaskContext_Validate(o,n);}
int PeStateTaskContext_Validate(const unsigned char*i,size_t n){FieldFxTaskContext fixture;memset(&fixture,0,sizeof fixture);return i&&n==PE_STATE_TASK_CONTEXT_BYTES&&walk(&fixture,i,0,0,0);}
int PeStateTaskContext_Restore(FieldFxTaskContext*c,const unsigned char*i,size_t n){return c&&PeStateTaskContext_Validate(i,n)&&walk(c,i,0,1,0);}

int PeStateTaskContext_ModelRequired(const unsigned char*i,size_t n){FieldFxTaskContext fixture;unsigned required=0;memset(&fixture,0,sizeof fixture);return i&&n==PE_STATE_TASK_CONTEXT_BYTES&&walk(&fixture,i,0,0,&required)?(int)required:-1;}
#include "pe_state_world.h"
#include "pe_state_assets.h"
#include "pe_state_boundary.h"
#include "pe1/pm.h"
#include "pe1/entity_allocation.h"
#include "psyz/state_audio.h"
#include "psyz/state_producers.h"
extern void *g_FieldFxTaskPrograms[104];
#define TASK_RECORD (16u+PE_STATE_TASK_CONTEXT_BYTES)
#define TASK_TOTAL (320u+11u*TASK_RECORD)
#include "pe1/scene_room_load.h"
#include "pe1/fieldfx_state.h"
extern s16 g_FieldFxLoadedTexturePage;
static uint32_t resident_crc(const unsigned char*p,size_t n){uint32_t c=~0u;for(size_t j=0;j<n;j++){c^=p[j];for(unsigned b=0;b<8;b++)c=(c>>1)^(0xedb88320u&-(c&1u));}return ~c;}
static int resident_key_offset(uint32_t key,uint32_t*out){const unsigned char*b=g_StartupResourceBlob;size_t bytes=sizeof g_StartupResourceBlob;uint32_t header=rd(b+4);if(header>bytes||bytes-header<12)return 0;uint32_t desc=rd(b+header+8),off=desc&0x3fffffu,count=desc>>22;if(off>bytes||count>(bytes-off)/12)return 0;unsigned found=0;for(unsigned j=0;j<count;j++)if(rd(b+off+j*12+8)==key){uint32_t data=rd(b+off+j*12+4)&0xffffffu;if(found++||data>bytes-32768)return 0;*out=data;}return found==1;}
static int resident_resources(const unsigned char*in,unsigned char*out,int apply){if(g_GameState.startup_resource_blob!=g_StartupResourceBlob)return 0;uint32_t offset[2];void*page[2]={g_FieldEffectTexturePage0,g_FieldEffectTexturePage1};for(unsigned j=0;j<2;j++){uintptr_t a=(uintptr_t)page[j],base=(uintptr_t)g_StartupResourceBlob;if(a<base||a-base>sizeof(g_StartupResourceBlob)-32768u)return 0;offset[j]=(uint32_t)(a-base);uint32_t expected;if(!resident_key_offset(j?0x57D41D84u:0x57D40D84u,&expected)||offset[j]!=expected)return 0;}int32_t selector=out?g_FieldFxLoadedTexturePage:(int32_t)rd(in+256);if(selector<-1||selector>1)return 0;uint32_t crc=resident_crc(g_StartupResourceBlob,sizeof g_StartupResourceBlob);unsigned char tint[12];memcpy(tint,&g_FieldFxGlowFountainTint,4);memcpy(tint+4,&g_FieldFxArcingEmitterTint,4);memcpy(tint+8,&g_FieldFxCosineEffectTint,4);uint32_t tintcrc=resident_crc(tint,sizeof tint);if(out){wr(out+256,selector);wr(out+260,crc);wr(out+264,offset[0]);wr(out+268,offset[1]);wr(out+272,32768);wr(out+276,tintcrc);}else if(rd(in+260)!=crc||rd(in+264)!=offset[0]||rd(in+268)!=offset[1]||rd(in+272)!=32768||rd(in+276)!=tintcrc)return 0;if(apply)g_FieldFxLoadedTexturePage=(s16)selector;return 1;}
static unsigned task_actor(const void*p){if(!p)return 0;for(unsigned j=0;j<14;j++)if(p==&g_EntityPool[j])return j+1;return UINT32_MAX;}
static int task_source(unsigned command){if(task_resident(command)){FieldFxTaskProgram*d=task_descriptor(command);if(!g_PmCmdHandlerTable||g_PmCmdHandlerTable[85]!=&g_FieldFxTaskClass||g_FieldFxTaskPrograms[command-85]!=d||d->script!=task_script(command)||d->initialize)return 0;FieldFxTaskCallback expected[3]={0};if(task_recovery(command)){expected[0]=FieldFx_FallingSpriteEmitter;expected[1]=command==85?FieldFx_RotatingEffect:FieldFx_PulsingStarFan;}else if(command==88)expected[0]=FieldFx_ScatterBurstEmitter;else if(command==89){expected[0]=FieldFx_CosineSparkEmitter;expected[1]=FieldFx_RisingSpriteEmitter;}else{expected[0]=FieldFx_ScatterBurstEmitter;expected[1]=FieldFx_RisingSpriteEmitter;expected[2]=FieldFx_CosineSparkEmitter;}for(unsigned k=0;k<=resident_max(command);k++)if(d->table.callbacks[k]!=expected[k]||d->table.sizes[k]!=(int)resident_payload(command,k))return 0;return 1;}if(command==117||command==118){PeScriptAsset asset;uint32_t native;if(!PeStateAssets_Get(&asset)||asset.room!=(command==117?5u:23u)||!PeStateAssets_NativeDescriptors(&native)||native!=1)return 0;}if(!g_PmCmdHandlerTable||g_PmCmdHandlerTable[85]!=&g_FieldFxTaskClass)return 0;if(command==118)return g_FieldFxTaskPrograms[118-85]==&g_RoomM023Effects&&g_RoomM023Effects.table.callbacks[0]==RoomM023_ScatterEmitter&&g_RoomM023Effects.table.callbacks[1]==RoomM023_JointGlow&&g_RoomM023Effects.table.callbacks[2]==RoomM023_JointBeacon&&g_RoomM023Effects.table.sizes[0]==sizeof(RoomM023ScatterState)&&g_RoomM023Effects.table.sizes[1]==8&&g_RoomM023Effects.table.sizes[2]==sizeof(RoomM023Beacon)&&g_RoomM023Effects.script==g_RoomM023EffectScript&&g_RoomM023Effects.initialize==RoomLib_SetTaskVar0;if(command==115)return g_FieldFxTaskPrograms[115-85]==&g_RoomOrbitFlareEffects&&g_RoomOrbitFlareEffects.table.callbacks[0]==RoomEffect_OrbitFlareController&&g_RoomOrbitFlareEffects.table.sizes[0]==sizeof(RoomOrbitFlareState)&&g_RoomOrbitFlareEffects.script==g_RoomOrbitFlareScript&&g_RoomOrbitFlareEffects.initialize==RoomLib_SetArgs3;return command==117&&g_FieldFxTaskPrograms[117-85]==&g_RoomM005Effects&&g_RoomM005Effects.table.callbacks[0]==RoomM005_Orbiter&&g_RoomM005Effects.table.callbacks[1]==RoomM005_CrossFlare&&g_RoomM005Effects.table.callbacks[2]==RoomM005_JointBeacon&&g_RoomM005Effects.table.sizes[0]==sizeof(RoomM005OrbiterState)&&g_RoomM005Effects.table.sizes[1]==4&&g_RoomM005Effects.table.sizes[2]==sizeof(RoomM005Beacon)&&g_RoomM005Effects.script==g_RoomM005EffectScript&&g_RoomM005Effects.initialize==RoomLib_SetTaskVar0;}
int PeStateTasks_HeaderSupported(unsigned index,unsigned status,unsigned command){return index<11&&status>=1&&status<=5&&(task_resident(command)||command==115||command==117||command==118)&&task_source(command);}
/* PM12 fixed prefix ends before the three original room script arguments.
 * This is the exact existing checkpoint.c PM_BYTES expression, not a pointer. */
#define TASK_PM_PREFIX_BYTES (24u+22u*20u+304u+104u*4u)
#include "pe_state_bank_sources.h"
static int task_joint_header(const void*pointer,unsigned command,int32_t attached){unsigned source;uint32_t offset;size_t available;uint64_t epoch;unsigned needed=command==118?17u:command==117?21u:command==115?(attached?1u:26u):0u;if(!needed||!PeStateBankSource_Locate(pointer,sizeof(RenderObjectHeader),&source,&offset,&available,&epoch)||!epoch)return 0;const RenderObjectHeader*h=pointer;return h->part_count>=needed;}
static int task_world(const PeWorldState*w,unsigned id,int apply,unsigned command,int32_t attached){if(!id||id>14)return 0;unsigned n=id-1;if(w&&w->targetMode){uint32_t types[14];for(unsigned j=0;j<14;j++)types[j]=w->actor[j].type;if(w->targetMode!=1||!PeWorld_ValidateTargetConstruction(w,types,w->liveMask)||!(w->liveMask&(1u<<n)))return 0;if(!apply)return task_resident(command)?(!task_recovery(command)||id==w->constructorPlayerId):task_joint_header(w->actor[n].header,command,attached);if(g_EntityPool[n].type_id!=w->actor[n].type||g_EntityPool[n].sub_id!=w->actor[n].sub||g_EntityPool[n].model_state.model_header!=w->actor[n].model||g_EntityPool[n].render_object.header!=w->actor[n].header)return 0;}
 unsigned visited=0;uint32_t mask=0;for(FieldActor*p=g_FieldActorListHead;p;p=p->next){unsigned j=task_actor(p);if(!j||j>14||visited++>=14||(mask&(1u<<(j-1))))return 0;mask|=1u<<(j-1);}return(mask&(1u<<n))&&(task_resident(command)?(!task_recovery(command)||id==task_actor(g_PlayerEntity)):(g_EntityPool[n].render_object.header&&g_EntityPool[n].render_object.matrices&&task_joint_header(g_EntityPool[n].render_object.header,command,attached)));
}
/* Held programs may touch owner/player combat flags in future updates. All
 * three authored scripts start with opcode5,0,0, so PC0 also requires these
 * nonnullable typed roles before that next update raises context.flags. */
static int task_cores(const PeWorldState*w,unsigned owner,int apply,int needed){if(!needed)return 1;if(!owner||owner>14)return 0;if(w&&w->targetMode&&!apply){unsigned player=w->constructorPlayerId;if(!player||player>14||!(w->liveMask&(1u<<(player-1))))return 0;const PeWorldBinding*o=&w->actor[owner-1],*p=&w->actor[player-1];return o->state&&p->state&&o->coreRole>0&&o->coreRole<=8&&p->coreRole>0&&p->coreRole<=8;}unsigned player=task_actor(g_PlayerEntity);if(!player||player>14)return 0;unsigned ids[2]={owner,player};for(unsigned j=0;j<2;j++){FieldActor*a=&g_EntityPool[ids[j]-1];uint32_t core,frame,contact;if(!a->state||!PeWorld_NativeActorRoles(a,&core,&frame,&contact)||!core)return 0;if(w&&w->targetMode&&(a->state!=w->actor[ids[j]-1].state||core!=w->actor[ids[j]-1].coreRole))return 0;}return !w||!w->targetMode||player==w->constructorPlayerId;}
static unsigned taskBlocker;static uint64_t taskBlockerScene;
void PeStateTasks_ResetBlocker(void){taskBlocker=0;taskBlockerScene=PeStateBoundary_SceneEpoch();}
const char *PeStateTasks_Blocker(void){if(taskBlockerScene!=PeStateBoundary_SceneEpoch())return 0;switch(taskBlocker){case 1:return "This room effect is not supported for checkpoints yet";case 2:return "This effect's actor layout is unsupported; try after it ends";case 3:return "Player/enemy combat data is not ready; try after the effect ends";case 4:return "Effect resources are not ready for checkpoints; try after it ends";default:return 0;}}
int PeStateTasks_CurrentSupported(unsigned index){taskBlocker=1;taskBlockerScene=PeStateBoundary_SceneEpoch();if(index>=11||!g_PmSlotTable)return 0;PmSlotHeader*h=&g_PmSlotTable[index].header;if(h->field02!=1||h->field03!=0||!PeStateTasks_HeaderSupported(index,h->state,h->command))return 0;taskBlocker=2;if(!task_world(0,task_actor(h->owner),1,h->command,g_RoomScriptArgs[2]))return 0;FieldFxTaskContext*ctx=&g_PmSlotTable[index].task.tasks;int needed=!task_resident(h->command)&&(ctx->flags||ctx->pc.script==((FieldFxTaskProgram*)g_FieldFxTaskPrograms[h->command-85])->script);taskBlocker=3;if(!task_cores(0,task_actor(h->owner),1,needed))return 0;taskBlocker=4;unsigned char data[PE_STATE_TASK_CONTEXT_BYTES];if(!PeStateTaskContext_Capture(ctx,data,sizeof data))return 0;taskBlocker=0;return 1;}

static unsigned task_role(const void*p,int kind){if(!p)return 0;for(unsigned j=0;j<11;j++){FieldFxTaskOwner*o=&g_PmSlotTable[j].task;if(kind==0&&p==&o->prefix)return j+1;if(kind==1&&p==&o->tasks)return j+1;if(kind==2)for(unsigned k=0;k<8;k++)if(p==&o->tasks.slots[k])return 1+j*8+k;}return UINT32_MAX;}
int PeStateTasks_Constructed(const PeWorldState*w,const unsigned char*h,const unsigned char*d){if(!h||!d||!g_PmSlotTable)return 0;for(unsigned j=0;j<11;j++)if(rd(h+24+j*20)&&(task_resident(rd(h+28+j*20))||rd(h+28+j*20)==115||rd(h+28+j*20)==117||rd(h+28+j*20)==118)){if(!PeStateTasks_HeaderSupported(j,rd(h+24+j*20),rd(h+28+j*20))||!task_world(w,rd(d+320+j*TASK_RECORD),1,rd(h+28+j*20),(int32_t)rd(h+TASK_PM_PREFIX_BYTES+8))||!task_cores(w,rd(d+320+j*TASK_RECORD),1,!task_resident(rd(h+28+j*20))&&(rd(d+320+j*TASK_RECORD+16+20)||!rd(d+320+j*TASK_RECORD+16+8))))return 0;}return 1;}
int PeStateTasks_WalkForWorld(const PeWorldState*w,const unsigned char*header,const unsigned char*theater,const unsigned char*in,unsigned char*out,int apply){if((!header||!in)&&!out)return 0;if(apply&&(out||!PeStateTasks_WalkForWorld(w,header,theater,in,0,0)||!PeStateTasks_Constructed(w,header,in)))return 0;if(!g_PmSlotTable||!g_PmSlotTable2)return 0;if(apply&&(!PeStateBoundary_AtCpuPoint()||!Psyz_StateKernelOwned()||!Psyz_StateAudioOwned()))return 0;if(out){memset(out,0,TASK_TOTAL);memcpy(out,"PETASK03",8);}else if(memcmp(in,"PETASK03",8))return 0;
 unsigned any117=0;for(unsigned j=0;j<11;j++){uint32_t status=out?g_PmSlotTable[j].header.state:rd(header+24+j*20),command=out?g_PmSlotTable[j].header.command:rd(header+28+j*20);if(status&&(task_resident(command)||command==115||command==117||command==118))any117=1;}
 uint32_t roles[4];if(out){roles[0]=task_role(g_FieldFxOwner,0);roles[1]=task_role(g_FieldFxTaskContext,1);roles[2]=task_role(g_FieldFxActiveTaskSlot,2);roles[3]=(uint32_t)g_FieldFxCurrentAge;if(!any117)roles[0]=roles[1]=roles[2]=0;}else for(unsigned j=0;j<4;j++)roles[j]=rd(in+8+j*4);if(roles[0]>11||roles[1]>11||roles[2]>88||(!any117&&(roles[0]||roles[1]||roles[2])))return 0;if(out)for(unsigned j=0;j<4;j++)wr(out+8+j*4,roles[j]);else if(rd(in+24)||rd(in+28))return 0;

 /* Shared original draw parameter state is numeric; resource lookup tables
  * remain source identity, not writable pointer/callback roles. */
 size_t shared=32;
 for(unsigned j=0;j<9;j++)if(!scalar(in,out,&shared,(char*)&g_FieldFxSpriteParams+j*2,2,apply))return 0;
 if((out?g_FieldFxSpriteParams.palette:(uint16_t)rd(in+40))>=6)return 0;
 if(!scalar(in,out,&shared,&g_FieldFxAltPaletteFlag,4,apply))return 0;
 for(unsigned j=0;j<16;j++){uint32_t v=g_FieldFxTpageSlots[j];if(v>3)return 0;if(out)wr(out+shared,v);else if(rd(in+shared)!=v)return 0;shared+=4;}
 for(unsigned j=0;j<4;j++){if(out)wr(out+shared,g_FieldFxTpageTable[j]);else if(rd(in+shared)!=g_FieldFxTpageTable[j])return 0;shared+=4;}
 for(unsigned j=0;j<6;j++){if(out)wr(out+shared,g_FieldFxPaletteTable[j]);else if(rd(in+shared)!=g_FieldFxPaletteTable[j])return 0;shared+=4;}

 /* Opcode2E produces a pointer into immutable current script source. Own the
  * source offset/count, never an actor/process address or copied script heap. */
 PeScriptAsset asset;if(!PeStateAssets_Get(&asset))return 0;
 uint32_t polygonCount=out?g_FloorPolygonVertexCount:rd(in+shared),present=out?(g_FloorPolygonVertices!=0):rd(in+shared+4),offset=out?0:rd(in+shared+8);
 if(polygonCount>512||present>1||(!present&&polygonCount))return 0;
 if(out&&present){uintptr_t pointer=(uintptr_t)g_FloorPolygonVertices,base=(uintptr_t)asset.data;if(pointer<base||pointer-base>UINT32_MAX)return 0;offset=(uint32_t)(pointer-base);}
 size_t polygonBytes=(size_t)(polygonCount?polygonCount:1)*sizeof(PolygonVertex);
 if(present&&(offset%_Alignof(PolygonVertex)||offset>asset.bytes||polygonBytes>asset.bytes-offset))return 0;if(!present&&offset)return 0;
 if(out){wr(out+shared,polygonCount);wr(out+shared+4,present);wr(out+shared+8,offset);}else if(apply){g_FloorPolygonVertexCount=(u16)polygonCount;g_FloorPolygonVertices=present?(PolygonVertex*)(asset.data+offset):0;}shared+=12;
 if(!scalar(in,out,&shared,&g_RoomFloorY[0].y,2,apply))return 0;
 if(shared!=192||!recovery_tracks(in,out,apply)||!resident_resources(in,out,apply))return 0; if(!out)for(size_t j=280;j<320;j++)if(in[j])return 0;
 for(unsigned j=0;j<11;j++){PmSlotHeader*h=&g_PmSlotTable[j].header;uint32_t status=out?h->state:rd(header+24+j*20),command=out?h->command:rd(header+28+j*20);size_t at=320+j*TASK_RECORD;int active=status&&(task_resident(command)||command==115||command==117||command==118);if(!active){if(!out)for(size_t z=0;z<TASK_RECORD;z++)if(in[at+z])return 0;continue;}if(!PeStateTasks_HeaderSupported(j,status,command)||(out?h->field02:rd(header+32+j*20))!=1||(out?h->field03:rd(header+36+j*20))!=0)return 0;uint32_t actor=out?task_actor(h->owner):rd(in+at);if(!out&&rd(in+at+16+28)!=command)return 0;if(!task_world(w,actor,apply,command,out?g_RoomScriptArgs[2]:(int32_t)rd(header+TASK_PM_PREFIX_BYTES+8)))return 0;int coreNeeded=!task_resident(command)&&(out?(g_PmSlotTable[j].task.tasks.flags||g_PmSlotTable[j].task.tasks.pc.script==((FieldFxTaskProgram*)g_FieldFxTaskPrograms[command-85])->script):(rd(in+at+16+20)||!rd(in+at+16+8)));if(!task_cores(w,actor,apply,coreNeeded))return 0;if(out){wr(out+at,actor);if(!PeStateTaskContext_Capture(&g_PmSlotTable[j].task.tasks,out+at+16,PE_STATE_TASK_CONTEXT_BYTES)||rd(out+at+16+28)!=command)return 0;}else{int modelRequired=PeStateTaskContext_ModelRequired(in+at+16,PE_STATE_TASK_CONTEXT_BYTES);if(modelRequired<0||(modelRequired&&(!theater||(command==117?(rd(theater+612)!=1||rd(theater+616)!=1):rd(theater+268)!=1))))return 0;if(rd(in+at+4)||rd(in+at+8)||rd(in+at+12)||!PeStateTaskContext_Validate(in+at+16,PE_STATE_TASK_CONTEXT_BYTES))return 0;if(apply){h->owner=&g_EntityPool[actor-1];if(!PeStateTaskContext_Restore(&g_PmSlotTable[j].task.tasks,in+at+16,PE_STATE_TASK_CONTEXT_BYTES))return 0;}}}
 if(apply){g_FieldFxOwner=roles[0]?&g_PmSlotTable[roles[0]-1].task.prefix:0;g_FieldFxTaskContext=roles[1]?&g_PmSlotTable[roles[1]-1].task.tasks:0;g_FieldFxActiveTaskSlot=roles[2]?&g_PmSlotTable[(roles[2]-1)/8].task.tasks.slots[(roles[2]-1)%8]:0;g_FieldFxCurrentAge=(int32_t)roles[3];}return 1;
}









