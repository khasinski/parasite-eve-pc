#include "pe1/geom_state.h"
#include "common.h"
u16 GetClut(int x, int y);
u16 GetTPage(int tp, int abr, int x, int y);
#include "pe1/render_prim.h"
#include "pe1/psyq_gpu.h"
#include <psyz/gpu.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
#include <psyz/video.h>
#include <SDL3/SDL.h>

#ifdef PE_PS5
const char *Psyz_CpuFieldMaterialDirectory(void);
#endif
static const char *Render_HdPackDirectory(void) {
#if defined(PE_PS5) && !defined(PE_PS5_GPU_NATIVE)
    return Psyz_CpuFieldMaterialDirectory();
#else
    return SDL_getenv("PE_HD_BACKGROUNDS");
#endif
}
static uint64_t hd_geometry_key;
/* Trace status0notattempted,1openfailed,2invalid,3loaded; no load policy change. */
static int hd_wide_trace_status;
static char hd_background_directory[4096];
static const char *hd_background_path = hd_background_directory;
static const GeomState *hd_prepared_state;

static uint64_t Render_HdHash(uint64_t hash, const void *data, size_t size) {
    const u8 *bytes=data;
    for (size_t i=0;i<size;++i) hash=(hash^bytes[i])*UINT64_C(1099511628211);
    return hash;
}

static int Render_HdCommand(const u_long *words, int available, void *unused) {
    (void)unused;
    if (available<3) return -1;
    Psyz_GpuSelectSpriteAtlas((uint64_t)(u32)words[1] | ((uint64_t)(u32)words[2]<<32),
                             (u32)words[0]&0xFFFFFF);
    return 3;
}

/* Optional presentation-only side tiles. Their commands are explicitly ignored
 * by native VRAM rendering; the original mesh remains the gameplay authority.
 * PEW1 is little endian, with no native pointers or host structure padding.
 * Each record: group u32, cells u32, atlas key u64, visible left/right/top/bottom
 * i32, then cells x,y i16 pairs. One backdrop record per view group, drawn
 * behind the whole room.
 * PEW2 adds a u32 layer kind after the bounds. Kind 0 is a PEW1 backdrop. Kind 1
 * is a depth-sorted layer: after the positions come cells u16 native depths (an
 * entry's depth plus its tile offset, 1..4095), and each tile is linked where the
 * original renderer links a tile of that depth. Side continuations of foreground
 * layers (a car, a canopy) then cover actors exactly as their native part does.
 * Several layers may share a view group; their art must be transparent outside
 * the object (alpha below 0.5 draws nothing). */
#define HD_WIDE_GROUPS 256
#define HD_WIDE_CELLS 4096
#define HD_FRONT_LAYERS 64
typedef struct {
    unsigned count, group;
    uint64_t key;
    int left, right;
    short *xy;
    u16 *depth;     /* depth-sorted layers only */
    u8 *seam;       /* backdrop cells bordering the original picture: -1 left, +1 right, 0 */
    RenderTilePacket *packets[2];   /* count cells, then a seam copy per cell */
} HdBackdropGroup;
static HdBackdropGroup hd_wide[HD_WIDE_GROUPS],hd_under[HD_WIDE_GROUPS],hd_front[HD_FRONT_LAYERS];
static unsigned hd_front_count;

static void Render_ResetHdWidescreen(void) {
    hd_wide_trace_status=0;
    /* Queued ordering tables may still point at these two packet buffers. */
    int queued=hd_front_count!=0;
    for (unsigned i=0;i<HD_WIDE_GROUPS;++i)
        if(hd_wide[i].count || hd_under[i].count)queued=1;
    if(queued)DrawSync(0);
    for (unsigned i=0;i<HD_WIDE_GROUPS;++i) {
        free(hd_wide[i].xy);free(hd_wide[i].seam);
        free(hd_wide[i].packets[0]);free(hd_wide[i].packets[1]);
        free(hd_under[i].xy);free(hd_under[i].seam);
        free(hd_under[i].packets[0]);free(hd_under[i].packets[1]);
    }
    for (unsigned i=0;i<HD_FRONT_LAYERS;++i) {
        free(hd_front[i].xy);free(hd_front[i].depth);
        free(hd_front[i].packets[0]);free(hd_front[i].packets[1]);
    }
    memset(hd_wide,0,sizeof(hd_wide));memset(hd_under,0,sizeof(hd_under));
    memset(hd_front,0,sizeof(hd_front));hd_front_count=0;
}

