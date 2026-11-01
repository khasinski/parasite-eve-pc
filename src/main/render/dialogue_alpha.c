/* SPDX-License-Identifier: MIT -- original game-side queued glyph adapter.
 * Font: retained IBM Plex OFL1.1; rasterizer: retained stb_truetype license.
 * These dependencies retain their notices; not relicensed by this header. */
#include "pe1/dialogue_alpha.h"
#include "pe1/psyq_gpu.h"
#include "pe1/render_buffers.h"
#include "pe1/draw_buffers.h"
#include "pe1/draw_state.h"
#include "pe1/render_prim.h"
#include <psyz/gpu.h>
#include <psyz/video.h>
#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#include "fonts/ibm_plex_sans.ttf.inc"
extern int g_ActiveDrawSlot;
enum{FONT_W=416,FONT_H=520,FONT_CELL=52,FONT_BYTES=FONT_W*FONT_H*4};
typedef struct{RenderNativeTag tag;uintptr_t command;}DialogueMarker;
static DialogueMarker markers[2][TEXTBOX_GLYPH_PACKET_CAPACITY];
typedef struct{
    int enabled,ready,ever_tagged;
    uint64_t font_hash,bank_generation,seen_generation;
    unsigned bank_page,bank_clut;
    int bank_notified,reference_valid,seen_compatible,gpu_reference_valid;
    unsigned short reference_texels[64*48],gpu_texels[64*48],gpu_palette[16];
    unsigned logs;unsigned char supported[78];
} NativeFontContext;
static NativeFontContext font_contexts[2];
static int registered;
static int menu_source_open;
static unsigned short menu_source_words[64*48];
static unsigned char menu_source_known[64*48];
void PeMenuFont_SourceBegin(void){menu_source_open=1;memset(menu_source_known,0,sizeof menu_source_known);}
void PeMenuFont_SourceUpload(const RECT *rect,const void *pixels){
    if(!menu_source_open||!rect||!pixels||rect->w<=0||rect->h<=0)return;
    int left=rect->x>320?rect->x:320,right=rect->x+rect->w<384?rect->x+rect->w:384;
    int top=rect->y>0?rect->y:0,bottom=rect->y+rect->h<48?rect->y+rect->h:48;
    const unsigned char *src=pixels;
    for(int y=top;y<bottom;y++)for(int x=left;x<right;x++){
        size_t offset=((size_t)(y-rect->y)*rect->w+x-rect->x)*2;unsigned dest=(unsigned)y*64+x-320;
        menu_source_words[dest]=(unsigned short)((unsigned)src[offset]|(unsigned)src[offset+1]<<8);menu_source_known[dest]=1;
    }
}
/* Exact native draw arena offset, not packet-size indexing: mixed FT4/G4/
 * state packets share each arena. Markers remain live as long as that same
 * original arena slot; Draw_SelectBuffer clears its OT before normal reuse.
 * GPU_Enqueue copies marker+original synchronously, no renderer heap address
 * is serialized and no linked marker is zeroed during checkpoint restore. */
