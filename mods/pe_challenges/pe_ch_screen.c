/* Challenges in Aya's own menu: a "Challenges" row (host extra rows, pe_main_menu.h) opening a
 * screen built from the game's menu widgets, laid out like the Bestiary's: a list in the game's
 * frame, an info panel below it, the title bar for help. Read-only: challenges are chosen in F1,
 * where the New Game only / locked-run / assisted rules live.
 *
 *   This run      the challenges this run has had, each with its level; one turned off or lowered
 *                 after the start reads dim. Info: Clean or Assisted (and Hardcore), battles won,
 *                 knockouts, time, what was lowered. The title bar: the highlighted challenge's
 *                 description, then the controls.
 *   Run history   every challenge run in the history (pe_ch_history.c), newest first: when, how it
 *                 ended (cleared in the game's yellow, fallen dim), how many challenges. Info: the
 *                 selected run's challenges and numbers.
 *   L1 / R1 switch pages, Circle or Cross goes back.
 *
 * The row shows with a game loaded once this run has had a challenge or the history has a run.
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
#include "pe1/menu_sound.h"
#include "pe1/menu_widget.h"

#define CH_LIST_ROWS   6           /* with the info panel's 4 rows, the screen's 224 lines */
#define CH_INFO_ROWS   4
#define CH_GAP         4
#define CH_MENU_L1     0x4u
#define CH_MENU_R1     0x8u
#define CH_COLOR_TEXT  0x808080    /* the game's menu white */
#define CH_COLOR_GOLD  0x408080    /* the game's yellow */
#define CH_HELP_TICKS  150         /* each title-bar page */
#define CH_ROW_ORDER   35          /* after Key Items 20, Bestiary 25, Wardrobe 30 */

enum { CH_PAGE_THIS_RUN, CH_PAGE_HISTORY, CH_PAGES };

static PeScreen s_screen;
static MenuWidgetNode *s_info;
static u8 s_text[96];
static int s_page, s_cursor[CH_PAGES], s_tick;
static int s_rows[CH_COUNT], s_row_count;   /* This run: challenge ids in F1 order */

/* ---- helpers ------------------------------------------------------------------------------ */

static void pe_print(const char *ascii) { Draw_PrintRawText(pe_text_from_ascii(s_text, sizeof s_text, ascii)); }

static void pe_time(char *out, size_t size, unsigned s) {
    snprintf(out, size, "%u:%02u:%02u", s / 3600, s / 60 % 60, s % 60);
}

static void pe_date(char *out, size_t size, int64_t at) {
    time_t t = (time_t)at;
    struct tm *tm = at > 0 ? localtime(&t) : NULL;
    if (tm) snprintf(out, size, "%04d-%02d-%02d", tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday);
    else snprintf(out, size, "----------");
}

/* A challenge with its level ("Tough crowd 2x HP"). */
static void pe_challenge_name(char *out, size_t size, const PeChallengeDef *d, int level) {
    if (d->levels && level > 0 && level < (int)d->level_count) snprintf(out, size, "%s %s", d->short_name, d->levels[level]);
    else snprintf(out, size, "%s", d->short_name);
}

static int pe_current_row(void) { return s_screen.list ? MenuWidget_GridCellIndex(s_screen.list) : 0; }

static void pe_build_this_run(void) {
    s_row_count = 0;
    for (unsigned i = 0; i < g_ch_def_count; ++i)
        if ((g_ch_run.ever | g_ch_run.active) & CH_BIT(g_ch_defs[i]->id)) s_rows[s_row_count++] = g_ch_defs[i]->id;
}

static int pe_page_rows(void) {
    return s_page == CH_PAGE_THIS_RUN ? s_row_count : pe_ch_history_count();
}

/* ---- This run ----------------------------------------------------------------------------- */

static void pe_this_run_row(int row) {
    const PeChallengeDef *d = row < s_row_count ? pe_ch_def(s_rows[row]) : NULL;
    char line[64];
    int level;
    if (!d) { Draw_SetTextDimmed(1); pe_print("No challenges this run"); Draw_SetTextDimmed(0); return; }
    level = pe_ch_on(d->id) ? pe_ch_level(d->id) : 0;
    pe_challenge_name(line, sizeof line, d, level ? level : 1);
    if (!pe_ch_on(d->id) || (g_ch_run.dropped & CH_BIT(d->id))) {
        Draw_SetTextDimmed(1);
        strncat(line, (g_ch_run.dropped & CH_BIT(d->id)) ? "  (lowered)" : "  (off)", sizeof line - strlen(line) - 1);
    } else if (d->flags & CH_DEF_LOCKS_RUN) {
        Draw_SetColor(CH_COLOR_GOLD);
    }
    pe_print(line);
    Draw_SetTextDimmed(0);
    Draw_SetColor(CH_COLOR_TEXT);
}

