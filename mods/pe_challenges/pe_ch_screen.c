/* Challenges in Aya's own menu: a "Challenges" row at the end of the game's System list (the SYS
 * icon, "Set User Preferences": Sound, Cursor, Window, Position), opening a screen built from the
 * game's menu widgets. No new icon in Aya's menu: challenges sit with the game's own settings.
 * (The code calls the System list the Config list.)
 *
 *   Active      (left)   the challenges on in this run, each with its level.
 *   Completed   (right)  every challenge cleared in any run (pe_ch_records.c); cleared without
 *                        assists in the game's yellow.
 *   Left / Right moves between the two lists; the list not in use is dimmed.
 *   Info panel           the highlighted challenge: on Active its level and what it does, on
 *                        Completed its clears, hardest level, fastest time and first clear.
 *   Title bar            this run (Clean or Assisted, battles won, knockouts, time), then the controls.
 *   Circle or Cross goes back to the Config list. Read-only: challenges are chosen in F1.
 *
 * The Config list is the game's (Menu_CreateMainMenuView): its 4 rows, then ours. The game's handler
 * treats rows 4 and 5 as a debug jump to bonus points, so Cross on our row never reaches it.
 */
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "pe_challenges.h"
#include "pe_game.h"
#include "pe_gametext.h"
#include "pe_main_menu.h"
#include "pe_mod_host.h"
#include "pe1/draw_state.h"
#include "pe1/menu_draw_list.h"
#include "pe1/menu_item_rows.h"
#include "pe1/menu_sound.h"
#include "pe1/menu_widget.h"
#include "pe1/text.h"

#define CH_HEAD_ROWS   1
#define CH_LIST_ROWS   4
#define CH_INFO_ROWS   3
#define CH_GAP         4
#define CH_MENU_LEFT   0x8000u
#define CH_MENU_RIGHT  0x2000u
#define CH_COLOR_TEXT  0x808080    /* the game's menu white */
#define CH_COLOR_GOLD  0x408080    /* the game's yellow */
#define CH_HELP_TICKS  150         /* each title-bar page */
#define CH_CONFIG_ROWS 4           /* the game's Config list */

enum { CH_ACTIVE, CH_DONE, CH_COLUMNS };

extern int g_DrawTextPosX;
int Menu_MainMenuInputHandler(MenuWidgetNode *node, u32 buttons);
void Menu_DrawMainMenuItem(int row);

static PeScreen s_screen;
static MenuWidgetNode *s_other, *s_head[CH_COLUMNS], *s_info, *s_config_panel;
static u8 s_text[128];
static int s_focus, s_cursor[CH_COLUMNS], s_scroll[CH_COLUMNS], s_tick, s_column_x[CH_COLUMNS];
static int s_rows[CH_COLUMNS][CH_COUNT], s_count[CH_COLUMNS];
static int s_drawing;   /* the column a list draw is for */

/* ---- text ---------------------------------------------------------------------------------- */

/* Print ASCII, cut with "..." to `width` pixels (the game's font is proportional). */
static void pe_print_fit(const char *ascii, int width) {
    size_t n = 0;
    pe_text_from_ascii(s_text, sizeof s_text, ascii);
    while (n < sizeof s_text - 1 && s_text[n] != 0xFF) ++n;
    if (width > 0 && Draw_MeasureTextWidth(s_text) > width) {
        while (n && (s_text[n] = 0xFF, Draw_MeasureTextWidth(s_text) > width)) --n;
        if (n >= 3) s_text[n - 3] = s_text[n - 2] = s_text[n - 1] = pe_glyph('.');
        while (n && Draw_MeasureTextWidth(s_text) > width) s_text[--n] = 0xFF;
    }
    Draw_PrintRawText(s_text);
}

/* A title-bar line, cut to the room left of the game's clock. */
#define CH_TITLE_W 236
static u8 *pe_title_fit(const char *ascii) {
    size_t n = 0;
    pe_text_from_ascii(s_text, sizeof s_text, ascii);
    while (n < sizeof s_text - 1 && s_text[n] != 0xFF) ++n;
    while (n && Draw_MeasureTextWidth(s_text) > CH_TITLE_W) s_text[--n] = 0xFF;
    return s_text;
}

static int pe_width(const char *ascii) {
    pe_text_from_ascii(s_text, sizeof s_text, ascii);
    return Draw_MeasureTextWidth(s_text);
}

