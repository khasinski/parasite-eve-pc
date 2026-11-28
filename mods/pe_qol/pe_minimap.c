#include <stdint.h>

#include "pe_game.h"
#include "pe_gametext.h"
#include "pe_main_menu.h"
#include "pe_mod_host.h"
#include "pe_mod_menu.h"
#include "pe_present.h"
#include "pe_save.h"
#include "pe1/draw_state.h"
#include "pe1/entity_frame_update.h"
#include "pe1/menu_draw_list.h"
#include "pe1/menu_sound.h"
#include "pe1/menu_widget.h"
#include "pe1/scene_dispatch.h"
#include "pe1/text.h"

enum { MAP_W = 92, MAP_H = 64, MAP_INSET = 6 };

typedef struct MapRoom {
    uint16_t id;
    uint8_t x, y;
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
    [R166] = {166, 8, 22}, [R167] = {167, 30, 22}, [R168] = {168, 51, 22},
    [R169] = {169, 16, 8}, [R170] = {170, 30, 8}, [R171] = {171, 44, 8},
    [R172] = {172, 30, 36}, [R173] = {173, 66, 22}, [R174] = {174, 83, 22},
    [R175] = {175, 83, 36}, [R176] = {176, 64, 45}, [R178] = {178, 64, 57},
    [R179] = {179, 83, 57}, [R180] = {180, 43, 45}, [R181] = {181, 24, 45}
};
static const MapLink s_sewer_links[] = {
    {R166, R167}, {R167, R168}, {R167, R169}, {R167, R170}, {R167, R171},
    {R167, R172}, {R168, R173}, {R173, R174}, {R174, R175}, {R175, R176},
    {R176, R178}, {R176, R179}, {R176, R180}, {R180, R181}
};
static uint8_t s_sewer_visits[(SEWER_ROOMS + 7) / 8];
static const MapArea s_areas[] = {{
    s_sewer_rooms, s_sewer_links, SEWER_ROOMS,
    sizeof s_sewer_links / sizeof s_sewer_links[0], s_sewer_visits, "Chinatown sewers"
}};
static const PeSaveRecord s_save = {
    .name = "pe_qol.map.chinatown_sewers", .version = 1,
    .data = s_sewer_visits, .size = sizeof s_sewer_visits
};

static int s_enabled = 1;
static const MapArea *s_shown_area;
static int s_shown_room;
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

static void pe_map_draw(const MapArea *area, unsigned active) {
    pe_map_rect(0, 0, MAP_W, MAP_H, 0x687480);
    pe_map_rect(1, 1, MAP_W - 2, MAP_H - 2, 0x141c26);
    for (unsigned i = 0; i < area->link_count; i++) {
        const MapLink *link = &area->links[i];
        if (!pe_map_visited(area, link->from) || !pe_map_visited(area, link->to)) continue;
        const MapRoom *a = &area->rooms[link->from], *b = &area->rooms[link->to];
        int x = a->x < b->x ? a->x : b->x;
        int y = a->y < b->y ? a->y : b->y;
        pe_map_rect(x, a->y, a->x + b->x - 2*x + 1, 1, 0x6d7a86);
        pe_map_rect(b->x, y, 1, a->y + b->y - 2*y + 1, 0x6d7a86);
    }
    for (unsigned i = 0; i < area->room_count; i++) {
        if (!pe_map_visited(area, i)) continue;
        const MapRoom *room = &area->rooms[i];
        pe_map_rect(room->x - 3, room->y - 3, 7, 7, i == active ? 0xffef9b : 0x8b99a4);
        pe_map_rect(room->x - 2, room->y - 2, 5, 5, i == active ? 0xd1aa4b : 0x354755);
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
    if (discovered) {
        area->visits[room / 8] |= (uint8_t)(1u << (room % 8));
        pe_log("minimap", "discovered room=%d", area->rooms[room].id);
    }
    if (!s_enabled || (g_FieldMoveLock & 1) || pe_mod_panel_open()) {
        pe_map_hide();
        return 0;
    }
    if (s_shown_area == area && s_shown_room == room && !discovered) return 0;

    pe_map_draw(area, (unsigned)room);
    const PeImage image = { MAP_W, MAP_H, s_pixels };
    if (pe_present_show(PE_PRESENT_MAP_SLOT, 320 - MAP_W - MAP_INSET, MAP_INSET,
                        &image, PE_PRESENT_BOX, 0x141c26) == 0) {
        s_shown_area = area;
        s_shown_room = room;
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
    pe_menu_bind_setting("PE Enhancements/Map/Minimap", &s_enabled, 0, 1);
    pe_system_menu_add(&row);
    pe_hook_add(PE_HOOK_FRAME_DRAWN, pe_map_frame, NULL, PE_PRIORITY_EARLY, mod_id);
    pe_hook_add(PE_HOOK_HELP_TEXT, pe_map_menu_help, NULL, PE_PRIORITY_LATE, mod_id);
    pe_hook_add(PE_HOOK_SAVE_READ, pe_map_reset_view, NULL, PE_PRIORITY_NORMAL, mod_id);
    pe_hook_add(PE_HOOK_NEW_GAME, pe_map_reset_view, NULL, PE_PRIORITY_NORMAL, mod_id);
}
