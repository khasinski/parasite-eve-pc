/* Aya's main menu: extra rows, and screens from the game's widgets. See pe_main_menu.h. */
#include <string.h>

#include "pe_game.h"
#include "pe_gametext.h"
#include "pe_main_menu.h"
#include "pe_mod_host.h"
#include "pe1/inventory_slots.h"
#include "pe1/draw_state.h"
#include "pe1/menu_dialog.h"
#include "pe1/render_prim.h"
#include "pe1/text.h"
#include "pe1/menu_scroll_cursor.h"
#include "pe1/menu_state.h"
#include "pe1/menu_widget.h"

void Menu_DrawItemList(MenuWidgetNode *node);

#define PE_HOST_ID      "host"
#define PE_ROWS_MAX     8
#define PE_DESC_PANEL   0x20   /* the options menu's panel and list layout */
#define PE_TITLE_BASE   17     /* a selected_base whose title-bar case always prints */

/* ---- extra rows ---- */

static struct { const PeMainMenuRow *row; int order; const char *mod_id; } s_rows[PE_ROWS_MAX];
static int s_row_count;
static u8 s_row_title[64];

void pe_main_menu_add(const PeMainMenuRow *row, int order) {
    int i;
    if (!row || s_row_count == PE_ROWS_MAX) return;
    for (i = s_row_count; i > 0 && s_rows[i - 1].order > order; --i) s_rows[i] = s_rows[i - 1];
    s_rows[i].row = row;
    s_rows[i].order = order;
    s_rows[i].mod_id = pe_mod_current();          /* an inactive mod's row isn't shown */
    ++s_row_count;
}

static int pe_menu_open_here(void) { return pe_game_loaded() && !pe_in_battle() && !Menu_GetBattleEquipMode(); }

static int pe_shown(int i) { return pe_mod_active(s_rows[i].mod_id) && (!s_rows[i].row->shown || s_rows[i].row->shown()); }

/* The game's rows: one per set action bit. */
static int pe_game_rows(void) {
    int bits = g_MenuItemContextFlag & 0x1EF, n = 0;
    while (bits) { n += bits & 1; bits >>= 1; }
    return n;
}

/* The extra row at list row `row`, or NULL. */
static const PeMainMenuRow *pe_row_at(int row) {
    int k = row - pe_game_rows(), i;
    if (k < 0) return NULL;
    for (i = 0; i < s_row_count; ++i)
        if (pe_shown(i) && k-- == 0) return s_rows[i].row;
    return NULL;
}

static int pe_is_main_list(const MenuWidgetNode *node) { return node && node->draw == (void (*)())Menu_DrawItemList; }

static int pe_on_list_rows(void *ctx, void *user) {
    const PeHookListRows *r = (const PeHookListRows *)ctx;
    int i, added = 0;
    (void)user;
    if (!pe_is_main_list(r->node) || !pe_menu_open_here()) return 0;
    for (i = 0; i < s_row_count; ++i) added += pe_shown(i);
    if (!added) return 0;
    *r->count += added;
    /* The main list shows at most visible_rows_mirror rows (8, its layout's cap): with every game
     * row present ours would sit past it. Raise the cap; the game grows the panel. */
    if (r->node->visible_rows_mirror < *r->count) r->node->visible_rows_mirror = *r->count;
    return 0;
}

static int pe_on_icon(void *ctx, void *user) {
    PeHookMainMenuIcon *icon = (PeHookMainMenuIcon *)ctx;
    const PeMainMenuRow *row;
    (void)user;
    if (icon->action != 9 || !pe_menu_open_here() || !(row = pe_row_at(icon->row))) return 0;
    if (row->icon) { row->icon(icon); return 0; }
    if (row->icon_item) {
        const ItemDataRecord *rec = Inv_LookupItemRecordScaled(row->icon_item);
        if (rec && rec->iconId) icon->sprite = rec->iconId - 1;
    }
    return 0;
}

void pe_icon_sprite(int sprite, int dx, int dy) {
    Draw_OffsetCursor(dx, dy);
    Draw_AllocSprite(sprite);
    Draw_OffsetCursor(-dx, -dy);
}

