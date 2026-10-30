/* Aya's Mod Catalog: native Tune Up-style panels. Drawing and viewport math
 * are separate modules; all mutations and dry runs remain pe_catalog's rules. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pe_economy.h"
#include "pe_catalog.h"
#include "pe_catalog_menu_state.h"
#include "pe_catalog_menu_view.h"
#include "pe_catalog_effect_icons.h"
#include "pe_game.h"
#include "pe_main_menu.h"
#include "pe_mod_host.h"
#include "pe_gametext.h"
#include "pe1/menu_draw_list.h"
#include "pe1/menu_scroll_cursor.h"
#include "pe1/menu_sound.h"
#include "pe1/text.h"

static PeScreen s_screen;
static struct {
    MenuWidgetNode *selector, *slots, *stats, *detail;
    struct { PeCatalogCursor slots, board; } gear[2];
    int which, review, review_count, *removed, removed_count;
    int ask_valid, ask_which, ask_slot, ask_effect;
    uint32_t ask_price, ask_junk;
    ItemDataRecord before;
    char result[96];
} s_cat;

static int cat_input(MenuWidgetNode *node, unsigned int buttons);
static void cat_refresh(void);
static void cat_draw_board(MenuWidgetNode *node);
static void cat_draw_slots(MenuWidgetNode *node);
static int cat_alive(void) {
    if (!s_screen.panel || !s_screen.list || !s_cat.slots || !s_cat.selector) return 0;
    unsigned found=0;
    MenuWidgetNode *n=g_MenuWidgetActiveListHead;
    for (int i=0;n && i<24;++i,n=n->next) {
        if(n==s_screen.panel)found|=1;
        if(n==s_screen.list)found|=2;
        if(n==s_cat.slots)found|=4;
        if(n==s_cat.selector)found|=8;
    }
    return found==15 && s_screen.list->draw==(void (*)())cat_draw_board &&
           s_cat.slots->draw==(void (*)())cat_draw_slots;
}
static void cat_save(void) {
    if (!s_cat.slots) return;
    s_cat.gear[s_cat.which].slots=(PeCatalogCursor){s_cat.slots->cursor_y,s_cat.slots->scroll_y};
    if (!s_cat.review) s_cat.gear[s_cat.which].board=(PeCatalogCursor){s_screen.list->cursor_y,s_screen.list->scroll_y};
}
static int cat_board_count(void) { return 1+pe_cat_board_count(s_cat.which); }
static int cat_effect(void) {
    int row=s_cat.gear[s_cat.which].board.row;
    return row>0 ? pe_cat_board_effect(s_cat.which,row-1) : 0;
}
static void cat_rows(MenuWidgetNode *list, int count, PeCatalogCursor cursor) {
    pe_catalog_cursor_clamp(&cursor,count,list->visible_rows_mirror);
    PeScreen adapter={.list=list};
    pe_screen_rows(&adapter,count > 0 ? count : 1,cursor.row);
    list->scroll_y=cursor.top;
    list->parent->visible_rows=list->visible_rows_mirror*list->disabled+8;
}
static void cat_focus(MenuWidgetNode *node) {
    s_cat.result[0]=0;
    MenuWidget_SetCurrentNode(node);
}
static void cat_clear_review(void) {
    s_cat.review=0; s_cat.ask_valid=0;
    free(s_cat.removed); s_cat.removed=NULL; s_cat.removed_count=0;
}
static void cat_selector_row(int which) {
    pe_catalog_view_text(which ? "Armor" : "Weapon",0,g_DrawTextPosX-4);
}
static void cat_draw_selector(MenuWidgetNode *node) { MenuWidget_DrawList(node,cat_selector_row); }
static void cat_slot_row(int row) { pe_catalog_view_slot(s_cat.which,row); }
static void cat_board_row(int row) {
    if(row==0)pe_catalog_view_remove(s_cat.which,s_cat.slots->cursor_y);
    else pe_catalog_view_effect(s_cat.which,s_cat.slots->cursor_y,pe_cat_board_effect(s_cat.which,row-1));
}
static void cat_review_text(int row, char *out, size_t size) {
    int n = s_cat.removed_count ? s_cat.removed_count : 1;
    if (row==0) snprintf(out,size,"%s",s_cat.ask_effect ? pe_cat_effect_name(s_cat.ask_which,s_cat.ask_effect) : "Remove effect");
    else if (row==1) snprintf(out,size,"Junk: %u",(unsigned)s_cat.ask_price);
    else if (row==2) snprintf(out,size,"%s",s_cat.ask_effect && pe_cat_needs_tool(s_cat.ask_which,s_cat.ask_effect) ? "Tool / Kit needed" : "No Tool needed");
    else if (row==3) snprintf(out,size,"After: %u",(unsigned)(s_cat.ask_junk-s_cat.ask_price));
    else if (row==4) snprintf(out,size,"Removes:");
    else if (row<5+n) snprintf(out,size,"%s",s_cat.removed_count ? pe_cat_effect_name(s_cat.ask_which,s_cat.removed[row-5]) : "Nothing");
    else snprintf(out,size,"X yes/no  O back");
}
static void cat_review_row(int row) {
    if(row==0 && s_cat.ask_effect) {
        pe_catalog_effect_icon(s_cat.ask_which,s_cat.ask_effect,0,124);
    } else if(row>=5 && row<5+s_cat.removed_count) {
        char index[16];snprintf(index,sizeof index,"%d",row-4);
        pe_catalog_view_text(index,0,18);
        pe_catalog_effect_icon(s_cat.ask_which,s_cat.removed[row-5],22,100);
    } else {
        char text[96];cat_review_text(row,text,sizeof text);
        pe_catalog_view_text(text,0,124);
    }
}
static void cat_draw_board(MenuWidgetNode *n) {
    if(s_cat.review && s_cat.ask_effect && !pe_cat_unlocked(s_cat.ask_which,s_cat.ask_effect)) {
        cat_clear_review();cat_refresh(); /* Debug/save reload revoked this discovery. */
    }
    /* F1 editors can change slot counts while Aya's menu is still open. */
    if (!s_cat.review && (s_cat.slots->y_limit!=(pe_cat_slots(s_cat.which)>0?pe_cat_slots(s_cat.which):1) ||
        n->y_limit!=cat_board_count())) {
        cat_save();cat_refresh();
    }
    MenuWidget_DrawList(n,s_cat.review ? cat_review_row : cat_board_row);
}
static void cat_draw_slots(MenuWidgetNode *n) { MenuWidget_DrawList(n,cat_slot_row); }
static void cat_draw_stats(MenuWidgetNode *n) { pe_catalog_view_stats(n,s_cat.which,s_cat.slots); }
static void cat_draw_detail(MenuWidgetNode *n) { pe_catalog_view_detail(n,s_cat.which,s_cat.slots->cursor_y,s_cat.review?s_cat.ask_effect:cat_effect(),s_cat.review); }

