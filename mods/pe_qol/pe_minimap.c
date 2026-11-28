#include <stdint.h>
#include <psyz/video.h>

#include "pe_game.h"
#include "pe_gametext.h"
#include "pe_main_menu.h"
#include "pe_mod_host.h"
#include "pe_mod_menu.h"
#include "pe_present.h"
#include "pe_save.h"
#include "pe1/draw_state.h"
#include "pe1/draw_buffers.h"
#include "pe1/display_env.h"
#include "pe1/entity_frame_update.h"
#include "pe1/game_timers.h"
#include "pe1/menu_draw_list.h"
#include "pe1/menu_sound.h"
#include "pe1/menu_widget.h"
#include "pe1/render_packets.h"
#include "pe1/scene_dispatch.h"
#include "pe1/text.h"

enum { MAP_W = 92, MAP_H = 64, MAP_INSET = 12, GRID_COLS = 11, GRID_ROWS = 5 };
enum { SEWER_GRID_ROW_VAR = 55, SEWER_GRID_COL_VAR = 56 };

typedef struct MapRoom {
    uint16_t id;
    uint8_t x, y;                   /* corridor anchor */
    uint8_t half_w, half_h;         /* room footprint around the anchor */
} MapRoom;

typedef struct MapLink {
    uint8_t from, to;
} MapLink;

typedef struct MapArea {
    const MapRoom *rooms;
    const MapLink *links;
    unsigned room_count, link_count;
    uint8_t *visits;
    const char *name;
} MapArea;

/* The array order is the save format. Keep the existing indices stable. */
enum {
    R166, R167, R168, R169, R170, R171, R172, R173,
    R174, R175, R176, R178, R179, R180, R181, SEWER_ROOMS
};
static const MapRoom s_sewer_rooms[SEWER_ROOMS] = {
    [R166] = {166, 61,  4, 3, 2},
    [R167] = { .id = 167 }, [R168] = { .id = 168 },
    [R169] = { .id = 169 }, [R170] = { .id = 170 },
    [R171] = { .id = 171 }, [R172] = { .id = 172 },
    [R173] = {173, 33, 39, 23, 2},
    [R174] = {174, 33, 50, 5, 4},
    [R175] = {175, 17, 50, 4, 3},
    [R176] = {176, 17, 58, 5, 3},
    [R178] = {178,  6, 58, 3, 3},
    [R179] = {179, 32, 58, 3, 3},
    [R180] = {180, 53, 58, 5, 3},
    [R181] = {181, 74, 58, 3, 3}
};
static const MapLink s_sewer_links[] = {
    {R173, R174}, {R174, R175}, {R175, R176},
    {R176, R178}, {R176, R179}, {R180, R181}
};
static uint8_t s_sewer_visits[(SEWER_ROOMS + 7) / 8];
static uint8_t s_grid_visits[(GRID_COLS * GRID_ROWS + 7) / 8];
static const MapArea s_areas[] = {{
    s_sewer_rooms, s_sewer_links, SEWER_ROOMS,
    sizeof s_sewer_links / sizeof s_sewer_links[0], s_sewer_visits, "Chinatown sewers"
}};
static const PeSaveRecord s_save = {
    .name = "pe_qol.map.chinatown_sewers", .version = 1,
    .data = s_sewer_visits, .size = sizeof s_sewer_visits
};
static const PeSaveRecord s_grid_save = {
    .name = "pe_qol.map.chinatown_tunnels", .version = 1,
    .data = s_grid_visits, .size = sizeof s_grid_visits
};

static int s_enabled = 1;
static const MapArea *s_shown_area;
static int s_shown_room;
static int s_shown_cell;
static int s_shown_x, s_shown_y;
static PeScreen s_screen;
static MenuWidgetNode *s_system_panel;
static uint8_t s_pixels[MAP_W * MAP_H * 4];
static u8 s_text[64];

