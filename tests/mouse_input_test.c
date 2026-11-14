/* Mouse support for the game's screens (src/main/boot/mouse_input.c): where a window point
 * lands on the game's screen, and what the mouse does to a menu list, a menu with its own
 * cursor handler, a dialogue choice and the title, frame by frame. The game is stood in for:
 * its menu lists are MenuWidgetNodes placed by their x, y, and a list with its own handler moves
 * its cursor here as the game's would on a d-pad press. */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"
#include "pe1/menu_scroll_cursor.h"
#include "pe1/menu_widget.h"
#include "pe1/mouse_input.h"

/* ---- the game, stood in for ---- */
s32 g_FieldRenderFlagTable[32];
MenuWidgetNode *g_MenuWidgetCurrentNode;
u8 g_DialogueChoiceCursor;
static int s_move_sounds;

void MenuWidget_ScreenPosition(MenuWidgetNode *node, int *x, int *y) {
    *x = node->x;
    *y = node->y;
}
int Menu_StepScrollCursor(MenuWidgetNode *node, unsigned int buttons) {
    (void)node;
    (void)buttons;
    return 0;
}
static int Custom_Step(MenuWidgetNode *node, unsigned int buttons) {
    (void)node;
    (void)buttons;
    return 0;
}
void Menu_PlayMoveSound(void) { s_move_sounds++; }

static int s_failed;
#define CHECK(cond, ...)                                                   \
    do {                                                                   \
        if (!(cond)) {                                                     \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                    \
            printf(__VA_ARGS__);                                           \
            printf("\n");                                                  \
            s_failed = 1;                                                  \
        }                                                                  \
    } while (0)

/* The game's default controller set-up (Boot_BuildRenderFlagTable): action bit -> buttons. */
static void default_buttons(void) {
    static const struct { unsigned bit, buttons; } table[] = {
        {0, 0x4000}, {7, 0x1000}, {8, 0x2000}, {3, 0x10}, {5, 0x40}, {6, 0x80}, {4, 0x20},
        {1, 1}, {2, 8}, {9, 0x2000}, {10, 0x4000}, {13, 0x8000}, {24, 0x100}, {25, 0x200},
        {26, 0x400}, {27, 0x800}, {28, 0x1000}, {29, 0x2000}, {30, 0x4000}, {31, 0x8000},
    };
    memset(g_FieldRenderFlagTable, 0, sizeof g_FieldRenderFlagTable);
    for (unsigned i = 0; i < sizeof table / sizeof table[0]; ++i)
        g_FieldRenderFlagTable[table[i].bit] = (s32)table[i].buttons;
}

static MenuWidgetNode list_node(int x, int y, int w, int h, int cols, int rows, int limit) {
    MenuWidgetNode node;
    memset(&node, 0, sizeof node);
    node.mode = 2;
    node.x = x;
    node.y = y;
    node.draw_state = w;      /* cell width */
    node.disabled = h;        /* row height */
    node.grid_width = cols;
    node.x_limit = cols;
    node.visible_rows = rows;
    node.y_limit = limit;
    node.update = Menu_StepScrollCursor;
    return node;
}

/* The buttons the mouse holds over `frames` frames, one string of hex per frame. */
static void frames(int count, unsigned *out) {
    for (int i = 0; i < count; ++i) out[i] = Mouse_FrameButtons();
}

static void idle(void) {
    unsigned scratch[16];
    frames(16, scratch);
}

