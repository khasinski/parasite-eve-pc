/*
 * Wayne's Workshop as a real menu screen, opened from his dialogue (Workshop Overhaul, plan steps
 * 1 and 2). One screen in the Bestiary's layout, a list over a details panel, in two modes:
 *   Workshop     the services: Requisition, Rebuild, Wardrobe (while a mod offers costumes), Mod
 *                Permit Card; the panel says what the highlighted one does. Circle: back to Wayne.
 *   Requisition  the goods with their prices (pe_workshop_model.c applies the rules: gray rows,
 *                "owned", the game's Yes / No on every purchase); the panel gives the item's
 *                details and why it can't be bought, or "X: Buy". Circle: back to the Workshop.
 * Rebuild, Wardrobe and Mod Permit Card get their own screens in the next step; until then those
 * rows close this screen and Wayne's box shows their dialogue version (pe_wayne_menu.c).
 *
 * Entering: what the field does on Start (Entity_FrameUpdate, src/main/entity/frame_update.c):
 * Render_BeginSceneLoad, field actors paused (g_GameStateBlock.flags 4) and the game in menu mode
 * (g_GameState.flags 0x9000). From the next frame Menu_RunFrameWithArg runs our widgets.
 * Leaving: what the game's own screens do on their last Circle, Inv_SetActiveList(9, 0): the
 * command result becomes -1 and the widget pool is emptied, so Entity_FrameUpdate clears the
 * flags and the field (and Wayne's box, still waiting for its answer) carries on.
 */
#include <stdio.h>
#include <string.h>

#include "pe_economy.h"
#include "pe_gametext.h"
#include "pe_main_menu.h"
#include "pe_mod_host.h"
#include "pe_requisition.h"
#include "pe_workshop_model.h"
#include "pe1/draw_state.h"
#include "pe1/game_state.h"
#include "pe1/game_state_types.h"
#include "pe1/inventory_slots.h"
#include "pe1/menu_draw_list.h"
#include "pe1/menu_sound.h"
#include "pe1/menu_widget.h"
#include "pe1/scene_transition.h"

#define PE_WS_ROWS          6         /* as the Bestiary */
#define PE_WS_INFO_ROWS     2
#define PE_WS_GAP           4
#define PE_FIELD_PAUSED     4u        /* g_GameStateBlock.flags: field actors stop */
#define PE_GAME_IN_MENU     0x9000u   /* g_GameState.flags: the menu runs instead of the field */
#define PE_MENU_EXIT        9         /* Inv_SetActiveList: back to the field */

void pe_wayne_menu_after_workshop(int service);   /* pe_wayne_menu.c: show its dialogue version */

enum { MODE_WORKSHOP, MODE_REQUISITION };

static PeScreen s_screen;
static MenuWidgetNode *s_info;
static int s_open, s_mode;
static int s_services[PE_WS_SERVICES], s_nservices;
static int s_req_cursor;                      /* Requisition's row, kept while the screen is open */
static char s_note[64];
static int s_buy_row = -1;                    /* the row the open Yes / No is about */
static u8 s_text[PE_GAMETEXT_MAX];

static int pe_cursor(void) { return s_screen.list ? MenuWidget_GridCellIndex(s_screen.list) : 0; }

/* ---- drawing ---- */
static void pe_print(const char *ascii) { Draw_PrintRawText(pe_text_from_ascii(s_text, sizeof s_text, ascii)); }

/* `ascii` at the right end of the row, `width` pixels from where the row starts. */
static void pe_print_right(const char *ascii, int width) {
    const int x = g_TextCursorX, y = g_TextCursorY;
    pe_text_from_ascii(s_text, sizeof s_text, ascii);
    g_TextCursorX = x + width - Draw_MeasureTextWidth(s_text);
    Draw_PrintRawText(s_text);
    g_TextCursorX = x;
    g_TextCursorY = y;
}

static void pe_row(int row) {
    const int width = g_DrawTextPosX - 4;
    if (s_mode == MODE_WORKSHOP) {
        if (row >= 0 && row < s_nservices) pe_print(pe_ws_service_name(s_services[row]));
        return;
    }
    {
        PeWsGoods g;
        char price[24];
        const int x = g_TextCursorX, y = g_TextCursorY;
        if (!pe_ws_goods(row, &g)) return;
        if (g.blocked) Draw_SetTextDimmed(1);                /* the game's gray, never red */
        if (g.name) pe_print(g.name);
        else {
            PeText t;
            pe_t_init(&t);
            pe_t_item(&t, g.id);                             /* the game's own item name */
            pe_t_byte(&t, G_END);
            Draw_PrintRawText(t.b);
        }
        g_TextCursorX = x;
        g_TextCursorY = y;
        if (g.owned) snprintf(price, sizeof price, "owned");
        else snprintf(price, sizeof price, "%u", (unsigned)g.price);
        pe_print_right(price, width);
        Draw_SetTextDimmed(0);
    }
}

static void pe_draw_list(MenuWidgetNode *node) { MenuWidget_DrawList(node, pe_row); }

static void pe_info_row(int row) {
    char line[96];
    line[0] = 0;
    if (s_mode == MODE_WORKSHOP) {
        const int c = pe_cursor();
        if (row == 0) snprintf(line, sizeof line, "Junk %u", (unsigned)pe_loot_junk());
        else if (c >= 0 && c < s_nservices) snprintf(line, sizeof line, "%s", pe_ws_service_help(s_services[c]));
    } else {
        PeWsGoods g;
        if (!pe_ws_goods(pe_cursor(), &g)) { if (row == 0) snprintf(line, sizeof line, "Nothing for sale now"); }
        else if (row == 0) pe_shop_details(g.id, line, sizeof line);
        else snprintf(line, sizeof line, "%s", g.owned ? "Already owned" : g.blocked ? g.blocked : "X: Buy");
    }
    if (line[0]) pe_print(line);
}

