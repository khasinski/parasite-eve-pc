/* Original project optional render-only tetrahedra. Original sprite stays. */
#include "pe1/particle_augment.h"
#include "pe1/game_state.h"
#include "pe1/geom_state.h"
#include "pe1/gte.h"
#include "pe1/render_matrix_slot.h"
#include "pe1/render_object.h"
#include "pe1/render_animation_frame.h"
#include "pe1/render_buffers.h"
#include "pe1/render_packets.h"
#include "pe1/field_sprite_params.h"
#include "pe1/field_textured_strip.h"
#include "pe1/psyq_gpu.h"
#include "pe_battle_particle_mesh.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <SDL3/SDL_log.h>
extern int g_ActiveDrawSlot;
unsigned PeBattleParticle_FromFallingSprite(int,const RenderBouncingSprite*,unsigned,unsigned,PeParticleFace[4]);
enum{PARTICLE_TRIANGLE_BUDGET=64,PARTICLE_GROUP_BUDGET=32,PARTICLE_ANCHOR_BUDGET=256,PARTICLE_NO_GROUP=0xffffffffu};
typedef struct{RenderNativeTag tag;u8 r,g,b,code;s16 x0,y0,x1,y1,x2,y2;}ParticleTriangle;
PE1_STATIC_ASSERT(sizeof(ParticleTriangle)==sizeof(RenderNativeTag)+16,particle_triangle_native_size);
typedef struct{ParticleTriangle packet;unsigned depth,group;}QueuedParticleTriangle;
/* Zero-length native OT entries are legal link-only nodes. Stable private
 * anchors let us suppress one owned packet without searching/changing other
 * nodes, even after unrelated writers prepend to that bucket. */