static DialogueMarker menu_markers[2][0x4000/sizeof(uintptr_t)];
static TextboxFontPage FontPage(unsigned context){
    if(!context)return g_TextboxFontPages[0];
    /* Modern menu presentation deliberately uses the canonical font-TIM
     * grayscale palette, NOT the original FT4 CLUT0x089c. Its source
     * packet/geometry/tint remain unchanged and Original is byte-exact. */
    TextboxFontPage page=g_TextboxFontPages[0];page.tpage=5;return page;
}
void PeFont_SetEnabled(unsigned context,int on){if(context>=2)return;NativeFontContext *f=&font_contexts[context];on=on!=0;if(on!=f->enabled){f->logs=0;f->seen_generation=0;}f->enabled=on;Psyz_GpuSetFontContextEnabled(context,f->enabled);if(!f->enabled)f->ready=0;}
int PeFont_GetEnabled(unsigned context){if(context>=2)return 0;NativeFontContext *f=&font_contexts[context];return f->enabled;}
int PeFont_StateSafe(unsigned context){if(context>=2)return 0;NativeFontContext *f=&font_contexts[context];return f->enabled>=0&&f->enabled<=1;}
int PeFont_GetEffective(unsigned context){if(context>=2)return 0;NativeFontContext *f=&font_contexts[context];return f->enabled&&f->ready&&Psyz_GpuHasFontContext(context)&&Psyz_VideoGetInternalResolution()>1;}
static int ascii(unsigned code){if(code<10)return '0'+code;if(code>=0x10&&code<=0x29)return 'A'+code-0x10;if(code>=0x30&&code<=0x49)return 'a'+code-0x30;if(code>=0xa&&code<=0xf)return "+-=*% "[code-0xa];if(code>=0x2a&&code<=0x2f)return "&!?\"',"[code-0x2a];if(code>=0x4a&&code<=0x4d)return ".:;/"[code-0x4a];return -1;}
static int command(const u_long *words,int available,void *unused){(void)unused;if(available<1)return -1;unsigned bits=(unsigned)words[0];if((bits&0xfffe0000u)!=0xf9000000u){Psyz_GpuSelectFontGlyph(2,0,0,0);return 1;}Psyz_GpuSelectFontGlyph((bits>>8)&1u,bits&255u,(bits>>9)&15u,((bits>>13)&15u)+1);return 1;}
static uint64_t hash(uint64_t h,const void *data,size_t bytes){const unsigned char *s=data;for(size_t i=0;i<bytes;i++)h=(h^s[i])*UINT64_C(1099511628211);return h;}
void PeDialogue_FontBankLoaded(void){for(unsigned context=0;context<2;context++){
    NativeFontContext *f=&font_contexts[context];TextboxFontPage descriptor=FontPage(context);const TextboxFontPage *page=&descriptor;
    f->bank_page=page->tpage;f->bank_clut=page->clut;
    f->bank_generation=Psyz_GpuWatchFontContext(context,f->bank_page,f->bank_clut);f->bank_notified=f->bank_generation!=0;
    f->reference_valid=f->seen_compatible=f->ready=0;f->seen_generation=0;
    if(context==1&&menu_source_open){unsigned known=0;for(unsigned i=0;i<64*48;i++)known+=menu_source_known[i]!=0;if(known==64*48){memcpy(f->reference_texels,menu_source_words,sizeof menu_source_words);f->reference_valid=1;}}
}menu_source_open=0;}