/* A textured quad the way the game's glyphs are drawn (Draw_EmitGlyph), from a sprite's page and CLUT. */
void pe_icon_part(int sprite, int u, int v, int w, int h, int dx, int dy, int dw, int dh) {
    const DrawGlyphDescriptor *g = Draw_LookupGlyphDescriptor(sprite);
    const size_t used = (size_t)(g_DrawPacketCursor - g_DrawPacketArenaBase);
    RenderTexturedQuad *q;
    RenderNativeTag *ot;
    int x0, y0;
    if (!g || used >= 0x4000 || sizeof *q >= 0x4000 - used) return;
    q = (RenderTexturedQuad *)g_DrawPacketCursor;
    g_DrawPacketCursor += sizeof *q;
    q->color.word = g_DrawTextDimmed ? g_DrawColorShaded : g_DrawPrimColor;
    q->tag.length = 9;
    q->color.bytes.code = 0x2C;                          /* textured four-point polygon */
    x0 = g_TextCursorX + dx;
    y0 = g_TextCursorY + dy;
    q->x0 = q->x2 = (u16)x0;
    q->x1 = q->x3 = (u16)(x0 + dw);
    q->y0 = q->y1 = (u16)y0;
    q->y2 = q->y3 = (u16)(y0 + dh);
    q->u0 = q->u2 = (u8)(g->u + u);
    q->u1 = q->u3 = (u8)(g->u + u + w);
    q->v0 = q->v1 = (u8)(g->v + v);
    q->v2 = q->v3 = (u8)(g->v + v + h);
    q->clut = g->clut;
    q->tpage = (u16)(((g->mode & 3) << 7) | 7);          /* the page Draw_AllocSprite sets */
    ot = (RenderNativeTag *)g_DrawOrderingTableEntry;
    q->tag.address = ot->address;
    ot->address = (uintptr_t)q;
}

static int pe_on_row_title(void *ctx, void *user) {
    u8 **text = (u8 **)ctx;
    MenuWidgetNode *current = MenuWidget_GetCurrentNode();
    const PeMainMenuRow *row;
    (void)user;
    if (!pe_is_main_list(current) || !pe_menu_open_here()) return 0;
    if ((row = pe_row_at(MenuWidget_GridCellIndex(current)))) *text = pe_text_from_ascii(s_row_title, sizeof s_row_title, row->title);
    return 0;
}

static int pe_on_main_action(void *ctx, void *user) {
    const PeHookMenuAction *a = (const PeHookMenuAction *)ctx;
    const PeMainMenuRow *row;
    (void)user;
    if (a->action != 9 || !pe_menu_open_here() || !(row = pe_row_at(MenuWidget_GridCellIndex(a->list)))) return 0;
    pe_mod_log(PE_HOST_ID, "main menu: %s", row->title);
    row->open(a->list);
    return 1;                                              /* handled: the game plays the confirm sound */
}

/* ---- screens ----
 * Like the game's own screens, a screen hides Aya's status panels (mode-1 panels 0x12, 0x18, 0x2D,
 * hidden while their cursor_y is set: MenuWidget_UpdateAndDraw) and the HP overlay, and puts them
 * back when it closes. */
extern int g_MenuBattleStatusOverlayActive;
enum { PE_HIDDEN_MAX = 4 };
static struct { MenuWidgetNode *node[PE_HIDDEN_MAX]; int selected[PE_HIDDEN_MAX]; int overlay; } s_hidden;

static void pe_hide_panels(void) {
    MenuWidgetNode *node = g_MenuWidgetActiveListHead;
    int n = 0, guard;
    memset(&s_hidden, 0, sizeof s_hidden);
    for (guard = 0; node && guard < 64 && n < PE_HIDDEN_MAX; ++guard, node = node->next) {
        const int sel = node->selected_base;
        if (node->mode == 1 && (sel == 0x12 || sel == 0x18 || sel == 0x2D) && node->cursor_y == 0) {
            node->cursor_y = 1;
            s_hidden.node[n] = node;
            s_hidden.selected[n++] = sel;
        }
    }
    s_hidden.overlay = g_MenuBattleStatusOverlayActive;
    g_MenuBattleStatusOverlayActive = 0;
}

static void pe_show_panels(void) {
    int i;
    for (i = 0; i < PE_HIDDEN_MAX; ++i) {
        MenuWidgetNode *node = s_hidden.node[i];
        if (node && node->mode == 1 && node->selected_base == s_hidden.selected[i]) node->cursor_y = 0;
    }
    g_MenuBattleStatusOverlayActive = s_hidden.overlay;
    memset(&s_hidden, 0, sizeof s_hidden);
}