typedef struct{RenderNativeTag tag;RenderNativeTag *original;uintptr_t original_next;unsigned group;}ParticleOriginalAnchor;
typedef struct{int complete,admitted;}ParticleOriginalGroup;
static ParticleOriginalAnchor particle_anchors[2][PARTICLE_ANCHOR_BUDGET];
static unsigned particle_anchor_count[2],particle_group_count,particle_last_group=PARTICLE_NO_GROUP;
static ParticleOriginalGroup particle_groups[PARTICLE_GROUP_BUDGET];
static int particle_capture_active;
static QueuedParticleTriangle particle_queue[PARTICLE_TRIANGLE_BUDGET];
static unsigned particle_count;
static int particle_enabled,particle_frame_open,particle_slot;
enum { EMBER_GLOW_QUAD_BUDGET=8 };
enum { GLOW_EMBER=0,GLOW_GROUND=1,GLOW_CROSS=2 };
typedef struct { FieldStripPacket packet; unsigned depth,kind; unsigned long long source_generation; } EmberGlow;
static EmberGlow ember_queue[EMBER_GLOW_QUAD_BUDGET];
static unsigned ember_count,ember_log_count;
static struct { int active,x,y,z;unsigned age; } ember_capture;
#include "pe_particle_light_native.h"
static struct {int active,x,y,z;unsigned age,joint,program;} cross_capture;
static unsigned cross_log_count;
static void FieldFx_CaptureCrossFlare(RenderNativeTag *entry,const RenderNativeTag *primitive);
extern int Psyz_GpuAuthoredFlareTint(unsigned page,unsigned clut,unsigned u,unsigned v,unsigned width,unsigned height,unsigned out[3]);
extern int Psyz_GpuCrossFlareSource(unsigned page,unsigned clut,unsigned u,unsigned v,unsigned width,unsigned height);
void FieldFx_BeginCrossFlareGlow(int x,int y,int z,unsigned age,unsigned joint){cross_capture.active=particle_frame_open&&particle_slot==g_ActiveDrawSlot&&age<35&&(joint==9||joint==20);cross_capture.x=x;cross_capture.y=y;cross_capture.z=z;cross_capture.age=age;cross_capture.joint=joint;cross_capture.program=117;}
void FieldFx_EndCrossFlareGlow(void){cross_capture.active=0;}
void FieldFx_BeginAuthoredFlareGlow(unsigned program,int x,int y,int z,unsigned age,unsigned joint){
 cross_capture.active=particle_frame_open&&particle_slot==g_ActiveDrawSlot&&((program==115&&joint==0)||(program==118&&(joint==8||joint==16)));
 cross_capture.x=x;cross_capture.y=y;cross_capture.z=z;cross_capture.age=age;cross_capture.joint=joint;cross_capture.program=program;
}
void FieldFx_EndAuthoredFlareGlow(void){cross_capture.active=0;}
static struct {int active,x,y,z;} ground_capture;
static void FieldFx_CaptureGroundGlow(const RenderNativeTag *primitive);
extern int Psyz_GpuParticleGlowSource(unsigned page,unsigned clut,unsigned u,unsigned v,unsigned width,unsigned height);
void FieldFx_BeginBurstGroundGlow(int x,int y,int z){ground_capture.active=particle_enabled==PE_PARTICLE_HYBRID&&particle_frame_open&&particle_slot==g_ActiveDrawSlot;ground_capture.x=x;ground_capture.y=y;ground_capture.z=z;}
void FieldFx_EndBurstGroundGlow(void){ground_capture.active=0;}
static void FieldFx_CaptureEmberGlow(const RenderNativeTag *primitive);
static void FieldFx_FlushEmberGlow(void);
void FieldFx_BeginEmberGlow(int x,int y,int z,unsigned age){ember_capture.active=particle_enabled==PE_PARTICLE_HYBRID&&particle_frame_open&&particle_slot==g_ActiveDrawSlot&&age<16;ember_capture.x=x;ember_capture.y=y;ember_capture.z=z;ember_capture.age=age;}
void FieldFx_EndEmberGlow(void){ember_capture.active=0;}
static unsigned particle_log_count,particle_source_log_count;
static int particle_log_initialized;
int FieldFx_GetParticleAugment(void){return particle_enabled;}
void FieldFx_SetParticleAugment(int mode){if(mode<0||mode>2)mode=0;if(!particle_log_initialized||mode!=particle_enabled){particle_log_initialized=1;particle_log_count=0;particle_source_log_count=0;ember_log_count=0;cross_log_count=0;SDL_Log("[3D falling debris] mode=%d (0Original1Hybrid2Geometry-only) render-only\n",mode);}particle_enabled=mode;}
void FieldFx_ParticleBeginFrame(void){cross_capture.active=0;ground_capture.active=0;FieldFx_ParticleLightsBegin();ember_count=0;ember_capture.active=0;particle_count=0;particle_frame_open=1;particle_slot=g_ActiveDrawSlot;particle_group_count=0;particle_capture_active=0;particle_last_group=PARTICLE_NO_GROUP;if((unsigned)particle_slot<2)particle_anchor_count[particle_slot]=0;}
void FieldFx_ParticleEndFrame(void){cross_capture.active=0;ground_capture.active=0;FieldFx_ParticleLightsEnd();ember_count=0;ember_capture.active=0;particle_count=0;particle_frame_open=0;particle_capture_active=0;particle_last_group=PARTICLE_NO_GROUP;particle_group_count=0;}
void FieldFx_BeginFallingOriginal(void){
    particle_capture_active=0;particle_last_group=PARTICLE_NO_GROUP;
    if(particle_enabled!=PE_PARTICLE_GEOMETRY||!particle_frame_open||particle_slot!=g_ActiveDrawSlot||
       (unsigned)particle_slot>=2||particle_count>PARTICLE_TRIANGLE_BUDGET-4||particle_group_count>=PARTICLE_GROUP_BUDGET)return;
    particle_last_group=particle_group_count++;particle_groups[particle_last_group]=(ParticleOriginalGroup){1,0};particle_capture_active=1;
}
void FieldFx_EndFallingOriginal(void){particle_capture_active=0;}
void FieldFx_LinkFallingOriginal(RenderNativeTag *entry,RenderNativeTag *primitive){
    if(cross_capture.active)FieldFx_CaptureCrossFlare(entry,primitive);
    if(ground_capture.active)FieldFx_CaptureGroundGlow(primitive);
    if(ember_capture.active)FieldFx_CaptureEmberGlow(primitive);
    /* Direct path matches the original two native link stores exactly. */
    primitive->address=entry->address;
    if(particle_capture_active&&particle_enabled==PE_PARTICLE_GEOMETRY&&particle_last_group<particle_group_count){
        unsigned group=particle_last_group;uintptr_t start=(uintptr_t)g_RenderAnimBufferBase[particle_slot],ptr=(uintptr_t)primitive;
        if(!start||ptr<start||ptr-start>RENDER_ANIM_BUFFER_BYTES-sizeof(*primitive)-36||
           g_DrawPacketOffset<0||ptr-start+sizeof(*primitive)+36>(unsigned)g_DrawPacketOffset||
           (ptr-start)%_Alignof(RenderNativeTag)||primitive->length!=9||
           (((const u8*)primitive)[sizeof(*primitive)+3]&~3u)!=0x2cu||
           particle_anchor_count[particle_slot]>=PARTICLE_ANCHOR_BUDGET){particle_groups[group].complete=0;}
        else {
            ParticleOriginalAnchor *a=&particle_anchors[particle_slot][particle_anchor_count[particle_slot]++];
            a->tag=(RenderNativeTag){(uintptr_t)primitive,0};a->original=primitive;a->original_next=primitive->address;a->group=group;
            entry->address=(uintptr_t)&a->tag;return;
        }
    }
    entry->address=(uintptr_t)primitive;
}
/* Read-only projection: does not change any GTE register or PGXP companion. */
#include "pe_particle_project.h"