void PeFont_Prepare(unsigned context){if(context>=2)return;NativeFontContext *f=&font_contexts[context];
    if(!f->enabled)return;
    f->ready=0;if(!context&&(unsigned)g_ActiveDrawSlot>=2)return;
    if(Psyz_VideoGetInternalResolution()<2){if(f->logs++<8)SDL_Log("[Native font] requires-render-scale-at-least2 original-fallback=1");return;}
    if(!registered){if(Psyz_GpuRegisterCommandHandlerIfVacant(0xf9,command,NULL)<0){if(f->logs++<8)SDL_Log("[Native font] opcode-occupied original-fallback=1");return;}registered=1;}
    TextboxFontPage descriptor=FontPage(context);const TextboxFontPage *page=&descriptor;
    if((page->tpage&0x180)!=0||!page->clut){if(f->logs++<8)SDL_Log("[Native font] original-font-page-unavailable original-fallback=1");return;}
    if(!f->bank_notified||f->bank_page!=page->tpage||f->bank_clut!=page->clut){if(f->logs++<8)SDL_Log("[Native font] source-font-bank-not-confirmed original-fallback=1");return;}
    if(context==1&&!f->reference_valid){if(f->logs++<8)SDL_Log("[Native font] context=1 source-alphabet-incomplete original-fallback=1");return;}
    uint64_t generation=Psyz_GpuFontContextGeneration(context,page->tpage,page->clut);if(!generation)return;
    if(generation==f->seen_generation){if(!f->seen_compatible)return;if(Psyz_GpuHasFontContext(context)){f->ready=1;return;}}
    /* A source-authorized upload must still be intact at first observation.
     * Arbitrary later VRAM writes never establish a new character mapping. */
    if(!f->reference_valid&&generation!=f->bank_generation){f->seen_generation=generation;f->seen_compatible=0;if(f->logs++<8)SDL_Log("[Native font] font-changed-before-source-reference original-fallback=1");return;}
    /* Actual original font + palette, not inferred widths or guessed glyphs.
     * Readback occurs only on observed region changes; prototype defaults OFF. */
    unsigned short texels[64*48],palette[16];
    if(!Psyz_GpuReadDialogueFont(page->tpage,page->clut,texels,palette)){if(f->logs++<8)SDL_Log("[Native font] native-font-readback-failed original-fallback=1");return;}
    if(!f->reference_valid){memcpy(f->reference_texels,texels,sizeof texels);f->reference_valid=1;}
    else if(memcmp(f->reference_texels,texels,sizeof texels)){f->seen_generation=generation;f->seen_compatible=0;if(f->logs++<8)SDL_Log("[Native font] original-glyph-bytes-modified original-fallback=1");return;}
    if(palette[0]){f->seen_generation=generation;f->seen_compatible=0;if(f->logs++<8)SDL_Log("[Native font] context=%u palette-zero-not-transparent original-fallback=1",context);return;}
    uint64_t h=hash(hash(UINT64_C(14695981039346656037),texels,sizeof texels),palette,sizeof palette);
    if(f->gpu_reference_valid&&!memcmp(f->gpu_texels,texels,sizeof texels)&&!memcmp(f->gpu_palette,palette,sizeof palette)&&Psyz_GpuHasFontContext(context)){f->seen_generation=generation;f->seen_compatible=f->ready=1;return;}
    unsigned char *rgba=calloc(1,FONT_BYTES);if(!rgba)return;
    stbtt_fontinfo info;int offset=stbtt_GetFontOffsetForIndex(ibm_plex_sans_ttf,0);
    if(offset<0||!stbtt_InitFont(&info,ibm_plex_sans_ttf,offset)){free(rgba);return;}
    unsigned char valid[78]={0};unsigned count=0;
    for(unsigned code=0;code<78;code++){
        int cp=ascii(code);if(cp<0)continue;
        int minx=12,miny=12,maxx=-1,maxy=-1,peak=0,dark=31,colored=0;
        for(int y=0;y<12;y++)for(int x=0;x<12;x++){
            unsigned u=(code%21)*12+x,v=(code/21)*12+y;
            unsigned index=(texels[v*64+u/4]>>((u&3)*4))&15;
            if(palette[index]){int r=palette[index]&31,g=(palette[index]>>5)&31,b=(palette[index]>>10)&31;if(r!=g||g!=b)colored=1;if(r>peak)peak=r;if(r<dark)dark=r;if(x<minx)minx=x;if(x>maxx)maxx=x;if(y<miny)miny=y;if(y>maxy)maxy=y;}
        }
        if(colored||maxx<minx||maxy<miny)continue;
        int bw,bh,x0,y0,x1,y1;float scale=stbtt_ScaleForPixelHeight(&info,48);
        stbtt_GetCodepointBitmapBox(&info,cp,scale,scale,&x0,&y0,&x1,&y1);bw=x1-x0;bh=y1-y0;if(bw<=0||bh<=0)continue;
        /* Keep the dark contour inside the original glyph coverage box. No
         * glyph advance, packet geometry, palette, or marker change. */
        const int contour=2,boxw=(maxx-minx+1)*4,boxh=(maxy-miny+1)*4;
        if(boxw<=2*contour||boxh<=2*contour)continue;
        float fitx=(float)(boxw-2*contour)/bw,fity=(float)(boxh-2*contour)/bh;scale*=(fitx<fity?fitx:fity)*0.98f;
        unsigned char *bitmap=stbtt_GetCodepointBitmap(&info,scale,scale,cp,&bw,&bh,&x0,&y0);if(!bitmap)continue;
        if(bw>0&&bh>0&&bw<=(maxx-minx+1)*4&&bh<=(maxy-miny+1)*4){
            int left=(code%8)*FONT_CELL+2+minx*4+((maxx-minx+1)*4-bw)/2;
            int top=(code/8)*FONT_CELL+2+(maxy+1)*4-bh-contour;
            /* The authored font has dark coverage as well as bright ink.
             * Recreate its dark contour around the new alpha, then composite
             * the ink in straight RGBA (shader premultiplies exactly once).
             * Opaque dark outline remains dark under per-vertex tint. */
            unsigned char shade=(unsigned char)((dark<peak/4?dark:0)*255/31);
            for(int y=-contour;y<bh+contour;y++)for(int x=-contour;x<bw+contour;x++){
                unsigned coverage=0;
                for(int dy=-contour;dy<=contour;dy++)for(int dx=-contour;dx<=contour;dx++){
                    int sx=x+dx,sy=y+dy;if(sx>=0&&sx<bw&&sy>=0&&sy<bh&&bitmap[sy*bw+sx]>coverage)coverage=bitmap[sy*bw+sx];
                }
                unsigned char *d=rgba+((top+y)*FONT_W+left+x)*4;
                d[0]=d[1]=d[2]=shade;d[3]=(unsigned char)coverage;
            }
            for(int y=0;y<bh;y++)for(int x=0;x<bw;x++){unsigned char *d=rgba+((top+y)*FONT_W+left+x)*4;unsigned ink=bitmap[y*bw+x],back=d[3];
                unsigned alpha=ink+(back*(255-ink)+127)/255;
                unsigned value=alpha?((unsigned)(peak*255/31)*ink+(unsigned)d[0]*back*(255-ink)/255+alpha/2)/alpha:0;
                d[0]=d[1]=d[2]=(unsigned char)value;d[3]=(unsigned char)alpha;}
            valid[code]=1;count++;
        }
        stbtt_FreeBitmap(bitmap,NULL);
    }
    f->seen_generation=generation;f->seen_compatible=0;
    if(count&&Psyz_GpuLoadFontContext(context,rgba,FONT_BYTES)){f->font_hash=h;memcpy(f->gpu_texels,texels,sizeof texels);memcpy(f->gpu_palette,palette,sizeof palette);f->gpu_reference_valid=1;memcpy(f->supported,valid,sizeof valid);f->seen_compatible=f->ready=1;}
    free(rgba);if(f->logs++<8)SDL_Log("[Native font] context=%u prepared-glyphs=%u effective-resource=%d original-layout=preserved source-alpha-box=actual-vram dark-contour=2-at-4x layout-unchanged=1 reference-hash=%016llx rendered-pixels=unobserved modern-menu-palette=canonical-font-tim original-ft4-clut=unchanged",context,count,f->ready,(unsigned long long)f->font_hash);
}
void PeDialogue_Tag(RenderNativeTag *ordering,TextboxGlyphPacket *packet,unsigned code){unsigned context=0;if(context>=2)return;NativeFontContext *f=&font_contexts[context];
    if(!f->enabled||!f->ready||code>=78||!f->supported[code]||(unsigned)g_ActiveDrawSlot>=2||!ordering||!packet)return;
    uintptr_t start=(uintptr_t)D_800B0E38.other_buffers[1+g_ActiveDrawSlot],ptr=(uintptr_t)packet;
    if(!start||ptr<start||(ptr-start)%sizeof(*packet)||(ptr-start)/sizeof(*packet)>=TEXTBOX_GLYPH_PACKET_CAPACITY)return;
    if(ordering->address!=ptr||packet->sprite.width!=12||packet->sprite.height!=12||packet->sprite.color.bytes.code!=0x64||packet->sprite.clut!=g_TextboxFontPages[0].clut)return;
    /* One immutable marker per actual cached glyph packet/slot: same lifetime
     * as its original payload. GPU_Enqueue copies both synchronously before
     * the game can recycle the slot. No arena allocation or original removal. */
    DialogueMarker *m=&markers[g_ActiveDrawSlot][(ptr-start)/sizeof(*packet)];m->tag.address=ptr;m->tag.length=1;m->command=0xf9000000u|(11u<<13)|code;ordering->address=(uintptr_t)m;f->ever_tagged=1;
}