static void test_mapping(void) {
    const MouseScreen game = {320, 224, 4.0f / 3.0f * 240.0f / 224.0f};
    const MouseScreen title = {320, 240, 4.0f / 3.0f};
    int x, y;
    /* A window of the screen's own shape: corner to corner. */
    CHECK(Mouse_WindowToScreen(0, 0, 1280, 896, &game, 0, &x, &y) && x == 0 && y == 0, "corner %d,%d", x, y);
    Mouse_WindowToScreen(640, 448, 1280, 896, &game, 0, &x, &y);
    CHECK(x == 160 && y == 112, "centre %d,%d", x, y);
    Mouse_WindowToScreen(1279, 895, 1280, 896, &game, 0, &x, &y);
    CHECK(x == 319 && y == 223, "far corner %d,%d", x, y);
    /* 16:9 window, 4:3 picture: bars at the sides (1543 wide from x 188). */
    Mouse_WindowToScreen(188, 0, 1920, 1080, &game, 0, &x, &y);
    CHECK(x == 0 && y == 0, "pillarbox left %d,%d", x, y);
    Mouse_WindowToScreen(960, 540, 1920, 1080, &game, 0, &x, &y);
    CHECK(x == 160 && y == 112, "pillarbox centre %d,%d", x, y);
    Mouse_WindowToScreen(100, 540, 1920, 1080, &game, 0, &x, &y);
    CHECK(x < 0, "the bar is left of the screen: %d", x);
    /* A tall window: bars above and below. */
    Mouse_WindowToScreen(400, 500, 800, 1000, &game, 0, &x, &y);
    CHECK(x == 160 && y == 112, "letterbox centre %d,%d", x, y);
    Mouse_WindowToScreen(0, 220, 800, 1000, &game, 0, &x, &y);
    CHECK(x == 0 && y == 0, "letterbox top %d,%d (picture from y 220)", x, y);
    /* Widescreen: the 16:9 view shows 398 game pixels around the centre (-39 .. 359). */
    Mouse_WindowToScreen(960, 540, 1920, 1080, &game, 1, &x, &y);
    CHECK(x == 160 && y == 112, "widescreen centre %d,%d", x, y);
    Mouse_WindowToScreen(0, 0, 1920, 1080, &game, 1, &x, &y);
    CHECK(x == -39 && y == 1, "widescreen corner %d,%d", x, y);
    Mouse_WindowToScreen(1919, 1079, 1920, 1080, &game, 1, &x, &y);
    CHECK(x == 358 && y == 222, "widescreen far corner %d,%d", x, y);
    /* The title's 240-line screen. */
    Mouse_WindowToScreen(512, 678, 1024, 768, &title, 0, &x, &y);
    CHECK(x == 160 && y == 211, "title %d,%d", x, y);
    /* Hi-DPI is the caller's: pixels in, so a 2x window of the same shape maps the same. */
    Mouse_WindowToScreen(1280, 896, 2560, 1792, &game, 0, &x, &y);
    CHECK(x == 160 && y == 112, "2x %d,%d", x, y);
    CHECK(!Mouse_WindowToScreen(10, 10, 0, 0, &game, 0, &x, &y), "an empty window has no point");
}