static void FieldFx_QueueParticleFaces(const PeParticleFace faces[4],unsigned group){
    unsigned h=Psyz_GteCtrlRead(26)&65535;int zsf=(s16)Psyz_GteCtrlRead(29);
    s32 ox=(s32)Psyz_GteCtrlRead(24),oy=(s32)Psyz_GteCtrlRead(25);
    if(!h||zsf<=0)return;
    QueuedParticleTriangle pending[4];unsigned accepted=0;memset(pending,0,sizeof(pending));
    for(unsigned f=0;f<4;f++){
        s16 xy[6];unsigned depth[3];for(unsigned v=0;v<3;v++)if(!PeParticle_Project(&faces[f].vertices[v],g_ViewMatrixSlot.value,h,ox,oy,&xy[v*2],&xy[v*2+1],&depth[v]))return;
        int64_t area=(int64_t)(xy[2]-xy[0])*(xy[5]-xy[1])-(int64_t)(xy[4]-xy[0])*(xy[3]-xy[1]);if(!area)continue;
        int64_t ot=(((int64_t)depth[0]+depth[1]+depth[2])*zsf)>>12;
        if(ot>65535)ot=65535; /* GTE AVSZ3 unsigned OTZ saturation. */
        ot-=(u16)g_FieldFxSpriteParams.depth;
        if(ot<0||ot>=4096)return;
        ParticleTriangle *t=&pending[accepted].packet;t->tag.length=4;t->code=0x20;
        t->r=(unsigned)faces[f].rgba[0]*faces[f].rgba[3]/255;t->g=(unsigned)faces[f].rgba[1]*faces[f].rgba[3]/255;t->b=(unsigned)faces[f].rgba[2]*faces[f].rgba[3]/255;
        t->x0=xy[0];t->y0=xy[1];t->x1=xy[2];t->y1=xy[3];t->x2=xy[4];t->y2=xy[5];pending[accepted].depth=(unsigned)ot;pending[accepted].group=group;accepted++;
    }
    memcpy(particle_queue+particle_count,pending,accepted*sizeof(*pending));particle_count+=accepted;
}
void FieldFx_QueueFallingParticle(const RenderBouncingSprite *state,unsigned age){
    unsigned group=particle_last_group;particle_last_group=PARTICLE_NO_GROUP;
    if(!particle_enabled||!particle_frame_open||particle_slot!=g_ActiveDrawSlot||
       particle_count>PARTICLE_TRIANGLE_BUDGET-4||!g_ViewMatrixSlot.value)return;
    if(particle_enabled==PE_PARTICLE_GEOMETRY&&(group>=particle_group_count||!particle_groups[group].complete))return;
    if(particle_source_log_count++<8)SDL_Log("[3D debris source] family=FallingSprite phase=draw mode=%d age=%u candidate=1 projection-not-yet-admitted",particle_enabled,age);
    PeParticleFace faces[4];if(PeBattleParticle_FromFallingSprite(1,state,age,4,faces)!=4)return;
    FieldFx_QueueParticleFaces(faces,group);
}
void FieldFx_QueueBurstOrbDebris(int x,int y,int z,unsigned age,unsigned state){
    unsigned group=particle_last_group;particle_last_group=PARTICLE_NO_GROUP;
    if(!particle_enabled||!particle_frame_open||particle_slot!=g_ActiveDrawSlot||
       particle_count>PARTICLE_TRIANGLE_BUDGET-4||!g_ViewMatrixSlot.value)return;
    /* Exactly the two FT4 debris sub-states; all orb/ring/halo draws remain. */
    unsigned duration=state==2?16:state==3?24:0;
    if(!duration||(particle_enabled==PE_PARTICLE_GEOMETRY&&(group>=particle_group_count||!particle_groups[group].complete)))return;
    if(particle_source_log_count++<8)SDL_Log("[3D debris source] family=BurstOrb state=%u phase=draw mode=%d age=%u candidate=1 projection-not-yet-admitted",state,particle_enabled,age);
    PeParticleFace faces[4];PeParticleWorldPoint point={x,y,z};
    if(PeBattleParticle_Build(1,point,age,duration,4,faces)!=4)return;
    FieldFx_QueueParticleFaces(faces,group);
}

/* Shares the original bounded queue/arena; no simulation/RNG/collision writes. */
static void FieldFx_QueueExtraSpark(int x,int y,int z,unsigned age,unsigned duration,unsigned family,unsigned r,unsigned g,unsigned b){
    unsigned group=particle_last_group;particle_last_group=PARTICLE_NO_GROUP;
    if(!particle_enabled||!particle_frame_open||particle_slot!=g_ActiveDrawSlot||particle_count>PARTICLE_TRIANGLE_BUDGET-4||!g_ViewMatrixSlot.value)return;
    if(particle_enabled==PE_PARTICLE_GEOMETRY&&(group>=particle_group_count||!particle_groups[group].complete))return;
    PeParticleFace faces[4];PeParticleWorldPoint point={x,y,z};
    if(PeBattleParticle_Build(1,point,age,duration,4,faces)!=4)return;
    if(r>255)r=255;if(g>255)g=255;if(b>255)b=255;
    for(unsigned i=0;i<4;i++){unsigned shade=255-i*24;faces[i].rgba[0]=r*shade/255;faces[i].rgba[1]=g*shade/255;faces[i].rgba[2]=b*shade/255;}
    if(particle_source_log_count++<8)SDL_Log("[3D debris source] family=%s phase=draw mode=%d age=%u candidate=1 projection-not-yet-admitted native-setting-independent-of-mod-master=1",family==1?"DampedSpark":"RisingJitterSpark",particle_enabled,age);
    FieldFx_QueueParticleFaces(faces,group);
}
void FieldFx_QueueDampedSpark(int x,int y,int z,unsigned age,unsigned variant){
    if(variant>1){particle_last_group=PARTICLE_NO_GROUP;return;}
    /* Original callback variants0/1 both expire at16, with independent movement. */
    if(age<16){unsigned tint=variant==0?128:96;unsigned gain=tint*(16-age)/16;FieldFx_EmitParticleLight(x,y,z,gain*2,gain*2,gain*2,192);}
    FieldFx_QueueExtraSpark(x,y,z,age,16,1,255,160,32);
}
void FieldFx_QueueRisingSpark(int x,int y,int z,unsigned age,unsigned r,unsigned g,unsigned b){
    /* Actual original RGB track, scaled from PS1 neutral128 to UNORM255. */
    if(!(r|g|b)){particle_last_group=PARTICLE_NO_GROUP;return;} /* Never replace a zero-colour glow with opaque black geometry. */
    if(age<14)FieldFx_EmitParticleLight(x,y,z,r>127?255:r*2,g>127?255:g*2,b>127?255:b*2,192);
    FieldFx_QueueExtraSpark(x,y,z,age,14,2,r>127?255:r*2,g>127?255:g*2,b>127?255:b*2);
}


