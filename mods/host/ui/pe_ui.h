#ifndef PE_UI_H
#define PE_UI_H

/* The mod host's on-screen UI, in three parts:
 *   pe_ui_raster.c  CPU drawing into an RGBA surface: rounded boxes, text (IBM Plex Sans via
 *                   stb_truetype, as the game's own settings panel)
 *   pe_ui_gpu.c     one textured quad over the game, through the PsyZ overlay callbacks
 *                   (chained, so the game's settings panel keeps working)
 *   users           pe_mod_panel.c (the F1 Mods panel), pe_toast.c (one-line messages)
 * Only one surface is on screen at a time: the panel is modal, a toast waits for it. */

#include <stdint.h>

typedef struct PeColor { float r, g, b, a; } PeColor;
typedef struct PeSurface { uint8_t *pixels; int w, h; } PeSurface; /* h includes the solid row */
typedef enum PeAlign { PE_ALIGN_LEFT, PE_ALIGN_CENTRE, PE_ALIGN_RIGHT } PeAlign;

/* Palette, shared by everything the host draws. */
extern const PeColor pe_ui_panel, pe_ui_border, pe_ui_accent, pe_ui_text, pe_ui_muted;
PeColor pe_ui_alpha(PeColor c, float a);

/* ---- raster ---- */
int pe_ui_font_ready(void);
/* Size the surface for a w x h image plus the one solid texel row the quad's dimming uses;
 * clears it. 0 if out of memory. */
int pe_ui_surface_begin(PeSurface *s, int w, int h);
void pe_ui_surface_finish(PeSurface *s);            /* sets the solid texel */
void pe_ui_surface_free(PeSurface *s);
void pe_ui_fill_rect(PeSurface *s, int x, int y, int w, int h, PeColor c);
void pe_ui_fill_round_rect(PeSurface *s, float x, float y, float w, float h, float r, PeColor c);
float pe_ui_text_width(const char *text, float em);
void pe_ui_draw_text(PeSurface *s, float x, float baseline, float em, const char *text, PeColor c, PeAlign align);

/* ---- GPU overlay ---- */
void pe_ui_gpu_install(void);
int pe_ui_gpu_ready(void);                          /* the in-window overlay exists */
int pe_ui_gpu_window_size(int *w, int *h);          /* pixels; 0 if no window */
/* Show `s` at (x, y) in window pixels, the rest of the screen dimmed by `dim` (0..1). */
int pe_ui_gpu_show(const PeSurface *s, int x, int y, float dim);
void pe_ui_gpu_hide(void);

#endif
