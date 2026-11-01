#ifndef PE1_BOOT_SETTINGS_OVERLAY_H
#define PE1_BOOT_SETTINGS_OVERLAY_H

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>
#include <psyz/overlay.h>
#include <psyz/overlay_sdl3.h>
#include <psyz/overlay_sdl3_gpu.h>

#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#include "fonts/ibm_plex_sans.ttf.inc"

/* Pre-compiled and signed shaders from SDL3 GPU renderer (SPIR-V, MSL, DXIL) */
#include "../../../third_party/psyz/external/SDL/src/render/gpu/shaders/tri_texture.vert.spv.h"
#include "../../../third_party/psyz/external/SDL/src/render/gpu/shaders/tri_texture.vert.msl.h"
#include "../../../third_party/psyz/external/SDL/src/render/gpu/shaders/tri_texture.vert.dxil.h"
#include "../../../third_party/psyz/external/SDL/src/render/gpu/shaders/texture_rgba.frag.spv.h"
#include "../../../third_party/psyz/external/SDL/src/render/gpu/shaders/texture_rgba.frag.msl.h"
#include "../../../third_party/psyz/external/SDL/src/render/gpu/shaders/texture_rgba.frag.dxil.h"

/* ---- geometry, in design units that scale with the window (from rage-racer-pc) ---- */

enum {
    OVERLAY_PAGE_GRAPHICS = 0,
    OVERLAY_PAGE_CONTROLS = 1,
    OVERLAY_PAGE_AUDIO = 2,
    OVERLAY_PAGE_DISCS = 3,
    OVERLAY_PAGE_MODS = 4,
    OVERLAY_PAGE_COUNT = 5,
    OVERLAY_ACTION_ROWS = 2,
    OVERLAY_VISIBLE_CONTROLS = 7,

    OVERLAY_PANEL_W = 600,
    OVERLAY_PAD = 32,
    OVERLAY_HEADER_H = 128,
    OVERLAY_ROW_H = 46,
    OVERLAY_ACTION_GAP = 14,
    OVERLAY_FOOTER_H = 74,
    OVERLAY_VALUE_W = 250,
    /* "< 2x >" stepper in front of the speed row's label */
    OVERLAY_SPEED_STEP_CENTRE = 22,
    OVERLAY_SPEED_STEP_REACH = 18,
    OVERLAY_SPEED_STEP_W = 54,
    OVERLAY_TAB_TOP = 62,
    OVERLAY_TAB_H = 34,
    OVERLAY_REFERENCE_H = 820,

    PROMPT_PANEL_W = 460,
    PROMPT_HEADER_H = 112,
    PROMPT_ROWS = 2, /* Yes, No */
    PROMPT_FOOTER_H = 60
};

typedef struct OverlaySurface {
    uint8_t *pixels;
    int w, h;
} OverlaySurface;

typedef struct OverlayLayout {
    float scale;
    int x, y, w, h;
} OverlayLayout;

typedef struct OverlayRect {
    float x, y, w, h;
} OverlayRect;

typedef struct Color {
    float r, g, b, a;
} Color;

typedef struct HostOverlayState {
    int open;
    int page;
    int row;
    int scroll;
    int bind_device;
    int capturing_binding;
    int quit;
    int dirty;
    const char *status;
    int prompt; /* the skip-movie question instead of the settings panel */
    int skip;   /* the question's answer */
} HostOverlayState;

static const Color kPanel = {0.075f, 0.082f, 0.102f, 0.96f};
static const Color kBorder = {1.0f, 1.0f, 1.0f, 0.10f};
static const Color kAccent = {0.27f, 0.62f, 1.0f, 1.0f};
static const Color kText = {0.93f, 0.95f, 0.98f, 1.0f};
static const Color kMuted = {0.60f, 0.65f, 0.73f, 1.0f};
static const Color kDanger = {1.0f, 0.45f, 0.42f, 1.0f};

static Color WithAlpha(Color c, float a) {
    c.a = a;
    return c;
}

/* ---- settings model (rage-racer-pc style) ---- */

typedef struct OverlaySetting {
    const char *label;
    const char *help;
    int count;
    const char *const *names;
    int (*get)(void);
    void (*set)(int index);
} OverlaySetting;

#define COUNT_OF(array) ((int)(sizeof(array) / sizeof((array)[0])))
#define ACCESSORS(Name, read, write)                 \
    static int Get##Name(void) { return (read); }    \
    static void Set##Name(int i) { write; }
