/* Native menu drawing only. Prices and eligibility always come from pe_catalog. */
#include <stdio.h>
#include <string.h>
#include "pe_catalog_menu_view.h"
#include "pe_catalog_effect_icons.h"
#include "pe_catalog.h"
#include "pe_economy.h"
#include "pe_gametext.h"
#include "pe_translate.h"
#include "pe1/draw_state.h"
#include "pe1/menu_draw_list.h"
#include "pe1/text.h"

void pe_catalog_view_layout(MenuWidgetNode *n, int visible, int width) {
    n->x = n->y = 4;
    n->grid_width = n->x_limit = 1;
    n->disabled = PE_CAT_ROW_HEIGHT;
    n->draw_state = width - 8;
    n->visible_rows = n->visible_rows_mirror = visible;
    n->parent->visible_rows = visible * PE_CAT_ROW_HEIGHT + 8;
    n->parent->draw_state = 0;
}

/* Fit by the actual native font metrics, not ASCII character count. Keep the
 * caller's cursor intact so aligned price/stat columns cannot drift. Full
 * selected names are also available in the wider help bar. */
void pe_catalog_view_text(const char *text, int offset, int width) {
    unsigned char glyphs[128];
    int x = g_TextCursorX, y = g_TextCursorY;
    pe_tr_ascii(glyphs, sizeof glyphs, text);
    size_t n = 0;
    while (glyphs[n] != 0xFF) ++n;
    if (width <= 0) return;
    if (Draw_MeasureTextWidth(glyphs) > width) {
        while (n && Draw_MeasureTextWidth(glyphs) > width) glyphs[--n] = 0xFF;
        if (n >= 3) {
            glyphs[n-1] = glyphs[n-2] = glyphs[n-3] = pe_glyph('.');
            while (n && Draw_MeasureTextWidth(glyphs) > width) glyphs[--n] = 0xFF;
        }
    }
    g_TextCursorX = x + offset;
    Draw_PrintRawText(glyphs);
    g_TextCursorX = x; g_TextCursorY = y;
}

const char *pe_catalog_view_problem(int code) {
    switch (code) {
    case PE_CAT_NO_GAME: return pe_l10n_ui("Load a game first");
    case PE_CAT_NO_ITEM: return pe_l10n_ui("Equip it first");
    case PE_CAT_NO_SLOTS: return pe_l10n_ui("This item has no mod slots");
    case PE_CAT_LOCKED: return pe_l10n_ui("Effect not unlocked");
    case PE_CAT_ALREADY: return pe_l10n_ui("Already in this slot");
    case PE_CAT_BAG: return pe_l10n_ui("Bag effects are protected");
    case PE_CAT_JUNK: return pe_l10n_ui("Not enough Junk");
    case PE_CAT_TOOL: return pe_l10n_ui("Needs a Tool or Tool Kit");
    case PE_CAT_EMPTY: return pe_l10n_ui("This slot is empty");
    case PE_CAT_OFF: return pe_l10n_ui("Junk economy is off");
    default: return pe_l10n_ui("Cannot do that now");
    }
}

void pe_catalog_view_slot(int which, int slot) {
    char number[16];
    if(pe_cat_slots(which)<=0){pe_catalog_view_text("No mod slots",0,g_DrawTextPosX-4);return;}
    snprintf(number,sizeof number,"%d",slot+1);
    pe_catalog_view_text(number,0,18);
    pe_catalog_effect_icon(which,pe_cat_slot_effect(which,slot),22,g_DrawTextPosX-28);
}

void pe_catalog_view_remove(int which, int slot) {
    Draw_SetTextDimmed(pe_cat_remove(which,slot,1)!=PE_CAT_OK);
    pe_catalog_view_text("Remove",0,g_DrawTextPosX-4);
    Draw_SetTextDimmed(0);
}

void pe_catalog_view_effect(int which, int slot, int effect) {
    char price[32];
    int width = g_DrawTextPosX - 4;
    if(!pe_cat_unlocked(which,effect)) {
        Draw_SetTextDimmed(1);
        pe_catalog_view_text("????",0,width);
        Draw_SetTextDimmed(0);
        return; /* No icon, price, Tool requirement or name before discovery. */
    }
    int dim = pe_cat_install(which, slot, effect, 1) != PE_CAT_OK;
    snprintf(price, sizeof price, "%s", "--");
    if (pe_cat_unlocked(which, effect))
        snprintf(price, sizeof price, "%u%s", (unsigned)pe_cat_price(which, effect), pe_cat_needs_tool(which, effect) ? "+T" : "");
    unsigned char bytes[40];
    pe_text_from_ascii(bytes, sizeof bytes, price);
    int price_width = Draw_MeasureTextWidth(bytes);
    Draw_SetTextDimmed(dim);
    pe_catalog_effect_icon(which,effect,0,width-price_width-5);
    pe_catalog_view_text(price, width - price_width, price_width);
    Draw_SetTextDimmed(0);
}