static const MapArea *pe_map_find(int map, int *room) {
    for (unsigned i = 0; i < sizeof s_areas / sizeof s_areas[0]; i++) {
        const MapArea *area = &s_areas[i];
        for (unsigned j = 0; j < area->room_count; j++) {
            if (area->rooms[j].id == map) {
                *room = (int)j;
                return area;
            }
        }
    }
    return NULL;
}

static int pe_map_visited(const MapArea *area, unsigned room) {
    return (area->visits[room / 8] & (1u << (room % 8))) != 0;
}

static void pe_map_rect(int x, int y, int w, int h, uint32_t color) {
    for (int row = y; row < y + h; row++) {
        for (int col = x; col < x + w; col++) {
            uint8_t *pixel = &s_pixels[4 * (row * MAP_W + col)];
            pixel[0] = (uint8_t)(color >> 16);
            pixel[1] = (uint8_t)(color >> 8);
            pixel[2] = (uint8_t)color;
            pixel[3] = 255;
        }
    }
}

/* The same scene can serve multiple junctions. The native sewer scripts use
 * variable 55 for the row (south to north) and 56 for the column. */
static int pe_sewer_cell(unsigned room) {
    if (room < R167 || room > R172) return -1;
    const unsigned row = g_ScriptVariableBank[SEWER_GRID_ROW_VAR];
    const unsigned col = g_ScriptVariableBank[SEWER_GRID_COL_VAR];
    if (row < 1 || row > GRID_ROWS || col < 1 || col > GRID_COLS) return -1;
    if ((row & 1) && (col & 1)) return -1;
    return (int)((GRID_ROWS - row) * GRID_COLS + col - 1);
}

static int pe_grid_visited(int cell) {
    return (s_grid_visits[cell / 8] & (1u << (cell % 8))) != 0;
}

static void pe_sewer_draw_grid(const MapArea *area, int active) {
    const uint32_t corridor = 0x6d7a86;
    for (int row = 0; row < GRID_ROWS; row++) {
        for (int col = 0; col < GRID_COLS; col++) {
            const int cell = row * GRID_COLS + col;
            if (!pe_grid_visited(cell)) continue;
            const int x = 5 + 8 * col, y = 9 + 6 * row;
            if ((row & 1) && col + 1 < GRID_COLS && pe_grid_visited(cell + 1))
                pe_map_rect(x, y - 1, 9, 3, corridor);
            if ((col & 1) && row + 1 < GRID_ROWS && pe_grid_visited(cell + GRID_COLS))
                pe_map_rect(x - 1, y, 3, 7, corridor);
            if (row == 1 && (col == 5 || col == 7) && pe_map_visited(area, R166)) {
                const MapRoom *entry = &area->rooms[R166];
                const int left = x < entry->x ? x : entry->x;
                const int right = x > entry->x ? x : entry->x;
                pe_map_rect(x - 1, entry->y, 3, y - entry->y + 1, corridor);
                pe_map_rect(left, entry->y, right - left + 1, 1, corridor);
            }
            if (row == GRID_ROWS - 1 && pe_map_visited(area, R173))
                pe_map_rect(x - 1, y, 3, 7, corridor);
            pe_map_rect(x - 2, y - 2, 5, 5, cell == active ? 0xffef9b : 0x8b99a4);
            pe_map_rect(x - 1, y - 1, 3, 3, cell == active ? 0xd1aa4b : 0x354755);
        }
    }
}

