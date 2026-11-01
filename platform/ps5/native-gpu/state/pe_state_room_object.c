#include "pe_state_room_object.h"
#include "pe_state_assets.h"
#include "pe_state_boundary.h"
#include "pe_state_checkpoint.h"
#include "pe1/game_state.h"
#include "pe1/player_entity.h"
#include "pe1/entity_allocation.h"
#include "pe1/render_setup.h"
#include <string.h>
#define ROOM_MAX 65536u
static struct{RenderObjectHeader *model;unsigned char *packet;uint64_t scene,generation;short x,y,unused,palette,init;int setup;}config;
static const void *refs[256];static unsigned refCount;static int rebuilding;
void PeStateRoom_Configured(RenderObjectEntity *o,RenderObjectHeader *m,unsigned char *p,short x,short y,short unused,short palette,short init,int setup){if(o!=&g_GameState.scene_object||rebuilding)return;config.model=m;config.packet=p;config.x=x;config.y=y;config.unused=unused;config.palette=palette;config.init=init;config.setup=setup;config.scene=PeStateBoundary_SceneEpoch();if(++config.generation==0)++config.generation;refCount=0;}
static uint32_t get(const unsigned char *p){return(uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void put(unsigned char *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(v>>(8*i));}
static unsigned id(const void *p){if(!p)return 0;for(unsigned i=0;i<refCount;i++)if(refs[i]==p)return i+1;if(refCount==256)return UINT32_MAX;refs[refCount++]=p;return refCount;}
static unsigned actor(const FieldActor *a){if(!a)return 0;for(unsigned i=0;i<14;i++)if(a==&g_EntityPool[i])return i+1;return UINT32_MAX;}
static unsigned source(const RenderObjectEntity *o){if(!o)return 0;for(unsigned i=0;i<14;i++)if(o==&g_EntityPool[i].render_object)return i+1;return UINT32_MAX;}
static int canonical(uint32_t v,unsigned k){return k==1?v<=255:k==2?v<=65535:k==3?((int32_t)v>=-32768&&(int32_t)v<=32767):k==4?((int32_t)v>=-128&&(int32_t)v<=127):1;}
static int walk(const unsigned char *in,unsigned char *out,size_t bytes,size_t *used,int apply){size_t at=40;RenderObjectEntity *o=&g_GameState.scene_object;unsigned present=o->header!=0;
#define WORD(v) do{if(at>bytes||bytes-at<4)return 0;if(out)put(out+at,(uint32_t)(v));else if(get(in+at)!=(uint32_t)(v))return 0;at+=4;}while(0)
#define FIELD(value,kind) do{if(at>bytes||bytes-at<4)return 0;uint32_t v=out?(uint32_t)(value):get(in+at);if(!canonical(v,kind))return 0;if(out)put(out+at,v);else if(apply)(value)=(__typeof__(value))v;at+=4;}while(0)
WORD(present);if(present){if(config.scene!=PeStateBoundary_SceneEpoch()||config.model!=g_GameState.scene_object_model||o->header!=config.model||!config.packet||!o->parts||!o->matrices||!o->bounds_vertices||!o->projection_origin)return 0;unsigned entries=0;for(unsigned j=0;j<o->header->part_count;j++)if(o->parts[j].visible==1&&o->bounds_vertices[j*2+1].pad>=0)entries++;if(entries&&!o->animation_entries)return 0;WORD(config.generation);WORD(config.generation>>32);WORD(o->header->part_count);WORD(entries);
unsigned active=0;if(o->active_matrix){if(o->active_matrix==&o->model_matrix)active=1;else{uintptr_t p=(uintptr_t)o->active_matrix,b=(uintptr_t)o->matrices;if(p<b||p-b>=(size_t)o->header->part_count*sizeof(RenderMatrix)||(p-b)%sizeof(RenderMatrix))return 0;active=2+(unsigned)((p-b)/sizeof(RenderMatrix));}}uint32_t bindings[4]={active,source(o->animation_source),actor(o->reflection_owner),out?id(o->animation_data):0};for(unsigned j=0;j<4;j++){if(at>bytes||bytes-at<4||bindings[j]==UINT32_MAX)return 0;if(out)put(out+at,bindings[j]);else bindings[j]=get(in+at);at+=4;}if(bindings[0]>1+o->header->part_count||bindings[1]>14||bindings[2]>14||bindings[3]>refCount)return 0;
if(!out){size_t p=at+12;unsigned expected=0;if(p>bytes||(size_t)o->header->part_count*8>bytes-p)return 0;for(unsigned j=0;j<o->header->part_count;j++)if(get(in+p+j*8)==1&&o->bounds_vertices[j*2+1].pad>=0)expected++;if(expected!=entries)return 0;}
FIELD(o->header->shadow_radius,2);FIELD(o->header->scale,2);FIELD(o->header->visible_part_count,2);for(unsigned j=0;j<o->header->part_count;j++){FIELD(o->parts[j].visible,1);FIELD(o->parts[j].translation_z,0);}
if(apply&&!out){u16 shadow=o->header->shadow_radius,scale=o->header->scale,visible=o->header->visible_part_count;s8 *texture=0;rebuilding=1;int ok=Render_SetupEntityPrims(o,config.model,config.packet,config.x,config.y,config.unused,config.palette,config.init,&texture,config.setup);rebuilding=0;if(!ok)return 0;o->header->shadow_radius=shadow;o->header->scale=scale;o->header->visible_part_count=visible;}
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

if(apply&&!out){o->active_matrix=bindings[0]==0?0:bindings[0]==1?&o->model_matrix:&o->matrices[bindings[0]-2];o->animation_source=bindings[1]?&g_EntityPool[bindings[1]-1].render_object:0;o->reflection_owner=bindings[2]?&g_EntityPool[bindings[2]-1]:0;o->animation_data=bindings[3]?(void *)refs[bindings[3]-1]:0;}}
#undef WORD
#undef FIELD
if(used)*used=at;return out?1:at==bytes;}
static int validate(void *ctx,const unsigned char *in,size_t n){(void)ctx;if(!in||n<44||n>ROOM_MAX||memcmp(in,"PEROOM01",8)||PeStateCheckpoint_Boundary())return 0;uint64_t values[4]={PeStateBoundary_Session(),PeStateBoundary_SceneEpoch(),PeStateAssets_Generation(),config.generation};for(unsigned i=0;i<4;i++)if(get(in+8+i*8)!=(uint32_t)values[i]||get(in+12+i*8)!=(uint32_t)(values[i]>>32))return 0;return walk(in,0,n,0,0);}
static int capture(void *ctx,unsigned char *out,size_t cap,size_t *used){(void)ctx;if(!out||!used||cap<44||cap>ROOM_MAX||PeStateCheckpoint_Boundary())return 0;memcpy(out,"PEROOM01",8);uint64_t values[4]={PeStateBoundary_Session(),PeStateBoundary_SceneEpoch(),PeStateAssets_Generation(),config.generation};for(unsigned i=0;i<4;i++){put(out+8+i*8,(uint32_t)values[i]);put(out+12+i*8,(uint32_t)(values[i]>>32));}return walk(0,out,cap,used,0)&&validate(0,out,*used);}
static int restore(void *ctx,const unsigned char *in,size_t n){return validate(ctx,in,n)&&walk(in,0,n,0,1);}
int PeState_RegisterRoomObject(PeStateRegistry *r){PeStateModule m={PE_STATE_EVENTS,1,ROOM_MAX,capture,0,validate,restore,0};return PeState_Register(r,m);}
