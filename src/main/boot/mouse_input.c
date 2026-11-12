/* Mouse support for the game's own screens: see pe1/mouse_input.h. */

#include <SDL3/SDL.h>
#include <math.h>
#include <psyz/video.h>
#include "common.h"
#include "pe1/psyq_gpu.h"
#include <libetc.h>
#include "pe1/menu_scroll_cursor.h"
#include "pe1/menu_sound.h"
#include "pe1/menu_widget.h"
#include "pe1/mouse_input.h"
#include "pe1/textbox_open.h"

extern s32 g_FieldRenderFlagTable[32];

#define SCREEN_W 320
#define CHOICE_ROW 12   /* a dialogue choice's row (textbox.c: 12 pixels per choice) */
#define QUEUE_SIZE 8
#define CLICK_HOLD 3    /* frames a click holds its button: menus confirm on release */
#define CLICK_GAP 2     /* frames released after it, so the next press is seen */
#define PAUSE_NS 250000000u /* frames further apart than this: the game was held still */

typedef struct MouseClick {
    int x, y;
    enum MouseAction action;
} MouseClick;

static struct {
    int x, y;             /* the last point on the game's screen */
    int known, moved;     /* a point was seen; it moved since the last frame */
    unsigned serial;      /* counts the moves, for Mouse_Moved */
    unsigned title;       /* the frame the title screen was last up */
    MouseClick queue[QUEUE_SIZE];
    unsigned head, count;
    unsigned held;        /* the buttons of the press under way */
    int hold, gap;        /* its frames left held, then released */
    unsigned frame;
    Uint64 frame_ns;      /* when the last game frame read the mouse */
    struct {
        int left, right, y, count;
        unsigned frame;
    } choice;             /* the dialogue choice drawn last */
    struct {
        MenuWidgetNode *node;
        int x, y;         /* the cell the d-pad is stepping towards */
        int confirm;      /* a click waits for the cursor to get there */
        int last;         /* the cell before the last step, to see a step that did nothing */
    } walk;
} s_mouse;

/* ---- where the mouse points ------------------------------------------------------------- */

/* The game's screen as the presenter shows it: its size in game pixels and
 * its picture's aspect (psyz GetCurrentGameAspectRatio), from the display
 * environment the game put last. */
static MouseScreen Mouse_CurrentScreen(void) {
    DISPENV env;
    MouseScreen screen;
    GetDispEnv(&env);
    const int pal = GetVideoMode() == MODE_PAL;
    const int lines = env.screen.h ? env.screen.h : 240;
    const int range = env.screen.w ? env.screen.w : 256;
    screen.w = env.disp.w > 0 ? env.disp.w : SCREEN_W;
    screen.h = lines * (env.disp.h > (pal ? 288 : 256) ? 2 : 1);
    if (Psyz_VideoGetAspectMode() == PSYZ_ASPECT_SQUARE && !Psyz_VideoGetWidescreen()) {
        screen.aspect = (float)screen.w / (float)screen.h;
    } else {
        screen.aspect = (4.0f / 3.0f) * ((float)range / 256.0f) / ((float)lines / (pal ? 288.0f : 240.0f));
    }
    return screen;
}

int Mouse_WindowToScreen(float px, float py, int win_w, int win_h, const MouseScreen *screen,
                         int widescreen, int *x, int *y) {
    if (win_w <= 0 || win_h <= 0 || screen->w <= 0 || screen->h <= 2 || screen->aspect <= 0.0f) {
        return 0;
    }
    /* As the presenter fits the picture (FitGameToWindow): the screen at its
     * aspect, or widescreen's 16:9 view, centred with bars. */
    const float aspect = widescreen ? 16.0f / 9.0f : screen->aspect;
    int rw, rh;
    if ((float)win_w / (float)win_h > aspect) {
        rh = win_h;
        rw = (int)((float)win_h * aspect + 0.5f);
    } else {
        rw = win_w;
        rh = (int)((float)win_w / aspect + 0.5f);
    }
    if (rw <= 0 || rh <= 0) {
        return 0;
    }
    const float rx = (float)((win_w - rw) / 2), ry = (float)((win_h - rh) / 2);
    const float w = (float)screen->w, h = (float)screen->h;
    float gx, gy;
    if (widescreen) {
        /* The 16:9 view (WidescreenViewHalf: 2 x 199 game pixels at 320 x 224)
         * around the screen's centre, where the screen's own layers (menus,
         * dialogue) are drawn; one buffer row is cut at the top and bottom. */
        const float view_w = 2.0f * floorf(fminf(w * (16.0f / 9.0f) / screen->aspect, w / 0.75f) * 0.5f);
        gx = w * 0.5f + (px - (rx + (float)rw * 0.5f)) * view_w / (float)rw;
        gy = 1.0f + (py - ry) * (h - 2.0f) / (float)rh;
    } else {
        gx = (px - rx) * w / (float)rw;
        gy = (py - ry) * h / (float)rh;
    }
    *x = (int)floorf(gx);
    *y = (int)floorf(gy);
    return 1;
}

