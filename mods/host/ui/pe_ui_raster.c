/* CPU drawing for the mod host's UI: rounded boxes and text into an RGBA surface. */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "../../../src/main/boot/stb_truetype.h"
#include "../../../src/main/boot/fonts/ibm_plex_sans.ttf.inc"

#include "pe_ui.h"

const PeColor pe_ui_panel = {0.075f, 0.082f, 0.102f, 0.96f};
const PeColor pe_ui_border = {1.0f, 1.0f, 1.0f, 0.10f};
const PeColor pe_ui_accent = {0.86f, 0.36f, 0.30f, 1.0f}; /* red, so it never looks like Settings */
const PeColor pe_ui_text = {0.93f, 0.95f, 0.98f, 1.0f};
const PeColor pe_ui_muted = {0.60f, 0.65f, 0.73f, 1.0f};

PeColor pe_ui_alpha(PeColor c, float a) { c.a = a; return c; }

static stbtt_fontinfo s_font;
static int s_font_state;

int pe_ui_font_ready(void) {
    int offset;
    if (s_font_state) return s_font_state > 0;
    offset = stbtt_GetFontOffsetForIndex(ibm_plex_sans_ttf, 0);
    s_font_state = offset >= 0 && stbtt_InitFont(&s_font, ibm_plex_sans_ttf, offset) ? 1 : -1;
    return s_font_state > 0;
}

int pe_ui_surface_begin(PeSurface *s, int w, int h) {
#ifdef PE_PS5_PLUGINS
    if (!s || w <= 0 || h <= 0 || w > 4096 || h >= 4096) return 0;
#endif
    if (!s->pixels || s->w != w || s->h != h + 1) {
        free(s->pixels);
        s->pixels = (uint8_t *)calloc((size_t)w * (size_t)(h + 1), 4u);
        s->w = s->pixels ? w : 0;
        s->h = s->pixels ? h + 1 : 0;
        return s->pixels != NULL;
    }
    memset(s->pixels, 0, (size_t)s->w * (size_t)s->h * 4u);
    return 1;
}

void pe_ui_surface_finish(PeSurface *s) {
    if (s->pixels) s->pixels[((size_t)(s->h - 1) * (size_t)s->w) * 4u + 3] = 255;
}

void pe_ui_surface_free(PeSurface *s) {
    free(s->pixels);
    memset(s, 0, sizeof *s);
}

static float pe_clamp01(float v) { return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v; }

static void pe_blend(PeSurface *s, int x, int y, PeColor c, float coverage) {
    const float a = c.a * coverage, keep = 1.0f - a;
    uint8_t *p;
    if (x < 0 || y < 0 || x >= s->w || y >= s->h - 1 || a <= 0.0f) return; /* last row: solid texel */
    p = s->pixels + ((size_t)y * (size_t)s->w + (size_t)x) * 4u;
    p[0] = (uint8_t)(c.r * a * 255.0f + p[0] * keep + 0.5f);
    p[1] = (uint8_t)(c.g * a * 255.0f + p[1] * keep + 0.5f);
    p[2] = (uint8_t)(c.b * a * 255.0f + p[2] * keep + 0.5f);
    p[3] = (uint8_t)(a * 255.0f + p[3] * keep + 0.5f);
}

void pe_ui_fill_round_rect(PeSurface *s, float x, float y, float w, float h, float r, PeColor c) {
    int ix, iy;
    for (iy = (int)floorf(y) - 1; iy < (int)ceilf(y + h) + 1; iy++) {
        for (ix = (int)floorf(x) - 1; ix < (int)ceilf(x + w) + 1; ix++) {
            const float qx = fabsf((float)ix + 0.5f - (x + w * 0.5f)) - (w * 0.5f - r);
            const float qy = fabsf((float)iy + 0.5f - (y + h * 0.5f)) - (h * 0.5f - r);
            const float ox = qx > 0.0f ? qx : 0.0f, oy = qy > 0.0f ? qy : 0.0f;
            const float inside = qx > qy ? qx : qy;
            const float d = sqrtf(ox * ox + oy * oy) + (inside < 0.0f ? inside : 0.0f) - r;
            pe_blend(s, ix, iy, c, pe_clamp01(0.5f - d));
        }
    }
}

float pe_ui_text_width(const char *text, float em) {
    const float scale = stbtt_ScaleForMappingEmToPixels(&s_font, em);
    float width = 0.0f;
    for (; *text; text++) {
        int advance, bearing;
        stbtt_GetCodepointHMetrics(&s_font, (unsigned char)*text, &advance, &bearing);
        width += (float)advance * scale;
    }
    return width;
}

void pe_ui_draw_text(PeSurface *s, float x, float baseline, float em, const char *text, PeColor c, PeAlign align) {
    enum { GLYPH_MAX = 256 };
    static uint8_t glyph[GLYPH_MAX * GLYPH_MAX];
    const float scale = stbtt_ScaleForMappingEmToPixels(&s_font, em);
    const float width = pe_ui_text_width(text, em);
    x -= align == PE_ALIGN_CENTRE ? width * 0.5f : align == PE_ALIGN_RIGHT ? width : 0.0f;
    for (; *text; text++) {
        int cp = (unsigned char)*text, advance, bearing, x0, y0, x1, y1, gx, gy;
        stbtt_GetCodepointHMetrics(&s_font, cp, &advance, &bearing);
        stbtt_GetCodepointBitmapBox(&s_font, cp, scale, scale, &x0, &y0, &x1, &y1);
        if (x1 > x0 && y1 > y0 && x1 - x0 <= GLYPH_MAX && y1 - y0 <= GLYPH_MAX) {
            const int gw = x1 - x0, gh = y1 - y0;
            const int ox = (int)floorf(x + 0.5f) + x0, oy = (int)floorf(baseline + 0.5f) + y0;
            stbtt_MakeCodepointBitmap(&s_font, glyph, gw, gh, gw, scale, scale, cp);
            for (gy = 0; gy < gh; gy++)
                for (gx = 0; gx < gw; gx++)
                    if (glyph[gy * gw + gx]) pe_blend(s, ox + gx, oy + gy, c, glyph[gy * gw + gx] / 255.0f);
        }
        x += (float)advance * scale;
    }
}