/* `ascii` word-wrapped to `width` pixels: line 0 is the words that fit, line 1 the rest (the
 * printer cuts it with "..." if it is still too long). */
static void pe_wrapped(char *out, size_t size, const char *ascii, int width, int line) {
    char buf[160];
    size_t len = strlen(ascii), fit = 0, at = 0;
    while (at < len) {
        size_t next = at;
        while (next < len && ascii[next] != ' ') ++next;
        snprintf(buf, sizeof buf, "%.*s", (int)next, ascii);
        if (fit && pe_width(buf) > width) break;
        fit = next;
        at = next + 1;
    }
    if (line == 0) snprintf(out, size, "%.*s", (int)fit, ascii);
    else snprintf(out, size, "%s", fit < len ? ascii + fit + 1 : "");
}

static void pe_time(char *out, size_t size, unsigned s) {
    snprintf(out, size, "%u:%02u:%02u", s / 3600, s / 60 % 60, s % 60);
}

static void pe_date(char *out, size_t size, int64_t at) {
    time_t t = (time_t)at;
    struct tm *tm = at > 0 ? localtime(&t) : NULL;
    if (tm) snprintf(out, size, "%04d-%02d-%02d", tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday);
    else snprintf(out, size, "-");
}

static const char *pe_level_name(const PeChallengeDef *d, int level) {
    return d->levels && level > 0 && level < (int)d->level_count ? d->levels[level] : NULL;
}

/* ---- the two lists ------------------------------------------------------------------------- */

static void pe_build(void) {
    s_count[CH_ACTIVE] = s_count[CH_DONE] = 0;
    for (unsigned i = 0; i < g_ch_def_count; ++i) {
        const PeChallengeDef *d = g_ch_defs[i];
        const PeChRecord *r = pe_ch_record(d->id);
        if (pe_ch_on(d->id)) s_rows[CH_ACTIVE][s_count[CH_ACTIVE]++] = d->id;
        if (r && (r->clean || r->assisted)) s_rows[CH_DONE][s_count[CH_DONE]++] = d->id;
    }
}

static const PeChallengeDef *pe_at(int column, int row) {
    return row >= 0 && row < s_count[column] ? pe_ch_def(s_rows[column][row]) : NULL;
}

static int pe_current_row(void) { return s_screen.list ? MenuWidget_GridCellIndex(s_screen.list) : 0; }
static const PeChallengeDef *pe_current(void) { return pe_at(s_focus, pe_current_row()); }

static void pe_list_row(int row) {
    const PeChallengeDef *d = pe_at(s_drawing, row);
    const int width = g_DrawTextPosX - 4;
    if (s_drawing != s_focus) Draw_SetTextDimmed(1);   /* the list not in use */
    if (!d) {
        if (row == 0) { Draw_SetTextDimmed(1); pe_print_fit(s_drawing == CH_ACTIVE ? "None" : "None yet", width); }
    } else if (s_drawing == CH_DONE) {
        const PeChRecord *r = pe_ch_record(d->id);
        if (r && r->clean) Draw_SetColor(CH_COLOR_GOLD);
        pe_print_fit(d->short_name, width);
    } else {
        pe_print_fit(d->short_name, width);
    }
    Draw_SetTextDimmed(0);
    Draw_SetColor(CH_COLOR_TEXT);
}

static void pe_draw_focus(MenuWidgetNode *node) { s_drawing = s_focus; MenuWidget_DrawList(node, pe_list_row); }
static void pe_draw_other(MenuWidgetNode *node) { s_drawing = !s_focus; MenuWidget_DrawList(node, pe_list_row); }

/* ---- the headers: "Active 3", "Completed 5/13" --------------------------------------------- */

static void pe_head_row(int column, int row) {
    char line[48];
    if (row) return;
    if (column == CH_ACTIVE) snprintf(line, sizeof line, "Active  %d", s_count[CH_ACTIVE]);
    else snprintf(line, sizeof line, "Completed  %d/%u", s_count[CH_DONE], g_ch_def_count);
    if (column != s_focus) Draw_SetTextDimmed(1);
    pe_text_from_ascii(s_text, sizeof s_text, line);
    Draw_PrintCenteredTextInWidth(s_text, g_DrawTextPosX);
    Draw_SetTextDimmed(0);
}
static void pe_head_active(int row) { pe_head_row(CH_ACTIVE, row); }
static void pe_head_done(int row) { pe_head_row(CH_DONE, row); }
static void pe_draw_head_active(MenuWidgetNode *node) { MenuWidget_DrawList(node, pe_head_active); }
static void pe_draw_head_done(MenuWidgetNode *node) { MenuWidget_DrawList(node, pe_head_done); }

