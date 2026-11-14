/* Shared native comparison shell. Explicit preview build only; no live transactions. */
#include "bp_ui_native.h"
#include "bp_ui.h"
#include "pe_main_menu.h"
#include "pe_gametext.h"
#include "pe1/draw_state.h"
#include "pe1/draw_area.h"
#include "pe1/draw_buffers.h"
#include <stddef.h>
#include "pe1/menu_draw_list.h"
#include "pe1/menu_sound.h"
#include "pe1/menu_widget.h"
#include "pe1/menu_item_rows.h"
#include "pe1/draw_level_bar.h"
#include "pe1/menu_context_help.h"
#include "pe1/inventory_slots.h"
#include "pe1/text.h"
#include <stdio.h>
#include <string.h>

static void print(const char *text);
/* ---- the game's own BP-screen pieces (item_actions.c, equip_select.c) ---- */
enum { SPRITE_BONUS_POINT = 0x93, SPRITE_BASE = 0x87, SPRITE_PLUS = 0x88,
       SPRITE_GUN_STATS = 0x7C, SPRITE_ARMOR_STATS = 0x7F };
static ItemDataRecord *visual_record(const BpUiVisual *v) {
    return v && v->record >= 0 && v->record < 128 ? &g_InventoryRuntime.equipment[v->record] : NULL;
}
/* The bag's row: icon, the game's item name, the gun's ammo. */
static void vanilla_item_row(const BpUiVisual *v) {
    ItemDataRecord *rec = visual_record(v);
    if (rec && rec->itemId) Menu_DrawSlotRow(rec, Text_LookupTable8(rec->itemId - 1u));
}
/* Vanilla stat line: banner, total, then small BASE and PLUS digits (Menu_DrawBonusPointSpendPanel
 * offsets). compare: show `plus` in the game's better/worse colour against `was`. */
static void vanilla_stat_line(int armor, int stat, int total, int base, int plus, int was, int compare) {
    const int x = g_TextCursorX, y = g_TextCursorY;
    Draw_AllocSprite((armor ? SPRITE_ARMOR_STATS : SPRITE_GUN_STATS) + stat);
    Draw_OffsetCursor(0x1E, 0);
    Draw_PrintNumberWidth4(total > 999 ? 999 : total);
    Draw_OffsetCursor(4, 2);
    Draw_PrintNumberWidth4Unk(base);
    Draw_OffsetCursor(5, 0);
    if (compare) Draw_SetStatCompareColor(was, plus);
    Draw_PrintSignedNumberWidth4(plus);
    Draw_SetColor(0x808080);
    g_TextCursorX = x; g_TextCursorY = y;
}
enum { SPRITE_AYA_STATS = 0x8C };
/* Vanilla Aya stat line (Menu_DrawBonusPointSpendPanel): banner, level, level bar with the
 * points still needed. compare: level in the better/worse colour against `was`. */
static void vanilla_aya_line(int stat, BpUiAyaLevel l, int was, int compare) {
    const int x = g_TextCursorX, y = g_TextCursorY;
    Draw_AllocSprite(SPRITE_AYA_STATS + stat);
    Draw_OffsetCursor(0x42, -1);
    if (compare) Draw_SetStatCompareColor(was, l.level);
    Draw_PrintNumberWidth2(l.level);
    Draw_SetColor(0x808080);
    Draw_OffsetCursor(2, 0);
    Draw_AllocTexturedRectAlt(l.distance, l.sub);
    g_TextCursorX = x; g_TextCursorY = y;
}
static void vanilla_base_plus_header(void) {
    const int x = g_TextCursorX, y = g_TextCursorY;
    Draw_OffsetCursor(0x1E + 0x24 + 4, 4);
    Draw_AllocSprite(SPRITE_BASE);
    Draw_OffsetCursor(0x19, 0);
    Draw_AllocSprite(SPRITE_PLUS);
    g_TextCursorX = x; g_TextCursorY = y;
}
static void vanilla_bonus_points(int bp) {
    const int x = g_TextCursorX, y = g_TextCursorY;
    Draw_AllocSprite(SPRITE_BONUS_POINT);
    g_TextCursorX = x + g_DrawTextPosX - 4 - 0x2D;
    if (bp >= 0) Draw_PrintNumberWidth5(bp);
    else print("--");
    g_TextCursorX = x; g_TextCursorY = y;
}