static void pe_map_draw(const MapArea *area, unsigned active, int cell) {
    pe_map_rect(0, 0, MAP_W, MAP_H, 0x687480);
    pe_map_rect(1, 1, MAP_W - 2, MAP_H - 2, 0x141c26);
    pe_sewer_draw_grid(area, cell);
    if (pe_map_visited(area, R176) && pe_map_visited(area, R180))
        pe_map_rect(area->rooms[R176].x, 61,
                    area->rooms[R180].x - area->rooms[R176].x + 1, 2, 0x6d7a86);
    for (unsigned i = 0; i < area->link_count; i++) {
        const MapLink *link = &area->links[i];
        if (!pe_map_visited(area, link->from) || !pe_map_visited(area, link->to)) continue;
        const MapRoom *a = &area->rooms[link->from], *b = &area->rooms[link->to];
        int x = a->x < b->x ? a->x : b->x;
        int y = a->y < b->y ? a->y : b->y;
        pe_map_rect(x, a->y - 1, a->x + b->x - 2*x + 1, 3, 0x6d7a86);
        pe_map_rect(b->x - 1, y, 3, a->y + b->y - 2*y + 1, 0x6d7a86);
    }
    for (unsigned i = 0; i < area->room_count; i++) {
        if (!pe_map_visited(area, i)) continue;
        const MapRoom *room = &area->rooms[i];
        if (!room->half_w) continue;
        const int x = room->x - room->half_w, y = room->y - room->half_h;
        const int w = 2 * room->half_w + 1, h = 2 * room->half_h + 1;
        uint32_t outline = i == active ? 0xffef9b : 0x8b99a4;
        uint32_t fill = i == active ? 0xd1aa4b : 0x354755;
        if (i == R166) {
            outline = i == active ? 0xdbffe7 : 0x7ad5ad;
            fill = i == active ? 0x7de3aa : 0x286552;
        }
        pe_map_rect(x, y, w, h, outline);
        pe_map_rect(x + 1, y + 1, w - 2, h - 2, fill);
        if (i == R173) {
            pe_map_rect(59, y, 23, h, outline);
            pe_map_rect(60, y + 1, 21, h - 2, fill);
        }
    }
}

static void pe_map_hide(void) {
    if (!s_shown_area) return;
    pe_present_hide(PE_PRESENT_MAP_SLOT);
    s_shown_area = NULL;
}

static int pe_map_frame(void *ctx, void *user) {
    (void)ctx;
    (void)user;
    int room;
    const MapArea *area = NULL;
    if (pe_game_loaded() && pe_free_field() && g_SceneDispatchCur == g_SceneDispatchToken)
        area = pe_map_find(pe_room_number(), &room);
    if (!area) {
        pe_map_hide();
        return 0;
    }

    const int discovered = !pe_map_visited(area, (unsigned)room);
    const int cell = pe_sewer_cell((unsigned)room);
    const int discovered_cell = cell >= 0 && !pe_grid_visited(cell);
    if (discovered) {
        area->visits[room / 8] |= (uint8_t)(1u << (room % 8));
        pe_log("minimap", "discovered room=%d", area->rooms[room].id);
    }
    if (discovered_cell) {
        s_grid_visits[cell / 8] |= (uint8_t)(1u << (cell % 8));
        pe_log("minimap", "discovered tunnel row=%u column=%u",
               g_ScriptVariableBank[SEWER_GRID_ROW_VAR], g_ScriptVariableBank[SEWER_GRID_COL_VAR]);
    }
    if (!s_enabled || (g_FieldMoveLock & 1)) {
        pe_map_hide();
        return 0;
    }
    const int slot = g_ActiveDrawSlot & 1;
    const RECT *frame = &g_RenderDrawEnvArray[1 - slot].clip;
    const RECT *display = &g_RenderDispEnvArray[slot].disp;
    int right = display->x + display->w - frame->x;
    int top = display->y - frame->y;
    if (right <= 0 || right > frame->w) right = frame->w;
    if (top < 0 || top >= frame->h) top = 0;
    if (Psyz_VideoGetWidescreen()) {
        const int safe_right = Render_WidescreenHudRight(1 - slot);
        if (right > safe_right) right = safe_right;
    }
    const int x = right - MAP_W - MAP_INSET, y = top + MAP_INSET;
    if (s_shown_area == area && s_shown_room == room && s_shown_cell == cell &&
        s_shown_x == x && s_shown_y == y && !discovered && !discovered_cell) return 0;

    pe_map_draw(area, (unsigned)room, cell);
    const PeImage image = { MAP_W, MAP_H, s_pixels };
    if (pe_present_show(PE_PRESENT_MAP_SLOT, x, y,
                        &image, PE_PRESENT_BOX, 0x141c26) == 0) {
        s_shown_area = area;
        s_shown_room = room;
        s_shown_cell = cell;
        s_shown_x = x;
        s_shown_y = y;
    } else {
        pe_map_hide();
    }
    return 0;
}

