/* SPDX-License-Identifier: MIT. Actual model silhouette on the original
 * horizontal receiver, using disjoint coverage runs, not stacked triangles.
 * Uses retained MPL2 particle projection helper without changing its notice. */
#ifndef PE_PLANAR_SHADOW_NATIVE_H
#define PE_PLANAR_SHADOW_NATIVE_H
#include "pe1/planar_shadow.h"
#include "pe1/player_entity.h"
#include "pe1/render_animation_frame.h"
#include <SDL3/SDL.h>
#include <stdint.h>
#include <string.h>
typedef struct {int x,y,z;} PeParticleWorldPoint;
#include "../fieldfx/particle_mesh/pe_particle_project.h"
extern int Psyz_GpuOriginalShadowSample(unsigned *);
enum{SHADOW_GRID=32,SHADOW_VERTICES=1024,SHADOW_FACES=512,SHADOW_RUNS=96,SHADOW_RASTER_WORK=131072};
typedef struct{int x,z;} ShadowPoint;
typedef struct{unsigned x0,x1,y0,y1;} ShadowRun;
static int shadow_style,shadow_frame_open,shadow_busy,shadow_frame_used;
static unsigned shadow_logs;
/* Keep the authored fade envelope: no minimum can brighten a faint source. */
static unsigned ShadowTint(unsigned source){unsigned tint=source/3;return tint>24?24:tint;}
void PeShadow_SetStyle(int style){shadow_style=style==1;}
int PeShadow_GetStyle(void){return shadow_style;}
void PeShadow_BeginFrame(void){shadow_frame_open=1;shadow_frame_used=0;}
void PeShadow_EndFrame(void){shadow_frame_open=0;}
int PeShadow_StateIdle(void){return !shadow_frame_open&&!shadow_busy;}
static int64_t ShadowArea(int ax,int az,int bx,int bz,int cx,int cz){return(int64_t)(bx-ax)*(cz-az)-(int64_t)(bz-az)*(cx-ax);}
static int ShadowRaster(uint32_t mask[SHADOW_GRID],ShadowPoint a,ShadowPoint b,ShadowPoint c,unsigned *work){
    int64_t area=ShadowArea(a.x,a.z,b.x,b.z,c.x,c.z);if(!area)return 1;
    int x0=a.x<b.x?a.x:b.x,x1=a.x>b.x?a.x:b.x,y0=a.z<b.z?a.z:b.z,y1=a.z>b.z?a.z:b.z;
    if(c.x<x0)x0=c.x;if(c.x>x1)x1=c.x;if(c.z<y0)y0=c.z;if(c.z>y1)y1=c.z;
    x0=x0/2;if(x0<0)x0=0;y0=y0/2;if(y0<0)y0=0;x1=(x1+1)/2;if(x1>=SHADOW_GRID)x1=SHADOW_GRID-1;y1=(y1+1)/2;if(y1>=SHADOW_GRID)y1=SHADOW_GRID-1;
    unsigned need=(unsigned)(x1-x0+1)*(unsigned)(y1-y0+1);if(need>SHADOW_RASTER_WORK-*work)return 0;*work+=need;
    for(int y=y0;y<=y1;y++)for(int x=x0;x<=x1;x++){
        int u=x*2+1,v=y*2+1;int64_t e0=ShadowArea(a.x,a.z,b.x,b.z,u,v),e1=ShadowArea(b.x,b.z,c.x,c.z,u,v),e2=ShadowArea(c.x,c.z,a.x,a.z,u,v);
        if(area>0?(e0>=0&&e1>=0&&e2>=0):(e0<=0&&e1<=0&&e2<=0))mask[y]|=UINT32_C(1)<<x;
    }return 1;
}
static int ShadowRuns(const uint32_t mask[SHADOW_GRID],ShadowRun runs[SHADOW_RUNS],unsigned *count){
    *count=0;for(unsigned y=0;y<SHADOW_GRID;y++)for(unsigned x=0;x<SHADOW_GRID;){
        if(!(mask[y]&(UINT32_C(1)<<x))){x++;continue;}unsigned start=x;while(x<SHADOW_GRID&&(mask[y]&(UINT32_C(1)<<x)))x++;
        unsigned r;for(r=0;r<*count;r++)if(runs[r].x0==start&&runs[r].x1==x&&runs[r].y1==y){runs[r].y1=y+1;break;}
        if(r==*count){if(*count==SHADOW_RUNS)return 0;runs[(*count)++]=(ShadowRun){start,x,y,y+1};}
    }return *count!=0;
}
/* Returns1 only after ALL replacement packets have been admitted. Caller has
 * not linked its original shadow yet, so failure retains its exact original. */
