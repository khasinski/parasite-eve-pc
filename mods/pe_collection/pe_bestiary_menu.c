/*
 * The Bestiary in Aya's own menu (bridge findings pe-bestiary-viewer-plan-2026-10-26.md, phase 3):
 * a "Bestiary" row after the game's rows (host extra rows, pe_main_menu.h), shown once a creature is
 * met, opens a screen built from the game's own widgets:
 *
 *   left   the area's creatures (L1 / R1 change area); unmet ones are "???"
 *   right  a game panel whose inside is the creature's model (image presenter, pe_present.h),
 *          turning slowly with its idle animation; Left / Right turn it by hand
 *   below  what is known, by tier: basics, weaknesses, loot; a locked tier says how to unlock it
 *
 * Unmet creatures (decisions, plan section 9): in an area the player has reached, a black silhouette;
 * in areas not reached, and bosses, nothing (their shapes are reveals). Triangle shows the model full
 * screen (Triangle, Cross or Circle back), Square pauses the animation, Select shows the next form
 * (parts, phases), Circle goes back. The model on show is a host stage (pe_model_stage.h).
 *
 * Cross buys the next tier of a met creature with Junk, after the game's Yes / No (pe_confirm).
 * NEW: a creature with tiers not looked at yet is listed in the game's yellow; looking at it for a
 * moment clears that, and its new tiers stay yellow in the info panel while it is selected. The
 * title bar takes turns showing the area's count and the controls. F1 Debug/Collection has the
 * testing tools (a creature's tiers, kills, NEW marks, resets).
 */
#include <stdio.h>
#include <string.h>

#include "pe_collection.h"
#include "pe_gametext.h"
#include "pe_main_menu.h"
#include "pe_mod_host.h"
#include "pe_translate.h"
#include "pe_model_stage.h"
#include "pe1/draw_state.h"
#include "pe1/menu_draw_list.h"
#include "pe1/menu_sound.h"
#include "pe1/menu_widget.h"
#include "pe1/pad_state.h"

#define PE_MAX            512
#define PE_AREAS          32
#define PE_LIST_ROWS      6       /* with the info panel's 4 rows, the screen's 224 lines */
#define PE_INFO_ROWS      4
#define PE_LIST_W         124     /* the creature list; the model's frame takes the rest */
#define PE_GAP            4
#define PE_PICTURE        128     /* largest model picture in the box */
#define PE_SLOT           2       /* presenter slot (the viewer test uses 0 and 1) */
#define PE_BOX_RGB        0x101828u
#define PE_TURN_HAND      96      /* 4096 a turn */
#define PE_MENU_L1        0x4u
#define PE_MENU_R1        0x8u
#define PE_MENU_LEFT      0x8000u
#define PE_MENU_RIGHT     0x2000u
#define PE_MENU_TRIANGLE  0x10u
#define PE_MENU_SELECT    0x100u
#define PE_MENU_SQUARE    0x80u
#define PE_MENU_PRESS     0x20u           /* Cross down (PE_MENU_CROSS comes on its release) */
#define PE_MENU_REPEAT    0x20000u        /* a held button's repeat */
#define PE_COLOR_NEW      0x408080        /* the game's yellow (a better stat) */
#define PE_COLOR_TEXT     0x808080
#define PE_LOOK_TICKS     30              /* selected this long: looked at, no longer NEW */
#define PE_HELP_TICKS     150             /* each title-bar page */
#define PE_SAID_TICKS     150             /* a purchase's result stays in the title bar */

static PeScreen s_screen;
static MenuWidgetNode *s_frame, *s_info;
static u8 s_text[96];

/* areas: the sections creatures register under, in order */
static const char *s_area_name[PE_AREAS];
static int s_areas, s_area, s_member[PE_MAX], s_members;
static int s_cursor[PE_AREAS];                      /* remembered per area */

/* the model on show */
static PeModelStage s_stage;
static int s_shown = -1, s_shown_form = -1, s_shown_mode = -1, s_form, s_paused;
static int s_tick, s_drawn_tick;

/* NEW marks and buying */
static unsigned s_fresh;                            /* the selected creature's NEW tiers when reached */
static int s_look_tick, s_looked;
static int s_buying = -1;                           /* the creature the Yes / No is about */
static int s_said_tick = -1000;
static char s_said[96];

static const PeBestiaryApi *pe_b(void) { return &g_pe_bestiary_api; }