static int pe_map_reset_view(void *ctx, void *user) {
    (void)ctx;
    (void)user;
    pe_map_hide();
    return 0;
}

static void pe_map_menu_row(int row) {
    (void)row;
    const char *label = s_enabled ? "Minimap  On" : "Minimap  Off";
    Draw_PrintRawText(pe_text_from_ascii(s_text, sizeof s_text, label));
}

static int pe_map_menu_help(void *ctx, void *user) {
    (void)user;
    if (!s_screen.list || MenuWidget_GetCurrentNode() != s_screen.list) return 0;
    int room;
    const MapArea *area = pe_map_find(pe_room_number(), &room);
    *(u8 **)ctx = pe_text_from_ascii(s_text, sizeof s_text, area ? area->name : "No map for this area");
    return 0;
}

static void pe_map_menu_draw(MenuWidgetNode *list) {
    MenuWidget_DrawList(list, pe_map_menu_row);
}

static int pe_map_menu_input(MenuWidgetNode *node, unsigned buttons) {
    if (node != s_screen.panel) return 0;
    if (buttons & PE_MENU_CIRCLE) {
        pe_screen_close(&s_screen);
        if (s_system_panel) s_system_panel->cursor_y = 0;
        s_system_panel = NULL;
        Menu_PlayCancelSound();
        return 1;
    }
    if (buttons & PE_MENU_CROSS) {
        if (MenuWidget_GridCellIndex(s_screen.list) == 0) {
            s_enabled = !s_enabled;
            pe_menu_save_settings();
            Menu_PlayConfirmSound();
        }
        return 1;
    }
    return 0;
}

static void pe_map_open(MenuWidgetNode *from) {
    pe_map_hide();
    s_system_panel = from->parent;
    if (s_system_panel && s_system_panel->mode == 1 && s_system_panel->cursor_y == 0)
        s_system_panel->cursor_y = 1;
    else
        s_system_panel = NULL;
    pe_screen_open_game(&s_screen, from, 1, pe_map_menu_draw, pe_map_menu_input);
    pe_screen_rows(&s_screen, 1, 0);
}

static int pe_map_menu_shown(void) {
    return pe_game_loaded() && !pe_in_battle();
}

void pe_minimap_shutdown(void) {
    pe_map_hide();
}

void pe_minimap_register(const char *mod_id) {
    static const PeSystemMenuRow row = { "Map", "Show or hide the minimap", pe_map_menu_shown, pe_map_open };
    if (pe_save_register(&s_save) != 0) return;
    if (pe_save_register(&s_grid_save) != 0) return;
    pe_menu_register_toggle("Map", "Minimap",
        "Show explored rooms and Aya's current room in the Chinatown sewers.", &s_enabled, NULL);
    pe_system_menu_add(&row);
    pe_hook_add(PE_HOOK_FRAME_DRAWN, pe_map_frame, NULL, PE_PRIORITY_EARLY, mod_id);
    pe_hook_add(PE_HOOK_HELP_TEXT, pe_map_menu_help, NULL, PE_PRIORITY_LATE, mod_id);
    pe_hook_add(PE_HOOK_SAVE_READ, pe_map_reset_view, NULL, PE_PRIORITY_NORMAL, mod_id);
    pe_hook_add(PE_HOOK_NEW_GAME, pe_map_reset_view, NULL, PE_PRIORITY_NORMAL, mod_id);
}
