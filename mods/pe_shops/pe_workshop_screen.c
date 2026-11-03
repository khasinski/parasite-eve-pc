/*
 * Wayne's Workshop as a real menu screen, opened from his dialogue (Workshop Overhaul, plan steps
 * 1 and 2). The Bestiary's layout, a list over a details panel, in four modes:
 *   Workshop     the services: Requisition, Rebuild, Wardrobe (while a mod offers costumes), Mod
 *                Permit Card; the panel says what the highlighted one does. Circle: back to Wayne.
 *   Requisition  the goods with their prices (pe_workshop_model.c applies the rules: gray rows,
 *                "owned", the game's Yes / No on every purchase); the panel gives the item's
 *                details and why it can't be bought, or "X: Buy". Circle: back to the Workshop.
 *   Rebuild      dismantled guns and armor with their prices, scrolling like Requisition; the
 *                panel shows the rebuilt item as the game's Change Weapon / Change Armor screens
 *                do: its three stats under the game's own labels, the "+N" bonuses, and its slots.
 *   Wardrobe     outfits with their prices (or owned / worn) beside the model turning in a frame,
 *                as the Wardrobe browser; Cross buys (after Yes / No) or wears an owned one.
 * Mod Permit Card isn't a screen yet: its row closes this screen and Wayne's box shows its
 * dialogue version (pe_wayne_menu.c).
 *
 * Entering: what the field does on Start (Entity_FrameUpdate, src/main/entity/frame_update.c):
 * Render_BeginSceneLoad, field actors paused (g_GameStateBlock.flags 4) and the game in menu mode
 * (g_GameState.flags 0x9000). From the next frame Menu_RunFrameWithArg runs our widgets. The menus
 * confirm on Cross's release (MenuInput_DispatchQueuedEvents), and the Cross that chose Workshop
 * in Wayne's box is still down: the menu input starts inactive, as Queue_Init leaves it, so it
 * waits for every button to be up and that release never reaches the screen.
 * Each mode lays the widgets out afresh (the Wardrobe's list is narrower, beside its frame).
 * Leaving: what the game's own screens do on their last Circle, Inv_SetActiveList(9, 0): the
 * command result becomes -1 and the widget pool is emptied, so Entity_FrameUpdate clears the
 * flags and the field (and Wayne's box, still waiting for its answer) carries on.
 */
#include <stdio.h>
#include <string.h>

#include "pe_shops.h"
#include "pe_gametext.h"
#include "pe_main_menu.h"
#include "pe_mod_host.h"
#include "pe_model_stage.h"
#include "pe_requisition.h"
#include "pe_services.h"
#include "pe_workshop_model.h"
#include "pe1/draw_state.h"
#include "pe1/game_state.h"
#include "pe1/game_state_types.h"
#include "pe1/inventory.h"
#include "pe1/inventory_slots.h"
#include "pe1/menu_context_help.h"
#include "pe1/menu_draw_list.h"
#include "pe1/menu_queue.h"
#include "pe1/menu_sound.h"
#include "pe1/menu_widget.h"
#include "pe1/scene_transition.h"
#include "pe1/text.h"

#define PE_WS_ROWS          6         /* as the Bestiary */
#define PE_WS_GAP           4
#define PE_WARD_LIST_W      124       /* the Wardrobe's list beside its model, as the browser */
#define PE_PICTURE          128
#define PE_FIELD_PAUSED     4u        /* g_GameStateBlock.flags: field actors stop */
#define PE_GAME_IN_MENU     0x9000u   /* g_GameState.flags: the menu runs instead of the field */
#define PE_MENU_EXIT        9         /* Inv_SetActiveList: back to the field */
#define PE_ARMOR_KIND       9         /* ItemDataRecord.kind: weapons 1-8 */
#define PE_LABEL_WEAPON     0x7C      /* the equip screens' stat labels: Attack / Range / Bullets */
#define PE_LABEL_ARMOR      0x7F      /* Defense / P.Energy / Critical (Menu_DrawEquipStatsDelta) */
#define PE_STAT_VALUE_X     30        /* the value's column, after the label, as the equip screens */
#define PE_STAT_BONUS_X     74
#define PE_STAT_MAX         999
#define PE_LEFT             0x8000u
#define PE_RIGHT            0x2000u
#define PE_TRIANGLE         0x10u
#define PE_SQUARE           0x80u
#define PE_REPEAT           0x20000u