static int pe_area_of(const PeCreatureInfo *c) {
    int a;
    for (a = 0; a < s_areas; ++a) if (strcmp(s_area_name[a], c->section ? c->section : "") == 0) return a;
    return -1;
}

static void pe_build_areas(void) {
    const PeCreatureInfo *c;
    int h;
    s_areas = 0;
    for (h = 0; h < PE_MAX && (c = pe_b()->info(h)); ++h)
        if (pe_area_of(c) < 0 && s_areas < PE_AREAS) s_area_name[s_areas++] = c->section ? c->section : "";
}

static void pe_build_members(void) {
    const PeCreatureInfo *c;
    int h;
    s_members = 0;
    for (h = 0; h < PE_MAX && (c = pe_b()->info(h)); ++h)
        if (pe_area_of(c) == s_area) s_member[s_members++] = h;
}

static int pe_met(int h) { return (pe_b()->state(h) & PE_BESTIARY_MET) != 0; }

/* An area is reached once a creature in it, or (story areas) in a later story area, is met. */
static int pe_reached(int area) {
    const PeCreatureInfo *c, *first = NULL;
    int h;
    for (h = 0; h < PE_MAX && (c = pe_b()->info(h)); ++h) {
        if (pe_area_of(c) == area && !first) first = c;
        if (pe_met(h) && (pe_area_of(c) == area || (pe_area_of(c) > area && first && !c->ex && !first->ex))) return 1;
    }
    return 0;
}

/* 0 nothing shown, 1 silhouette, 2 the model */
static int pe_view_mode(int h) {
    const PeCreatureInfo *c = pe_b()->info(h);
    if (!c) return 0;
    if (pe_met(h)) return 2;
    return !c->boss && pe_reached(s_area) ? 1 : 0;
}

static int pe_current(void) {
    const int row = s_screen.list ? MenuWidget_GridCellIndex(s_screen.list) : 0;
    return row >= 0 && row < s_members ? s_member[row] : -1;
}

/* ---- the list ---- */
static void pe_row(int row) {
    const int h = row < s_members ? s_member[row] : -1;
    const PeCreatureInfo *c = h >= 0 ? pe_b()->info(h) : NULL;
    if (!c) return;
    if (!pe_met(h)) Draw_SetTextDimmed(1);
    else if (pe_bestiary_new(h)) Draw_SetColor(PE_COLOR_NEW);
    Draw_PrintRawText(pe_tr_ascii(s_text, sizeof s_text, pe_met(h) ? pe_l10n_ui(c->name) : "???"));
    Draw_SetTextDimmed(0);
    Draw_SetColor(PE_COLOR_TEXT);
}

/* The selection's NEW tiers are kept while it stays selected; after a moment it counts as looked at. */
static void pe_update_new(int h, int changed) {
    if (changed) {
        s_fresh = h >= 0 ? pe_bestiary_new(h) : 0;
        s_look_tick = s_tick;
        s_looked = 0;
    }
    if (h >= 0 && !s_looked && s_tick - s_look_tick >= PE_LOOK_TICKS) {
        s_looked = 1;
        if (pe_bestiary_new(h)) pe_bestiary_seen(h);
    }
}

/* ---- the model ---- */
static void pe_update_model(void) {
    const int h = pe_current(), mode = h >= 0 ? pe_view_mode(h) : 0;
    s_drawn_tick = s_tick;
    pe_update_new(h, h != s_shown);
    if (pe_confirm_open()) {                        /* the Yes / No sits where the model would paste over it */
        pe_stage_hide(&s_stage);
        s_shown_mode = -1;                          /* shown again once it closes */
        return;
    }
    if (h != s_shown || s_form != s_shown_form || mode != s_shown_mode) {
        PeModelSource src;
        if (h != s_shown) s_form = 0;
        s_shown = h;
        s_shown_form = s_form;
        s_shown_mode = mode;
        pe_stage_show(&s_stage, mode && pe_b()->model(h, s_form, &src) ? &src : NULL, mode == 1);
    }
    pe_stage_pause(&s_stage, s_paused);
    pe_stage_draw(&s_stage);
}

static void pe_draw_list(MenuWidgetNode *node) {
    MenuWidget_DrawList(node, pe_row);
    pe_update_model();                              /* once a menu frame */
}

