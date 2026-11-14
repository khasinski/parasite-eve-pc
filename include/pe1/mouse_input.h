#ifndef PE1_MOUSE_INPUT_H
#define PE1_MOUSE_INPUT_H

/* Mouse support for the game's own screens (the menus, dialogue, battle).
 *
 * The mouse never takes the controller's place: it points at what a pad would
 * select and presses the game's own buttons for it, so every screen keeps its
 * rules. Over a menu list, moving the mouse moves the menu's cursor to the
 * cell under it; left click is the confirm button (it also turns dialogue
 * pages), right click the cancel button, the middle button the menu button
 * (Triangle) and the wheel steps Up/Down in a list or a dialogue choice. The
 * buttons pressed are the ones the game's controller set-up assigns. */

union SDL_Event;

enum MouseAction {
    MOUSE_MOVE,
    MOUSE_LEFT,
    MOUSE_RIGHT,
    MOUSE_MIDDLE,
    MOUSE_WHEEL_UP,
    MOUSE_WHEEL_DOWN,
};

/* An SDL event from the game window (called for every event). */
void Mouse_HandleEvent(const union SDL_Event *event);
/* Forgets queued clicks and moves (focus lost, pad reset). Clicks made while the game
 * was held still (the F1 or Esc panel, a load) are dropped by themselves. */
void Mouse_Reset(void);
/* A mouse event at a point of the game's screen, in game pixels (headless tests). */
void Mouse_TestEvent(int x, int y, enum MouseAction action);

/* The game's screen: w x h game pixels, shown at aspect (width / height). */
typedef struct MouseScreen {
    int w, h;
    float aspect;
} MouseScreen;

/* Window pixel (px, py) of a win_w x win_h window -> the point of the game's
 * screen shown there, as the presenter places it (the screen fitted with
 * bars, or widescreen's 16:9 view of it). Returns 0 for an empty window. */
int Mouse_WindowToScreen(float px, float py, int win_w, int win_h, const MouseScreen *screen,
                         int widescreen, int *x, int *y);

/* Where the mouse is on the game's screen, when it moved since *seen (pass
 * the same counter each time; it starts at 0). For screens that keep their
 * own cursor (the title menu). */
int Mouse_Moved(unsigned *seen, int *x, int *y);
/* The title's "press Start" screen and menu are up this frame: a left click
 * also presses Start. */
void Mouse_NoteTitleScreen(void);

/* Once per game frame, before the buttons are mapped: points menus and
 * dialogue choices at the mouse and returns the controller buttons (packet
 * bits, Cross = 0x2000) the mouse holds this frame. */
unsigned Mouse_FrameButtons(void);

/* The dialogue box draws a choice: `count` rows of 12 pixels from y, between
 * left and right, this frame. */
void Mouse_NoteDialogueChoice(int left, int right, int y, int count);

#endif /* PE1_MOUSE_INPUT_H */
