#include "pe_state_world.h"
#include "pe_state_boundary.h"
#include "pe_state_assets.h"
#include "pe_state_node_types.h"
#include "pe_state_lifetime.h"
#include "pe_state_bank_sources.h"
#include "pe1/collision_database.h"
#include "pe1/entity_allocation.h"
#include "pe1/entity_frame_update.h"
#include "pe1/battle_runtime.h"
#include "pe1/battle_saved_state.h"
#include "pe1/battle_start.h"
#include "pe1/script_state.h"
#include "pe1/script_queue.h"
#include "pe1/player_entity.h"
#include "pe1/game_state.h"
#include "pe1/scene_dispatch.h"
#include <string.h>
#include <limits.h>
#define ACTOR_VALUES 123u
typedef struct Ref {uint32_t region,offset;} Ref;
typedef struct Cursor {const unsigned char *in;unsigned char *out;size_t size,at;int ok;} Cursor;
static void put(Cursor *c,uint32_t v){if(c->at>c->size||c->size-c->at<4){c->ok=0;return;}for(unsigned i=0;i<4;i++)c->out[c->at++]=(unsigned char)(v>>(8*i));}
static uint32_t get(Cursor *c){uint32_t v=0;if(c->at>c->size||c->size-c->at<4){c->ok=0;return 0;}for(unsigned i=0;i<4;i++)v|=(uint32_t)c->in[c->at++]<<(8*i);return v;}
static void put64(Cursor *c,uint64_t v){put(c,(uint32_t)v);put(c,(uint32_t)(v>>32));}
static uint64_t get64(Cursor *c){uint64_t lo=get(c);uint64_t hi=get(c);return lo|(hi<<32);}
static uint32_t checksum(const unsigned char *p,size_t n){uint32_t v=~0u;for(size_t i=0;i<n;i++){v^=p[i];for(unsigned j=0;j<8;j++)v=(v>>1)^(0xedb88320u&-(int32_t)(v&1));}return ~v;}
static uint32_t actor_id(const FieldActor *p){if(!p)return 0;uintptr_t n=(uintptr_t)p,b=(uintptr_t)g_EntityPool;if(n<b||n-b>=sizeof g_EntityPool||(n-b)%sizeof(FieldActor))return UINT32_MAX;return (uint32_t)((n-b)/sizeof(FieldActor))+1;}
static uint32_t node_id(const ScriptNode *p){if(!p)return 0;uintptr_t n=(uintptr_t)p,b=(uintptr_t)g_ScriptNodes;if(n<b||n-b>=sizeof g_ScriptNodes||(n-b)%sizeof(ScriptNode))return UINT32_MAX;return (uint32_t)((n-b)/sizeof(ScriptNode))+1;}
static FieldActor *actor_ptr(uint32_t id){return id?&g_EntityPool[id-1]:0;}
static ScriptNode *node_ptr(uint32_t id){return id?&g_ScriptNodes[id-1]:0;}
static int live_mask(uint32_t *mask){uint32_t m=0;FieldActor *a=g_FieldActorListHead;unsigned count=0;while(a){uint32_t id=actor_id(a);if(!id||id==UINT32_MAX||count++>=14||(m&(1u<<(id-1))))return 0;m|=1u<<(id-1);a=a->next;}*mask=m;return 1;}
void PeWorld_Init(PeWorldState *state){if(state)memset(state,0,sizeof *state);}
int PeWorld_RegisterScriptRegion(PeWorldState *s,uint32_t id,const unsigned char *base,size_t bytes){
    if(!PeStateBoundary_AtCpuPoint()||!s||!id||!base||!bytes||bytes>PE_STATE_MAX_BYTES||s->regionCount>=PE_WORLD_REGIONS||s->regionBytes>PE_STATE_MAX_BYTES-bytes||(uintptr_t)base>UINTPTR_MAX-bytes)return PE_STATE_INVALID;
    for(unsigned i=0;i<s->regionCount;i++){const PeWorldRegion *r=&s->regions[i];uintptr_t a=(uintptr_t)base,b=(uintptr_t)r->base;if(r->id==id||(a<b+r->bytes&&b<a+bytes))return PE_STATE_INVALID;}
    s->regions[s->regionCount++]=(PeWorldRegion){id,(uint32_t)bytes,checksum(base,bytes),base};s->regionBytes+=bytes;return 0;
}
static int source_role(const void*p,size_t bytes,PeWorldSourceRole*out){unsigned id;uint32_t offset;size_t available;uint64_t epoch;if(!PeStateBankSource_Locate(p,bytes,&id,&offset,&available,&epoch))return 0;*out=(PeWorldSourceRole){id,offset,epoch};return 1;}
static int same_source(const void*p,size_t bytes,const PeWorldSourceRole*expected){PeWorldSourceRole now;return source_role(p,bytes,&now)&&now.source==expected->source&&now.offset==expected->offset&&now.epoch==expected->epoch;}
static int retained_bindings_known(const PeWorldState*);
/* Only source opcodes Set/ClearObjEntryFlag80 mutate triangle kind bit80.
 * Other bytes remain source identity; blocked bits are explicitly serialized. */