/* Snapshot only an actually linked original FT4. Original command, CLUT,
 * texture window UV and page/ABR are copied, never guessed or made opaque.
 * Two narrow crossed world planes add volume while retaining every authored
 * sprite. No original ownership anchor is involved in this Hybrid-only path. */
static void FieldFx_CaptureEmberGlow(const RenderNativeTag *primitive){
    ember_capture.active=0;
    if(!particle_enabled||!particle_frame_open||particle_slot!=g_ActiveDrawSlot||(unsigned)particle_slot>=2)return;
    uintptr_t start=(uintptr_t)g_RenderAnimBufferBase[particle_slot],ptr=(uintptr_t)primitive;
    if(!start||ptr<start||ptr-start>RENDER_ANIM_BUFFER_BYTES-sizeof(FieldStripPacket)||g_DrawPacketOffset<0||ptr-start+sizeof(FieldStripPacket)>(unsigned)g_DrawPacketOffset||ptr%_Alignof(FieldStripPacket))return;
    const FieldStripPacket *original=(const FieldStripPacket*)primitive;
    if(original->tag.length!=9||original->code!=0x2e||!(original->r0|original->g0|original->b0))return;
    FieldFx_EmitParticleLight(ember_capture.x,ember_capture.y,ember_capture.z,original->r0>127?255:original->r0*2,original->g0>127?255:original->g0*2,original->b0>127?255:original->b0*2,256);
    if(particle_enabled!=PE_PARTICLE_HYBRID||ember_count>EMBER_GLOW_QUAD_BUDGET-2||!g_ViewMatrixSlot.value)return;
    unsigned h=Psyz_GteCtrlRead(26)&65535;int zsf=(s16)Psyz_GteCtrlRead(29);
    if(!h||zsf<=0)return;
    s32 ox=(s32)Psyz_GteCtrlRead(24),oy=(s32)Psyz_GteCtrlRead(25);
    EmberGlow pending[2];unsigned accepted=0;memset(pending,0,sizeof pending);
    int radius=2+(int)(15-ember_capture.age)/4,height=radius*4;
    for(unsigned plane=0;plane<2;plane++){
        unsigned depths[4];s16 xy[8];
        for(unsigned v=0;v<4;v++){
            int side=(v&1)?radius:-radius;
            PeParticleWorldPoint point={ember_capture.x+(plane?0:side),ember_capture.y-((v&2)?0:height),ember_capture.z+(plane?side:0)};
            if(!PeParticle_Project(&point,g_ViewMatrixSlot.value,h,ox,oy,&xy[v*2],&xy[v*2+1],&depths[v]))return;
        }
        int64_t area=(int64_t)(xy[2]-xy[0])*(xy[5]-xy[1])-(int64_t)(xy[4]-xy[0])*(xy[3]-xy[1]);if(!area)continue;
        int64_t depth=(((int64_t)depths[0]+depths[1]+depths[2])*zsf)>>12;if(depth>65535)depth=65535;depth-=(u16)g_FieldFxSpriteParams.depth;if(depth<0||depth>=4096)return;
        pending[accepted].packet=*original;pending[accepted].packet.tag.address=0;
        /* Quarter authored intensity: accent, not a doubled opaque flame. */
        pending[accepted].packet.r0=original->r0/4;pending[accepted].packet.g0=original->g0/4;pending[accepted].packet.b0=original->b0/4;
        pending[accepted].packet.x0=xy[0];pending[accepted].packet.y0=xy[1];pending[accepted].packet.x1=xy[2];pending[accepted].packet.y1=xy[3];pending[accepted].packet.x2=xy[4];pending[accepted].packet.y2=xy[5];pending[accepted].packet.x3=xy[6];pending[accepted].packet.y3=xy[7];pending[accepted].depth=(unsigned)depth;accepted++;
    }
    if(!accepted)return;memcpy(ember_queue+ember_count,pending,accepted*sizeof(*pending));ember_count+=accepted;
    if(ember_log_count++<8)SDL_Log("[World flame accent] family=Program109.RisingEmber state=1 mode=Hybrid age=%u queued-worldplanes=1-or-2 original-retained=1 visual=unobserved",ember_capture.age);
}
/* Source quad is already floor-aligned by the game. Two extra world-plane
 * footprints reuse its actual UV/CLUT and ABR3 quarter-add, never a shadow
 * subtract page. Original packet/link stores remain untouched. */
