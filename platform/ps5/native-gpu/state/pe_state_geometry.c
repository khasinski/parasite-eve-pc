#include "pe_state_checkpoint.h"
#include "pe_state_boundary.h"
#include "pe_state_assets.h"
#include "pe1/geom_state.h"
#include "pe1/render_camera.h"
#include <string.h>
#define GEOMETRY_MAX (4u*1024u*1024u)
_Static_assert(PE1_OFFSETOF(GeomEntry,u30)==0x30,"only source-defined pointer-free mesh prefix");
static void put(unsigned char *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(v>>(8*i));}
static uint32_t get(const unsigned char *p){return(uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static unsigned half(const unsigned char *p){return p[0]|(unsigned)p[1]<<8;}
static int canonical(uint32_t v,unsigned k){return k==1?v<=255:k==2?v<=65535:k==3?((int32_t)v>=-32768&&(int32_t)v<=32767):k==4?((int32_t)v>=-128&&(int32_t)v<=127):1;}
static int layout(size_t *disk,size_t *native,unsigned *count,unsigned *views){GeomState *g=g_GameState.geometry_state;if(!g)return 0;uint32_t n;memcpy(&n,g,4);*disk=g_GameState.geometry_disk_bytes;*native=n;*count=g->entry_count06;*views=half((const unsigned char *)g+10);if(*disk<sizeof *g||*disk>GEOMETRY_MAX||*native<*disk||*native>GEOMETRY_MAX||g->entry_offset<0||(size_t)g->entry_offset<*disk||(size_t)g->entry_offset>*native||*count>(*native-(size_t)g->entry_offset)/sizeof(GeomEntry)||!*views||g->entry_offset_1C<(int)sizeof *g||(size_t)g->entry_offset_1C>*disk||*views>(*disk-(size_t)g->entry_offset_1C)/sizeof(CameraViewport)||g->ctrl_offset<0||(size_t)g->ctrl_offset>*disk||g->entry_count>(*disk-(size_t)g->ctrl_offset)/sizeof(GeomCtrlEntry))return 0;return 1;}
static int camera(const unsigned char *in,unsigned char *out,int apply){size_t at=0;
#define FIELD(value,kind) do{uint32_t v=out?(uint32_t)(value):get(in+at);if(!canonical(v,kind))return 0;if(out)put(out+at,v);else if(apply)(value)=(__typeof__(value))v;at+=4;}while(0)
FIELD(g_GeomScroll.state.flags,0);
FIELD(g_GeomScroll.state.position.x,3);
FIELD(g_GeomScroll.state.position.y,3);
FIELD(g_GeomScroll.state.position.savedX,2);
FIELD(g_GeomScroll.state.position.savedY,2);
FIELD(g_GeomScroll.state.position.screenOffsetX,2);
FIELD(g_GeomScroll.state.position.screenOffsetY,2);
FIELD(g_GeomScroll.state.position.startX,2);
FIELD(g_GeomScroll.state.position.startY,2);
FIELD(g_GeomScroll.state.position.targetX,2);
FIELD(g_GeomScroll.state.position.targetY,2);
FIELD(g_GeomScroll.state.position.elapsed,2);
FIELD(g_GeomScroll.state.position.duration,2);
FIELD(g_GeomScroll.state.position.boundsMinX,3);
FIELD(g_GeomScroll.state.position.boundsMaxX,3);
FIELD(g_GeomScroll.state.position.boundsMinY,3);
FIELD(g_GeomScroll.state.position.boundsMaxY,3);
FIELD(g_GeomScroll.state.position.cameraOffsetX,3);
FIELD(g_GeomScroll.state.position.cameraOffsetY,3);
FIELD(g_GeomScroll.state.position.meshTintLevel,4);
FIELD(g_GeomScroll.state.position.groupSelect,1);
FIELD(g_GeomScroll.state.position.focusHeight,3);
FIELD(g_GeomScroll.state.position.baseAngleDigital,2);
FIELD(g_GeomScroll.state.position.baseAngleAnalog,2);
FIELD(g_GeomScroll.state.position.otZ,1);
FIELD(g_GeomScroll.state.position.originX,3);
FIELD(g_GeomScroll.state.position.originY,3);
FIELD(g_GeomScroll.state.position.tint.targetR,3);
FIELD(g_GeomScroll.state.position.tint.targetG,3);
FIELD(g_GeomScroll.state.position.tint.targetB,3);
FIELD(g_GeomScroll.state.position.tint.fadeMode,1);
FIELD(g_GeomScroll.state.position.tint.blendMode,1);
FIELD(g_GeomScroll.state.position.tint.startR,3);
FIELD(g_GeomScroll.state.position.tint.startG,3);
FIELD(g_GeomScroll.state.position.tint.startB,3);
FIELD(g_GeomScroll.state.position.tint.duration,2);
FIELD(g_GeomScroll.state.position.tint.frame,2);
FIELD(g_GeomScroll.state.position.tint.fadeFrameCount,1);
FIELD(g_GeomScroll.state.position.tint.fadeFrameCounter,1);
FIELD(g_GeomScroll.state.position.tint.tiles[0].r,1);
FIELD(g_GeomScroll.state.position.tint.tiles[0].g,1);
FIELD(g_GeomScroll.state.position.tint.tiles[0].b,1);
FIELD(g_GeomScroll.state.position.tint.tiles[0].command,1);
FIELD(g_GeomScroll.state.position.tint.tiles[0].x,3);
FIELD(g_GeomScroll.state.position.tint.tiles[0].y,3);
FIELD(g_GeomScroll.state.position.tint.tiles[0].width,3);
FIELD(g_GeomScroll.state.position.tint.tiles[0].height,3);
FIELD(g_GeomScroll.state.position.tint.modes[0].command,0);
FIELD(g_GeomScroll.state.position.tint.tiles[1].r,1);
FIELD(g_GeomScroll.state.position.tint.tiles[1].g,1);
FIELD(g_GeomScroll.state.position.tint.tiles[1].b,1);
FIELD(g_GeomScroll.state.position.tint.tiles[1].command,1);
FIELD(g_GeomScroll.state.position.tint.tiles[1].x,3);
FIELD(g_GeomScroll.state.position.tint.tiles[1].y,3);
FIELD(g_GeomScroll.state.position.tint.tiles[1].width,3);
FIELD(g_GeomScroll.state.position.tint.tiles[1].height,3);
FIELD(g_GeomScroll.state.position.tint.modes[1].command,0);
FIELD(g_GeomScroll.state.position.lightTint.r,1);
FIELD(g_GeomScroll.state.position.lightTint.g,1);
FIELD(g_GeomScroll.state.position.lightTint.b,1);
FIELD(g_GeomScroll.state.position.cameraBasis.m[0][0],3);
FIELD(g_GeomScroll.state.position.cameraBasis.m[0][1],3);
FIELD(g_GeomScroll.state.position.cameraBasis.m[0][2],3);
FIELD(g_GeomScroll.state.position.cameraBasis.m[1][0],3);
FIELD(g_GeomScroll.state.position.cameraBasis.m[1][1],3);
FIELD(g_GeomScroll.state.position.cameraBasis.m[1][2],3);
FIELD(g_GeomScroll.state.position.cameraBasis.m[2][0],3);
FIELD(g_GeomScroll.state.position.cameraBasis.m[2][1],3);
FIELD(g_GeomScroll.state.position.cameraBasis.m[2][2],3);
FIELD(g_GeomScroll.state.position.cameraBasis.t[0],0);
FIELD(g_GeomScroll.state.position.cameraBasis.t[1],0);
FIELD(g_GeomScroll.state.position.cameraBasis.t[2],0);
FIELD(g_EntityRenderScratch[0],0);
FIELD(g_EntityRenderScratch[1],0);
FIELD(g_EntityRenderScratch[2],0);
FIELD(g_EntityRenderScratch[3],0);
FIELD(g_EntityRenderScratch[4],0);
FIELD(g_EntityRenderScratch[5],0);
FIELD(g_EntityRenderScratch[6],0);
FIELD(g_EntityRenderScratch[7],0);
FIELD(g_EntityRenderScratch[8],0);
#undef FIELD
return at==324;}
static int validate(void *ctx,const unsigned char *in,size_t bytes){(void)ctx;size_t disk,native;unsigned count,views;if(!in||bytes<40+324||bytes>GEOMETRY_MAX||memcmp(in,"PEGEOM01",8)||PeStateCheckpoint_Boundary()||!layout(&disk,&native,&count,&views)||get(in+8)!=(uint32_t)PeStateBoundary_SceneEpoch()||get(in+12)!=(uint32_t)(PeStateBoundary_SceneEpoch()>>32)||get(in+16)!=(uint32_t)PeStateAssets_Generation()||get(in+20)!=(uint32_t)(PeStateAssets_Generation()>>32)||get(in+24)!=disk||get(in+28)!=native||get(in+32)!=count||get(in+36)!=324||bytes!=40+disk+(size_t)count*52+324)return 0;GeomState saved;memcpy(&saved,in+40,sizeof saved);GeomState *g=g_GameState.geometry_state;if(saved.entry_count!=g->entry_count||saved.entry_count06!=g->entry_count06||saved.ctrl_offset!=g->ctrl_offset||saved.entry_offset!=g->entry_offset||saved.entry_offset_1C!=g->entry_offset_1C||get(in+40)!=native||half(in+50)!=views)return 0;
/* Same bounds as actual Scene_LoadRoom, before any prefix bytes become live. */
for(unsigned i=0;i<saved.entry_count;i++){size_t at=(size_t)saved.ctrl_offset+i*sizeof(GeomCtrlEntry);GeomCtrlEntry c;memcpy(&c,in+40+at,sizeof c);int64_t offset=(int64_t)at+c.slot_offset;uint32_t slots=c.head.packed>>8;if(!slots)slots=1;if(offset<0||(uint64_t)offset>disk||slots>(disk-(size_t)offset)/sizeof(GeomAnimationSlot))return 0;for(unsigned j=0;j<slots;j++)if(in[40+(size_t)offset+j*sizeof(GeomAnimationSlot)]>=count)return 0;}
GeomEntry *entries=(GeomEntry *)((unsigned char *)g+g->entry_offset);size_t at=40+disk;for(unsigned i=0;i<count;i++){GeomEntry e={0};memcpy(&e,in+at,48);if(e.prim_count!=entries[i].prim_count||e.group>=views||(e.prim_count&&(e.u28.pos_ptr!=entries[i].u28.pos_ptr||e.u2C.uv_ptr!=entries[i].u2C.uv_ptr)))return 0;at+=52;}return camera(in+at,0,0);}
static int capture(void *ctx,unsigned char *out,size_t cap,size_t *used){(void)ctx;if(used)*used=0;size_t disk,native;unsigned count,views;if(!out||!used||PeStateCheckpoint_Boundary()||!layout(&disk,&native,&count,&views)||g_GeomScroll.state.position.matrixWords!=g_EntityRenderScratch||g_GeomScroll.state.position.projectionDistance!=(s32 *)(void *)&g_EntityRenderScratch[8])return 0;size_t total=40+disk+(size_t)count*52+324;if(total>cap||total>GEOMETRY_MAX)return 0;memcpy(out,"PEGEOM01",8);put(out+8,(uint32_t)PeStateBoundary_SceneEpoch());put(out+12,(uint32_t)(PeStateBoundary_SceneEpoch()>>32));put(out+16,(uint32_t)PeStateAssets_Generation());put(out+20,(uint32_t)(PeStateAssets_Generation()>>32));put(out+24,disk);put(out+28,native);put(out+32,count);put(out+36,324);GeomState *g=g_GameState.geometry_state;memcpy(out+40,g,disk);GeomEntry *entries=(GeomEntry *)((unsigned char *)g+g->entry_offset);size_t at=40+disk;for(unsigned i=0;i<count;i++){memcpy(out+at,&entries[i],48);put(out+at+48,entries[i].prim_count?0:(uint32_t)(u16)entries[i].u30.by.min_y|((uint32_t)(u16)entries[i].u30.by.max_y<<16));at+=52;}if(!camera(0,out+at,0)||!validate(0,out,total))return 0;*used=total;return 1;}
static int restore(void *ctx,const unsigned char *in,size_t bytes){if(!validate(ctx,in,bytes))return 0;size_t disk=get(in+24);unsigned count=get(in+32);GeomState *g=g_GameState.geometry_state;memcpy(g,in+40,disk);GeomEntry *entries=(GeomEntry *)((unsigned char *)g+g->entry_offset);size_t at=40+disk;for(unsigned i=0;i<count;i++){memcpy(&entries[i],in+at,48);if(!entries[i].prim_count){uint32_t value=get(in+at+48);entries[i].u30.by.min_y=(s16)value;entries[i].u30.by.max_y=(s16)(value>>16);}at+=52;}if(!camera(in+at,0,1))return 0;g_GeomScroll.state.position.matrixWords=g_EntityRenderScratch;g_GeomScroll.state.position.projectionDistance=(s32 *)(void *)&g_EntityRenderScratch[8];return 1;}
int PeState_RegisterGeometry(PeStateRegistry *r){PeStateModule m={PE_STATE_GEOMETRY,1,GEOMETRY_MAX,capture,0,validate,restore,0};return PeState_Register(r,m);}
