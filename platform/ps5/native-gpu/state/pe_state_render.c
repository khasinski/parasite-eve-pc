#include "pe_state_render.h"
#include "pe_state_room_object.h"
#include "pe_state_actor_prepare.h"
#include "pe_state_boundary.h"
#include "pe_state_assets.h"
#include "pe_state_lifetime.h"
#include "pe_state_bank_sources.h"
#include "pe1/entity_allocation.h"
#include "pe1/render_buffers.h"
#include "pe1/render_setup.h"
#include "pe1/render_shadow.h"
#include <string.h>
#include <stdint.h>
#define RENDER_MAX_BYTES 1048576u
typedef struct Config{RenderObjectHeader *model;unsigned char *packet;uint64_t generation;short x,y,unused,palette,init;int setup;}Config;
static uint64_t restoreRevision;
uint64_t PeStateRender_Revision(void){return restoreRevision;}
static Config configs[14];static uint64_t serial=1;static int reconstructing;
static unsigned actor_id(const FieldActor *a){uintptr_t p=(uintptr_t)a,b=(uintptr_t)g_EntityPool;return !a?0:p>=b&&p-b<sizeof g_EntityPool&&(p-b)%sizeof *a==0?(unsigned)((p-b)/sizeof *a)+1:UINT32_MAX;}
static unsigned object_id(const RenderObjectEntity *o){if(!o)return 0;for(unsigned i=0;i<14;i++)if(o==&g_EntityPool[i].render_object)return i+1;return UINT32_MAX;}
void PeStateRender_Configured(RenderObjectEntity *o,RenderObjectHeader *m,unsigned char *p,short x,short y,short unused,short palette,short init,int setup){PeStateRoom_Configured(o,m,p,x,y,unused,palette,init,setup);unsigned id=object_id(o);if(id==0||id==UINT32_MAX||reconstructing)return;configs[id-1]=(Config){m,p,++serial,x,y,unused,palette,init,setup};}
/* Render_DrawWithAnim stores the actor's authentic action_data. World ownership
 * already identifies those immutable rows by source bank/row, never insertion order. */
