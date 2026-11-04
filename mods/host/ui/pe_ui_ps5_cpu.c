/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 PE-PORT contributors.
 * Main-thread CPU presentation adapter for the existing premultiplied RGBA UI.
 * No SDL surface/window or caller-owned canvas survives a callback.
 */
#include <SDL3/SDL.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <psyz/overlay.h>
#include "pe_ui.h"

enum { MAX_SIDE = 4096, MAX_PITCH = MAX_SIDE * 8 };
static uint8_t *s_pixels;
static int s_w, s_h, s_x, s_y, s_win_w, s_win_h, s_visible;
static unsigned s_dim;
static PsyzOverlayDestroyCB s_prev_destroy;

void pe_ui_gpu_hide(void) {
    free(s_pixels);
    s_pixels = NULL;
    s_visible = 0;
}
static void pe_destroy(void) {
    pe_ui_gpu_hide();
    s_win_w = s_win_h = 0;
    if (s_prev_destroy) s_prev_destroy();
}
void pe_ui_gpu_install(void) {
    s_prev_destroy = Psyz_OverlayDestroyCB(pe_destroy);
}
int pe_ui_gpu_ready(void) { return s_win_w > 0 && s_win_h > 0; }
int pe_ui_gpu_window_size(int *w, int *h) {
    if (!w || !h) return 0;
    *w = s_win_w; *h = s_win_h;
    return pe_ui_gpu_ready();
}
int pe_ui_gpu_show(const PeSurface *s, int x, int y, float dim) {
    uint8_t *copy;
    size_t bytes;
    if (!s || !s->pixels || s->w <= 0 || s->h <= 1 ||
        s->w > MAX_SIDE || s->h > MAX_SIDE ||
        x < -MAX_SIDE || y < -MAX_SIDE || x > MAX_SIDE || y > MAX_SIDE ||
        !(dim >= 0.0f && dim <= 1.0f)) {
        pe_ui_gpu_hide();
        return 0;
    }
    /* Bounded before multiplication; exclude the rasterizer's solid texel row. */
    bytes = (size_t)s->w * (size_t)(s->h - 1) * 4u;
    copy = malloc(bytes);
    if (!copy) { pe_ui_gpu_hide(); return 0; }
    memcpy(copy, s->pixels, bytes);
    free(s_pixels);
    s_pixels = copy; s_w = s->w; s_h = s->h - 1;
    s_x = x; s_y = y; s_dim = (unsigned)(dim * 255.0f + 0.5f);
    s_visible = 1;
    return 1;
}

void pe_ps5_mods_present(SDL_Surface *dst) {
    const SDL_PixelFormatDetails *fmt;
    int x, y;
    s_win_w = s_win_h = 0;
    if (!dst || !dst->pixels || dst->w <= 0 || dst->h <= 0 ||
        dst->w > MAX_SIDE || dst->h > MAX_SIDE || dst->pitch < dst->w * 4 ||
        dst->pitch > MAX_PITCH) return;
    /* Only ordinary 32-bit RGB byte layouts; SDL reports shifts and masks.
     * RGB565, indexed, planar, HDR and other layouts fail closed. */
    fmt = SDL_GetPixelFormatDetails(dst->format);
    if (!fmt || fmt->bytes_per_pixel != 4 || fmt->Rbits != 8 ||
        fmt->Gbits != 8 || fmt->Bbits != 8 ||
        (fmt->Abits != 0 && fmt->Abits != 8)) return;
    s_win_w = dst->w; s_win_h = dst->h;
    if (!s_visible || !s_pixels) return;
    if (!SDL_LockSurface(dst)) return;
    for (y = 0; y < dst->h; ++y) {
        uint8_t *row = (uint8_t *)dst->pixels + (size_t)y * (size_t)dst->pitch;
        for (x = 0; x < dst->w; ++x) {
            uint32_t pixel, rgb[3];
            const int sx = x - s_x, sy = y - s_y;
            const uint8_t *src = NULL;
            unsigned i, alpha = 0;
            const unsigned shifts[3] = {fmt->Rshift, fmt->Gshift, fmt->Bshift};
            memcpy(&pixel, row + (size_t)x * 4u, 4);
            if (sx >= 0 && sy >= 0 && sx < s_w && sy < s_h) {
                src = s_pixels + ((size_t)sy * (size_t)s_w + (size_t)sx) * 4u;
                alpha = src[3];
            }
            for (i = 0; i < 3; ++i) {
                unsigned value = (pixel >> shifts[i]) & 255u;
                value = (value * (255u - s_dim) + 127u) / 255u;
                value = (src ? src[i] : 0u) + (value * (255u - alpha) + 127u) / 255u;
                rgb[i] = value > 255u ? 255u : value;
            }
            pixel = (pixel & ~(fmt->Rmask | fmt->Gmask | fmt->Bmask)) |
                (rgb[0] << fmt->Rshift) | (rgb[1] << fmt->Gshift) | (rgb[2] << fmt->Bshift);
            memcpy(row + (size_t)x * 4u, &pixel, 4);
        }
    }
    SDL_UnlockSurface(dst);
}