/* ---- the info panel: what is known, by tier ---- */
static void pe_elements(char *out, size_t size, unsigned bits) {
    static const char *const names[5] = {"heat", "frost", "cyanide", "acid", "tranq"};
    int i, n = 0;
    out[0] = 0;
    for (i = 0; i < 5; ++i) {
        if (!(bits & (1u << i)) || (size_t)n >= size) continue;
        int added = snprintf(out+n,size-(size_t)n,"%s%s",n?", ":"",pe_l10n_ui(names[i]));
        if (added<0 || (size_t)added>=size-(size_t)n) break;
        n += added;
    }
    if (!n) snprintf(out, size, "%s", pe_l10n_ui("none"));
}

static void pe_info_row(int row) {
    const int h = pe_current();
    const PeCreatureInfo *c = h >= 0 ? pe_b()->info(h) : NULL;
    const unsigned state = h >= 0 ? pe_b()->state(h) : 0;
    char line[96], a[48], b[48];
    if (!c) return;
    line[0] = 0;
    if (!(state & PE_BESTIARY_MET)) {
        if (row == 0) snprintf(line, sizeof line, "???");
        if (row == 1) snprintf(line, sizeof line, "%s", c->boss ? pe_l10n_ui("Not met yet") : pe_l10n_ui("Not met yet: defeat one to learn about it"));
    } else if (row == 0) {
        snprintf(line, sizeof line, pe_l10n_ui("%s  HP %u  Def %u  Kills %u"), pe_l10n_ui(c->name), c->hp, c->defense, pe_b()->kills(h));
    } else if (row == 1) {
        if (state & PE_BESTIARY_WEAK) {
            pe_elements(a, sizeof a, c->weak);
            pe_elements(b, sizeof b, c->strong);
            snprintf(line, sizeof line, pe_l10n_ui("Weak: %s   Strong: %s"), a, b);
        } else {
            snprintf(line, sizeof line, pe_l10n_ui("Weaknesses: Scan, %s or %u Junk"), c->boss ? pe_l10n_ui("defeat") : pe_l10n_ui("5 kills"), c->price_weak);
        }
    } else if (row == 2) {
        if (state & PE_BESTIARY_LOOT) snprintf(line, sizeof line, pe_l10n_ui("Drop: %s"), c->drop ? pe_l10n_ui(c->drop) : "-");
        else if (c->boss) snprintf(line, sizeof line, pe_l10n_ui("Loot: %u Junk"), c->price_loot);
        else snprintf(line, sizeof line, pe_l10n_ui("Loot: 15 kills or %u Junk"), c->price_loot);
    } else if (row == 3 && (state & PE_BESTIARY_LOOT)) {
        snprintf(line, sizeof line, pe_l10n_ui("Steal: %s"), c->steal ? pe_l10n_ui(c->steal) : "-");
    }
    {
        const unsigned tier = row == 0 ? PE_BESTIARY_MET : row == 1 ? PE_BESTIARY_WEAK : PE_BESTIARY_LOOT;
        if (!(state & PE_BESTIARY_MET) || (state & tier)) Draw_SetTextDimmed(0);
        else Draw_SetTextDimmed(1);                 /* locked tiers read dimmer */
        if ((state & PE_BESTIARY_MET) && (s_fresh & tier)) Draw_SetColor(PE_COLOR_NEW);
    }
    Draw_PrintRawText(pe_tr_ascii(s_text, sizeof s_text, line));
    Draw_SetTextDimmed(0);
    Draw_SetColor(PE_COLOR_TEXT);
}

static void pe_draw_info(MenuWidgetNode *node) { MenuWidget_DrawList(node, pe_info_row); }

/* ---- input ---- */
static void pe_set_area(int area) {
    if (s_screen.list) s_cursor[s_area] = MenuWidget_GridCellIndex(s_screen.list);
    s_area = (area % s_areas + s_areas) % s_areas;
    pe_build_members();
    pe_screen_rows(&s_screen, s_members ? s_members : 1, s_cursor[s_area]);
}

static void pe_close(void) {
    pe_stage_hide(&s_stage);
    s_cursor[s_area] = MenuWidget_GridCellIndex(s_screen.list);
    pe_screen_close(&s_screen);
    s_frame = s_info = NULL;
    s_shown = -1;
}

static void pe_say(const char *text) {
    snprintf(s_said, sizeof s_said, "%s", text);
    s_said_tick = s_tick;
}

/* The Yes / No's answer: Yes spends the Junk. */
static void pe_buy_answer(int yes) {
    const int h = s_buying;
    unsigned bit;
    char msg[96];
    s_buying = -1;
    if (!yes || h < 0) return;
    (void)pe_bestiary_next_price(h, &bit);
    if (pe_bestiary_buy(h)) {
        if (h == pe_current()) s_fresh |= bit;
        snprintf(msg, sizeof msg, pe_l10n_ui("Learned its %s   Junk %u"), bit == PE_BESTIARY_WEAK ? pe_l10n_ui("weaknesses") : pe_l10n_ui("loot"), pe_col_junk());
        pe_say(msg);
    } else {
        pe_say(pe_col_last_said());
        Menu_PlayErrorSound();
    }
}