static uint32_t collision_crc(const PeWorldState*s){uint32_t crc=~0u;const unsigned char*p=s->collisionBase;size_t bytes=(size_t)s->collisionCount*s->collisionStride;for(size_t i=0;i<bytes;i++){unsigned v=p[i];if(i%s->collisionStride==0)v&=127;crc^=v;for(unsigned j=0;j<8;j++)crc=(crc>>1)^(0xedb88320u&-(int32_t)(crc&1));}return ~crc;}
static int collision_layout(uint32_t*out){if(!out||(g_CollisionDb?g_CollisionDb->planes.pointer:0)!=g_CollisionPlaneTable)return 0;*out=g_CollisionDb?(g_CollisionPlaneTable?sizeof(CollisionTriangleXYZ):sizeof(CollisionTriangleXZ)):0;return 1;}
static int snapshot_sources(PeWorldState*s){uint32_t stride;if(!collision_layout(&stride))return 0;s->storyDay=g_CurrentStoryDay[0];for(unsigned i=0;i<10;i++)for(unsigned j=0;j<48;j++)if(g_GameState.bank_rows[i][j]&&!source_role(g_GameState.bank_rows[i][j],1,&s->rowSources[i][j]))return 0;for(unsigned i=0;i<10;i++)if(g_GameState.bank_slots[i]&&!source_role(g_GameState.bank_slots[i],sizeof(RenderObjectHeader),&s->bankSources[i]))return 0;s->collisionBase=g_CollisionDb?g_CollisionDb->triangles.pointer:0;s->collisionCount=g_CollisionDb?g_CollisionDb->triangleCount:0;s->collisionStride=stride;if(s->collisionCount&&(!s->collisionBase||!source_role(s->collisionBase,(size_t)s->collisionCount*s->collisionStride,&s->collisionSource)))return 0;s->collisionCrc=collision_crc(s);return 1;}
/* Immutable source compatibility is independent from actor allocation/cohort. */
static int source_bound(const PeWorldState*s){uint32_t stride;if(!collision_layout(&stride))return 0;if(!s||!s->binding||!PeStateBoundary_AtCpuPoint()||s->geometryOwner!=g_GameState.geometry_state||s->assetGeneration!=PeStateAssets_Generation()||s->session!=PeStateBoundary_Session()||s->scene!=PeStateBoundary_SceneEpoch()||s->storyDay!=g_CurrentStoryDay[0])return 0;for(unsigned i=0;i<10;i++){if(g_GameState.bank_slots[i]){if(!same_source(g_GameState.bank_slots[i],sizeof(RenderObjectHeader),&s->bankSources[i]))return 0;}else if(s->bankSources[i].source)return 0;for(unsigned j=0;j<48;j++)if(s->bankRows[i][j]!=g_GameState.bank_rows[i][j]||(s->bankRows[i][j]&&!same_source(s->bankRows[i][j],1,&s->rowSources[i][j])))return 0;}if(s->collisionBase!=(g_CollisionDb?g_CollisionDb->triangles.pointer:0)||s->collisionCount!=(g_CollisionDb?g_CollisionDb->triangleCount:0)||s->collisionStride!=stride)return 0;if(s->collisionCount&&!same_source(s->collisionBase,(size_t)s->collisionCount*s->collisionStride,&s->collisionSource))return 0;if(collision_crc(s)!=s->collisionCrc)return 0;for(unsigned i=0;i<s->regionCount;i++)if(checksum(s->regions[i].base,s->regions[i].bytes)!=s->regions[i].crc)return 0;return retained_bindings_known(s);}
int PeWorld_BindCurrentScene(PeWorldState *s){
    static uint64_t bindings;
    if(!PeStateBoundary_AtCpuPoint()||!s||!PeStateBoundary_Session()||!PeStateBoundary_SceneEpoch()||!live_mask(&s->liveMask))return PE_STATE_INCOMPLETE;
    s->session=PeStateBoundary_Session();s->scene=PeStateBoundary_SceneEpoch();s->binding=++bindings;if(!s->binding)s->binding=++bindings;
    s->assetGeneration=PeStateAssets_Generation();s->resourceEpoch=PeStateResource_Epoch();
    memcpy(s->bankRows,g_GameState.bank_rows,sizeof s->bankRows);s->geometryOwner=g_GameState.geometry_state;if(!snapshot_sources(s))return PE_STATE_INCOMPLETE;
    s->constructorPlayerId=actor_id(g_PlayerEntity);
    s->serialCounter=g_EntitySerialCounter;s->liveCount=g_EntityFreePoolCount;
    for(unsigned i=0;i<14;i++){const FieldActor *a=&g_EntityPool[i];s->actor[i]=(PeWorldBinding){a->state,a->model_state.model_header,a->render_object.header,a->action_data,a->script_base,a->frame_callback,a->contact_callback,a->field_sfx_id};s->actor[i].type=a->type_id;s->actor[i].sub=a->sub_id;if((s->liveMask&(1u<<i))&&!PeWorld_NativeActorRoles(a,&s->actor[i].coreRole,&s->actor[i].frameRole,&s->actor[i].contactRole))return PE_STATE_INCOMPLETE;}
    return 0;
}
static int bound(const PeWorldState *s){uint32_t mask;if(!source_bound(s)||!s||!s->binding||s->geometryOwner!=g_GameState.geometry_state||s->resourceEpoch!=PeStateResource_Epoch()||s->assetGeneration!=PeStateAssets_Generation()||s->session!=PeStateBoundary_Session()||s->scene!=PeStateBoundary_SceneEpoch()||s->serialCounter!=g_EntitySerialCounter||s->liveCount!=g_EntityFreePoolCount||!live_mask(&mask)||mask!=s->liveMask)return 0;
    for(unsigned i=0;i<14;i++)if(mask&(1u<<i)){const FieldActor *a=&g_EntityPool[i];const PeWorldBinding *b=&s->actor[i];if(b->type!=a->type_id||b->sub!=a->sub_id||b->state!=a->state||b->model!=a->model_state.model_header||b->header!=a->render_object.header||b->script!=a->script_base||b->frame!=a->frame_callback||b->contact!=a->contact_callback||b->serial!=a->field_sfx_id)return 0;}
    for(unsigned i=0;i<10;i++)for(unsigned j=0;j<48;j++)if(s->bankRows[i][j]!=g_GameState.bank_rows[i][j])return 0;
    for(unsigned i=0;i<s->regionCount;i++)if(checksum(s->regions[i].base,s->regions[i].bytes)!=s->regions[i].crc)return 0;return 1;
}
int PeWorld_IsCurrent(const PeWorldState*s){return bound(s);}
static int constructed_bound(const PeWorldState *s){uint32_t mask;if(!source_bound(s)||!s||!s->binding||s->geometryOwner!=g_GameState.geometry_state||s->assetGeneration!=PeStateAssets_Generation()||s->session!=PeStateBoundary_Session()||s->scene!=PeStateBoundary_SceneEpoch()||s->serialCounter!=g_EntitySerialCounter||s->liveCount!=g_EntityFreePoolCount||!live_mask(&mask)||mask!=s->liveMask)return 0;
    for(unsigned i=0;i<14;i++)if(mask&(1u<<i)){const FieldActor *a=&g_EntityPool[i];const PeWorldBinding *b=&s->actor[i];if(b->type!=a->type_id||b->sub!=a->sub_id||b->state!=a->state||b->model!=a->model_state.model_header||b->header!=a->render_object.header||b->script!=a->script_base||b->frame!=a->frame_callback||b->contact!=a->contact_callback||b->serial!=a->field_sfx_id)return 0;}
    for(unsigned i=0;i<10;i++)for(unsigned j=0;j<48;j++)if(s->bankRows[i][j]!=g_GameState.bank_rows[i][j])return 0;
    for(unsigned i=0;i<s->regionCount;i++)if(checksum(s->regions[i].base,s->regions[i].bytes)!=s->regions[i].crc)return 0;return 1;
}
static int encode_ref(const PeWorldState *s,const void *pointer,Ref *ref){ref->region=ref->offset=0;if(!pointer)return 1;uintptr_t value=(uintptr_t)pointer;for(unsigned i=0;i<s->regionCount;i++){const PeWorldRegion *r=&s->regions[i];uintptr_t base=(uintptr_t)r->base;if(value>=base&&value-base<r->bytes&&((value-base)&1)==0){*ref=(Ref){r->id,(uint32_t)(value-base)};return 1;}}return 0;}
static int valid_ref(const PeWorldState *s,Ref ref){if(!ref.region)return ref.offset==0;for(unsigned i=0;i<s->regionCount;i++)if(s->regions[i].id==ref.region)return ref.offset<s->regions[i].bytes&&!(ref.offset&1);return 0;}
static void *decode_ref(const PeWorldState *s,Ref ref){if(!ref.region)return 0;for(unsigned i=0;i<s->regionCount;i++)if(s->regions[i].id==ref.region)return (void *)(s->regions[i].base+ref.offset);return 0;}
static void putref(Cursor *c,PeWorldState *s,const void *p){Ref ref;if(!encode_ref(s,p,&ref)){s->lastError=PE_WORLD_UNMAPPED_SCRIPT;c->ok=0;return;}put(c,ref.region);put(c,ref.offset);}
static Ref getref(Cursor *c,const PeWorldState *s){Ref ref;ref.region=get(c);ref.offset=get(c);if(!valid_ref(s,ref))c->ok=0;return ref;}
static int canonical(uint32_t v,unsigned kind){if(kind==1)return v<=255;if(kind==2)return v<=65535;if(kind==3)return (int32_t)v>=INT16_MIN&&(int32_t)v<=INT16_MAX;return 1;}
static void actor_write(Cursor *c,const FieldActor *a){
    put(c,(uint32_t)a->type_id);
    put(c,(uint32_t)a->sub_id);
    put(c,(uint32_t)a->mode);
    put(c,(uint32_t)a->action);
    put(c,(uint32_t)a->pad_010);
    put(c,(uint32_t)a->anim_frame_target);
    put(c,(uint32_t)a->anim.fixed);
    put(c,(uint32_t)a->anim_prev);
    put(c,(uint32_t)a->anim_step);
    put(c,(uint32_t)a->move_factor);
    put(c,(uint32_t)a->field_sfx_id);
    put(c,(uint32_t)a->move_speed);
    put(c,(uint32_t)a->pos_x);
    put(c,(uint32_t)a->pos_y);
    put(c,(uint32_t)a->pos_z);
    put(c,(uint32_t)a->rot_x);
    put(c,(uint32_t)a->rot_y);
    put(c,(uint32_t)a->rot_z);
    put(c,(uint32_t)a->base_x);
    put(c,(uint32_t)a->base_y);
    put(c,(uint32_t)a->base_z);
    put(c,(uint32_t)a->field_4c);
    put(c,(uint32_t)a->saved_rot_x);
    put(c,(uint32_t)a->saved_rot_y);
    put(c,(uint32_t)a->saved_rot_z);
    put(c,(uint32_t)a->delta_x);
    put(c,(uint32_t)a->delta_y);
    put(c,(uint32_t)a->delta_z);
    put(c,(uint32_t)a->motion_x);
    put(c,(uint32_t)a->motion_y);
    put(c,(uint32_t)a->motion_z);
    put(c,(uint32_t)a->accel_x);
    put(c,(uint32_t)a->accel_y);
    put(c,(uint32_t)a->accel_z);
    put(c,(uint32_t)a->gravity_x);
    put(c,(uint32_t)a->gravity_y);
    put(c,(uint32_t)a->gravity_z);
    put(c,(uint32_t)a->flags);
    put(c,(uint32_t)a->pad_198);
    put(c,(uint32_t)a->pad_034[0]);
    put(c,(uint32_t)a->pad_034[1]);
    put(c,(uint32_t)a->pad_034[2]);
    put(c,(uint32_t)a->pad_034[3]);
    put(c,(uint32_t)a->pad_03E[0]);
    put(c,(uint32_t)a->pad_03E[1]);
    put(c,(uint32_t)a->pad_056[0]);
    put(c,(uint32_t)a->pad_056[1]);
    put(c,(uint32_t)a->pad_064[0]);
    put(c,(uint32_t)a->pad_064[1]);
    put(c,(uint32_t)a->pad_064[2]);
    put(c,(uint32_t)a->pad_064[3]);
    put(c,(uint32_t)a->pad_074[0]);
    put(c,(uint32_t)a->pad_074[1]);
    put(c,(uint32_t)a->pad_074[2]);
    put(c,(uint32_t)a->pad_074[3]);
    put(c,(uint32_t)a->pad_084[0]);
    put(c,(uint32_t)a->pad_084[1]);
    put(c,(uint32_t)a->pad_084[2]);
    put(c,(uint32_t)a->pad_084[3]);
    put(c,(uint32_t)a->pad_094[0]);
    put(c,(uint32_t)a->pad_094[1]);
    put(c,(uint32_t)a->pad_094[2]);
    put(c,(uint32_t)a->pad_094[3]);
    put(c,(uint32_t)a->pad_0AC[0]);
    put(c,(uint32_t)a->pad_0AC[1]);
    put(c,(uint32_t)a->pad_0AC[2]);
    put(c,(uint32_t)a->pad_0AC[3]);
    put(c,(uint32_t)a->pad_0AC[4]);
    put(c,(uint32_t)a->pad_0AC[5]);
    put(c,(uint32_t)a->pad_0AC[6]);
    put(c,(uint32_t)a->pad_0AC[7]);
    put(c,(uint32_t)a->pad_0AC[8]);
    put(c,(uint32_t)a->pad_0AC[9]);
    put(c,(uint32_t)a->pad_0AC[10]);
    put(c,(uint32_t)a->pad_0AC[11]);
    put(c,(uint32_t)a->pad_0AC[12]);
    put(c,(uint32_t)a->pad_0AC[13]);
    put(c,(uint32_t)a->pad_0AC[14]);
    put(c,(uint32_t)a->pad_0AC[15]);
    put(c,(uint32_t)a->pad_0AC[16]);
    put(c,(uint32_t)a->pad_0AC[17]);
    put(c,(uint32_t)a->pad_0AC[18]);
    put(c,(uint32_t)a->pad_0AC[19]);
    put(c,(uint32_t)a->pad_0AC[20]);
    put(c,(uint32_t)a->pad_0AC[21]);
    put(c,(uint32_t)a->pad_0AC[22]);
    put(c,(uint32_t)a->pad_0AC[23]);
    put(c,(uint32_t)a->pad_0AC[24]);
    put(c,(uint32_t)a->pad_0AC[25]);
    put(c,(uint32_t)a->pad_0AC[26]);
    put(c,(uint32_t)a->pad_0AC[27]);
    put(c,(uint32_t)a->pad_0AC[28]);
    put(c,(uint32_t)a->pad_0AC[29]);
    put(c,(uint32_t)a->pad_0AC[30]);
    put(c,(uint32_t)a->pad_0AC[31]);
    put(c,(uint32_t)a->pad_0AC[32]);
    put(c,(uint32_t)a->pad_0AC[33]);
    put(c,(uint32_t)a->pad_0AC[34]);
    put(c,(uint32_t)a->pad_0AC[35]);
    put(c,(uint32_t)a->pad_0AC[36]);
    put(c,(uint32_t)a->pad_0AC[37]);
    put(c,(uint32_t)a->pad_0AC[38]);
    put(c,(uint32_t)a->pad_0AC[39]);
    put(c,(uint32_t)a->pad_0AC[40]);
    put(c,(uint32_t)a->pad_0AC[41]);
    put(c,(uint32_t)a->pad_0AC[42]);
    put(c,(uint32_t)a->pad_0AC[43]);
    put(c,(uint32_t)a->pad_0AC[44]);
    put(c,(uint32_t)a->pad_0AC[45]);
    put(c,(uint32_t)a->pad_0AC[46]);
    put(c,(uint32_t)a->pad_0AC[47]);
    put(c,(uint32_t)a->pad_0AC[48]);
    put(c,(uint32_t)a->pad_0AC[49]);
    put(c,(uint32_t)a->pad_0AC[50]);
    put(c,(uint32_t)a->pad_0AC[51]);
    put(c,(uint32_t)a->pad_0AC[52]);
    put(c,(uint32_t)a->pad_0AC[53]);
    put(c,(uint32_t)a->pad_0AC[54]);
    put(c,(uint32_t)a->pad_0AC[55]);
    put(c,(uint32_t)a->mirror_line.x0);
    put(c,(uint32_t)a->mirror_line.z0);
    put(c,(uint32_t)a->mirror_line.x1);
    put(c,(uint32_t)a->mirror_line.z1);
}
static int actor_read(Cursor *c,uint32_t *values){
    values[0]=get(c);if(!canonical(values[0],1))c->ok=0;
    values[1]=get(c);if(!canonical(values[1],1))c->ok=0;
    values[2]=get(c);if(!canonical(values[2],1))c->ok=0;
    values[3]=get(c);if(!canonical(values[3],1))c->ok=0;
    values[4]=get(c);if(!canonical(values[4],2))c->ok=0;
    values[5]=get(c);if(!canonical(values[5],2))c->ok=0;
    values[6]=get(c);if(!canonical(values[6],0))c->ok=0;
    values[7]=get(c);if(!canonical(values[7],0))c->ok=0;
    values[8]=get(c);if(!canonical(values[8],0))c->ok=0;
    values[9]=get(c);if(!canonical(values[9],0))c->ok=0;
    values[10]=get(c);if(!canonical(values[10],2))c->ok=0;
    values[11]=get(c);if(!canonical(values[11],2))c->ok=0;
    values[12]=get(c);if(!canonical(values[12],0))c->ok=0;
    values[13]=get(c);if(!canonical(values[13],0))c->ok=0;
    values[14]=get(c);if(!canonical(values[14],0))c->ok=0;
    values[15]=get(c);if(!canonical(values[15],3))c->ok=0;
    values[16]=get(c);if(!canonical(values[16],3))c->ok=0;
    values[17]=get(c);if(!canonical(values[17],3))c->ok=0;
    values[18]=get(c);if(!canonical(values[18],0))c->ok=0;
    values[19]=get(c);if(!canonical(values[19],0))c->ok=0;
    values[20]=get(c);if(!canonical(values[20],0))c->ok=0;
    values[21]=get(c);if(!canonical(values[21],0))c->ok=0;
    values[22]=get(c);if(!canonical(values[22],3))c->ok=0;
    values[23]=get(c);if(!canonical(values[23],3))c->ok=0;
    values[24]=get(c);if(!canonical(values[24],3))c->ok=0;
    values[25]=get(c);if(!canonical(values[25],0))c->ok=0;
    values[26]=get(c);if(!canonical(values[26],0))c->ok=0;
    values[27]=get(c);if(!canonical(values[27],0))c->ok=0;
    values[28]=get(c);if(!canonical(values[28],0))c->ok=0;
    values[29]=get(c);if(!canonical(values[29],0))c->ok=0;
    values[30]=get(c);if(!canonical(values[30],0))c->ok=0;
    values[31]=get(c);if(!canonical(values[31],0))c->ok=0;
    values[32]=get(c);if(!canonical(values[32],0))c->ok=0;
    values[33]=get(c);if(!canonical(values[33],0))c->ok=0;
    values[34]=get(c);if(!canonical(values[34],0))c->ok=0;
    values[35]=get(c);if(!canonical(values[35],0))c->ok=0;
    values[36]=get(c);if(!canonical(values[36],0))c->ok=0;
    values[37]=get(c);if(!canonical(values[37],0))c->ok=0;
    values[38]=get(c);if(!canonical(values[38],0))c->ok=0;
    values[39]=get(c);if(!canonical(values[39],1))c->ok=0;
values[40]=get(c);if(!canonical(values[40],1))c->ok=0;
values[41]=get(c);if(!canonical(values[41],1))c->ok=0;
values[42]=get(c);if(!canonical(values[42],1))c->ok=0;
values[43]=get(c);if(!canonical(values[43],1))c->ok=0;
values[44]=get(c);if(!canonical(values[44],1))c->ok=0;
values[45]=get(c);if(!canonical(values[45],1))c->ok=0;
values[46]=get(c);if(!canonical(values[46],1))c->ok=0;
values[47]=get(c);if(!canonical(values[47],1))c->ok=0;
values[48]=get(c);if(!canonical(values[48],1))c->ok=0;
values[49]=get(c);if(!canonical(values[49],1))c->ok=0;
values[50]=get(c);if(!canonical(values[50],1))c->ok=0;
values[51]=get(c);if(!canonical(values[51],1))c->ok=0;
values[52]=get(c);if(!canonical(values[52],1))c->ok=0;
values[53]=get(c);if(!canonical(values[53],1))c->ok=0;
values[54]=get(c);if(!canonical(values[54],1))c->ok=0;
values[55]=get(c);if(!canonical(values[55],1))c->ok=0;
values[56]=get(c);if(!canonical(values[56],1))c->ok=0;
values[57]=get(c);if(!canonical(values[57],1))c->ok=0;
values[58]=get(c);if(!canonical(values[58],1))c->ok=0;
values[59]=get(c);if(!canonical(values[59],1))c->ok=0;
values[60]=get(c);if(!canonical(values[60],1))c->ok=0;
values[61]=get(c);if(!canonical(values[61],1))c->ok=0;
values[62]=get(c);if(!canonical(values[62],1))c->ok=0;
values[63]=get(c);if(!canonical(values[63],0))c->ok=0;
values[64]=get(c);if(!canonical(values[64],0))c->ok=0;
values[65]=get(c);if(!canonical(values[65],0))c->ok=0;
values[66]=get(c);if(!canonical(values[66],0))c->ok=0;
values[67]=get(c);if(!canonical(values[67],0))c->ok=0;
values[68]=get(c);if(!canonical(values[68],0))c->ok=0;
values[69]=get(c);if(!canonical(values[69],0))c->ok=0;
values[70]=get(c);if(!canonical(values[70],0))c->ok=0;
values[71]=get(c);if(!canonical(values[71],0))c->ok=0;
values[72]=get(c);if(!canonical(values[72],0))c->ok=0;
values[73]=get(c);if(!canonical(values[73],0))c->ok=0;
values[74]=get(c);if(!canonical(values[74],0))c->ok=0;
values[75]=get(c);if(!canonical(values[75],0))c->ok=0;
values[76]=get(c);if(!canonical(values[76],0))c->ok=0;
values[77]=get(c);if(!canonical(values[77],0))c->ok=0;
values[78]=get(c);if(!canonical(values[78],0))c->ok=0;
values[79]=get(c);if(!canonical(values[79],0))c->ok=0;
values[80]=get(c);if(!canonical(values[80],0))c->ok=0;
values[81]=get(c);if(!canonical(values[81],0))c->ok=0;
values[82]=get(c);if(!canonical(values[82],0))c->ok=0;
values[83]=get(c);if(!canonical(values[83],0))c->ok=0;
values[84]=get(c);if(!canonical(values[84],0))c->ok=0;
values[85]=get(c);if(!canonical(values[85],0))c->ok=0;
values[86]=get(c);if(!canonical(values[86],0))c->ok=0;
values[87]=get(c);if(!canonical(values[87],0))c->ok=0;
values[88]=get(c);if(!canonical(values[88],0))c->ok=0;
values[89]=get(c);if(!canonical(values[89],0))c->ok=0;
values[90]=get(c);if(!canonical(values[90],0))c->ok=0;
values[91]=get(c);if(!canonical(values[91],0))c->ok=0;
values[92]=get(c);if(!canonical(values[92],0))c->ok=0;
values[93]=get(c);if(!canonical(values[93],0))c->ok=0;
values[94]=get(c);if(!canonical(values[94],0))c->ok=0;
values[95]=get(c);if(!canonical(values[95],0))c->ok=0;
values[96]=get(c);if(!canonical(values[96],0))c->ok=0;
values[97]=get(c);if(!canonical(values[97],0))c->ok=0;
values[98]=get(c);if(!canonical(values[98],0))c->ok=0;
values[99]=get(c);if(!canonical(values[99],0))c->ok=0;
values[100]=get(c);if(!canonical(values[100],0))c->ok=0;
values[101]=get(c);if(!canonical(values[101],0))c->ok=0;
values[102]=get(c);if(!canonical(values[102],0))c->ok=0;
values[103]=get(c);if(!canonical(values[103],0))c->ok=0;
values[104]=get(c);if(!canonical(values[104],0))c->ok=0;
values[105]=get(c);if(!canonical(values[105],0))c->ok=0;
values[106]=get(c);if(!canonical(values[106],0))c->ok=0;
values[107]=get(c);if(!canonical(values[107],0))c->ok=0;
values[108]=get(c);if(!canonical(values[108],0))c->ok=0;
values[109]=get(c);if(!canonical(values[109],0))c->ok=0;
values[110]=get(c);if(!canonical(values[110],0))c->ok=0;
values[111]=get(c);if(!canonical(values[111],0))c->ok=0;
values[112]=get(c);if(!canonical(values[112],0))c->ok=0;
values[113]=get(c);if(!canonical(values[113],0))c->ok=0;
values[114]=get(c);if(!canonical(values[114],0))c->ok=0;
values[115]=get(c);if(!canonical(values[115],0))c->ok=0;
values[116]=get(c);if(!canonical(values[116],0))c->ok=0;
values[117]=get(c);if(!canonical(values[117],0))c->ok=0;
values[118]=get(c);if(!canonical(values[118],0))c->ok=0;
values[119]=get(c);if(!canonical(values[119],2))c->ok=0;
values[120]=get(c);if(!canonical(values[120],2))c->ok=0;
values[121]=get(c);if(!canonical(values[121],2))c->ok=0;
values[122]=get(c);if(!canonical(values[122],2))c->ok=0;
    return c->ok;
}
static void actor_restore(FieldActor *a,const uint32_t *values){
    a->type_id=(unsigned char)values[0];
    a->sub_id=(unsigned char)values[1];
    a->mode=(unsigned char)values[2];
    a->action=(unsigned char)values[3];
    a->pad_010=(unsigned short)values[4];
    a->anim_frame_target=(unsigned short)values[5];
    a->anim.fixed=(int)values[6];
    a->anim_prev=(int)values[7];
    a->anim_step=(int)values[8];
    a->move_factor=(int)values[9];
    a->field_sfx_id=(unsigned short)values[10];
    a->move_speed=(unsigned short)values[11];
    a->pos_x=(int)values[12];
    a->pos_y=(int)values[13];
    a->pos_z=(int)values[14];
    a->rot_x=(short)values[15];
    a->rot_y=(short)values[16];
    a->rot_z=(short)values[17];
    a->base_x=(int)values[18];
    a->base_y=(int)values[19];
    a->base_z=(int)values[20];
    a->field_4c=(int)values[21];
    a->saved_rot_x=(short)values[22];
    a->saved_rot_y=(short)values[23];
    a->saved_rot_z=(short)values[24];
    a->delta_x=(int)values[25];
    a->delta_y=(int)values[26];
    a->delta_z=(int)values[27];
    a->motion_x=(int)values[28];
    a->motion_y=(int)values[29];
    a->motion_z=(int)values[30];
    a->accel_x=(int)values[31];
    a->accel_y=(int)values[32];
    a->accel_z=(int)values[33];
    a->gravity_x=(int)values[34];
    a->gravity_y=(int)values[35];
    a->gravity_z=(int)values[36];
    a->flags=(unsigned int)values[37];
    a->pad_198=(unsigned int)values[38];
    a->pad_034[0]=(unsigned char)values[39];
    a->pad_034[1]=(unsigned char)values[40];
    a->pad_034[2]=(unsigned char)values[41];
    a->pad_034[3]=(unsigned char)values[42];
    a->pad_03E[0]=(unsigned char)values[43];
    a->pad_03E[1]=(unsigned char)values[44];
    a->pad_056[0]=(unsigned char)values[45];
    a->pad_056[1]=(unsigned char)values[46];
    a->pad_064[0]=(unsigned char)values[47];
    a->pad_064[1]=(unsigned char)values[48];
    a->pad_064[2]=(unsigned char)values[49];
    a->pad_064[3]=(unsigned char)values[50];
    a->pad_074[0]=(unsigned char)values[51];
    a->pad_074[1]=(unsigned char)values[52];
    a->pad_074[2]=(unsigned char)values[53];
    a->pad_074[3]=(unsigned char)values[54];
    a->pad_084[0]=(unsigned char)values[55];
    a->pad_084[1]=(unsigned char)values[56];
    a->pad_084[2]=(unsigned char)values[57];
    a->pad_084[3]=(unsigned char)values[58];
    a->pad_094[0]=(unsigned char)values[59];
    a->pad_094[1]=(unsigned char)values[60];
    a->pad_094[2]=(unsigned char)values[61];
    a->pad_094[3]=(unsigned char)values[62];
    a->pad_0AC[0]=(unsigned int)values[63];
    a->pad_0AC[1]=(unsigned int)values[64];
    a->pad_0AC[2]=(unsigned int)values[65];
    a->pad_0AC[3]=(unsigned int)values[66];
    a->pad_0AC[4]=(unsigned int)values[67];
    a->pad_0AC[5]=(unsigned int)values[68];
    a->pad_0AC[6]=(unsigned int)values[69];
    a->pad_0AC[7]=(unsigned int)values[70];
    a->pad_0AC[8]=(unsigned int)values[71];
    a->pad_0AC[9]=(unsigned int)values[72];
    a->pad_0AC[10]=(unsigned int)values[73];
    a->pad_0AC[11]=(unsigned int)values[74];
    a->pad_0AC[12]=(unsigned int)values[75];
    a->pad_0AC[13]=(unsigned int)values[76];
    a->pad_0AC[14]=(unsigned int)values[77];
    a->pad_0AC[15]=(unsigned int)values[78];
    a->pad_0AC[16]=(unsigned int)values[79];
    a->pad_0AC[17]=(unsigned int)values[80];
    a->pad_0AC[18]=(unsigned int)values[81];
    a->pad_0AC[19]=(unsigned int)values[82];
    a->pad_0AC[20]=(unsigned int)values[83];
    a->pad_0AC[21]=(unsigned int)values[84];
    a->pad_0AC[22]=(unsigned int)values[85];
    a->pad_0AC[23]=(unsigned int)values[86];
    a->pad_0AC[24]=(unsigned int)values[87];
    a->pad_0AC[25]=(unsigned int)values[88];
    a->pad_0AC[26]=(unsigned int)values[89];
    a->pad_0AC[27]=(unsigned int)values[90];
    a->pad_0AC[28]=(unsigned int)values[91];
    a->pad_0AC[29]=(unsigned int)values[92];
    a->pad_0AC[30]=(unsigned int)values[93];
    a->pad_0AC[31]=(unsigned int)values[94];
    a->pad_0AC[32]=(unsigned int)values[95];
    a->pad_0AC[33]=(unsigned int)values[96];
    a->pad_0AC[34]=(unsigned int)values[97];
    a->pad_0AC[35]=(unsigned int)values[98];
    a->pad_0AC[36]=(unsigned int)values[99];
    a->pad_0AC[37]=(unsigned int)values[100];
    a->pad_0AC[38]=(unsigned int)values[101];
    a->pad_0AC[39]=(unsigned int)values[102];
    a->pad_0AC[40]=(unsigned int)values[103];
    a->pad_0AC[41]=(unsigned int)values[104];
    a->pad_0AC[42]=(unsigned int)values[105];
    a->pad_0AC[43]=(unsigned int)values[106];
    a->pad_0AC[44]=(unsigned int)values[107];
    a->pad_0AC[45]=(unsigned int)values[108];
    a->pad_0AC[46]=(unsigned int)values[109];
    a->pad_0AC[47]=(unsigned int)values[110];
    a->pad_0AC[48]=(unsigned int)values[111];
    a->pad_0AC[49]=(unsigned int)values[112];
    a->pad_0AC[50]=(unsigned int)values[113];
    a->pad_0AC[51]=(unsigned int)values[114];
    a->pad_0AC[52]=(unsigned int)values[115];
    a->pad_0AC[53]=(unsigned int)values[116];
    a->pad_0AC[54]=(unsigned int)values[117];
    a->pad_0AC[55]=(unsigned int)values[118];
    a->mirror_line.x0=(unsigned short)values[119];
    a->mirror_line.z0=(unsigned short)values[120];
    a->mirror_line.x1=(unsigned short)values[121];
    a->mirror_line.z1=(unsigned short)values[122];
}