enum { UI_X = 8, UI_WIDTH = 304, UI_LEFT = 128, UI_GAP = 4, UI_HEIGHT = 224 };
static PeScreen screen;
static BpUiController ui;
static int opened, highlights_drawn;
static const struct { unsigned edge, fill; } theme = {0x00808080u, 0x00282828u};
typedef struct UiRect { int x,y,w,h; } UiRect;
static UiRect focus_rect, tab_rect;
/* Each panel owns bounded, double-buffered native packets. Dense text must not
 * consume the original field/menu arena or starve later selection packets. */
typedef union UiPacketArena { max_align_t align; unsigned char bytes[0x4000]; } UiPacketArena;
static UiPacketArena panel_packets[2][5];
typedef struct UiPacketScope { unsigned char *base, *cursor; } UiPacketScope;
static UiPacketScope packets_begin(int panel_index) {
    UiPacketScope saved={g_DrawPacketArenaBase,g_DrawPacketCursor};
    g_DrawPacketArenaBase=panel_packets[g_DrawBufferIndex != 0][panel_index].bytes;
    g_DrawPacketCursor=g_DrawPacketArenaBase;return saved;
}
static void packets_end(UiPacketScope saved) {
#ifdef BP_UI_HEADLESS_TEST
    if((size_t)(g_DrawPacketCursor-g_DrawPacketArenaBase)>=0x3f00)
        fprintf(stderr,"BP_UI_PACKET_PRESSURE page=%s\n",ui.page->id);
#endif
    g_DrawPacketArenaBase=saved.base;g_DrawPacketCursor=saved.cursor;
}