static int Mouse_EventPoint(SDL_WindowID id, float wx, float wy, int *x, int *y) {
    SDL_Window *window = SDL_GetWindowFromID(id);
    int pw = 0, ph = 0;
    if (!window || !SDL_GetWindowSizeInPixels(window, &pw, &ph)) {
        return 0;
    }
    float density = SDL_GetWindowPixelDensity(window);
    if (density <= 0.0f) {
        density = 1.0f;
    }
    const MouseScreen screen = Mouse_CurrentScreen();
    return Mouse_WindowToScreen(wx * density, wy * density, pw, ph, &screen, Psyz_VideoGetWidescreen(), x, y);
}

/* ---- events ----------------------------------------------------------------------------- */

static void Mouse_Enqueue(int x, int y, enum MouseAction action) {
    if (s_mouse.count < QUEUE_SIZE) {
        s_mouse.queue[(s_mouse.head + s_mouse.count++) % QUEUE_SIZE] = (MouseClick){x, y, action};
    }
}

static void Mouse_At(int x, int y) {
    if (!s_mouse.known || x != s_mouse.x || y != s_mouse.y) {
        s_mouse.moved = 1;
        s_mouse.serial++;
    }
    s_mouse.x = x;
    s_mouse.y = y;
    s_mouse.known = 1;
}

void Mouse_Reset(void) {
    s_mouse.count = 0;
    s_mouse.head = 0;
    s_mouse.held = 0;
    s_mouse.hold = 0;
    s_mouse.gap = 0;
    s_mouse.moved = 0;
    s_mouse.walk.node = NULL;
}

void Mouse_HandleEvent(const SDL_Event *event) {
    int x, y;
    switch (event->type) {
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        Mouse_Reset();
        break;
    case SDL_EVENT_MOUSE_MOTION:
        if (Mouse_EventPoint(event->motion.windowID, event->motion.x, event->motion.y, &x, &y)) {
            Mouse_At(x, y);
        }
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (Mouse_EventPoint(event->button.windowID, event->button.x, event->button.y, &x, &y)) {
            Mouse_At(x, y);
            if (event->button.button == SDL_BUTTON_LEFT) {
                Mouse_Enqueue(x, y, MOUSE_LEFT);
            } else if (event->button.button == SDL_BUTTON_RIGHT) {
                Mouse_Enqueue(x, y, MOUSE_RIGHT);
            } else if (event->button.button == SDL_BUTTON_MIDDLE) {
                Mouse_Enqueue(x, y, MOUSE_MIDDLE);
            }
        }
        break;
    case SDL_EVENT_MOUSE_WHEEL: {
        float step = event->wheel.y;
        if (event->wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
            step = -step;
        }
        if (step != 0.0f) {
            Mouse_Enqueue(s_mouse.x, s_mouse.y, step > 0.0f ? MOUSE_WHEEL_UP : MOUSE_WHEEL_DOWN);
        }
        break;
    }
    default:
        break;
    }
}

void Mouse_TestEvent(int x, int y, enum MouseAction action) {
    s_mouse.frame_ns = SDL_GetTicksNS(); /* the run was held for the command, not by a dialog */
    Mouse_At(x, y);
    if (action != MOUSE_MOVE) {
        Mouse_Enqueue(x, y, action);
    }
}

int Mouse_Moved(unsigned *seen, int *x, int *y) {
    if (!s_mouse.known || *seen == s_mouse.serial) {
        return 0;
    }
    *seen = s_mouse.serial;
    *x = s_mouse.x;
    *y = s_mouse.y;
    return 1;
}

void Mouse_NoteTitleScreen(void) {
    s_mouse.title = s_mouse.frame;
}

void Mouse_NoteDialogueChoice(int left, int right, int y, int count) {
    s_mouse.choice.left = left;
    s_mouse.choice.right = right;
    s_mouse.choice.y = y;
    s_mouse.choice.count = count;
    s_mouse.choice.frame = s_mouse.frame;
}

/* ---- the game's buttons ----------------------------------------------------------------- */

/* The packet buttons the controller set-up gives game action `bit`. */
static unsigned Mouse_Buttons(int bit, unsigned fallback) {
    unsigned buttons = (unsigned)g_FieldRenderFlagTable[bit] & 0xFFFFu;
    return buttons ? buttons : fallback;
}

