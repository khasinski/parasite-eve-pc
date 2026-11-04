/* MPL-2.0. Original bounded, render-only point-light contribution.
 * Included after particle renderer ownership globals. No original simulation
 * or lighting cache is modified: temporary native RGB words are restored as
 * soon as the actual synchronous PsyZ enqueue has copied them. */
#ifndef PE_PARTICLE_LIGHT_NATIVE_H
#define PE_PARTICLE_LIGHT_NATIVE_H
#include "pe1/field_actor.h"
extern FieldActor *g_FieldActorListHead;
enum { PARTICLE_LIGHT_CAP=8,PARTICLE_LIGHT_MODEL_CAP=16,PARTICLE_LIGHT_COLOUR_CAP=8192,PARTICLE_LIGHT_ACTOR_SCAN_CAP=64 };
extern unsigned long long Psyz_GpuAuthoredFlareGeneration(void);
typedef struct { int x,y,z;unsigned r,g,b,radius;unsigned long long source_generation; } ParticleWorldLight;
typedef struct { u32 *word;u32 original; } ParticleColourRestore;
static ParticleWorldLight particle_lights[PARTICLE_LIGHT_CAP];
static ParticleColourRestore particle_light_restore[PARTICLE_LIGHT_COLOUR_CAP];
static unsigned particle_light_count,particle_light_restore_count,particle_light_logs,particle_light_source_logs;
static int particle_lighting,particle_light_frame_open,particle_light_applied,particle_light_fault;
void FieldFx_SetParticleLighting(int mode){mode=!!mode;if(mode!=particle_lighting)SDL_Log("[Particle lights] requested=%d scope=source-known-sparks/flame model-colours=distance-falloff original-particle-mode-disables=1",mode);particle_lighting=mode;}
int FieldFx_GetParticleLighting(void){return particle_lighting;}
int FieldFx_ParticleLightStateIdle(void){return !particle_light_frame_open&&!particle_light_count&&!particle_light_restore_count&&!particle_light_applied&&!particle_light_fault;}
static void FieldFx_ParticleLightsBegin(void){
    if(particle_light_restore_count||particle_light_applied){particle_light_fault=1;SDL_Log("[Particle lights] pending colour restoration at frame begin; disabled until restart");return;}
    particle_light_count=0;particle_light_frame_open=1;
}
void FieldFx_RestoreParticleLights(void){
    if(particle_light_fault)return;
    for(unsigned i=0;i<particle_light_restore_count;i++)*particle_light_restore[i].word=particle_light_restore[i].original;
    particle_light_restore_count=0;particle_light_applied=0;
}
static void FieldFx_ParticleLightsEnd(void){
    FieldFx_RestoreParticleLights();particle_light_count=0;particle_light_frame_open=0;
}
static void FieldFx_EmitParticleLight(int x,int y,int z,unsigned r,unsigned g,unsigned b,unsigned radius){
    if(!particle_lighting||!particle_enabled||!particle_light_frame_open||!particle_frame_open||particle_slot!=g_ActiveDrawSlot||particle_light_fault||particle_light_applied||particle_light_count==PARTICLE_LIGHT_CAP||!radius||radius>512||!(r|g|b))return;
    if(r>255)r=255;if(g>255)g=255;if(b>255)b=255;
    particle_lights[particle_light_count++]=(ParticleWorldLight){x,y,z,r,g,b,radius};
    if(particle_light_source_logs<8){particle_light_source_logs++;SDL_Log("[Particle lights source] world=(%d,%d,%d) rgb=(%u,%u,%u) radius=%u source-known-render-callback=1 no-simulation-change=1",x,y,z,r,g,b,radius);}
}
static void FieldFx_LightColour(u32 *word,unsigned r,unsigned g,unsigned b){
    if(particle_light_restore_count==PARTICLE_LIGHT_COLOUR_CAP)return;
    u32 base=*word;unsigned red=base&255,green=(base>>8)&255,blue=(base>>16)&255;
    /* A bounded RGB gain preserves authored black/tint/fade and original room
     * lighting. Distance/RGB comes from the actual emitter, not the screen. */
    red+=red*r/128;green+=green*g/128;blue+=blue*b/128;
    if(red>255)red=255;if(green>255)green=255;if(blue>255)blue=255;
    u32 lit=(base&0xff000000u)|red|(green<<8)|(blue<<16);
    if(lit==base)return;
    particle_light_restore[particle_light_restore_count++]=(ParticleColourRestore){word,base};*word=lit;
}
void FieldFx_ApplyParticleLights(void){
    if(!particle_lighting||!particle_enabled||!particle_light_frame_open||!particle_frame_open||particle_slot!=g_ActiveDrawSlot||particle_light_fault||particle_light_applied||!particle_light_count||(unsigned)g_ActiveDrawSlot>=2)return;
    particle_light_applied=1;
    struct { uintptr_t first,last; } spans[PARTICLE_LIGHT_MODEL_CAP];unsigned model_count=0,visited=0;
    FieldActor *actor=g_FieldActorListHead;
    for(;actor&&visited<PARTICLE_LIGHT_ACTOR_SCAN_CAP;actor=actor->next,visited++){
        RenderObjectEntity *o=&actor->render_object;RenderObjectHeader *h=o->header;
        if(model_count==PARTICLE_LIGHT_MODEL_CAP)break;
        /* Match the original normal actor draw path. No UI/background/floor
         * sprite, actor-owned shadow, morph-only or hidden model conversion. */
        if((actor->flags&0x20000040u)||!h||!o->draw_count||o->variant_visible!=1||!o->primitive_buffer)continue;
        if(h->packet34_count>256||h->packet28_count>256||h->packet24_count>256||h->packet1c_count>256)continue;
        unsigned words=4u*h->packet34_count+3u*h->packet28_count+4u*h->packet24_count+3u*h->packet1c_count;
        if(words>PARTICLE_LIGHT_COLOUR_CAP-particle_light_restore_count)continue;
        size_t bytes=2u*((size_t)h->packet34_count*sizeof(RenderPacket34)+(size_t)h->packet28_count*sizeof(RenderPacket28)+(size_t)h->packet24_count*sizeof(RenderPacket24)+(size_t)h->packet1c_count*sizeof(RenderPacket1C));
        uintptr_t first=(uintptr_t)o->primitive_buffer,last=first+bytes;int overlaps=last<first;
        for(unsigned i=0;i<model_count;i++)if(first<spans[i].last&&spans[i].first<last)overlaps=1;
        if(overlaps)continue;
        unsigned r=0,g=0,b=0;int ax=actor->pos_x>>16,ay=actor->pos_y>>16,az=actor->pos_z>>16;
        for(unsigned i=0;i<particle_light_count;i++){
            ParticleWorldLight *l=&particle_lights[i];if(l->source_generation&&l->source_generation!=Psyz_GpuAuthoredFlareGeneration())continue;int64_t dx=(int64_t)ax-l->x,dy=(int64_t)ay-l->y,dz=(int64_t)az-l->z;
            uint64_t distance=(uint64_t)(dx*dx+dy*dy+dz*dz),radius=(uint64_t)l->radius*l->radius;
            if(distance>=radius)continue;
            unsigned weight=(unsigned)((radius-distance)*24/radius);
            r+=l->r*weight/255;g+=l->g*weight/255;b+=l->b*weight/255;
        }
        if(r>48)r=48;if(g>48)g=48;if(b>48)b=48;if(!(r|g|b))continue;
        spans[model_count].first=first;spans[model_count++].last=last;
        RenderPacket34 *q=(RenderPacket34*)o->primitive_buffer;
        for(unsigned i=0;i<h->packet34_count;i++,q+=2){RenderPacket34 *p=q+g_ActiveDrawSlot;if(!p->tag||p->length!=12||(p->values0.bytes.command&~3u)!=0x3cu)continue;FieldFx_LightColour(&p->values0.value,r,g,b);FieldFx_LightColour(&p->value1,r,g,b);FieldFx_LightColour(&p->value2,r,g,b);FieldFx_LightColour(&p->value3,r,g,b);}
        RenderPacket28 *t=(RenderPacket28*)q;
        for(unsigned i=0;i<h->packet28_count;i++,t+=2){RenderPacket28 *p=t+g_ActiveDrawSlot;if(!p->tag||p->length!=9||(p->values0.bytes.command&~3u)!=0x34u)continue;FieldFx_LightColour(&p->values0.value,r,g,b);FieldFx_LightColour(&p->value1,r,g,b);FieldFx_LightColour(&p->value2,r,g,b);}
        RenderPacket24 *uq=(RenderPacket24*)t;
        for(unsigned i=0;i<h->packet24_count;i++,uq+=2){RenderPacket24 *p=uq+g_ActiveDrawSlot;if(!p->tag||p->length!=8||(p->values0.bytes.command&~3u)!=0x38u)continue;FieldFx_LightColour(&p->values0.value,r,g,b);FieldFx_LightColour(&p->value1,r,g,b);FieldFx_LightColour(&p->value2,r,g,b);FieldFx_LightColour(&p->value3,r,g,b);}
        RenderPacket1C *ut=(RenderPacket1C*)uq;
        for(unsigned i=0;i<h->packet1c_count;i++,ut+=2){RenderPacket1C *p=ut+g_ActiveDrawSlot;if(!p->tag||p->length!=6||(p->values0.bytes.command&~3u)!=0x30u)continue;FieldFx_LightColour(&p->values0.value,r,g,b);FieldFx_LightColour(&p->value1,r,g,b);FieldFx_LightColour(&p->value2,r,g,b);}
    }
    if(particle_light_logs<16&&particle_light_restore_count){particle_light_logs++;SDL_Log("[Particle lights] emitter-count=%u live-models=%u temporary-colour-words=%u same-frame=1 normals=none original-colours-restored-after-enqueue=1 visible-pixels=human-unverified",particle_light_count,model_count,particle_light_restore_count);}
}
#endif