/* Original collision producers select only the canonical triangle table. */
static uint32_t face_ref(PeWorldState*s,const void*p){if(!p)return 0;if(!s->collisionBase||!s->collisionStride)return UINT32_MAX;uintptr_t value=(uintptr_t)p,base=(uintptr_t)s->collisionBase;if(value<base||value-base>=(size_t)s->collisionCount*s->collisionStride||(value-base)%s->collisionStride)return UINT32_MAX;return(uint32_t)((value-base)/s->collisionStride)+1;}
static void*face_ptr(PeWorldState*s,uint32_t id){return id?(void*)((const unsigned char*)s->collisionBase+(size_t)(id-1)*s->collisionStride):0;}
static uint32_t action_ref(const FieldActor *a){if(!a->action_data)return 0;if(a->type_id>=10)return UINT32_MAX;for(unsigned j=0;j<48;j++)if(g_GameState.bank_rows[a->type_id][j]==a->action_data)return j+1;return UINT32_MAX;}
typedef struct ActorRecord {uint32_t alive,next,prev,parent,actionRef,faces[2],heads[3],values[ACTOR_VALUES];Ref script[3];} ActorRecord;
typedef struct NodeRecord {uint32_t flags,seq,trigger,active,t14,t1c,t20,next,prev,targetKind,targetValue;Ref current,pending;} NodeRecord;
typedef struct WorldDecoded {uint32_t actorHead,actorFree,player,currentActor,nodeFree,currentNode,sequence;Ref cursor;ActorRecord actor[14];NodeRecord node[72];uint32_t variables[512],activeFlags[64],collisionBlocked[2048];} WorldDecoded;
static int get_world(PeWorldState *,const unsigned char *,size_t,WorldDecoded *);
static int put_world(void *context,unsigned char *out,size_t capacity,size_t *used){
    PeWorldState *s=context;Cursor c={0,out,capacity,0,1};if(used)*used=0;
    if(!s)return 0;s->lastError=PE_WORLD_READY;
    if(!used||!out||!bound(s)){s->lastError=PE_WORLD_UNBOUND;return 0;}
    put(&c,0x35574c50u);put64(&c,s->session);put64(&c,s->scene);put64(&c,s->binding);
    put(&c,(uint32_t)g_SceneDispatchToken);put(&c,(uint32_t)g_SceneDispatchCur);
    put(&c,s->serialCounter);put(&c,s->liveCount);put(&c,s->liveMask);put(&c,s->regionCount);
    for(unsigned i=0;i<s->regionCount;i++){put(&c,s->regions[i].id);put(&c,s->regions[i].bytes);put(&c,s->regions[i].crc);}
    put(&c,s->collisionCount);put(&c,s->collisionStride);put(&c,s->collisionCrc);for(unsigned w=0;w<(s->collisionCount+31)/32;w++){uint32_t bits=0;for(unsigned j=0;j<32&&w*32+j<s->collisionCount;j++){const CollisionTriangleHeader*h=(const void*)((const unsigned char*)s->collisionBase+(size_t)(w*32+j)*s->collisionStride);if(h->kind&128)bits|=1u<<j;}put(&c,bits);}
    put(&c,actor_id(g_FieldActorListHead));put(&c,actor_id(g_EntityFreeListHead));put(&c,actor_id(g_PlayerEntity));put(&c,actor_id(g_CurrentEntity));
    put(&c,node_id(g_ScriptNodeFreeListHead));put(&c,node_id(g_ScriptNodePool));putref(&c,s,g_ScriptCursor);
    put(&c,g_ScriptNodeSeqCounter[0]);
    for(unsigned i=0;i<14;i++){
        const FieldActor *a=&g_EntityPool[i];unsigned alive=(s->liveMask>>i)&1;
        put(&c,alive);put(&c,actor_id(a->next));if(!alive)continue;
        put(&c,actor_id(a->prev));put(&c,actor_id(a->parent));
        for(unsigned j=0;j<3;j++)put(&c,node_id(a->script_node_lists[j]));
        putref(&c,s,a->script_base);putref(&c,s,a->script_cursor_19c);putref(&c,s,a->script_cursor_1a0);
        uint32_t action=action_ref(a);if(action==UINT32_MAX)return 0;put(&c,action);uint32_t face=face_ref(s,a->collision_face),mirror=face_ref(s,a->collision_face_mirror);if(face==UINT32_MAX||mirror==UINT32_MAX)return 0;put(&c,face);put(&c,mirror);
        actor_write(&c,a);
    }
    unsigned char freeNodes[72]={0};unsigned freeSteps=0;for(ScriptNode *free=g_ScriptNodeFreeListHead;free;free=free->next){uint32_t id=node_id(free);if(!id||id>72||freeSteps++>=72||freeNodes[id-1])return 0;freeNodes[id-1]=1;}
    for(unsigned i=0;i<72;i++){
        const ScriptNode *n=&g_ScriptNodes[i];int freeNode=freeNodes[i]||(n->flags&0x10)!=0;
        /* Source producer tags distinguish signed coordinates from actor references. */
        unsigned kind=freeNode?PE_NODE_UNKNOWN:PeStateNode_Type(n);
        uint32_t value=0;
        if(kind==PE_NODE_COORDINATE)value=(uint32_t)n->target18.coordinate;
        else if(kind==PE_NODE_ACTOR){value=actor_id(n->target18.actor);if(value==UINT32_MAX||(value&&!(s->liveMask&(1u<<(value-1)))))return 0;}
        else if(!freeNode&&n->target18.actor){s->lastError=PE_WORLD_UNCLASSIFIED_UNION;return 0;}
        put(&c,kind);put(&c,value);
        put(&c,n->flags);put(&c,n->seq);put(&c,(uint32_t)n->trigger_value);put(&c,(uint32_t)n->active);
        put(&c,(uint32_t)n->target14);put(&c,(uint32_t)n->target1c);put(&c,(uint32_t)n->target20);
        put(&c,node_id(n->next));put(&c,node_id(n->prev));
        putref(&c,s,freeNode?0:n->current.script);putref(&c,s,freeNode?0:(void *)(uintptr_t)n->next_value);
    }
    for(unsigned i=0;i<512;i++)put(&c,g_ScriptVariableBank[i]);
    for(unsigned i=0;i<64;i++)put(&c,(uint32_t)g_ScriptNodeActiveFlags[i]);
    if(!c.ok){if(!s->lastError)s->lastError=PE_WORLD_BOUNDS;return 0;}WorldDecoded checked;if(!get_world(s,out,c.at,&checked)){s->lastError=PE_WORLD_GRAPH_OR_COMPATIBILITY;return 0;}*used=c.at;return 1;
}
static int chain_actor(const WorldDecoded *d,uint32_t head,int alive){uint32_t seen=0,previous=0;unsigned steps=0;while(head){if(head>14||steps++>=14||(seen&(1u<<(head-1))))return 0;const ActorRecord *a=&d->actor[head-1];if((int)a->alive!=alive||(alive&&a->prev!=previous))return 0;seen|=1u<<(head-1);previous=head;head=a->next;}for(unsigned i=0;i<14;i++)if((int)d->actor[i].alive==alive&&!(seen&(1u<<i)))return 0;return 1;}
static int chain_nodes(const WorldDecoded *d,uint32_t head,uint32_t *owned,int freeList){uint32_t previous=0;unsigned steps=0;while(head){if(head>72||steps++>=72)return 0;unsigned i=head-1;if(owned[i/32]&(1u<<(i%32)))return 0;owned[i/32]|=1u<<(i%32);const NodeRecord *n=&d->node[i];if(!freeList&&n->prev!=previous)return 0;if(freeList&&(n->current.region||n->pending.region))return 0;previous=head;head=n->next;}return 1;}
static int get_world_mode(PeWorldState *s,const unsigned char *data,size_t bytes,WorldDecoded *d,int target){
    Cursor c={data,0,bytes,0,1};if(!data||!d||bytes>PE_WORLD_MAX_BYTES||!(target?source_bound(s):bound(s)))return 0;memset(d,0,sizeof *d);
    if(get(&c)!=0x35574c50u||get64(&c)!=s->session||get64(&c)!=s->scene||get64(&c)!=s->binding)return 0;
    if(get(&c)!=(uint32_t)g_SceneDispatchToken||get(&c)!=(uint32_t)g_SceneDispatchCur||get(&c)!=s->serialCounter||get(&c)!=s->liveCount||get(&c)!=s->liveMask||get(&c)!=s->regionCount)return 0;
    for(unsigned i=0;i<s->regionCount;i++)if(get(&c)!=s->regions[i].id||get(&c)!=s->regions[i].bytes||get(&c)!=s->regions[i].crc)return 0;
    if(get(&c)!=s->collisionCount||get(&c)!=s->collisionStride||get(&c)!=s->collisionCrc)return 0;unsigned collisionWords=(s->collisionCount+31)/32;if(collisionWords>2048)return 0;for(unsigned w=0;w<collisionWords;w++)d->collisionBlocked[w]=get(&c);if(collisionWords&&(s->collisionCount%32)&&(d->collisionBlocked[collisionWords-1]>>(s->collisionCount%32)))return 0;
    d->actorHead=get(&c);d->actorFree=get(&c);d->player=get(&c);d->currentActor=get(&c);d->nodeFree=get(&c);d->currentNode=get(&c);d->cursor=getref(&c,s);d->sequence=get(&c);
    if(d->actorHead>14||d->actorFree>14||d->player>14||d->currentActor>14||d->nodeFree>72||d->currentNode>72||d->sequence>65535)return 0;
    if((d->player&&!(s->liveMask&(1u<<(d->player-1))))||(d->currentActor&&!(s->liveMask&(1u<<(d->currentActor-1)))))return 0;
    for(unsigned i=0;i<14;i++){
        ActorRecord *a=&d->actor[i];a->alive=get(&c);a->next=get(&c);
        if(a->alive!=((s->liveMask>>i)&1)||a->next>14)return 0;if(!a->alive)continue;
        a->prev=get(&c);a->parent=get(&c);if(a->prev>14||a->parent>14||a->parent==i+1)return 0;
        for(unsigned j=0;j<3;j++){a->heads[j]=get(&c);if(a->heads[j]>72)return 0;}
        for(unsigned j=0;j<3;j++)a->script[j]=getref(&c,s);
        a->actionRef=get(&c);if(a->actionRef>48)return 0;a->faces[0]=get(&c);a->faces[1]=get(&c);if(a->faces[0]>s->collisionCount||a->faces[1]>s->collisionCount)return 0;
        if(!actor_read(&c,a->values)||a->values[0]!=s->actor[i].type||a->values[1]!=s->actor[i].sub||a->values[10]!=s->actor[i].serial)return 0;
        if(a->actionRef&&(a->values[0]>=10||!s->bankRows[a->values[0]][a->actionRef-1]))return 0;
        if(a->parent&&!(s->liveMask&(1u<<(a->parent-1))))return 0;
    }
    for(unsigned i=0;i<14;i++){uint32_t parent=d->actor[i].parent;unsigned count=0;while(parent){if(++count>14)return 0;parent=d->actor[parent-1].parent;}}
    for(unsigned i=0;i<72;i++){
        NodeRecord *n=&d->node[i];n->targetKind=get(&c);n->targetValue=get(&c);if(n->targetKind>PE_NODE_ACTOR||(n->targetKind==PE_NODE_UNKNOWN&&n->targetValue)||(n->targetKind==PE_NODE_ACTOR&&(n->targetValue>14||(n->targetValue&&!(s->liveMask&(1u<<(n->targetValue-1)))))))return 0;n->flags=get(&c);n->seq=get(&c);n->trigger=get(&c);n->active=get(&c);n->t14=get(&c);n->t1c=get(&c);n->t20=get(&c);n->next=get(&c);n->prev=get(&c);n->current=getref(&c,s);n->pending=getref(&c,s);
        if(n->flags>65535||n->seq>65535||n->next>72||n->prev>72||((n->flags&0x10)&&(n->current.region||n->pending.region)))return 0;
    }
    for(unsigned i=0;i<512;i++)d->variables[i]=get(&c);
    for(unsigned i=0;i<64;i++)d->activeFlags[i]=get(&c);
    if(!c.ok||c.at!=bytes||!chain_actor(d,d->actorHead,1)||!chain_actor(d,d->actorFree,0))return 0;
    uint32_t owned[3]={0};if(!chain_nodes(d,d->nodeFree,owned,1))return 0;
    for(unsigned i=0;i<14;i++)if(d->actor[i].alive)for(unsigned j=0;j<3;j++)if(!chain_nodes(d,d->actor[i].heads[j],owned,0))return 0;
    return 1;
}
static int get_world(PeWorldState*s,const unsigned char*data,size_t bytes,WorldDecoded*d){return get_world_mode(s,data,bytes,d,0);}
int PeWorld_PreflightTarget(PeWorldState*s,const unsigned char*data,size_t bytes){WorldDecoded d;return get_world_mode(s,data,bytes,&d,1);}
static int validate_world(void *context,const unsigned char *data,size_t bytes){PeWorldState*s=context;WorldDecoded decoded;return s&&s->targetMode?get_world_mode(s,data,bytes,&decoded,1):get_world(s,data,bytes,&decoded);}
static int restore_world(void *context,const unsigned char *data,size_t bytes){
    PeWorldState *s=context;WorldDecoded d;if(!s||!(s->targetMode?constructed_bound(s)&&get_world_mode(s,data,bytes,&d,1):get_world(s,data,bytes,&d)))return 0;
    /* Every reference and whole graph is checked before the first live write. */
    for(unsigned i=0;i<s->collisionCount;i++){CollisionTriangleHeader*h=(void*)((unsigned char*)s->collisionBase+(size_t)i*s->collisionStride);h->kind=(u8)((h->kind&127u)|(((d.collisionBlocked[i/32]>>(i%32))&1u)<<7));}
    for(unsigned i=0;i<14;i++){
        FieldActor *a=&g_EntityPool[i];ActorRecord *v=&d.actor[i];a->next=actor_ptr(v->next);if(!v->alive)continue;
        actor_restore(a,v->values);a->action_data=v->actionRef?s->bankRows[v->values[0]][v->actionRef-1]:0;a->collision_face=face_ptr(s,v->faces[0]);a->collision_face_mirror=face_ptr(s,v->faces[1]);a->prev=actor_ptr(v->prev);a->parent=actor_ptr(v->parent);
        for(unsigned j=0;j<3;j++)a->script_node_lists[j]=node_ptr(v->heads[j]);
        a->script_base=decode_ref(s,v->script[0]);a->script_cursor_19c=decode_ref(s,v->script[1]);a->script_cursor_1a0=decode_ref(s,v->script[2]);
    }
    for(unsigned i=0;i<72;i++){
        ScriptNode *n=&g_ScriptNodes[i];NodeRecord *v=&d.node[i];n->flags=(unsigned short)v->flags;n->seq=(unsigned short)v->seq;
        n->trigger_value=(int)v->trigger;n->active=(int)v->active;n->target14=(int)v->t14;n->target1c=(int)v->t1c;n->target20=(int)v->t20;n->target18.actor=0;
        if(v->targetKind==PE_NODE_COORDINATE)n->target18.coordinate=(int32_t)v->targetValue;
        else if(v->targetKind==PE_NODE_ACTOR)n->target18.actor=actor_ptr(v->targetValue);
        PeStateNode_Tag(n,v->targetKind);
        n->next=node_ptr(v->next);n->prev=node_ptr(v->prev);n->current.script=decode_ref(s,v->current);n->next_value=(intptr_t)decode_ref(s,v->pending);
    }
    for(unsigned i=0;i<512;i++)g_ScriptVariableBank[i]=d.variables[i];
    for(unsigned i=0;i<64;i++)g_ScriptNodeActiveFlags[i]=(int)d.activeFlags[i];
    g_FieldActorListHead=actor_ptr(d.actorHead);g_EntityFreeListHead=actor_ptr(d.actorFree);g_PlayerEntity=actor_ptr(d.player);g_CurrentEntity=actor_ptr(d.currentActor);
    g_ScriptNodeFreeListHead=node_ptr(d.nodeFree);g_ScriptNodePool=node_ptr(d.currentNode);g_ScriptCursor=decode_ref(s,d.cursor);g_ScriptNodeSeqCounter[0]=(unsigned short)d.sequence;
    return 1;
}
int PeState_RegisterWorldSubset(PeStateRegistry *registry,PeWorldState *context){PeStateModule module={PE_STATE_SCENE,5,PE_WORLD_MAX_BYTES,put_world,context,validate_world,restore_world,0};return PeState_Register(registry,module);}