static void pe_this_run_info(int row) {
    char line[96], t[16], names[64];
    line[0] = 0;
    if (row == 0) snprintf(line, sizeof line, "Run: %s", pe_ch_status_text());
    else if (row == 1) snprintf(line, sizeof line, "Battles won %u   Knockouts %u", (unsigned)g_ch_run.battles, pe_ch_knockouts());
    else if (row == 2) { pe_time(t, sizeof t, g_ch_run.seconds); snprintf(line, sizeof line, "Time with challenges  %s", t); }
    else if (row == 3) {
        if (g_ch_run.dropped) { pe_ch_names(g_ch_run.dropped, names, sizeof names); snprintf(line, sizeof line, "Lowered: %s", names); }
        else if (g_ch_run.flags & CH_FLAG_CHEATS) snprintf(line, sizeof line, "A cheat or assist was used");
        else if (g_ch_run.ever) snprintf(line, sizeof line, "Nothing lowered, no cheats");
    }
    if (row == 0 && !pe_ch_assisted() && g_ch_run.ever) Draw_SetColor(CH_COLOR_GOLD);
    if (row == 3 && (g_ch_run.dropped || (g_ch_run.flags & CH_FLAG_CHEATS))) Draw_SetTextDimmed(1);
    pe_print(line);
    Draw_SetTextDimmed(0);
    Draw_SetColor(CH_COLOR_TEXT);
}

/* ---- Run history -------------------------------------------------------------------------- */

static const char *pe_state_name(uint32_t state) {
    return state == CH_RUN_CLEARED ? "Cleared" : state == CH_RUN_FALLEN ? "Fallen" : "Playing";
}

static int pe_count_bits(uint32_t v) { int n = 0; for (; v; v &= v - 1) ++n; return n; }

static void pe_history_row(int row) {
    const PeChRunEntry *e = pe_ch_history_newest(row);
    char line[80], date[16];
    int n;
    if (!e) { Draw_SetTextDimmed(1); pe_print("No challenge runs yet"); Draw_SetTextDimmed(0); return; }
    pe_date(date, sizeof date, e->at);
    n = pe_count_bits(e->ever);
    snprintf(line, sizeof line, "%s  %-7s  %d challenge%s%s", date, pe_state_name(e->state), n, n == 1 ? "" : "s",
             e->id == g_ch_run.run_id ? "  (this run)" : "");
    if (e->state == CH_RUN_CLEARED) Draw_SetColor(CH_COLOR_GOLD);
    else if (e->state == CH_RUN_FALLEN) Draw_SetTextDimmed(1);
    pe_print(line);
    Draw_SetTextDimmed(0);
    Draw_SetColor(CH_COLOR_TEXT);
}

/* The selected run's challenges, with levels, as far as a line holds. */
static void pe_entry_challenges(char *out, size_t size, const PeChRunEntry *e) {
    size_t used = 0;
    out[0] = 0;
    for (unsigned i = 0; i < g_ch_def_count && used + 1 < size; ++i) {
        const PeChallengeDef *d = g_ch_defs[i];
        char name[40];
        if (!(e->ever & CH_BIT(d->id))) continue;
        pe_challenge_name(name, sizeof name, d, pe_ch_entry_level(e, d->id));
        used += (size_t)snprintf(out + used, size - used, "%s%s", used ? ", " : "", name);
        if (used >= size) used = size - 1;
    }
    if (!out[0]) snprintf(out, size, "No challenges");
}

static void pe_history_info(int row) {
    const PeChRunEntry *e = pe_ch_history_newest(pe_current_row());
    char line[96], t[16];
    if (!e) return;
    line[0] = 0;
    if (row == 0) pe_entry_challenges(line, sizeof line, e);
    else if (row == 1) { pe_time(t, sizeof t, e->seconds); snprintf(line, sizeof line, "Battles %u   Knockouts %u   Time %s", (unsigned)e->battles, (unsigned)e->knockouts, t); }
    else if (row == 2) snprintf(line, sizeof line, "%s%s", (e->dropped || (e->flags & CH_FLAG_CHEATS)) ? "Assisted" : "Clean",
                                (e->ever & CH_BIT(CH_HARDCORE)) ? ", Hardcore" : "");
    else if (row == 3) { char date[16]; pe_date(date, sizeof date, e->at); snprintf(line, sizeof line, "%s %s", e->state == CH_RUN_PLAYING ? "Last saved" : pe_state_name(e->state), date); }
    if (row == 2 && !(e->dropped || (e->flags & CH_FLAG_CHEATS))) Draw_SetColor(CH_COLOR_GOLD);
    pe_print(line);
    Draw_SetColor(CH_COLOR_TEXT);
}