static void cat_refresh(void) {
    cat_rows(s_cat.slots,pe_cat_slots(s_cat.which),s_cat.gear[s_cat.which].slots);
    cat_rows(s_screen.list,cat_board_count(),s_cat.gear[s_cat.which].board);
    cat_save();
}
static void cat_answer(int yes) {
    if (!s_cat.ask_valid) return;
    if (!cat_alive()) { cat_clear_review(); return; }
    s_cat.ask_valid=0; /* A callback cannot apply a transaction twice. */
    if (yes) {
        const ItemDataRecord *rec=pe_cat_record(s_cat.ask_which);
        int code;
        if (!rec || memcmp(rec,&s_cat.before,sizeof *rec) || pe_loot_junk()!=s_cat.ask_junk ||
            (s_cat.ask_effect && pe_cat_price(s_cat.ask_which,s_cat.ask_effect)!=s_cat.ask_price)) {
            snprintf(s_cat.result,sizeof s_cat.result,"Equipment or price changed. Review again");
            Menu_PlayErrorSound();
        } else {
            code=s_cat.ask_effect ? pe_cat_install(s_cat.ask_which,s_cat.ask_slot,s_cat.ask_effect,0) : pe_cat_remove(s_cat.ask_which,s_cat.ask_slot,0);
            snprintf(s_cat.result,sizeof s_cat.result,"%s",code==PE_CAT_OK ? (s_cat.ask_effect ? "Installed. Effects stay unlocked" : "Removed. Effect stays unlocked") : pe_catalog_view_problem(code));
            if (code==PE_CAT_OK) { Menu_PlayConfirmSound(); pe_log("catalog_menu","which=%d slot=%d effect=%d",s_cat.ask_which,s_cat.ask_slot,s_cat.ask_effect); }
            else Menu_PlayErrorSound();
        }
    }
    cat_clear_review(); cat_refresh();
    MenuWidget_SetCurrentNode(s_screen.list);
}
static void cat_review(int effect) {
    if(effect && !pe_cat_unlocked(s_cat.which,effect)) {
        snprintf(s_cat.result,sizeof s_cat.result,"????");Menu_PlayErrorSound();return;
    }
    const int slot=s_cat.slots->cursor_y;
    int code=effect ? pe_cat_install(s_cat.which,slot,effect,1) : pe_cat_remove(s_cat.which,slot,1);
    const ItemDataRecord *rec=pe_cat_record(s_cat.which);
    if (code!=PE_CAT_OK || !rec) {
        snprintf(s_cat.result,sizeof s_cat.result,"%s",pe_catalog_view_problem(code)); Menu_PlayErrorSound(); return;
    }
    cat_save(); cat_clear_review();
    int count=pe_cat_slots(s_cat.which);
    if (count<1 || (size_t)count>SIZE_MAX/sizeof(int) || !(s_cat.removed=malloc((size_t)count*sizeof(int)))) {
        snprintf(s_cat.result,sizeof s_cat.result,"Cannot prepare review"); Menu_PlayErrorSound(); return;
    }
    if (effect) s_cat.removed_count=pe_cat_replaces(s_cat.which,slot,effect,s_cat.removed,count);
    else { s_cat.removed[0]=pe_cat_slot_effect(s_cat.which,slot); s_cat.removed_count=1; }
    s_cat.ask_which=s_cat.which; s_cat.ask_slot=slot; s_cat.ask_effect=effect;
    s_cat.before=*rec; s_cat.ask_price=effect ? pe_cat_price(s_cat.which,effect) : 0; s_cat.ask_junk=pe_loot_junk();
    s_cat.ask_valid=1; s_cat.review=1;
    s_cat.review_count=6+(s_cat.removed_count ? s_cat.removed_count : 1);
    cat_rows(s_screen.list,s_cat.review_count,(PeCatalogCursor){0,0});
    cat_focus(s_screen.list); Menu_PlayConfirmSound();
}
static void cat_choose(int which, MenuWidgetNode *focus) {
    cat_save(); cat_clear_review(); s_cat.which=which;
    s_cat.selector->cursor_x=which;
    cat_refresh(); cat_focus(focus); Menu_PlayMoveSound();
}
static int cat_input(MenuWidgetNode *node, unsigned int buttons) {
    if (node!=s_cat.selector && node!=s_cat.slots && node!=s_screen.list) return 0;
    if (pe_confirm_open()) return 1;
    if (buttons & (4u|8u)) { /* L1/R1 remains a shortcut from either list. */
        if (!s_cat.review) cat_choose(s_cat.which^1,node==s_cat.selector?s_cat.selector:s_cat.slots);
        return 1;
    }
    if (buttons & PE_MENU_CIRCLE) {
        if (s_cat.review) { cat_clear_review(); cat_refresh(); cat_focus(s_screen.list); }
        else if (node==s_screen.list) cat_focus(s_cat.slots);
        else if (node==s_cat.slots) cat_focus(s_cat.selector);
        else { cat_save(); cat_clear_review(); s_cat.selector=s_cat.slots=s_cat.stats=s_cat.detail=NULL; pe_screen_close(&s_screen); }
        Menu_PlayCancelSound(); return 1;
    }
    if (!s_cat.review && (buttons & (0x8000u|0x2000u))) { /* Left / Right */
        if (node==s_cat.selector) cat_choose((buttons&0x8000u)?PE_GUN:PE_ARMOR,s_cat.selector);
        else {
            cat_save();
            cat_focus((buttons&0x8000u)?(node==s_cat.slots?s_cat.selector:s_cat.slots):s_screen.list);
            Menu_PlayMoveSound();
        }
        return 1;
    }
    if (node==s_cat.selector) {
        if (buttons & (PE_MENU_CROSS|0x4000u)) { cat_focus(s_cat.slots);Menu_PlayConfirmSound(); }
        return 1;
    }
    if (buttons & PE_MENU_CROSS) {
        s_cat.result[0]=0;
        if (s_cat.review) pe_confirm(s_cat.ask_effect ? "Install effect?" : "Remove effect?",0,cat_answer);
        else if (node==s_cat.slots) {
            if (pe_cat_slots(s_cat.which)>0) {cat_focus(s_screen.list);Menu_PlayConfirmSound();}
            else {snprintf(s_cat.result,sizeof s_cat.result,"No mod slots");Menu_PlayErrorSound();}
        } else cat_review(cat_effect());
        return 1;
    }
    if(buttons&0x80u)return 1; /* Removal is the first Catalog row, never a Square shortcut. */
    if (buttons & (0x1000u|0x4000u)) {
        int handled=Menu_StepScrollCursor(node,buttons), old_top=node->scroll_y;
        PeCatalogCursor cursor={node->cursor_y,node->scroll_y};
        pe_catalog_cursor_clamp(&cursor,node->y_limit,node->visible_rows);
        node->cursor_y=cursor.row;node->scroll_y=cursor.top;
        if (old_top!=cursor.top) node->scroll_adjust=0;
        s_cat.result[0]=0; cat_save(); return handled;
    }
    return 0;
}