static uint32_t ref_id(const void*p){if(!p)return 0;for(unsigned bank=0;bank<10;bank++)for(unsigned row=0;row<48;row++)if(g_GameState.bank_rows[bank][row]==p)return 1+bank*48+row;return UINT32_MAX;}
static int valid_animation(uint32_t id){return !id||(id<=480&&g_GameState.bank_rows[(id-1)/48][(id-1)%48]);}
static void*animation_ptr(uint32_t id){return id?g_GameState.bank_rows[(id-1)/48][(id-1)%48]:0;}
static void put(unsigned char *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(v>>(8*i));}
static uint32_t get(const unsigned char *p){return(uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static int canonical(uint32_t v,unsigned kind){return kind==1?v<=255:kind==2?v<=65535:kind==3?((int32_t)v>=-32768&&(int32_t)v<=32767):kind==4?((int32_t)v>=-128&&(int32_t)v<=127):1;}
static int span(const void *ptr,size_t bytes,uintptr_t base,size_t cap){uintptr_t p=(uintptr_t)ptr;return p>=base&&p-base<=cap&&bytes<=cap-(p-base);}
static int owner(unsigned i,unsigned *entries){FieldActor *a=&g_EntityPool[i];RenderObjectEntity *o=&a->render_object;Config *c=&configs[i];if(!c->generation||!o->header||c->model!=o->header||a->model_state.model_header!=o->header||c->packet!=o->primitive_buffer)return 0;uintptr_t base=a->allocation_block;size_t cap=0;for(unsigned j=0;j<16;j++)if(g_EntityAllocationTable[j].address==base){unsigned n=g_EntityAllocationTable[j].blockCount;if(n!=1&&n!=2&&n!=4&&n!=8)return 0;if(j+n>16)return 0;cap=(size_t)n*ENTITY_ALLOCATION_BLOCK_SIZE;break;}if(!cap||!span((void *)base,2*sizeof(RenderShadowQuad),base,cap)||!span(c->packet,0,base,cap)||!span(o->animation_entries,0,base,cap)||!span(o->matrices,(size_t)o->header->part_count*sizeof(RenderMatrix),base,cap))return 0;uintptr_t lo=(uintptr_t)o->animation_entries,hi=(uintptr_t)o->matrices;if(hi<lo||(hi-lo)%sizeof(RenderAnimationLookupEntry)||(hi-lo)/sizeof(RenderAnimationLookupEntry)>255)return 0;*entries=(unsigned)((hi-lo)/sizeof(RenderAnimationLookupEntry));if(*entries>o->header->animation_entry_count)return 0;size_t packets=(size_t)o->header->packet34_count*sizeof(RenderPacket34)+(size_t)o->header->packet28_count*sizeof(RenderPacket28)+(size_t)o->header->packet24_count*sizeof(RenderPacket24)+(size_t)o->header->packet1c_count*sizeof(RenderPacket1C);size_t required=(c->setup?packets*2:0)+(size_t)o->header->animation_entry_count*sizeof(RenderAnimationLookupEntry)+(size_t)o->header->part_count*sizeof(RenderMatrix);return span(c->packet,required,base,cap);}
static int live_mask(uint32_t *out){uint32_t mask=0;unsigned n=0;for(FieldActor *a=g_FieldActorListHead;a;a=a->next){unsigned id=actor_id(a);if(!id||id>14||n++>=14||(mask&(1u<<(id-1))))return 0;mask|=1u<<(id-1);}*out=mask;return 1;}
#define PLAN_WORDS 14u
#define PLAN_BYTES (14u*PLAN_WORDS*4u)
static int current_plan(unsigned char *out){uint32_t mask;if(!live_mask(&mask))return 0;uint32_t used=0;for(unsigned i=0;i<14;i++){FieldActor *a=&g_EntityPool[i];Config *c=&configs[i];uint32_t values[14]={0};if(mask&(1u<<i)){values[0]=1;values[1]=a->type_id;values[2]=a->sub_id;if(a->model_state.allocation_active){unsigned entries;if(!owner(i,&entries))return 0;values[3]=(uint32_t)c->setup;for(unsigned j=0;j<16;j++)if(g_EntityAllocationTable[j].address==a->allocation_block){values[4]=j+1;values[5]=g_EntityAllocationTable[j].blockCount;break;}if(!values[4]||values[5]>8)return 0;uint32_t bits=((1u<<values[5])-1)<<(values[4]-1);if(used&bits)return 0;used|=bits;values[6]=(uint32_t)((uintptr_t)c->packet-a->allocation_block);values[7]=(uint32_t)c->x;values[8]=(uint32_t)c->y;values[9]=(uint32_t)c->unused;values[10]=(uint32_t)c->palette;values[11]=(uint32_t)c->init;values[12]=(uint32_t)c->generation;values[13]=(uint32_t)(c->generation>>32);}}for(unsigned j=0;j<14;j++)put(out+(i*14+j)*4,values[j]);}
/* Reject scratch allocations whose owner has not been captured. */
for(unsigned i=0;i<16;i++)if(g_EntityAllocationTable[i].address){int found=0;for(unsigned j=0;j<14;j++)if(get(out+(j*14+4)*4)==i+1)found=1;if(!found)return 0;}return 1;}
typedef struct ModelShape{const RenderObjectHeader*header;const RenderVec3s*bounds;unsigned source;uint32_t offset;uint64_t epoch;}ModelShape;
/* Exact original Render_SetupEntityPrims source section order. No object setup,
 * packet writes, allocation, callbacks or reads outside its witnessed loader span. */
static int model_shape(const RenderObjectHeader*m,ModelShape*out){unsigned source;uint32_t offset;size_t available;uint64_t epoch;if(!m||!out||((uintptr_t)m%_Alignof(RenderObjectHeader))||!PeStateBankSource_Locate(m,sizeof *m,&source,&offset,&available,&epoch))return 0;size_t at=sizeof *m;
#define SECTION(count,type) do{size_t n=(size_t)(count);if(at>available||n>(available-at)/sizeof(type))return 0;at+=n*sizeof(type);}while(0)
SECTION(m->part_count,RenderObjectPart);SECTION(m->vertex_count,RenderVec3s);SECTION(m->vertex_count,unsigned int);size_t primitives=(size_t)m->packet34_count+m->packet28_count+m->packet24_count+m->packet1c_count;SECTION(primitives,RenderPrimitiveDescriptor);SECTION(2,RenderVec3s);size_t bounds=at;SECTION((size_t)m->part_count*2,RenderVec3s);SECTION(1,RenderVec3s);SECTION(((size_t)m->matrix_command_bytes+3)&~(size_t)3,unsigned char);SECTION(m->packet34_count,RenderQuadTexture);SECTION(m->packet28_count,RenderTriTexture);
#undef SECTION
*out=(ModelShape){m,(const RenderVec3s*)((const unsigned char*)m+bounds),source,offset,epoch};return 1;}
int PeStateRender_GetConstructionPlan(const unsigned char *in,size_t bytes,PeActorConstructionPlan *out){if(!in||!out||bytes<44+PLAN_BYTES||memcmp(in,"PEREND03",8)||!PeStateBoundary_AtCpuPoint()||get(in+8)!=(uint32_t)PeStateBoundary_Session()||get(in+12)!=(uint32_t)(PeStateBoundary_Session()>>32)||get(in+16)!=(uint32_t)PeStateBoundary_SceneEpoch()||get(in+20)!=(uint32_t)(PeStateBoundary_SceneEpoch()>>32)||get(in+24)!=(uint32_t)PeStateAssets_Generation()||get(in+28)!=(uint32_t)(PeStateAssets_Generation()>>32))return 0;PeScriptAsset asset;if(!PeStateAssets_Get(&asset))return 0;memset(out,0,sizeof *out);for(unsigned i=0;i<14;i++){const unsigned char *p=in+40+i*56;uint32_t v[14];for(unsigned j=0;j<14;j++)v[j]=get(p+j*4);if(v[0]>1)return 0;if(!v[0]){for(unsigned j=1;j<14;j++)if(v[j])return 0;continue;}if(v[1]>=13||v[2]>255||v[1]>=asset.count)return 0;out->liveMask|=1u<<i;RenderObjectHeader *model=v[1]<10?g_GameState.bank_slots[v[1]]:0;ModelShape shape;if(model&&!model_shape(model,&shape))return 0;if(!model){for(unsigned j=3;j<14;j++)if(v[j])return 0;}else{if(v[3]>1||!v[4]||v[4]>16||(v[5]!=1&&v[5]!=2&&v[5]!=4&&v[5]!=8)||v[4]-1+v[5]>16||v[6]!=2*sizeof(RenderShadowQuad)||(!v[12]&&!v[13]))return 0;for(unsigned j=7;j<12;j++)if((int32_t)v[j]<-32768||(int32_t)v[j]>32767)return 0;size_t needed=2*sizeof(RenderShadowQuad)+(size_t)model->animation_entry_count*sizeof(RenderAnimationLookupEntry)+(size_t)model->part_count*sizeof(RenderMatrix);if(v[3])needed+=2*((size_t)model->packet34_count*sizeof(RenderPacket34)+(size_t)model->packet28_count*sizeof(RenderPacket28)+(size_t)model->packet24_count*sizeof(RenderPacket24)+(size_t)model->packet1c_count*sizeof(RenderPacket1C));unsigned blocks=needed<=ENTITY_ALLOCATION_BLOCK_SIZE?1:needed<=2*ENTITY_ALLOCATION_BLOCK_SIZE?2:needed<=4*ENTITY_ALLOCATION_BLOCK_SIZE?4:8;if(needed>8*ENTITY_ALLOCATION_BLOCK_SIZE||v[5]!=blocks)return 0;uint32_t bits=((1u<<blocks)-1)<<(v[4]-1);if(out->usedBlockMask&bits)return 0;out->usedBlockMask|=bits;}out->actor[i]=(PeActorConstruction){v[0],v[1],v[2],v[3],v[4],v[5],v[6],(int32_t)v[7],(int32_t)v[8],(int32_t)v[9],(int32_t)v[10],(int32_t)v[11],v[12],v[13]};}return get(in+40+PLAN_BYTES)==out->liveMask;}
static int walk(const unsigned char *in,unsigned char *out,size_t bytes,size_t *used,int apply){size_t at=40+PLAN_BYTES;uint32_t mask;if(!live_mask(&mask))return 0;
#define WORD(value) do{if(at>bytes||bytes-at<4)return 0;if(out)put(out+at,value);else if(get(in+at)!=(uint32_t)(value))return 0;at+=4;}while(0)
#define FIELD(value,kind) do{if(at>bytes||bytes-at<4)return 0;uint32_t v=out?(uint32_t)(value):get(in+at);if(!canonical(v,kind))return 0;if(out)put(out+at,v);else if(apply)(value)=(__typeof__(value))v;at+=4;}while(0)
WORD(mask);for(unsigned i=0;i<14;i++){if(!(mask&(1u<<i)))continue;FieldActor *a=&g_EntityPool[i];RenderObjectEntity *o=&a->render_object;Config *c=&configs[i];unsigned has=a->model_state.allocation_active?1:0;WORD(has);if(!has)continue;unsigned entries;if(!owner(i,&entries))return 0;WORD((uint32_t)c->generation);WORD((uint32_t)(c->generation>>32));WORD(o->header->part_count);WORD(entries);unsigned active=0;if(o->active_matrix){if(o->active_matrix==&o->model_matrix)active=1;else{uintptr_t p=(uintptr_t)o->active_matrix,b=(uintptr_t)o->matrices;if(p<b||p-b>=(size_t)o->header->part_count*sizeof(RenderMatrix)||(p-b)%sizeof(RenderMatrix))return 0;active=2+(unsigned)((p-b)/sizeof(RenderMatrix));}}unsigned source=object_id(o->animation_source),reflection=actor_id(o->reflection_owner),animation=out?ref_id(o->animation_data):0;if(source==UINT32_MAX||reflection==UINT32_MAX||animation==UINT32_MAX)return 0;
uint32_t refs[4]={active,source,reflection,animation};for(unsigned j=0;j<4;j++){if(at>bytes||bytes-at<4)return 0;if(out)put(out+at,refs[j]);else refs[j]=get(in+at);at+=4;}if(refs[0]>1+o->header->part_count||refs[1]>14||refs[2]>14||!valid_animation(refs[3]))return 0;if((refs[1]&&!(mask&(1u<<(refs[1]-1))))||(refs[2]&&!(mask&(1u<<(refs[2]-1)))))return 0;
/* Shared source-model state is applied before its original parser reconstructs packets. */
if(!out){size_t p=at+12;unsigned expected=0;if(p>bytes||(size_t)o->header->part_count*8>bytes-p)return 0;for(unsigned j=0;j<o->header->part_count;j++){uint32_t visible=get(in+p+j*8);if(visible==1&&o->bounds_vertices[j*2+1].pad>=0)expected++;}if(expected!=entries)return 0;}
FIELD(o->header->shadow_radius,2);FIELD(o->header->scale,2);FIELD(o->header->visible_part_count,2);for(unsigned j=0;j<o->header->part_count;j++){FIELD(o->parts[j].visible,1);FIELD(o->parts[j].translation_z,0);}
if(apply&&!out){u16 shadow=o->header->shadow_radius,scale=o->header->scale,visible=o->header->visible_part_count;s8 *texture=0;reconstructing=1;int ok=Render_SetupEntityPrims(o,c->model,c->packet,c->x,c->y,c->unused,c->palette,c->init,&texture,c->setup);reconstructing=0;if(!ok)return 0;o->header->shadow_radius=shadow;o->header->scale=scale;o->header->visible_part_count=visible;}
FIELD(o->reserved9f,1);
FIELD(o->animation_state,3);
FIELD(o->animation_id,3);
FIELD(o->table_value2c,2);
FIELD(o->table_value2e,2);
FIELD(o->table_value30,2);
FIELD(o->table_index,2);
FIELD(o->projected_x,3);
FIELD(o->projected_y,3);
FIELD(o->shade,1);
FIELD(o->lightNegativeY,1);
FIELD(o->lightPositiveY,1);
FIELD(o->fade_blue_step,1);
FIELD(o->script_param97,1);
FIELD(o->script_param98,1);
FIELD(o->script_param99,1);
FIELD(o->script_value9a,3);
FIELD(o->flags_9C,2);
FIELD(o->variant_visible,1);
FIELD(o->target_x,3);
FIELD(o->target_y,3);
FIELD(o->target_z,3);
FIELD(o->draw_count,3);
FIELD(o->projected_target_x,3);
FIELD(o->projected_target_y,3);
FIELD(o->fade_remaining,4);
FIELD(o->fade_duration,4);
FIELD(o->fade_red_step,1);
FIELD(o->fade_green_step,1);
FIELD(o->primitive_red,1);
FIELD(o->primitive_green,1);
FIELD(o->primitive_blue,1);
FIELD(o->fade_red,1);
FIELD(o->fade_green,1);
FIELD(o->fade_blue,1);
for(unsigned j=0;j<2;j++){FIELD(o->rotation_overrides[j].x,2);FIELD(o->rotation_overrides[j].y,2);FIELD(o->rotation_overrides[j].z,2);FIELD(o->rotation_overrides[j].matrix_index,1);FIELD(o->rotation_overrides[j].flags,1);}
for(unsigned j=0;j<2;j++){RenderAnimationLookupEntry *entry=j?&o->hit_body:&o->hit_cylinder;FIELD(entry->value0,3);FIELD(entry->value1,3);FIELD(entry->value2,3);FIELD(entry->animation_id,3);FIELD(entry->radius,3);FIELD(entry->bounds_value1,3);}
for(unsigned j=0;j<=o->header->part_count;j++){RenderMatrix *m=j?&o->matrices[j-1]:&o->model_matrix;for(unsigned y=0;y<3;y++)for(unsigned x=0;x<3;x++){FIELD(m->rotation[y][x],3);}for(unsigned x=0;x<3;x++){FIELD(m->translation[x],0);}}
for(unsigned j=0;j<entries;j++){RenderAnimationLookupEntry *entry=&o->animation_entries[j];FIELD(entry->value0,3);FIELD(entry->value1,3);FIELD(entry->value2,3);FIELD(entry->animation_id,3);FIELD(entry->radius,3);FIELD(entry->bounds_value1,3);}
FIELD(a->shadow_matrix_index,1);FIELD(a->shadow_intensity,1);if(apply&&!out){o->active_matrix=refs[0]==0?0:refs[0]==1?&o->model_matrix:&o->matrices[refs[0]-2];o->animation_source=refs[1]?&g_EntityPool[refs[1]-1].render_object:0;o->reflection_owner=refs[2]?&g_EntityPool[refs[2]-1]:0;o->animation_data=animation_ptr(refs[3]);}
}
#undef WORD
#undef FIELD
if(used)*used=at;return out?1:at==bytes;}
typedef struct TargetCursor{const unsigned char*in;size_t bytes,at;int ok;}TargetCursor;
static uint32_t target_take(TargetCursor*c,unsigned kind){if(c->at>c->bytes||c->bytes-c->at<4){c->ok=0;return 0;}uint32_t v=get(c->in+c->at);c->at+=4;if(!canonical(v,kind))c->ok=0;return v;}
int PeStateRender_PreflightTarget(const unsigned char*in,size_t bytes){PeActorConstructionPlan plan;if(!PeStateRender_GetConstructionPlan(in,bytes,&plan)||bytes>RENDER_MAX_BYTES)return 0;TargetCursor c={in,bytes,40+PLAN_BYTES,1};if(target_take(&c,0)!=plan.liveMask)return 0;static const unsigned char scalarKinds[]={1,3,3,2,2,2,2,3,3,1,1,1,1,1,1,1,3,2,1,3,3,3,3,3,3,4,4,1,1,1,1,1,1,1,1};
for(unsigned i=0;i<14;i++)if(plan.liveMask&(1u<<i)){PeActorConstruction*p=&plan.actor[i];unsigned has=target_take(&c,0);if(has>1||has!=(p->slot!=0))return 0;if(!has)continue;ModelShape shape;RenderObjectHeader*model=g_GameState.bank_slots[p->type];if(!model_shape(model,&shape))return 0;
if(target_take(&c,0)!=p->generationLo||target_take(&c,0)!=p->generationHi||target_take(&c,0)!=model->part_count)return 0;unsigned entries=target_take(&c,0);if(entries>model->animation_entry_count||entries>model->part_count)return 0;
unsigned active=target_take(&c,0),source=target_take(&c,0),reflection=target_take(&c,0),animation=target_take(&c,0);if(active>1u+model->part_count||source>14||reflection>14||!valid_animation(animation)||(source&&!(plan.liveMask&(1u<<(source-1))))||(reflection&&!(plan.liveMask&(1u<<(reflection-1)))))return 0;
(void)target_take(&c,2);(void)target_take(&c,2);unsigned visibleCount=target_take(&c,2);if(visibleCount>model->part_count)return 0;unsigned expected=0;for(unsigned j=0;j<model->part_count;j++){unsigned visible=target_take(&c,1);(void)target_take(&c,0);if(visible==1&&shape.bounds[j*2+1].pad>=0)expected++;}if(expected!=entries)return 0;
for(unsigned j=0;j<sizeof scalarKinds;j++)(void)target_take(&c,scalarKinds[j]);for(unsigned j=0;j<2;j++){for(unsigned k=0;k<3;k++)(void)target_take(&c,2);for(unsigned k=0;k<2;k++)(void)target_take(&c,1);}for(unsigned j=0;j<2;j++)for(unsigned k=0;k<6;k++)(void)target_take(&c,3);
for(unsigned j=0;j<=model->part_count;j++){for(unsigned k=0;k<9;k++)(void)target_take(&c,3);for(unsigned k=0;k<3;k++)(void)target_take(&c,0);}for(unsigned j=0;j<entries;j++)for(unsigned k=0;k<6;k++)(void)target_take(&c,3);(void)target_take(&c,1);(void)target_take(&c,1);if(!c.ok)return 0;}return c.ok&&c.at==bytes;}
static int validate(void *ctx,const unsigned char *in,size_t bytes){PeWorldState*world=ctx;if(world&&world->targetMode)return PeStateRender_PreflightTarget(in,bytes);if(!in||bytes<44+PLAN_BYTES||bytes>RENDER_MAX_BYTES||memcmp(in,"PEREND03",8)||!PeStateBoundary_AtCpuPoint())return 0;uint64_t session=(uint64_t)get(in+8)|((uint64_t)get(in+12)<<32),scene=(uint64_t)get(in+16)|((uint64_t)get(in+20)<<32),assets=(uint64_t)get(in+24)|((uint64_t)get(in+28)<<32),resource=(uint64_t)get(in+32)|((uint64_t)get(in+36)<<32);PeActorConstructionPlan plan;unsigned char current[PLAN_BYTES];if(!PeStateRender_PreflightTarget(in,bytes)||!PeStateRender_GetConstructionPlan(in,bytes,&plan)||!current_plan(current)||memcmp(current,in+40,PLAN_BYTES))return 0;return session==PeStateBoundary_Session()&&scene==PeStateBoundary_SceneEpoch()&&assets==PeStateAssets_Generation()&&resource==PeStateResource_Epoch()&&walk(in,0,bytes,0,0);}
static int capture(void *ctx,unsigned char *out,size_t cap,size_t *used){(void)ctx;if(used)*used=0;if(!out||!used||cap<44+PLAN_BYTES||cap>RENDER_MAX_BYTES||!PeStateBoundary_AtCpuPoint())return 0;memcpy(out,"PEREND03",8);uint64_t values[4]={PeStateBoundary_Session(),PeStateBoundary_SceneEpoch(),PeStateAssets_Generation(),PeStateResource_Epoch()};for(unsigned i=0;i<4;i++){put(out+8+i*8,(uint32_t)values[i]);put(out+12+i*8,(uint32_t)(values[i]>>32));}if(!current_plan(out+40)||!walk(0,out,cap,used,0))return 0;return validate(0,out,*used);}
static int restore(void *ctx,const unsigned char *in,size_t bytes){PeWorldState*world=ctx;if(world&&world->targetMode){unsigned char current[PLAN_BYTES];if(!PeStateRender_PreflightTarget(in,bytes)||!current_plan(current)||memcmp(current,in+40,PLAN_BYTES)||!walk(in,0,bytes,0,0))return 0;}else if(!validate(ctx,in,bytes))return 0;if(!walk(in,0,bytes,0,1))return 0;if(++restoreRevision==0)restoreRevision=1;return 1;}
int PeState_RegisterRenderGraph(PeStateRegistry *r,PeWorldState *world){PeStateModule m={PE_STATE_ACTORS,3,RENDER_MAX_BYTES,capture,world,validate,restore,PeStateActor_Prepare};return PeState_Register(r,m);}

int PeStateRender_BeginConstruction(void){if(reconstructing)return 0;reconstructing=1;return 1;}
void PeStateRender_EndConstruction(void){reconstructing=0;}
int PeStateRender_InstallConstructionBindings(const PeActorConstructionPlan *plan){if(!plan||!reconstructing||!PeStateResource_IsRebuilding())return 0;for(unsigned i=0;i<14;i++){const PeActorConstruction *p=&plan->actor[i];if(!p->alive||!p->slot){memset(&configs[i],0,sizeof configs[i]);continue;}configs[i]=(Config){g_GameState.bank_slots[p->type],g_RenderScratchBufferBase[0]+(size_t)(p->slot-1)*ENTITY_ALLOCATION_BLOCK_SIZE+p->packetOffset,(uint64_t)p->generationLo|((uint64_t)p->generationHi<<32),(short)p->x,(short)p->y,(short)p->unused,(short)p->palette,(short)p->init,(int)p->full};unsigned entries;if(!owner(i,&entries))return 0;}return 1;}

int PeStateRender_CurrentCandidateValid(const unsigned char *in,size_t n){return validate(0,in,n);}