/* ---- the info panel ------------------------------------------------------------------------ */

static void pe_info_row(int row) {
    const PeChallengeDef *d = pe_current();
    const int width = g_DrawTextPosX - 4;
    char line[128], a[32], b[32];
    line[0] = 0;
    if (!d) {
        if (row == 0) snprintf(line, sizeof line, "%s", s_focus == CH_ACTIVE ? "No challenges are on in this run."
                                                                             : "No challenge completed yet.");
        if (row == 1) snprintf(line, sizeof line, "%s", s_focus == CH_ACTIVE ? "Choose them in F1: Challenges."
                                                                             : "Beat the game with one on.");
        Draw_SetTextDimmed(1);
    } else if (s_focus == CH_ACTIVE) {
        if (row == 0) {
            const char *level = pe_level_name(d, pe_ch_level(d->id));
            snprintf(line, sizeof line, level ? "%s: %s" : "%s", d->label, level);
            if (d->flags & CH_DEF_LOCKS_RUN) Draw_SetColor(CH_COLOR_GOLD);
        } else {
            pe_wrapped(line, sizeof line, d->description, width, row - 1);
        }
    } else {
        const PeChRecord *r = pe_ch_record(d->id);
        if (!r) return;
        if (row == 0) {
            snprintf(line, sizeof line, "%s%s", d->label, r->hardcore ? " - Hardcore" : "");
            if (r->clean) Draw_SetColor(CH_COLOR_GOLD);
        } else if (row == 1) {
            const char *best = pe_level_name(d, (int)r->best_level);
            snprintf(line, sizeof line, best ? "Clears %u  Clean %u  Best %s" : "Clears %u  Clean %u",
                     (unsigned)(r->clean + r->assisted), (unsigned)r->clean, best);
        } else {
            pe_time(a, sizeof a, r->fastest);
            pe_date(b, sizeof b, r->first);
            snprintf(line, sizeof line, "Fastest %s   First %s", r->fastest ? a : "-", b);
        }
    }
    pe_print_fit(line, width);
    Draw_SetTextDimmed(0);
    Draw_SetColor(CH_COLOR_TEXT);
}

static void pe_draw_info(MenuWidgetNode *node) { MenuWidget_DrawList(node, pe_info_row); }

/* ---- focus: the selectable list moves to the other column ---------------------------------- */

/* Both lists keep their full height (the game's frames don't shrink to a short list); the cursor
 * stays on real rows (pe_list_input). */
static int pe_rows_shown(int column) { return s_count[column] > CH_LIST_ROWS ? s_count[column] : CH_LIST_ROWS; }

static int pe_list_input(MenuWidgetNode *list, unsigned int buttons) {
    const int handled = pe_screen_preview_input(list, buttons);
    const int last = s_count[s_focus] ? s_count[s_focus] - 1 : 0;
    if (list->cursor_y > last) {
        list->cursor_y = last;
        if (list->scroll_y > last) list->scroll_y = last;
    }
    return handled;
}

static void pe_place(void) {
    MenuWidgetNode *other_panel = s_other ? s_other->parent : NULL;
    if (!s_screen.panel || !other_panel) return;
    s_screen.panel->x = s_column_x[s_focus];
    other_panel->x = s_column_x[!s_focus];
    pe_screen_rows(&s_screen, pe_rows_shown(s_focus), s_cursor[s_focus]);
    s_screen.list->scroll_y = s_scroll[s_focus] <= s_screen.list->cursor_y &&
                              s_screen.list->cursor_y < s_scroll[s_focus] + CH_LIST_ROWS ? s_scroll[s_focus]
                                                                                       : s_screen.list->scroll_y;
    s_other->cursor_x = -1;
    s_other->cursor_y = 0;
    s_other->scroll_y = s_scroll[!s_focus];
    s_other->scroll_adjust = 0;
    Draw_SetPrimCallback(s_other, pe_rows_shown(!s_focus));
}

static void pe_set_focus(int focus) {
    s_cursor[s_focus] = pe_current_row();
    s_scroll[s_focus] = s_screen.list->scroll_y;
    s_focus = focus;
    pe_place();
    s_tick = 0;
}