static void cat_open(MenuWidgetNode *from) {
    /* Ten panel/list nodes, two scroll-arrow nodes and the two-node Yes/No
     * dialog must fit the native 24-node pool alongside Aya's menu. */
    int used=0;
    for (MenuWidgetNode *n=g_MenuWidgetActiveListHead;n && used<=24;n=n->next) ++used;
    if (used>10) { pe_toast("Close other screens before opening the Catalog"); Menu_PlayErrorSound(); return; }
    pe_screen_open(&s_screen,from,170,32,142,PE_CAT_BOARD_ROWS,cat_draw_board,NULL);
    pe_catalog_view_layout(s_screen.list,PE_CAT_BOARD_ROWS,142);
    s_cat.selector=pe_screen_panel(&s_screen,30,32,136,1,cat_draw_selector);
    s_cat.slots=pe_screen_panel(&s_screen,30,154,136,PE_CAT_SLOT_ROWS,cat_draw_slots);
    s_cat.stats=pe_screen_panel(&s_screen,30,58,136,6,cat_draw_stats);
    s_cat.detail=pe_screen_panel(&s_screen,170,158,142,4,cat_draw_detail);
    pe_catalog_view_layout(s_cat.selector,1,136);
    s_cat.selector->grid_width=s_cat.selector->x_limit=2;
    s_cat.selector->draw_state=(136-8)/2;
    s_cat.selector->cursor_x=s_cat.which;s_cat.selector->cursor_y=0;
    Draw_SetPrimCallback(s_cat.selector,2);
    pe_catalog_view_layout(s_cat.slots,PE_CAT_SLOT_ROWS,136);
    pe_catalog_view_layout(s_cat.stats,6,136);
    pe_catalog_view_layout(s_cat.detail,4,142);
    s_cat.stats->cursor_x=s_cat.detail->cursor_x=-1;
    s_cat.selector->update=s_cat.slots->update=s_screen.list->update=cat_input;
    cat_clear_review(); cat_refresh(); cat_focus(s_cat.selector);
    pe_log("catalog_menu","open native panels slots=%d board=%d",pe_cat_slots(s_cat.which),pe_cat_board_count(s_cat.which));
}
static int cat_help(void *ctx,void *user) {
    static unsigned char title[96];
    char line[96];
    MenuWidgetNode *node=MenuWidget_GetCurrentNode();
    (void)user;
    if (!cat_alive() || (node!=s_cat.selector && node!=s_cat.slots && node!=s_screen.list)) return 0;
    if((s_cat.review && s_cat.ask_effect && !pe_cat_unlocked(s_cat.ask_which,s_cat.ask_effect)) ||
       (!s_cat.review && node==s_screen.list && cat_effect() && !pe_cat_unlocked(s_cat.which,cat_effect())))
        snprintf(line,sizeof line,"????");
    else if (s_cat.result[0]) snprintf(line,sizeof line,"%s",s_cat.result);
    else if (s_cat.review) cat_review_text(node->cursor_y,line,sizeof line);
    else if (node==s_cat.selector) snprintf(line,sizeof line,"Choose Weapon or Armor / X select");
    else if (node==s_cat.slots) snprintf(line,sizeof line,"Slot %d: %s",node->cursor_y+1,pe_cat_effect_name(s_cat.which,pe_cat_slot_effect(s_cat.which,node->cursor_y)));
    else snprintf(line,sizeof line,"%s / Junk %u",cat_effect()?pe_cat_effect_name(s_cat.which,cat_effect()):"Remove effect",(unsigned)pe_loot_junk());
    pe_text_from_ascii(title,sizeof title,line);
    /* The wide title bar is still finite, even with custom names. */
    size_t n=0;while(title[n]!=0xFF)++n;
    if(n>48){n=48;title[n]=0xFF;} /* also bound packets with very narrow replacement fonts */
    while(n && Draw_MeasureTextWidth(title)>278)title[--n]=0xFF;
    *(unsigned char **)ctx=title; return 0;
}
/* A scene/menu teardown can bypass Circle. Do not retain review allocations
 * or recognize a recycled game widget as our Catalog. */
static int cat_frame(void *ctx,void *user) {
    (void)ctx;(void)user;
    if (s_cat.removed && !cat_alive()) cat_clear_review();
    return 0;
}
static const PeMainMenuRow s_row={"Mod Catalog",0,pe_loot_enabled,cat_open};
void pe_catalog_menu_register(const char *mod_id) {
    pe_main_menu_add(&s_row,10);
    pe_hook_add(PE_HOOK_FRAME,cat_frame,NULL,PE_PRIORITY_NORMAL,mod_id);
    pe_hook_add(PE_HOOK_HELP_TEXT,cat_help,NULL,PE_PRIORITY_LATE,mod_id);
}