void pe_wayne_menu_after_workshop(int service);   /* pe_wayne_menu.c: show its dialogue version */

enum { MODE_WORKSHOP, MODE_REQUISITION, MODE_REBUILD, MODE_WARDROBE, MODES };
static const int s_info_rows[MODES] = { 2, 2, 4, 3 };

static PeScreen s_screen;
static MenuWidgetNode *s_info, *s_frame;
static PeModelStage s_stage;
static int s_open, s_mode;
static int s_services[PE_WS_SERVICES], s_nservices;
static int s_cursor[MODES];                   /* each mode's row, kept while the screen is open */
static char s_note[64];
static int s_buy_row = -1;                    /* the row the open Yes / No is about */
static int s_tick, s_drawn_tick, s_shown = -1, s_preview, s_paused;
static u8 s_text[PE_GAMETEXT_MAX];

static int pe_cursor(void) { return s_screen.list ? MenuWidget_GridCellIndex(s_screen.list) : 0; }
static const PeWardrobeApi *pe_ward(void) { return (const PeWardrobeApi *)pe_service_get("wardrobe", 1); }

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

/* Cut game text to `width` pixels, ending in "..." when it had to be cut (the font varies in width). */
static void pe_fit(u8 *text, size_t size, int width) {
    size_t n = 0;
    while (n < size - 1 && text[n] != G_END) ++n;
    if (Draw_MeasureTextWidth(text) <= width) return;
    while (n && (text[n] = G_END, Draw_MeasureTextWidth(text) > width)) --n;
    if (n >= 3) text[n - 3] = text[n - 2] = text[n - 1] = pe_glyph('.');
    while (n && Draw_MeasureTextWidth(text) > width) text[--n] = G_END;
}

/* A name on the left (the game's own name for base item `item` when `name` is NULL), cut so the
 * price (or "owned") fits on the right; gray when it can't be had now. */
static void pe_priced_row(const char *name, uint16_t item, const char *price, int dim) {
    const int width = g_DrawTextPosX - 4, x = g_TextCursorX, y = g_TextCursorY;
    u8 label[PE_GAMETEXT_MAX];
    int room;
    pe_text_from_ascii(s_text, sizeof s_text, price);
    room = width - Draw_MeasureTextWidth(s_text) - 8;
    if (name) pe_text_from_ascii(label, sizeof label, name);
    else {
        PeText t;
        pe_t_init(&t);
        pe_t_item(&t, item);
        pe_t_byte(&t, G_END);
        memcpy(label, t.b, t.n < sizeof label ? t.n : sizeof label);
        label[sizeof label - 1] = G_END;
    }
    pe_fit(label, sizeof label, room);
    if (dim) Draw_SetTextDimmed(1);                          /* the game's gray, never red */
    Draw_PrintRawText(label);
    g_TextCursorX = x;
    g_TextCursorY = y;
    pe_print_right(price, width);
    Draw_SetTextDimmed(0);
}

static void pe_row(int row) {
    char price[24];
    switch (s_mode) {
    case MODE_WORKSHOP:
        if (row >= 0 && row < s_nservices) pe_print(pe_ws_service_name(s_services[row]));
        return;
    case MODE_REQUISITION: {
        PeWsGoods g;
        if (!pe_ws_goods(row, &g)) return;
        if (g.owned) snprintf(price, sizeof price, "owned");
        else snprintf(price, sizeof price, "%u", (unsigned)g.price);
        pe_priced_row(g.name, g.id, price, g.blocked != NULL);
        return;
    }
    case MODE_REBUILD: {
        PeWsRebuild b;
        if (!pe_ws_rebuild(row, &b, NULL)) { if (row == 0) pe_print("Nothing dismantled yet"); return; }
        snprintf(price, sizeof price, "%u", (unsigned)b.price);
        pe_priced_row(NULL, b.id, price, b.blocked != NULL);
        return;
    }
    default: {
        const PeWardrobeApi *w = pe_ward();
        int state;
        if (!w || row < 0 || row >= w->count()) return;
        state = w->state(row);
        if (state == 2) snprintf(price, sizeof price, "worn");
        else if (state == 1) snprintf(price, sizeof price, "owned");
        else snprintf(price, sizeof price, "%u", w->price(row));
        if (state == 2) Draw_SetColor(0x408080);                 /* worn, the game's yellow */
        pe_priced_row(w->name(row), 0, price, state == 0 && w->price(row) > pe_shopw_junk());
        Draw_SetColor(0x808080);
        return;
    }
    }
}