/* The stock grid handler advances the viewport by one row, even when its Options layout wraps
 * the cursor to the opposite end. Keep those jumps (and input arriving during a scroll animation)
 * inside the list's viewport. */
static int pe_scroll_input(MenuWidgetNode *list, unsigned int buttons) {
    int handled = Menu_StepScrollCursor(list, buttons);
    int top = list->scroll_y;
    if (list->cursor_y < top) top = list->cursor_y;
    else if (list->cursor_y >= top + list->visible_rows) top = list->cursor_y - list->visible_rows + 1;
    if (top != list->scroll_y) {
        list->scroll_y = top;
        list->scroll_adjust = 0;
    }
    return handled;
}

/* Model browsers are one-column lists. The stock grid consumes horizontal arrows
 * even without a neighbouring column; let them bubble to the screen's rotation handler. */
int pe_screen_preview_input(MenuWidgetNode *list, unsigned int buttons) {
    if (buttons & (0x8000u | 0x2000u)) return 0;
    return pe_scroll_input(list, buttons);
}

void pe_screen_open(PeScreen *s, MenuWidgetNode *from, int x, int y, int width, int rows_visible,
                    void (*draw)(MenuWidgetNode *), int (*input)(MenuWidgetNode *, unsigned int)) {
    pe_hide_panels();
    s->from = from;
    s->extras = 0;
    s->panel = MenuWidget_CreateSimpleNode(PE_DESC_PANEL, from, 0, 0);
    s->list = MenuWidget_CreateNode(PE_DESC_PANEL, s->panel, s->panel);
    s->panel->update = input;
    s->panel->x = x;
    s->panel->y = y;
    s->panel->grid_width = width;
    /* The list clips to cell width, independently of the panel's background. Replace the borrowed
     * Options layout's 68-pixel cell with the panel's inner width (the list's x offset supplies
     * the margin on either side). */
    s->list->draw_state = width - 2 * s->list->x;
    s->list->draw = (void (*)())draw;
    s->list->update = pe_scroll_input;
    s->list->selected_base = PE_TITLE_BASE;
    s->list->visible_rows_mirror = rows_visible;
    MenuWidget_SetCurrentNode(s->list);
}

#define PE_DESC_GAME_SCREEN 1   /* Aya's inventory panel (Menu_CreateEquipScreen) */

void pe_screen_open_game(PeScreen *s, MenuWidgetNode *from, int rows_visible,
                         void (*draw)(MenuWidgetNode *), int (*input)(MenuWidgetNode *, unsigned int)) {
    const MenuWidgetSimpleDescriptor *d = MenuWidget_LookupSimpleDescriptor(PE_DESC_GAME_SCREEN);
    const int x = d && d->x > 0 ? d->x : 28, y = d && d->y > 0 ? d->y : 24, width = d && d->width > 0 ? d->width : 252;
    pe_screen_open(s, from, x, y, width, rows_visible, draw, input);
}

void pe_screen_close(PeScreen *s) {
    /* Extra panels hang off the screen's panel without a child slot (CreateSimpleNode's slot
     * parent is 0), so destroying the panel doesn't reach them. */
    while (s->extras > 0) MenuWidget_DestroyNode(s->extra[--s->extras]);
    if (s->panel) MenuWidget_DestroyNode(s->panel);
    s->panel = s->list = NULL;
    pe_show_panels();
    if (s->from) MenuWidget_SetCurrentNode(s->from);
}

static void pe_draw_nothing(MenuWidgetNode *node) { (void)node; }

MenuWidgetNode *pe_screen_panel(PeScreen *s, int x, int y, int width, int rows, void (*draw)(MenuWidgetNode *)) {
    MenuWidgetNode *panel, *list;
    if (!s->panel || s->extras == PE_SCREEN_EXTRA) return NULL;
    panel = MenuWidget_CreateSimpleNode(PE_DESC_PANEL, s->panel, 0, 0);
    if (!panel) return NULL;
    s->extra[s->extras++] = panel;
    list = MenuWidget_CreateNode(PE_DESC_PANEL, panel, panel);
    if (!list) return NULL;
    panel->update = NULL;
    panel->x = x;
    panel->y = y;
    panel->grid_width = width;
    list->draw_state = width - 2 * list->x;
    list->draw = (void (*)())(draw ? draw : pe_draw_nothing);
    list->update = NULL;
    list->selected_base = PE_TITLE_BASE;
    list->visible_rows_mirror = rows;
    Draw_SetPrimCallback(list, rows);
    return list;
}