static void test_list(void) {
    /* The Triangle menu's icon column (seen in the game: x 10, y 34, 16 x 16, five rows). */
    MenuWidgetNode menu = list_node(10, 34, 16, 16, 1, 5, 5);
    unsigned out[8];
    g_MenuWidgetCurrentNode = &menu;
    s_move_sounds = 0;
    Mouse_TestEvent(18, 74, MOUSE_MOVE);
    frames(1, out);
    CHECK(menu.cursor_y == 2 && menu.cursor_x == 0 && s_move_sounds == 1 && !out[0],
          "hover: cursor %d,%d sounds %d buttons %x", menu.cursor_x, menu.cursor_y, s_move_sounds, out[0]);
    Mouse_TestEvent(18, 76, MOUSE_MOVE);
    frames(1, out);
    CHECK(s_move_sounds == 1, "the same cell again: no sound");
    /* Click: the cell, then Cross held 3 frames (menus confirm on release), released 2. */
    Mouse_TestEvent(18, 42, MOUSE_LEFT);
    frames(6, out);
    CHECK(menu.cursor_y == 0, "click takes the cell: %d", menu.cursor_y);
    CHECK(out[0] == 0x2000 && out[1] == 0x2000 && out[2] == 0x2000 && !out[3] && !out[4],
          "confirm %x %x %x %x %x", out[0], out[1], out[2], out[3], out[4]);
    /* Two clicks in one frame: the second waits for the first to be released. */
    Mouse_TestEvent(18, 42, MOUSE_LEFT);
    Mouse_TestEvent(18, 42, MOUSE_RIGHT);
    frames(8, out);
    CHECK(out[0] == 0x2000 && out[2] == 0x2000 && !out[3] && !out[4] && out[5] == 0x4000,
          "queued %x %x %x %x %x %x", out[0], out[1], out[2], out[3], out[4], out[5]);
    idle();
    /* Middle: the menu button; wheel: Up/Down for one frame. */
    Mouse_TestEvent(18, 42, MOUSE_MIDDLE);
    frames(1, out);
    CHECK(out[0] == 0x1000, "middle %x", out[0]);
    idle();
    Mouse_TestEvent(18, 42, MOUSE_WHEEL_DOWN);
    frames(3, out);
    CHECK(out[0] == 0x40 && !out[1], "wheel down %x %x", out[0], out[1]);
    Mouse_TestEvent(18, 42, MOUSE_WHEEL_UP);
    frames(3, out);
    CHECK(out[0] == 0x10 || out[1] == 0x10, "wheel up %x %x", out[0], out[1]);
    idle();
    /* Inside the list but past its rows: no cell, and the click does nothing. */
    menu.y_limit = 3;
    menu.cursor_y = 1;
    Mouse_TestEvent(18, 34 + 16 * 4 + 4, MOUSE_LEFT);
    frames(4, out);
    CHECK(menu.cursor_y == 1 && !out[0] && !out[1], "past the end: cursor %d buttons %x", menu.cursor_y, out[0]);
    /* Outside every list: the click is the plain confirm button. */
    Mouse_TestEvent(200, 150, MOUSE_LEFT);
    frames(1, out);
    CHECK(out[0] == 0x2000 && menu.cursor_y == 1, "outside: %x, cursor %d", out[0], menu.cursor_y);
    idle();
    /* A scrolled list: the row shown first is scroll_y. */
    MenuWidgetNode items = list_node(30, 34, 124, 16, 2, 5, 20);
    items.scroll_y = 7;
    g_MenuWidgetCurrentNode = &items;
    Mouse_TestEvent(30 + 124 + 5, 34 + 16 * 3 + 1, MOUSE_MOVE);
    frames(1, out);
    CHECK(items.cursor_x == 1 && items.cursor_y == 10, "scrolled: %d,%d", items.cursor_x, items.cursor_y);
    /* A trailing scroll row takes only its first column; a 7-wide grid steps over column 5. */
    items.has_scroll = 1;
    items.scroll_y = 15;
    items.cursor_x = items.cursor_y = 0;
    Mouse_TestEvent(30 + 124 + 5, 34 + 16 * 4 + 1, MOUSE_MOVE);
    frames(1, out);
    CHECK(items.cursor_y == 0, "the scroll row's second column is no cell: %d,%d", items.cursor_x, items.cursor_y);
    MenuWidgetNode grid = list_node(72, 70, 16, 16, 11, 9, 9);
    g_MenuWidgetCurrentNode = &grid;
    Mouse_TestEvent(72 + 5 * 16 + 8, 70 + 8, MOUSE_MOVE);
    frames(1, out);
    CHECK(grid.cursor_x == 0, "column 5 of the name grid is no cell: %d", grid.cursor_x);
    Mouse_TestEvent(72 + 8 * 16 + 8, 70 + 5 * 16 + 8, MOUSE_MOVE);
    frames(1, out);
    CHECK(grid.cursor_x == 8 && grid.cursor_y == 5, "name grid: %d,%d", grid.cursor_x, grid.cursor_y);
    idle();
}

static void test_linked(void) {
    /* Two lists side by side (the equipment screen): pointing at the other one moves the focus. */
    MenuWidgetNode left = list_node(30, 34, 124, 16, 1, 5, 5);
    MenuWidgetNode right = list_node(180, 34, 124, 16, 1, 5, 5);
    unsigned out[2];
    left.linkedNext = &right;
    right.linkedPrevious = &left;
    right.cursor_x = -1;
    g_MenuWidgetCurrentNode = &left;
    Mouse_TestEvent(200, 34 + 16 * 2 + 3, MOUSE_MOVE);
    frames(1, out);
    CHECK(g_MenuWidgetCurrentNode == &right && left.cursor_x == -1 && right.cursor_x == 0 && right.cursor_y == 2,
          "linked: focus %s, left %d, right %d,%d", g_MenuWidgetCurrentNode == &right ? "right" : "left",
          left.cursor_x, right.cursor_x, right.cursor_y);
    idle();
}

