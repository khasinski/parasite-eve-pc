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
 * PEW1 is little endian, with no native pointers or host structure padding. */
#define HD_WIDE_GROUPS 256
#define HD_WIDE_CELLS 4096
static struct {
    unsigned count;
    uint64_t key;
    int left, right;
    short *xy;
    RenderTilePacket *packets[2];
} hd_wide[HD_WIDE_GROUPS];

static void Render_ResetHdWidescreen(void) {
    hd_wide_trace_status=0;
    /* Queued ordering tables may still point at these two packet buffers. */
    for (unsigned i=0;i<HD_WIDE_GROUPS;++i)
        if(hd_wide[i].count){DrawSync(0);break;}
    for (unsigned i=0;i<HD_WIDE_GROUPS;++i) {
        free(hd_wide[i].xy);
        free(hd_wide[i].packets[0]);free(hd_wide[i].packets[1]);
    }
    memset(hd_wide,0,sizeof(hd_wide));
}

static int Render_ReadHdWide(SDL_IOStream *file, unsigned bytes, uint64_t *value) {
    *value=0;
    for (unsigned i=0;i<bytes;++i) {
        unsigned char byte;if(SDL_ReadIO(file,&byte,1)!=1)return 0;
        *value|=(uint64_t)(unsigned)byte<<(8*i);
    }
    return 1;
}

static void Render_LoadHdWidescreen(void) {
    hd_wide_trace_status=1;
    char filename[4096];
    int size=snprintf(filename,sizeof(filename),"%s/%016" PRIx64 "-wide.bin",hd_background_path,hd_geometry_key);
    if(size<0 || (size_t)size>=sizeof(filename))return;
    SDL_IOStream *file=SDL_IOFromFile(filename,"rb");if(!file)return;
    hd_wide_trace_status=2;
    Sint64 wide_bytes=SDL_GetIOSize(file);
    if(wide_bytes<8 || wide_bytes>8+HD_WIDE_GROUPS*32+HD_WIDE_CELLS*4){SDL_CloseIO(file);return;}
    uint64_t magic,groups,value;
    unsigned total=0;
    int valid=Render_ReadHdWide(file,4,&magic) && magic==0x31574550u &&
        Render_ReadHdWide(file,4,&groups) && groups<=HD_WIDE_GROUPS;
    for(unsigned i=0;valid && i<groups;++i) {
        uint64_t group,count,key,left,right,top,bottom;
        valid=Render_ReadHdWide(file,4,&group) && group<HD_WIDE_GROUPS &&
            !hd_wide[group].count && Render_ReadHdWide(file,4,&count) && count>0 && count<=HD_WIDE_CELLS-total &&
            Render_ReadHdWide(file,8,&key) && key && Render_ReadHdWide(file,4,&left) &&
            Render_ReadHdWide(file,4,&right) && Render_ReadHdWide(file,4,&top) && Render_ReadHdWide(file,4,&bottom);
        if(!valid)break;
        int l=(int32_t)(uint32_t)left,r=(int32_t)(uint32_t)right;
        int y0=(int32_t)(uint32_t)top,y1=(int32_t)(uint32_t)bottom;
        if(l < -2048 || r>4096 || l>=r || y0 < -2048 || y1>4096 || y0>=y1){valid=0;break;}
        total+=(unsigned)count;
        hd_wide[group].xy=malloc((size_t)count*2*sizeof(short));
        hd_wide[group].packets[0]=calloc((size_t)count,sizeof(RenderTilePacket));
        hd_wide[group].packets[1]=calloc((size_t)count,sizeof(RenderTilePacket));
        if(!hd_wide[group].xy || !hd_wide[group].packets[0] || !hd_wide[group].packets[1]){valid=0;break;}
        for(unsigned n=0;valid && n<count*2;++n) {
            valid=Render_ReadHdWide(file,2,&value);
            hd_wide[group].xy[n]=(int16_t)(uint16_t)value;
        }
        for(unsigned n=0;valid && n<count;++n) {
            int x=hd_wide[group].xy[n*2],y=hd_wide[group].xy[n*2+1];
            /* Bounds describe the visible span, not complete-cell extents.
             * Official side art overlaps the right edge; presenter crops it. */
            if(x<l || x>=r || y<y0 || y>=y1)valid=0;
        }
        if(!valid)break;
        int loaded=0;const char *formats[]={"webp","png","bmp"};
        for(unsigned format=0;format<3 && !loaded;++format) {
            size=snprintf(filename,sizeof(filename),"%s/%016" PRIx64 ".%s",hd_background_path,key,formats[format]);
            if(size>=0 && (size_t)size<sizeof(filename))loaded=Psyz_GpuLoadSpriteAtlas(key,filename)==1;
        }
        if(!loaded){valid=0;break;}
        for(unsigned n=0;n<count;++n)
            Psyz_GpuRegisterInterpolationBackgroundPair(&hd_wide[group].packets[0][n].r,&hd_wide[group].packets[1][n].r);
        hd_wide[group].count=(unsigned)count;hd_wide[group].key=key;
        hd_wide[group].left=l;hd_wide[group].right=r;
    }
    unsigned char trailing;if(valid && SDL_ReadIO(file,&trailing,1)!=0)valid=0;
    SDL_CloseIO(file);
    if(!valid){Render_ResetHdWidescreen();hd_wide_trace_status=2;Psyz_GpuResetSpriteAtlases();fprintf(stderr,"Ignoring incomplete HD widescreen geometry %016" PRIx64 "\n",hd_geometry_key);}
    else hd_wide_trace_status=3;
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

void Render_LinkHdWidescreenBackdrop(int slot,RenderNativeTag *ordering) {
    int left,right;
    if(slot<0 || slot>1 || !Render_GetHdWidescreenPicture(&left,&right))return;
    unsigned group=g_GeomScroll.state.position.groupSelect;
    const GeomEntry *entries=(const void *)((const u8 *)g_GeomState+g_GeomState->entry_offset);
    const RenderTilePacket *color=NULL;
    /* Original fades/tints update these packets, not the source artwork. */
    for(unsigned n=0;n<g_GeomState->entry_count06;++n) {
        if(entries[n].group==group && (entries[n].head.b.flags&2) && entries[n].prim_count && entries[n].u30.prim) {
            color=(const RenderTilePacket *)entries[n].u30.prim+slot*entries[n].prim_count;break;
        }
    }
    for(unsigned i=0;i<hd_wide[group].count;++i) {
        Psyz_GpuEnsureInterpolationBackgroundPair(&hd_wide[group].packets[0][i].r,&hd_wide[group].packets[1][i].r);
        RenderTilePacket *sprite=&hd_wide[group].packets[slot][i];
        int x=hd_wide[group].xy[i*2]+(short)g_GeomState->out_disp_x;
        int y=hd_wide[group].xy[i*2+1]+(short)g_GeomState->out_disp_y;
        if(x+16 < -112 || x>432 || y+16<0 || y>240)continue;
        sprite->length=7;sprite->hd_command=0xF8800000u|i;
        sprite->hd_key_low=(u32)hd_wide[group].key;sprite->hd_key_high=(u32)(hd_wide[group].key>>32);
        sprite->packed_marker=0x005A4D50u;sprite->code=0x7c;
        sprite->r=color?color->r:128;sprite->g=color?color->g:128;sprite->b=color?color->b:128;
        sprite->x=(u16)x;sprite->y=(u16)y;
        sprite->address=ordering[4095].address;ordering[4095].address=(uintptr_t)sprite;
    }
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
