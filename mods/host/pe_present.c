/* Image presenter: pictures pasted into the finished frame. See pe_present.h.
 *
 * Draw_RenderFrame (draw/frame.c) waits for the previous frame's drawing (DrawSync: PsyZ flushes
 * every queued packet), then the game pastes its own save-screen background, then PE_HOOK_FRAME_DRAWN
 * runs, then the frame is shown. Double buffering (Render_ResetScene): draw environment i draws at
 * y = i * height and the frame just drawn is g_RenderDrawEnvArray[1 - g_ActiveDrawSlot]. A paste
 * there (LoadImage) lands in the frame about to be shown; PsyZ copies it into its upscaled VRAM
 * too, so it shows at any internal resolution, scaled like a game sprite. */
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>

#include "pe_mod_host.h"
#include "pe_present.h"
#include "pe1/display_env.h"
#include "pe1/draw_buffers.h"
#include "pe1/psyq_gpu.h"

#define PE_FRAME_W 320
#define PE_FRAME_H 224
#define PE_COVERED 128              /* alpha from which a pixel is pasted (STRIPS) */

typedef struct PeRun { uint16_t row, start, len; } PeRun;

typedef struct PeSlot {
    int shown, x, y, w, h;
    PePresentMode mode;
    uint16_t *pixels;               /* w * h, PS1 15-bit (BOX: composited over the box colour) */
    PeRun *runs;                    /* STRIPS: covered spans */
    int run_count;
} PeSlot;

static PeSlot s_slot[PE_PRESENT_SLOTS];
static PePresentStats s_stats;
static double s_total_us;
static unsigned s_frames;

static uint16_t pe_15bit(unsigned r, unsigned g, unsigned b) {
    return (uint16_t)((r >> 3) | (g >> 3) << 5 | (b >> 3) << 10);
}

static void pe_slot_free(PeSlot *s) {
    free(s->pixels);
    free(s->runs);
    memset(s, 0, sizeof *s);
}

int pe_present_show(int slot, int x, int y, const PeImage *image, PePresentMode mode, uint32_t box_rgb) {
    PeSlot *s;
    int i, row, n;
    const unsigned br = box_rgb >> 16 & 255, bg = box_rgb >> 8 & 255, bb = box_rgb & 255;
    if (slot < 0 || slot >= PE_PRESENT_SLOTS || !image || !image->rgba || image->w <= 0 || image->h <= 0 ||
        image->w > PE_FRAME_W || image->h > PE_FRAME_H) return -1;
    s = &s_slot[slot];
    pe_slot_free(s);
    s->pixels = malloc((size_t)image->w * image->h * sizeof *s->pixels);
    if (!s->pixels) return -1;
    for (i = 0; i < image->w * image->h; ++i) {
        const uint8_t *p = image->rgba + 4 * i;
        const unsigned a = p[3];
        if (mode == PE_PRESENT_BOX)     /* straight alpha over the box colour */
            s->pixels[i] = pe_15bit((p[0] * a + br * (255 - a)) / 255, (p[1] * a + bg * (255 - a)) / 255,
                                    (p[2] * a + bb * (255 - a)) / 255);
        else
            s->pixels[i] = pe_15bit(p[0], p[1], p[2]);
    }
    if (mode == PE_PRESENT_STRIPS) {
        s->runs = malloc(sizeof *s->runs * ((size_t)image->w / 2 + 1) * image->h);
        if (!s->runs) { pe_slot_free(s); return -1; }
        for (row = 0, n = 0; row < image->h; ++row) {
            const uint8_t *line = image->rgba + (size_t)4 * image->w * row;
            int col = 0;
            while (col < image->w) {
                int start;
                while (col < image->w && line[4 * col + 3] < PE_COVERED) ++col;
                start = col;
                while (col < image->w && line[4 * col + 3] >= PE_COVERED) ++col;
                if (col > start) s->runs[n++] = (PeRun){(uint16_t)row, (uint16_t)start, (uint16_t)(col - start)};
            }
        }
        s->run_count = n;
    }
    s->x = x;
    s->y = y;
    s->w = image->w;
    s->h = image->h;
    s->mode = mode;
    s->shown = 1;
    return 0;
}

void pe_present_hide(int slot) {
    if (slot >= 0 && slot < PE_PRESENT_SLOTS) pe_slot_free(&s_slot[slot]);
}

void pe_present_stats(PePresentStats *out) {
    if (out) *out = s_stats;
}

/* Paste a w x h block (or one strip) of the slot at frame position (fx, fy), clipped to the frame. */
static unsigned pe_paste(const RECT *frame, const PeSlot *s, int sx, int sy, int w, int h) {
    RECT r;
    int fx = s->x + sx, fy = s->y + sy;
    if (fx < 0 || fy < 0 || fx + w > frame->w || fy + h > frame->h) return 0;   /* off the frame: skip */
    r.x = (short)(frame->x + fx);
    r.y = (short)(frame->y + fy);
    r.w = (short)w;
    r.h = (short)h;
    if (w == s->w)
        LoadImage(&r, s->pixels + (size_t)sy * s->w);
    else {
        int row;
        for (row = 0; row < h; ++row) {   /* a strip is one row; contiguous in the slot */
            r.y = (short)(frame->y + fy + row);
            r.h = 1;
            LoadImage(&r, s->pixels + (size_t)(sy + row) * s->w + sx);
        }
    }
    return 1;
}

static int pe_on_frame_drawn(void *ctx, void *user) {
    int i, j, any = 0;
    unsigned pastes = 0;
    Uint64 t0;
    const RECT *frame;
    (void)ctx;
    (void)user;
    for (i = 0; i < PE_PRESENT_SLOTS; ++i) any |= s_slot[i].shown;
    if (!any) return 0;
    if (g_RenderDispEnvArray[g_ActiveDrawSlot & 1].isrgb24) return 0;   /* 24-bit display (movies): 15-bit pixels would garble */
    t0 = SDL_GetPerformanceCounter();
    frame = &g_RenderDrawEnvArray[1 - (g_ActiveDrawSlot & 1)].clip;
    for (i = 0; i < PE_PRESENT_SLOTS; ++i) {
        const PeSlot *s = &s_slot[i];
        if (!s->shown) continue;
        if (s->mode == PE_PRESENT_BOX) pastes += pe_paste(frame, s, 0, 0, s->w, s->h);
        else for (j = 0; j < s->run_count; ++j)
            pastes += pe_paste(frame, s, s->runs[j].start, s->runs[j].row, s->runs[j].len, 1);
    }
    s_stats.frame_x = frame->x;
    s_stats.frame_y = frame->y;
    s_stats.frame_w = frame->w;
    s_stats.frame_h = frame->h;
    s_stats.draw_slot = g_ActiveDrawSlot;
    s_stats.pastes = pastes;
    s_total_us += (double)(SDL_GetPerformanceCounter() - t0) * 1e6 / (double)SDL_GetPerformanceFrequency();
    if (++s_frames >= 60) {                /* a running one-second average */
        s_stats.avg_us = s_total_us / s_frames;
        s_total_us = 0;
        s_frames = 0;
    }
    return 0;
}

void pe_present_install(void) {
    pe_hook_add(PE_HOOK_FRAME_DRAWN, pe_on_frame_drawn, NULL, PE_PRIORITY_NORMAL, "present");
}

#if PE_NATIVE_STATE_CPU_COMPONENTS
int pe_present_state_empty(void){for(unsigned i=0;i<PE_PRESENT_SLOTS;i++)if(s_slot[i].shown||s_slot[i].pixels||s_slot[i].runs)return 0;return 1;}
#endif