/* A list with its own handler: the mouse walks it with the d-pad; the game moves its cursor. */
static void test_walk(void) {
    MenuWidgetNode menu = list_node(30, 34, 80, 20, 1, 3, 3);
    unsigned out, prev = 0, confirms = 0, steps = 0;
    menu.update = Custom_Step;
    g_MenuWidgetCurrentNode = &menu;
    Mouse_TestEvent(40, 34 + 20 * 2 + 5, MOUSE_LEFT);
    for (int i = 0; i < 20; ++i) {
        out = Mouse_FrameButtons();
        if ((out & 0x40) && !(prev & 0x40)) {   /* the game's handler: Down on its press */
            menu.cursor_y++;
            steps++;
        }
        if ((out & 0x2000) && !(prev & 0x2000)) confirms++;
        CHECK(!(out & 0x2000) || menu.cursor_y == 2, "confirm before the cursor got there (%d)", menu.cursor_y);
        prev = out;
    }
    CHECK(menu.cursor_y == 2 && steps == 2 && confirms == 1, "walk: cursor %d, %u steps, %u confirms",
          menu.cursor_y, steps, confirms);
    /* A step the handler refuses: the walk gives up, nothing is confirmed. */
    menu.cursor_y = 0;
    confirms = 0;
    prev = 0;
    Mouse_TestEvent(40, 34 + 20 * 2 + 5, MOUSE_LEFT);
    for (int i = 0; i < 20; ++i) {
        out = Mouse_FrameButtons();
        if ((out & 0x2000) && !(prev & 0x2000)) confirms++;
        prev = out;
    }
    CHECK(!confirms && menu.cursor_y == 0, "stuck walk: %u confirms", confirms);
    idle();
}

static void test_choice(void) {
    unsigned out[4];
    g_MenuWidgetCurrentNode = NULL;
    g_DialogueChoiceCursor = 0;
    /* Field: no list, no choice. The wheel would walk Aya, so it does nothing. */
    Mouse_TestEvent(100, 100, MOUSE_WHEEL_DOWN);
    frames(2, out);
    CHECK(!out[0] && !out[1], "wheel in the field %x", out[0]);
    /* A two-row choice drawn from y 150 (each frame the box draws it). */
    Mouse_NoteDialogueChoice(20, 300, 150, 2);
    Mouse_TestEvent(100, 150 + 12 + 4, MOUSE_MOVE);
    frames(1, out);
    CHECK(g_DialogueChoiceCursor == 1, "choice hover %d", g_DialogueChoiceCursor);
    Mouse_NoteDialogueChoice(20, 300, 150, 2);
    Mouse_TestEvent(100, 151, MOUSE_LEFT);
    frames(1, out);
    CHECK(g_DialogueChoiceCursor == 0 && out[0] == 0x2000, "choice click %d %x", g_DialogueChoiceCursor, out[0]);
    idle();
    /* A choice no longer drawn: pointing there changes nothing. */
    Mouse_TestEvent(100, 150 + 12 + 4, MOUSE_MOVE);
    frames(1, out);
    CHECK(g_DialogueChoiceCursor == 0, "stale choice %d", g_DialogueChoiceCursor);
}

static void test_title_and_pause(void) {
    unsigned out[2];
    g_MenuWidgetCurrentNode = NULL;
    Mouse_NoteTitleScreen();
    Mouse_TestEvent(160, 120, MOUSE_LEFT);
    frames(1, out);
    CHECK(out[0] == (0x2000 | 0x8), "title click: Cross and Start %x", out[0]);
    idle();
    Mouse_TestEvent(160, 120, MOUSE_LEFT);
    frames(1, out);
    CHECK(out[0] == 0x2000, "after the title: Cross alone %x", out[0]);
    idle();
    /* A click while the game is held still (a panel over it): dropped. */
    Mouse_TestEvent(160, 120, MOUSE_LEFT);
    SDL_Delay(400);
    frames(2, out);
    CHECK(!out[0] && !out[1], "click while paused %x", out[0]);
    /* The controller set-up swapped Cross and Circle: the click presses what confirms. */
    g_FieldRenderFlagTable[8] = g_FieldRenderFlagTable[29] = 0x4000;
    g_FieldRenderFlagTable[30] = 0x2000;
    Mouse_TestEvent(160, 120, MOUSE_LEFT);
    frames(1, out);
    CHECK(out[0] == 0x4000, "remapped confirm %x", out[0]);
    idle();
    Mouse_TestEvent(160, 120, MOUSE_RIGHT);
    frames(1, out);
    CHECK(out[0] == 0x2000, "remapped cancel %x", out[0]);
    idle();
    default_buttons();
}

int main(void) {
    default_buttons();
    Mouse_Reset();
    idle();
    test_mapping();
    test_list();
    test_linked();
    test_walk();
    test_choice();
    test_title_and_pause();
    if (s_failed) return 1;
    printf("mouse input: all checks passed\n");
    return 0;
}