/* ---- the Wardrobe's model, beside the list ---- */
static void pe_update_model(void) {
    const PeWardrobeApi *w = pe_ward();
    const int i = pe_cursor();
    s_drawn_tick = s_tick;
    if (pe_confirm_open()) { pe_stage_hide(&s_stage); s_shown = -1; return; }
    if (i != s_shown) {
        PeModelSource src;
        s_shown = i;
        s_preview = w && w->preview && i >= 0 && w->preview(i, &src) && pe_stage_show(&s_stage, &src, 0);
        if (!s_preview) pe_stage_show(&s_stage, NULL, 0);
    }
    pe_stage_pause(&s_stage, s_paused);
    pe_stage_draw(&s_stage);
}

static void pe_draw_list(MenuWidgetNode *node) {
    MenuWidget_DrawList(node, pe_row);
    if (s_mode == MODE_WARDROBE) pe_update_model();
}

static void pe_frame_row(int row) { if (!s_preview && row == 2) pe_print("No preview"); }
static void pe_draw_frame(MenuWidgetNode *node) { MenuWidget_DrawList(node, pe_frame_row); }

/* ---- the details panel ---- */

/* One stat as the equip screens show it: the game's label, the total (capped at 999 as they do)
 * and the "+N" bonus when there is one. */
static void pe_stat_row(const ItemDataRecord *rec, int stat) {
    const int x = g_TextCursorX, y = g_TextCursorY;
    int total = rec->baseStats[stat] + rec->bonusStats[stat];
    if (total > PE_STAT_MAX) total = PE_STAT_MAX;
    Draw_StatePush();
    Draw_AllocSprite((rec->kind == PE_ARMOR_KIND ? PE_LABEL_ARMOR : PE_LABEL_WEAPON) + stat);
    Draw_OffsetCursor(PE_STAT_VALUE_X, 0);
    Draw_PrintNumberWidth4(total);
    if (rec->bonusStats[stat]) {
        g_TextCursorX = x + PE_STAT_BONUS_X;
        g_TextCursorY = y;
        Draw_PrintSignedNumberWidth4(rec->bonusStats[stat]);
    }
    Draw_StatePop();
}

static void pe_info_row(int row) {
    char line[96];
    line[0] = 0;
    switch (s_mode) {
    case MODE_WORKSHOP: {
        const int c = pe_cursor();
        if (row == 0) snprintf(line, sizeof line, "Junk %u", (unsigned)pe_shopw_junk());
        else if (c >= 0 && c < s_nservices) snprintf(line, sizeof line, "%s", pe_ws_service_help(s_services[c]));
        break;
    }
    case MODE_REQUISITION: {
        PeWsGoods g;
        if (!pe_ws_goods(pe_cursor(), &g)) { if (row == 0) snprintf(line, sizeof line, "Nothing for sale now"); }
        else if (row == 0) pe_shop_details(g.id, line, sizeof line);
        else snprintf(line, sizeof line, "%s", g.owned ? "Already owned" : g.blocked ? g.blocked : "X: Buy");
        break;
    }
    case MODE_REBUILD: {
        PeWsRebuild b;
        ItemDataRecord rec;
        if (!pe_ws_rebuild(pe_cursor(), &b, &rec)) {
            if (row == 0) snprintf(line, sizeof line, "Dismantle gear to rebuild it here");
            break;
        }
        if (row < 3) {
            if (rec.iconId) pe_stat_row(&rec, row);              /* no stats from an older Catalog */
            return;
        }
        if (b.bought) snprintf(line, sizeof line, "Slots %u (+%u)   %s", (unsigned)rec.tailCount, b.bought,
                               b.blocked ? b.blocked : "X: Rebuild");
        else snprintf(line, sizeof line, "Slots %u   %s", (unsigned)rec.tailCount, b.blocked ? b.blocked : "X: Rebuild");
        break;
    }
    default: {
        const PeWardrobeApi *w = pe_ward();
        const int i = pe_cursor();
        int state;
        if (!w || i < 0 || i >= w->count()) break;
        state = w->state(i);
        if (row == 0) snprintf(line, sizeof line, "%s", w->name(i));
        else if (row == 1) {
            if (state == 2) snprintf(line, sizeof line, "Worn");
            else if (state == 1) snprintf(line, sizeof line, "Owned   X: Wear");
            else if (w->price(i) > pe_shopw_junk()) snprintf(line, sizeof line, "%u Junk   Not enough Junk", w->price(i));
            else snprintf(line, sizeof line, "%u Junk   X: Buy and wear", w->price(i));
        } else if (s_preview) snprintf(line, sizeof line, "Triangle: Full view");
        break;
    }
    }
    if (line[0]) pe_print(line);
}

