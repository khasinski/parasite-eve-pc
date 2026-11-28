#ifndef PE_PRESENT_H
#define PE_PRESENT_H

/* Image presenter: the only host code that puts pictures into the game's frame. A mod asks for a
 * picture at a place on the game screen (320 x 224, game coordinates); the presenter shows it every
 * frame until hidden, in the best way available. Callers never touch VRAM or the framebuffer.
 *
 * Methods (backends), chosen per picture or automatically:
 *   BOX     one opaque rectangle pasted into the finished frame (PE_HOOK_FRAME_DRAWN): see-through
 *           pixels show the box colour.
 *   STRIPS  only the picture's covered pixels (alpha >= 128) pasted, row by row, at the same hook:
 *           what the game drew stays visible around it. Hard edges. Slow: each strip is one PsyZ
 *           upload (~50 us each; ~11 ms a frame for a 128 x 128 card at 2x), so not for real use
 *           until the engine offers a masked or batched upload.
 * Nothing is pasted while the display is in 24-bit mode (movies).
 * Both sit on top of everything the game drew that frame (cursor, pop-ups included). Nothing is
 * read back from the screen (PsyZ's read-back is stale at internal resolution > 1), and nothing is
 * left behind: a pasted picture exists only in the frame it was pasted into.
 * Later backends (a borrowed texture page, an engine image) are added here; callers don't change. */

#include "pe_image.h"

#define PE_PRESENT_MAP_SLOT 4
#define PE_PRESENT_SLOTS 5

typedef enum PePresentMode { PE_PRESENT_BOX, PE_PRESENT_STRIPS } PePresentMode;

/* Show `image` (copied; the caller keeps its own) with its top-left at (x, y) in game coordinates,
 * in slot `slot` (0 .. PE_PRESENT_SLOTS - 1), replacing what the slot showed. box_rgb (0xRRGGBB) is
 * the BOX background. 0 on success. */
int pe_present_show(int slot, int x, int y, const PeImage *image, PePresentMode mode, uint32_t box_rgb);
void pe_present_hide(int slot);

/* For debug rows and logs. */
typedef struct PePresentStats {
    int frame_x, frame_y, frame_w, frame_h;   /* the buffer pasted into last */
    int draw_slot;                            /* g_ActiveDrawSlot at the paste */
    unsigned pastes;                          /* LoadImage calls last frame */
    double avg_us;                            /* average cost of a frame's pastes, microseconds */
} PePresentStats;
void pe_present_stats(PePresentStats *out);

/* Host start-up (all mods on). */
void pe_present_install(void);

#endif