#define BUTTONS_CONFIRM() (Mouse_Buttons(29, 0x2000) | Mouse_Buttons(8, 0x2000)) /* menus, dialogue */
#define BUTTONS_CANCEL() Mouse_Buttons(30, 0x4000)
#define BUTTONS_MENU() (Mouse_Buttons(7, 0x1000) | Mouse_Buttons(28, 0x1000))
#define BUTTONS_START() Mouse_Buttons(2, 0x8)
#define BUTTONS_UP() Mouse_Buttons(3, 0x10)
#define BUTTONS_DOWN() Mouse_Buttons(5, 0x40)
#define BUTTONS_LEFT() Mouse_Buttons(6, 0x80)
#define BUTTONS_RIGHT() Mouse_Buttons(4, 0x20)

static unsigned Mouse_Press(unsigned buttons, int frames) {
    s_mouse.held = buttons;
    s_mouse.hold = frames - 1;  /* this frame, then hold more */
    s_mouse.gap = s_mouse.hold ? 0 : 1;
    return buttons;
}

/* ---- menu lists and dialogue choices ---------------------------------------------------- */

/* The focused list widget (a grid of cells), or NULL. */
static MenuWidgetNode *Mouse_FocusedList(void) {
    MenuWidgetNode *node = g_MenuWidgetCurrentNode;
    return node && node->mode == 2 ? node : NULL;
}

/* The cell of list `node` at (x, y): 1 a cell the cursor can take, -1 inside
 * the list where no cell is, 0 outside it. */
static int Mouse_ListCell(MenuWidgetNode *node, int x, int y, int *cx, int *cy) {
    const int w = node->draw_state, h = node->disabled;
    const int cols = node->grid_width, rows = node->visible_rows;
    int lx, ly;
    if (node->mode != 2 || w <= 0 || h <= 0 || cols <= 0 || rows <= 0 ||
        w * cols > 640 || h * rows > 480) {
        return 0;
    }
    MenuWidget_ScreenPosition(node, &lx, &ly);
    if (x < lx || y < ly || x >= lx + w * cols || y >= ly + h * rows) {
        return 0;
    }
    *cx = (x - lx) / w;
    *cy = (y - ly) / h + node->scroll_y;
    /* Menu_StepScrollCursor's rules: no cell past the list's end, none but the
     * first column on a trailing scroll row, and column 5 of a 7-wide grid is
     * stepped over. */
    if (*cx >= node->x_limit || *cy < 0 || *cy >= node->y_limit) {
        return -1;
    }
    if (node->has_scroll && *cy == node->y_limit - 1 && *cx != 0) {
        return -1;
    }
    if (node->x_limit > 6 && *cx == 5) {
        return -1;
    }
    return 1;
}

/* Points the menu or dialogue choice under (x, y) at it. Returns 1 when the
 * point is on a cell or choice, -1 inside a list but on no cell, else 0. A
 * list with the game's plain cursor handler takes the cell at once (as its
 * d-pad would, with the move sound); one with its own handler is walked there
 * with d-pad presses (Mouse_FrameButtons), so its handler sees each step. */
static int Mouse_Point(int x, int y, int click) {
    if (s_mouse.choice.count > 0 && s_mouse.frame - s_mouse.choice.frame <= 2) {
        const int top = s_mouse.choice.y - 1;
        if (x >= s_mouse.choice.left - 16 && x < s_mouse.choice.right && y >= top &&
            y < top + s_mouse.choice.count * CHOICE_ROW) {
            g_DialogueChoiceCursor = (u8)((y - top) / CHOICE_ROW);
            return 1;
        }
        return 0;
    }
    MenuWidgetNode *node = Mouse_FocusedList();
    if (!node) {
        return 0;
    }
    MenuWidgetNode *lists[3] = {node, node->linkedPrevious, node->linkedNext};
    for (int i = 0; i < 3; ++i) {
        MenuWidgetNode *list = lists[i];
        int cx, cy, hit;
        if (!list || (i && list == node)) {
            continue;
        }
        hit = Mouse_ListCell(list, x, y, &cx, &cy);
        if (hit < 0) {
            return -1;
        }
        if (!hit) {
            continue;
        }
        if (list->update != Menu_StepScrollCursor) {
            if (list != node) {
                return -1; /* its own handler: only its own cells are walked to */
            }
            if (cx != node->cursor_x || cy != node->cursor_y || click) {
                s_mouse.walk.node = node;
                s_mouse.walk.x = cx;
                s_mouse.walk.y = cy;
                s_mouse.walk.last = -1;
                s_mouse.walk.confirm = click;
            }
            return 1;
        }
        if (list != node) {
            node->cursor_x = -1;
            g_MenuWidgetCurrentNode = list;
        }
        if (list->cursor_x != cx || list->cursor_y != cy) {
            list->cursor_x = cx;
            list->cursor_y = cy;
            Menu_PlayMoveSound();
        }
        s_mouse.walk.node = NULL;
        return 1;
    }
    return 0;
}