static int PeShadow_TryDraw(RenderShadowActor *actor,const RenderShadowQuad *original,int receiverY){
    if(!shadow_style||!shadow_frame_open||shadow_busy||shadow_frame_used||actor!=g_PlayerEntity||!(actor->flags&0x04000000)||(unsigned)g_ActiveDrawSlot>=2)return 0;
    const RenderObjectEntity*o=&actor->render_object;const RenderObjectHeader*h=o->header;
    if(!h||!h->shadow_radius||!original->r0||original->code!=0x2e||original->tpage!=0xcb||original->clut!=0x7210||!o->parts||!o->vertices||!o->matrices||!o->primitive_descriptors||!h->vertex_count||h->vertex_count>SHADOW_VERTICES||!h->part_count||h->part_count>32)return 0;
    unsigned faces=(unsigned)h->packet34_count+h->packet28_count+h->packet24_count+h->packet1c_count;
    unsigned sourceSample=0;if(!faces||faces>SHADOW_FACES||!Psyz_GpuOriginalShadowSample(&sourceSample))return 0;
    if(!D_800B0E38.ordering[g_ActiveDrawSlot]||!g_RenderAnimBufferBase[g_ActiveDrawSlot]||D_800B0E38.packets[g_ActiveDrawSlot]!=(char*)g_RenderAnimBufferBase[g_ActiveDrawSlot]||g_DrawPacketOffset<0)return 0;
    shadow_busy=1;
    ShadowPoint points[SHADOW_VERTICES];unsigned char known[SHADOW_VERTICES]={0};int minx=INT32_MAX,minz=INT32_MAX,maxx=INT32_MIN,maxz=INT32_MIN;unsigned seen=0;
    for(unsigned part=0;part<h->part_count;part++){
        const RenderObjectPart*p=&o->parts[part];if(p->vertex_start>h->vertex_count||p->vertex_count>h->vertex_count-p->vertex_start)goto reject;
        if(p->visible!=1)continue;const RenderMatrix*m=&o->matrices[part];
        for(unsigned j=0;j<p->vertex_count;j++){
            unsigned i=p->vertex_start+j;if(known[i])goto reject;const RenderVec3s*v=&o->vertices[i];int64_t world[3];
            for(unsigned r=0;r<3;r++)world[r]=(((int64_t)m->rotation[r][0]*v->x+(int64_t)m->rotation[r][1]*v->y+(int64_t)m->rotation[r][2]*v->z)>>12)+m->translation[r];
            int64_t height=(int64_t)receiverY-world[1];if(height < -16||height>1024)goto reject;if(height<0)height=0;
            /* Explicit stylized parallel light, not an inferred room light. */
            int64_t x=world[0]+height/2,z=world[2]+height/4;if(x<-32760||x>32760||z<-32760||z>32760)goto reject;
            points[i]=(ShadowPoint){(int)x,(int)z};known[i]=1;seen++;
            if(x<minx)minx=(int)x;if(x>maxx)maxx=(int)x;if(z<minz)minz=(int)z;if(z>maxz)maxz=(int)z;
        }
    }
    if(seen<3||maxx<=minx||maxz<=minz||maxx-minx>2048||maxz-minz>2048)goto reject;
    --minx;--minz;++maxx;++maxz;int spanx=maxx-minx,spanz=maxz-minz;
    for(unsigned i=0;i<h->vertex_count;i++)if(known[i]){points[i].x=(int)((int64_t)(points[i].x-minx)*(SHADOW_GRID*2)/spanx);points[i].z=(int)((int64_t)(points[i].z-minz)*(SHADOW_GRID*2)/spanz);}
    uint32_t mask[SHADOW_GRID]={0};unsigned work=0;
    for(unsigned f=0;f<faces;f++){
        const RenderPrimitiveDescriptor*d=&o->primitive_descriptors[f];int quad=f<h->packet34_count||(f>=h->packet34_count+h->packet28_count&&f<h->packet34_count+h->packet28_count+h->packet24_count);unsigned a=d->lookup_indices[0],b=d->lookup_indices[1],c=d->lookup_indices[2],e=quad?d->lookup_indices[3]:0;
        if(a>=h->vertex_count||b>=h->vertex_count||c>=h->vertex_count||(quad&&e>=h->vertex_count))goto reject;
        if(!known[a]||!known[b]||!known[c]||(quad&&!known[e]))continue;
        if(!ShadowRaster(mask,points[a],points[b],points[c],&work)||(quad&&!ShadowRaster(mask,points[b],points[e],points[c],&work)))goto reject;
    }
    ShadowRun runs[SHADOW_RUNS];unsigned count=0;if(!ShadowRuns(mask,runs,&count))goto reject;
    RenderShadowQuad pending[SHADOW_RUNS];unsigned depths[SHADOW_RUNS];memset(pending,0,count*sizeof(*pending));
    unsigned screen=Psyz_GteCtrlRead(26)&65535u;s32 ox=(s32)Psyz_GteCtrlRead(24),oy=(s32)Psyz_GteCtrlRead(25);if(!screen)goto reject;
    for(unsigned r=0;r<count;r++){
        RenderShadowQuad*q=&pending[r];q->tag.length=9;q->r0=q->g0=q->b0=(u8)ShadowTint(original->r0);
        q->code=original->code;q->tpage=original->tpage;q->clut=original->clut;q->u0=q->u1=q->u2=q->u3=31;q->v0=q->v1=q->v2=q->v3=96;
        s16 xy[8];unsigned maxDepth=0;
        for(unsigned v=0;v<4;v++){
            unsigned gx=v&1?runs[r].x1:runs[r].x0,gz=v&2?runs[r].y1:runs[r].y0;
            PeParticleWorldPoint point={minx+(int)((int64_t)spanx*gx/SHADOW_GRID),receiverY,minz+(int)((int64_t)spanz*gz/SHADOW_GRID)};unsigned depth;
            if(!PeParticle_Project(&point,(const GteMatrix*)g_EntityRenderScratch,screen,ox,oy,&xy[v*2],&xy[v*2+1],&depth))goto reject;if(depth>maxDepth)maxDepth=depth;
        }
        int64_t a=ShadowArea(xy[0],xy[1],xy[2],xy[3],xy[4],xy[5]),b=ShadowArea(xy[2],xy[3],xy[6],xy[7],xy[4],xy[5]);if(!a||!b||(a>0)!=(b>0))goto reject;
        depths[r]=maxDepth>>2;if(depths[r]>=4096)goto reject;
        q->xy0=(u16)xy[0]|((u32)(u16)xy[1]<<16);q->xy1=(u16)xy[2]|((u32)(u16)xy[3]<<16);q->xy2=(u16)xy[4]|((u32)(u16)xy[5]<<16);q->xy3=(u16)xy[6]|((u32)(u16)xy[7]<<16);
    }
    size_t offset=(unsigned)g_DrawPacketOffset,align=_Alignof(RenderShadowQuad);if((uintptr_t)g_RenderAnimBufferBase[g_ActiveDrawSlot]%align||offset>RENDER_ANIM_BUFFER_BYTES)goto reject;
    offset=(offset+align-1)&~(align-1);size_t bytes=count*sizeof(RenderShadowQuad);if(offset>RENDER_ANIM_BUFFER_BYTES||bytes>RENDER_ANIM_BUFFER_BYTES-offset)goto reject;
    RenderShadowQuad*dest=(void*)(g_RenderAnimBufferBase[g_ActiveDrawSlot]+offset);memcpy(dest,pending,bytes);
    /* No fallible operation remains after touching the arena or original OT. */
    for(unsigned r=0;r<count;r++){RenderNativeTag*entry=&D_800B0E38.ordering[g_ActiveDrawSlot][depths[r]];dest[r].tag.address=entry->address;entry->address=(uintptr_t)&dest[r];}
    g_DrawPacketOffset=(int)(offset+bytes);shadow_frame_used=1;shadow_busy=0;
    if(shadow_logs++<12)SDL_Log("[Planar player shadow] admitted-runs=%u mesh-vertices=%u faces=%u work=%u source-center=%04x receiver=original-room-floor light=stylized-parallel arena-bytes=%zu source-blob=not-linked visual=unobserved",count,seen,faces,work,sourceSample,bytes);
    return 1;
reject:
    shadow_busy=0;if(shadow_logs++<12)SDL_Log("[Planar player shadow] original-fallback=1 no-original-suppression=1");return 0;
}
#endif