static void FieldFx_CaptureGroundGlow(const RenderNativeTag *primitive){
    ground_capture.active=0;
    if(particle_enabled!=PE_PARTICLE_HYBRID||!particle_frame_open||particle_slot!=g_ActiveDrawSlot||(unsigned)particle_slot>=2||ember_count>EMBER_GLOW_QUAD_BUDGET-2||g_FieldFxSpriteParams.parameter0A!=0||g_FieldFxSpriteParams.extent_x!=32||g_FieldFxSpriteParams.extent_y!=32||!g_ViewMatrixSlot.value)return;
    uintptr_t start=(uintptr_t)g_RenderAnimBufferBase[particle_slot],ptr=(uintptr_t)primitive;
    if(!start||ptr<start||ptr-start>RENDER_ANIM_BUFFER_BYTES-sizeof(FieldStripPacket)||g_DrawPacketOffset<0||ptr-start+sizeof(FieldStripPacket)>(unsigned)g_DrawPacketOffset||ptr%_Alignof(FieldStripPacket))return;
    const FieldStripPacket *src=(const FieldStripPacket*)primitive;
    if(src->tag.length!=9||src->code!=0x2e||((src->tpage>>5)&3)!=3||!(src->r0|src->g0|src->b0))return;
    if(src->u0!=src->u2||src->u1!=src->u3||src->v0!=src->v1||src->v2!=src->v3||src->u1<src->u0||src->v2<src->v0||
       !Psyz_GpuParticleGlowSource(src->tpage,src->clut,src->u0,src->v0,(unsigned)src->u1-src->u0+1,(unsigned)src->v2-src->v0+1))return;
    unsigned h=Psyz_GteCtrlRead(26)&65535;int zsf=(s16)Psyz_GteCtrlRead(29);
    if(!h||zsf<=0)return;
    s32 ox=(s32)Psyz_GteCtrlRead(24),oy=(s32)Psyz_GteCtrlRead(25);
    EmberGlow pending[2];memset(pending,0,sizeof pending);
    for(unsigned layer=0;layer<2;layer++){
        /* Authored template0 is +/-60 with extent32 and scale3: radius360.
         * A low-energy outer wash and smaller core use the SAME receiver. */
        int radius=layer?240:450;unsigned depths[4];s16 xy[8];
        for(unsigned v=0;v<4;v++){
            PeParticleWorldPoint point={ground_capture.x+((v&1)?radius:-radius),ground_capture.y,ground_capture.z+((v&2)?radius:-radius)};
            if(!PeParticle_Project(&point,g_ViewMatrixSlot.value,h,ox,oy,&xy[v*2],&xy[v*2+1],&depths[v]))return;
        }
        int64_t a=(int64_t)(xy[2]-xy[0])*(xy[5]-xy[1])-(int64_t)(xy[4]-xy[0])*(xy[3]-xy[1]);
        int64_t b=(int64_t)(xy[6]-xy[2])*(xy[5]-xy[3])-(int64_t)(xy[4]-xy[2])*(xy[7]-xy[3]);
        if(!a||!b||(a<0)!=(b<0))return;
        int64_t depth=(((int64_t)depths[0]+depths[1]+depths[2])*zsf)>>12;if(depth>65535)depth=65535;depth-=(u16)g_FieldFxSpriteParams.depth;if(depth<0||depth>=4096)return;
        pending[layer].packet=*src;pending[layer].packet.tag.address=0;
        unsigned divisor=layer?4:8;
        pending[layer].packet.r0=src->r0/divisor;pending[layer].packet.g0=src->g0/divisor;pending[layer].packet.b0=src->b0/divisor;
        pending[layer].packet.x0=xy[0];pending[layer].packet.y0=xy[1];pending[layer].packet.x1=xy[2];pending[layer].packet.y1=xy[3];pending[layer].packet.x2=xy[4];pending[layer].packet.y2=xy[5];pending[layer].packet.x3=xy[6];pending[layer].packet.y3=xy[7];pending[layer].depth=(unsigned)depth;pending[layer].kind=GLOW_GROUND;
    }
    memcpy(ember_queue+ember_count,pending,sizeof pending);ember_count+=2;
    if(ember_log_count++<8)SDL_Log("[Ground glow accent] family=BurstOrb state=0 mode=Hybrid receiver=authored-floor quads=2 ABR=quarter-add original-retained=1 pixels=unobserved");
}
static void FieldFx_CaptureCrossFlare(RenderNativeTag *entry,const RenderNativeTag *primitive){
    cross_capture.active=0;
    if(!particle_frame_open||particle_slot!=g_ActiveDrawSlot||(unsigned)particle_slot>=2)return;
    uintptr_t start=(uintptr_t)g_RenderAnimBufferBase[particle_slot],ptr=(uintptr_t)primitive,ot=(uintptr_t)D_800B0E38.ordering[particle_slot],ep=(uintptr_t)entry;
    if(!start||!ot||ptr<start||ptr-start>RENDER_ANIM_BUFFER_BYTES-sizeof(FieldStripPacket)||g_DrawPacketOffset<0||ptr-start+sizeof(FieldStripPacket)>(unsigned)g_DrawPacketOffset||ptr%_Alignof(FieldStripPacket)||ep<ot||(ep-ot)%sizeof(RenderNativeTag)||(ep-ot)/sizeof(RenderNativeTag)>=4096)return;
    const FieldStripPacket *src=(const FieldStripPacket*)primitive;
    if(src->tag.length!=9||src->code!=0x2e||((src->tpage>>5)&3)!=1)return;
    if(cross_log_count<12){cross_log_count++;SDL_Log("[Theater VFX source] program=%u callback=%s phase=2 age=%u joint=%u mode=%d lights=%d original-retained=1",cross_capture.program,cross_capture.program==115?"OrbitFlareController":cross_capture.program==118?"JointGlow":"CrossFlare",cross_capture.age,cross_capture.joint,particle_enabled,particle_lighting);}
    if(!particle_enabled||!(src->r0|src->g0|src->b0))return;
    if(!g_ViewMatrixSlot.value)return;
    PeParticleWorldPoint center={cross_capture.x,cross_capture.y,cross_capture.z};s16 sx,sy;unsigned depth;
    if(!PeParticle_Project(&center,g_ViewMatrixSlot.value,Psyz_GteCtrlRead(26)&65535,(s32)Psyz_GteCtrlRead(24),(s32)Psyz_GteCtrlRead(25),&sx,&sy,&depth))return;
    /* Actual linked primitive RGB contains the original sampled tint/fade.
     * Existing8-light owner applies distance gain to models then restores RGB. */
    unsigned prior_lights=particle_light_count;unsigned long long source_generation=0;
    unsigned light[3]={src->r0>127?255:src->r0*2,src->g0>127?255:src->g0*2,src->b0>127?255:src->b0*2};
    if(cross_capture.program!=117){
        if((!particle_lighting||particle_light_count>=PARTICLE_LIGHT_CAP||particle_light_fault||particle_light_applied)&&(particle_enabled!=PE_PARTICLE_HYBRID||ember_count>EMBER_GLOW_QUAD_BUDGET-2))return;
        unsigned tint[3];
        if(src->u0!=src->u2||src->u1!=src->u3||src->v0!=src->v1||src->v2!=src->v3||src->u1<src->u0||src->v2<src->v0||!Psyz_GpuAuthoredFlareTint(src->tpage,src->clut,src->u0,src->v0,(unsigned)src->u1-src->u0+1,(unsigned)src->v2-src->v0+1,tint))return;
        source_generation=Psyz_GpuAuthoredFlareGeneration();if(!source_generation)return;
        for(unsigned j=0;j<3;j++)light[j]=(light[j]*tint[j]+127)/255;
    }
    FieldFx_EmitParticleLight(center.x,center.y,center.z,light[0],light[1],light[2],256);
    if(source_generation&&particle_light_count>prior_lights)particle_lights[prior_lights].source_generation=source_generation;
    if(particle_light_count>prior_lights&&cross_log_count<24){cross_log_count++;SDL_Log("[Theater VFX light admitted] program=%u phase=2 joint=%u queued-emitter=1 original-RGB-fade=1 model-pixels=unobserved",cross_capture.program,cross_capture.joint);}
    if(particle_enabled!=PE_PARTICLE_HYBRID||ember_count>EMBER_GLOW_QUAD_BUDGET-2||src->u0!=src->u2||src->u1!=src->u3||src->v0!=src->v1||src->v2!=src->v3||src->u1<src->u0||src->v2<src->v0)return;
    if(!Psyz_GpuCrossFlareSource(src->tpage,src->clut,src->u0,src->v0,(unsigned)src->u1-src->u0+1,(unsigned)src->v2-src->v0+1))return;
    const s16 xy[8]={src->x0,src->y0,src->x1,src->y1,src->x2,src->y2,src->x3,src->y3};int cx=((int)xy[0]+xy[2]+xy[4]+xy[6])/4,cy=((int)xy[1]+xy[3]+xy[5]+xy[7])/4;
    EmberGlow pending[2];memset(pending,0,sizeof pending);
    for(unsigned layer=0;layer<2;layer++){
        s16 q[8];int scale=layer?3:5;
        for(unsigned v=0;v<4;v++){int x=cx+((int)xy[v*2]-cx)*scale/4,y=cy+((int)xy[v*2+1]-cy)*scale/4;if(x< -1024||x>1023||y< -1024||y>1023)return;q[v*2]=(s16)x;q[v*2+1]=(s16)y;}
        int64_t a=(int64_t)(q[2]-q[0])*(q[5]-q[1])-(int64_t)(q[4]-q[0])*(q[3]-q[1]);int64_t b=(int64_t)(q[6]-q[2])*(q[5]-q[3])-(int64_t)(q[4]-q[2])*(q[7]-q[3]);if(!a||!b||(a<0)!=(b<0))return;
        pending[layer].packet=*src;pending[layer].packet.tag.address=0;unsigned divisor=layer?4:8;
        pending[layer].packet.r0=src->r0/divisor;pending[layer].packet.g0=src->g0/divisor;pending[layer].packet.b0=src->b0/divisor;
        pending[layer].packet.x0=q[0];pending[layer].packet.y0=q[1];pending[layer].packet.x1=q[2];pending[layer].packet.y1=q[3];pending[layer].packet.x2=q[4];pending[layer].packet.y2=q[5];pending[layer].packet.x3=q[6];pending[layer].packet.y3=q[7];pending[layer].depth=(unsigned)((ep-ot)/sizeof(RenderNativeTag));pending[layer].kind=GLOW_CROSS;pending[layer].source_generation=source_generation;
    }
    if(!((pending[0].packet.r0|pending[0].packet.g0|pending[0].packet.b0)|(pending[1].packet.r0|pending[1].packet.g0|pending[1].packet.b0)))return;
    memcpy(ember_queue+ember_count,pending,sizeof pending);ember_count+=2;
    if(cross_log_count<24){cross_log_count++;SDL_Log("[Theater VFX admitted] program=%u phase=2 joint=%u mode=Hybrid queued-billboard-glows=2 original-ABR=add original-retained=1 pixels=unobserved",cross_capture.program,cross_capture.joint);}
}
static int FieldFx_GlowSourceAllowed(const EmberGlow *queued){
    if(queued->source_generation&&queued->source_generation!=Psyz_GpuAuthoredFlareGeneration())return 0;
    const FieldStripPacket*q=&queued->packet;
    if(queued->kind==GLOW_EMBER)return 1;
    unsigned w=(unsigned)q->u1-q->u0+1,h=(unsigned)q->v2-q->v0+1;
    if(queued->kind==GLOW_GROUND)return Psyz_GpuParticleGlowSource(q->tpage,q->clut,q->u0,q->v0,w,h);
    if(queued->kind==GLOW_CROSS)return Psyz_GpuCrossFlareSource(q->tpage,q->clut,q->u0,q->v0,w,h);
    return 0;
}
static void FieldFx_FlushEmberGlow(void){
    unsigned count=ember_count;ember_count=0;ember_capture.active=0;ground_capture.active=0;cross_capture.active=0;
    if(!count||particle_enabled!=PE_PARTICLE_HYBRID||!particle_frame_open||particle_slot!=g_ActiveDrawSlot||(unsigned)particle_slot>=2||!D_800B0E38.ordering[particle_slot]||!g_RenderAnimBufferBase[particle_slot]||D_800B0E38.packets[particle_slot]!=(char*)g_RenderAnimBufferBase[particle_slot]||g_DrawPacketOffset<0)return;
    size_t offset=(unsigned)g_DrawPacketOffset,alignment=_Alignof(FieldStripPacket),bytes=count*sizeof(FieldStripPacket);
    if((uintptr_t)g_RenderAnimBufferBase[particle_slot]%alignment||offset>RENDER_ANIM_BUFFER_BYTES)return;
    offset=(offset+alignment-1)&~(alignment-1);
    if(bytes>RENDER_ANIM_BUFFER_BYTES-offset){if(ember_log_count++<16)SDL_Log("[World flame accent] capacity-rejected=1 original-retained=1");return;}
    for(unsigned i=0;i<count;i++)if(!FieldFx_GlowSourceAllowed(ember_queue+i))return;
    FieldStripPacket *out=(FieldStripPacket*)(g_RenderAnimBufferBase[particle_slot]+offset);
    for(unsigned i=0;i<count;i++){out[i]=ember_queue[i].packet;AddPrim(D_800B0E38.ordering[particle_slot]+ember_queue[i].depth,&out[i]);}
    g_DrawPacketOffset=(int)(offset+bytes);
    if(ember_log_count++<16)SDL_Log("[World flame accent] linked-quads=%u original-retained=1 capacity=%u visual=unobserved",count,(unsigned)RENDER_ANIM_BUFFER_BYTES);
}