/* Cross: the next tier, if it can be bought now, after a Yes / No. */
static void pe_buy(int h) {
    char msg[96];
    unsigned bit = 0, price;
    if (h < 0 || !pe_met(h)) { pe_say("Meet it first"); Menu_PlayErrorSound(); return; }
    price = pe_bestiary_next_price(h, &bit);
    if (!bit) { pe_say("Everything about it is known"); Menu_PlayErrorSound(); return; }
    if (!pe_col_junk_ready(msg, sizeof msg)) { pe_say(msg); Menu_PlayErrorSound(); return; }
    if (pe_col_junk() < price) {
        snprintf(msg, sizeof msg, pe_l10n_ui("Need %u Junk (have %u)"), price, pe_col_junk());
        pe_say(msg);
        Menu_PlayErrorSound();
        return;
    }
    snprintf(msg, sizeof msg, pe_l10n_ui("Learn its %s for %u Junk?"), bit == PE_BESTIARY_WEAK ? pe_l10n_ui("weaknesses") : pe_l10n_ui("loot"), price);
    s_buying = h;
    pe_confirm(msg, 1, pe_buy_answer);
    Menu_PlayConfirmSound();
}

static int pe_input(MenuWidgetNode *panel, unsigned int buttons) {
    const int h = pe_current();
    if (panel != s_screen.panel) return 0;
    if (s_stage.fullscreen && (buttons & (PE_MENU_CIRCLE | PE_MENU_CROSS | PE_MENU_TRIANGLE))) {   /* back */
        pe_stage_fullscreen(&s_stage, 0);
        Menu_PlayCancelSound();
        return 1;
    }
    if (buttons & PE_MENU_CIRCLE) { pe_close(); Menu_PlayCancelSound(); return 1; }
    if (buttons & PE_MENU_PRESS) return 1;          /* Cross down: acted on at its release */
    if (buttons & PE_MENU_CROSS) { pe_buy(h); return 1; }
    if (buttons & PE_MENU_TRIANGLE) {               /* the model full screen */
        if (h < 0 || !pe_view_mode(h)) { Menu_PlayErrorSound(); return 1; }
        pe_stage_fullscreen(&s_stage, 1);
        Menu_PlayConfirmSound();
        return 1;
    }
    if (buttons & (PE_MENU_L1 | PE_MENU_R1)) {
        pe_set_area(s_area + ((buttons & PE_MENU_R1) ? 1 : -1));
        Menu_PlayMoveSound();
        return 1;
    }
    if (buttons & (PE_MENU_LEFT | PE_MENU_RIGHT)) {
        pe_stage_turn(&s_stage, (buttons & PE_MENU_RIGHT) ? PE_TURN_HAND : -PE_TURN_HAND);
        return 1;
    }
    if ((buttons & PE_MENU_SQUARE) && !(buttons & PE_MENU_REPEAT)) { s_paused = !s_paused; Menu_PlayMoveSound(); return 1; }
    if (buttons & PE_MENU_SELECT) {
        if (h >= 0 && pe_view_mode(h) && pe_b()->model(h, s_form + 1, NULL)) ++s_form;
        else if (s_form) s_form = 0;
        else { Menu_PlayErrorSound(); return 1; }
        Menu_PlayMoveSound();
        return 1;
    }
    if (buttons & ~PE_MENU_REPEAT) pe_log("bestiary", "screen buttons=0x%X (unhandled)", buttons);
    return 0;
}

/* ---- the title bar: a purchase just made or refused; else, in turn, the area's count and the controls ---- */
static int pe_on_help_text(void *ctx, void *user) {
    u8 **text = (u8 **)ctx;
    char line[96];
    int i, met = 0;
    (void)user;
    if (!s_screen.list || MenuWidget_GetCurrentNode() != s_screen.list) return 0;
    if (s_tick - s_said_tick < PE_SAID_TICKS) {
        snprintf(line, sizeof line, "%s", s_said);
    } else {
        switch ((s_tick / PE_HELP_TICKS) % 3) {
        case 0:
            for (i = 0; i < s_members; ++i) met += pe_met(s_member[i]);
            snprintf(line, sizeof line, pe_l10n_ui("%s   %d / %d   Junk %u"), s_areas ? pe_l10n_ui(s_area_name[s_area]) : "", met, s_members, pe_col_junk());
            break;
        case 1: snprintf(line, sizeof line, "%s", pe_l10n_ui("L1 R1: Area   Left Right: Turn")); break;
        default: snprintf(line, sizeof line, "%s", pe_l10n_ui("X: Buy   Triangle: View   Square: Pause")); break;
        }
    }
    *text = pe_tr_ascii(s_text, sizeof s_text, line);
    return 0;
}