/* One d-pad step of a walk to a cell, the confirm press once there, or 0
 * when the walk is over. */
static unsigned Mouse_Walk(void) {
    MenuWidgetNode *node = s_mouse.walk.node;
    if (!node || node != g_MenuWidgetCurrentNode) {
        s_mouse.walk.node = NULL;
        return 0;
    }
    const int cell = node->cursor_y * 64 + node->cursor_x;
    if (node->cursor_x == s_mouse.walk.x && node->cursor_y == s_mouse.walk.y) {
        s_mouse.walk.node = NULL;
        return s_mouse.walk.confirm ? Mouse_Press(BUTTONS_CONFIRM(), CLICK_HOLD) : 0;
    }
    if (cell == s_mouse.walk.last) {
        s_mouse.walk.node = NULL; /* the last step did nothing: the cell can't be reached */
        return 0;
    }
    s_mouse.walk.last = cell;
    unsigned step = s_mouse.walk.y < node->cursor_y ? BUTTONS_UP()
                    : s_mouse.walk.y > node->cursor_y ? BUTTONS_DOWN()
                    : s_mouse.walk.x < node->cursor_x ? BUTTONS_LEFT()
                                                      : BUTTONS_RIGHT();
    return Mouse_Press(step, 1);
}

static unsigned Mouse_ClickButtons(const MouseClick *click) {
    const int lists = Mouse_FocusedList() != NULL ||
                      (s_mouse.choice.count > 0 && s_mouse.frame - s_mouse.choice.frame <= 2);
    switch (click->action) {
    case MOUSE_LEFT: {
        int hit = Mouse_Point(click->x, click->y, 1);
        if (hit < 0) {
            return 0;
        }
        if (s_mouse.walk.node) {
            return Mouse_Walk(); /* confirms once the cursor is there */
        }
        if (s_mouse.frame - s_mouse.title <= 2) {
            /* the title: Start leaves "press Start", Cross picks the menu's row */
            return Mouse_Press(BUTTONS_CONFIRM() | BUTTONS_START(), CLICK_HOLD);
        }
        return Mouse_Press(BUTTONS_CONFIRM(), CLICK_HOLD);
    }
    case MOUSE_RIGHT:
        s_mouse.walk.node = NULL;
        return Mouse_Press(BUTTONS_CANCEL(), CLICK_HOLD);
    case MOUSE_MIDDLE:
        s_mouse.walk.node = NULL;
        return Mouse_Press(BUTTONS_MENU(), CLICK_HOLD);
    case MOUSE_WHEEL_UP:
    case MOUSE_WHEEL_DOWN:
        /* Only where Up/Down step a list or a choice: in the field they'd walk. */
        if (!lists) {
            return 0;
        }
        s_mouse.walk.node = NULL;
        return Mouse_Press(click->action == MOUSE_WHEEL_UP ? BUTTONS_UP() : BUTTONS_DOWN(), 1);
    default:
        return 0;
    }
}

unsigned Mouse_FrameButtons(void) {
    /* A game held still (the F1 or Esc panel open, a load) had the mouse
     * pointing at something else: drop what came meanwhile. */
    const Uint64 now = SDL_GetTicksNS();
    if (s_mouse.frame_ns && now - s_mouse.frame_ns > PAUSE_NS) {
        Mouse_Reset();
    }
    s_mouse.frame_ns = now;
    if (!++s_mouse.frame) {
        s_mouse.frame = 1; /* 0 stays "never" for the title and choice frames */
    }
    if (s_mouse.moved && s_mouse.known) {
        s_mouse.moved = 0;
        if (!s_mouse.hold) {
            Mouse_Point(s_mouse.x, s_mouse.y, 0);
        }
    }
    if (s_mouse.hold > 0) {
        if (!--s_mouse.hold) {
            s_mouse.gap = CLICK_GAP;
        }
        return s_mouse.held;
    }
    if (s_mouse.gap > 0) {
        s_mouse.gap--;
        return 0;
    }
    if (s_mouse.walk.node) {
        unsigned buttons = Mouse_Walk();
        if (buttons || s_mouse.walk.node) {
            return buttons;
        }
    }
    while (s_mouse.count) {
        MouseClick click = s_mouse.queue[s_mouse.head];
        s_mouse.head = (s_mouse.head + 1) % QUEUE_SIZE;
        s_mouse.count--;
        unsigned buttons = Mouse_ClickButtons(&click);
        if (buttons) {
            return buttons;
        }
    }
    return 0;
}
