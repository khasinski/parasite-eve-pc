/* Wardrobe in Aya's menu: Bestiary-style owned outfit browser and an isolated model stage.
 * Cross only wears; this screen never purchases, unlocks or invokes a provider's gameplay loader.
 * Ownership, availability and deferred equipment changes remain in pe_wardrobe.c.
 */
#include <stdio.h>
#include <string.h>
#include "pe_collection.h"
#include "pe_gametext.h"
#include "pe_main_menu.h"
#include "pe_mod_host.h"
#include "pe_model_stage.h"
#include "pe1/draw_state.h"
#include "pe1/menu_draw_list.h"
#include "pe1/menu_sound.h"
#include "pe1/menu_widget.h"

#define PE_LIST_ROWS 6
#define PE_INFO_ROWS 3
#define PE_LIST_W 124
#define PE_GAP 4
#define PE_PICTURE 128
#define PE_GROUPS 258              /* one unique group per outfit, plus All outfits */
#define PE_MAX 257                 /* wardrobe registry's 256, plus story clothes */
#define PE_ICON_SHIFT_X 3
#define PE_ICON_SHIFT_Y 2
#define PE_L1 0x4u
#define PE_R1 0x8u
#define PE_LEFT 0x8000u
#define PE_RIGHT 0x2000u
#define PE_TRIANGLE 0x10u
#define PE_SQUARE 0x80u
#define PE_PRESS 0x20u
#define PE_REPEAT 0x20000u

static PeScreen s_screen;
static MenuWidgetNode *s_frame, *s_info;
static PeModelStage s_stage;
static u8 s_text[160];
static char s_note[96];
static const char *s_group[PE_GROUPS];
static int s_groups, s_category, s_member[PE_MAX], s_members, s_cursor[PE_GROUPS];
static int s_shown = -1, s_bank = -1, s_preview, s_paused;
static int s_tick, s_drawn_tick, s_note_tick = -1000;

/* Index zero is always the story outfit; all other indices come from the owned-only registry. */
static const PeCostume *pe_costume(int index) { return index > 0 ? pe_wardrobe_owned(index - 1) : NULL; }
static const char *pe_group_name(const PeCostume *c) { return c && c->group && *c->group ? c->group : "Aya's outfits"; }
static int pe_current(void) {
    int row = s_screen.list ? MenuWidget_GridCellIndex(s_screen.list) : -1;
    return row >= 0 && row < s_members ? s_member[row] : -1;
}

/* The native font varies in width. Fit every line, including names supplied by other mods. */
/* Six names of at most 16 glyphs plus 72 info glyphs stay below 10 KB of native
 * text packets, reserving over 6 KB of the 16 KB menu arena for the stock chrome. */
static void pe_print(const char *line, int width, unsigned cap) {
    size_t n;
    pe_text_from_ascii(s_text, cap + 1, line);
    n = strlen(line);
    if (n > cap) n = cap;
    if (n >= sizeof s_text) n = sizeof s_text - 1;
    if (strlen(line) > cap || Draw_MeasureTextWidth(s_text) > width) {
        while (n && (s_text[n] = 0xFF, Draw_MeasureTextWidth(s_text) > width)) --n;
        if (n >= 3) s_text[n - 3] = s_text[n - 2] = s_text[n - 1] = pe_glyph('.');
        while (n && Draw_MeasureTextWidth(s_text) > width) s_text[--n] = 0xFF;
    }
    Draw_PrintRawText(s_text);
}

static void pe_build_groups(void) {
    int i, g;
    s_groups = 1; s_group[0] = "All outfits";
    for (i = 0; i <= pe_wardrobe_owned_count() && i < PE_MAX; ++i) {
        const char *name = pe_group_name(pe_costume(i));
        for (g = 1; g < s_groups && strcmp(s_group[g], name); ++g) {}
        if (g == s_groups && s_groups < PE_GROUPS) s_group[s_groups++] = name;
    }
}

static void pe_build_members(void) {
    int i;
    s_members = 0;
    for (i = 0; i <= pe_wardrobe_owned_count() && i < PE_MAX; ++i)
        if (!s_category || !strcmp(pe_group_name(pe_costume(i)), s_group[s_category])) s_member[s_members++] = i;
}

static void pe_row(int row) {
    const PeCostume *c;
    if (row < 0 || row >= s_members) return;
    c = pe_costume(s_member[row]);
    if (pe_wardrobe_worn() == c) Draw_SetColor(0x408080);   /* worn outfit in the game's yellow */
    pe_print(c ? c->name : "Aya's own clothes", s_screen.list->draw_state - 4, 16);
    Draw_SetColor(0x808080);
}

