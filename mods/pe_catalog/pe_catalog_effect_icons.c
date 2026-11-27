#include "pe_catalog_effect_icons.h"
#include "pe_game.h"
#include "pe_main_menu.h"
#include "pe_gametext.h"
#include "pe1/draw_state.h"

void pe_catalog_effect_icon(int which, int effect, int offset, int width) {
    int x=g_TextCursorX,y=g_TextCursorY,dim=g_DrawTextDimmed;
    if(width<=0)return;
    g_TextCursorX=x+offset;
    /* Economy's existing three-letter badges add two pixels of padding. */
    if(which && effect>=17 && effect<=20)g_TextCursorY-=2;
    if(effect>0 && PE_HOOK(PE_HOOK_EQUIP_EFFECT,(&(PeHookEquipEffect){effect,which!=0})))goto done;
    g_TextCursorY=y;
    if(effect<0 || (which ? effect>PE_ARMOR_EFFECTS_GAME : effect>PE_GUN_EFFECTS_GAME)) {
        unsigned char unknown[]={0x2C,0xFF}; /* no retail sprite: don't index unrelated art */
        Draw_PrintRawText(unknown);
        goto done;
    }
    int id=effect ? effect+(which?0x36:0x22) : 0x46;
    const DrawGlyphDescriptor *g=Draw_LookupGlyphDescriptor(id);
    if(!g || !g->width || !g->height)goto done;
    int w=g->width,h=g->height;
    if(h>12){w=w*12/h;h=12;}
    if(w>width){h=h*width/w;w=width;}
    if(w<=0 || h<=0)goto done;
    if(w==g->width && h==g->height)Draw_AllocSprite(id);
    else pe_icon_part(id,0,0,g->width,g->height,0,0,w,h);
done:
    g_TextCursorX=x;g_TextCursorY=y;g_DrawTextDimmed=dim;
}
