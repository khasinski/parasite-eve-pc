#include "pe1/render_shadow.h"
#define SetLineF2 PeTest_SetLineF2
#include "pe1/render_prim.h"
#undef SetLineF2
#include "pe1/room_fx_model.h"
#include "pe1/render_matrix_slot.h"
#include "pe1/render_buffers.h"
#include "pe1/room_floor.h"
#include "pe1/gte.h"
#include "pe1/geom_state.h"
#include "pe1/game_state.h"
GeomScrollRecord g_GeomScroll;
int Render_ScrollEntries(void);
int Render_StartFadeIn(int frames);
int Render_StepFade(void);
int Render_BeginSceneLoad(void);
int Render_ApplyScreenTint(void);
int Render_DrawSpriteEntry(GeomEntry *entry);
int Render_ClipPoint(int x,int y,int z);
#include <libgpu.h>
#include <psyz/video.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
extern bool InitPlatform(void);
#include <string.h>
_Static_assert(sizeof(Pe1FadeTile)==sizeof(TILE),"native game fade tile size");
_Static_assert(offsetof(Pe1FadeTile,r)==offsetof(TILE,r0),"native game fade colour");
_Static_assert(offsetof(Pe1FadeTile,x)==offsetof(TILE,x0),"native game fade geometry");
_Static_assert(sizeof(Pe1FadeTpage)==sizeof(DR_TPAGE),"native game fade page size");
_Static_assert(offsetof(Pe1FadeTpage,mode)==offsetof(DR_TPAGE,code),"native game fade page command");
_Static_assert(sizeof(((Pe1FadeTpage *)0)->mode)==sizeof(((DR_TPAGE *)0)->code[0]),"native game fade command width");
_Static_assert(sizeof(RenderShadowQuad)==sizeof(POLY_FT4),"native shadow packet size");
_Static_assert(offsetof(RenderShadowQuad,xy3)==offsetof(POLY_FT4,x3),"native shadow payload");
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); return 1; } } while(0)
static int widescreen_gpu_capture(void)
{
    if(!InitPlatform())return 77;
    ResetGraph(0);
    DISPENV display;DRAWENV draw;
    SetDefDispEnv(&display,0,0,320,224);PutDispEnv(&display);
    SetDefDrawEnv(&draw,0,0,320,224);draw.isbg=1;
    setRGB0(&draw,0,0,0);PutDrawEnv(&draw);SetDispMask(1);
    CHECK(Psyz_VideoSetWidescreen(1)==0);
    const unsigned scales[]={1,2,4,7,8};
    const int spans[3][2]={{-96,416},{0,512},{-192,320}};
    for(unsigned scale=0;scale<5;++scale){
        CHECK(Psyz_VideoSetInternalResolution(scales[scale])==0);
        int width=0,height=0;
        unsigned char *pixels=Psyz_VideoAllocRenderedFrame(&width,&height);
        free(pixels); /* apply the pending resolution before submitting geometry */
        for(unsigned picture=0;picture<3;++picture){
            Psyz_VideoSetWidescreenPicture(0,spans[picture][0],spans[picture][1]);
            PutDrawEnv(&draw);
            TILE tiles[3];const int x[]={-32,336,152};
            for(unsigned i=0;i<3;++i){
                Psyz_VideoSetWidescreenCentred(i==2);
                setTile(&tiles[i]);setXY0(&tiles[i],x[i],96);setWH(&tiles[i],16,16);
                setRGB0(&tiles[i],i==0?255:0,i==1?255:0,i==2?255:0);
                DrawPrim(&tiles[i]);
            }
            Psyz_VideoSetWidescreenCentred(0);DrawSync(0);
            pixels=Psyz_VideoAllocRenderedFrame(&width,&height);
            CHECK(pixels && width>0 && height>0 && abs(width*9-height*16)<=5);
            unsigned left=0,right=0,middle=0;double blue_x=0;
            for(int y=0;y<height;++y)for(int column=0;column<width;++column){
                const unsigned char *p=pixels+((size_t)y*width+column)*3;
                if(column<width/5 && p[0]>200 && p[1]<20 && p[2]<20)++left;
                if(column>width*4/5 && p[1]>200 && p[0]<20 && p[2]<20)++right;
                if(p[2]>200 && p[0]<20 && p[1]<20){++middle;blue_x+=column;}
            }
            free(pixels);CHECK(middle>20);
            CHECK(blue_x/middle>width*0.47 && blue_x/middle<width*0.53);
            if(!picture)CHECK(left>20 && right>20);
            printf("Widescreen GPU: x%u span%d..%d final %dx%d, edge pixels %u/%u, menu centre %.3f\n",
                scales[scale],spans[picture][0],spans[picture][1],width,height,left,right,blue_x/middle/width);
        }
    }
    Psyz_VideoSetWidescreen(0);
    return 0;
}
/* Force many alternating blend groups without presenting a frame. At x8 this
 * also exercises the independent native and scaled VRAM paths. */