#if PE_NATIVE_STATE_CPU_COMPONENTS
#include <psyz/state_gpu.h>
static uint32_t DialogueStateGet(const unsigned char *p){return(uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void DialogueStatePut(unsigned char *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(v>>(i*8));}
int PeFont_StateValidate(unsigned context,const unsigned char *in,size_t bytes){if(context>=2)return 0;NativeFontContext *f=&font_contexts[context];
    if(menu_source_open||!in||bytes!=PE_FONT_CONTEXT_STATE_BYTES||memcmp(in,"PEDIAL01",8)||!Psyz_GpuDialogueStateIdle()||DialogueStateGet(in+8)!=(unsigned)f->enabled)return 0;
    unsigned notified=DialogueStateGet(in+12),reference=DialogueStateGet(in+16),first=DialogueStateGet(in+20),page=DialogueStateGet(in+24),clut=DialogueStateGet(in+28);
    if((context==1&&first)||notified>1||reference>1||first>1||(reference&&!notified)||(first&&(!notified||reference)))return 0;
    for(unsigned i=32;i<64;i++)if(in[i])return 0;
    if(notified){if(page>65535||(page&0x180)||clut>32767||page!=FontPage(context).tpage||clut!=FontPage(context).clut)return 0;}
    else if(page||clut)return 0;
    if(!reference)for(unsigned i=64;i<PE_FONT_CONTEXT_STATE_BYTES;i++)if(in[i])return 0;
    return 1;
}
int PeFont_StateCapture(unsigned context,unsigned char *out,size_t bytes){if(context>=2)return 0;NativeFontContext *f=&font_contexts[context];
    if(menu_source_open||!out||bytes!=PE_FONT_CONTEXT_STATE_BYTES||!Psyz_GpuDialogueStateIdle())return 0;
    memset(out,0,bytes);memcpy(out,"PEDIAL01",8);DialogueStatePut(out+8,(unsigned)f->enabled);
    DialogueStatePut(out+12,(unsigned)f->bank_notified);DialogueStatePut(out+16,(unsigned)f->reference_valid);
    if(f->bank_notified){
        /* A new reference may only be authorized by the actual source load,
         * still intact BEFORE capture. Never reinterpret mutated VRAM as text. */
        uint64_t generation=Psyz_GpuFontContextGeneration(context,f->bank_page,f->bank_clut);if(!generation)return 0;
        DialogueStatePut(out+20,context==0&&!f->reference_valid&&generation==f->bank_generation);
        DialogueStatePut(out+24,f->bank_page);DialogueStatePut(out+28,f->bank_clut);
    }
    if(f->reference_valid)for(unsigned i=0;i<64u*48u;i++){out[64+i*2]=(unsigned char)f->reference_texels[i];out[65+i*2]=(unsigned char)(f->reference_texels[i]>>8);}
    return PeFont_StateValidate(context,out,bytes);
}
int PeFont_StateRestore(unsigned context,const unsigned char *in,size_t bytes){if(context>=2)return 0;NativeFontContext *f=&font_contexts[context];
    if(!PeFont_StateValidate(context,in,bytes))return 0;
    unsigned notified=DialogueStateGet(in+12),reference=DialogueStateGet(in+16),first=DialogueStateGet(in+20),page=DialogueStateGet(in+24),clut=DialogueStateGet(in+28);
    uint64_t generation=notified?Psyz_GpuWatchFontContext(context,page,clut):0;if(notified&&!generation)return 0;
    f->bank_notified=(int)notified;f->reference_valid=(int)reference;f->bank_page=page;f->bank_clut=clut;
    f->bank_generation=first?generation:0;
    for(unsigned i=0;i<64u*48u;i++)f->reference_texels[i]=(unsigned short)((unsigned)in[64+i*2]|(unsigned)in[65+i*2]<<8);
    /* No OT/marker pointer is serialized or cleared. Existing markers retain
     * their original packet links; next source draw rewrites each used slot.
     * GPU selection is invalid until this restored alphabet/palette rebuilds. */
    f->ready=f->seen_compatible=f->gpu_reference_valid=0;f->seen_generation=0;f->font_hash=0;memset(f->supported,0,sizeof f->supported);
    return 1;
}
#endif

void PeDialogue_SetEnabled(int on){PeFont_SetEnabled(0,on);}
int PeDialogue_GetEnabled(void){return PeFont_GetEnabled(0);}
int PeDialogue_StateSafe(void){return !menu_source_open&&PeFont_StateSafe(0)&&PeFont_StateSafe(1);}
int PeDialogue_GetEffective(void){return PeFont_GetEffective(0);}
void PeDialogue_Prepare(void){PeFont_Prepare(0);}
void PeMenuFont_SetEnabled(int on){PeFont_SetEnabled(1,on);}
int PeMenuFont_GetEnabled(void){return PeFont_GetEnabled(1);}
int PeMenuFont_GetEffective(void){return PeFont_GetEffective(1);}
void PeMenuFont_Prepare(void){PeFont_Prepare(1);}
void PeMenuFont_Tag(RenderNativeTag *ordering,RenderTexturedQuad *packet,unsigned code,unsigned crop,unsigned width){
    NativeFontContext *f=&font_contexts[1];
    if(!g_DrawPresentEnabled||!f->enabled||!f->ready||code>=78||!f->supported[code]||crop>=12||!width||width>12-crop||!ordering||!packet)return;
    uintptr_t ptr=(uintptr_t)packet,base=(uintptr_t)g_DrawPacketArenaBase;int slot=-1;
    for(int i=0;i<2;i++)if(base&&(uintptr_t)g_DrawFrameBuffers[i].frontBufferBase==base)slot=i;
    if(slot<0||ptr<base||ptr-base>=0x4000-sizeof(*packet)||(ptr-base)%sizeof(uintptr_t)||ordering->address!=ptr)return;
    if(packet->tag.length!=9||packet->color.bytes.code!=0x2c||packet->tpage!=5||packet->clut!=0x089c||packet->u0!=(code%21)*12+crop||packet->v0!=(code/21)*12||packet->x1-packet->x0!=width||packet->y2-packet->y0!=12)return;
    DialogueMarker *m=&menu_markers[slot][(ptr-base)/sizeof(uintptr_t)];
    m->tag.address=ptr;m->tag.length=1;m->command=0xf9000000u|code|(1u<<8)|(crop<<9)|((width-1)<<13);ordering->address=(uintptr_t)m;f->ever_tagged=1;
}
#if PE_NATIVE_STATE_CPU_COMPONENTS
int PeDialogue_StateValidate(const unsigned char *in,size_t bytes){
 if(!in||bytes!=PE_DIALOGUE_STATE_BYTES||memcmp(in,"PEDIAL02",8)||in[8]!=2)return 0;
 for(unsigned i=9;i<16;i++)if(in[i])return 0;
 return PeFont_StateValidate(0,in+16,PE_FONT_CONTEXT_STATE_BYTES)&&PeFont_StateValidate(1,in+16+PE_FONT_CONTEXT_STATE_BYTES,PE_FONT_CONTEXT_STATE_BYTES);
}
int PeDialogue_StateCapture(unsigned char *out,size_t bytes){
 if(!out||bytes!=PE_DIALOGUE_STATE_BYTES)return 0;memset(out,0,16);memcpy(out,"PEDIAL02",8);out[8]=2;
 return PeFont_StateCapture(0,out+16,PE_FONT_CONTEXT_STATE_BYTES)&&PeFont_StateCapture(1,out+16+PE_FONT_CONTEXT_STATE_BYTES,PE_FONT_CONTEXT_STATE_BYTES)&&PeDialogue_StateValidate(out,bytes);
}
int PeDialogue_StateRestore(const unsigned char *in,size_t bytes){
 /* Validate BOTH source authorities/preferences before any mutation. Reset
  * both GPU contexts ONCE, then rebind each to its own fresh watcher token. */
 if(!PeDialogue_StateValidate(in,bytes)||!Psyz_StateDialogueReset())return 0;
 return PeFont_StateRestore(0,in+16,PE_FONT_CONTEXT_STATE_BYTES)&&PeFont_StateRestore(1,in+16+PE_FONT_CONTEXT_STATE_BYTES,PE_FONT_CONTEXT_STATE_BYTES);
}
#endif