static void pe_draw_info(MenuWidgetNode *node) { MenuWidget_DrawList(node, pe_info_row); }

/* ---- modes ---- */
static int pe_mode_rows(int mode) {
    const PeWardrobeApi *w = pe_ward();
    switch (mode) {
    case MODE_WORKSHOP: return s_nservices;
    case MODE_REQUISITION: return pe_shop_count();
    case MODE_REBUILD: return pe_ws_rebuild_count();
    default: return w ? w->count() : 0;
    }
}

static void pe_hide_model(void) {
    pe_stage_fullscreen(&s_stage, 0);
    pe_stage_hide(&s_stage);
    s_shown = -1;
}

static int pe_input(MenuWidgetNode *panel, unsigned int buttons);

/* Lay the screen out for `mode` (each has its own panels) with the cursor on `cursor`. The area
 * is Aya's inventory panel's, as pe_screen_open_game uses. */
static void pe_show_mode(int mode, int cursor) {
    const MenuWidgetSimpleDescriptor *d = MenuWidget_LookupSimpleDescriptor(1);
    const int x = d && d->x > 0 ? d->x : 28, y = d && d->y > 0 ? d->y : 24, width = d && d->width > 0 ? d->width : 252;
    int ix, iy, iw, ih, rows;
    pe_hide_model();
    if (s_screen.panel) pe_screen_close(&s_screen);
    s_info = s_frame = NULL;
    s_mode = mode;
    s_note[0] = 0;
    s_preview = s_paused = 0;
    if (mode == MODE_WORKSHOP) s_nservices = pe_ws_services(s_services);
    if (mode != MODE_WARDROBE) pe_screen_open(&s_screen, NULL, x, y, width, PE_WS_ROWS, pe_draw_list, pe_input);
    else {                                                    /* narrower, the model beside it */
        int bx, by, bw, bh;
        pe_screen_open(&s_screen, NULL, x, y, PE_WARD_LIST_W, PE_WS_ROWS, pe_draw_list, pe_input);
        s_screen.list->update = pe_screen_preview_input;      /* Left / Right turn the model */
        s_frame = pe_screen_panel(&s_screen, x + PE_WARD_LIST_W + PE_WS_GAP, y, width - PE_WARD_LIST_W - PE_WS_GAP,
                                  PE_WS_ROWS, pe_draw_frame);
        if (s_frame) {
            pe_screen_inside(s_frame, &bx, &by, &bw, &bh);
            if (bw > PE_PICTURE) bx += (bw - PE_PICTURE) / 2, bw = PE_PICTURE;
            if (bh > PE_PICTURE) by += (bh - PE_PICTURE) / 2, bh = PE_PICTURE;
            pe_stage_place(&s_stage, bx, by, bw, bh);
        }
    }
    pe_screen_inside(s_screen.list, &ix, &iy, &iw, &ih);
    s_info = pe_screen_panel(&s_screen, x, iy + ih + 3 * PE_WS_GAP, width, s_info_rows[mode], pe_draw_info);
    rows = pe_mode_rows(mode);
    if (cursor >= rows) cursor = rows > 0 ? rows - 1 : 0;
    pe_screen_rows(&s_screen, rows > 0 ? rows : 1, cursor);
}

static void pe_close(void) {
    pe_hide_model();
    pe_screen_close(&s_screen);
    s_info = s_frame = NULL;
    s_open = 0;
    pe_shop_at_counter(0);
    Inv_SetActiveList(PE_MENU_EXIT, 0);
    pe_log("workshop", "screen closed");
}