/* The screen's model stays only while the screen draws (a battle or a closed menu stops it). */
static int pe_on_frame(void *ctx, void *user) {
    (void)ctx;
    (void)user;
    ++s_tick;
    if (s_shown >= 0 && s_tick - s_drawn_tick > 2) { pe_stage_hide(&s_stage); s_shown = -1; }
    return 0;
}

static void pe_open(MenuWidgetNode *main_list) {
    const MenuWidgetSimpleDescriptor *d = MenuWidget_LookupSimpleDescriptor(1);   /* the game's screen area */
    const int x = d && d->x > 0 ? d->x : 28, y = d && d->y > 0 ? d->y : 24, width = d && d->width > 0 ? d->width : 252;
    int ix, iy, iw, ih, bx, by, bw, bh;
    pe_build_areas();
    if (!s_areas) return;
    if (s_area >= s_areas) s_area = 0;
    pe_screen_open(&s_screen, main_list, x, y, PE_LIST_W, PE_LIST_ROWS, pe_draw_list, pe_input);
    s_frame = pe_screen_panel(&s_screen, x + PE_LIST_W + PE_GAP, y, width - PE_LIST_W - PE_GAP, PE_LIST_ROWS, NULL);
    pe_screen_inside(s_screen.list, &ix, &iy, &iw, &ih);
    s_info = pe_screen_panel(&s_screen, x, iy + ih + 3 * PE_GAP, width, PE_INFO_ROWS, pe_draw_info);
    pe_screen_inside(s_frame, &bx, &by, &bw, &bh);
    if (bw > PE_PICTURE) bx += (bw - PE_PICTURE) / 2, bw = PE_PICTURE;
    if (bh > PE_PICTURE) by += (bh - PE_PICTURE) / 2, bh = PE_PICTURE;
    pe_stage_place(&s_stage, bx, by, bw, bh);
    pe_log("bestiary", "screen: list %d,%d %dx%d  model box %d,%d %dx%d", ix, iy, iw, ih, bx, by, bw, bh);
    s_shown = -1;
    pe_build_members();
    pe_screen_rows(&s_screen, s_members ? s_members : 1, s_cursor[s_area]);
}

/* Shown once a creature is met. */
static int pe_row_shown(void) {
    int h;
    for (h = 0; h < PE_MAX && pe_b()->info(h); ++h) if (pe_met(h)) return 1;
    return 0;
}

/* The row's icon: the "B" of the menu's own BP icon (its texels at 2,1, 6 x 8), drawn 9 x 12 (one
 * and a half times) and centred in the icon's spot. */
#define PE_B_U 2
#define PE_B_V 1
#define PE_B_W 6
#define PE_B_H 8
#define PE_B_DRAW_W 9
#define PE_B_DRAW_H 12
static void pe_icon(PeHookMainMenuIcon *icon) {
    pe_icon_part(PE_MAIN_MENU_SPRITE + PE_ACTION_BONUS_POINTS, PE_B_U, PE_B_V, PE_B_W, PE_B_H,
                 (PE_ICON_SIZE - PE_B_DRAW_W) / 2, (PE_ICON_SIZE - PE_B_DRAW_H) / 2, PE_B_DRAW_W, PE_B_DRAW_H);
    icon->sprite = -1;
}

static const PeMainMenuRow s_row = { "Bestiary", 0, pe_row_shown, pe_open, pe_icon };

void pe_bestiary_menu_register(const char *mod_id) {
    pe_stage_init(&s_stage, PE_SLOT, PE_BOX_RGB);
    pe_main_menu_add(&s_row, 25);                   /* between Key Items and the Wardrobe */
    pe_hook_add(PE_HOOK_HELP_TEXT, pe_on_help_text, NULL, PE_PRIORITY_LATE, mod_id);
    pe_hook_add(PE_HOOK_FRAME, pe_on_frame, NULL, PE_PRIORITY_NORMAL, mod_id);
}