static void pe_update_model(void) {
    int index = pe_current(), bank = pe_wardrobe_story_bank();
    const PeCostume *c = pe_costume(index);
    s_drawn_tick = s_tick;
    if (pe_confirm_open()) { pe_stage_hide(&s_stage); s_shown = -1; return; }
    if (index != s_shown || bank != s_bank) {
        PeModelSource src = {0};
        s_shown = index; s_bank = bank; s_preview = 0;
        src.kind = PE_MODEL_SKIN;
        src.map = (uint16_t)(c && c->bank >= 0 ? c->bank : bank);
        if (c && c->bank < 0) src.folder = c->bundle;
        /* A custom gameplay callback must never run while browsing. Its provider may not
         * have a standalone skin bundle, in which case keep the outfit wearable without a preview. */
        if (index >= 0 && (!c || c->bank >= 0 || (!c->load && c->bundle && *c->bundle)))
            s_preview = pe_stage_show(&s_stage, &src, 0);
        else pe_stage_show(&s_stage, NULL, 0);
    }
    pe_stage_pause(&s_stage, s_paused);
    pe_stage_draw(&s_stage);
}

static void pe_draw_list(MenuWidgetNode *node) { MenuWidget_DrawList(node, pe_row); pe_update_model(); }
static void pe_preview_row(int row) {
    if (!s_preview && row == 2) pe_print("No preview", s_frame->draw_state - 4, 16);
}
static void pe_draw_preview(MenuWidgetNode *node) { MenuWidget_DrawList(node, pe_preview_row); }

static void pe_info_row(int row) {
    int index = pe_current();
    const PeCostume *c = pe_costume(index);
    const char *line = "";
    if (index < 0) return;
    if (row == 0) line = c ? c->name : "Aya's own clothes";
    if (row == 1) line = pe_wardrobe_worn() == c ? "Worn" : "X: Wear this outfit";
    if (row == 2) line = s_preview ? "Triangle: Full view" : "Preview unavailable";
    static const unsigned caps[3] = {28, 24, 20};
    pe_print(line, s_info->draw_state - 4, caps[row]);
}
static void pe_draw_info(MenuWidgetNode *node) { MenuWidget_DrawList(node, pe_info_row); }

static void pe_set_group(int group) {
    s_cursor[s_category] = MenuWidget_GridCellIndex(s_screen.list);
    s_category = (group + s_groups) % s_groups;
    pe_build_members();
    pe_screen_rows(&s_screen, s_members ? s_members : 1, s_cursor[s_category]);
}

static void pe_close(void) {
    pe_stage_fullscreen(&s_stage, 0);
    pe_stage_hide(&s_stage);
    pe_screen_close(&s_screen);
    s_frame = s_info = NULL;
    s_shown = -1;
}

static int pe_input(MenuWidgetNode *panel, unsigned int buttons) {
    const int index = pe_current();
    const PeCostume *c = pe_costume(index);
    if (panel != s_screen.panel) return 0;
    if (s_stage.fullscreen && (buttons & (PE_MENU_CIRCLE | PE_MENU_CROSS | PE_TRIANGLE))) {
        pe_stage_fullscreen(&s_stage, 0); Menu_PlayCancelSound(); return 1;
    }
    if (buttons & PE_MENU_CIRCLE) { pe_close(); Menu_PlayCancelSound(); return 1; }
    if (buttons & PE_PRESS) return 1;
    if (buttons & PE_MENU_CROSS) {
        if (index < 0 || (index > 0 && !c) || pe_wardrobe_worn() == c) { Menu_PlayErrorSound(); return 1; }
        pe_wardrobe_wear_later(c);
        snprintf(s_note, sizeof s_note, "%s when you close the menu", c ? "Wearing it" : "Back to her own clothes");
        s_note_tick = s_tick;
        Menu_PlayConfirmSound(); return 1;
    }
    if (buttons & PE_TRIANGLE) {
        if (!s_preview || index != s_shown) { Menu_PlayErrorSound(); return 1; }
        pe_stage_fullscreen(&s_stage, 1); Menu_PlayConfirmSound(); return 1;
    }
    if (buttons & (PE_L1 | PE_R1)) {
        pe_set_group(s_category + ((buttons & PE_R1) ? 1 : -1)); Menu_PlayMoveSound(); return 1;
    }
    if (buttons & (PE_LEFT | PE_RIGHT)) { pe_stage_turn(&s_stage, (buttons & PE_RIGHT) ? 96 : -96); return 1; }
    if ((buttons & PE_SQUARE) && !(buttons & PE_REPEAT)) { s_paused = !s_paused; Menu_PlayMoveSound(); return 1; }
    return 0;
}