static int Render_ReadHdWide(SDL_IOStream *file, unsigned bytes, uint64_t *value) {
    *value=0;
    for (unsigned i=0;i<bytes;++i) {
        unsigned char byte;if(SDL_ReadIO(file,&byte,1)!=1)return 0;
        *value|=(uint64_t)(unsigned)byte<<(8*i);
    }
    return 1;
}

static void Render_LoadHdBackdrop(HdBackdropGroup *tiles,const char *suffix) {
    hd_wide_trace_status=1;
    char filename[4096];
    int size=snprintf(filename,sizeof(filename),"%s/%016" PRIx64 "-%s.bin",hd_background_path,hd_geometry_key,suffix);
    if(size<0 || (size_t)size>=sizeof(filename))return;
    SDL_IOStream *file=SDL_IOFromFile(filename,"rb");if(!file)return;
    hd_wide_trace_status=2;
    Sint64 wide_bytes=SDL_GetIOSize(file);
    if(wide_bytes<8 || wide_bytes>8+(HD_WIDE_GROUPS+HD_FRONT_LAYERS)*36+HD_WIDE_CELLS*6){SDL_CloseIO(file);return;}
    uint64_t magic,groups,value;
    unsigned total=0;
    int valid=Render_ReadHdWide(file,4,&magic) && (magic==0x31574550u || magic==0x32574550u) &&
        Render_ReadHdWide(file,4,&groups) && groups<=HD_WIDE_GROUPS+HD_FRONT_LAYERS;
    const int layered=magic==0x32574550u;
    for(unsigned i=0;valid && i<groups;++i) {
        uint64_t group,count,key,left,right,top,bottom,kind=0;
        valid=Render_ReadHdWide(file,4,&group) && group<HD_WIDE_GROUPS &&
            Render_ReadHdWide(file,4,&count) && count>0 && count<=HD_WIDE_CELLS-total &&
            Render_ReadHdWide(file,8,&key) && key && Render_ReadHdWide(file,4,&left) &&
            Render_ReadHdWide(file,4,&right) && Render_ReadHdWide(file,4,&top) && Render_ReadHdWide(file,4,&bottom) &&
            (!layered || Render_ReadHdWide(file,4,&kind)) && kind<=1;
        if(!valid)break;
        /* Depth-sorted layers continue the room's own layers, so only side art has them. */
        HdBackdropGroup *layer=kind ? (tiles==hd_wide && hd_front_count<HD_FRONT_LAYERS ? &hd_front[hd_front_count] : NULL)
                                    : (tiles[group].count ? NULL : &tiles[group]);
        if(!layer){valid=0;break;}
        int l=(int32_t)(uint32_t)left,r=(int32_t)(uint32_t)right;
        int y0=(int32_t)(uint32_t)top,y1=(int32_t)(uint32_t)bottom;
        if(l < -2048 || r>4096 || l>=r || y0 < -2048 || y1>4096 || y0>=y1){valid=0;break;}
        total+=(unsigned)count;
        layer->xy=malloc((size_t)count*2*sizeof(short));
        layer->depth=kind ? malloc((size_t)count*sizeof(u16)) : NULL;
        layer->packets[0]=calloc((size_t)count*2,sizeof(RenderTilePacket));
        layer->packets[1]=calloc((size_t)count*2,sizeof(RenderTilePacket));
        layer->seam=kind ? NULL : calloc((size_t)count,1);
        if(kind)++hd_front_count;   /* owned from here, so a failure below still frees it */
        if(!layer->xy || !layer->packets[0] || !layer->packets[1] || (kind ? !layer->depth : !layer->seam)){valid=0;break;}
        for(unsigned n=0;valid && n<count*2;++n) {
            valid=Render_ReadHdWide(file,2,&value);
            layer->xy[n]=(int16_t)(uint16_t)value;
        }
        for(unsigned n=0;valid && kind && n<count;++n) {
            valid=Render_ReadHdWide(file,2,&value) && value>=1 && value<=4095;
            if(valid)layer->depth[n]=(u16)value;
        }
        for(unsigned n=0;valid && n<count;++n) {
            int x=layer->xy[n*2],y=layer->xy[n*2+1];
            /* Bounds describe the visible span, not complete-cell extents.
             * Official side art overlaps the right edge; presenter crops it. */
            if(x<l || x>=r || y<y0 || y>=y1)valid=0;
        }
        if(!valid)break;
        /* Side art that only abuts the original picture leaves its last, partly
         * covered column over the clear colour: a dark line at the 4:3 edge once
         * the squeezed view is shown. A backdrop cell with no neighbour towards
         * the picture is drawn once more, 4 px inside it and behind it, so that
         * column blends with the side's own material instead. */
        for(unsigned n=0;!kind && n<count;++n) {
            int x=layer->xy[n*2],y=layer->xy[n*2+1],inward=x<0 ? 16 : -16,neighbour=0;
            for(unsigned m=0;m<count && !neighbour;++m)
                neighbour=layer->xy[m*2]==x+inward && layer->xy[m*2+1]==y;
            if(!neighbour && (x<0 ? x+16<=8 : x>=320-8))layer->seam[n]=(u8)(x<0 ? 1 : 2);
        }
        int loaded=0;const char *formats[]={"webp","png","bmp"};
        for(unsigned format=0;format<3 && !loaded;++format) {
            size=snprintf(filename,sizeof(filename),"%s/%016" PRIx64 ".%s",hd_background_path,key,formats[format]);
            if(size>=0 && (size_t)size<sizeof(filename))loaded=Psyz_GpuLoadSpriteAtlas(key,filename)==1;
        }
        if(!loaded){valid=0;break;}
        for(unsigned n=0;n<count*2;++n)
            Psyz_GpuRegisterInterpolationBackgroundPair(&layer->packets[0][n].r,&layer->packets[1][n].r);
        layer->count=(unsigned)count;layer->key=key;layer->group=(unsigned)group;
        layer->left=l;layer->right=r;
    }
    unsigned char trailing;if(valid && SDL_ReadIO(file,&trailing,1)!=0)valid=0;
    SDL_CloseIO(file);
    if(!valid){
        Render_ResetHdWidescreen();hd_wide_trace_status=2;Psyz_GpuResetSpriteAtlases();
        fprintf(stderr,"Ignoring incomplete HD %s geometry %016" PRIx64 "\n",suffix,hd_geometry_key);
    }
    else hd_wide_trace_status=3;
}