void FieldFx_FlushParticleAugment(void){
    FieldFx_FlushEmberGlow();
    unsigned count=particle_count;particle_count=0;int open=particle_frame_open;particle_frame_open=0;
    if(!particle_enabled||!open||!count||(unsigned)g_ActiveDrawSlot>=2||particle_slot!=g_ActiveDrawSlot||
       !D_800B0E38.ordering[g_ActiveDrawSlot]||!g_RenderAnimBufferBase[g_ActiveDrawSlot]||
       D_800B0E38.packets[g_ActiveDrawSlot]!=(char*)g_RenderAnimBufferBase[g_ActiveDrawSlot]||g_DrawPacketOffset<0)return;
    if(particle_enabled==PE_PARTICLE_GEOMETRY)for(unsigned a=0;a<particle_anchor_count[particle_slot];a++){
        ParticleOriginalAnchor *anchor=&particle_anchors[particle_slot][a];
        uintptr_t start=(uintptr_t)g_RenderAnimBufferBase[particle_slot],ptr=(uintptr_t)anchor->original;
        int valid=ptr>=start&&ptr-start<=RENDER_ANIM_BUFFER_BYTES-sizeof(RenderNativeTag)-36&&
            ptr-start+sizeof(RenderNativeTag)+36<=(unsigned)g_DrawPacketOffset;
        if(anchor->group>=particle_group_count||!valid||anchor->tag.length||anchor->tag.address!=(uintptr_t)anchor->original||
           anchor->original->length!=9||(((const u8*)anchor->original)[sizeof(RenderNativeTag)+3]&~3u)!=0x2cu||
           anchor->original->address!=anchor->original_next){if(anchor->group<particle_group_count)particle_groups[anchor->group].complete=0;}
    }
    unsigned eligible=0;for(unsigned i=0;i<count;i++)if(particle_enabled!=PE_PARTICLE_GEOMETRY||(particle_queue[i].group<particle_group_count&&particle_groups[particle_queue[i].group].complete))eligible++;
    if(!eligible)return;
    size_t offset=(unsigned)g_DrawPacketOffset,alignment=_Alignof(ParticleTriangle);
    if((uintptr_t)g_RenderAnimBufferBase[g_ActiveDrawSlot]%alignment||offset>RENDER_ANIM_BUFFER_BYTES)return;
    offset=(offset+alignment-1)&~(alignment-1);
    /* REAL initialized animation arena, after all original frame writers.
     * All-or-none admission; never force original writers out of their room. */
    size_t bytes=(size_t)eligible*sizeof(ParticleTriangle);if(offset>RENDER_ANIM_BUFFER_BYTES||bytes>RENDER_ANIM_BUFFER_BYTES-offset){if(particle_log_count++<16)SDL_Log("[3D falling debris] packets=0 capacity-rejected used=%zu capacity=%u originals-retained=1\n",offset,(unsigned)RENDER_ANIM_BUFFER_BYTES);return;}
    ParticleTriangle *out=(ParticleTriangle*)(g_RenderAnimBufferBase[g_ActiveDrawSlot]+offset);
    unsigned written=0;for(unsigned i=0;i<count;i++){
        unsigned group=particle_queue[i].group;if(particle_enabled==PE_PARTICLE_GEOMETRY&&(group>=particle_group_count||!particle_groups[group].complete))continue;
        out[written]=particle_queue[i].packet;AddPrim(D_800B0E38.ordering[g_ActiveDrawSlot]+particle_queue[i].depth,&out[written++]);if(group<particle_group_count)particle_groups[group].admitted=1;
    }
    unsigned suppressed=0;if(particle_enabled==PE_PARTICLE_GEOMETRY)for(unsigned a=0;a<particle_anchor_count[particle_slot];a++){
        ParticleOriginalAnchor *anchor=&particle_anchors[particle_slot][a];
        if(anchor->group<particle_group_count&&particle_groups[anchor->group].complete&&particle_groups[anchor->group].admitted){anchor->tag.address=anchor->original_next;suppressed++;}
    }
    g_DrawPacketOffset=(int)(offset+bytes);
    if(particle_log_count++<16)SDL_Log("[3D falling debris] mode=%d packets-linked=%u original-packets-suppressed=%u used=%d capacity=%u GPU-output=unobserved\n",particle_enabled,written,suppressed,g_DrawPacketOffset,(unsigned)RENDER_ANIM_BUFFER_BYTES);
}