static int descriptor_gpu_capture(const char *path)
{
    if(!InitPlatform())return 77;
    ResetGraph(0);
    DISPENV display;DRAWENV draw;
    SetDefDispEnv(&display,0,0,320,224);PutDispEnv(&display);
    SetDefDrawEnv(&draw,0,0,320,224);draw.isbg=1;
    setRGB0(&draw,0,0,0);PutDrawEnv(&draw);SetDispMask(1);
    FILE *output=fopen(path,"wb");CHECK(output);
    const unsigned scales[]={1,2,8};
    for(unsigned scale=0;scale<3;++scale){
        CHECK(Psyz_VideoSetInternalResolution(scales[scale])==0);
        int width=0,height=0;
        free(Psyz_VideoAllocRenderedFrame(&width,&height));
        PutDrawEnv(&draw);
        DR_TPAGE page;setDrawTPage(&page,0,0,GetTPage(0,0,0,0));DrawPrim(&page);
        for(int y=0;y<14;++y)for(int x=0;x<20;++x){
            TILE tile;setTile(&tile);setXY0(&tile,x*16,y*16);setWH(&tile,16,16);
            setRGB0(&tile,192,0,0);DrawPrim(&tile);
            setSemiTrans(&tile,1);setRGB0(&tile,0,192,0);DrawPrim(&tile);
        }
        DrawSync(0);
        RECT native_rect={0,0,320,224};
        u_long native_pixels[320*224/2];StoreImage(&native_rect,native_pixels);
        CHECK(fwrite(native_pixels,1,320u*224u*2u,output)==320u*224u*2u);
        unsigned char *pixels=Psyz_VideoAllocRenderedFrame(&width,&height);
        CHECK(pixels && width>=320 && height>=224);
        size_t size=(size_t)width*height*3;
        unsigned bright=0;
        for(size_t j=0;j<size;j+=3)if(pixels[j]>40 && pixels[j+1]>40)++bright;
        CHECK(bright>(unsigned)(width*height/2));
        CHECK(fwrite(&width,sizeof width,1,output)==1);
        CHECK(fwrite(&height,sizeof height,1,output)==1);
        CHECK(fwrite(pixels,1,size,output)==size);free(pixels);
    }
    CHECK(fclose(output)==0);return 0;
}
int main(int argc,char **argv)
{
    if(argc==3 && !strcmp(argv[1],"--descriptor-gpu"))return descriptor_gpu_capture(argv[2]);
    if(argc==2 && !strcmp(argv[1],"--widescreen-gpu"))return widescreen_gpu_capture();
    FieldActor actors[3] = {0}, reflection = {0};
    RoomFxMirror mirror = {0};
    mirror.header.owner = &reflection;
    actors[0].next = &actors[1]; actors[1].next = &actors[2];
    actors[0].type_id = actors[1].type_id = actors[2].type_id = 7;
    actors[0].sub_id = 2; actors[1].sub_id = actors[2].sub_id = 3;
    actors[1].flags = 0x10;
    g_FieldActorListHead = actors;
    g_RoomFxMirrorHandlers.init();
    g_RoomFxMirrorHandlers.initialize(&mirror.header);
    CHECK(mirror.header.owner == &reflection && !mirror.target && !mirror.active);
    CHECK(!g_RoomFxMirrorHandlers.send(&mirror.header, 0, 0, 7, 3, 0));
    CHECK(mirror.target == &actors[2]);
    CHECK(!g_RoomFxMirrorHandlers.send(&mirror.header, 1, 0, 0, 0, 0));
    CHECK(mirror.target == &actors[2]);
    CHECK(!g_RoomFxMirrorHandlers.send(&mirror.header, 0, 1, -100, 30, 0));
    CHECK(!g_RoomFxMirrorHandlers.send(&mirror.header, 0, 2, 100, 30, 0));
    CHECK(g_RoomFxMirrorHandlers.send(&mirror.header, 0, 4, 0, 0, 0) == -6);
    CHECK(!g_RoomFxMirrorHandlers.start(&mirror.header));
    CHECK(mirror.bound && reflection.render_object.reflection_owner == &actors[2]);
    CHECK(reflection.render_object.animation_state == 5 &&
          (reflection.render_object.flags_9C & 0x1000) &&
          (s16)reflection.mirror_line.x0 == -100 && reflection.mirror_line.z1 == 30);
    CHECK(!g_RoomFxMirrorHandlers.execute(&mirror.header));
    CHECK(!g_RoomFxMirrorHandlers.stop(&mirror.header) && mirror.header.state == 4);
    g_RoomFxMirrorHandlers.unload();
    g_FieldActorListHead = NULL;
    actors[2].render_object.model_matrix.translation[0] = 20;
    actors[2].render_object.model_matrix.translation[1] = 5;
    actors[2].render_object.model_matrix.translation[2] = 50;
    actors[2].render_object.table_value2c = 100;
    actors[2].render_object.table_value30 = 200;
    RoomEffect_ModelUpdate(&reflection);
    CHECK(reflection.render_object.model_matrix.translation[0] == 20 &&
          reflection.render_object.model_matrix.translation[1] == 5 &&
          reflection.render_object.model_matrix.translation[2] == 90);
    CHECK((s16)reflection.render_object.table_value2c == -100 &&
          (s16)reflection.render_object.table_value2e == -0x800 &&
          (s16)reflection.render_object.table_value30 == -200);
    {
        RenderObjectHeader header = {0};
        RenderPrimitiveDescriptor descriptors[4] = {0};
        struct {
            RenderPacket34 quad[2]; RenderPacket28 tri[2];
            RenderPacket24 flat_quad[2]; RenderPacket1C flat_tri[2];
        } packets = {0};
        RenderNativeTag ordering[4096] = {0};
        RenderObjectEntity object = {0};
        header.packet34_count = header.packet28_count = 1;
        header.packet24_count = header.packet1c_count = 1;
        object.header = &header; object.primitive_descriptors = descriptors;
        object.primitive_buffer = (u8 *)&packets;
        for (unsigned i = 0; i < 4; ++i) {
            for (unsigned j = 0; j < 4; ++j) descriptors[i].lookup_indices[j] = j;
            D_800A636C[i] = 64;
        }
        D_800B1644[0] = 0; D_800B1644[1] = 10u << 16;
        D_800B1644[2] = 10; D_800B1644[3] = 10u | (10u << 16);
        for (unsigned slot = 0; slot < 2; ++slot) {
            g_ActiveDrawSlot = slot; D_800B0E38.ordering[slot] = ordering;
            ordering[16].address = (uintptr_t)&object;
            RoomEffect_ModelDraw(&object);
            CHECK(ordering[16].address == (uintptr_t)&packets.flat_tri[slot]);
            CHECK(((RenderNativeTag *)&packets.flat_tri[slot])->address == (uintptr_t)&packets.flat_quad[slot]);
            CHECK(((RenderNativeTag *)&packets.flat_quad[slot])->address == (uintptr_t)&packets.tri[slot]);
            CHECK(((RenderNativeTag *)&packets.tri[slot])->address == (uintptr_t)&packets.quad[slot]);
            CHECK(((RenderNativeTag *)&packets.quad[slot])->address == (uintptr_t)&object);
            CHECK(packets.quad[slot].sxy3 == D_800B1644[3]);
            D_800B1644[1] = 10; D_800B1644[2] = 10u << 16;
            ordering[16].address = (uintptr_t)&object;
            RoomEffect_ModelDraw(&object);
            CHECK(ordering[16].address == (uintptr_t)&object &&
                  !((RenderNativeTag *)&packets.quad[slot])->address &&
                  !((RenderNativeTag *)&packets.tri[slot])->address &&
                  !((RenderNativeTag *)&packets.flat_quad[slot])->address &&
                  !((RenderNativeTag *)&packets.flat_tri[slot])->address);
            D_800B1644[1] = 10u << 16; D_800B1644[2] = 10;
            for (unsigned i = 0; i < 4; ++i) D_800A636C[i] = -64;
            RoomEffect_ModelDraw(&object);
            CHECK(ordering[16].address == (uintptr_t)&object);
            for (unsigned i = 0; i < 4; ++i) D_800A636C[i] = 16384;
            RoomEffect_ModelDraw(&object);
            CHECK(ordering[16].address == (uintptr_t)&object);
            for (unsigned i = 0; i < 4; ++i) D_800A636C[i] = 64;
        }
        /* The wider view must not lose faces at the old 0/320 boundaries.
         * Check the original actor renderer, both DMA buffers and all four
         * polygon packet classes wholly outside and across those boundaries. */
        for(unsigned wide=0;wide<2;++wide){
            Psyz_VideoSetWidescreen(wide);
            for(unsigned slot=0;slot<2;++slot)for(int x=-96;x<=416;x+=8){
                g_ActiveDrawSlot=slot;D_800B0E38.ordering[slot]=ordering;
                D_800B1644[0]=(u16)x|(96u<<16);
                D_800B1644[1]=(u16)(x+10)|(96u<<16);
                D_800B1644[2]=(u16)x|(106u<<16);
                D_800B1644[3]=(u16)(x+10)|(106u<<16);
                ordering[16].address=(uintptr_t)&object;
                Render_DrawTexturedQuads(&object);
                CHECK(ordering[16].address==(uintptr_t)&packets.flat_tri[slot]);
                CHECK(((RenderNativeTag *)&packets.flat_tri[slot])->address==(uintptr_t)&packets.flat_quad[slot]);
                CHECK(((RenderNativeTag *)&packets.flat_quad[slot])->address==(uintptr_t)&packets.tri[slot]);
                CHECK(((RenderNativeTag *)&packets.tri[slot])->address==(uintptr_t)&packets.quad[slot]);
                CHECK(((RenderNativeTag *)&packets.quad[slot])->address==(uintptr_t)&object);
                CHECK(packets.quad[slot].sxy0==D_800B1644[0] && packets.quad[slot].sxy3==D_800B1644[3]);
            }
        }
        Psyz_VideoSetWidescreen(0);
        puts("Original actor edges: 1040 faces across both buffers and aspect modes retain signed positions beyond 4:3");
        D_800B0E38.ordering[0] = D_800B0E38.ordering[1] = NULL;
    }

    struct { GeomState header; GeomEntryView entries[3]; } scrolling={0};
    scrolling.header.entry_offset=(size_t)((u8 *)scrolling.entries-(u8 *)&scrolling);
    scrolling.header.entry_count06=3; g_GeomState=&scrolling.header;
    for(unsigned i=0;i<3;++i) {
        GeomScrollEntry *e=&scrolling.entries[i].scroll;
        e->flags=4; e->modulusX=64; e->modulusY=32;
        e->x=10+i; e->y=20+i; e->speedX=384; e->speedY=-128;
    }
    CHECK(Render_ScrollEntries()==0);
    for(unsigned i=0;i<3;++i) {
        GeomScrollEntry *e=&scrolling.entries[i].scroll;
        CHECK(e->x==11+i && e->y==19+i && e->fractionX.word==128 && e->fractionY.word==128);
    }
    CHECK(Render_ScrollEntries()==0);
    for(unsigned i=0;i<3;++i) {
        GeomScrollEntry *e=&scrolling.entries[i].scroll;
        CHECK(e->x==13+i && e->y==19+i && e->fractionX.word==0 && e->fractionY.word==0);
    }
    g_GeomScroll.state.position.x=50; g_GeomScroll.state.position.originX=10;
    g_GeomScroll.state.position.y=-30; g_GeomScroll.state.position.originY=10;
    for(unsigned i=0;i<3;++i) {
        GeomScrollEntry *e=&scrolling.entries[i].scroll;
        e->flags=8; e->baseX=100+i; e->baseY=200+i;
        e->speedX=128; e->speedY=64;
    }
    g_GeomScroll.state.flags=0x80;
    CHECK(Render_ScrollEntries()==0);
    for(unsigned i=0;i<3;++i) {
        GeomScrollEntry *e=&scrolling.entries[i].scroll;
        CHECK(e->x==120+i && e->y==190+i);
    }
    CHECK(!(g_GeomScroll.state.flags&0x80));
    CHECK(g_GeomScroll.state.position.savedX==50 && (s16)g_GeomScroll.state.position.savedY==-30);
    GeomEntryView saved_entries[3]; memcpy(saved_entries,scrolling.entries,sizeof(saved_entries));
    for(unsigned flag=4;flag<=0x100;flag<<=6) {
        g_GameStateFlags=flag; g_GeomScroll.state.position.x++;
        CHECK(Render_ScrollEntries()==0 && !memcmp(saved_entries,scrolling.entries,sizeof(saved_entries)));
    }
    g_GameStateFlags=0;
    struct { RenderNativeTag tag; u32 hd[4]; u8 r,g,b,code; u16 x,y; u8 u,v; u16 clut; } primitives[3][4];
    for(unsigned i=0;i<3;++i) {
        scrolling.entries[i].render.prim_count=2;
        scrolling.entries[i].render.u30.prim=primitives[i];
    }
    for(unsigned duration=2;duration<=255;++duration) {
        CHECK(Render_StartFadeIn(duration)==0);
        CHECK(g_ScreenTint.fadeFrameCount==duration && g_ScreenTint.fadeFrameCounter==0);
        for(unsigned frame=0;frame<duration;++frame) {
            memset(primitives,0x5a,sizeof(primitives));
            g_ActiveDrawSlot=frame&1;
            CHECK(Render_StepFade()==0);
            for(unsigned entry=0;entry<3;++entry)for(unsigned j=0;j<4;++j) {
                unsigned expected=j/2==(frame&1)?128-frame*128/(duration-1):0x5a;
                CHECK(primitives[entry][j].r==expected && primitives[entry][j].g==expected && primitives[entry][j].b==expected);
            }
        }
        CHECK(!(g_GeomScroll.state.flags&0x400) && (g_GeomScroll.state.flags&0x800));
        CHECK(scrolling.header.field26==0x1ff0);
    }
    g_GeomScroll.state.flags=0; g_GeomScroll.state.position.meshTintLevel=45;
    for(unsigned phase=0;phase<2;++phase) {
        CHECK(Render_BeginSceneLoad()==0);
        for(unsigned slot=0;slot<2;++slot) {
            g_ActiveDrawSlot=slot;
            CHECK(Render_ApplyScreenTint()==0);
            for(unsigned entry=0;entry<3;++entry)for(unsigned j=0;j<2;++j)
                CHECK(primitives[entry][slot*2+j].r==(phase?128:45));
        }
        CHECK(!(g_GeomScroll.state.flags&0x1000));
    }
    GeomState geometry={0}; g_GeomState=&geometry;
    geometry.clip_offset_x=12; geometry.clip_offset_y=-7;
    geometry.clip_min_x=-100; geometry.clip_max_x=100;
    geometry.clip_min_y=-80; geometry.clip_max_y=80; geometry.depth_offset=20;
    for(int x=-150;x<=150;++x)for(int y=-100;y<=100;++y) {
        int expected_x=x+12,expected_y=y-7;
        if(expected_x < -100)expected_x=-100;
        if(expected_x > 100)expected_x=100;
        if(expected_y < -80)expected_y=-80;
        if(expected_y > 80)expected_y=80;
        CHECK(Render_ClipPoint(x,y,30)==0);
        CHECK((s16)geometry.disp_src_x==expected_x && (s16)geometry.disp_src_y==expected_y && geometry.field26==50);
    }
    struct { GeomEntry entry; u32 positions[2]; u32 textures[4]; } mesh={0};
    struct { RenderNativeTag tag; u32 hd[4]; u8 r,g,b,code; u16 x,y; u8 u,v; u16 clut; } sprites[4]={0};
    struct { RenderNativeTag tag; u32 command; } pages[4]={0};
    RenderNativeTag sprite_ordering[4096];
    mesh.entry.prim_count=2; mesh.entry.u30.prim=sprites;
    mesh.entry.u34.pagePackets=(void *)pages;
    mesh.entry.u28.pos_ptr=(u8 *)mesh.positions-(u8 *)&mesh.entry;
    mesh.positions[0]=(20u<<22)|(30u<<12)|16;
    mesh.positions[1]=(40u<<22)|(50u<<12)|16;
    geometry.field26=0;
    for(unsigned i=0;i<4;++i) { sprites[i].tag.length=3; pages[i].tag.length=1; }
    for(unsigned slot=0;slot<2;++slot) {
        for(unsigned i=0;i<4096;++i)sprite_ordering[i]=(RenderNativeTag){0,0};
        D_800B0E38.ordering[slot]=sprite_ordering; g_ActiveDrawSlot=slot;
        CHECK(Render_DrawSpriteEntry(&mesh.entry)==0);
        unsigned j=slot*2;
        CHECK(sprite_ordering[16].address==(uintptr_t)&pages[j+1]);
        CHECK(pages[j+1].tag.address==(uintptr_t)&sprites[j+1]);
        CHECK(sprites[j+1].tag.address==(uintptr_t)&pages[j]);
        CHECK(pages[j].tag.address==(uintptr_t)&sprites[j] && sprites[j].tag.address==0);
        CHECK(sprites[j].x==20 && sprites[j].y==30 && sprites[j+1].x==40 && sprites[j+1].y==50);
        CHECK(sprites[j].tag.length==3 && pages[j].tag.length==1);
    }
    _Alignas(max_align_t) u8 mesh_work[4*sizeof(RenderTilePacket)+4*sizeof(pages[0])];
    mesh.entry.u2C.uv_ptr=(u8 *)mesh.textures-(u8 *)&mesh.entry;
    void *mesh_end=NULL;
    CHECK(Render_LoadMeshEntry(&mesh.entry,(void *)mesh_work,&mesh_end)==0);
    CHECK(mesh_end==mesh_work+sizeof(mesh_work));
    CHECK(mesh.entry.u30.prim==mesh_work);
    for(unsigned i=0;i<4;++i) {
        RenderTilePacket *sprite=(RenderTilePacket *)mesh_work+i;
        RenderNativeTag *page=(RenderNativeTag *)(mesh_work+4*sizeof(RenderTilePacket)+i*sizeof(pages[0]));
        CHECK(((RenderNativeTag *)sprite)->length==7 && page->length==1);
        CHECK(sprite->r==128 && sprite->g==128 && sprite->b==128 && sprite->code==0x7c);
        CHECK(sprite->x==(i%2?40:20) && sprite->y==(i%2?50:30));
    }
    RenderShadowActor actor={0}; RenderObjectHeader model={0};
    RenderMatrix bone={0}; RenderShadowQuad packets[2]={0};
    RenderNativeTag ordering[4096];
    actor.render_object.header=&model; actor.render_object.matrices=&bone;
    actor.render_object.variant_visible=1; actor.shadow_quads=packets; actor.shadow_intensity=128;
    model.shadow_radius=32;
    bone.rotation[0][0]=bone.rotation[1][1]=bone.rotation[2][2]=4096;
    GteMatrix view={0}; view.m[0][0]=4096; view.m[1][2]=-4096; view.m[2][1]=4096; view.t[2]=1024;
    memcpy(g_EntityRenderScratch,&view,sizeof(view)); g_EntityRenderScratch[8]=256;
    ResetGraph(0); gte_SetGeomOffset(160,112);
    for(unsigned slot=0;slot<2;++slot) {
        ClearOTagR((OT_TYPE *)ordering,4096);
        D_800B0E38.ordering[slot]=ordering; g_ActiveDrawSlot=slot;
        actor.render_object.flags_9C=slot?2:0;
        actor.render_object.fade_red=30; actor.render_object.fade_green=60; actor.render_object.fade_blue=90;
        CHECK(Render_DrawActorShadow(&actor)==0);
        RenderShadowQuad *q=&packets[slot];
        CHECK(q->tag.length==9 && q->code==0x2e);
        CHECK(q->r0==(slot?30:64) && q->g0==q->r0 && q->b0==q->r0);
        CHECK(q->clut==0x7210 && q->tpage==0xcb && q->u3==63 && q->v3==127);
        unsigned linked=0;
        for(unsigned i=0;i<4096;++i) if(ordering[i].address==(uintptr_t)q) ++linked;
        CHECK(linked==1 && q->tag.address!=(uintptr_t)q);
        CHECK(q->xy0!=q->xy1 && q->xy0!=q->xy2);
    }
    CHECK(Psyz_VideoSetDynamicLighting(1)==0 && Psyz_VideoGetDynamicLighting()==1);
    CHECK(Psyz_VideoSetEffectGeometryCorrection(1)==0 && Psyz_VideoGetEffectGeometryCorrection()==1);
    Psyz_PgxpSetActive(1);
    Psyz_GteSetPgxpEffectMode(1);
    {
        double m[3][3]={{1.0,0.0,0.0},{0.0,0.0,-1.0},{0.0,1.0,0.0}};
        double t[3]={0.0,0.0,1024.0};
        u32 fx_sxy=(163u)|((112u)<<16);
        float sx=0.0f, sy=0.0f, sz=0.0f;
        Psyz_GteSetPgxpMatrix(m, t);
        Psyz_GteProjectFloatVertex(12.25, -8.5, 0.0, &fx_sxy, fx_sxy);
        CHECK(Psyz_PgxpLookupVertex(&fx_sxy, fx_sxy, &sx, &sy, &sz)==2);
        CHECK(sx > 163.0f && sx < 163.2f);
        Psyz_VideoSetEffectGeometryCorrection(0);
        CHECK(Psyz_PgxpLookupVertex(&fx_sxy, fx_sxy, &sx, &sy, &sz)==0);
    }
    Psyz_GteSetPgxpEffectMode(0);
    Psyz_VideoSetEffectGeometryCorrection(0);
    Psyz_VideoSetDynamicLighting(0);
    RenderShadowQuad saved=packets[1];
    actor.flags=0x400; CHECK(Render_DrawActorShadow(&actor)==0 && !memcmp(&saved,&packets[1],sizeof(saved)));
    actor.flags=0; actor.render_object.variant_visible=0;
    CHECK(Render_DrawActorShadow(&actor)==0 && !memcmp(&saved,&packets[1],sizeof(saved)));
    puts("Original mesh module: 60501 clip points passed");
    puts("Original shadow: native FT4 layout, both slots, projection, shade and ordering links passed");
    return 0;
}