int PeWorld_BindLoadedScene(PeWorldState *state){
    PeScriptAsset asset;
    if(!state||!PeStateAssets_Get(&asset))return PE_STATE_INCOMPLETE;
    PeWorld_Init(state);
    int result=PeWorld_RegisterScriptRegion(state,1,asset.data,asset.bytes);
    if(result)return result;
    return PeWorld_BindCurrentScene(state);
}

static uint32_t native_core_id(const void*p){if(!p)return 0;if(p==(const void*)&g_AyaBattleState)return 1;for(unsigned i=0;i<7;i++)if(p==(const void*)&g_BattleEnemySlots[i].combatant)return 2+i;return UINT32_MAX;}
static void*native_core_ptr(uint32_t id){return !id?0:id==1?(void*)&g_AyaBattleState:id<=8?(void*)&g_BattleEnemySlots[id-2].combatant:0;}
int PeWorld_NativeActorRoles(const FieldActor*a,uint32_t*core,uint32_t*frame,uint32_t*contact){if(!a||!core||!frame||!contact||a->type_id>=13)return 0;unsigned type=a->type_id;PeScriptAsset asset;if(!PeStateAssets_Get(&asset)||type>=asset.count||a->script_base!=asset.view->offsets[type])return 0;*core=native_core_id(a->state);if(*core==UINT32_MAX)return 0;*frame=!a->frame_callback?0:a->frame_callback==g_FieldActorTypeCallbacks[type].frame?1:UINT32_MAX;*contact=!a->contact_callback?0:a->contact_callback==g_FieldActorTypeCallbacks[type].contact?1:a->contact_callback==Entity_CheckActionIdMatch?2:UINT32_MAX;return *frame!=UINT32_MAX&&*contact!=UINT32_MAX;}
static int retained_bindings_known(const PeWorldState*s){PeScriptAsset asset;if(!PeStateAssets_Get(&asset))return 0;for(unsigned i=0;i<14;i++)if(s->liveMask&(1u<<i)){const PeWorldBinding*b=&s->actor[i];unsigned type=b->type;if(type>=13||type>=asset.count||b->sub>255||b->coreRole>8||b->frameRole>1||b->contactRole>2||native_core_ptr(b->coreRole)!=b->state||b->script!=asset.view->offsets[type]||b->model!=(type<10?g_GameState.bank_slots[type]:0)||b->header!=b->model)return 0;if((b->frameRole?g_FieldActorTypeCallbacks[type].frame:0)!=b->frame||(b->contactRole==2?Entity_CheckActionIdMatch:b->contactRole?g_FieldActorTypeCallbacks[type].contact:0)!=b->contact)return 0;if(b->action){int found=0;if(type<10)for(unsigned j=0;j<48;j++)if(b->action==s->bankRows[type][j])found=1;if(!found)return 0;}}return 1;}
static int known_core(const void *p){if(!p||p==(const void *)&g_AyaBattleState)return 1;for(unsigned i=0;i<7;i++)if(p==(const void *)&g_BattleEnemySlots[i].combatant)return 1;return 0;}
int PeWorld_ValidateTargetConstruction(const PeWorldState*s,const uint32_t types[14],uint32_t mask){if(!types||!source_bound(s)||mask!=s->liveMask||!s->constructorPlayerId||s->constructorPlayerId>14||!(mask&(1u<<(s->constructorPlayerId-1))))return 0;unsigned count=0;for(unsigned i=0;i<14;i++)if(mask&(1u<<i)){if(types[i]!=s->actor[i].type)return 0;count++;}return count==s->liveCount;}
int PeWorld_ValidateEquivalentConstruction(const PeWorldState *s,const uint32_t types[14],uint32_t mask){
    if(!s||!types||!PeStateBoundary_AtCpuPoint()||!s->binding||s->liveMask!=mask||s->geometryOwner!=g_GameState.geometry_state||s->resourceEpoch!=PeStateResource_Epoch()||s->assetGeneration!=PeStateAssets_Generation()||s->session!=PeStateBoundary_Session()||s->scene!=PeStateBoundary_SceneEpoch())return 0;
    unsigned count=0;for(unsigned i=0;i<14;i++)if(mask&(1u<<i))count++;if(count!=s->liveCount||!s->constructorPlayerId||s->constructorPlayerId>14||!(mask&(1u<<(s->constructorPlayerId-1))))return 0;
    for(unsigned i=0;i<10;i++)for(unsigned j=0;j<48;j++)if(s->bankRows[i][j]!=g_GameState.bank_rows[i][j])return 0;
    for(unsigned i=0;i<s->regionCount;i++)if(checksum(s->regions[i].base,s->regions[i].bytes)!=s->regions[i].crc)return 0;
    PeScriptAsset asset;if(!PeStateAssets_Get(&asset))return 0;
    for(unsigned i=0;i<14;i++)if(mask&(1u<<i)){const PeWorldBinding *b=&s->actor[i];unsigned type=types[i];if(b->coreRole>8||b->frameRole>1||b->contactRole>2||native_core_ptr(b->coreRole)!=b->state||(b->frameRole?g_FieldActorTypeCallbacks[type<13?type:0].frame:0)!=b->frame||(b->contactRole==2?Entity_CheckActionIdMatch:b->contactRole?g_FieldActorTypeCallbacks[type<13?type:0].contact:0)!=b->contact)return 0;if(type>=13||type>=asset.count||!known_core(b->state)||b->script!=asset.view->offsets[type]||b->frame!=g_FieldActorTypeCallbacks[type].frame||(b->contact!=g_FieldActorTypeCallbacks[type].contact&&b->contact!=Entity_CheckActionIdMatch)||b->model!=(type<10?g_GameState.bank_slots[type]:0))return 0;if(b->model&&b->header!=b->model)return 0;}
    return 1;
}
int PeWorld_RestoreConstructorBindings(PeWorldState *s){
    if(!s||!PeStateResource_IsRebuilding()||g_EntityFreePoolCount!=s->liveCount)return 0;
    uint32_t mask;if(!live_mask(&mask)||mask!=s->liveMask)return 0;
    for(unsigned i=0;i<14;i++)if(mask&(1u<<i)){FieldActor *a=&g_EntityPool[i];const PeWorldBinding *b=&s->actor[i];if(b->coreRole>8||b->frameRole>1||b->contactRole>2||native_core_ptr(b->coreRole)!=b->state)return 0;unsigned type=a->type_id;if(type>=13)return 0;a->state=(FieldActorState*)native_core_ptr(b->coreRole);a->frame_callback=b->frameRole?g_FieldActorTypeCallbacks[type].frame:0;a->contact_callback=b->contactRole==2?Entity_CheckActionIdMatch:b->contactRole?g_FieldActorTypeCallbacks[type].contact:0;a->field_sfx_id=b->serial;a->script_base=(unsigned char *)b->script;}
    g_EntitySerialCounter=s->serialCounter;g_PlayerEntity=actor_ptr(s->constructorPlayerId);return s->targetMode?constructed_bound(s):bound(s);
}