static void pe_close(void) {
    s_cursor[s_focus] = pe_current_row();
    s_scroll[s_focus] = s_screen.list ? s_screen.list->scroll_y : 0;
    pe_screen_close(&s_screen);
    if (s_config_panel) s_config_panel->cursor_y = 0;   /* the Config list again */
    s_config_panel = s_other = s_info = s_head[0] = s_head[1] = NULL;
}

static int pe_input(MenuWidgetNode *panel, unsigned int buttons) {
    if (panel != s_screen.panel) return 0;
    if (buttons & (PE_MENU_CIRCLE | PE_MENU_CROSS)) { pe_close(); Menu_PlayCancelSound(); return 1; }
    if (buttons & (CH_MENU_LEFT | CH_MENU_RIGHT)) {
        const int want = (buttons & CH_MENU_RIGHT) ? CH_DONE : CH_ACTIVE;
        if (want != s_focus) { pe_set_focus(want); Menu_PlayMoveSound(); }
        return 1;
    }
    return 0;
}

/* The title bar: this run, then the controls. */
static int pe_on_help_text(void *ctx, void *user) {
    u8 **text = (u8 **)ctx;
    char line[96], t[16];
    (void)user;
    if (!s_screen.list || MenuWidget_GetCurrentNode() != s_screen.list) return 0;
    ++s_tick;
    if ((s_tick / CH_HELP_TICKS) % 2) {
        snprintf(line, sizeof line, "Left Right: Switch list   O: Back");
    } else if (g_ch_run.ever) {
        pe_time(t, sizeof t, g_ch_run.seconds);
        snprintf(line, sizeof line, "%s   Won %u   KOs %u   %s", pe_ch_status_text(), (unsigned)g_ch_run.battles,
                 pe_ch_knockouts(), t);
    } else {
        snprintf(line, sizeof line, "Choose challenges in F1");
    }
    *text = pe_title_fit(line);
    return 0;
}

/* An extra framed box; no cursor box in it (only the list in use has one). */
static MenuWidgetNode *pe_box(int x, int y, int width, int rows, void (*draw)(MenuWidgetNode *)) {
    MenuWidgetNode *list = pe_screen_panel(&s_screen, x, y, width, rows, draw);
    if (list) list->cursor_x = -1;
    return list;
}

/* A framed one-row box over a column, for its name. */
static MenuWidgetNode *pe_head(int x, int y, int width, void (*draw)(MenuWidgetNode *)) {
    return pe_box(x, y, width, CH_HEAD_ROWS, draw);
}

static void pe_open(MenuWidgetNode *config_list) {
    const MenuWidgetSimpleDescriptor *d = MenuWidget_LookupSimpleDescriptor(1);   /* the game's screen area */
    const int x = d && d->x > 0 ? d->x : 28, y = d && d->y > 0 ? d->y : 24, width = d && d->width > 0 ? d->width : 252;
    const int column_w = (width - CH_GAP) / 2;
    int ix, iy, iw, ih, inset, row_h, list_y;
    pe_build();
    s_column_x[CH_ACTIVE] = x;
    s_column_x[CH_DONE] = x + column_w + CH_GAP;
    s_focus = s_count[CH_ACTIVE] || !s_count[CH_DONE] ? CH_ACTIVE : CH_DONE;
    /* The selectable list first (the screen's own panel); measured, then moved below the headers. */
    pe_screen_open(&s_screen, config_list, x, y, column_w, CH_LIST_ROWS, pe_draw_focus, pe_input);
    s_screen.list->update = pe_list_input;   /* Left / Right reach pe_input */
    pe_screen_inside(s_screen.list, &ix, &iy, &iw, &ih);
    inset = iy - y;
    row_h = ih / CH_LIST_ROWS;
    list_y = y + inset + CH_HEAD_ROWS * row_h + 3 * CH_GAP;
    s_screen.panel->y = list_y;
    s_head[CH_ACTIVE] = pe_head(s_column_x[CH_ACTIVE], y, column_w, pe_draw_head_active);
    s_head[CH_DONE] = pe_head(s_column_x[CH_DONE], y, column_w, pe_draw_head_done);
    s_other = pe_box(s_column_x[CH_DONE], list_y, column_w, CH_LIST_ROWS, pe_draw_other);
    s_info = pe_box(x, list_y + inset + ih + 3 * CH_GAP, width, CH_INFO_ROWS, pe_draw_info);
    if (!s_head[CH_ACTIVE] || !s_head[CH_DONE] || !s_other || !s_info) { pe_close(); return; }
    /* The Config list hides behind the screen, as the game's own screens hide what they cover (a
     * mode-1 panel with cursor_y set isn't drawn: MenuWidget_UpdateAndDraw). */
    s_config_panel = config_list->parent;
    if (s_config_panel && s_config_panel->mode == 1 && s_config_panel->cursor_y == 0) s_config_panel->cursor_y = 1;
    else s_config_panel = NULL;
    pe_place();
    pe_log("challenges", "screen: active %d, completed %d, list y %d, row %d", s_count[CH_ACTIVE], s_count[CH_DONE],
           list_y, row_h);
}

