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
#ifdef PE_PS5_GPU_NATIVE
    OVERLAY_PAGE_GRAPHICS=0,OVERLAY_PAGE_MODS=1,OVERLAY_PAGE_CHEATS=2,OVERLAY_PAGE_STATES=3,
    OVERLAY_PAGE_AUDIO=4,OVERLAY_PAGE_CONTROLS=5,OVERLAY_PAGE_DISCS=6,OVERLAY_PAGE_COUNT=7,
    OVERLAY_ACTION_ROWS=3,
#else
    OVERLAY_PAGE_GRAPHICS = 0,
    OVERLAY_PAGE_CONTROLS = 1,
    OVERLAY_PAGE_AUDIO = 2,
    OVERLAY_PAGE_DISCS = 3,
    OVERLAY_PAGE_MODS = 4,
    OVERLAY_PAGE_COUNT = 5,
    OVERLAY_ACTION_ROWS = 2,
#endif
    OVERLAY_VISIBLE_CONTROLS = 7,

#ifdef PE_PS5_GPU_NATIVE
    OVERLAY_PANEL_W = 840,
#else
    OVERLAY_PANEL_W = 600,
#endif
    OVERLAY_PAD = 32,
    OVERLAY_HEADER_H = 128,
    OVERLAY_ROW_H = 46,
    OVERLAY_ACTION_GAP = 14,
#ifdef PE_PS5_GPU_NATIVE
    OVERLAY_FOOTER_H = 94,
#else
    OVERLAY_FOOTER_H = 100,
#endif
#ifdef PE_PS5_GPU_NATIVE
    OVERLAY_VALUE_W = 320,
#else
    OVERLAY_VALUE_W = 250,
#endif
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
    PROMPT_FOOTER_H = 60,

    SCREENSHOT_PANEL_W = 600,
    SCREENSHOT_HEADER_H = 112,
    SCREENSHOT_ROW_TAG = 0,
    SCREENSHOT_ROW_NOTE = 1,
    SCREENSHOT_ROW_UPLOAD = 2,
    SCREENSHOT_ROW_SAVE = 3,
    SCREENSHOT_ROW_CANCEL = 4,
    SCREENSHOT_ROWS = 5,
    SCREENSHOT_OSK_ROWS = 4,
    SCREENSHOT_OSK_COLS = 10,
    SCREENSHOT_OSK_ROW_H = 38,
    SCREENSHOT_OSK_H = 164,
#ifdef PE_PS5_GPU_NATIVE
    SCREENSHOT_FOOTER_H = 68
#else
    SCREENSHOT_FOOTER_H = 94
#endif
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
#ifdef PE_PS5_GPU_NATIVE
    int installer;
#endif
    int page;
    int row;
    int scroll;
#ifdef PE_PS5_GPU_NATIVE
    int page_rows[OVERLAY_PAGE_COUNT],page_scrolls[OVERLAY_PAGE_COUNT];
#endif
    int stick_x, stick_y; /* Menu-only hysteresis, no gameplay mapping changes. */
    int bind_device;
    int capturing_binding;
    int quit;
    int dirty;
    const char *status;
    int prompt;     /* the skip-movie question instead of the settings panel */
    int skip;       /* the question's answer */
    int screenshot; /* the screenshot note & report modal */
    int save_shot;  /* 1 when confirmed to save (and optionally upload) */
    int tag_index;  /* preset issue tag index */
    int osk_open;   /* 1 when on-screen controller keyboard is open */
    int osk_row;    /* 0..3 */
    int osk_col;    /* 0..9 */
    char note[128]; /* user sentence saved alongside the screenshot */
    char bg_summary[128];
} HostOverlayState;

#ifdef PE_PS5_GPU_NATIVE
static const Color kPanelModern={0.052f,0.066f,0.064f,0.965f},kPanelOriginal={0.075f,0.082f,0.102f,0.96f};
#define kPanel (host_options.menu_style?kPanelModern:kPanelOriginal)
#else
static const Color kPanel = {0.075f, 0.082f, 0.102f, 0.96f};
#endif
static const Color kBorder = {1.0f, 1.0f, 1.0f, 0.10f};
#ifdef PE_PS5_GPU_NATIVE
static const Color kAccentModern={0.38f,0.72f,0.62f,1.0f},kAccentOriginal={0.27f,0.62f,1.0f,1.0f};
#define kAccent (host_options.menu_style?kAccentModern:kAccentOriginal)
#else
static const Color kAccent = {0.27f, 0.62f, 1.0f, 1.0f};
#endif
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

#if defined(PE_PS5) && !defined(PE_PS5_GPU_NATIVE)
static const char *const kScaleNames[]={"1x original","2x experimental","4x experimental","6x experimental","8x experimental"};
static const unsigned kCpuScales[]={1,2,4,6,8};
static int GetScale(void){for(int i=0;i<5;++i)if(host_options.scale==kCpuScales[i])return i;return 0;}
static void SetScale(int i){if(i<0 || i>=5)return;host_options.scale=kCpuScales[i];}
#else
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

#endif

ACCESSORS(Fullscreen, host_options.fullscreen != 0,
          host_options.fullscreen = i)

#if defined(PE_PS5) && !defined(PE_PS5_GPU_NATIVE)
static const char *const kAspectNames[] = {"Classic 4:3", "Square pixels", "Wide (experimental)"};
#else
static const char *const kAspectNames[] = {"Classic 4:3", "Square pixels", "Widescreen"};
#endif
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

#if defined(PE_PS5) && !defined(PE_PS5_GPU_NATIVE)
static const char *const kUnavailableNames[]={"Unavailable (CPU)"};
static int GetUnavailable(void){return 0;}
static void SetUnavailable(int i){(void)i;}
#endif
#ifdef PE_PS5_GPU_NATIVE
static const char *const kFrameRateNames[]={"30 Hz (original)","60 Hz requested"};
ACCESSORS(FrameInterpolation,host_options.frame_interpolation!=0,host_options.frame_interpolation=i)
static const char *const kFsrStatus[]={"Off","Bypassed","Active","Fallback"};
ACCESSORS(FsrStatus,ClampIndex(Psyz_VideoGetFsr1Status(),4),(void)i)
static const char *const kFsrSharpness[]={"Strong (0 stops)","Balanced (1 stop)","Soft (2 stops)"};
ACCESSORS(Fsr1,host_options.fsr1!=0,host_options.fsr1=i)
static const char *const kNativeBackgroundNames[]={"Original","HD pack requested"};
static const char *const kNativeModelNames[]={"Original","Duck images requested"};
static const char *const kMovieBars[]={"Original black","Blurred backdrop"};
ACCESSORS(MovieBlur,host_options.movie_blur!=0,host_options.movie_blur=i)
ACCESSORS(FsrSharpness,ClampIndex(host_options.fsr1_sharpness,3),host_options.fsr1_sharpness=i)
static const char *const kModelFillNames[]={"Original","Soft fill","World key fill"};
ACCESSORS(ModelFill,ClampIndex(host_options.model_fill_lighting,3),host_options.model_fill_lighting=i)
static const char *const kNativeOutputNames[]={"Always fullscreen"};
ACCESSORS(NativeOutput,0,(void)i)
ACCESSORS(GameplayFill,host_options.gameplay_fill!=0,host_options.gameplay_fill=i)
static const char *const kMovieCleanupNames[]={"Original","Gentle","Stronger","Block smoothing"};
ACCESSORS(MovieCleanup,ClampIndex(host_options.movie_cleanup,4),host_options.movie_cleanup=i)
ACCESSORS(ShowFps,!!host_options.show_fps,host_options.show_fps=i)
static const char *const kPlayerShadowNames[]={"Original","Projected mesh"};
ACCESSORS(PlayerShadow,ClampIndex(host_options.player_shadow,2),host_options.player_shadow=i)
ACCESSORS(SpriteFiltering,host_options.sprite_filtering!=0,host_options.sprite_filtering=i)
ACCESSORS(MovieStretch,host_options.movie_presentation==0,host_options.movie_presentation=i?0:1;host_options.movie_blur=!i)
static const char *const kMovieFrameNames[]={"Original + blurred sides","Fill screen (stretch)"};
ACCESSORS(MovieFrame,host_options.movie_presentation==0,host_options.movie_presentation=i?0:1;host_options.movie_blur=!i)
ACCESSORS(HdBackgrounds,host_options.hd_backgrounds!=0,if(i!=!!host_options.hd_backgrounds)Host_HdAction(0))
ACCESSORS(HdModels,host_options.hd_models!=0,host_options.hd_models=i)
static const char *const kParticleModes[]={"Original","Hybrid","New geometry"};
static const char *const kDialogueNames[]={"Original","Modern (Plex)"};
ACCESSORS(DialogueText,host_options.dialogue_text!=0,host_options.dialogue_text=i)
static const char *const kTriangleFontNames[]={"Original","Modern (Plex)"};
ACCESSORS(TriangleFont,host_options.triangle_menu_font!=0,host_options.triangle_menu_font=i)
static const char *const kTriangleFontStatus[]={"Original","Prepared for menu","Original fallback / waiting"};
ACCESSORS(TriangleFontStatus,!host_options.triangle_menu_font?0:PeMenuFont_GetEffective()?1:2,(void)i)
static const char *const kDialogueStatus[]={"Original","Prepared for dialogue","Original fallback / waiting"};
ACCESSORS(DialogueStatus,!host_options.dialogue_text?0:PeDialogue_GetEffective()?1:2,(void)i)
static const char *const kParticleLights[]={"Off","On"};
ACCESSORS(ParticleLights,host_options.particle_lighting!=0,host_options.particle_lighting=i)
ACCESSORS(ParticleMode,ClampIndex(host_options.battle_debris_3d,3),host_options.battle_debris_3d=i)
static const char *const kMenuStyles[]={"Original overlay","Modern"};
static const char *const kMenuTextSizes[]={"Original","Large"};
static const char *const kMenuPointers[]={"Original bar","Chevron","Both"};
ACCESSORS(MenuStyle,ClampIndex(host_options.menu_style,2),host_options.menu_style=i)
ACCESSORS(MenuTextSize,ClampIndex(host_options.menu_text_size,2),host_options.menu_text_size=i)
ACCESSORS(MenuPointer,ClampIndex(host_options.menu_pointer,3),host_options.menu_pointer=i)
static const OverlaySetting kSystem[]={
 SETTING("Menu appearance","Previous native overlay or charcoal/teal styling. Text size and pointer are independent; the game's Triangle menu is unchanged.",kMenuStyles,MenuStyle),
 SETTING("Menu text","Uses the existing licensed Plex font. Original restores the previous native row size; Large improves TV readability.",kMenuTextSizes,MenuTextSize),
 SETTING("Focus pointer","Select the previous native focus bar, a restrained chevron, or both. No game art is replaced.",kMenuPointers,MenuPointer),
};
ACCESSORS(DynamicLighting,host_options.dynamic_lighting!=0,host_options.dynamic_lighting=i)
ACCESSORS(EffectGeometryCorrection,host_options.effect_geometry_correction!=0,host_options.effect_geometry_correction=i)
#else
static const char *const kFrameRateNames[]={"30 Hz (original)","60 Hz (interpolated)"};
ACCESSORS(FrameInterpolation,host_options.frame_interpolation!=0,
          host_options.frame_interpolation=i)

