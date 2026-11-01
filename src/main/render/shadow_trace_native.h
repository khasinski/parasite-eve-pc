/* SPDX-License-Identifier: MIT. Bounded source-packet diagnostics only. */
#ifndef PE_NATIVE_SHADOW_TRACE_H
#define PE_NATIVE_SHADOW_TRACE_H
#include <SDL3/SDL.h>
static void NativeShadowTraceSource(const RenderShadowActor *actor,const RenderShadowQuad *q,int radius,int depth,int last_flag,const GteVector *facing,const GteMatrixStorage *ground){
    static unsigned count;if(count>=32)return;++count;
    /* Copy initialized payload values, not tag links or unwritten padding. */
    int32_t xy[4]={q->xy0,q->xy1,q->xy2,q->xy3};int x[4],y[4];
    for(unsigned i=0;i<4;i++){x[i]=(int16_t)(xy[i]&65535);y[i]=(int16_t)((uint32_t)xy[i]>>16);}
    int64_t a=(int64_t)(x[1]-x[0])*(y[2]-y[0])-(int64_t)(y[1]-y[0])*(x[2]-x[0]);
    int64_t b=(int64_t)(x[3]-x[1])*(y[2]-y[1])-(int64_t)(y[3]-y[1])*(x[2]-x[1]);
    SDL_Log("[Shadow source] trace=%u actor-type=%u serial=%u slot=%d flags=%08x radius=%d depth=%d linked-eligible=%d last-gte-flag=%08x floor-authority=%s receiver=%d,%d,%d facing=%d,%d,%d page=%u clut=%u code=%02x abr=%u rgb=%u,%u,%u xy=%d,%d/%d,%d/%d,%d/%d,%d area=%lld/%lld uv=%u,%u/%u,%u/%u,%u/%u,%u",
        count,(unsigned)actor->type_id,(unsigned)actor->field_sfx_id,g_ActiveDrawSlot,(unsigned)actor->flags,radius,depth,depth>=0&&depth<4096,(unsigned)last_flag,
        actor->flags&0x04000000?"room-floor":"actor-height",ground->matrix.t[0],ground->matrix.t[1],ground->matrix.t[2],facing->x,facing->y,facing->z,
        (unsigned)q->tpage,(unsigned)q->clut,(unsigned)q->code,(q->tpage>>5)&3u,(unsigned)q->r0,(unsigned)q->g0,(unsigned)q->b0,
        x[0],y[0],x[1],y[1],x[2],y[2],x[3],y[3],(long long)a,(long long)b,
        (unsigned)q->u0,(unsigned)q->v0,(unsigned)q->u1,(unsigned)q->v1,(unsigned)q->u2,(unsigned)q->v2,(unsigned)q->u3,(unsigned)q->v3);
}
#endif