/* ---- the Config list's extra row ----------------------------------------------------------- */

static int pe_config_has_row(void) { return pe_game_loaded() && !pe_in_battle(); }

static void pe_config_item(int row) {
    if (row < CH_CONFIG_ROWS) { Menu_DrawMainMenuItem(row); return; }
    pe_text_from_ascii(s_text, sizeof s_text, "Challenges");
    Draw_PrintCenteredTextInWidth(s_text, g_DrawTextPosX);
}

/* The game prints no title-bar line for a Config row past its 4 (selected_base 0x20); on ours the
 * list borrows the host's base that always prints, and pe_on_config_help gives the line. */
static int s_config_base = -1;
static void pe_config_draw(MenuWidgetNode *node) {
    if (s_config_base >= 0)
        node->selected_base = MenuWidget_GridCellIndex(node) >= CH_CONFIG_ROWS ? pe_screen_title_base() : s_config_base;
    MenuWidget_DrawList(node, pe_config_item);
}
static int pe_is_config_list(const MenuWidgetNode *node) {
    return node && (node->draw == (void (*)())Menu_DrawMainMenuList || node->draw == (void (*)())pe_config_draw);
}

static int pe_config_input(MenuWidgetNode *panel, u32 buttons) {
    MenuWidgetNode *list = MenuWidget_GetChild(panel, 0);
    if ((buttons & PE_MENU_CROSS) && list && MenuWidget_GridCellIndex(list) >= CH_CONFIG_ROWS) {
        pe_open(list);
        Menu_PlayConfirmSound();
        return 1;
    }
    return Menu_MainMenuInputHandler(panel, buttons);
}

/* The Config list is laid out: one more row, drawn and confirmed by us. */
static int pe_on_list_rows(void *ctx, void *user) {
    PeHookListRows *r = ctx;
    (void)user;
    if (!r || !r->count || !pe_is_config_list(r->node) || *r->count != CH_CONFIG_ROWS || !pe_config_has_row()) return 0;
    if (!r->node->parent || r->node->parent->update != (int (*)())Menu_MainMenuInputHandler) return 0;
    r->node->draw = (void (*)())pe_config_draw;
    r->node->parent->update = (int (*)())pe_config_input;
    s_config_base = r->node->selected_base;
    *r->count = CH_CONFIG_ROWS + 1;
    /* The panel is as wide as the game's longest word; widen it (to the right) for ours. */
    {
        const int need = pe_width("Challenges") + 12, grow = need - r->node->draw_state;
        if (grow > 0) {
            r->node->draw_state += grow;
            r->node->parent->grid_width += grow;
        }
    }
    return 0;
}

/* The row's title-bar line. */
static int pe_on_config_help(void *ctx, void *user) {
    MenuWidgetNode *current = MenuWidget_GetCurrentNode();
    (void)user;
    if (!current || current->draw != (void (*)())pe_config_draw || MenuWidget_GridCellIndex(current) < CH_CONFIG_ROWS)
        return 0;
    *(u8 **)ctx = pe_text_from_ascii(s_text, sizeof s_text, "Active and completed challenges");
    return 0;
}

void pe_ch_screen_register(const char *mod_id) {
    pe_hook_add(PE_HOOK_LIST_ROWS, pe_on_list_rows, NULL, PE_PRIORITY_NORMAL, mod_id);
    pe_hook_add(PE_HOOK_HELP_TEXT, pe_on_config_help, NULL, PE_PRIORITY_LATE, mod_id);
    pe_hook_add(PE_HOOK_HELP_TEXT, pe_on_help_text, NULL, PE_PRIORITY_LATE, mod_id);
}