/* ---- widgets ------------------------------------------------------------------------------ */

static void pe_list_row(int row) {
    if (s_page == CH_PAGE_THIS_RUN) pe_this_run_row(row);
    else pe_history_row(row);
}

static void pe_info_row(int row) {
    if (s_page == CH_PAGE_THIS_RUN) pe_this_run_info(row);
    else pe_history_info(row);
}

static void pe_draw_list(MenuWidgetNode *node) { MenuWidget_DrawList(node, pe_list_row); }
static void pe_draw_info(MenuWidgetNode *node) { MenuWidget_DrawList(node, pe_info_row); }

static void pe_set_page(int page) {
    int rows;
    if (s_screen.list) s_cursor[s_page] = pe_current_row();
    s_page = (page % CH_PAGES + CH_PAGES) % CH_PAGES;
    if (s_page == CH_PAGE_THIS_RUN) pe_build_this_run();
    rows = pe_page_rows();
    if (s_cursor[s_page] >= rows) s_cursor[s_page] = 0;
    pe_screen_rows(&s_screen, rows ? rows : 1, s_cursor[s_page]);
    s_tick = 0;
}

static void pe_close(void) {
    s_cursor[s_page] = pe_current_row();
    pe_screen_close(&s_screen);
    s_info = NULL;
}

static int pe_input(MenuWidgetNode *panel, unsigned int buttons) {
    if (panel != s_screen.panel) return 0;
    if (buttons & (PE_MENU_CIRCLE | PE_MENU_CROSS)) { pe_close(); Menu_PlayCancelSound(); return 1; }
    if (buttons & (CH_MENU_L1 | CH_MENU_R1)) {
        pe_set_page(s_page + ((buttons & CH_MENU_R1) ? 1 : -1));
        Menu_PlayMoveSound();
        return 1;
    }
    return 0;
}

/* The title bar: the page, the highlighted challenge's description, then the controls. */
static int pe_on_help_text(void *ctx, void *user) {
    u8 **text = (u8 **)ctx;
    char line[96];
    (void)user;
    if (!s_screen.list || MenuWidget_GetCurrentNode() != s_screen.list) return 0;
    ++s_tick;
    if ((s_tick / CH_HELP_TICKS) % 2) {
        snprintf(line, sizeof line, "L1 R1: %s   O: Back", s_page == CH_PAGE_THIS_RUN ? "Run history" : "This run");
    } else if (s_page == CH_PAGE_THIS_RUN) {
        const int row = pe_current_row();
        const PeChallengeDef *d = row < s_row_count ? pe_ch_def(s_rows[row]) : NULL;
        snprintf(line, sizeof line, "%s", d ? d->description : "Challenges are chosen in F1 > Challenges");
    } else {
        snprintf(line, sizeof line, "Run history   %d run%s", pe_ch_history_count(), pe_ch_history_count() == 1 ? "" : "s");
    }
    *text = pe_text_from_ascii(s_text, sizeof s_text, line);
    return 0;
}

static void pe_open(MenuWidgetNode *main_list) {
    const MenuWidgetSimpleDescriptor *d = MenuWidget_LookupSimpleDescriptor(1);   /* the game's screen area */
    const int x = d && d->x > 0 ? d->x : 28, y = d && d->y > 0 ? d->y : 24, width = d && d->width > 0 ? d->width : 252;
    int ix, iy, iw, ih;
    pe_screen_open(&s_screen, main_list, x, y, width, CH_LIST_ROWS, pe_draw_list, pe_input);
    pe_screen_inside(s_screen.list, &ix, &iy, &iw, &ih);
    s_info = pe_screen_panel(&s_screen, x, iy + ih + 3 * CH_GAP, width, CH_INFO_ROWS, pe_draw_info);
    pe_set_page(s_page);
}

static int pe_row_shown(void) {
    return pe_game_loaded() && (g_ch_run.ever || pe_ch_history_count() > 0);
}

/* The row's icon: Revive's inventory icon (item 0x12), the game's own art for life and death. */
#define CH_ROW_ICON_ITEM 0x12
static const PeMainMenuRow s_row = { "Challenges", CH_ROW_ICON_ITEM, pe_row_shown, pe_open, NULL };

void pe_ch_screen_register(const char *mod_id) {
    pe_main_menu_add(&s_row, CH_ROW_ORDER);
    pe_hook_add(PE_HOOK_HELP_TEXT, pe_on_help_text, NULL, PE_PRIORITY_LATE, mod_id);
}