static void Render_LoadHdWidescreen(void) {
    Render_LoadHdBackdrop(hd_under,"under");
    Render_LoadHdBackdrop(hd_wide,"wide");
}

void Render_TraceWidescreenMetadata(int buffer_y,unsigned tiles,int left,int right,int available,int hd_left,int hd_right) {
#if defined(PE_PS5) && defined(PE_PS5_GPU_NATIVE)
    unsigned group=g_GeomScroll.state.position.groupSelect;
    Psyz_GpuTraceResolutionRoom(buffer_y,tiles,left,right,available,hd_left,hd_right,
        (unsigned long long)hd_geometry_key,group,hd_wide[group].count,hd_wide_trace_status);
#else
    (void)buffer_y;(void)tiles;(void)left;(void)right;(void)available;(void)hd_left;(void)hd_right;
#endif
}

int Render_GetHdWidescreenPicture(int *left,int *right) {
    if(!hd_geometry_key || !g_GeomState || !Psyz_VideoGetWidescreen() || !Psyz_GpuGetSpriteAtlasesEnabled())return 0;
    unsigned group=g_GeomScroll.state.position.groupSelect;
    if(!hd_wide[group].count)return 0;
    *left=hd_wide[group].left+(short)g_GeomState->out_disp_x;
    *right=hd_wide[group].right+(short)g_GeomState->out_disp_x;
    return 1;
}