ACCESSORS(DynamicLighting, host_options.dynamic_lighting != 0,
          host_options.dynamic_lighting = i)

ACCESSORS(EffectGeometryCorrection, host_options.effect_geometry_correction != 0,
          host_options.effect_geometry_correction = i)
#endif
static const OverlaySetting kGraphics[] = {
#if defined(PE_PS5) && !defined(PE_PS5_GPU_NATIVE)
    SETTING("CPU SCALE TEST v2", "Experimental; rejected frames use original 1x. Ordered field HD at every scale. Unsupported frames use 1x; see logs.", kScaleNames, Scale),
#else
    SETTING("Picture detail", "Higher values increase image detail and GPU load.", kScaleNames, Scale),
#endif
#ifdef PE_PS5_GPU_NATIVE
    SETTING("Fill the TV", "On stretches gameplay to fill the TV. Off preserves the selected scene aspect. Genuine widescreen and movie stretch are separate.", kSwitchNames, GameplayFill),
#elif defined(PE_PS5)
    SETTING("Fullscreen", "This GPU option is unavailable on the CPU renderer.", kUnavailableNames, Unavailable),
#else
    SETTING("Fullscreen", "Switch between windowed and fullscreen display.", kSwitchNames, Fullscreen),
#endif
#ifdef PE_PS5_GPU_NATIVE
    SETTING("HD backgrounds","Native background replacements from your installed pack. Independent of character textures and filtering; original assets remain the fallback.",kNativeBackgroundNames,HdBackgrounds),
    SETTING("HD character textures","Matched player and supported nonplayer images on original geometry. Unregistered, unsupported or ambiguous matches remain original; independent of backgrounds and filtering.",kNativeModelNames,HdModels),
    SETTING("Movie framing","Original proportions with blurred sides is the default. Fill screen stretches movies for comparison; gameplay framing is separate.",kMovieFrameNames,MovieFrame),
    SETTING("Dialogue font","Optional Modern Plex for supported Latin dialogue. Original spacing/layout retained; requires 2x or above. Other game menus and unsupported glyphs remain original.",kDialogueNames,DialogueText),
    SETTING("Dialogue font status","Updates as dialogue is drawn. Unsupported letters keep the original font; if the new font is not ready, the original remains.",kDialogueStatus,DialogueStatus),
    SETTING("Triangle menu font","Plex lettering for supported Latin text in the game's Triangle menu. Keeps spacing and controls; modern letters use a grayscale base with the game's tint colours. Settings appearance is separate.",kTriangleFontNames,TriangleFont),
    SETTING("Triangle font status","Only supported letters are replaced. The original font is used if the new font is not ready. Change the Settings menu's look in System.",kTriangleFontStatus,TriangleFontStatus),
    SETTING("Show FPS","Temporary display frame counter. Counts submitted TV frames, not game logic updates.",kSwitchNames,ShowFps),
    SETTING("Player shadow","Experimental player-outline projection onto supported flat floors using a fixed light direction. Other actors, unsupported scenes and source/resource failures keep original shadows. No wall or stair occlusion.",kPlayerShadowNames,PlayerShadow),
    SETTING("Particle lights","Supported sparks/flame can light nearby actors. Original effects disables their contribution even when this switch is On. No shadows or ray tracing.",kParticleLights,ParticleLights),
    SETTING("Extra particle geometry","Original disables extra meshes. Hybrid/New geometry affect only supported families; no gameplay or simulation speed changes.",kParticleModes,ParticleMode),

    SETTING("Movie cleanup", "Optional smoothing for decoded movies. Block smoothing reduces compression seams but can soften detail; no added detail or frames. Original is unfiltered; failures keep it.", kMovieCleanupNames, MovieCleanup),
    SETTING("FSR1 spatial upscale", "Optional EASU + RCAS before UI. Original fallback on errors or downscale; not frame generation.", kSwitchNames, Fsr1),
    SETTING("FSR1 status", "Last frame: active, bypassed for downscaling, or original fallback after an error. Read only.", kFsrStatus, FsrStatus),
    SETTING("FSR1 sharpness", "RCAS strength; only active when FSR1 succeeds. Lower stops sharpen more.", kFsrSharpness, FsrSharpness),
    SETTING("Model fill lighting", "Colour-only fill for lit 3D models and props as they update. World key uses original normals. No reflections or geometry smoothing.", kModelFillNames, ModelFill),
    SETTING("Gameplay display", "Requests smoother presentation. Game timing stays original. Extra GPU memory; allocation failure keeps original presentation while retaining your request. The FPS counter reports actual submitted frames.", kFrameRateNames, FrameInterpolation),
    SETTING("Dynamic lighting", "Upstream smooth directional lighting on characters and monsters. Separate from experimental model fill.", kSwitchNames, DynamicLighting),
    SETTING("Effect geometry", "Upstream subpixel precision for original spell and particle geometry; does not replace sprites with models.", kSwitchNames, EffectGeometryCorrection),
#endif
#ifdef PE_PS5_GPU_NATIVE
    SETTING("Scene framing", "Classic 4:3 or square pixels preserve the original scene shape when Force wide gameplay is Off. Widescreen requests available extra room imagery; no missing side art is invented.", kAspectNames, Aspect),
#else
    SETTING("Aspect ratio", "Classic 4:3 matches the PlayStation. Wide draws extra room geometry, with bars at room edges. Native0.8 HD side art covers limited rooms. Unsupported frames use classic output.", kAspectNames, Aspect),
#endif
    SETTING("Dithering", "PlayStation 15-bit color dithering on shaded polygons.",
            kDitherNames, Dither),
#if defined(PE_PS5) && !defined(PE_PS5_GPU_NATIVE)
    SETTING("Geometry correction", "This GPU option is unavailable on the CPU renderer.", kUnavailableNames, Unavailable),
    SETTING("Antialiasing", "This GPU option is unavailable on the CPU renderer.", kUnavailableNames, Unavailable),
    SETTING("Model textures", "This GPU option is unavailable on the CPU renderer.", kUnavailableNames, Unavailable),
#elif defined(PE_PS5_GPU_NATIVE)
    SETTING("Antialiasing", "Smoother edges: twice each axis, four samples per pixel. Uses more GPU time and memory.", kAntialiasNames, Antialiasing),
    SETTING("Model filtering", "Bilinear filtering of the original 3D texture; separate from HD player replacements.", kFilterNames, ModelFiltering),
#else
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
#endif
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


#ifdef PE_PS5_GPU_NATIVE
static const char *const kPresentationNames[]={"Classic","Modern"};
static const OverlaySetting kExperience[]={SETTING("Presentation","Modern remembers your custom visual choices. Classic uses original presentation with required rendering corrections. Gameplay mods, cheats, controls, audio and saves are unaffected.",kPresentationNames,Presentation)};
#endif

static const char *const kPageNames[OVERLAY_PAGE_COUNT] = {
#ifdef PE_PS5_GPU_NATIVE
    "Experience", "Mods", "Cheats", "Save states", "Advanced", "Controls", "System"
#else
    "Graphics", "Controls", "Audio", "Discs", "Mods"
#endif
};

static const char *OverlayPageName(int page) {
    return page >= 0 && page < OVERLAY_PAGE_COUNT ? kPageNames[page] : "";
}

static const OverlaySetting *OverlayPageSettings(int page, int *count) {
#ifdef PE_PS5_GPU_NATIVE
    if(page==OVERLAY_PAGE_GRAPHICS){*count=COUNT_OF(kExperience);return kExperience;}
    if(page==OVERLAY_PAGE_AUDIO){*count=COUNT_OF(kGraphics);return kGraphics;}
    if(page==OVERLAY_PAGE_DISCS){*count=COUNT_OF(kSystem);return kSystem;}
#endif
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
#ifdef PE_PS5_GPU_NATIVE
    if(page==OVERLAY_PAGE_GRAPHICS)return 2;
    if(page==OVERLAY_PAGE_AUDIO)return COUNT_OF(kGraphics);
    if(page==OVERLAY_PAGE_MODS||page==OVERLAY_PAGE_CHEATS||page==OVERLAY_PAGE_STATES||page==OVERLAY_PAGE_DISCS)return Host_ModernRowCount(page);
#endif
    if (page == OVERLAY_PAGE_GRAPHICS) return COUNT_OF(kGraphics);
    if (page == OVERLAY_PAGE_CONTROLS) return 3 + HOST_BINDING_COUNT;
    if (page == OVERLAY_PAGE_AUDIO) return COUNT_OF(kAudio);
    if (page == OVERLAY_PAGE_MODS) {
#ifdef PE_PS5_GPU_NATIVE
#ifdef PE_MODS
        return 6;
#else
        return 4;
#endif
#elif defined(PE_MODS) && !defined(PE_PS5)
        return 4;
#else
        return 3;
#endif
    }
#ifndef PE_PS5
    if (page == OVERLAY_PAGE_DISCS) return 3;
#endif
    return 2;
}

static int Host_SettingsTotalRowCount(int page) {
    return Host_SettingsPureRowCount(page) + OVERLAY_ACTION_ROWS;
}

enum {
    OVERLAY_CONTROLS_ITEM_COUNT = HOST_BINDING_COUNT + 2,
#ifdef PE_PS5_GPU_NATIVE
    OVERLAY_CONTROLS_VISIBLE_BINDINGS = OVERLAY_VISIBLE_CONTROLS - 1,
#else
    /* The window is as tall as the longest page: bindings fill it instead of leaving a gap. */
    OVERLAY_CONTROLS_VISIBLE_BINDINGS =
        (COUNT_OF(kGraphics) > OVERLAY_VISIBLE_CONTROLS ? COUNT_OF(kGraphics) : OVERLAY_VISIBLE_CONTROLS) - 1,
#endif
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
    if(state->page!=OVERLAY_PAGE_CONTROLS){
        int capacity=Host_SettingsVisibleSettingCount(state->page);
        int maximum=Host_SettingsPureRowCount(state->page)-capacity;if(maximum<0)maximum=0;
        if(state->scroll<0)state->scroll=0;
        if(state->scroll>maximum)state->scroll=maximum;
        if(state->row<Host_SettingsPureRowCount(state->page)){
            if(state->row<state->scroll)state->scroll=state->row;
            if(state->row>=state->scroll+capacity)state->scroll=state->row-capacity+1;
        }
        return;
    }
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

static int OverlayScreenshotDesignHeight(const HostOverlayState *state) {
    return SCREENSHOT_HEADER_H + SCREENSHOT_ROWS * OVERLAY_ROW_H +
           (state->osk_open ? SCREENSHOT_OSK_H : 0) + OVERLAY_ACTION_GAP +
           SCREENSHOT_FOOTER_H;
}

static OverlayRect OverlayScreenshotRowRect(const HostOverlayState *state, int row) {
    float top = (float)(SCREENSHOT_HEADER_H + row * OVERLAY_ROW_H);
    if (state->osk_open && row > SCREENSHOT_ROW_NOTE) {
        top += (float)SCREENSHOT_OSK_H;
    }
    if (row >= SCREENSHOT_ROW_SAVE) {
        top += (float)OVERLAY_ACTION_GAP;
    }
    return (OverlayRect){(float)(OVERLAY_PAD / 2), top,
                         (float)(SCREENSHOT_PANEL_W - OVERLAY_PAD),
                         (float)OVERLAY_ROW_H};
}

static OverlayRect OverlayScreenshotOskKeyRect(int oskRow, int oskCol) {
    float pad = (float)OVERLAY_PAD;
    float gridW = (float)SCREENSHOT_PANEL_W - 2.0f * pad;
    float cellW = gridW / (float)SCREENSHOT_OSK_COLS;
    float top = (float)(SCREENSHOT_HEADER_H + 2 * OVERLAY_ROW_H + 6) +
                (float)oskRow * (float)SCREENSHOT_OSK_ROW_H;
    return (OverlayRect){pad + (float)oskCol * cellW + 2.0f, top,
                         cellW - 4.0f, (float)(SCREENSHOT_OSK_ROW_H - 4)};
}

static void OverlayComputeStateLayout(const HostOverlayState *state, int windowW,
                                      int windowH, OverlayLayout *layout) {
#ifdef PE_PS5_GPU_NATIVE
    if(state->installer){OverlayFitLayout(600,650,windowW,windowH,layout);return;}
#endif
    if (state->prompt) {
        OverlayFitLayout(PROMPT_PANEL_W, OverlayPromptDesignHeight(), windowW, windowH,
                         layout);
    } else if (state->screenshot) {
        OverlayFitLayout(SCREENSHOT_PANEL_W, OverlayScreenshotDesignHeight(state), windowW,
                         windowH, layout);
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
    if(page!=OVERLAY_PAGE_CONTROLS){
        int maximum=pureCount-visibleSettings;if(maximum<0)maximum=0;
        int scroll=state->scroll<0?0:state->scroll>maximum?maximum:state->scroll;
        return row>=scroll&&row<scroll+visibleSettings?row-scroll:-1;
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

/* ---- the refined look: deep navy window, silver double frame, red ----
 * The PC menu wears it; the PS5 native menu keeps its own Original/Modern menu styles. */

#ifdef PE_PS5_GPU_NATIVE
#define OVERLAY_REFINED_LOOK 0
#else
#define OVERLAY_REFINED_LOOK 1
#endif

static const Color kLookRed = {0.90f, 0.14f, 0.15f, 1.0f};
static const Color kLookSteel = {0.42f, 0.58f, 0.80f, 1.0f};
static const Color kLookSilver = {0.80f, 0.86f, 0.95f, 1.0f};
static const Color kLookText = {0.92f, 0.94f, 0.98f, 1.0f};
static const Color kLookDim = {0.55f, 0.62f, 0.74f, 1.0f};

/* Where row labels start: right of the red cursor in the refined look. */
static float OverlayLabelX(void) { return (float)(OVERLAY_PAD + (OVERLAY_REFINED_LOOK ? 16 : 0)); }

static int LookThick(float s) { return (int)(2.0f * s) > 1 ? (int)(2.0f * s) : 2; }

/* Whole pixels, so stacked strips meet without seams. */
static void LookRect(OverlaySurface *s, int x, int y, int w, int h, Color c) {
    int ix, iy;
    for (iy = y; iy < y + h; iy++)
        for (ix = x; ix < x + w; ix++) BlendPixel(s, ix, iy, c, 1.0f);
}

static Color LookMix(Color a, Color b, float t) {
    Color c = {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
    return c;
}

static void LookVGradient(OverlaySurface *s, int x, int y, int w, int h, Color top, Color bottom) {
    int i;
    for (i = 0; i < h; i++) LookRect(s, x, y + i, w, 1, LookMix(top, bottom, h > 1 ? (float)i / (float)(h - 1) : 0.0f));
}

static void LookHGradient(OverlaySurface *s, int x, int y, int w, int h, Color left, Color right) {
    int i;
    for (i = 0; i < w; i++) LookRect(s, x + i, y, 1, h, LookMix(left, right, w > 1 ? (float)i / (float)(w - 1) : 0.0f));
}

static void LookFrame(OverlaySurface *s, int x, int y, int w, int h, Color c) {
    LookRect(s, x, y, w, 1, c);
    LookRect(s, x, y + h - 1, w, 1, c);
    LookRect(s, x, y + 1, 1, h - 2, c);
    LookRect(s, x + w - 1, y + 1, 1, h - 2, c);
}

/* Bright L-shaped brackets on a box's four corners. */
static void LookCorners(OverlaySurface *s, int x, int y, int w, int h, int len, int t, Color c) {
    LookRect(s, x, y, len, t, c);                   LookRect(s, x, y, t, len, c);
    LookRect(s, x + w - len, y, len, t, c);         LookRect(s, x + w - t, y, t, len, c);
    LookRect(s, x, y + h - t, len, t, c);           LookRect(s, x, y + h - len, t, len, c);
    LookRect(s, x + w - len, y + h - t, len, t, c); LookRect(s, x + w - t, y + h - len, t, len, c);
}

/* A solid triangle `size` tall, its point at (x, y): dir 1 points right, -1 left. */
static void LookArrow(OverlaySurface *s, float x, float y, float size, int dir, Color c) {
    const int half = (int)(size * 0.5f + 0.5f), px = (int)(x + 0.5f), py = (int)(y + 0.5f);
    int i;
    for (i = 0; i < half; i++) {
        const int col = dir > 0 ? px - 1 - i : px + i;
        LookRect(s, col, py - i - 1, 1, 2 * i + 2, c);
    }
}

/* Letter-spaced text (`track` pixels between letters). */
static float SpacedWidth(const char *text, float em, float track) {
    const size_t n = strlen(text);
    return TextWidth(text, em) + (n > 1 ? (float)(n - 1) * track : 0.0f);
}

static void DrawSpaced(OverlaySurface *s, float x, float baseline, float em, float track, const char *text, Color c,
                       TextAlign align) {
    char one[2] = {0, 0};
    if (align != ALIGN_LEFT) x -= SpacedWidth(text, em, track) * (align == ALIGN_CENTRE ? 0.5f : 1.0f);
    for (; *text; text++) {
        one[0] = *text;
        DrawText(s, x, baseline, em, one, c, ALIGN_LEFT);
        x += TextWidth(one, em) + track;
    }
}

static void UpperCopy(char *out, size_t size, const char *in) {
    size_t i;
    for (i = 0; in[i] && i + 1 < size; i++) out[i] = (char)(in[i] >= 'a' && in[i] <= 'z' ? in[i] - 32 : in[i]);
    out[i] = '\0';
}

/* Word-wrap `text` into up to `lines` lines of at most `width` pixels; the last line takes the rest. */
static void DrawWrapped(OverlaySurface *s, float x, float baseline, float step, float width, float em, int lines,
                        const char *text, Color c) {
    char line[320];
    int n;
    for (n = 0; n < lines && text && *text; n++) {
        size_t fit = 0, i = 0;
        if (n < lines - 1)
            for (;;) {
                size_t next = i;
                while (text[next] && text[next] != ' ') next++;
                if (next >= sizeof line) break;
                memcpy(line, text, next);
                line[next] = '\0';
                if (fit && TextWidth(line, em) > width) break;
                fit = next;
                if (!text[next]) break;
                i = next + 1;
            }
        else
            fit = strlen(text);
        if (fit >= sizeof line) fit = sizeof line - 1;
        if (!fit) fit = strlen(text) < sizeof line - 1 ? strlen(text) : sizeof line - 1;
        memcpy(line, text, fit);
        line[fit] = '\0';
        DrawText(s, x, baseline + step * (float)n, em, line, c, ALIGN_LEFT);
        text += fit;
        while (*text == ' ') text++;
    }
}

/* ---- the panel (rage-racer-pc Painter) ---- */

typedef struct Painter {
    OverlaySurface *surface;
    const HostOverlayState *state;
    float s; /* design units to pixels */
} Painter;

/* "PARASITE EVE" in red over the title in big spaced capitals; a red rule fading out at ruleY. */
static void LookTitle(const Painter *p, float panelW, float ruleY, const char *title, const char *corner) {
    const float s = p->s;
    const int x = (int)((float)OVERLAY_PAD * s), w = (int)((panelW - 2.0f * OVERLAY_PAD) * s);

    DrawSpaced(p->surface, (float)OVERLAY_PAD * s, 26.0f * s, 11.0f * s, 4.0f * s, "PARASITE EVE", kLookRed, ALIGN_LEFT);
    DrawSpaced(p->surface, (panelW - (float)OVERLAY_PAD) * s, 26.0f * s, 11.0f * s, 3.0f * s, corner,
               WithAlpha(kLookDim, 0.85f), ALIGN_RIGHT);
    DrawSpaced(p->surface, (float)OVERLAY_PAD * s, 55.0f * s, 24.0f * s, 5.0f * s, title, kLookText, ALIGN_LEFT);
    LookHGradient(p->surface, x, (int)(ruleY * s), w, LookThick(s), kLookRed, WithAlpha(kLookRed, 0.0f));
    LookRect(p->surface, x, (int)((ruleY + 6.0f) * s), w, 1, WithAlpha(kLookSilver, 0.08f));
}

/* The selected row: a steel band fading right, hairlines, the red cursor. Others: a faint rule above. */
static void LookRowBand(const Painter *p, float panelW, float top, float mid, int selected, int first) {
    const float s = p->s;
    const int bx = (int)((float)(OVERLAY_PAD / 2) * s), by = (int)(top + 4.0f * s);
    const int bw = (int)((panelW - (float)OVERLAY_PAD) * s), bh = (int)((float)(OVERLAY_ROW_H - 8) * s);

    if (selected) {
        LookHGradient(p->surface, bx, by, bw, bh, WithAlpha(kLookSteel, 0.34f), WithAlpha(kLookSteel, 0.0f));
        LookHGradient(p->surface, bx, by, bw, 1, WithAlpha(kLookSilver, 0.55f), WithAlpha(kLookSilver, 0.0f));
        LookHGradient(p->surface, bx, by + bh - 1, bw, 1, WithAlpha(kLookSilver, 0.40f), WithAlpha(kLookSilver, 0.0f));
        LookArrow(p->surface, (float)(OVERLAY_PAD + 10) * s, mid, 12.0f * s, 1, kLookRed);
    } else if (!first) {
        LookHGradient(p->surface, (int)((float)(OVERLAY_PAD + 16) * s), (int)top,
                      (int)((panelW - 2.0f * OVERLAY_PAD - 16.0f) * s), 1, WithAlpha(kLookSilver, 0.07f),
                      WithAlpha(kLookSilver, 0.0f));
    }
}

/* A scroll bar: a hairline track, a red thumb. */
static void LookScrollBar(const Painter *p, float x, float trackTop, float trackH, float thumbTop, float thumbH) {
    const float s = p->s;
    LookRect(p->surface, (int)(x + 1.0f * s), (int)trackTop, 1, (int)trackH, WithAlpha(kLookSilver, 0.15f));
    LookRect(p->surface, (int)x, (int)thumbTop, (int)(3.0f * s), (int)thumbH, kLookRed);
}

/* The help box, as the game frames its own: the description, then the keys in small capitals. */
static void LookHelpBox(const Painter *p, float panelW, float boxTop, float boxH, const char *help, const char *keys) {
    const float s = p->s;
    const int bx = (int)((float)(OVERLAY_PAD / 2) * s), by = (int)(boxTop * s);
    const int bw = (int)((panelW - (float)OVERLAY_PAD) * s), bh = (int)(boxH * s);
    char upper[160];

    LookVGradient(p->surface, bx, by, bw, bh, (Color){0.02f, 0.03f, 0.06f, 0.85f}, (Color){0.0f, 0.0f, 0.0f, 0.85f});
    LookFrame(p->surface, bx, by, bw, bh, WithAlpha(kLookSilver, 0.30f));
    LookCorners(p->surface, bx, by, bw, bh, (int)(10.0f * s), 1, WithAlpha(kLookSilver, 0.85f));
    DrawWrapped(p->surface, (float)OVERLAY_PAD * s, (boxTop + 23.0f) * s, 19.0f * s, (panelW - 2.0f * OVERLAY_PAD) * s,
                14.0f * s, 2, help, kLookText);
    UpperCopy(upper, sizeof upper, keys);
    DrawSpaced(p->surface, (float)OVERLAY_PAD * s, (boxTop + boxH - 12.0f) * s, 10.0f * s, 1.6f * s, upper,
               WithAlpha(kLookDim, 0.85f), ALIGN_LEFT);
}

static void PaintFrame(const Painter *p, float width, float height) {
    const float s = p->s;

    if (OVERLAY_REFINED_LOOK) {   /* navy to black, faint scanlines, a silver double frame with bright corners */
        const int w = (int)ceilf(width), h = (int)ceilf(height), inset = (int)(5.0f * s);
        int i;
        LookVGradient(p->surface, 0, 0, w, h, (Color){0.055f, 0.075f, 0.140f, 0.97f}, (Color){0.010f, 0.014f, 0.032f, 0.97f});
        for (i = 0; i < h; i += 3) LookRect(p->surface, 1, i, w - 2, 1, WithAlpha(kLookSilver, 0.022f));
        LookFrame(p->surface, 0, 0, w, h, WithAlpha(kLookSilver, 0.50f));
        LookFrame(p->surface, inset, inset, w - 2 * inset, h - 2 * inset, WithAlpha(kLookSilver, 0.13f));
        LookCorners(p->surface, 0, 0, w, h, (int)(22.0f * s), LookThick(s), WithAlpha(kLookSilver, 0.95f));
        return;
    }

    FillRoundRect(p->surface, 0.0f, 0.0f, width, height, 16.0f * s, kBorder);
    FillRoundRect(p->surface, 1.0f, 1.0f, width - 2.0f, height - 2.0f,
                  15.0f * s, kPanel);
}

static void PaintHeader(const Painter *p) {
    const float s = p->s;
    int page;

    if (OVERLAY_REFINED_LOOK) {
        LookTitle(p, (float)OVERLAY_PANEL_W, (float)(OVERLAY_HEADER_H - 20), "SETTINGS", "GAME PAUSED");
        for (page = 0; page < OVERLAY_PAGE_COUNT; page++) {
            OverlayRect tab = OverlayTabRect(page);
            int current = page == p->state->page;
            char name[32];

            UpperCopy(name, sizeof name, OverlayPageName(page));
            if (current) {   /* a steel glow over a red bar under the page you are on */
                const int tx = (int)((tab.x + 3.0f) * s), tw = (int)((tab.w - 6.0f) * s);
                const int ty = (int)(tab.y * s), th = (int)(tab.h * s);
                LookVGradient(p->surface, tx, ty, tw, th, WithAlpha(kLookSteel, 0.0f), WithAlpha(kLookSteel, 0.30f));
                LookRect(p->surface, tx, ty + th - LookThick(s), tw, LookThick(s), kLookRed);
            }
            DrawSpaced(p->surface, (tab.x + tab.w * 0.5f) * s, (tab.y + tab.h * 0.5f + 5.0f) * s, 12.0f * s, 2.0f * s,
                       name, current ? kLookText : WithAlpha(kLookDim, 0.9f), ALIGN_CENTRE);
        }
        return;
    }
#ifdef PE_PS5_GPU_NATIVE
    if(host_options.menu_style)DrawText(p->surface,OVERLAY_PAD*s,14*s,11*s,"PARASITE EVE",kAccent,ALIGN_LEFT);
    DrawText(p->surface,OVERLAY_PAD*s,(host_options.menu_style?44:40)*s,(host_options.menu_style?28:26)*s,Host_RegistryTitle(p->state->page),kText,ALIGN_LEFT);
#else
    DrawText(p->surface, (float)OVERLAY_PAD * s, 40.0f * s, 26.0f * s,"Settings", kText, ALIGN_LEFT);
#endif
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

#ifdef PE_PS5_GPU_NATIVE
static const char *FitRowText(const char*text,float em,float width,char out[1024]){
 SDL_strlcpy(out,text?text:"",1024);size_t n=strlen(out);if(TextWidth(out,em)<=width)return out;
 while(n){do{--n;}while(n&&((unsigned char)out[n]&0xc0)==0x80);out[n]=0;if(n+3<1024){strcat(out,"...");if(TextWidth(out,em)<=width)return out;out[n]=0;}}
 out[0]=0;return out;
}
#endif

static void PaintValueRow(const Painter *p, const char *labelText,
                          const char *valueText, float mid, float baseline,
                          int selected, int highlightValue, Color label) {
    const float s = p->s;
    float centre = OverlayValueCentre() * s;
    float reach = ((float)OVERLAY_VALUE_W * 0.5f - 14.0f) * s;
    Color chevron = selected ? kAccent : WithAlpha(kMuted, 0.75f);

    if (OVERLAY_REFINED_LOOK) {   /* the label, a framed field for the value, red arrows on the selected row */
        const float arrows = ((float)OVERLAY_VALUE_W * 0.5f - 20.0f) * s;
        const int fx = (int)(centre - ((float)OVERLAY_VALUE_W * 0.5f - 6.0f) * s);
        const int fy = (int)(mid - (float)(OVERLAY_ROW_H - 16) * 0.5f * s);
        const int fw = (int)((float)(OVERLAY_VALUE_W - 12) * s), fh = (int)((float)(OVERLAY_ROW_H - 16) * s);

        DrawText(p->surface, OverlayLabelX() * s, baseline, 17.0f * s, labelText, label, ALIGN_LEFT);
        LookRect(p->surface, fx, fy, fw, fh, (Color){0.0f, 0.0f, 0.0f, selected ? 0.45f : 0.25f});
        LookFrame(p->surface, fx, fy, fw, fh, WithAlpha(kLookSilver, selected ? 0.38f : 0.12f));
        if (selected) {
            LookArrow(p->surface, centre - arrows, mid, 10.0f * s, -1, kLookRed);
            LookArrow(p->surface, centre + arrows, mid, 10.0f * s, 1, kLookRed);
        }
        if (strcmp(valueText, "On") == 0 || strcmp(valueText, "Off") == 0) {
            const int on = valueText[1] == 'n';
            DrawSpaced(p->surface, centre, baseline - 1.0f * s, 14.0f * s, 3.0f * s, on ? "ON" : "OFF",
                       on ? WithAlpha(kLookRed, selected ? 1.0f : 0.85f) : WithAlpha(kLookDim, 0.9f), ALIGN_CENTRE);
        } else {
            DrawText(p->surface, centre, baseline, 17.0f * s, valueText,
                     highlightValue ? kLookRed : WithAlpha(kLookText, selected ? 1.0f : 0.85f), ALIGN_CENTRE);
        }
        (void)reach;
        (void)chevron;
        return;
    }

    float rowEm=17.0f*s;
#ifdef PE_PS5_GPU_NATIVE
    rowEm=(host_options.menu_text_size?19.0f:17.0f)*s;
#endif
    #ifdef PE_PS5_GPU_NATIVE
    char fittedLabel[1024],fittedValue[1024];
    labelText=FitRowText(labelText,rowEm,(OVERLAY_PANEL_W-2*OVERLAY_PAD-OVERLAY_VALUE_W-20)*s,fittedLabel);
    valueText=FitRowText(valueText,rowEm,(OVERLAY_VALUE_W-52)*s,fittedValue);
    #endif
    DrawText(p->surface, (float)OVERLAY_PAD * s, baseline,rowEm,labelText,label,ALIGN_LEFT);
    DrawChevron(p->surface, centre - reach, mid, 5.0f * s, -1, 2.0f * s,
                chevron);
    DrawChevron(p->surface, centre + reach, mid, 5.0f * s, 1, 2.0f * s,
                chevron);
    DrawText(p->surface, centre, baseline,rowEm,valueText,
             highlightValue ? kAccent : (selected ? kText : WithAlpha(kText, 0.9f)),
             ALIGN_CENTRE);
}

/* "< 3x > Fast forward": the arrows step the speed; a dim arrow is the limit. */
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
    if (OVERLAY_REFINED_LOOK) {
        const float x = OverlayLabelX() * s;
        const Color arrow = selected ? kLookRed : WithAlpha(kLookDim, 0.75f), end = WithAlpha(kLookDim, 0.3f);
        centre = x + (float)OVERLAY_SPEED_STEP_CENTRE * s;
        LookArrow(p->surface, centre - reach, mid, 9.0f * s, -1, factor > 2 ? arrow : end);
        LookArrow(p->surface, centre + reach, mid, 9.0f * s, 1, factor < PSYZ_SPEED_MAX ? arrow : end);
        DrawText(p->surface, centre, baseline, 17.0f * s, text, label, ALIGN_CENTRE);
        DrawText(p->surface, x + (float)OVERLAY_SPEED_STEP_W * s, baseline, 17.0f * s, "Fast forward", label, ALIGN_LEFT);
        (void)chevron;
        (void)limit;
        return;
    }
    DrawChevron(p->surface, centre - reach, mid, 5.0f * s, -1, 2.0f * s,
                factor > 2 ? chevron : limit);
    DrawChevron(p->surface, centre + reach, mid, 5.0f * s, 1, 2.0f * s,
                factor < PSYZ_SPEED_MAX ? chevron : limit);
    DrawText(p->surface, centre, baseline, 17.0f * s, text, label, ALIGN_CENTRE);
    DrawText(p->surface, (float)(OVERLAY_PAD + OVERLAY_SPEED_STEP_W) * s,
             baseline, 17.0f * s, "Fast forward", label, ALIGN_LEFT);
}

static void PaintSettingRow(const Painter *p, const OverlaySetting *setting,
                            float mid, float baseline, int selected,
                            Color label) {
    int value = setting->get();
    if (value < 0) value = 0;
    if (value >= setting->count) value = setting->count - 1;
#ifdef PE_PS5_GPU_NATIVE
    if((setting->set==SetFsrStatus||setting->set==SetDialogueStatus||setting->set==SetTriangleFontStatus)){char fitLabel[1024],fitValue[1024];float em=(host_options.menu_text_size?19:17)*p->s;DrawText(p->surface,OVERLAY_PAD*p->s,baseline,em,FitRowText(setting->label,em,(OVERLAY_PANEL_W-2*OVERLAY_PAD-OVERLAY_VALUE_W-20)*p->s,fitLabel),label,ALIGN_LEFT);DrawText(p->surface,(OVERLAY_PANEL_W-OVERLAY_PAD)*p->s,baseline,17*p->s,FitRowText(setting->names[value],17*p->s,OVERLAY_VALUE_W*p->s,fitValue),selected?kText:kMuted,ALIGN_RIGHT);return;}
#endif
    const char *valueText=setting->names[value];
#ifdef PE_PS5_GPU_NATIVE
    if(setting->set==SetParticleLights&&value&&!FieldFx_GetParticleAugment())valueText="On; Original effects";
#endif
    PaintValueRow(p, setting->label, valueText, mid, baseline,
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
        Color label = OVERLAY_REFINED_LOOK ? WithAlpha(kLookText, selected ? 1.0f : 0.78f)
                                        : (selected ? kText : WithAlpha(kText, 0.82f));

        if (OVERLAY_REFINED_LOOK) {
            LookRowBand(p, (float)OVERLAY_PANEL_W, top, mid, selected, slot == 0 || slot == visibleSettings);
        } else if (selected) {
            FillRoundRect(p->surface, (float)(OVERLAY_PAD / 2) * s,
                          top + 3.0f * s, (float)(OVERLAY_PANEL_W - OVERLAY_PAD) * s,
                          (float)(OVERLAY_ROW_H - 6) * s, 9.0f * s,
                          WithAlpha(kAccent, 0.16f));
#ifdef PE_PS5_GPU_NATIVE
            if(host_options.menu_pointer==1||host_options.menu_pointer==2)DrawChevron(p->surface,(OVERLAY_PAD/2+4)*s,mid,5*s,1,2*s,kAccent);
            if(host_options.menu_pointer!=1)
#endif
            FillRoundRect(p->surface, (float)(OVERLAY_PAD / 2) * s + 4.0f * s,
                          mid - 10.0f * s, 3.0f * s, 20.0f * s, 1.5f * s,
                          kAccent);
        }
        if (row < pureCount) {
            if (settings != NULL && row < declCount) {
                PaintSettingRow(p, &settings[row], mid, baseline, selected, label);
#ifdef PE_PS5_GPU_NATIVE
            } else if(page==OVERLAY_PAGE_GRAPHICS&&row==1){PaintValueRow(p,"Advanced picture settings","Open",mid,baseline,selected,0,label);
            } else if(page==OVERLAY_PAGE_MODS||page==OVERLAY_PAGE_CHEATS||page==OVERLAY_PAGE_STATES||page==OVERLAY_PAGE_DISCS){
                char value[160];Host_ModernRowValue(page,row,value,sizeof value);
                if(Host_ModernRowKind(page,row)==2)PaintValueRow(p,Host_ModernRowLabel(page,row),value,mid,baseline,selected,0,label);
                else {char fitLabel[1024],fitValue[1024];float em=(host_options.menu_text_size?19.0f:17.0f)*s;DrawText(p->surface,OVERLAY_PAD*s,baseline,em,FitRowText(Host_ModernRowLabel(page,row),em,(OVERLAY_PANEL_W-2*OVERLAY_PAD-OVERLAY_VALUE_W-20)*s,fitLabel),label,ALIGN_LEFT);DrawText(p->surface,(OVERLAY_PANEL_W-OVERLAY_PAD)*s,baseline,16*s,FitRowText(value,16*s,OVERLAY_VALUE_W*s,fitValue),selected?kText:kMuted,ALIGN_RIGHT);}
#endif
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
                    PaintValueRow(p, "Fast-forward button", host_options.speed_toggle ? "Toggle" : "Hold",
                                  mid, baseline, selected, 0, label);
                } else {
                    int idx = row - 1;
                    int capturing = selected && p->state->capturing_binding;
                    const char *val = capturing
                                          ? "Press key or button..."
                                          : Host_ControlBindingValueName(p->state, idx);
                    int speed = idx == HOST_SPEED_BINDING;
                    PaintValueRow(p, speed ? "" : Host_BindingLabel((unsigned)idx),
                                  val, mid, baseline, selected, capturing, label);
                    if (speed) {
                        PaintSpeedLabel(p, mid, baseline, selected, label);
                    }
                }
            } else if (page == OVERLAY_PAGE_MODS) {
#ifdef PE_PS5
#ifdef PE_PS5_GPU_NATIVE
                const char *names[]={"GPU HD backgrounds","Local background pack","Download / update","Stretch movies"
#ifdef PE_MODS
,"Cheats / assists","3D effects (experimental)"
#endif
};
 #else
                const char *names[]={"Title HD","CPU field HD (experimental)","Field pack directory"};
 #endif
#else
                const char *names[]={"HD backgrounds","Background pack","Integrity check","Mods menu"};
#endif
#ifdef PE_PS5_GPU_NATIVE
#ifdef PE_MODS
                if(row==4)PaintValueRow(p,names[row],"Open menu",mid,baseline,selected,0,label);
                else
#endif
                if(row==5)PaintValueRow(p,names[row],host_options.battle_debris_3d==2?"New geometry":host_options.battle_debris_3d?"Hybrid geometry":"Original",mid,baseline,selected,0,label);
                else if(row==3)PaintValueRow(p,names[row],host_options.movie_presentation==0?"On":"Off (aspect + blur)",mid,baseline,selected,0,label);
                else
#endif
#if defined(PE_MODS) && !defined(PE_PS5)
                if(row==3)PaintValueRow(p,names[row],"Open menu",mid,baseline,selected,0,label);
                else
#endif
                if(row==0)PaintValueRow(p,names[row],Host_HdValue(row),mid,baseline,selected,0,label);
                else {
                    DrawText(p->surface,OverlayLabelX()*s,baseline,17*s,names[row],label,ALIGN_LEFT);
                    if (OVERLAY_REFINED_LOOK)
                        DrawText(p->surface,OverlayValueCentre()*s,baseline,17*s,Host_HdValue(row),selected?kLookRed:WithAlpha(kLookText,0.85f),ALIGN_CENTRE);
                    else
                        DrawText(p->surface,OverlayValueCentre()*s,baseline,17*s,Host_HdValue(row),selected?kAccent:kText,ALIGN_CENTRE);
                }
            } else if (page == OVERLAY_PAGE_DISCS && row == 2) {
                PaintValueRow(p, "Setup wizard", "Run again", mid, baseline, selected, 0, label);
            } else if (page == OVERLAY_PAGE_DISCS) {
                char discLabel[32];
                snprintf(discLabel, sizeof(discLabel), "Disc %d", row + 1);
                const char *val = g_CdImageVolumes[row] ? "Mounted" : "Select image...";
                PaintValueRow(p, discLabel, val, mid, baseline, selected, 0, label);
            }
        } else {
#ifdef PE_PS5_GPU_NATIVE
            int quit = row == pureCount + 2;
#else
            int quit = row == pureCount + 1;
#endif

            DrawText(p->surface, OverlayLabelX() * s, baseline, 17.0f * s,
#ifdef PE_PS5_GPU_NATIVE
                     quit ? "Quit game" : row==pureCount+1 ? "Back" : "Resume game",quit?kDanger:label,
#else
                     quit ? "Quit game" : "Resume game", quit ? WithAlpha(kLookRed, selected ? 1.0f : 0.85f) : label,
#endif
                     ALIGN_LEFT);
            if (OVERLAY_REFINED_LOOK && selected) {
                DrawSpaced(p->surface, OverlayValueCentre() * s, baseline - 1.0f * s, 12.0f * s, 2.5f * s,
                           "PRESS ENTER", kLookRed, ALIGN_CENTRE);
            }
        }
    }

    if(page==OVERLAY_PAGE_MODS
#ifdef PE_PS5_GPU_NATIVE
       &&0 /* Native pack details live in rows/help, never over the pinned actions. */
#endif
    ) {
        float x=OVERLAY_PAD*s,y=(OVERLAY_HEADER_H+Host_SettingsPureRowCount(OVERLAY_PAGE_MODS)*OVERLAY_ROW_H)*s,w=(OVERLAY_PANEL_W-2*OVERLAY_PAD)*s;
        if(Host_HdBusy()) {
            char text[80];snprintf(text,sizeof(text),"Downloading / verifying: %d%%",Host_HdPercent());
            DrawText(p->surface,x,y,15*s,text,kLookText,ALIGN_LEFT);
            LookRect(p->surface,(int)x,(int)(y+18*s),(int)w,(int)(10*s),(Color){0,0,0,.45f});
            LookFrame(p->surface,(int)x,(int)(y+18*s),(int)w,(int)(10*s),WithAlpha(kLookSilver,.30f));
            LookHGradient(p->surface,(int)x+1,(int)(y+19*s),(int)(w*Host_HdPercent()/100.f)-2,(int)(8*s),WithAlpha(kLookRed,.7f),kLookRed);
        } else {
#if defined(PE_PS5_GPU_NATIVE)
            char text[96];snprintf(text,sizeof(text),"Local pack: %s",Host_GpuHdPackPresent(Host_GpuHdDirectory())?"found (atlas checks on use)":"not found");
#else
            char text[96];snprintf(text,sizeof(text),"Installed version: %s",*host_hd.version?host_hd.version:host_hd.development?"local preview":"none");
#endif
            DrawText(p->surface,x,y,15*s,text,kLookDim,ALIGN_LEFT);
        }
    }
    if(page!=OVERLAY_PAGE_CONTROLS && pureCount>visibleSettings){
        float trackTop=(OVERLAY_HEADER_H+4)*s,trackH=(visibleSettings*OVERLAY_ROW_H-8)*s;
        float thumbH=trackH*(float)visibleSettings/(float)pureCount;
        float top=trackTop+(trackH-thumbH)*(float)p->state->scroll/(float)(pureCount-visibleSettings);
        if (OVERLAY_REFINED_LOOK) LookScrollBar(p,(OVERLAY_PANEL_W-13)*s,trackTop,trackH,top,thumbH);
        else {
        FillRoundRect(p->surface,(OVERLAY_PANEL_W-12)*s,trackTop,4*s,trackH,2*s,WithAlpha(kText,.10f));
        FillRoundRect(p->surface,(OVERLAY_PANEL_W-12)*s,top,4*s,thumbH,2*s,WithAlpha(kAccent,.65f));
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
        if (OVERLAY_REFINED_LOOK) {
            LookScrollBar(p, trackX - 1.0f * s, trackTop, trackH, thumbTop, thumbH);
        } else {
            FillRoundRect(p->surface, trackX, trackTop, 4.0f * s, trackH, 2.0f * s,
                          WithAlpha(kText, 0.10f));
            FillRoundRect(p->surface, trackX, thumbTop, 4.0f * s, thumbH, 2.0f * s,
                          WithAlpha(kAccent, 0.65f));
        }
    }
}

static const char *OverlayRowHelp(const HostOverlayState *state) {
#ifdef PE_PS5_GPU_NATIVE
    if(state->page==OVERLAY_PAGE_GRAPHICS&&state->row==1)return "Adjust individual picture choices. Changes become part of your remembered Modern presentation; gameplay and audio preferences are unchanged.";
    int customCount=Host_SettingsPureRowCount(state->page);
    if(state->row<customCount&&(state->page==OVERLAY_PAGE_MODS||state->page==OVERLAY_PAGE_CHEATS||state->page==OVERLAY_PAGE_STATES||(state->page==OVERLAY_PAGE_DISCS&&state->row>=3)))return Host_ModernRowHelp(state->page,state->row);
    if(state->row==customCount)return "Resume the game with your current settings.";
    if(state->row==customCount+1)return state->page==OVERLAY_PAGE_AUDIO?"Return to Experience; your picture choices are kept.":(state->page==OVERLAY_PAGE_MODS||state->page==OVERLAY_PAGE_CHEATS)?"Return to the parent page, or resume from a category root. Choices are kept.":"Back to the game; current choices are kept, like Circle.";
    if(state->row==customCount+2)return "Quit the game. Save normal progress before leaving.";
#endif
#ifdef PE_PS5_GPU_NATIVE
    if(state->page==OVERLAY_PAGE_MODS&&state->row==5)
        return "Supported spark/debris meshes. Hybrid also accents Program109 flames. Unsupported or rejected effects retain originals; not universal fire or smoke.";
    if(state->page==OVERLAY_PAGE_MODS&&state->row==4)
        return "Open cheats and assists. Circle goes back to mod pages, including Manage mods. Master changes need a game restart.";
#endif
    if (state->status != NULL && state->status[0] != '\0') {
        return state->status;
    }
    if (state->capturing_binding) {
        return "Press a key or controller button to bind. Esc cancels.";
    }
#ifdef PE_PS5_GPU_NATIVE
    if(state->page==OVERLAY_PAGE_MODS&&state->row==3)
        return "On fills the TV. Off keeps the original movie aspect with blurred borders. Applies to both original video formats.";
#endif
#if defined(PE_MODS) && !defined(PE_PS5)
    if(state->page==OVERLAY_PAGE_MODS&&state->row==3)
        return "Open the Mods menu (F1, Guide or the Mods button) to turn mods on or off and set their options.";
#endif
    int pureCount = Host_SettingsPureRowCount(state->page);
    if (state->row == pureCount) {
#ifndef PE_PS5
        if (host_setup_mode) return "Start the game with these settings. Esc or the Settings button changes them later.";
#endif
        return "Return to the game.";
    }
    if (state->row == pureCount + OVERLAY_ACTION_ROWS-1) {
        return "Close the game.";
    }
    int declCount = 0;
    const OverlaySetting *settings = OverlayPageSettings(state->page, &declCount);
    if (settings != NULL && state->row >= 0 && state->row < declCount) {
#if defined(PE_PS5) && !defined(PE_PS5_GPU_NATIVE)
        if(state->page==OVERLAY_PAGE_GRAPHICS && state->row==0){
            static char status[240];
            snprintf(status,sizeof(status),"CPU geometry requested %ux; last prepared frame %ux. Experimental; rejected epochs use canonical 1x. Geometry overrides field HD.",Psyz_CpuGeometryRequested(),Psyz_CpuGeometryEffective());
            return status;
        }
#endif
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
            int unbound = state->bind_device == HOST_BIND_CONTROLLER
                ? host_options.buttons[HOST_SPEED_BINDING] == HOST_UNBOUND_BUTTON
                : host_options.keys[HOST_SPEED_BINDING] == HOST_UNBOUND_KEY;
            if (unbound)
                return "No fast-forward button assigned. Enter/South: choose a binding. Left/Right: 2x-4x.";
            return host_options.speed_toggle
                       ? "Press to speed up, again for normal. Left/Right: 2x-4x. Enter/South binds; Del/West unbinds."
                       : "Hold to speed up. Left/Right: 2x-4x. Enter/South binds; Del/West unbinds.";
        }
        if (state->row == HOST_SPEED_MODE_ROW) {
            return "Hold: fast while the speed button is held. Toggle: each press switches it on or off.";
        }
        if (state->row == HOST_SCREENSHOT_BINDING + 1) {
            return "Capture a screenshot with note & background ID. Enter/South binds; Del/West unbinds.";
        }
        if (state->row == HOST_SETTINGS_BINDING + 1 || state->row == HOST_SETTINGS2_BINDING + 1) {
            return "Open this menu: hold both together (one if the other is unbound). Esc always works.";
        }
        if (state->row == HOST_MODS_BINDING + 1) {
            return "Open or close the Mods menu, like F1 or the Guide button. Del/West unbinds.";
        }
        return "Press Enter or South button to rebind. Duplicate bindings swap automatically.";
    }
    if (state->page == OVERLAY_PAGE_DISCS) {
        if (state->row == 2) return "Restart the game into setup: storage, discs, settings and mods.";
        return g_CdImageVolumes[state->row]
                   ? "This disc image is validated and mounted."
                   : "Press Enter to select a Parasite Eve (USA) CUE or BIN disc image.";
    }
    return "";
}

#ifdef PE_PS5_GPU_NATIVE
/* Two bounded help lines: long option descriptions must not leave the panel. */
static void DrawNativeHelp(const Painter *p,float top,const char *text){
    const float width=(OVERLAY_PANEL_W-2*OVERLAY_PAD)*p->s,em=13*p->s;
    char line[256];
    for(int row=0;row<2 && text && *text;row++){
        size_t n=0,last_space=0;
        while(text[n] && n<sizeof(line)-4){line[n]=text[n];line[n+1]=0;if(TextWidth(line,em)>width)break;if(text[n]==' ')last_space=n;n++;}
        if(text[n] && row==0 && last_space)n=last_space;
        line[n]=0;text+=n;while(*text==' ')text++;
        if(row==1 && *text){while(n && TextWidth(line,em)+TextWidth("...",em)>width)line[--n]=0;strcat(line,"...");}
        DrawText(p->surface,OVERLAY_PAD*p->s,top+(23+18*row)*p->s,em,line,kMuted,ALIGN_LEFT);
    }
}
#endif
static void PaintFooter(const Painter *p, int visibleRowCount) {
    const float s = p->s;
    float footerTop = (float)(OverlayDesignHeight(visibleRowCount) - OVERLAY_FOOTER_H) * s;
    const char *help = OverlayRowHelp(p->state);

    if (OVERLAY_REFINED_LOOK) {
        LookHelpBox(p, (float)OVERLAY_PANEL_W, footerTop / s + 8.0f, (float)(OVERLAY_FOOTER_H - 20), help,
                    p->state->page == OVERLAY_PAGE_MODS
                        ? "Enter / click activate   PgUp/PgDn page   Esc resume"
                        : "Up/Down select   Left/Right change   PgUp/PgDn page   Esc resume");
        return;
    }
    FillRoundRect(p->surface, (float)OVERLAY_PAD * s, footerTop,
                  (float)(OVERLAY_PANEL_W - 2 * OVERLAY_PAD) * s, 1.0f, 0.0f,
                  WithAlpha(kText, 0.10f));
#ifdef PE_PS5_GPU_NATIVE
    DrawNativeHelp(p,footerTop,help);
#else
    DrawText(p->surface, (float)OVERLAY_PAD * s, footerTop + 28.0f * s,
             14.0f * s, help, kMuted, ALIGN_LEFT);
#endif
    DrawText(p->surface, (float)OVERLAY_PAD * s, footerTop +
#ifdef PE_PS5_GPU_NATIVE
             72.0f * s,
#else
             54.0f * s,
#endif
             13.0f * s,
#ifdef PE_PS5_GPU_NATIVE
             "D-pad / left stick select   Left/right change   Cross confirm   L1/R1 tabs   Circle back",
#else
             p->state->page==OVERLAY_PAGE_MODS?"Enter / click activate   PgUp/PgDn page   Esc resume":"Up/Down select   Left/Right change   PgUp/PgDn page   Esc resume",
#endif
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
    if (OVERLAY_REFINED_LOOK) {
        LookTitle(&painter, (float)PROMPT_PANEL_W, 66.0f, "SKIP CUTSCENE", "MOVIE PAUSED");
        DrawText(surface, (float)OVERLAY_PAD * s, 96.0f * s, 16.0f * s, "Do you want to skip the cutscene?",
                 WithAlpha(kLookText, 0.82f), ALIGN_LEFT);
        for (row = 0; row < PROMPT_ROWS; row++) {
            OverlayRect rect = OverlayPromptRowRect(row);
            float top = rect.y * s;
            float mid = top + (float)OVERLAY_ROW_H * 0.5f * s;
            int selected = row == state->row;

            LookRowBand(&painter, (float)PROMPT_PANEL_W, top, mid, selected, row == 0);
            DrawText(surface, OverlayLabelX() * s, mid + 6.0f * s, 17.0f * s, kAnswers[row],
                     WithAlpha(kLookText, selected ? 1.0f : 0.78f), ALIGN_LEFT);
        }
        LookRect(surface, (int)((float)OVERLAY_PAD * s), (int)(footerTop + 4.0f * s),
                 (int)((float)(PROMPT_PANEL_W - 2 * OVERLAY_PAD) * s), 1, WithAlpha(kLookSilver, 0.10f));
        DrawSpaced(surface, (float)OVERLAY_PAD * s, footerTop + 36.0f * s, 10.0f * s, 1.6f * s,
                   "UP/DOWN SELECT   ENTER CONFIRM   ESC KEEP WATCHING", WithAlpha(kLookDim, 0.85f), ALIGN_LEFT);
        solid = surface->pixels + (size_t)(surface->h - 1) * (size_t)surface->w * 4u;
        solid[3] = 255;
        return;
    }
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

static const char *const kScreenshotTagNames[] = {
    "None",
    "Background hallucination",
    "Lighting / tint issue",
    "Widescreen seam / edge",
    "Depth / mask layering",
    "Missing / wrong tile",
    "Geometry / alignment"
};

static const char *const kScreenshotOskKeys[SCREENSHOT_OSK_ROWS][SCREENSHOT_OSK_COLS] = {
    {"a", "b", "c", "d", "e", "f", "g", "h", "i", "j"},
    {"k", "l", "m", "n", "o", "p", "q", "r", "s", "t"},
    {"u", "v", "w", "x", "y", "z", "0", "1", "2", "3"},
    {"4", "5", "6", "7", "8", "9", "Space", "Bksp", "Clear", "Done"}
};

static const char *Host_ScreenshotUploadUrl(void) {
    const char *env = SDL_getenv("PE_SCREENSHOT_UPLOAD_URL");
    if (env && *env) return env;
    if (*host_options.screenshot_upload_url) return host_options.screenshot_upload_url;
    return NULL;
}

static void OverlayPaintScreenshot(OverlaySurface *surface, const HostOverlayState *state,
                                   const OverlayLayout *layout) {
    Painter painter = {surface, state, layout->scale};
    const float s = layout->scale;
    int designH = OverlayScreenshotDesignHeight(state);
    float footerTop = (float)(designH - SCREENSHOT_FOOTER_H) * s;
    const char *uploadUrl = Host_ScreenshotUploadUrl();
    int tagIdx = ClampIndex(state->tag_index, COUNT_OF(kScreenshotTagNames));
    uint8_t *solid;
    int row;

    memset(surface->pixels, 0, (size_t)surface->w * (size_t)surface->h * 4u);
    PaintFrame(&painter, (float)SCREENSHOT_PANEL_W * s, (float)designH * s);
    if (OVERLAY_REFINED_LOOK) {
        LookTitle(&painter, (float)SCREENSHOT_PANEL_W, 66.0f, "SAVE SCREENSHOT", "GAME PAUSED");
        DrawText(surface, (float)OVERLAY_PAD * s, 96.0f * s, 15.0f * s,
                 state->bg_summary[0] ? state->bg_summary : "Background ID: none", kLookSteel, ALIGN_LEFT);
    } else {
    DrawText(surface, (float)OVERLAY_PAD * s, 40.0f * s, 26.0f * s, "Save screenshot",
             kText, ALIGN_LEFT);
    DrawText(surface, (float)(SCREENSHOT_PANEL_W - OVERLAY_PAD) * s, 38.0f * s, 14.0f * s,
             "GAME PAUSED", kMuted, ALIGN_RIGHT);
    DrawText(surface, (float)OVERLAY_PAD * s, 78.0f * s, 15.0f * s,
             state->bg_summary[0] ? state->bg_summary : "Background ID: none",
             WithAlpha(kAccent, 0.92f), ALIGN_LEFT);
    FillRoundRect(surface, (float)OVERLAY_PAD * s, (float)(SCREENSHOT_HEADER_H - 12) * s,
                  (float)(SCREENSHOT_PANEL_W - 2 * OVERLAY_PAD) * s, 1.0f, 0.0f,
                  WithAlpha(kText, 0.10f));
    }

    for (row = 0; row < SCREENSHOT_ROWS; row++) {
        OverlayRect rect = OverlayScreenshotRowRect(state, row);
        float top = rect.y * s;
        float mid = top + (float)OVERLAY_ROW_H * 0.5f * s;
        float baseline = mid + 6.0f * s;
        int selected = !state->osk_open && row == state->row;
        Color label = OVERLAY_REFINED_LOOK ? WithAlpha(kLookText, selected ? 1.0f : 0.78f)
                                        : (selected ? kText : WithAlpha(kText, 0.82f));
        const float labelX = OverlayLabelX() * s;

        if (OVERLAY_REFINED_LOOK) {
            LookRowBand(&painter, (float)SCREENSHOT_PANEL_W, top, mid,
                        selected || (state->osk_open && row == SCREENSHOT_ROW_NOTE),
                        row == 0 || row == SCREENSHOT_ROW_SAVE);
        } else if (selected || (state->osk_open && row == SCREENSHOT_ROW_NOTE)) {
            FillRoundRect(surface, rect.x * s, top + 3.0f * s, rect.w * s,
                          (float)(OVERLAY_ROW_H - 6) * s, 9.0f * s,
                          WithAlpha(kAccent, 0.16f));
            FillRoundRect(surface, rect.x * s + 4.0f * s, mid - 10.0f * s, 3.0f * s,
                          20.0f * s, 1.5f * s, kAccent);
        }

        if (row == SCREENSHOT_ROW_TAG) {
            PaintValueRow(&painter, "Issue tag", kScreenshotTagNames[tagIdx], mid, baseline,
                          selected, 0, label);
        } else if (row == SCREENSHOT_ROW_NOTE) {
            char displayNote[140];
            DrawText(surface, labelX, baseline, 17.0f * s, "Note", label,
                     ALIGN_LEFT);
            if (state->note[0] != '\0' || state->osk_open || selected) {
                snprintf(displayNote, sizeof(displayNote), "%s%s", state->note,
                         (selected || state->osk_open) ? "|" : "");
                DrawText(surface, labelX + 90.0f * s, baseline, 16.0f * s,
                         displayNote, OVERLAY_REFINED_LOOK ? kLookText : kText, ALIGN_LEFT);
            } else {
                DrawText(surface, labelX + 90.0f * s, baseline, 15.0f * s,
                         "Type or press Enter / South for keyboard...", OVERLAY_REFINED_LOOK ? kLookDim : kMuted,
                         ALIGN_LEFT);
            }
        } else if (row == SCREENSHOT_ROW_UPLOAD) {
            const char *val = !uploadUrl ? "Not configured"
                              : (host_options.screenshot_auto_upload ? "On" : "Off");
            PaintValueRow(&painter, "Auto-upload", val, mid, baseline, selected, 0, label);
        } else if (row == SCREENSHOT_ROW_SAVE) {
            const char *saveLabel = (uploadUrl && host_options.screenshot_auto_upload)
                                        ? "Save & upload screenshot"
                                        : "Save screenshot";
            DrawText(surface, labelX, baseline, 17.0f * s, saveLabel,
                     selected ? (OVERLAY_REFINED_LOOK ? kLookText : kAccent) : label, ALIGN_LEFT);
        } else if (row == SCREENSHOT_ROW_CANCEL) {
            DrawText(surface, labelX, baseline, 17.0f * s, "Cancel",
                     selected ? (OVERLAY_REFINED_LOOK ? kLookRed : kDanger) : label, ALIGN_LEFT);
        }
    }

    if (state->osk_open) {
        for (int r = 0; r < SCREENSHOT_OSK_ROWS; r++) {
            for (int c = 0; c < SCREENSHOT_OSK_COLS; c++) {
                OverlayRect cell = OverlayScreenshotOskKeyRect(r, c);
                int active = (r == state->osk_row && c == state->osk_col);
                if (OVERLAY_REFINED_LOOK) {   /* framed keys; the one under the cursor glows red */
                    const int kx = (int)(cell.x * s), ky = (int)(cell.y * s), kw = (int)(cell.w * s), kh = (int)(cell.h * s);
                    LookRect(surface, kx, ky, kw, kh, active ? WithAlpha(kLookRed, 0.30f) : (Color){0.0f, 0.0f, 0.0f, 0.35f});
                    LookFrame(surface, kx, ky, kw, kh, active ? kLookRed : WithAlpha(kLookSilver, 0.18f));
                    DrawText(surface, (cell.x + cell.w * 0.5f) * s, (cell.y + cell.h * 0.5f + 5.0f) * s, 14.0f * s,
                             kScreenshotOskKeys[r][c], active ? kLookText : kLookDim, ALIGN_CENTRE);
                    continue;
                }
                FillRoundRect(surface, cell.x * s, cell.y * s, cell.w * s, cell.h * s,
                              6.0f * s,
                              active ? WithAlpha(kAccent, 0.35f) : WithAlpha(kText, 0.08f));
                DrawText(surface, (cell.x + cell.w * 0.5f) * s,
                         (cell.y + cell.h * 0.5f + 5.0f) * s, 14.0f * s,
                         kScreenshotOskKeys[r][c], active ? kText : kMuted, ALIGN_CENTRE);
            }
        }
    }

    const char *help = (state->status && state->status[0])
                           ? state->status
                       : state->osk_open
                           ? "D-pad select key   South/Enter type   West Bksp   North Space   Start Done"
                           : "Type note or press South/Enter on Note for controller keyboard. Start saves.";
    if (OVERLAY_REFINED_LOOK) {
        LookHelpBox(&painter, (float)SCREENSHOT_PANEL_W, footerTop / s + 6.0f, (float)(SCREENSHOT_FOOTER_H - 16), help,
                    "Up/Down select   Left/Right change   Start/Enter save   Esc/East cancel");
    } else {
    FillRoundRect(surface, (float)OVERLAY_PAD * s, footerTop,
                  (float)(SCREENSHOT_PANEL_W - 2 * OVERLAY_PAD) * s, 1.0f, 0.0f,
                  WithAlpha(kText, 0.10f));
    DrawText(surface, (float)OVERLAY_PAD * s, footerTop + 26.0f * s, 14.0f * s, help, kMuted,
             ALIGN_LEFT);
    DrawText(surface, (float)OVERLAY_PAD * s, footerTop + 50.0f * s, 13.0f * s,
             "Up/Down select   Left/Right change   Start/Enter save   Esc/East cancel",
             WithAlpha(kMuted, 0.7f), ALIGN_LEFT);
    }
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

static int s_overlayIsFps;
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
    Host_WriteOverlayQuad(verts, -1.0f, 1.0f, 1.0f, -1.0f, solidU, solidV, solidU, solidV, s_overlayIsFps?0.0f:0.62f);

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
    if(!copy){SDL_CancelGPUCommandBuffer(command);SDL_ReleaseGPUTransferBuffer(device,transfer);return 0;}

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
    bool submitted=SDL_SubmitGPUCommandBuffer(command);
    SDL_ReleaseGPUTransferBuffer(device, transfer);
    return submitted;
}

#ifdef PE_PS5_GPU_NATIVE
#include "installer/pe_install_paint.h"
#endif
static void Host_OverlayRefreshGPU(
    SDL_GPUDevice *device,
    SDL_Window *window,
    HostOverlayState *state
) {
    s_overlayIsFps=0;
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
    OverlayLayout raster=layout;
#ifdef PE_PS5_GPU_NATIVE
    /* Keep the UI allocation bounded; scale the completed Plex panel to its
     * physical layout, never allocate a full4K CPU game framebuffer. */
    static float rasterScaleLimit=2.5f;
    const double budget=6.0*1024.0*1024.0;
    float designW=(float)layout.w/layout.scale,designH=(float)layout.h/layout.scale;
    float limit=(float)sqrt(budget/(4.0*(double)designW*(double)designH));
    if(limit>rasterScaleLimit)limit=rasterScaleLimit;
    if(raster.scale>limit)raster.scale=floorf(limit*8.0f)/8.0f;
    if(raster.scale<0.5f)raster.scale=0.5f;
    for(;;){
        raster.w=(int)ceilf(designW*raster.scale);raster.h=(int)ceilf(designH*raster.scale);
        if(OverlaySurfaceResize(&s_overlaySurface,raster.w,raster.h))break;
        if(raster.scale<=0.5f){SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"Native menu raster allocation failed at bounded minimum");return;}
        raster.scale=floorf(raster.scale*0.75f*8.0f)/8.0f;if(raster.scale<0.5f)raster.scale=0.5f;
        rasterScaleLimit=raster.scale; /* Do not retry a known failed allocation next repaint. */
    }
#else
    if(!OverlaySurfaceResize(&s_overlaySurface,layout.w,layout.h))return;
#endif
#ifdef PE_PS5_GPU_NATIVE
    if(state->installer)OverlayPaintInstaller(&s_overlaySurface,&raster);else
#endif
    if (state->prompt) {
        OverlayPaintPrompt(&s_overlaySurface, state, &raster);
    } else if (state->screenshot) {
        OverlayPaintScreenshot(&s_overlaySurface, state, &raster);
    } else {
        OverlayPaint(&s_overlaySurface, state, &raster);
    }
    if (Host_EnsureOverlayPipeline(device, SDL_GetGPUSwapchainTextureFormat(device, window)) &&
        Host_UploadOverlaySurfaceAndVertices(device)) {
        state->dirty = 0;
    }
}

/* Reuses the existing Plex raster, overlay pipeline/context and transfer
 * path. No full-screen surface, extra renderer or shader. Update at most4Hz. */
#ifdef PE_PS5_GPU_NATIVE
extern float Psyz_GpuGetPresentedFps(void);
static void Host_FpsRefreshGPU(void){
    SDL_Window *window=Psyz_GetWindow_SDL3();int w=0,h=0;if(!window||!s_overlayGpuDevice||!OverlayFontReady())return;
    SDL_GetWindowSizeInPixels(window,&w,&h);if(w<=0||h<=0)return;
    static Uint64 last;Uint64 now=SDL_GetTicks();
    if(s_overlayIsFps&&w==s_overlayWindowW&&h==s_overlayWindowH&&now-last<250)return;
    if(!OverlaySurfaceResize(&s_overlaySurface,260,55))return;
    memset(s_overlaySurface.pixels,0,260u*55u*4u);
    FillRoundRect(&s_overlaySurface,0,0,260,54,8,(Color){0.02f,0.025f,0.03f,0.80f});
    char label[64];float fps=Psyz_GpuGetPresentedFps();
    if(fps>0&&fps<10000)snprintf(label,sizeof label,"Display %.1f FPS",fps);else SDL_strlcpy(label,"Display FPS: measuring",sizeof label);
    DrawText(&s_overlaySurface,14,35,23,label,kText,ALIGN_LEFT);
    float scale=(float)h/1080;if(scale<1)scale=1;if(scale>3)scale=3;
    s_overlayWindowW=w;s_overlayWindowH=h;s_overlayLayout=(OverlayLayout){.x=(int)(w*0.03f),.y=(int)(h*0.03f),.w=(int)(260*scale),.h=(int)(54*scale),.scale=scale};
    if(!Host_EnsureOverlayPipeline(s_overlayGpuDevice,SDL_GetGPUSwapchainTextureFormat(s_overlayGpuDevice,window)))return;
    s_overlayIsFps=1;
    if(!Host_UploadOverlaySurfaceAndVertices(s_overlayGpuDevice)){s_overlayIsFps=0;return;}
    last=now;s_overlayState.dirty=1; /* Next real menu must repaint its own panel. */
}
#endif
static void Host_OverlayRenderGPU(SDL_GPUCommandBuffer *command, SDL_GPURenderPass *pass) {
    if (s_prevOverlayRender != NULL) {
        s_prevOverlayRender(command, pass);
    }
#ifdef PE_PS5_GPU_NATIVE
    if(!s_overlayState.open&&host_options.show_fps)Host_FpsRefreshGPU();
    if ((!s_overlayState.open&&(!host_options.show_fps||!s_overlayIsFps)) || s_overlayPipeline == NULL || s_overlayTexture == NULL ||
        s_overlayVertexBuffer == NULL || s_overlayWindowW <= 0 || s_overlayWindowH <= 0) {
        return;
    }
#else
    if (!s_overlayState.open || s_overlayPipeline == NULL || s_overlayTexture == NULL ||
        s_overlayVertexBuffer == NULL || s_overlayWindowW <= 0 || s_overlayWindowH <= 0) {
        return;
    }
#endif

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
