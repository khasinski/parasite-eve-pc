/* Real menu adapters and inventory renderer, with only drawing/assets isolated. */
#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "pe1/aya.h"
#include "pe1/inventory_queries.h"
#include "pe1/inventory_state.h"
#include "pe1/menu_item_rows.h"
#include "pe1/menu_memcard_display.h"
#include "pe1/save.h"
#include "pe1/psyq_cd.h"
#include "pe1/text.h"
#include "pe1/draw_state.h"
#include "pe1/mod_text.h"
#include "pe1/bounds_check.h"
#include "pe1/field_bg_load.h"
#include "pe1/textbox.h"
#include "pe_hooks.h"

void Menu_DrawArmorEquipItem(int index);

_Static_assert(_Generic(&Inv_GetActiveListItem, short (*)(int):1, default:0),
               "Active-list values must be sign-extended from the declared return type");
_Static_assert(_Generic(((MenuWidgetNode *)0)->draw,
               void (*)(MenuWidgetNode *):1, default:0), "Widget draw callback needs its node");
_Static_assert(_Generic(&MemCard_DrawProgress,
               void (*)(MenuWidgetNode *):1, default:0), "Progress callback needs its node");

SaveRuntimeState g_SaveRuntimeState;
InventoryItemWorkspace g_InventoryItemWorkspace;
int g_InvActiveListCount;
static ItemDataRecord bases[255], *drawn;
static unsigned draws;
static u8 names[255];
ItemDataRecord *Item_LookupBaseData(unsigned index) {
    assert(index<255);return &bases[index];
}
void *Text_LookupTable8(unsigned index) { assert(index<255);return &names[index]; }
void Menu_DrawSlotRow(ItemDataRecord *record,u8 *text) {
    assert(record && text);drawn=record;++draws;
}

static void row_indices(void) {
    g_InvActiveListCount=10;
    for(int i=0;i<10;++i) {
        g_InvActiveListItems[i]=(s16)(0x100+i);
        g_InventoryRuntime.equipment[i].itemId=(u8)(i+1);
    }
    for(int i=0;i<10;++i) {
        drawn=NULL;Menu_DrawArmorEquipItem(i);
        assert(drawn==&g_InventoryRuntime.equipment[i]);
    }
    unsigned before=draws;
    for(unsigned i=0;i<4;++i) {
        int index=(int[]){-1,10,INT_MIN,INT_MAX}[i];
        Inv_ShowItemDescriptionPanel(index);
        Menu_DrawArmorEquipItem(index);
    }
    assert(draws==before);
    g_InvActiveListCount=2;Menu_DrawArmorEquipItem(2);assert(draws==before);
    g_InvActiveListCount=10;
    for(int id=0x200;id<=0x208;++id) {
        g_KeyItemRecords[id-0x200].itemId=1;
        g_InvActiveListItems[3]=(s16)id;
        Menu_DrawArmorEquipItem(3);
        assert(drawn==&g_KeyItemRecords[id-0x200]);
    }
    before=draws;
    for(int id=0x209;id<=SHRT_MAX;++id) {
        g_InvActiveListItems[3]=(s16)id;Menu_DrawArmorEquipItem(3);
    }
    assert(draws==before);
}

static void pointer_identity(void) {
    MenuWidgetNode first={0},second={0};
    uintptr_t low=0x1234;
    uintptr_t high=sizeof(uintptr_t)>4 ? (uintptr_t)UINT64_C(0x100000000) : 0;
    MenuWidgetNode *one=(MenuWidgetNode *)(high|low);
    MenuWidgetNode *two=(MenuWidgetNode *)((high*2)|low);
    first.children[0]=one;first.next=&second;second.children[0]=two;
    g_MenuWidgetActiveListHead=&first;
    assert(MenuWidget_FindByFieldValue(one)==&first);
    if(high)assert(MenuWidget_FindByFieldValue(two)==&second);
    assert(MenuWidget_FindByFieldValue((MenuWidgetNode *)(high|0x5678))==NULL);
    g_MenuWidgetActiveListHead=NULL;
}
/* Real native formatters; only the font metrics, table data and optional
 * glyph provider are synthetic. Compare actual packet bytes and cursor state. */