static struct { int line, margin, tabs_y, body_y, footer_y, rows, right_x, right_width; } layout;
static u8 glyphs[PE_GAMETEXT_MAX];
static struct { MenuWidgetNode *node; int selected_base; } hidden[24];
static size_t hidden_count;
static void hide_parent_panels(void) {
    MenuWidgetNode *node;
    hidden_count = 0;
    for (node = g_MenuWidgetActiveListHead; node; node = node->next) {
        if (node->mode != 1 || node == screen.panel || node->cursor_y) continue;
        if (hidden_count == sizeof hidden / sizeof hidden[0]) break;
        hidden[hidden_count].node = node;
        hidden[hidden_count++].selected_base = node->selected_base;
        node->cursor_y = 1;
    }
}
static void restore_parent_panels(void) {
    while (hidden_count) {
        MenuWidgetNode *node = hidden[--hidden_count].node;
        if (node->mode == 1 && node->selected_base == hidden[hidden_count].selected_base)
            node->cursor_y = 0;
    }
}
static int measure(const char *text) {
    return Draw_MeasureTextWidth(pe_text_from_ascii(glyphs, sizeof glyphs, text ? text : ""));
}
static void print(const char *text) {
    pe_text_from_ascii(glyphs, sizeof glyphs, text ? text : "");
#ifdef BP_UI_HEADLESS_TEST
    if (text && Draw_MeasureTextWidth(glyphs) > g_DrawTextPosX - 4)
        fprintf(stderr, "BP_UI_TEXT_OVERFLOW page=%s width=%d limit=%d text=%s\n",
            ui.page->id, Draw_MeasureTextWidth(glyphs), g_DrawTextPosX - 4, text);
#endif
    Draw_PrintRawText(glyphs);
}
static void fit(const char *text, int width) {
    char part[BP_UI_TEXT];
    size_t n;
    snprintf(part, sizeof part, "%s", text ? text : ""); n = strlen(part);
    if (measure(part) <= width) { print(part); return; }
    while (n && measure(part) > width) part[--n] = 0;
    if (n >= 3) part[n-1] = part[n-2] = part[n-3] = '.';
    while (n && measure(part) > width) part[--n] = 0;
    print(part);
}
static void pair(const char *label, const char *value) {
    const int x = g_TextCursorX, y = g_TextCursorY, width = g_DrawTextPosX - 4;
    int value_width = measure(value);
    if (value && value[0] && value_width + 16 <= width) {
        fit(label, width - value_width - 8);
        g_TextCursorX = x + width - value_width; g_TextCursorY = y;
        print(value);
    } else fit(label, width);
    g_TextCursorX = x; g_TextCursorY = y;
}
static void center(const char *text) {
    const int x = g_TextCursorX, y = g_TextCursorY, width = g_DrawTextPosX - 4;
    int w = measure(text);
    if (w < width) g_TextCursorX += (width - w) / 2;
    fit(text, width);
    g_TextCursorX = x; g_TextCursorY = y;
}
/* Opaque native quad packets make focus independent of palette/blend/pulse state. */
static int solid_rect(int x,int y,int width,int height,unsigned rgb) {
    const size_t used=(size_t)(g_DrawPacketCursor-g_DrawPacketArenaBase);
    RenderGouraudQuad *p;RenderNativeTag *ot=(RenderNativeTag *)g_DrawOrderingTableEntry;
    u8 r=(u8)rgb,g=(u8)(rgb>>8),b=(u8)(rgb>>16);
    if(width<=0||height<=0||used>0x4000||sizeof(*p)>0x4000-used)return 0;
    p=(RenderGouraudQuad *)g_DrawPacketCursor;g_DrawPacketCursor+=sizeof(*p);memset(p,0,sizeof(*p));
    p->tag.length=8;p->code=0x38;
    p->r0=p->r1=p->r2=p->r3=r;p->g0=p->g1=p->g2=p->g3=g;p->b0=p->b1=p->b2=p->b3=b;
    p->x0=p->x2=(s16)x;p->x1=p->x3=(s16)(x+width);
    p->y0=p->y1=(s16)y;p->y2=p->y3=(s16)(y+height);
    p->tag.address=ot->address;ot->address=(uintptr_t)p;return 1;
}
/* The game's own list cursor (MenuWidget_DrawList's bevelled bar), held steady: the approved
 * design has one steady highlight, so the native blink (bevel inversion) is not used. Drawn into
 * this panel's own packet arena, so dense text cannot starve it. */
static void selection_bar(int width,int height) {
    const size_t before=(size_t)(g_DrawPacketCursor-g_DrawPacketArenaBase);
    extern int g_DrawTextDimmed;
    const int dimmed=g_DrawTextDimmed;
    const unsigned prim=g_DrawPrimColor,shaded=g_DrawColorShaded;
    /* The bar takes its colours from global draw state that other panels' text changes; draw it
     * with the game's defaults (Draw_DropShadow restores the same pair) and never dimmed. */
    g_DrawTextDimmed=0; g_DrawPrimColor=0x808080; g_DrawColorShaded=0x404040;
    Draw_AllocColorTriGradient(width,height,0,0);   /* 0,0: no bevel blink, no fill pulse */
    g_DrawTextDimmed=dimmed; g_DrawPrimColor=prim; g_DrawColorShaded=shaded;
#ifdef BP_UI_HEADLESS_TEST
    if (prim!=0x808080u||shaded!=0x404040u)
        fprintf(stderr,"BP_UI_CURSOR_COLOR prim=%06X shaded=%06X\n",prim,shaded);
#endif
    if((size_t)(g_DrawPacketCursor-g_DrawPacketArenaBase)==before)
        fprintf(stderr,"BP_UI_HIGHLIGHT_ERROR selection packet budget exceeded\n");
    (void)solid_rect;(void)theme;
}
/* Draw after the list has emitted its text/masks, matching native cursor order.
 * An explicit viewport reset prevents a text run from clipping the focus bar. */
