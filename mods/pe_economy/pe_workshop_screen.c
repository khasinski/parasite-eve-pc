/*
 * The Workshop as a real menu screen, opened from Wayne's dialogue (Workshop Overhaul, plan step 1:
 * the spike). Proves a menu screen can open over the field from his choice box and hand control
 * back to the same box when it closes.
 *
 * Entering: what the field does on Start (Entity_FrameUpdate, src/main/entity/frame_update.c):
 * Render_BeginSceneLoad, field actors paused (g_GameStateBlock.flags 4) and the game in menu mode
 * (g_GameState.flags 0x9000). From the next frame Menu_RunFrameWithArg runs our widgets.
 * Leaving: what the game's own screens do on their last Circle, Inv_SetActiveList(9, 0): the
 * command result becomes -1 and the widget pool is emptied, so Entity_FrameUpdate clears the
 * flags and the field (and Wayne's box, still waiting for its answer) carries on.
 *
 * The rows are placeholders for now: Cross plays the error sound and says so in the title.
 */
#include <stdio.h>
#include <string.h>

#include "pe_economy.h"
#include "pe_gametext.h"
#include "pe_main_menu.h"
#include "pe_mod_host.h"
#include "pe1/draw_state.h"
#include "pe1/game_state.h"
#include "pe1/game_state_types.h"
#include "pe1/inventory_slots.h"
#include "pe1/menu_draw_list.h"
#include "pe1/menu_sound.h"
#include "pe1/menu_widget.h"
#include "pe1/scene_transition.h"

#define PE_WS_ROWS          6
#define PE_FIELD_PAUSED     4u        /* g_GameStateBlock.flags: field actors stop */
#define PE_GAME_IN_MENU     0x9000u   /* g_GameState.flags: the menu runs instead of the field */
#define PE_MENU_EXIT        9         /* Inv_SetActiveList: back to the field */

static const char *const s_rows[] = { "Requisition", "Rebuild", "Wardrobe", "Mod Permit Card" };
#define PE_WS_COUNT ((int)(sizeof s_rows / sizeof s_rows[0]))

static PeScreen s_screen;
static int s_open;
static char s_note[48];
static u8 s_text[PE_GAMETEXT_MAX];

static void pe_ws_row(int row) {
    if (row >= 0 && row < PE_WS_COUNT) Draw_PrintRawText(pe_text_from_ascii(s_text, sizeof s_text, s_rows[row]));
}

static void pe_ws_draw(MenuWidgetNode *node) { MenuWidget_DrawList(node, pe_ws_row); }

static void pe_ws_close(void) {
    pe_screen_close(&s_screen);
    s_open = 0;
    Inv_SetActiveList(PE_MENU_EXIT, 0);
    pe_log("workshop", "screen closed");
}

static int pe_ws_input(MenuWidgetNode *panel, unsigned int buttons) {
    (void)panel;
    if (buttons & PE_MENU_CIRCLE) { Menu_PlayCancelSound(); pe_ws_close(); return 1; }
    if (buttons & PE_MENU_CROSS) {
        const int row = s_screen.list ? MenuWidget_GridCellIndex(s_screen.list) : 0;
        snprintf(s_note, sizeof s_note, "%s: not built yet", row >= 0 && row < PE_WS_COUNT ? s_rows[row] : "?");
        Menu_PlayErrorSound();
        return 1;
    }
    return 0;
}

/* The title bar: the note after Cross, else the controls. */
static int pe_ws_help(void *ctx, void *user) {
    u8 **text = (u8 **)ctx;
    (void)user;
    if (!s_open || !s_screen.list || MenuWidget_GetCurrentNode() != s_screen.list) return 0;
    *text = pe_text_from_ascii(s_text, sizeof s_text, s_note[0] ? s_note : "Workshop   X: Open   O: Back");
    return 0;
}

int pe_workshop_screen_open(void) {
    if (s_open) return 1;
    s_note[0] = '\0';
    Render_BeginSceneLoad();
    g_GameStateBlock.flags |= PE_FIELD_PAUSED;
    g_GameState.flags |= PE_GAME_IN_MENU;
    pe_screen_open_game(&s_screen, NULL, PE_WS_ROWS, pe_ws_draw, pe_ws_input);
    pe_screen_rows(&s_screen, PE_WS_COUNT, 0);
    s_open = 1;
    pe_log("workshop", "screen opened from Wayne's menu");
    return 1;
}

/* Open, or the game still in the menu it left (the frame the flags clear). */
int pe_workshop_screen_busy(void) { return s_open || (g_GameState.flags & PE_GAME_IN_MENU) != 0; }

void pe_workshop_screen_register(const char *mod_id) {
    pe_hook_add(PE_HOOK_HELP_TEXT, pe_ws_help, NULL, PE_PRIORITY_NORMAL, mod_id);
}
