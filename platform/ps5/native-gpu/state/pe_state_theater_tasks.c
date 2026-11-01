#include "pe_state_field_models.h"
#include "pe_state_theater_tasks.h"
#include "pe1/room_m005.h"
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
static size_t aligned(size_t n){size_t a=_Alignof(void*);return(n+a-1)/a*a;}
static int scalar(const unsigned char*i,unsigned char*o,size_t*at,void*p,unsigned width,int apply){uint32_t v=0;if(o){if(width==2){int16_t s;memcpy(&s,p,2);v=(uint32_t)(int32_t)s;}else memcpy(&v,p,4);wr(o+*at,v);}else{v=rd(i+*at);if(width==2&&((int32_t)v<INT16_MIN||(int32_t)v>INT16_MAX))return 0;if(apply){if(width==2){int16_t s=(int16_t)v;memcpy(p,&s,2);}else memcpy(p,&v,4);}}*at+=4;return 1;}
static int state(const unsigned char*i,unsigned char*o,size_t*at,void*p,unsigned program,unsigned id,int apply){
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
 if(needsModels)*needsModels=0;const size_t arenaBytes=sizeof(c->arena);uint32_t pc,used,count,flags,delay,program=o?(c->table==&g_RoomM005Effects.table?117:c->table==&g_RoomOrbitFlareEffects.table?115:0):rd(i+28);if(program!=115&&program!=117)return 0;FieldFxTaskProgram*descriptor=program==117?&g_RoomM005Effects:&g_RoomOrbitFlareEffects;u16*script=program==117?g_RoomM005EffectScript:g_RoomOrbitFlareScript;if(descriptor->script!=script)return 0;size_t at=32;unsigned live=0;unsigned char occupied[sizeof(c->arena)];memset(occupied,0,sizeof occupied);
 if(o){if(c->argument!=(program==117?(intptr_t)&g_RoomScriptRecord:(intptr_t)g_RoomScriptArgs))return 0;uintptr_t a=(uintptr_t)c->pc.script,b=(uintptr_t)script;if(a<b||a-b>=sizeof(g_RoomM005EffectScript)||(a-b)%6)return 0;pc=(uint32_t)((a-b)/2);used=c->used;count=c->count;flags=c->flags;delay=c->delay;if(used>arenaBytes||(uintptr_t)c->cursor!=(uintptr_t)c->arena+used)return 0;memset(o,0,PE_STATE_TASK_CONTEXT_BYTES);memcpy(o,"PETCTX02",8);wr(o+8,pc);wr(o+12,used);wr(o+16,count);wr(o+20,flags);wr(o+24,delay);wr(o+28,program);
 }else{if(memcmp(i,"PETCTX02",8))return 0;pc=rd(i+8);used=rd(i+12);count=rd(i+16);flags=rd(i+20);delay=rd(i+24);}
 if(pc>=30||pc%3||used>arenaBytes||count>8||flags>1||delay>65535)return 0;
 for(unsigned j=0;j<7;j++)if(!scalar(i,o,&at,&c->variables[j],2,apply))return 0;
 for(unsigned k=0;k<8;k++){
  FieldFxTaskSlot*s=&c->slots[k];uint32_t id=o?s->id:rd(i+at),age=o?s->age:rd(i+at+4),offset=0;
  if(o&&id!=65535){uintptr_t p=(uintptr_t)s->start,b=(uintptr_t)c->arena;if(p<b||p-b>arenaBytes)return 0;offset=(uint32_t)(p-b);}else if(!o)offset=rd(i+at+8);
  if(o){wr(o+at,id);wr(o+at+4,id==65535?0:age);wr(o+at+8,offset);}at+=12;
  if(id==65535){if(!o&&(age||offset))return 0;if(apply){s->id=65535;s->age=0;s->start=s->end=0;}continue;}
  /* JointBeacon additionally mutates model UV source bytes; that owner is not yet included. */
  if(id>(program==117?2:0)||(id==2&&!PeFieldModels_Ready(program,o!=0))||age>65535||offset%_Alignof(void*))return 0;live++;if(id==2&&needsModels)*needsModels=1;
  const size_t payload=aligned(program==115?sizeof(RoomOrbitFlareState):id==0?sizeof(RoomM005OrbiterState):id==1?4:sizeof(RoomM005Beacon));const unsigned entries=program==115?16:id==0?24:id==2?8:0;const size_t stride=aligned(sizeof(FieldFxCallbackListEntry)+(program==115?12:16)),listBytes=entries?sizeof(FieldFxCallbackList)+entries*stride:0,total=payload+listBytes;
  if(offset>used||total>used-offset)return 0;
  for(size_t z=offset;z<offset+total;z++){if(occupied[z])return 0;occupied[z]=1;}
  char*start=c->arena+offset;FieldFxCallbackList*l=(FieldFxCallbackList*)(start+payload);
  if(o&&(s->end!=(entries?(char*)l:0)||(entries&&(l->stride!=(int)stride||l->count!=(int)entries||l->callback!=(program==115?RoomEffect_OrbitFlareDrift:id==0?RoomM005_FxOrbiter:RoomM005_BeaconDriftCallback)))))return 0;
  if(!state(i,o,&at,start,program,id,apply))return 0;
  if(entries){for(unsigned q=0;q<entries;q++){FieldFxCallbackListEntry*e=(void*)(l->entries+q*stride);uint32_t active=o?(uint32_t)e->active:rd(i+at),elapsed=o?(uint32_t)(int32_t)e->age:rd(i+at+4);if(active>1||(int32_t)elapsed<INT16_MIN||(int32_t)elapsed>INT16_MAX)return 0;if(o){wr(o+at,active);wr(o+at+4,active?elapsed:0);}else if(!active&&elapsed)return 0;at+=8;if(apply){e->active=(int16_t)active;e->age=(int16_t)elapsed;}if(active){/* both known children contain eight original s16 fields, no pointers */if(program==115){for(unsigned z=0;z<4;z++)if(!scalar(i,o,&at,e->state+z*2,2,apply))return 0;if(!scalar(i,o,&at,e->state+8,4,apply))return 0;}else for(unsigned z=0;z<8;z++)if(!scalar(i,o,&at,e->state+z*2,2,apply))return 0;}}}
  if(apply){s->id=(u16)id;s->age=(u16)age;s->start=start;s->end=entries?(char*)l:0;if(entries){l->stride=(int)stride;l->count=(int)entries;l->callback=program==115?RoomEffect_OrbitFlareDrift:id==0?RoomM005_FxOrbiter:RoomM005_BeaconDriftCallback;}}
 }
 if(live!=count||at>PE_STATE_TASK_CONTEXT_BYTES)return 0;if(!o)for(size_t z=at;z<PE_STATE_TASK_CONTEXT_BYTES;z++)if(i[z])return 0;
 if(apply){c->table=&descriptor->table;c->argument=program==117?(intptr_t)&g_RoomScriptRecord:(intptr_t)g_RoomScriptArgs;c->pc.script=script+pc;c->cursor=c->arena+used;c->used=(u16)used;c->count=(u8)count;c->flags=(u8)flags;c->delay=(u16)delay;}return 1;
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
#define TASK_TOTAL (256u+11u*TASK_RECORD)
static unsigned task_actor(const void*p){if(!p)return 0;for(unsigned j=0;j<14;j++)if(p==&g_EntityPool[j])return j+1;return UINT32_MAX;}
static int task_source(unsigned command){if(!g_PmCmdHandlerTable||g_PmCmdHandlerTable[85]!=&g_FieldFxTaskClass)return 0;if(command==115)return g_FieldFxTaskPrograms[115-85]==&g_RoomOrbitFlareEffects&&g_RoomOrbitFlareEffects.table.callbacks[0]==RoomEffect_OrbitFlareController&&g_RoomOrbitFlareEffects.table.sizes[0]==sizeof(RoomOrbitFlareState)&&g_RoomOrbitFlareEffects.script==g_RoomOrbitFlareScript&&g_RoomOrbitFlareEffects.initialize==RoomLib_SetArgs3;return command==117&&g_FieldFxTaskPrograms[117-85]==&g_RoomM005Effects&&g_RoomM005Effects.table.callbacks[0]==RoomM005_Orbiter&&g_RoomM005Effects.table.callbacks[1]==RoomM005_CrossFlare&&g_RoomM005Effects.table.callbacks[2]==RoomM005_JointBeacon&&g_RoomM005Effects.table.sizes[0]==sizeof(RoomM005OrbiterState)&&g_RoomM005Effects.table.sizes[1]==4&&g_RoomM005Effects.table.sizes[2]==sizeof(RoomM005Beacon)&&g_RoomM005Effects.script==g_RoomM005EffectScript&&g_RoomM005Effects.initialize==RoomLib_SetTaskVar0;}
int PeStateTasks_HeaderSupported(unsigned index,unsigned status,unsigned command){return index<11&&status>=1&&status<=5&&(command==115||command==117)&&task_source(command);}
/* PM12 fixed prefix ends before the three original room script arguments.
 * This is the exact existing checkpoint.c PM_BYTES expression, not a pointer. */
#define TASK_PM_PREFIX_BYTES (24u+22u*20u+304u+104u*4u)
#include "pe_state_bank_sources.h"
static int task_joint_header(const void*pointer,unsigned command,int32_t attached){unsigned source;uint32_t offset;size_t available;uint64_t epoch;unsigned needed=command==117?21u:command==115?(attached?1u:26u):0u;if(!needed||!PeStateBankSource_Locate(pointer,sizeof(RenderObjectHeader),&source,&offset,&available,&epoch)||!epoch)return 0;const RenderObjectHeader*h=pointer;return h->part_count>=needed;}
static int task_world(const PeWorldState*w,unsigned id,int apply,unsigned command,int32_t attached){if(!id||id>14)return 0;unsigned n=id-1;if(w&&w->targetMode){uint32_t types[14];for(unsigned j=0;j<14;j++)types[j]=w->actor[j].type;if(w->targetMode!=1||!PeWorld_ValidateTargetConstruction(w,types,w->liveMask)||!(w->liveMask&(1u<<n)))return 0;if(!apply)return task_joint_header(w->actor[n].header,command,attached);if(g_EntityPool[n].type_id!=w->actor[n].type||g_EntityPool[n].sub_id!=w->actor[n].sub||g_EntityPool[n].model_state.model_header!=w->actor[n].model||g_EntityPool[n].render_object.header!=w->actor[n].header)return 0;}
 unsigned visited=0;uint32_t mask=0;for(FieldActor*p=g_FieldActorListHead;p;p=p->next){unsigned j=task_actor(p);if(!j||j>14||visited++>=14||(mask&(1u<<(j-1))))return 0;mask|=1u<<(j-1);}return(mask&(1u<<n))&&g_EntityPool[n].render_object.header&&g_EntityPool[n].render_object.matrices&&task_joint_header(g_EntityPool[n].render_object.header,command,attached);
}
int PeStateTasks_CurrentSupported(unsigned index){if(index>=11||!g_PmSlotTable)return 0;PmSlotHeader*h=&g_PmSlotTable[index].header;if(h->field02!=1||h->field03!=0||!PeStateTasks_HeaderSupported(index,h->state,h->command)||!task_world(0,task_actor(h->owner),1,h->command,g_RoomScriptArgs[2]))return 0;unsigned char data[PE_STATE_TASK_CONTEXT_BYTES];return PeStateTaskContext_Capture(&g_PmSlotTable[index].task.tasks,data,sizeof data);}
static unsigned task_role(const void*p,int kind){if(!p)return 0;for(unsigned j=0;j<11;j++){FieldFxTaskOwner*o=&g_PmSlotTable[j].task;if(kind==0&&p==&o->prefix)return j+1;if(kind==1&&p==&o->tasks)return j+1;if(kind==2)for(unsigned k=0;k<8;k++)if(p==&o->tasks.slots[k])return 1+j*8+k;}return UINT32_MAX;}
int PeStateTasks_Constructed(const PeWorldState*w,const unsigned char*h,const unsigned char*d){if(!h||!d||!g_PmSlotTable)return 0;for(unsigned j=0;j<11;j++)if(rd(h+24+j*20)&&(rd(h+28+j*20)==115||rd(h+28+j*20)==117)){if(!PeStateTasks_HeaderSupported(j,rd(h+24+j*20),rd(h+28+j*20))||!task_world(w,rd(d+256+j*TASK_RECORD),1,rd(h+28+j*20),(int32_t)rd(h+TASK_PM_PREFIX_BYTES+8)))return 0;}return 1;}
int PeStateTasks_WalkForWorld(const PeWorldState*w,const unsigned char*header,const unsigned char*theater,const unsigned char*in,unsigned char*out,int apply){if((!header||!in)&&!out)return 0;if(apply&&(out||!PeStateTasks_WalkForWorld(w,header,theater,in,0,0)||!PeStateTasks_Constructed(w,header,in)))return 0;if(!g_PmSlotTable||!g_PmSlotTable2)return 0;if(apply&&(!PeStateBoundary_AtCpuPoint()||!Psyz_StateKernelOwned()||!Psyz_StateAudioOwned()))return 0;if(out){memset(out,0,TASK_TOTAL);memcpy(out,"PETASK01",8);}else if(memcmp(in,"PETASK01",8))return 0;
 unsigned any117=0;for(unsigned j=0;j<11;j++){uint32_t status=out?g_PmSlotTable[j].header.state:rd(header+24+j*20),command=out?g_PmSlotTable[j].header.command:rd(header+28+j*20);if(status&&(command==115||command==117))any117=1;}
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
 if(!out)for(size_t j=shared;j<256;j++)if(in[j])return 0;
 for(unsigned j=0;j<11;j++){PmSlotHeader*h=&g_PmSlotTable[j].header;uint32_t status=out?h->state:rd(header+24+j*20),command=out?h->command:rd(header+28+j*20);size_t at=256+j*TASK_RECORD;int active=status&&(command==115||command==117);if(!active){if(!out)for(size_t z=0;z<TASK_RECORD;z++)if(in[at+z])return 0;continue;}if(!PeStateTasks_HeaderSupported(j,status,command)||(out?h->field02:rd(header+32+j*20))!=1||(out?h->field03:rd(header+36+j*20))!=0)return 0;uint32_t actor=out?task_actor(h->owner):rd(in+at);if(!out&&rd(in+at+16+28)!=command)return 0;if(!task_world(w,actor,apply,command,out?g_RoomScriptArgs[2]:(int32_t)rd(header+TASK_PM_PREFIX_BYTES+8)))return 0;if(out){wr(out+at,actor);if(!PeStateTaskContext_Capture(&g_PmSlotTable[j].task.tasks,out+at+16,PE_STATE_TASK_CONTEXT_BYTES)||rd(out+at+16+28)!=command)return 0;}else{int modelRequired=PeStateTaskContext_ModelRequired(in+at+16,PE_STATE_TASK_CONTEXT_BYTES);if(modelRequired<0||(modelRequired&&(!theater||rd(theater+612)!=1||rd(theater+616)!=1)))return 0;if(rd(in+at+4)||rd(in+at+8)||rd(in+at+12)||!PeStateTaskContext_Validate(in+at+16,PE_STATE_TASK_CONTEXT_BYTES))return 0;if(apply){h->owner=&g_EntityPool[actor-1];if(!PeStateTaskContext_Restore(&g_PmSlotTable[j].task.tasks,in+at+16,PE_STATE_TASK_CONTEXT_BYTES))return 0;}}}
 if(apply){g_FieldFxOwner=roles[0]?&g_PmSlotTable[roles[0]-1].task.prefix:0;g_FieldFxTaskContext=roles[1]?&g_PmSlotTable[roles[1]-1].task.tasks:0;g_FieldFxActiveTaskSlot=roles[2]?&g_PmSlotTable[(roles[2]-1)/8].task.tasks.slots[(roles[2]-1)%8]:0;g_FieldFxCurrentAge=(int32_t)roles[3];}return 1;
}