/* Retail Tune Up stat/header artwork; a replacement font with larger art
 * falls back to fitted text rather than drawing over the neighboring column. */
static int stat_sprite(int id, int offset, int width) {
    const DrawGlyphDescriptor *glyph=Draw_LookupGlyphDescriptor(id);
    if (!glyph || !glyph->width || glyph->width>width || glyph->height>PE_CAT_ROW_HEIGHT) return 0;
    int x=g_TextCursorX,y=g_TextCursorY;
    g_TextCursorX=x+offset;Draw_AllocSprite(id);
    g_TextCursorX=x;g_TextCursorY=y;
    return 1;
}
static int s_stats_which;
static const MenuWidgetNode *s_stats_slots;
static void stats_row(int row) {
    char line[96];
    const ItemDataRecord *rec = pe_cat_record(s_stats_which);
    if (row == 0) {
        snprintf(line, sizeof line, "%s: %s", s_stats_which ? "Armor" : "Gun", rec ? pe_cat_item_name(rec->itemId) : "None");
        pe_catalog_view_text(line, 0, g_DrawTextPosX - 4);
    } else if (row == 1) {
        if(!stat_sprite(0x87,54,33))pe_catalog_view_text("BASE",54,33);
        if(!stat_sprite(0x88,90,34))pe_catalog_view_text("PLUS",90,34);
    } else if (row < 5) {
        static const char *labels[2][3] = {{"Attack", "Range", "Bullets"}, {"Defense", "PEnergy", "Critical"}};
        if(!stat_sprite((s_stats_which?0x7F:0x7C)+row-2,0,52))
            pe_catalog_view_text(labels[s_stats_which][row-2],0,52);
        snprintf(line, sizeof line, "%d", rec ? rec->baseStats[row-2] : 0);
        pe_catalog_view_text(line, 54, 33);
        snprintf(line, sizeof line, "%+d", rec ? rec->bonusStats[row-2] : 0);
        pe_catalog_view_text(line, 90, 34);
    } else {
        int count = pe_cat_slots(s_stats_which), top = s_stats_slots->scroll_y;
        int last = top + s_stats_slots->visible_rows;
        if (last > count) last = count;
        snprintf(line, sizeof line, "Effects %d-%d/%d", count ? top+1 : 0, last, count);
        pe_catalog_view_text(line, 0, g_DrawTextPosX - 4);
    }
}
void pe_catalog_view_stats(MenuWidgetNode *node, int which, const MenuWidgetNode *slots) {
    s_stats_which = which; s_stats_slots = slots;
    MenuWidget_DrawList(node, stats_row);
}

static int s_detail_which, s_detail_slot, s_detail_effect, s_detail_review;
static void detail_row(int row) {
    char line[96];
    const int e = s_detail_effect, which = s_detail_which;
    if(e && !pe_cat_unlocked(which,e)) {
        if(row==0)pe_catalog_view_text("????",0,g_DrawTextPosX-4);
        return;
    }
    int code=e ? pe_cat_install(which,s_detail_slot,e,1) : pe_cat_remove(which,s_detail_slot,1);
    if(s_detail_review) {
        static const char *review_lines[]={"Review changes","X: Yes / No","O: back",""};
        snprintf(line,sizeof line,"%s",review_lines[row]);
    } else if(!e) {
        if(row==0)snprintf(line,sizeof line,"Remove effect");
        else if(row==1)snprintf(line,sizeof line,"No Junk cost");
        else if(row==2)snprintf(line,sizeof line,"%s",code==PE_CAT_OK?"Stays unlocked":pe_catalog_view_problem(code));
        else snprintf(line,sizeof line,"X: review removal");
    } else if(row==0)snprintf(line,sizeof line,"%s",pe_cat_effect_name(which,e));
    else if(row==1)snprintf(line,sizeof line,"%u Junk%s",(unsigned)pe_cat_price(which,e),pe_cat_needs_tool(which,e)?" +Tool":"");
    else if(row==2)snprintf(line,sizeof line,"%s",code==PE_CAT_OK?"X: review changes":pe_catalog_view_problem(code));
    else snprintf(line,sizeof line,"O: slots");
    pe_catalog_view_text(line, 0, g_DrawTextPosX - 4);
}
void pe_catalog_view_detail(MenuWidgetNode *node, int which, int slot, int effect, int review) {
    s_detail_which=which; s_detail_slot=slot; s_detail_effect=effect; s_detail_review=review;
    MenuWidget_DrawList(node, detail_row);
}