/* Native checkpoint owner. All augmentation is derived from original phase2
 * draws: no independent simulation, persistent age, RNG, or gameplay callbacks.
 * The final CPU service follows ParticleEndFrame. Old two-slot OT anchors must
 * NOT be cleared here: they may still be linked in the previous packet arena.
 * They are retired by BeginFrame+ClearOTagR before that slot is built again.
 * GPU state separately fences/captures authoritative VRAM; no OT is replayed.
 * Preserve settings exactly, rebuilding packets from the restored source draw.
 */
int FieldFx_ParticleStateIdle(void){
    return FieldFx_ParticleLightStateIdle()&&particle_enabled>=0&&particle_enabled<=2&&!particle_frame_open&&
        !particle_count&&!particle_group_count&&!particle_capture_active&&
        particle_last_group==PARTICLE_NO_GROUP&&!ember_count&&!ember_capture.active&&!ground_capture.active&&!cross_capture.active;
}
int FieldFx_ParticleStateWalk(const unsigned char *in,unsigned char *out){
    if(!FieldFx_ParticleStateIdle()||(!in&&!out))return 0;
    unsigned char exact[16]={ 'P','E','P','A','R','T','0','2',0,0,0,0,0,0,0,0 };
    exact[8]=(unsigned char)particle_enabled;
    exact[9]=(unsigned char)FieldFx_GetParticleLighting();
    if(in&&memcmp(in,exact,sizeof exact))return 0;
    if(out)memcpy(out,exact,sizeof exact);
    return 1;
}