void pe_screen_inside(const MenuWidgetNode *list, int *x, int *y, int *w, int *h) {
    const MenuWidgetNode *panel = list ? list->parent : NULL;
    *x = (panel ? panel->x : 0) + (list ? list->x : 0);
    *y = (panel ? panel->y : 0) + (list ? list->y : 0);
    *w = list ? list->draw_state : 0;
    *h = list ? list->disabled * list->visible_rows_mirror : 0;   /* row height x rows */
}

void pe_screen_rows(PeScreen *s, int rows, int cursor) {
    const int visible = s->list->visible_rows_mirror;
    s->list->scroll_adjust = 0;
    s->list->cursor_x = 0;
    s->list->cursor_y = cursor >= 0 && cursor < rows ? cursor : 0;
    s->list->scroll_y = s->list->cursor_y >= visible ? s->list->cursor_y - visible + 1 : 0;
    Draw_SetPrimCallback(s->list, rows);
}

int pe_screen_title_base(void) { return PE_TITLE_BASE; }

/* ---- Yes / No: the game's item-detail confirmation (Menu_StepItemDetailPanel), our question ---- */
static MenuWidgetNode *s_confirm_from, *s_confirm_choice;
static void (*s_confirm_answer)(int yes);

static void pe_confirm_done(MenuWidgetNode *dialog, int yes) {
    void (*answer)(int) = s_confirm_answer;
    (void)dialog;
    s_confirm_answer = NULL;
    s_confirm_choice = NULL;
    if (s_confirm_from) MenuWidget_SetCurrentNode(s_confirm_from);
    if (answer) answer(yes);
}

void pe_confirm(const char *question, int default_yes, void (*answer)(int yes)) {
    MenuWidgetNode *panel, *choice;
    int width;
    s_confirm_from = MenuWidget_GetCurrentNode();
    panel = MenuWidget_CreateSimpleNode(0x29, s_confirm_from, 0, 1);
    choice = MenuWidget_CreateNode(0x29, panel, panel);
    panel->draw = Menu_DrawItemLabel;                    /* prints g_MenuItemLabelBuffer */
    panel->update = Menu_ConfirmDialogHandler;
    choice->draw = Menu_DrawActionOptionList;
    g_MenuActionTextBase = 5;                            /* Yes / No */
    MenuWidget_SetCurrentNode(choice);
    choice->cursor_x = default_yes ? 0 : 1;
    pe_text_from_ascii(g_MenuItemLabelBuffer, sizeof g_MenuItemLabelBuffer, question);
    g_MenuItemPrimListHandles[0] = 0;
    width = Draw_MeasureTextWidth(g_MenuItemLabelBuffer);
    if (width < 120) width = 120;                        /* the game's layout */
    panel->grid_width = width + 20;
    panel->visible_rows = 50;
    panel->x = (300 - width) >> 1;
    choice->x = (panel->grid_width - 128) >> 1;
    choice->y = panel->visible_rows - 20;
    s_confirm_choice = choice;
    s_confirm_answer = answer;
    g_MenuConfirmCallback = pe_confirm_done;
}

int pe_confirm_open(void) { return s_confirm_choice && MenuWidget_GetCurrentNode() == s_confirm_choice; }

void pe_main_menu_init(void) {
    pe_hook_add(PE_HOOK_LIST_ROWS, pe_on_list_rows, NULL, PE_PRIORITY_NORMAL, PE_HOST_ID);
    pe_hook_add(PE_HOOK_MAIN_MENU_ICON, pe_on_icon, NULL, PE_PRIORITY_NORMAL, PE_HOST_ID);
    pe_hook_add(PE_HOOK_HELP_TEXT, pe_on_row_title, NULL, PE_PRIORITY_LATE, PE_HOST_ID);
    pe_hook_add(PE_HOOK_MAIN_MENU_ACTION, pe_on_main_action, NULL, PE_PRIORITY_NORMAL, PE_HOST_ID);
}