static int pe_on_help_text(void *ctx, void *user) {
    char line[160];
    size_t n;
    (void)user;
    if (!s_screen.list || MenuWidget_GetCurrentNode() != s_screen.list) return 0;
    if (s_tick - s_note_tick < 150) snprintf(line, sizeof line, "%s", s_note);
    else if ((s_tick / 150) % 3 == 1) snprintf(line, sizeof line, "L1 R1: Group   Left Right: Turn");
    else if ((s_tick / 150) % 3 == 2) snprintf(line, sizeof line, "X: Wear   Square: Pause   Circle: Back");
    else snprintf(line, sizeof line, "%s   %d outfits   X: Wear", s_group[s_category], s_members);
    pe_text_from_ascii(s_text, sizeof s_text, line);
    n = strlen(line);
    while (n && Draw_MeasureTextWidth(s_text) > 278) s_text[--n] = 0xFF;
    *(u8 **)ctx = s_text;
    return 0;
}

static int pe_on_frame(void *ctx, void *user) {
    (void)ctx; (void)user; ++s_tick;
    if (s_shown >= 0 && s_tick - s_drawn_tick > 2) { pe_stage_hide(&s_stage); s_shown = -1; }
    return 0;
}

static void pe_open(MenuWidgetNode *main_list) {
    const MenuWidgetSimpleDescriptor *d = MenuWidget_LookupSimpleDescriptor(1);
    const int x = d && d->x > 0 ? d->x : 28, y = d && d->y > 0 ? d->y : 24;
    const int width = d && d->width > 0 ? d->width : 252;
    int ix, iy, iw, ih, bx, by, bw, bh, row = 0, k;
    const PeCostume *worn = pe_wardrobe_worn();
    pe_build_groups(); s_category = 0; pe_build_members();
    for (k = 0; worn && k < pe_wardrobe_owned_count(); ++k) if (pe_wardrobe_owned(k) == worn) row = k + 1;
    s_note_tick = -1000; s_shown = -1; s_preview = s_paused = 0;
    pe_stage_fullscreen(&s_stage, 0);
    pe_screen_open(&s_screen, main_list, x, y, PE_LIST_W, PE_LIST_ROWS, pe_draw_list, pe_input);
    s_screen.list->update = pe_screen_preview_input;
    s_frame = pe_screen_panel(&s_screen, x + PE_LIST_W + PE_GAP, y, width - PE_LIST_W - PE_GAP, PE_LIST_ROWS, pe_draw_preview);
    pe_screen_inside(s_screen.list, &ix, &iy, &iw, &ih);
    s_info = pe_screen_panel(&s_screen, x, iy + ih + 3 * PE_GAP, width, PE_INFO_ROWS, pe_draw_info);
    if (!s_frame || !s_info) { pe_close(); return; }
    pe_screen_inside(s_frame, &bx, &by, &bw, &bh);
    if (bw > PE_PICTURE) bx += (bw - PE_PICTURE) / 2, bw = PE_PICTURE;
    if (bh > PE_PICTURE) by += (bh - PE_PICTURE) / 2, bh = PE_PICTURE;
    pe_stage_place(&s_stage, bx, by, bw, bh);
    pe_screen_rows(&s_screen, s_members, row);
}

static int pe_row_shown(void) { return pe_wardrobe_owned_count() > 0; }

/* The Equip Armor icon twice, the one behind up and right; the pair centred on the icon's spot. */
static void pe_icon(PeHookMainMenuIcon *icon) {
    const int armor = PE_MAIN_MENU_SPRITE + PE_ACTION_EQUIP_ARMOR;
    const int x = -(PE_ICON_SHIFT_X / 2), y = (PE_ICON_SHIFT_Y + 1) / 2;
    pe_icon_sprite(armor, x + PE_ICON_SHIFT_X, y - PE_ICON_SHIFT_Y);   /* the one behind */
    pe_icon_sprite(armor, x, y);
    icon->sprite = -1;
}

static const PeMainMenuRow s_row = { "Wardrobe", 0, pe_row_shown, pe_open, pe_icon };

void pe_wardrobe_menu_register(const char *mod_id) {
    pe_stage_init(&s_stage, 3, 0x101828u);  /* Bestiary owns slot 2; tools use 0 and 1 */
    pe_main_menu_add(&s_row, 30);
    pe_hook_add(PE_HOOK_HELP_TEXT, pe_on_help_text, NULL, PE_PRIORITY_LATE, mod_id);
    pe_hook_add(PE_HOOK_FRAME, pe_on_frame, NULL, PE_PRIORITY_NORMAL, mod_id);
}