/* Original fades/tints update the room's own packets, not the source artwork. */
static const RenderTilePacket *Render_HdBackdropColor(int slot,unsigned group) {
    const GeomEntry *entries=(const void *)((const u8 *)g_GeomState+g_GeomState->entry_offset);
    for(unsigned n=0;n<g_GeomState->entry_count06;++n) {
        if(entries[n].group==group && (entries[n].head.b.flags&2) && entries[n].prim_count && entries[n].u30.prim)
            return (const RenderTilePacket *)entries[n].u30.prim+slot*entries[n].prim_count;
    }
    return NULL;
}

/* Links one layer's tiles: a backdrop behind everything, a depth-sorted layer
 * where Render_DrawSpriteEntry links a native tile of the same depth. */
static void Render_LinkHdTiles(int slot,RenderNativeTag *ordering,HdBackdropGroup *layer,unsigned extension,
                               const RenderTilePacket *color) {
    for(unsigned j=0;j<layer->count*2;++j) {
        /* j < count: the cells; then each seam cell's copy (Render_LoadHdBackdrop) */
        unsigned i=j<layer->count ? j : j-layer->count;
        int shift=0;
        if(j>=layer->count) {
            if(!layer->seam || !layer->seam[i])continue;
            shift=layer->seam[i]==1 ? 4 : -4;
        }
        Psyz_GpuEnsureInterpolationBackgroundPair(&layer->packets[0][j].r,&layer->packets[1][j].r);
        RenderTilePacket *sprite=&layer->packets[slot][j];
        int x=layer->xy[i*2]+shift+(short)g_GeomState->out_disp_x;
        int y=layer->xy[i*2+1]+(short)g_GeomState->out_disp_y;
        if(x+16 < -112 || x>432 || y+16<0 || y>240)continue;
        int depth=4095;
        if(layer->depth) {
            depth=(s16)(g_GeomState->field26+layer->depth[i]);
            if(((depth-8)&0xFFFF)>=0xFF1)continue;   /* the native tile would not be drawn either */
        }
        sprite->length=7;sprite->hd_command=0xF8000000u|extension|i;
        sprite->hd_key_low=(u32)layer->key;sprite->hd_key_high=(u32)(layer->key>>32);
        sprite->packed_marker=0x005A4D50u;sprite->code=0x7c;
        sprite->r=color?color->r:128;sprite->g=color?color->g:128;sprite->b=color?color->b:128;
        sprite->x=(u16)x;sprite->y=(u16)y;
        sprite->address=ordering[depth].address;ordering[depth].address=(uintptr_t)sprite;
    }
}

static void Render_LinkHdBackdrop(int slot,RenderNativeTag *ordering,HdBackdropGroup *tiles,unsigned extension) {
    if(slot<0 || slot>1 || !hd_geometry_key || !g_GeomState || !Psyz_GpuGetSpriteAtlasesEnabled())return;
    unsigned group=g_GeomScroll.state.position.groupSelect;
    if(!tiles[group].count)return;
    const RenderTilePacket *color=Render_HdBackdropColor(slot,group);
    Render_LinkHdTiles(slot,ordering,&tiles[group],extension,color);
    if(tiles!=hd_wide)return;
    for(unsigned n=0;n<hd_front_count;++n)
        if(hd_front[n].group==group && hd_front[n].count)
            Render_LinkHdTiles(slot,ordering,&hd_front[n],extension,color);
}

