#ifndef PE_MAIN_MENU_H
#define PE_MAIN_MENU_H

/* Aya's main menu (the column of icons) and screens built from the game's menu widgets.
 *
 * Extra rows: mods add rows after the game's last one. The host gives the list its rows
 * (PE_HOOK_LIST_ROWS), their icons (PE_HOOK_MAIN_MENU_ICON), their title-bar names
 * (PE_HOOK_HELP_TEXT) and opens the one confirmed (PE_HOOK_MAIN_MENU_ACTION); rows are shown in
 * `order`, lowest first, outside battle with a game loaded.
 *
 * Screens: a mod's screen is a panel node with its own input handler and a list node with its own
 * row drawer, the way the game's screens are. The helpers below build one in the options menu's
 * layout, hide Aya's status panels behind it as the game's own screens do, and keep scrolling
 * inside the list. */

#include "pe_hooks.h"

struct MenuWidgetNode;

typedef struct PeMainMenuRow {
    const char *title;    /* in the title bar while the row is highlighted */
    int icon_item;        /* item whose inventory icon the row shows; 0 = the game's spare icon */
    int (*shown)(void);   /* NULL = always */
    void (*open)(struct MenuWidgetNode *main_list);   /* confirmed */
    /* Optional, instead of icon_item: draw extra sprites at the icon's spot (the text cursor) and/or
     * set icon->sprite, which the game then draws there (-1: nothing more, the row drew its icon). */
    void (*icon)(PeHookMainMenuIcon *icon);
} PeMainMenuRow;

/* The main menu's own icons: action + PE_MAIN_MENU_SPRITE (0 Use Item .. 3 Equip Armor .. 8 BP). */
#define PE_MAIN_MENU_SPRITE 0x56
#define PE_ACTION_EQUIP_ARMOR 3
#define PE_ACTION_BONUS_POINTS 8
#define PE_ICON_SIZE 16                 /* an icon's spot, 16 x 16 from the text cursor */

/* For a row's icon(): a sprite drawn (dx, dy) from the icon's spot; and part of a sprite (the
 * texels at u, v, w x h inside it) drawn dw x dh at (dx, dy). Both dim as the game's icons do on
 * rows not highlighted, and leave the text cursor where it was. */
void pe_icon_sprite(int sprite, int dx, int dy);
void pe_icon_part(int sprite, int u, int v, int w, int h, int dx, int dy, int dw, int dh);

void pe_main_menu_add(const PeMainMenuRow *row, int order);   /* row must outlive the game */

/* A screen: panel at (x, y), `width` wide, list of `rows_visible` rows (longer lists scroll).
 * The list's draw and the panel's update are the caller's; the list is made current. */
#define PE_SCREEN_EXTRA 4
typedef struct PeScreen {
    struct MenuWidgetNode *panel, *list, *from;
    struct MenuWidgetNode *extra[PE_SCREEN_EXTRA];   /* pe_screen_panel's, closed with the screen */
    int extras;
} PeScreen;

void pe_screen_open(PeScreen *s, struct MenuWidgetNode *from, int x, int y, int width, int rows_visible,
                    void (*draw)(struct MenuWidgetNode *), int (*input)(struct MenuWidgetNode *, unsigned int));
/* The same, flush where the game's own screens sit (the inventory panel's layout, descriptor 1). */
void pe_screen_open_game(PeScreen *s, struct MenuWidgetNode *from, int rows_visible,
                         void (*draw)(struct MenuWidgetNode *), int (*input)(struct MenuWidgetNode *, unsigned int));
/* Opt-in list update for model browsers: Up/Down scroll normally; Left/Right
 * reach the parent panel instead of being consumed by the native grid. */
int pe_screen_preview_input(struct MenuWidgetNode *list, unsigned int buttons);
void pe_screen_close(PeScreen *s);   /* back to `from` (extra panels go with it) */
/* An extra panel on an open screen: a framed box at (x, y), `width` wide and `rows` rows tall, its
 * rows drawn by `draw` (NULL: an empty frame, e.g. for a picture the image presenter puts inside).
 * Not selectable; destroyed with the screen. Returns its list node (for pe_screen_inside), or NULL. */
struct MenuWidgetNode *pe_screen_panel(PeScreen *s, int x, int y, int width, int rows,
                                       void (*draw)(struct MenuWidgetNode *));
/* A list's inside on the game screen (320 x 224): where its rows draw. */
void pe_screen_inside(const struct MenuWidgetNode *list, int *x, int *y, int *w, int *h);
void pe_screen_rows(PeScreen *s, int rows, int cursor);   /* the list's row count and cursor */
int pe_screen_title_base(void);     /* a selected_base whose title bar always prints */
/* The game's Yes / No dialog (as its Discard asks) over the current screen: `question` on one line
 * (ASCII), the cursor on Yes or No; `answer` gets 1 for Yes, 0 for No or Circle, after the dialog
 * closes and the screen has its cursor back. pe_confirm_open: the dialog is up. */
void pe_confirm(const char *question, int default_yes, void (*answer)(int yes));
int pe_confirm_open(void);

void pe_main_menu_init(void);       /* the host, at start-up */

#define PE_MENU_CROSS  0x10000u
#define PE_MENU_CIRCLE 0x40u

#endif