/* ---- buying: the game's Yes / No, cursor on No ---- */
static void pe_answer(int yes) {
    char said[96] = "";
    const int row = s_buy_row;
    int ok = 0;
    s_buy_row = -1;
    if (!yes) { Menu_PlayCancelSound(); return; }
    if (s_mode == MODE_REQUISITION) {
        PeWsGoods g;
        if (!pe_ws_goods(row, &g) || g.blocked) {                 /* changed while the question was up */
            Menu_PlayErrorSound();
            snprintf(s_note, sizeof s_note, "%s", g.blocked ? g.blocked : "Gone");
            return;
        }
        ok = pe_shop_buy(g.id, said, sizeof said);
    } else if (s_mode == MODE_REBUILD) {
        ok = pe_ws_rebuild_do(row, said, sizeof said);
    } else if (pe_ward()) {
        ok = pe_ward()->pick(row, said, sizeof said);
        s_shown = -1;                                            /* the state changed */
    }
    if (ok) Menu_PlayConfirmSound();                             /* every purchase, repeats too */
    else Menu_PlayErrorSound();
    {                                                            /* the board may change; the widgets stay */
        const int rows = pe_mode_rows(s_mode);
        pe_screen_rows(&s_screen, rows > 0 ? rows : 1, row < rows ? row : rows > 0 ? rows - 1 : 0);
    }
    snprintf(s_note, sizeof s_note, "%s", said[0] ? said : ok ? "Done" : "Couldn't do that");
}

static void pe_ask(int row, const char *question) {
    s_buy_row = row;
    pe_confirm(question, 0, pe_answer);
}

static void pe_refuse(const char *why) {
    Menu_PlayErrorSound();
    snprintf(s_note, sizeof s_note, "%s", why);
}

static void pe_cross(void) {
    char msg[96];
    const int row = pe_cursor();
    switch (s_mode) {
    case MODE_WORKSHOP: {
        const int service = row >= 0 && row < s_nservices ? s_services[row] : -1;
        Menu_PlayConfirmSound();
        s_cursor[MODE_WORKSHOP] = row;
        if (service == PE_WS_REQUISITION) pe_show_mode(MODE_REQUISITION, s_cursor[MODE_REQUISITION]);
        else if (service == PE_WS_REBUILD) pe_show_mode(MODE_REBUILD, s_cursor[MODE_REBUILD]);
        else if (service == PE_WS_WARDROBE) pe_show_mode(MODE_WARDROBE, s_cursor[MODE_WARDROBE]);
        else if (service >= 0) {                              /* not a screen yet: Wayne's box shows it */
            pe_wayne_menu_after_workshop(service);
            pe_close();
        }
        return;
    }
    case MODE_REQUISITION:
        if (pe_ws_cross(row, msg, sizeof msg) == PE_WS_REFUSE) pe_refuse(msg);
        else pe_ask(row, msg);
        return;
    case MODE_REBUILD:
        if (pe_ws_rebuild_cross(row, msg, sizeof msg) == PE_WS_REFUSE) pe_refuse(msg);
        else pe_ask(row, msg);
        return;
    default: {
        const PeWardrobeApi *w = pe_ward();
        const int state = w && row >= 0 && row < w->count() ? w->state(row) : 2;
        if (state == 2) { pe_refuse("Already wearing it"); return; }
        if (state == 1) { s_buy_row = row; pe_answer(1); return; }   /* owned: wear, no Junk, no question */
        if (w->price(row) > pe_shopw_junk()) { pe_refuse("Not enough Junk"); return; }
        snprintf(msg, sizeof msg, "Buy %s for %u Junk?", w->name(row), w->price(row));
        pe_ask(row, msg);
        return;
    }
    }
}