void Render_LinkHdWidescreenBackdrop(int slot,RenderNativeTag *ordering) {
    int left,right;
    if(Render_GetHdWidescreenPicture(&left,&right))Render_LinkHdBackdrop(slot,ordering,hd_wide,0x800000u);
}

void Render_LinkHdUnderlayBackdrop(int slot,RenderNativeTag *ordering) {
    Render_LinkHdBackdrop(slot,ordering,hd_under,0x400000u);
}

/* Hash immutable descriptors before scripts animate the native entries. The
 * exporter hashes the same little-endian fields, independent of host pointers. */
void Render_PrepareHdBackgrounds(void) {
    Psyz_GpuResetInterpolation();
    const uint64_t previous_key=hd_geometry_key;
    Render_ResetHdWidescreen();
    const char *pack=Render_HdPackDirectory();
    snprintf(hd_background_directory,sizeof(hd_background_directory),"%s",pack?pack:"");
    hd_prepared_state=g_GeomState;
    hd_geometry_key=0;
    Psyz_GpuRegisterCommandHandler(0xF8,Render_HdCommand,NULL);
    const GeomEntry *entries=(const void *)((const u8 *)g_GeomState+g_GeomState->entry_offset);
    uint64_t hash=UINT64_C(14695981039346656037);
    for (unsigned i=0;i<g_GeomState->entry_count06;++i) {
        const GeomEntry *entry=&entries[i];
        hash=Render_HdHash(hash,&entry->group,1);
        hash=Render_HdHash(hash,&entry->prim_count,2);
        hash=Render_HdHash(hash,&entry->base_x,4);
        hash=Render_HdHash(hash,(const u8 *)entry+entry->u28.pos_ptr,(size_t)entry->prim_count*4);
        hash=Render_HdHash(hash,(const u8 *)entry+entry->u2C.uv_ptr,(size_t)entry->prim_count*8);
    }
    if (previous_key!=hash) Psyz_GpuResetSpriteAtlases();
    hd_geometry_key=hash;
    if (*hd_background_path) Render_LoadHdWidescreen();
    fprintf(stderr,"HD background geometry %016" PRIx64 " (%u entries)\n",hash,(unsigned)g_GeomState->entry_count06);
}

void Render_GetBackgroundInfo(PeBackgroundInfo *out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->geometry_key = hd_geometry_key;
    out->hd_enabled = Psyz_GpuGetSpriteAtlasesEnabled() != 0;
    out->hd_pack_path = hd_background_directory;
    if (!g_GeomState) return;
    unsigned group = g_GeomScroll.state.position.groupSelect;
    out->active_group = group;
    out->disp_x = (short)g_GeomState->out_disp_x;
    out->disp_y = (short)g_GeomState->out_disp_y;
    out->total_entry_count = g_GeomState->entry_count06;
    if (group < HD_WIDE_GROUPS) out->widescreen_key = hd_wide[group].key;
    if (!hd_geometry_key) return;
    const GeomEntry *entries = (const void *)((const u8 *)g_GeomState + g_GeomState->entry_offset);
    for (u32 i = 0; i < g_GeomState->entry_count06; ++i) {
        if (entries[i].group == group && (entries[i].head.b.flags & 2)) {
            if (out->active_entry_count < 16)
                out->active_entry_keys[out->active_entry_count++] = Render_HdHash(hd_geometry_key, &i, 4);
        }
    }
}

/* Replace presentation metadata only. Never rebuild native mesh packets or
 * re-hash descriptors after scripts have animated them. */