#define SETTING(label, help, names, Name) \
    {label, help, COUNT_OF(names), names, Get##Name, Set##Name}

static int ClampIndex(int index, int count) {
    if (index < 0) return 0;
    if (index >= count) return count - 1;
    return index;
}

static const char *const kSwitchNames[] = {"Off", "On"};

/* graphics */

static const unsigned kScaleValues[] = {1, 2, 4, 6, 8};
static const char *const kScaleNames[] = {
    "1x", "2x", "4x", "6x", "8x"
};
static int ScaleIndex(unsigned scale) {
    for (int i = 0; i < COUNT_OF(kScaleValues); ++i) {
        if (scale <= kScaleValues[i]) return i;
    }
    return COUNT_OF(kScaleValues) - 1;
}
ACCESSORS(Scale, ScaleIndex(host_options.scale),
          host_options.scale = kScaleValues[ClampIndex(i, COUNT_OF(kScaleValues))])

ACCESSORS(Fullscreen, host_options.fullscreen != 0,
          host_options.fullscreen = i)

static const char *const kAspectNames[] = {"Classic 4:3", "Square pixels", "Widescreen"};
ACCESSORS(Aspect, host_options.widescreen ? 2 : host_options.aspect != 0,
          (host_options.widescreen = i == 2, host_options.aspect = i == 1))

static const char *const kDitherNames[] = {"Original", "Off"};
ACCESSORS(Dither, host_options.dither != 0,
          host_options.dither = i)

ACCESSORS(GeometryCorrection, host_options.geometry_correction != 0,
          host_options.geometry_correction = i)

static const char *const kAntialiasNames[] = {"Off", "4x SSAA"};
ACCESSORS(Antialiasing, host_options.antialiasing != 0,
          host_options.antialiasing = i)
static const char *const kFilterNames[] = {"Original", "Bilinear"};
ACCESSORS(ModelFiltering, host_options.model_filtering != 0,
          host_options.model_filtering = i)

static const char *const kFrameRateNames[]={"30 Hz (original)","60 Hz (interpolated)"};
ACCESSORS(FrameInterpolation,host_options.frame_interpolation!=0,
          host_options.frame_interpolation=i)

ACCESSORS(DynamicLighting, host_options.dynamic_lighting != 0,
          host_options.dynamic_lighting = i)

ACCESSORS(EffectGeometryCorrection, host_options.effect_geometry_correction != 0,
          host_options.effect_geometry_correction = i)

static const OverlaySetting kGraphics[] = {
    SETTING("Render resolution", "Higher values increase image detail and GPU load.",
            kScaleNames, Scale),
    SETTING("Fullscreen", "Switch between windowed and fullscreen display.",
            kSwitchNames, Fullscreen),
    SETTING("Aspect ratio", "Classic 4:3 matches the PlayStation. Widescreen is 16:9 where the background allows.",
            kAspectNames, Aspect),
    SETTING("Dithering", "PlayStation 15-bit color dithering on shaded polygons.",
            kDitherNames, Dither),
    SETTING("Geometry correction", "Subpixel vertex precision and perspective-correct 3D textures.",
            kSwitchNames, GeometryCorrection),
    SETTING("Antialiasing", "Smoother edges: twice each axis, four samples per pixel. Uses more GPU time and memory.",
            kAntialiasNames, Antialiasing),
    SETTING("Model textures", "Bilinear filtering for 3D models; backgrounds and text keep original sampling.",
            kFilterNames, ModelFiltering),
    SETTING("Gameplay display", "Interpolate models and scrolling between original game frames. Game speed and input timing stay original.",
            kFrameRateNames, FrameInterpolation),
    SETTING("Dynamic lighting", "Smooth 3D directional lighting on characters and monsters.",
            kSwitchNames, DynamicLighting),
    SETTING("Effect geometry", "Subpixel vertex precision for spell and particle effects.",
            kSwitchNames, EffectGeometryCorrection),
};

/* audio */

static const char *const kVolumeNames[] = {
    "Off", "5%",  "10%", "15%", "20%", "25%", "30%", "35%", "40%", "45%", "50%",
    "55%", "60%", "65%", "70%", "75%", "80%", "85%", "90%", "95%", "100%"
};
ACCESSORS(Volume, ClampIndex(((int)host_options.volume + 2) / 5, COUNT_OF(kVolumeNames)),
          host_options.volume = (unsigned)(i * 5))

static const OverlaySetting kAudio[] = {
    SETTING("Master volume", "Overall SPU music, voice and sound effect volume.",
            kVolumeNames, Volume),
};

static const char *const kPageNames[OVERLAY_PAGE_COUNT] = {
    "Graphics", "Controls", "Audio", "Discs", "Mods"
};

static const char *OverlayPageName(int page) {
    return page >= 0 && page < OVERLAY_PAGE_COUNT ? kPageNames[page] : "";
}

static const OverlaySetting *OverlayPageSettings(int page, int *count) {
    switch (page) {
    case OVERLAY_PAGE_GRAPHICS:
        *count = COUNT_OF(kGraphics);
        return kGraphics;
    case OVERLAY_PAGE_AUDIO:
        *count = COUNT_OF(kAudio);
        return kAudio;
    default:
        *count = 0;
        return NULL;
    }
}

static int Host_SettingsPureRowCount(int page) {
    if (page == OVERLAY_PAGE_GRAPHICS) return COUNT_OF(kGraphics);
    if (page == OVERLAY_PAGE_CONTROLS) return 3 + HOST_BINDING_COUNT;
    if (page == OVERLAY_PAGE_AUDIO) return COUNT_OF(kAudio);
    if (page == OVERLAY_PAGE_MODS) return 3;
    return 2;
}

static int Host_SettingsTotalRowCount(int page) {
    return Host_SettingsPureRowCount(page) + OVERLAY_ACTION_ROWS;
}

enum {
    OVERLAY_CONTROLS_ITEM_COUNT = HOST_BINDING_COUNT + 2,
    OVERLAY_CONTROLS_VISIBLE_BINDINGS = OVERLAY_VISIBLE_CONTROLS - 1,
    OVERLAY_CONTROLS_MAX_SCROLL = OVERLAY_CONTROLS_ITEM_COUNT - OVERLAY_CONTROLS_VISIBLE_BINDINGS
};

static int Host_SettingsVisibleSettingCount(int page) {
    (void)page;
    return COUNT_OF(kGraphics)>OVERLAY_VISIBLE_CONTROLS ? COUNT_OF(kGraphics) : OVERLAY_VISIBLE_CONTROLS;
}

static int Host_SettingsVisibleRowCount(int page) {
    return Host_SettingsVisibleSettingCount(page) + OVERLAY_ACTION_ROWS;
}

static int Host_ClampControlsScroll(int scroll) {
    if (scroll < 0) return 0;
    if (scroll > OVERLAY_CONTROLS_MAX_SCROLL) return OVERLAY_CONTROLS_MAX_SCROLL;
    return scroll;
}

/* Scrolls Controls by the minimum amount needed to keep a selected binding
 * or field-threshold row in view. Mouse hover never calls this. */
static void Host_EnsureControlsRowVisible(HostOverlayState *state) {
    state->scroll = Host_ClampControlsScroll(state->scroll);
    if (state->page != OVERLAY_PAGE_CONTROLS) {
        return;
    }
    if (state->row <= 0 || state->row > OVERLAY_CONTROLS_ITEM_COUNT) {
        return;
    }
    int bindingIdx = state->row - 1;
    if (bindingIdx < state->scroll) {
        state->scroll = bindingIdx;
    } else if (bindingIdx >= state->scroll + OVERLAY_CONTROLS_VISIBLE_BINDINGS) {
        state->scroll = bindingIdx - OVERLAY_CONTROLS_VISIBLE_BINDINGS + 1;
    }
}

/* ---- surface ---- */

static int OverlaySurfaceResize(OverlaySurface *surface, int width, int height) {
    size_t bytes = (size_t)width * (size_t)(height + 1) * 4u;

    if (width == surface->w && height + 1 == surface->h &&
        surface->pixels != NULL) {
        return 1;
    }
    free(surface->pixels);
    surface->pixels = (uint8_t *)calloc(1, bytes);
    surface->w = surface->pixels != NULL ? width : 0;
    surface->h = surface->pixels != NULL ? height + 1 : 0;
    return surface->pixels != NULL;
}

static void OverlaySurfaceRelease(OverlaySurface *surface) {
    free(surface->pixels);
    *surface = (OverlaySurface){0};
}

static float Clamp01(float v) { return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v; }

static void BlendPixel(OverlaySurface *s, int x, int y, Color c, float coverage) {
    uint8_t *p;
    float a = c.a * coverage;
    float keep = 1.0f - a;

    /* The last row is the solid texel, not part of the panel. */
    if (x < 0 || y < 0 || x >= s->w || y >= s->h - 1 || a <= 0.0f) return;
    p = s->pixels + ((size_t)y * (size_t)s->w + (size_t)x) * 4u;
    p[0] = (uint8_t)(c.r * a * 255.0f + p[0] * keep + 0.5f);
    p[1] = (uint8_t)(c.g * a * 255.0f + p[1] * keep + 0.5f);
    p[2] = (uint8_t)(c.b * a * 255.0f + p[2] * keep + 0.5f);
    p[3] = (uint8_t)(a * 255.0f + p[3] * keep + 0.5f);
}

/* ---- shapes ---- */

static float RoundRectDistance(float px, float py, float x, float y, float w,
                               float h, float r) {
    float cx = x + w * 0.5f;
    float cy = y + h * 0.5f;
    float qx = fabsf(px - cx) - (w * 0.5f - r);
    float qy = fabsf(py - cy) - (h * 0.5f - r);
    float ox = qx > 0.0f ? qx : 0.0f;
    float oy = qy > 0.0f ? qy : 0.0f;
    float inside = qx > qy ? qx : qy;

    return sqrtf(ox * ox + oy * oy) + (inside < 0.0f ? inside : 0.0f) - r;
}

static void FillRoundRect(OverlaySurface *s, float x, float y, float w, float h,
                          float r, Color c) {
    int x0 = (int)floorf(x) - 1, x1 = (int)ceilf(x + w) + 1;
    int y0 = (int)floorf(y) - 1, y1 = (int)ceilf(y + h) + 1;
    int ix, iy;

    for (iy = y0; iy < y1; iy++) {
        for (ix = x0; ix < x1; ix++) {
            float d = RoundRectDistance((float)ix + 0.5f, (float)iy + 0.5f, x,
                                        y, w, h, r);
            BlendPixel(s, ix, iy, c, Clamp01(0.5f - d));
        }
    }
}

/* A capsule: the thick line segment the chevrons are made of. */
static void DrawSegment(OverlaySurface *s, float ax, float ay, float bx,
                        float by, float width, Color c) {
    float minX = (ax < bx ? ax : bx) - width - 1.0f;
    float maxX = (ax > bx ? ax : bx) + width + 1.0f;
    float minY = (ay < by ? ay : by) - width - 1.0f;
    float maxY = (ay > by ? ay : by) + width + 1.0f;
    float ex = bx - ax, ey = by - ay;
    float length2 = ex * ex + ey * ey;
    int ix, iy;

    for (iy = (int)floorf(minY); iy < (int)ceilf(maxY); iy++) {
        for (ix = (int)floorf(minX); ix < (int)ceilf(maxX); ix++) {
            float px = (float)ix + 0.5f - ax;
            float py = (float)iy + 0.5f - ay;
            float t = length2 > 0.0f ? (px * ex + py * ey) / length2 : 0.0f;
            float qx, qy, d;

            t = Clamp01(t);
            qx = px - ex * t;
            qy = py - ey * t;
            d = sqrtf(qx * qx + qy * qy) - width * 0.5f;
            BlendPixel(s, ix, iy, c, Clamp01(0.5f - d));
        }
    }
}

static void DrawChevron(OverlaySurface *s, float cx, float cy, float size,
                        int direction, float width, Color c) {
    float tipX = cx + (float)direction * size * 0.5f;
    float backX = cx - (float)direction * size * 0.5f;

    DrawSegment(s, backX, cy - size, tipX, cy, width, c);
    DrawSegment(s, backX, cy + size, tipX, cy, width, c);
}

/* ---- text ---- */

static stbtt_fontinfo s_font;
static int s_fontState;

static int OverlayFontReady(void) {
    int offset;

    if (s_fontState != 0) return s_fontState > 0;
    offset = stbtt_GetFontOffsetForIndex(ibm_plex_sans_ttf, 0);
    if (offset < 0 || (size_t)offset >= ibm_plex_sans_ttf_len ||
        !stbtt_InitFont(&s_font, ibm_plex_sans_ttf, offset)) {
        s_fontState = -1;
        return 0;
    }
    s_fontState = 1;
    return 1;
}

static float TextWidth(const char *text, float em) {
    float scale = stbtt_ScaleForMappingEmToPixels(&s_font, em);
    float width = 0.0f;

    for (; *text != '\0'; text++) {
        int advance, bearing;

        stbtt_GetCodepointHMetrics(&s_font, (unsigned char)*text, &advance,
                                   &bearing);
        width += (float)advance * scale;
    }
    return width;
}

typedef enum TextAlign { ALIGN_LEFT, ALIGN_CENTRE, ALIGN_RIGHT } TextAlign;

static void DrawText(OverlaySurface *s, float x, float baseline, float em,
                     const char *text, Color c, TextAlign align) {
    enum { GLYPH_MAX = 256 };
    static uint8_t glyph[GLYPH_MAX * GLYPH_MAX];
    float scale = stbtt_ScaleForMappingEmToPixels(&s_font, em);
    float width = TextWidth(text, em);

    x -= align == ALIGN_CENTRE ? width * 0.5f
         : align == ALIGN_RIGHT ? width : 0.0f;
    for (; *text != '\0'; text++) {
        int codepoint = (unsigned char)*text;
        int advance, bearing, x0, y0, x1, y1, gx, gy;

        stbtt_GetCodepointHMetrics(&s_font, codepoint, &advance, &bearing);
        stbtt_GetCodepointBitmapBox(&s_font, codepoint, scale, scale, &x0, &y0,
                                    &x1, &y1);
        if (x1 - x0 > 0 && y1 - y0 > 0 && x1 - x0 <= GLYPH_MAX &&
            y1 - y0 <= GLYPH_MAX) {
            int gw = x1 - x0, gh = y1 - y0;
            int ox = (int)floorf(x + 0.5f) + x0;
            int oy = (int)floorf(baseline + 0.5f) + y0;

            stbtt_MakeCodepointBitmap(&s_font, glyph, gw, gh, gw, scale, scale,
                                      codepoint);
            for (gy = 0; gy < gh; gy++) {
                for (gx = 0; gx < gw; gx++) {
                    uint8_t coverage = glyph[gy * gw + gx];
                    if (coverage != 0) {
                        BlendPixel(s, ox + gx, oy + gy, c,
                                   (float)coverage / 255.0f);
                    }
                }
            }
        }
        x += (float)advance * scale;
    }
}

/* ---- geometry helpers ---- */

static int OverlayDesignHeight(int rowCount) {
    return OVERLAY_HEADER_H + rowCount * OVERLAY_ROW_H + OVERLAY_ACTION_GAP +
           OVERLAY_FOOTER_H;
}

static void OverlayFitLayout(int designW, int designH, int windowW, int windowH,
                             OverlayLayout *layout) {
    float scale = (float)windowH / (float)OVERLAY_REFERENCE_H;
    float widthLimit = (float)windowW * 0.94f / (float)designW;
    float heightLimit = (float)windowH * 0.94f / (float)designH;

    if (scale > widthLimit) scale = widthLimit;
    if (scale > heightLimit) scale = heightLimit;
    if (scale > 2.5f) scale = 2.5f;
    if (scale < 0.5f) scale = 0.5f;
    scale = floorf(scale * 8.0f) / 8.0f; /* no repaint on every resize pixel */
    layout->scale = scale;
    layout->w = (int)ceilf((float)designW * scale);
    layout->h = (int)ceilf((float)designH * scale);
    layout->x = (windowW - layout->w) / 2;
    layout->y = (windowH - layout->h) / 2;
}

static void OverlayComputeLayout(int page, int windowW, int windowH,
                                 OverlayLayout *layout) {
    OverlayFitLayout(OVERLAY_PANEL_W, OverlayDesignHeight(Host_SettingsVisibleRowCount(page)),
                     windowW, windowH, layout);
}

static int OverlayPromptDesignHeight(void) {
    return PROMPT_HEADER_H + PROMPT_ROWS * OVERLAY_ROW_H + PROMPT_FOOTER_H;
}

static OverlayRect OverlayPromptRowRect(int row) {
    return (OverlayRect){(float)(OVERLAY_PAD / 2),
                         (float)(PROMPT_HEADER_H + row * OVERLAY_ROW_H),
                         (float)(PROMPT_PANEL_W - OVERLAY_PAD),
                         (float)OVERLAY_ROW_H};
}

static void OverlayComputeStateLayout(const HostOverlayState *state, int windowW,
                                      int windowH, OverlayLayout *layout) {
    if (state->prompt) {
        OverlayFitLayout(PROMPT_PANEL_W, OverlayPromptDesignHeight(), windowW, windowH,
                         layout);
    } else {
        OverlayComputeLayout(state->page, windowW, windowH, layout);
    }
}

static float OverlayRowTop(int settingCount, int row) {
    return (float)(OVERLAY_HEADER_H + row * OVERLAY_ROW_H +
                   (row >= settingCount ? OVERLAY_ACTION_GAP : 0));
}

static OverlayRect OverlayTabRect(int page) {
    float width = (float)(OVERLAY_PANEL_W - 2 * OVERLAY_PAD) /
                  (float)OVERLAY_PAGE_COUNT;

    return (OverlayRect){(float)OVERLAY_PAD + width * (float)page,
                         (float)OVERLAY_TAB_TOP, width,
                         (float)OVERLAY_TAB_H};
}

static float OverlayValueCentre(void) {
    return (float)(OVERLAY_PANEL_W - OVERLAY_PAD) - (float)OVERLAY_VALUE_W * 0.5f;
}

/* Maps a logical row on `state->page` to its visible slot (0..visibleRows-1), or -1 if scrolled off */
static int OverlayVisibleSlotForRow(const HostOverlayState *state, int row) {
    int page = state->page;
    int pureCount = Host_SettingsPureRowCount(page);
    int visibleSettings = Host_SettingsVisibleSettingCount(page);
    if (row >= pureCount) {
        return visibleSettings + (row - pureCount);
    }
    if (page != OVERLAY_PAGE_CONTROLS) {
        return row;
    }
    if (row == 0) {
        return 0;
    }
    int scroll = Host_ClampControlsScroll(state->scroll);
    int bindingIdx = row - 1;
    if (bindingIdx < scroll || bindingIdx >= scroll + OVERLAY_CONTROLS_VISIBLE_BINDINGS) {
        return -1;
    }
    return 1 + (bindingIdx - scroll);
}

static OverlayRect OverlayRowRect(const HostOverlayState *state, int row) {
    int visibleSettings = Host_SettingsVisibleSettingCount(state->page);
    int slot = OverlayVisibleSlotForRow(state, row);
    if (slot < 0) {
        return (OverlayRect){0.0f, -1000.0f, 0.0f, 0.0f};
    }
    float top = OverlayRowTop(visibleSettings, slot);
    return (OverlayRect){
        (float)(OVERLAY_PAD / 2),
        top,
        (float)(OVERLAY_PANEL_W - OVERLAY_PAD),
        (float)OVERLAY_ROW_H
    };
}

/* ---- the panel (rage-racer-pc Painter) ---- */

typedef struct Painter {
    OverlaySurface *surface;
    const HostOverlayState *state;
    float s; /* design units to pixels */
} Painter;

static void PaintFrame(const Painter *p, float width, float height) {
    const float s = p->s;

    FillRoundRect(p->surface, 0.0f, 0.0f, width, height, 16.0f * s, kBorder);
    FillRoundRect(p->surface, 1.0f, 1.0f, width - 2.0f, height - 2.0f,
                  15.0f * s, kPanel);
}

static void PaintHeader(const Painter *p) {
    const float s = p->s;
    int page;

    DrawText(p->surface, (float)OVERLAY_PAD * s, 40.0f * s, 26.0f * s,
             "Settings", kText, ALIGN_LEFT);
    DrawText(p->surface, (float)(OVERLAY_PANEL_W - OVERLAY_PAD) * s, 38.0f * s,
             14.0f * s, "GAME PAUSED", kMuted, ALIGN_RIGHT);
    for (page = 0; page < OVERLAY_PAGE_COUNT; page++) {
        OverlayRect tab = OverlayTabRect(page);
        int current = page == p->state->page;

        if (current) {
            FillRoundRect(p->surface, tab.x * s, tab.y * s, tab.w * s,
                          tab.h * s, 8.0f * s, WithAlpha(kAccent, 0.18f));
        }
        DrawText(p->surface, (tab.x + tab.w * 0.5f) * s,
                 (tab.y + tab.h * 0.5f + 5.5f) * s, 15.0f * s,
                 OverlayPageName(page), current ? kText : kMuted, ALIGN_CENTRE);
    }
    FillRoundRect(p->surface, (float)OVERLAY_PAD * s,
                  (float)(OVERLAY_HEADER_H - 12) * s,
                  (float)(OVERLAY_PANEL_W - 2 * OVERLAY_PAD) * s, 1.0f, 0.0f,
                  WithAlpha(kText, 0.10f));
}

static void PaintValueRow(const Painter *p, const char *labelText,
                          const char *valueText, float mid, float baseline,
                          int selected, int highlightValue, Color label) {
    const float s = p->s;
    float centre = OverlayValueCentre() * s;
    float reach = ((float)OVERLAY_VALUE_W * 0.5f - 14.0f) * s;
    Color chevron = selected ? kAccent : WithAlpha(kMuted, 0.75f);

    DrawText(p->surface, (float)OVERLAY_PAD * s, baseline, 17.0f * s,
             labelText, label, ALIGN_LEFT);
    DrawChevron(p->surface, centre - reach, mid, 5.0f * s, -1, 2.0f * s,
                chevron);
    DrawChevron(p->surface, centre + reach, mid, 5.0f * s, 1, 2.0f * s,
                chevron);
    DrawText(p->surface, centre, baseline, 17.0f * s, valueText,
             highlightValue ? kAccent : (selected ? kText : WithAlpha(kText, 0.9f)),
             ALIGN_CENTRE);
}

/* "< 3x > speed": the arrows step the hold's speed; a dim arrow is the limit. */
static void PaintSpeedLabel(const Painter *p, float mid, float baseline,
                            int selected, Color label) {
    const float s = p->s;
    float centre = (float)(OVERLAY_PAD + OVERLAY_SPEED_STEP_CENTRE) * s;
    float reach = (float)OVERLAY_SPEED_STEP_REACH * s;
    unsigned factor = host_options.speed_factor;
    Color chevron = selected ? kAccent : WithAlpha(kMuted, 0.75f);
    Color limit = WithAlpha(kMuted, 0.3f);
    char text[8];

    snprintf(text, sizeof(text), "%ux", factor);
    DrawChevron(p->surface, centre - reach, mid, 5.0f * s, -1, 2.0f * s,
                factor > 2 ? chevron : limit);
    DrawChevron(p->surface, centre + reach, mid, 5.0f * s, 1, 2.0f * s,
                factor < PSYZ_SPEED_MAX ? chevron : limit);
    DrawText(p->surface, centre, baseline, 17.0f * s, text, label, ALIGN_CENTRE);
    DrawText(p->surface, (float)(OVERLAY_PAD + OVERLAY_SPEED_STEP_W) * s,
             baseline, 17.0f * s, "speed", label, ALIGN_LEFT);
}

static void PaintSettingRow(const Painter *p, const OverlaySetting *setting,
                            float mid, float baseline, int selected,
                            Color label) {
    int value = setting->get();
    if (value < 0) value = 0;
    if (value >= setting->count) value = setting->count - 1;
    PaintValueRow(p, setting->label, setting->names[value], mid, baseline,
                  selected, 0, label);
}

static const char *Host_ControlBindingValueName(const HostOverlayState *state,
                                                int bindingIndex) {
    if (state->bind_device == HOST_BIND_CONTROLLER) {
        int btn = host_options.buttons[bindingIndex];
        if (btn == SDL_GAMEPAD_BUTTON_COUNT) return "Left trigger";
        if (btn == SDL_GAMEPAD_BUTTON_COUNT + 1) return "Right trigger";
        const char *name = SDL_GetGamepadStringForButton((SDL_GamepadButton)btn);
        return (name && *name) ? name : "Unbound";
    } else {
        /* Bindings are physical scancodes; label them with the key's name in the active
         * layout (SDL_GetScancodeName always reports the US-layout name, so QWERTZ's Y read "Z"). */
        SDL_Scancode scancode = (SDL_Scancode)host_options.keys[bindingIndex];
        SDL_Keycode keycode = SDL_GetKeyFromScancode(scancode, SDL_KMOD_NONE, false);
        const char *name = keycode != SDLK_UNKNOWN ? SDL_GetKeyName(keycode) : NULL;
        /* DrawText and the bundled font are Latin-only: Cyrillic, kana and other non-ASCII
         * names fall back to the Latin legend, which those keyboards also print on the key. */
        for (const char *c = name; c && *c; c++) {
            if ((unsigned char)*c >= 0x80) {
                name = NULL;
                break;
            }
        }
        if (!name || !*name) name = SDL_GetScancodeName(scancode);
        return (name && *name) ? name : "Unbound";
    }
}

static void PaintRows(const Painter *p) {
    const float s = p->s;
    int page = p->state->page;
    int declCount = 0;
    const OverlaySetting *settings = OverlayPageSettings(page, &declCount);
    int pureCount = Host_SettingsPureRowCount(page);
    int visibleSettings = Host_SettingsVisibleSettingCount(page);
    int totalRows = pureCount + OVERLAY_ACTION_ROWS;
    int row;

    for (row = 0; row < totalRows; row++) {
        int slot = OverlayVisibleSlotForRow(p->state, row);
        if (slot < 0) continue;

        float top = OverlayRowTop(visibleSettings, slot) * s;
        float mid = top + (float)OVERLAY_ROW_H * 0.5f * s;
        float baseline = mid + 6.0f * s;
        int selected = row == p->state->row;
        Color label = selected ? kText : WithAlpha(kText, 0.82f);

        if (selected) {
            FillRoundRect(p->surface, (float)(OVERLAY_PAD / 2) * s,
                          top + 3.0f * s, (float)(OVERLAY_PANEL_W - OVERLAY_PAD) * s,
                          (float)(OVERLAY_ROW_H - 6) * s, 9.0f * s,
                          WithAlpha(kAccent, 0.16f));
            FillRoundRect(p->surface, (float)(OVERLAY_PAD / 2) * s + 4.0f * s,
                          mid - 10.0f * s, 3.0f * s, 20.0f * s, 1.5f * s,
                          kAccent);
        }
        if (row < pureCount) {
            if (settings != NULL && row < declCount) {
                PaintSettingRow(p, &settings[row], mid, baseline, selected, label);
            } else if (page == OVERLAY_PAGE_CONTROLS) {
                if (row == 0) {
                    const char *dev = p->state->bind_device == HOST_BIND_CONTROLLER
                                          ? "Controller"
                                          : "Keyboard";
                    PaintValueRow(p, "Input device", dev, mid, baseline,
                                  selected, 0, label);
                } else if (row == HOST_ANALOG_RUN_ROW) {
                    char value[32];
                    snprintf(value, sizeof(value), "%u%%", host_options.analog_run_threshold);
                    PaintValueRow(p, "Field run threshold", value, mid, baseline, selected, 0, label);
                } else if (row == HOST_SPEED_MODE_ROW) {
                    PaintValueRow(p, "Speed button", host_options.speed_toggle ? "Toggle" : "Hold",
                                  mid, baseline, selected, 0, label);
                } else {
                    int idx = row - 1;
                    int capturing = selected && p->state->capturing_binding;
                    const char *val = capturing
                                          ? "Press key or button..."
                                          : Host_ControlBindingValueName(p->state, idx);
                    int speed = idx == HOST_SPEED_BINDING;
                    PaintValueRow(p, speed ? "" : host_button_names[idx],
                                  val, mid, baseline, selected, capturing, label);
                    if (speed) {
                        PaintSpeedLabel(p, mid, baseline, selected, label);
                    }
                }
            } else if (page == OVERLAY_PAGE_MODS) {
                const char *names[]={"HD backgrounds","Background pack","Integrity check"};
                if(row==0)PaintValueRow(p,names[row],Host_HdValue(row),mid,baseline,selected,0,label);
                else {
                    DrawText(p->surface,OVERLAY_PAD*s,baseline,17*s,names[row],label,ALIGN_LEFT);
                    DrawText(p->surface,OverlayValueCentre()*s,baseline,17*s,Host_HdValue(row),selected?kAccent:kText,ALIGN_CENTRE);
                }
            } else if (page == OVERLAY_PAGE_DISCS) {
                char discLabel[32];
                snprintf(discLabel, sizeof(discLabel), "Disc %d", row + 1);
                const char *val = g_CdImageVolumes[row] ? "Mounted" : "Select image...";
                PaintValueRow(p, discLabel, val, mid, baseline, selected, 0, label);
            }
        } else {
            int quit = row == pureCount + 1;

            DrawText(p->surface, (float)OVERLAY_PAD * s, baseline, 17.0f * s,
                     quit ? "Quit game" : "Resume game", quit ? kDanger : label,
                     ALIGN_LEFT);
        }
    }

    if(page==OVERLAY_PAGE_MODS) {
        float x=OVERLAY_PAD*s,y=(OVERLAY_HEADER_H+5*OVERLAY_ROW_H)*s,w=(OVERLAY_PANEL_W-2*OVERLAY_PAD)*s;
        if(Host_HdBusy()) {
            char text[80];snprintf(text,sizeof(text),"Downloading / verifying: %d%%",Host_HdPercent());
            DrawText(p->surface,x,y,15*s,text,kText,ALIGN_LEFT);
            FillRoundRect(p->surface,x,y+18*s,w,10*s,5*s,WithAlpha(kText,.12f));
            FillRoundRect(p->surface,x,y+18*s,w*Host_HdPercent()/100.f,10*s,5*s,kAccent);
        } else {
            char text[96];snprintf(text,sizeof(text),"Installed version: %s",*host_hd.version?host_hd.version:host_hd.development?"local preview":"none");
            DrawText(p->surface,x,y,15*s,text,kMuted,ALIGN_LEFT);
        }
    }
    if (page == OVERLAY_PAGE_CONTROLS) {
        int scroll = Host_ClampControlsScroll(p->state->scroll);
        float trackTop = (OverlayRowTop(visibleSettings, 1) + 4.0f) * s;
        float trackH = (float)(OVERLAY_CONTROLS_VISIBLE_BINDINGS * OVERLAY_ROW_H - 8) * s;
        float trackX = (float)(OVERLAY_PANEL_W - 12) * s;
        float thumbH = trackH * ((float)OVERLAY_CONTROLS_VISIBLE_BINDINGS /
                                 (float)OVERLAY_CONTROLS_ITEM_COUNT);
        float thumbTop = trackTop + (trackH - thumbH) *
                         ((float)scroll / (float)OVERLAY_CONTROLS_MAX_SCROLL);
        FillRoundRect(p->surface, trackX, trackTop, 4.0f * s, trackH, 2.0f * s,
                      WithAlpha(kText, 0.10f));
        FillRoundRect(p->surface, trackX, thumbTop, 4.0f * s, thumbH, 2.0f * s,
                      WithAlpha(kAccent, 0.65f));
    }
}

static const char *OverlayRowHelp(const HostOverlayState *state) {
    if (state->status != NULL && state->status[0] != '\0') {
        return state->status;
    }
    if (state->capturing_binding) {
        return "Press a key or controller button to bind. Esc cancels.";
    }
    int pureCount = Host_SettingsPureRowCount(state->page);
    if (state->row == pureCount) {
        return "Return to the game.";
    }
    if (state->row == pureCount + 1) {
        return "Close the game.";
    }
    int declCount = 0;
    const OverlaySetting *settings = OverlayPageSettings(state->page, &declCount);
    if (settings != NULL && state->row >= 0 && state->row < declCount) {
        return settings[state->row].help;
    }
    if (state->page == OVERLAY_PAGE_CONTROLS) {
        if (state->row == 0) {
            return "Choose whether to inspect and rebind keyboard or controller inputs.";
        }
        if (state->row == HOST_ANALOG_RUN_ROW) {
            return "Left-stick deflection needed to run in the field. Lower values start running sooner.";
        }
        if (state->row == HOST_SPEED_BINDING + 1) {
            return host_options.speed_toggle
                       ? "Press to speed up, again for normal. Left/Right: 2x-4x. Enter/South binds; Del/West unbinds."
                       : "Hold to speed up. Left/Right: 2x-4x. Enter/South binds; Del/West unbinds.";
        }
        if (state->row == HOST_SPEED_MODE_ROW) {
            return "Hold: fast while the speed button is held. Toggle: each press switches it on or off.";
        }
        return "Press Enter or South button to rebind. Duplicate bindings swap automatically.";
    }
    if (state->page == OVERLAY_PAGE_DISCS) {
        return g_CdImageVolumes[state->row]
                   ? "This disc image is validated and mounted."
                   : "Press Enter to select a Parasite Eve (USA) CUE or BIN disc image.";
    }
    return "";
}

static void PaintFooter(const Painter *p, int visibleRowCount) {
    const float s = p->s;
    float footerTop = (float)(OverlayDesignHeight(visibleRowCount) - OVERLAY_FOOTER_H) * s;
    const char *help = OverlayRowHelp(p->state);

    FillRoundRect(p->surface, (float)OVERLAY_PAD * s, footerTop,
                  (float)(OVERLAY_PANEL_W - 2 * OVERLAY_PAD) * s, 1.0f, 0.0f,
                  WithAlpha(kText, 0.10f));
    DrawText(p->surface, (float)OVERLAY_PAD * s, footerTop + 28.0f * s,
             14.0f * s, help, kMuted, ALIGN_LEFT);
    DrawText(p->surface, (float)OVERLAY_PAD * s, footerTop + 54.0f * s,
             13.0f * s,
             p->state->page==OVERLAY_PAGE_MODS?"Enter / click activate   PgUp/PgDn page   Esc resume":"Up/Down select   Left/Right change   PgUp/PgDn page   Esc resume",
             WithAlpha(kMuted, 0.7f), ALIGN_LEFT);
}

static void OverlayPaint(OverlaySurface *surface, const HostOverlayState *state,
                         const OverlayLayout *layout) {
    Painter painter = {surface, state, layout->scale};
    int visibleRows = Host_SettingsVisibleRowCount(state->page);
    uint8_t *solid;

    memset(surface->pixels, 0, (size_t)surface->w * (size_t)surface->h * 4u);
    PaintFrame(&painter, (float)OVERLAY_PANEL_W * layout->scale,
               (float)OverlayDesignHeight(visibleRows) * layout->scale);
    PaintHeader(&painter);
    PaintRows(&painter);
    PaintFooter(&painter, visibleRows);
    solid = surface->pixels + (size_t)(surface->h - 1) * (size_t)surface->w * 4u;
    solid[3] = 255;
}

/* The skip-movie question, drawn like the settings panel. */
static void OverlayPaintPrompt(OverlaySurface *surface, const HostOverlayState *state,
                               const OverlayLayout *layout) {
    static const char *const kAnswers[PROMPT_ROWS] = {"Yes", "No"};
    Painter painter = {surface, state, layout->scale};
    const float s = layout->scale;
    float footerTop = (float)(OverlayPromptDesignHeight() - PROMPT_FOOTER_H) * s;
    uint8_t *solid;
    int row;

    memset(surface->pixels, 0, (size_t)surface->w * (size_t)surface->h * 4u);
    PaintFrame(&painter, (float)PROMPT_PANEL_W * s, (float)OverlayPromptDesignHeight() * s);
    DrawText(surface, (float)OVERLAY_PAD * s, 40.0f * s, 26.0f * s, "Skip cutscene",
             kText, ALIGN_LEFT);
    DrawText(surface, (float)(PROMPT_PANEL_W - OVERLAY_PAD) * s, 38.0f * s, 14.0f * s,
             "MOVIE PAUSED", kMuted, ALIGN_RIGHT);
    DrawText(surface, (float)OVERLAY_PAD * s, 80.0f * s, 17.0f * s,
             "Do you want to skip the cutscene?", WithAlpha(kText, 0.82f), ALIGN_LEFT);
    FillRoundRect(surface, (float)OVERLAY_PAD * s, (float)(PROMPT_HEADER_H - 12) * s,
                  (float)(PROMPT_PANEL_W - 2 * OVERLAY_PAD) * s, 1.0f, 0.0f,
                  WithAlpha(kText, 0.10f));

    for (row = 0; row < PROMPT_ROWS; row++) {
        OverlayRect rect = OverlayPromptRowRect(row);
        float top = rect.y * s;
        float mid = top + (float)OVERLAY_ROW_H * 0.5f * s;
        int selected = row == state->row;

        if (selected) {
            FillRoundRect(surface, rect.x * s, top + 3.0f * s, rect.w * s,
                          (float)(OVERLAY_ROW_H - 6) * s, 9.0f * s,
                          WithAlpha(kAccent, 0.16f));
            FillRoundRect(surface, rect.x * s + 4.0f * s, mid - 10.0f * s, 3.0f * s,
                          20.0f * s, 1.5f * s, kAccent);
        }
        DrawText(surface, (float)OVERLAY_PAD * s, mid + 6.0f * s, 17.0f * s,
                 kAnswers[row], selected ? kText : WithAlpha(kText, 0.82f), ALIGN_LEFT);
    }

    FillRoundRect(surface, (float)OVERLAY_PAD * s, footerTop,
                  (float)(PROMPT_PANEL_W - 2 * OVERLAY_PAD) * s, 1.0f, 0.0f,
                  WithAlpha(kText, 0.10f));
    DrawText(surface, (float)OVERLAY_PAD * s, footerTop + 36.0f * s, 13.0f * s,
             "Up/Down select   Enter confirm   Esc keep watching",
             WithAlpha(kMuted, 0.7f), ALIGN_LEFT);
    solid = surface->pixels + (size_t)(surface->h - 1) * (size_t)surface->w * 4u;
    solid[3] = 255;
}

/* ---- SDL_GPU in-window overlay pipeline ---- */

typedef struct OverlayGpuVertex {
    float x, y;
    float r, g, b, a;
    float u, v;
} OverlayGpuVertex;

static SDL_Window *s_overlayGpuWindow;
static SDL_GPUDevice *s_overlayGpuDevice;
static SDL_GPUGraphicsPipeline *s_overlayPipeline;
static SDL_GPUSampler *s_overlaySampler;
static SDL_GPUTexture *s_overlayTexture;
static SDL_GPUBuffer *s_overlayVertexBuffer;
static int s_overlayTextureW, s_overlayTextureH;
static int s_overlayWindowW, s_overlayWindowH;
static OverlaySurface s_overlaySurface;
static OverlayLayout s_overlayLayout;
static HostOverlayState s_overlayState;
static PsyzOverlayInitCB_SDL3GPU s_prevOverlayInit;
static PsyzOverlayRenderCB_SDL3GPU s_prevOverlayRender;
static PsyzOverlayDestroyCB s_prevOverlayDestroy;

static void Host_OverlayReleaseGpuResources(void) {
    if (s_overlayGpuDevice != NULL) {
        if (s_overlayTexture != NULL) {
            SDL_ReleaseGPUTexture(s_overlayGpuDevice, s_overlayTexture);
        }
        if (s_overlayVertexBuffer != NULL) {
            SDL_ReleaseGPUBuffer(s_overlayGpuDevice, s_overlayVertexBuffer);
        }
        if (s_overlaySampler != NULL) {
            SDL_ReleaseGPUSampler(s_overlayGpuDevice, s_overlaySampler);
        }
        if (s_overlayPipeline != NULL) {
            SDL_ReleaseGPUGraphicsPipeline(s_overlayGpuDevice, s_overlayPipeline);
        }
    }
    s_overlayTexture = NULL;
    s_overlayVertexBuffer = NULL;
    s_overlaySampler = NULL;
    s_overlayPipeline = NULL;
    s_overlayTextureW = s_overlayTextureH = 0;
    s_overlayWindowW = s_overlayWindowH = 0;
}

static void Host_OverlayDestroyGPU(void) {
    if (s_prevOverlayDestroy != NULL) {
        s_prevOverlayDestroy();
    }
    Host_OverlayReleaseGpuResources();
    OverlaySurfaceRelease(&s_overlaySurface);
    s_overlayGpuDevice = NULL;
    s_overlayGpuWindow = NULL;
}

static void Host_OverlayInitGPU(SDL_Window *window, SDL_GPUDevice *device) {
    if (s_prevOverlayInit != NULL) {
        s_prevOverlayInit(window, device);
    }
    if (s_overlayGpuDevice != NULL && s_overlayGpuDevice != device) {
        Host_OverlayReleaseGpuResources();
    }
    s_overlayGpuWindow = window;
    s_overlayGpuDevice = device;
}

static SDL_GPUShader *Host_CreateOverlayShader(
    SDL_GPUDevice *device,
    SDL_GPUShaderStage stage,
    const unsigned char *spirv,
    size_t spirvSize,
    const unsigned char *msl,
    size_t mslSize,
    const unsigned char *dxil,
    size_t dxilSize,
    Uint32 samplers,
    Uint32 uniforms
) {
    SDL_GPUShaderFormat formats = SDL_GetGPUShaderFormats(device);
    SDL_GPUShaderCreateInfo info;
    SDL_zero(info);
    info.stage = stage;
    info.num_samplers = samplers;
    info.num_uniform_buffers = uniforms;
    if (formats & SDL_GPU_SHADERFORMAT_SPIRV) {
        info.code = spirv;
        info.code_size = spirvSize;
        info.entrypoint = "main";
        info.format = SDL_GPU_SHADERFORMAT_SPIRV;
    } else if (formats & SDL_GPU_SHADERFORMAT_MSL) {
        info.code = msl;
        info.code_size = mslSize;
        info.entrypoint = "main0";
        info.format = SDL_GPU_SHADERFORMAT_MSL;
    } else if (formats & SDL_GPU_SHADERFORMAT_DXIL) {
        info.code = dxil;
        info.code_size = dxilSize;
        info.entrypoint = "main";
        info.format = SDL_GPU_SHADERFORMAT_DXIL;
    } else {
        return NULL;
    }
    return SDL_CreateGPUShader(device, &info);
}

static int Host_EnsureOverlayPipeline(SDL_GPUDevice *device, SDL_GPUTextureFormat format) {
    if (format == SDL_GPU_TEXTUREFORMAT_INVALID) {
        return 0;
    }
    if (s_overlayGpuDevice != NULL && s_overlayGpuDevice != device) {
        Host_OverlayReleaseGpuResources();
    }
    s_overlayGpuDevice = device;
    if (s_overlayPipeline != NULL && s_overlaySampler != NULL && s_overlayVertexBuffer != NULL) {
        return 1;
    }

    SDL_GPUShader *vertex = Host_CreateOverlayShader(
        device,
        SDL_GPU_SHADERSTAGE_VERTEX,
        tri_texture_vert_spv,
        sizeof(tri_texture_vert_spv),
        tri_texture_vert_msl,
        sizeof(tri_texture_vert_msl),
        tri_texture_vert_dxil,
        sizeof(tri_texture_vert_dxil),
        0,
        1
    );
    SDL_GPUShader *fragment = Host_CreateOverlayShader(
        device,
        SDL_GPU_SHADERSTAGE_FRAGMENT,
        texture_rgba_frag_spv,
        sizeof(texture_rgba_frag_spv),
        texture_rgba_frag_msl,
        sizeof(texture_rgba_frag_msl),
        texture_rgba_frag_dxil,
        sizeof(texture_rgba_frag_dxil),
        1,
        1
    );
    if (vertex == NULL || fragment == NULL) {
        if (vertex != NULL) {
            SDL_ReleaseGPUShader(device, vertex);
        }
        if (fragment != NULL) {
            SDL_ReleaseGPUShader(device, fragment);
        }
        return 0;
    }

    SDL_GPUVertexBufferDescription vbd;
    SDL_zero(vbd);
    vbd.slot = 0;
    vbd.pitch = sizeof(OverlayGpuVertex);
    vbd.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

    SDL_GPUVertexAttribute attribs[3];
    SDL_zero(attribs);
    attribs[0].location = 0;
    attribs[0].buffer_slot = 0;
    attribs[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    attribs[0].offset = 0;
    attribs[1].location = 1;
    attribs[1].buffer_slot = 0;
    attribs[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
    attribs[1].offset = 2 * sizeof(float);
    attribs[2].location = 2;
    attribs[2].buffer_slot = 0;
    attribs[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    attribs[2].offset = 6 * sizeof(float);

    SDL_GPUColorTargetDescription color;
    SDL_zero(color);
    color.format = format;
    color.blend_state.enable_blend = true;
    color.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    color.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    color.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    color.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    color.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    color.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;

    SDL_GPUGraphicsPipelineCreateInfo info;
    SDL_zero(info);
    info.vertex_shader = vertex;
    info.fragment_shader = fragment;
    info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    info.vertex_input_state.num_vertex_buffers = 1;
    info.vertex_input_state.vertex_buffer_descriptions = &vbd;
    info.vertex_input_state.num_vertex_attributes = 3;
    info.vertex_input_state.vertex_attributes = attribs;
    info.target_info.color_target_descriptions = &color;
    info.target_info.num_color_targets = 1;
    s_overlayPipeline = SDL_CreateGPUGraphicsPipeline(device, &info);
    SDL_ReleaseGPUShader(device, vertex);
    SDL_ReleaseGPUShader(device, fragment);

    SDL_GPUSamplerCreateInfo sampler;
    SDL_zero(sampler);
    sampler.min_filter = SDL_GPU_FILTER_NEAREST;
    sampler.mag_filter = SDL_GPU_FILTER_NEAREST;
    sampler.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    s_overlaySampler = SDL_CreateGPUSampler(device, &sampler);

    SDL_GPUBufferCreateInfo bci;
    SDL_zero(bci);
    bci.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
    bci.size = sizeof(OverlayGpuVertex) * 12;
    s_overlayVertexBuffer = SDL_CreateGPUBuffer(device, &bci);

    if (s_overlayPipeline == NULL || s_overlaySampler == NULL || s_overlayVertexBuffer == NULL) {
        Host_OverlayReleaseGpuResources();
        return 0;
    }
    return 1;
}

static void Host_WriteOverlayQuad(
    OverlayGpuVertex *out,
    float x0,
    float y0,
    float x1,
    float y1,
    float u0,
    float v0,
    float u1,
    float v1,
    float alpha
) {
    out[0] = (OverlayGpuVertex){x0, y0, 1.0f, 1.0f, 1.0f, alpha, u0, v0};
    out[1] = (OverlayGpuVertex){x1, y0, 1.0f, 1.0f, 1.0f, alpha, u1, v0};
    out[2] = (OverlayGpuVertex){x0, y1, 1.0f, 1.0f, 1.0f, alpha, u0, v1};
    out[3] = (OverlayGpuVertex){x0, y1, 1.0f, 1.0f, 1.0f, alpha, u0, v1};
    out[4] = (OverlayGpuVertex){x1, y0, 1.0f, 1.0f, 1.0f, alpha, u1, v0};
    out[5] = (OverlayGpuVertex){x1, y1, 1.0f, 1.0f, 1.0f, alpha, u1, v1};
}

static int Host_UploadOverlaySurfaceAndVertices(SDL_GPUDevice *device) {
    if (s_overlaySurface.w <= 0 || s_overlaySurface.h <= 0 || s_overlayWindowW <= 0 ||
        s_overlayWindowH <= 0 || s_overlayVertexBuffer == NULL) {
        return 0;
    }
    if (s_overlayTexture == NULL || s_overlayTextureW != s_overlaySurface.w ||
        s_overlayTextureH != s_overlaySurface.h) {
        SDL_GPUTextureCreateInfo info;
        if (s_overlayTexture != NULL) {
            SDL_ReleaseGPUTexture(device, s_overlayTexture);
        }
        SDL_zero(info);
        info.type = SDL_GPU_TEXTURETYPE_2D;
        info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        info.width = (Uint32)s_overlaySurface.w;
        info.height = (Uint32)s_overlaySurface.h;
        info.layer_count_or_depth = 1;
        info.num_levels = 1;
        s_overlayTexture = SDL_CreateGPUTexture(device, &info);
        if (s_overlayTexture == NULL) {
            s_overlayTextureW = 0;
            s_overlayTextureH = 0;
            return 0;
        }
        s_overlayTextureW = s_overlaySurface.w;
        s_overlayTextureH = s_overlaySurface.h;
    }

    OverlayGpuVertex verts[12];
    float w = (float)s_overlayWindowW;
    float h = (float)s_overlayWindowH;
    float solidU = 0.5f / (float)s_overlayTextureW;
    float solidV = ((float)s_overlayTextureH - 0.5f) / (float)s_overlayTextureH;
    Host_WriteOverlayQuad(verts, -1.0f, 1.0f, 1.0f, -1.0f, solidU, solidV, solidU, solidV, 0.62f);

    float x0 = (float)s_overlayLayout.x / w * 2.0f - 1.0f;
    float y0 = 1.0f - (float)s_overlayLayout.y / h * 2.0f;
    float x1 = (float)(s_overlayLayout.x + s_overlayLayout.w) / w * 2.0f - 1.0f;
    float y1 = 1.0f - (float)(s_overlayLayout.y + s_overlayLayout.h) / h * 2.0f;
    float panelV1 = (float)(s_overlayTextureH - 1) / (float)s_overlayTextureH;
    Host_WriteOverlayQuad(verts + 6, x0, y0, x1, y1, 0.0f, 0.0f, 1.0f, panelV1, 1.0f);

    Uint32 texBytes = (Uint32)((size_t)s_overlaySurface.w * (size_t)s_overlaySurface.h * 4u);
    Uint32 vtxBytes = (Uint32)sizeof(verts);
    SDL_GPUTransferBufferCreateInfo transferInfo;
    SDL_zero(transferInfo);
    transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transferInfo.size = texBytes + vtxBytes;
    SDL_GPUTransferBuffer *transfer = SDL_CreateGPUTransferBuffer(device, &transferInfo);
    if (transfer == NULL) {
        return 0;
    }
    uint8_t *mapped = (uint8_t *)SDL_MapGPUTransferBuffer(device, transfer, false);
    if (mapped == NULL) {
        SDL_ReleaseGPUTransferBuffer(device, transfer);
        return 0;
    }
    memcpy(mapped, s_overlaySurface.pixels, texBytes);
    memcpy(mapped + texBytes, verts, vtxBytes);
    SDL_UnmapGPUTransferBuffer(device, transfer);

    SDL_GPUCommandBuffer *command = SDL_AcquireGPUCommandBuffer(device);
    if (command == NULL) {
        SDL_ReleaseGPUTransferBuffer(device, transfer);
        return 0;
    }
    SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(command);

    SDL_GPUTextureTransferInfo texSrc;
    SDL_zero(texSrc);
    texSrc.transfer_buffer = transfer;
    texSrc.offset = 0;
    texSrc.pixels_per_row = (Uint32)s_overlaySurface.w;
    texSrc.rows_per_layer = (Uint32)s_overlaySurface.h;

    SDL_GPUTextureRegion texDst;
    SDL_zero(texDst);
    texDst.texture = s_overlayTexture;
    texDst.w = (Uint32)s_overlaySurface.w;
    texDst.h = (Uint32)s_overlaySurface.h;
    texDst.d = 1;
    SDL_UploadToGPUTexture(copy, &texSrc, &texDst, false);

    SDL_GPUTransferBufferLocation vtxSrc;
    SDL_zero(vtxSrc);
    vtxSrc.transfer_buffer = transfer;
    vtxSrc.offset = texBytes;

    SDL_GPUBufferRegion vtxDst;
    SDL_zero(vtxDst);
    vtxDst.buffer = s_overlayVertexBuffer;
    vtxDst.offset = 0;
    vtxDst.size = vtxBytes;
    SDL_UploadToGPUBuffer(copy, &vtxSrc, &vtxDst, false);

    SDL_EndGPUCopyPass(copy);
    SDL_SubmitGPUCommandBuffer(command);
    SDL_ReleaseGPUTransferBuffer(device, transfer);
    return 1;
}

static void Host_OverlayRefreshGPU(
    SDL_GPUDevice *device,
    SDL_Window *window,
    HostOverlayState *state
) {
    int w = 0, h = 0;
    OverlayLayout layout;
    SDL_GetWindowSizeInPixels(window, &w, &h);
    if (w <= 0 || h <= 0 || !OverlayFontReady()) {
        return;
    }
    OverlayComputeStateLayout(state, w, h, &layout);
    if (w != s_overlayWindowW || h != s_overlayWindowH || layout.w != s_overlayLayout.w ||
        layout.h != s_overlayLayout.h) {
        state->dirty = 1;
    }
    s_overlayWindowW = w;
    s_overlayWindowH = h;
    s_overlayLayout = layout;
    if (!state->dirty) {
        return;
    }
    if (!OverlaySurfaceResize(&s_overlaySurface, layout.w, layout.h)) {
        return;
    }
    if (state->prompt) {
        OverlayPaintPrompt(&s_overlaySurface, state, &layout);
    } else {
        OverlayPaint(&s_overlaySurface, state, &layout);
    }
    if (Host_EnsureOverlayPipeline(device, SDL_GetGPUSwapchainTextureFormat(device, window)) &&
        Host_UploadOverlaySurfaceAndVertices(device)) {
        state->dirty = 0;
    }
}

static void Host_OverlayRenderGPU(SDL_GPUCommandBuffer *command, SDL_GPURenderPass *pass) {
    if (s_prevOverlayRender != NULL) {
        s_prevOverlayRender(command, pass);
    }
    if (!s_overlayState.open || s_overlayPipeline == NULL || s_overlayTexture == NULL ||
        s_overlayVertexBuffer == NULL || s_overlayWindowW <= 0 || s_overlayWindowH <= 0) {
        return;
    }

    static const float kIdentityMvp[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
    static const float kColorScale[4] = {1.0f, 0.0f, 0.0f, 0.0f};

    SDL_GPUViewport viewport = {
        0.0f, 0.0f, (float)s_overlayWindowW, (float)s_overlayWindowH, 0.0f, 1.0f
    };
    SDL_Rect scissor = {0, 0, s_overlayWindowW, s_overlayWindowH};
    SDL_SetGPUViewport(pass, &viewport);
    SDL_SetGPUScissor(pass, &scissor);
    SDL_BindGPUGraphicsPipeline(pass, s_overlayPipeline);

    SDL_GPUTextureSamplerBinding samplerBind;
    SDL_zero(samplerBind);
    samplerBind.texture = s_overlayTexture;
    samplerBind.sampler = s_overlaySampler;
    SDL_BindGPUFragmentSamplers(pass, 0, &samplerBind, 1);

    SDL_GPUBufferBinding vtxBind;
    SDL_zero(vtxBind);
    vtxBind.buffer = s_overlayVertexBuffer;
    vtxBind.offset = 0;
    SDL_BindGPUVertexBuffers(pass, 0, &vtxBind, 1);

    SDL_PushGPUVertexUniformData(command, 0, kIdentityMvp, sizeof(kIdentityMvp));
    SDL_PushGPUFragmentUniformData(command, 0, kColorScale, sizeof(kColorScale));
    SDL_DrawGPUPrimitives(pass, 12, 1, 0, 0);
}

#endif