u8 *g_DrawPacketCursor,*g_DrawPacketArenaBase;
u32 g_DrawPrimColor,g_DrawColorShaded,*g_DrawOrderingTableEntry;
int g_DrawTextDimmed;
/* The text renderer's dialogue state (textbox.c, text/tables.c); menus only read the font page. */
unsigned char g_TextboxKernGlyph0F;
static const TextboxGlyphSpacing s_textbox_widths[78];
const TextboxGlyphSpacing *g_TextboxGlyphWidths = s_textbox_widths;
unsigned g_TextboxGlyphCount = 78;
/* The USA release unless a case sets the Japanese one (pe1/pe_image.h). */
u8 g_GameRegion;
const TextboxGlyphSpacing g_TextboxGlyphSpacingJapanese[60];
FieldFontTextureState g_FieldFontTextureState;
extern int g_TextCursorStackBottom[28];
static u8 *format_text;
static int extended_provider;
static unsigned glyph_draws;
void *Text_LookupTable4(unsigned index) { assert(index==7);return format_text; }
DrawGlyphDescriptor *Draw_LookupGlyphDescriptor(int index) {
    static DrawGlyphDescriptor descriptor={0,0,0x89C,12,12,0,0};
    assert(index==0x77);return &descriptor;
}
int Draw_LookupGlyphMetrics(int glyph) {
    assert((unsigned)glyph<441);return ((5+glyph%5)<<4)|(glyph%3);
}
void Inv_BoundsCheckAssertStub(int code,...) { (void)code;assert(!"Unexpected text cursor/packet overflow"); }
int pe_hook_dispatch(int id,void *ctx) {
    if(id!=PE_HOOK_TEXT_GLYPH || !extended_provider)return 0;
    PeHookTextGlyph *g=ctx;unsigned index;
    assert(Pe_ExtendedGlyph(g->text,&index));
    g->bytes=4;g->advance=12;g->u=(u8)(index%21*12);g->v=(u8)(index/21%21*12);
    glyph_draws+=g->draw!=0;return 1;
}
static union { uintptr_t alignment;u8 bytes[0x4000]; } text_packets;
static RenderNativeTag text_order;
static void reset_text(int mode,int dimmed) {
    memset(&text_packets,0,sizeof text_packets);memset(g_TextCursorStackBottom,0,sizeof g_TextCursorStackBottom);
    text_order.address=0;g_DrawOrderingTableEntry=(u32 *)&text_order;
    g_DrawPacketArenaBase=g_DrawPacketCursor=text_packets.bytes;
    g_TextCursorStackPtr=g_TextCursorStackBottom;g_TextCursorX=23;g_TextCursorY=31;
    g_TextRenderMode=mode;g_DrawTextBoxWidth=100;g_DrawTextDimmed=dimmed;
    g_DrawPrimColor=0x808080;g_DrawColorShaded=0x404040;glyph_draws=0;
}
static uint32_t native_text_digest(void) {
    uint32_t hash=2166136261u;
    for(u8 *at=text_packets.bytes;at<g_DrawPacketCursor;at+=sizeof(RenderTexturedQuad)){
        RenderTexturedQuad *p=(RenderTexturedQuad *)at;assert(p->tag.length==9);
        const u8 *payload=(const u8 *)&p->color;
        for(unsigned i=0;i<36;++i)hash=(hash^payload[i])*16777619u;
    }
    return hash;
}
static void centered_text(void) {
    static u8 ordinary[]={0x10,0x31,0x0F,0x02,0xFF};
    static u8 pages[]={0x10,0xFB,0x11,0xFA,0x02,0xFB,0x00,0xFF};
    static u8 empty[]={0xFF};
    static u8 cjk[]={0xF1,0xC5,0,0,0xF1,0xC5,2,95,0xFF};
    static u8 mixed[]={0x10,0xF1,0xC5,0,0,0x02,0xF1,0xC5,2,95,0xFF};
    u8 corpus[251];for(unsigned i=0;i<250;++i)corpus[i]=(u8)i;corpus[250]=0xFF;
    u8 *cases[]={ordinary,pages,empty,corpus,cjk,mixed};
    for(unsigned provider=0;provider<2;++provider)for(unsigned sample=0;sample<6;++sample)
    for(unsigned dimmed=0;dimmed<2;++dimmed)
    for(unsigned seed=0;seed<(sample<4?2u:1u);++seed){
        extended_provider=provider;format_text=cases[sample];reset_text(seed,dimmed);
        Draw_FlushPrimList(7);
        assert(g_TextCursorX==23 && g_TextCursorY==31 && g_TextCursorStackPtr==g_TextCursorStackBottom);
        /* GPU payload hashes recorded from the previous native renderer.
         * Addresses and host tag sizes are intentionally excluded. */
        if(sample==3){
            static const uint32_t native_golden[]={0x1127b771u,0xe165dcf1u};
            assert(native_text_digest()==native_golden[dimmed]);
        }
        size_t bytes=(size_t)(g_DrawPacketCursor-g_DrawPacketArenaBase);
        u8 actual[0x4000];memcpy(actual,text_packets.bytes,bytes);
        int stack[28];memcpy(stack,g_TextCursorStackBottom,sizeof stack);
        unsigned actual_glyphs=glyph_draws;
        reset_text(seed,dimmed);Draw_PrintCenteredText(format_text);
        if(bytes!=(size_t)(g_DrawPacketCursor-g_DrawPacketArenaBase))
            fprintf(stderr,"Centered mismatch: provider=%u sample=%u dimmed=%u mode=%u\n",provider,sample,dimmed,seed);
        assert(bytes==(size_t)(g_DrawPacketCursor-g_DrawPacketArenaBase));
        assert(!memcmp(actual,text_packets.bytes,bytes));
        assert(!memcmp(stack,g_TextCursorStackBottom,sizeof stack));
        assert(g_TextCursorX==23 && g_TextCursorY==31 && g_TextCursorStackPtr==g_TextCursorStackBottom);
        if(provider && sample>=4)assert(actual_glyphs==2);
    }
    extended_provider=1;format_text=cjk;reset_text(0,1);Draw_FlushPrimList(7);
    assert(glyph_draws==2 && g_DrawPacketCursor-g_DrawPacketArenaBase==2*sizeof(RenderTexturedQuad));
    RenderTexturedQuad *p=(RenderTexturedQuad *)text_packets.bytes;
    assert(p[0].x0==59 && p[0].x1==71 && p[1].x0==71 && p[1].x1==83);
    assert(p[0].y0==32 && p[0].y1==32 && p[0].clut==0x89C);
    assert(p[0].u0==0 && p[0].v0==0 && p[1].u0==(575%21)*12 && p[1].v0==(575/21%21)*12);
    assert(p[0].color.word==0x2C404040 && p[1].color.word==0x2C404040);
    puts("Centered labels: native and CJK packet bytes, dimensions, palette, dimming and cursor restoration passed");
}

int main(void) {
    row_indices();pointer_identity();centered_text();
    /* The image-backed transfer is synchronous, even for polling callers. */
    assert(DsDataSync(0)==0 && DsDataSync(1)==0 && DsDataSync(-1)==0);
    assert(DsGetSector(NULL,8)==0 && DsGetSector2(NULL,8)==0);
    u32 sector[588];
    assert(DsGetSector(sector,-1)==0 && DsGetSector2(sector,INT_MAX)==0);
    puts("Menu ABI: row forwarding, bounds, key IDs and full-width child pointers passed");
    return 0;
}