void Render_RefreshHdBackgrounds(void) {
    if (!hd_geometry_key || !g_GeomState || hd_prepared_state!=g_GeomState) return;
    const char *pack=Render_HdPackDirectory();
    DrawSync(0);
    Render_ResetHdWidescreen();
    Psyz_GpuResetSpriteAtlases();
    snprintf(hd_background_directory,sizeof(hd_background_directory),"%s",pack?pack:"");
    if (*hd_background_path) Render_LoadHdWidescreen();
    GeomEntry *entries=(void *)((u8 *)g_GeomState+g_GeomState->entry_offset);
    for (u32 index=0;index<g_GeomState->entry_count06;++index) {
        GeomEntry *entry=entries+index;
        if (!entry->u30.prim) continue;
        uint64_t key=Render_HdHash(hd_geometry_key,&index,4);
        int loaded=0;const char *formats[]={"webp","png","bmp"};char filename[4096];
        for (unsigned i=0;*hd_background_path && i<3 && !loaded;++i) {
            int n=snprintf(filename,sizeof(filename),"%s/%016" PRIx64 ".%s",hd_background_path,key,formats[i]);
            if (n>=0 && (size_t)n<sizeof(filename)) loaded=Psyz_GpuLoadSpriteAtlas(key,filename)==1;
        }
        if (!loaded) key=0;
        RenderTilePacket *packets=entry->u30.prim;
        for (unsigned slot=0;slot<2;++slot) for (unsigned i=0;i<entry->prim_count;++i) {
            RenderTilePacket *p=packets+slot*entry->prim_count+i;
            p->hd_command=key?0xF8000000u|i:0;
            p->hd_key_low=(u32)key;p->hd_key_high=(u32)(key>>32);
        }
    }
}

/* A mesh entry seen as the address its self-relative array offsets add to. */
typedef union GeomEntryAddress {
    GeomEntry *entry;
    uintptr_t address;
} GeomEntryAddress;

/* Build both frame buffers of room-tile sprites and texture-page packets. */
int Render_LoadMeshEntry(GeomEntry *entry, RenderTilePacket *buffer, void **end)
{
    unsigned int paletteStart = g_GeomScroll.state.position.otZ;
    GeomEntryAddress base;
    unsigned int count;
    u32 *positions;
    RenderTileTexture *textures;
    unsigned int pass;
    RenderTexturePagePacket *pages;
    unsigned int shade;
    int paletteMax;
    RenderTexturePagePacket **pageSlot;
    u16 *dispX;

    base.entry = entry;
    count = entry->prim_count;
    positions = (u32 *)(base.address + entry->u28.pos_ptr);
    textures = (RenderTileTexture *)(base.address + entry->u2C.uv_ptr);
    pass = 0;
    pages = (RenderTexturePagePacket *)(buffer + count * 2);
    paletteStart += 479;
    shade = 128;
    paletteMax = paletteStart;

    uint64_t atlas_key=0;
    if (hd_geometry_key && *hd_background_path) {
        const GeomEntry *entries=(const void *)((const u8 *)g_GeomState+g_GeomState->entry_offset);
        u32 index=(u32)(entry-entries);
        atlas_key=Render_HdHash(hd_geometry_key,&index,4);
        char filename[4096];
        const char *formats[]={"webp","png","bmp"};
        int loaded=0;
        for (unsigned format=0;format<3 && !loaded;++format) {
            int size=snprintf(filename,sizeof(filename),"%s/%016" PRIx64 ".%s",hd_background_path,atlas_key,formats[format]);
            if (size>=0 && (size_t)size<sizeof(filename))
                loaded=Psyz_GpuLoadSpriteAtlas(atlas_key,filename)==1;
        }
        if (!loaded) atlas_key=0;
    }
    for(unsigned i=0;i<count;++i)
        Psyz_GpuRegisterInterpolationBackgroundPair(&buffer[i].r,&buffer[count+i].r);
    entry->u30.prim = buffer;
    /* The page pointer and disp_x are stored through pointers to the fields.
     * GCC 2.7 lets a load of a fixed scalar pass a structure field store, but
     * not an indirect store, so the g_GeomState reload stays after them as in
     * retail. */
    pageSlot = &entry->u34.pagePackets;
    *pageSlot = pages;
    dispX = &entry->disp_x;
    *dispX = entry->scr_x + g_GeomState->out_disp_x;
    entry->disp_y = entry->scr_y + g_GeomState->out_disp_y;

    do {
        unsigned int i = 0;
        /* Keep the relational guard: GCC's eliminated comparison leaves a
         * reload slot that is part of the retail 104-byte stack frame. */
        if (i < count) {
            RenderTileTexture *texture = textures;
            u32 *position = positions;
            RenderTexturePagePacket *page = pages;
            RenderTilePacket *sprite = buffer;
            do {
                sprite->length = 7;
                sprite->hd_command = atlas_key ? 0xF8000000u | i : 0;
                sprite->hd_key_low = atlas_key ? (u32)atlas_key : 0;
                sprite->hd_key_high = atlas_key ? (u32)(atlas_key>>32) : 0;
                sprite->packed_marker = 0x005A4D50u;
                sprite->code = 0x7c;
                sprite->r = shade;
                sprite->g = shade;
                sprite->b = shade;
                sprite->x = entry->disp_x + (*position >> 22);
                sprite->y = entry->disp_y + ((*position >> 12) & 1023);
                sprite->u = texture->bytes[4];
                sprite->v = texture->bytes[3];
                if (texture->words[1] & 0x10000000)
                    sprite->code |= 2;
                else
                    sprite->code &= ~2;
                sprite->clut = GetClut((texture->words[0] >> 5) & 0x3f0,
                                      texture->words[0] & 511);
                if (paletteMax < (int)(texture->words[0] & 511))
                    paletteMax = texture->words[0] & 511;
                page->length = 1;
                {
                    u32 word = texture->words[0];
                    page->command = (GetTPage(1, (word >> 22) & 3,
                                             (word >> 10) & 0x3c0,
                                             (word >> 7) & 256) & 0x9ff) | 0xe1000600;
                }
                texture++;
                position++;
                sprite++;
                page++;
            } while (++i < count);
        }
        buffer += count;
        pages += count;
    } while (++pass < 2);
    *end = pages;
    g_GeomScroll.state.position.otZ = paletteMax + 33;
    return 0;
}