static void paint_selection(UiRect r) {
    const int x=g_TextCursorX,y=g_TextCursorY;
    GpuCmdPacket *packet;
    DrawAreaRect area={0,0,320,448};
    size_t used;
    g_TextCursorX=r.x;g_TextCursorY=r.y;selection_bar(r.w,r.h);
    used=(size_t)(g_DrawPacketCursor-g_DrawPacketArenaBase);
    if (used<=0x4000 && sizeof(*packet)<=0x4000-used) {
        packet=(GpuCmdPacket *)g_DrawPacketCursor;g_DrawPacketCursor+=sizeof(*packet);
        /* Field and menu buffers can advance separately. The geometry stays
         * in its own 224-line target; admit either native buffer in this clip. */
        SetDrawArea(packet,&area);Draw_LinkDrawArea(g_DrawOrderingTableEntry,packet);
    } else fprintf(stderr,"BP_UI_HIGHLIGHT_ERROR clip packet budget exceeded\n");
    g_TextCursorX=x;g_TextCursorY=y;
}
static void draw_row(int at) {
    const BpUiRow *r;
    const int x = g_TextCursorX, y = g_TextCursorY;
    if (at < 0 || (size_t)at >= ui.page->row_count) return;
    r = &ui.page->rows[at];
    if (!bp_ui_row_selectable(r)) { Draw_SetTextDimmed(1); pair(r->label, r->value); return; }
    Draw_SetTextDimmed((size_t)at != ui.selected);
    if (r->visual && r->visual->kind == BP_UI_VISUAL_GEAR_ITEM) {
        if (r->visual->equipped) { Draw_DropShadow(); g_TextCursorX = x; g_TextCursorY = y; }
        vanilla_item_row(r->visual);
    }
    else if (r->visual && r->visual->kind == BP_UI_VISUAL_AYA_STAT) {
        Draw_AllocSprite(SPRITE_AYA_STATS + r->visual->stat);
        g_TextCursorX = x + g_DrawTextPosX - 4 - 0x14;
        Draw_PrintNumberWidth2(r->visual->now.level);
        g_TextCursorX = x; g_TextCursorY = y;
    }
    else if (r->visual && r->visual->kind == BP_UI_VISUAL_GEAR_STAT) {
        const BpUiVisual *v = r->visual;
        Draw_AllocSprite((v->armor ? SPRITE_ARMOR_STATS : SPRITE_GUN_STATS) + v->stat);
        g_TextCursorX = x + g_DrawTextPosX - 4 - 0x24;
        Draw_PrintNumberWidth4(v->total[v->stat]);
        g_TextCursorX = x; g_TextCursorY = y;
    } else pair(r->label, r->value);
    if ((size_t)at == ui.selected) {
        g_TextCursorX = x - 2; g_TextCursorY = y - 2;
        focus_rect.x=g_TextCursorX;focus_rect.y=g_TextCursorY;focus_rect.w=g_DrawTextPosX;focus_rect.h=g_MenuListRowHeight;
        ++highlights_drawn;
    }
    g_TextCursorX = x; g_TextCursorY = y;
    Draw_SetTextDimmed(0);
}
static void draw_list(MenuWidgetNode *node) {
    UiPacketScope scope=packets_begin(0);
    BpUiFrame frame;
    int cursor = node->cursor_x;
    highlights_drawn = 0; node->cursor_x = -1;
    MenuWidget_DrawList(node, draw_row); node->cursor_x = cursor;
    bp_ui_frame(&ui, &frame);
    if (frame.selected && highlights_drawn==1) paint_selection(focus_rect);
    packets_end(scope);
#ifdef BP_UI_HEADLESS_TEST
    if (frame.selected && highlights_drawn != 1)
        fprintf(stderr, "BP_UI_HIGHLIGHT_ERROR page=%s row=%zu count=%d\n", ui.page->id, ui.selected, highlights_drawn);
#endif
}
static void draw_readonly(MenuWidgetNode *node, void (*row)(int)) {
    node->cursor_x = -1; MenuWidget_DrawList(node, row);
}
static void header_row(int row) {
    if (row == 0) pair(ui.page->title, ui.page->wallet);
    else if (row == 1) pair(ui.page->subject ? ui.page->subject : ui.page->context, ui.catalog->badge ? ui.catalog->badge : "PREVIEW");
}
static void draw_header(MenuWidgetNode *node) { UiPacketScope s=packets_begin(1);draw_readonly(node, header_row);packets_end(s); }
static void tabs_row(int row) {
    const int origin_x = g_TextCursorX, origin_y = g_TextCursorY;
    int budget = g_DrawTextPosX - 4, used = 0, start = 0, active = -1;
    const BpUiTab *visible[BP_UI_HISTORY];
    size_t count = 0;
    for (size_t i=0;i<ui.catalog->tab_count;++i)
        if (bp_ui_tab_visible(&ui,i)) visible[count++]=&ui.catalog->tabs[i];
    if (row) return;
    if (!ui.page->tab_id) { fit("SELECT AN OPTION", budget); return; }
    for (size_t i = 0; i < count; ++i)
        if (!strcmp(visible[i]->id, ui.page->tab_id)) active = (int)i;
    /* Longer future tab catalogs scroll the ribbon around the active category. */
    for (int i = 0; i <= active; ++i) {
        used += measure(visible[i]->label) + 16;
        while (used > budget && start < i) used -= measure(visible[start++]->label) + 16;
    }
    used = 0;
    for (size_t i = (size_t)start; i < count; ++i) {
        const BpUiTab *tab = visible[i];
        int w = measure(tab->label), cell = w + 16;
        if (used + cell > budget) break;
        g_TextCursorX = origin_x + used + 4; g_TextCursorY = origin_y;
        Draw_SetTextDimmed((int)i != active); print(tab->label);
        if ((int)i == active) {
            g_TextCursorX = origin_x + used; g_TextCursorY = origin_y - 2;
            tab_rect=(UiRect){g_TextCursorX,g_TextCursorY,cell-4,g_MenuListRowHeight};
        }
        used += cell;
    }
    g_TextCursorX = origin_x; g_TextCursorY = origin_y; Draw_SetTextDimmed(0);
}
static void draw_tabs(MenuWidgetNode *node) { UiPacketScope s=packets_begin(2);tab_rect.w=0;draw_readonly(node,tabs_row);if(tab_rect.w)paint_selection(tab_rect);packets_end(s); }
static void note_line(const char *text, int wanted) {
    char line[BP_UI_TEXT];
    int index = 0, width = g_DrawTextPosX - 4;
    if (!text) return;
    do {
        size_t n = 0, space = 0, take;
        while (text[n] && n + 1 < sizeof line) {
            line[n] = text[n]; line[n+1] = 0;
            if (measure(line) > width) break;
            if (text[n] == ' ') space = n;
            ++n;
        }
        take = text[n] && space ? space : n;
        if (!take && text[0]) take = 1;
        memcpy(line, text, take); line[take] = 0;
        if (index++ == wanted) { print(line); return; }
        text += take; while (*text == ' ') ++text;
    } while (*text);
}
/* Gear rows in the game's own look. Returns 0 to fall back to the text rows (pinned cost). */
static int visual_detail_row(const BpUiRow *r, int row) {
    const BpUiVisual *v = r->visual;
    const int last = layout.rows - 1;
    if (v->kind == BP_UI_VISUAL_AYA_STAT) {
        /* Now, then after one step; the pinned cost row stays text. */
        if (row == 0) { vanilla_aya_line(v->stat, v->now, 0, 0); return 1; }
        if (row == 1) {
            if (r->state == BP_UI_AVAILABLE) vanilla_aya_line(v->stat, v->next, v->now.level, 1);
            return 1;
        }
        if (row == last) { vanilla_bonus_points(v->bp_after); return 1; }
        if (row == last - 1) return 0;
        if (row >= 2) note_line(r->state != BP_UI_AVAILABLE ? r->reason : r->preview && r->preview->note ? r->preview->note : "", row - 2);
        return 1;
    }
    if (row == 0) { vanilla_item_row(v); return 1; }
    if (row == 1) { vanilla_base_plus_header(); return 1; }
    if (v->kind == BP_UI_VISUAL_GEAR_ITEM) {
        if (row >= 2 && row <= 4) {
            int s = row - 2;
            vanilla_stat_line(v->armor, s, v->total[s], v->base[s], v->bonus[s], 0, 0);
            return 1;
        }
        if (row == last) { vanilla_bonus_points(v->bp_after); return 1; }
        if (row >= 5 && row < last) note_line(r->state != BP_UI_AVAILABLE ? r->reason : v->equipped ? "Equipped" : "In the bag", row - 5);
        return 1;
    }
    /* GEAR_STAT: now, then after one purchase in the game's better/worse colour. */
    if (row == 2) { vanilla_stat_line(v->armor, v->stat, v->total[v->stat], v->base[v->stat], v->bonus[v->stat], 0, 0); return 1; }
    if (row == 3) {
        if (r->state == BP_UI_AVAILABLE)
            vanilla_stat_line(v->armor, v->stat, v->next_total, v->base[v->stat], v->next_bonus, v->bonus[v->stat], 1);
        return 1;
    }
    if (row == last) { vanilla_bonus_points(v->bp_after); return 1; }
    if (row == last - 1) return 0;   /* the pinned cost row */
    if (row >= 4) note_line(r->state != BP_UI_AVAILABLE ? r->reason : "Never refunded", row - 4);
    return 1;
}
static void detail_row(int row) {
    BpUiFrame f;
    const BpUiPreview *p;
    char text[BP_UI_TEXT];
    int content_rows = layout.rows - 2;
    bp_ui_frame(&ui, &f);
    if (!f.selected) { if (!row) print("No upgrades available"); return; }
    if (!ui.expanded && f.selected->visual && visual_detail_row(f.selected, row)) return;
    p = f.selected->preview;
    if (row == layout.rows - 2 || row == layout.rows - 1) {
        if (p && p->cost) {
            const char *label = row == layout.rows - 2 ? "Cost" : "Remaining";
            const char *value = row == layout.rows - 2 ? p->cost : p->remaining;
            if (value) {
                if (measure(label) + measure(value) + 8 > g_DrawTextPosX - 4) label = "Left";
                pair(label, value);
            }
        }
        return;
    }
    if (ui.expanded || !p) {
        if (!row) fit(ui.expanded ? "FULL DETAILS" : f.selected->label, g_DrawTextPosX - 4);
        else if (row <= 3) print(f.detail[row - 1]);
        else if (row < content_rows) { snprintf(text, sizeof text, "Page %zu of %zu", f.detail_page + 1, f.detail_pages); print(text); }
        return;
    }
    if (!row) fit(p->title, g_DrawTextPosX - 4);
    else if (row == 1) {
        if (p->before && p->after) { snprintf(text, sizeof text, "%s to %s", p->before, p->after); center(text); }
        else if (p->before) center(p->before);
    } else if (row == 2) fit(p->benefit, g_DrawTextPosX - 4);
    else note_line(f.selected->state != BP_UI_AVAILABLE ? f.selected->reason : p->note, row - 3);
}
static void draw_details(MenuWidgetNode *node) { UiPacketScope s=packets_begin(3);draw_readonly(node, detail_row);packets_end(s); }
static void footer_row(int row) {
    BpUiFrame f;
    char text[96];
    bp_ui_frame(&ui, &f);
    if (!row) {
        if (f.notice && f.notice[0]) { fit(f.notice, g_DrawTextPosX - 4); return; }
        snprintf(text, sizeof text, "%s%zu/%zu", ui.expanded ? "L1/R1: Details " : "Details ",
            f.detail_pages ? f.detail_page + 1 : 0, f.detail_pages);
        pair(ui.page->tab_id ? "L/R: Tabs" : ui.page->context, text);
    } else {
        const char *action = "";
        if (f.selected) {
            if (f.selected->state != BP_UI_AVAILABLE) action = "Unavailable  ";
            else if (f.selected->action == BP_UI_ROUTE) action = "X: Open  ";
            else if (f.selected->action == BP_UI_ASK) action = ui.page->tab_id && strncmp(ui.page->tab_id,"refund.",7) ? "X: Upgrade  " : "X: Confirm  ";
            else if (f.selected->action == BP_UI_STAGE) action = "X: Stage  ";
            else if (f.selected->action == BP_UI_SETTING) action = "X: Set  ";
        }
        snprintf(text, sizeof text, "%sSquare: Help  O: Back", action); print(text);
    }
}
static void draw_footer(MenuWidgetNode *node) { UiPacketScope s=packets_begin(4);draw_readonly(node, footer_row);packets_end(s); }
/* The game's four menu sounds, as the controller chose them. */
static void play(BpUiSound s) {
    switch (s) {
    case BP_UI_SOUND_MOVE: Menu_PlayMoveSound(); break;
    case BP_UI_SOUND_CONFIRM: Menu_PlayConfirmSound(); break;
    case BP_UI_SOUND_CANCEL: Menu_PlayCancelSound(); break;
    case BP_UI_SOUND_ERROR: Menu_PlayErrorSound(); break;
    default: break;
    }
#ifdef BP_UI_HEADLESS_TEST
    if (s) { printf("BP_UI_SOUND %s\n", s == BP_UI_SOUND_MOVE ? "move" : s == BP_UI_SOUND_CONFIRM ? "confirm" : s == BP_UI_SOUND_CANCEL ? "cancel" : "error"); fflush(stdout); }
#endif
}
static void answer(int yes) { bp_ui_answer(&ui, yes); play(ui.sound); }
static void sync_list(void) {
    pe_screen_rows(&screen, (int)(ui.page->row_count ? ui.page->row_count : 1), (int)ui.selected);
    screen.list->scroll_y = (int)ui.top;
    /* A short list keeps the same frame as a long list. Only its rows change. */
    screen.panel->visible_rows = layout.margin * 2 + layout.rows * layout.line;
}
static void event(BpUiInput key) {
    BpUiEvent e = bp_ui_input(&ui, key);
    if (e == BP_UI_EXIT) {
        pe_screen_close(&screen); restore_parent_panels(); opened = 0;
    } else if (e == BP_UI_REQUEST_CONFIRM) pe_confirm(ui.pending->question, 0, answer);
    else if (e == BP_UI_CHANGED) sync_list();
    play(ui.sound);
}
static int input(MenuWidgetNode *node, unsigned int buttons) {
    (void)node;
    if (pe_confirm_open()) return 1;
    if ((buttons & PE_MENU_CIRCLE) && !(buttons & 0x20000u)) event(BP_UI_CANCEL);
    else if ((buttons & PE_MENU_CROSS) && !(buttons & 0x20000u)) event(BP_UI_CONFIRM);
    else if ((buttons & 0x80u) && !(buttons & 0x20000u)) event(BP_UI_HELP);
    else if (buttons & 0x1000u) event(BP_UI_UP);
    else if (buttons & 0x4000u) event(BP_UI_DOWN);
    else if (buttons & 0x8000u) event(BP_UI_LEFT);
    else if (buttons & 0x2000u) event(BP_UI_RIGHT);
    else if (buttons & 0x4u) event(BP_UI_PAGE_PREV);
    else if (buttons & 0x8u) event(BP_UI_PAGE_NEXT);
    return 1;
}
static int panel(int x, int y, int width, int rows, void (*draw)(MenuWidgetNode *)) {
    MenuWidgetNode *list = pe_screen_panel(&screen, x, y, width, rows, draw);
    if (!list) return 0;
    list->cursor_x = -1;
    list->parent->visible_rows = 2 * list->y + rows * list->disabled;
    return 1;
}
static const BpUiCatalog *(*catalog_source)(void);
static const BpUiBackend *catalog_backend;
void pe_bp_ui_set_source(const BpUiCatalog *(*source)(void), const BpUiBackend *backend) {
    catalog_source = source; catalog_backend = backend;
}
int pe_bp_ui_preview_open(MenuWidgetNode *from, const char *page_id) {
    int x, y, width, height;
    const BpUiCatalog *live = catalog_source ? catalog_source() : NULL;
    if (opened || !from || pe_confirm_open() || !bp_ui_init(&ui, live ? live : bp_ui_demo_catalog(), page_id, 7)) return 0;
    if (live) bp_ui_set_backend(&ui, catalog_backend);
    pe_screen_open(&screen, from, UI_X, 64, UI_LEFT, 7, draw_list, input);
    hide_parent_panels(); screen.list->update = NULL;
    layout.line = screen.list->disabled; layout.margin = screen.list->y;
    if (layout.line <= 0 || layout.margin < 0) goto fail;
    layout.tabs_y = 2 * layout.margin + 2 * layout.line + 2;
    layout.body_y = layout.tabs_y + 2 * layout.margin + layout.line + 4;
    layout.footer_y = UI_HEIGHT - (2 * layout.margin + 2 * layout.line);
    layout.rows = (layout.footer_y - 4 - layout.body_y - 2 * layout.margin) / layout.line;
    layout.right_x = UI_X + UI_LEFT + UI_GAP;
    layout.right_width = UI_WIDTH - UI_LEFT - UI_GAP;
    if (layout.rows < 5) goto fail;
    screen.panel->y = layout.body_y; screen.list->visible_rows_mirror = layout.rows;
    bp_ui_set_visible(&ui, (size_t)layout.rows); sync_list();
    pe_screen_inside(screen.list, &x, &y, &width, &height);
    bp_ui_set_text_metrics(&ui, layout.right_width - 2 * screen.list->x - 4, measure);
    if (!panel(UI_X, 0, UI_WIDTH, 2, draw_header) ||
        !panel(UI_X, layout.tabs_y, UI_WIDTH, 1, draw_tabs) ||
        !panel(layout.right_x, layout.body_y, layout.right_width, layout.rows, draw_details) ||
        !panel(UI_X, layout.footer_y, UI_WIDTH, 2, draw_footer)) goto fail;
    opened = 1;
#ifdef BP_UI_HEADLESS_TEST
    printf("BP_UI_LAYOUT rows=%d line=%d body=%d footer=%d columns=%d/%d\n", layout.rows, layout.line, layout.body_y, layout.footer_y, UI_LEFT, layout.right_width);
#endif
    return 1;
fail:
    pe_screen_close(&screen); restore_parent_panels(); return 0;
}
#ifdef BP_UI_HEADLESS_TEST
void pe_bp_ui_preview_report(void) {
    static char last[768],last_focus[128]; char state[768],focus[128]; BpUiFrame f;
    if (!ui.page) return;
    bp_ui_frame(&ui, &f);
    snprintf(state, sizeof state,
        "BP_UI_STATE open=%d page=%s row=%s selected=%zu top=%zu detail=%zu pages=%zu pending=%d help=%d notice=%s",
        opened, ui.page->id, f.selected ? f.selected->id : "-", ui.selected, ui.top, ui.detail_page, f.detail_pages, !!ui.pending, ui.expanded, ui.notice);
    if (strcmp(state, last)) { puts(state); fflush(stdout); snprintf(last, sizeof last, "%s", state); }
    snprintf(focus,sizeof focus,"BP_UI_FOCUS x=%d y=%d w=%d h=%d",focus_rect.x,focus_rect.y,focus_rect.w,focus_rect.h);
    if (opened && strcmp(focus,last_focus)) { puts(focus);fflush(stdout);snprintf(last_focus,sizeof last_focus,"%s",focus); }
}
#endif