static int pe_input(MenuWidgetNode *panel, unsigned int buttons) {
    if (panel != s_screen.panel || pe_confirm_open()) return 0;
    if (s_stage.fullscreen && (buttons & (PE_MENU_CIRCLE | PE_MENU_CROSS | PE_TRIANGLE))) {
        pe_stage_fullscreen(&s_stage, 0);
        Menu_PlayCancelSound();
        return 1;
    }
    if (buttons & PE_MENU_CIRCLE) {
        Menu_PlayCancelSound();
        if (s_mode == MODE_WORKSHOP) { pe_close(); return 1; }
        s_cursor[s_mode] = pe_cursor();
        {
            static const int service[MODES] = { -1, PE_WS_REQUISITION, PE_WS_REBUILD, PE_WS_WARDROBE };
            int i, back = s_cursor[MODE_WORKSHOP];
            for (i = 0; i < s_nservices; ++i) if (s_services[i] == service[s_mode]) back = i;
            pe_show_mode(MODE_WORKSHOP, back);
        }
        return 1;
    }
    if (buttons & PE_MENU_CROSS) { pe_cross(); return 1; }
    if (s_mode == MODE_WARDROBE) {
        if (buttons & PE_TRIANGLE) {
            if (!s_preview) Menu_PlayErrorSound();
            else { pe_stage_fullscreen(&s_stage, 1); Menu_PlayConfirmSound(); }
            return 1;
        }
        if (buttons & (PE_LEFT | PE_RIGHT)) { pe_stage_turn(&s_stage, (buttons & PE_RIGHT) ? 96 : -96); return 1; }
        if ((buttons & PE_SQUARE) && !(buttons & PE_REPEAT)) { s_paused = !s_paused; Menu_PlayMoveSound(); return 1; }
    }
    return 0;
}

/* The title bar: what just happened, else where we are, your Junk and the controls. */
static int pe_help(void *ctx, void *user) {
    u8 **text = (u8 **)ctx;
    char line[96];
    (void)user;
    if (!s_open || !s_screen.list || MenuWidget_GetCurrentNode() != s_screen.list) return 0;
    if (s_note[0]) snprintf(line, sizeof line, "%s", s_note);
    else if (s_mode == MODE_WORKSHOP) snprintf(line, sizeof line, "Workshop   X: Open   O: Back to Wayne");
    else if (s_mode == MODE_REQUISITION) snprintf(line, sizeof line, "Requisition   Junk %u   X: Buy   O: Back", (unsigned)pe_shopw_junk());
    else if (s_mode == MODE_REBUILD) snprintf(line, sizeof line, "Rebuild   Junk %u   X: Rebuild   O: Back", (unsigned)pe_shopw_junk());
    else snprintf(line, sizeof line, "Wardrobe   Junk %u   X: Buy or wear   O: Back", (unsigned)pe_shopw_junk());
    *text = pe_text_from_ascii(s_text, sizeof s_text, line);
    return 0;
}

static int pe_on_frame(void *ctx, void *user) {
    (void)ctx;
    (void)user;
    ++s_tick;
    if (s_shown >= 0 && s_tick - s_drawn_tick > 2) { pe_stage_hide(&s_stage); s_shown = -1; }   /* not drawn: gone */
    return 0;
}

int pe_workshop_screen_open(void) {
    if (s_open) return 1;
    memset(s_cursor, 0, sizeof s_cursor);
    s_buy_row = -1;
    Render_BeginSceneLoad();
    g_GameStateBlock.flags |= PE_FIELD_PAUSED;
    g_GameState.flags |= PE_GAME_IN_MENU;
    g_MenuInputActive = 0;                                    /* wait for the Cross that opened us to be released */
    g_MenuInputHeldStatusMask = 0;
    pe_shop_at_counter(1);                                    /* buying here is at Wayne's counter */
    s_open = 1;
    pe_show_mode(MODE_WORKSHOP, 0);
    pe_log("workshop", "screen opened from Wayne's menu");
    return 1;
}

/* Open, or the game still in the menu it left (the frame the flags clear). */
int pe_workshop_screen_busy(void) { return s_open || (g_GameState.flags & PE_GAME_IN_MENU) != 0; }

void pe_workshop_screen_register(const char *mod_id) {
    pe_stage_init(&s_stage, 1, 0x101828u);                   /* Bestiary 2, Wardrobe browser 3, tools 0 */
    pe_hook_add(PE_HOOK_HELP_TEXT, pe_help, NULL, PE_PRIORITY_NORMAL, mod_id);
    pe_hook_add(PE_HOOK_FRAME, pe_on_frame, NULL, PE_PRIORITY_NORMAL, mod_id);
}