/* The adjacent point command updates the same mesh-entry bounds and depth. */

int Render_TransformPoint(GeomEntry *entry, int x, int y, int depth) {
    GeomEntry *current;
    int screenX;
    int screenY;
    int minimum;
    int truncated;
    int sum;
    int bound;
    int comparison;
    unsigned int depthWord;
    unsigned int packed;
    unsigned int mask;

    current = entry;
    sum = current->base_x;
    screenY = current->base_y;
    depthWord = current->head.packed;
    sum += x;
    screenX = sum;
    /* Compare the wrapped signed coordinate while retaining the original
     * sum when it falls within the bounds. */
    depthWord >>= 20;
    depth += depthWord;

    bound = current->bounds.minX;
    minimum = bound;
    screenY += y;
    truncated = (short)sum;
    comparison = truncated < bound;
    if (!comparison) {
        bound = current->bounds.maxX;
        minimum = bound;
        comparison = bound < truncated;
    }
    screenX = comparison ? minimum : screenX;
    sum = (unsigned int)screenY << 16;
    bound = current->bounds.minY;
    truncated = sum >> 16;
    minimum = bound;
    comparison = truncated < bound;
    if (!comparison) {
        bound = current->bounds.maxY;
        minimum = bound;
        comparison = bound < truncated;
    }
    screenY = comparison ? minimum : screenY;
    current->scr_x = screenX;
    current->scr_y = screenY;
    mask = 0xFFF000FF;
    packed = current->head.packed & mask;
    /* Clamping is complete; reuse the scratch for the packed depth. */
    bound = depth;
    packed |= (bound & 0xFFF) << 8;
    current->head.packed = packed;
    return 0;
}