static void pe_draw_info(MenuWidgetNode *node) { MenuWidget_DrawList(node, pe_info_row); }

/* ---- modes ---- */
static void pe_show_mode(int mode, int cursor) {
    s_mode = mode;
    s_note[0] = 0;
    if (mode == MODE_WORKSHOP) {
        s_nservices = pe_ws_services(s_services);
        pe_screen_rows(&s_screen, s_nservices, cursor);
    } else {
        const int n = pe_shop_count();
        if (cursor >= n) cursor = n > 0 ? n - 1 : 0;
        pe_screen_rows(&s_screen, n > 0 ? n : 1, cursor);
    }
}

static void pe_close(void) {
    pe_screen_close(&s_screen);
    s_info = NULL;
    s_open = 0;
    pe_shop_at_counter(0);
    Inv_SetActiveList(PE_MENU_EXIT, 0);
    pe_log("workshop", "screen closed");
}

/* ---- buying: the game's Yes / No, cursor on No ---- */
static void pe_bought(int yes) {
    PeWsGoods g;
    char said[64] = "";
    const int row = s_buy_row;
    s_buy_row = -1;
    if (!yes) { Menu_PlayCancelSound(); return; }
    if (!pe_ws_goods(row, &g) || g.blocked) {                 /* changed while the question was up */
        Menu_PlayErrorSound();
        snprintf(s_note, sizeof s_note, "%s", g.blocked ? g.blocked : "Gone");
        return;
    }
    if (pe_shop_buy(g.id, said, sizeof said)) Menu_PlayConfirmSound();   /* every purchase, repeats too */
    else Menu_PlayErrorSound();
    pe_show_mode(MODE_REQUISITION, row);                     /* the board may change */
    snprintf(s_note, sizeof s_note, "%s", said[0] ? said : "Bought");
}

static void pe_requisition_cross(void) {
    char msg[96];
    const int row = pe_cursor();
    if (pe_ws_cross(row, msg, sizeof msg) == PE_WS_REFUSE) {
        Menu_PlayErrorSound();
        snprintf(s_note, sizeof s_note, "%s", msg);
        return;
    }
    s_buy_row = row;
    pe_confirm(msg, 0, pe_bought);
}

static int pe_input(MenuWidgetNode *panel, unsigned int buttons) {
    (void)panel;
    if (pe_confirm_open()) return 0;
    if (buttons & PE_MENU_CIRCLE) {
        Menu_PlayCancelSound();
        if (s_mode == MODE_REQUISITION) {
            int i, back = 0;
            s_req_cursor = pe_cursor();
            for (i = 0; i < s_nservices; ++i) if (s_services[i] == PE_WS_REQUISITION) back = i;
            pe_show_mode(MODE_WORKSHOP, back);
        } else pe_close();
        return 1;
    }
    if (!(buttons & PE_MENU_CROSS)) return 0;
    if (s_mode == MODE_REQUISITION) { pe_requisition_cross(); return 1; }
    {
        const int c = pe_cursor(), service = c >= 0 && c < s_nservices ? s_services[c] : -1;
        Menu_PlayConfirmSound();
        if (service == PE_WS_REQUISITION) pe_show_mode(MODE_REQUISITION, s_req_cursor);
        else if (service >= 0) {                              /* not a screen yet: Wayne's box shows it */
            pe_wayne_menu_after_workshop(service);
            pe_close();
        }
    }
    return 1;
}

/* The title bar: what just happened, else where we are, your Junk and the controls. */
static int pe_help(void *ctx, void *user) {
    u8 **text = (u8 **)ctx;
    char line[96];
    (void)user;
    if (!s_open || !s_screen.list || MenuWidget_GetCurrentNode() != s_screen.list) return 0;
    if (s_note[0]) snprintf(line, sizeof line, "%s", s_note);
    else if (s_mode == MODE_WORKSHOP) snprintf(line, sizeof line, "Workshop   X: Open   O: Back to Wayne");
    else snprintf(line, sizeof line, "Requisition   Junk %u   X: Buy   O: Back", (unsigned)pe_loot_junk());
    *text = pe_text_from_ascii(s_text, sizeof s_text, line);
    return 0;
}

int pe_workshop_screen_open(void) {
    int ix, iy, iw, ih;
    if (s_open) return 1;
    s_req_cursor = 0;
    s_buy_row = -1;
    Render_BeginSceneLoad();
    g_GameStateBlock.flags |= PE_FIELD_PAUSED;
    g_GameState.flags |= PE_GAME_IN_MENU;
    pe_screen_open_game(&s_screen, NULL, PE_WS_ROWS, pe_draw_list, pe_input);
    pe_screen_inside(s_screen.list, &ix, &iy, &iw, &ih);
    s_info = pe_screen_panel(&s_screen, s_screen.panel->x, iy + ih + 3 * PE_WS_GAP, s_screen.panel->grid_width,
                             PE_WS_INFO_ROWS, pe_draw_info);
    pe_shop_at_counter(1);                                    /* buying here is at Wayne's counter */
    s_open = 1;
    pe_show_mode(MODE_WORKSHOP, 0);
    pe_log("workshop", "screen opened from Wayne's menu");
    return 1;
}

/* Open, or the game still in the menu it left (the frame the flags clear). */
int pe_workshop_screen_busy(void) { return s_open || (g_GameState.flags & PE_GAME_IN_MENU) != 0; }

void pe_workshop_screen_register(const char *mod_id) {
    pe_hook_add(PE_HOOK_HELP_TEXT, pe_help, NULL, PE_PRIORITY_NORMAL, mod_id);
}
