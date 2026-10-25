#ifndef PE_BG_EDITOR_H
#define PE_BG_EDITOR_H

/* Parasite Eve Interactive Background & Animation Studio.
 *
 * Inspects, plays, and edits room backgrounds (GeomState), including:
 *   - Frame-by-frame group animations (GeomCtrlEntry / GeomAnimationSlot)
 *   - Continuous scrolling & parallax layers (GeomEntry flags 0x04 / 0x08)
 *   - Layer 1 (base grid) and Layer 2 (foreground 3D actor occluders)
 *   - Ordering Table Z-depth occlusion maps, PS1 ABR semi-transparency modes
 *   - Live 8bpp VRAM tile texel painting & 16-color BGR555 CLUT editing
 *   - One-click BMP export of room views and animation frames
 */

#include "pe1/asset_table.h"
#include "pe1/render_camera.h"
#include "pe1/render_prim.h"
#include "pe1/save.h"
#include "pe1/scene_assets.h"
#include "pe1/scene_room.h"
#include "pe1/scene_room_load.h"
#include <sys/stat.h>

s32 Scene_StreamTexturePage(void);
int Render_PrepareFrame(void);

enum {
    BG_STUDIO_W = 1280,
    BG_STUDIO_H = 800,
    BG_CANVAS_W = 320,
    BG_CANVAS_H = 224,
    BG_VRAM_W = 1024,
    BG_VRAM_H = 512,

    BG_VIEW_COMPOSITE = 0,
    BG_VIEW_LAYER1 = 1,
    BG_VIEW_LAYER2_ANIM = 2,
    BG_VIEW_ZDEPTH = 3,
    BG_VIEW_VRAM_ATLAS = 4,

    BG_TAB_LAYERS = 0,
    BG_TAB_PAINTER = 1,
    BG_TAB_PRESETS = 2,

    BG_MAX_WIDGETS = 128
};

typedef enum BgActionId {
    BG_ACT_NONE = 0,
    BG_ACT_ROOM_MINUS10,
    BG_ACT_ROOM_PREV,
    BG_ACT_ROOM_NEXT,
    BG_ACT_ROOM_PLUS10,
    BG_ACT_ROOM_NEXT_ANIM,
    BG_ACT_CAM_GROUP_NEXT,
    BG_ACT_PLAY_TOGGLE,
    BG_ACT_STEP_FRAME,
    BG_ACT_EXPORT_BMP,
    BG_ACT_EXPORT_STRIP,
    BG_ACT_CLOSE,

    BG_ACT_VIEW_MODE,          /* param = 0..4 */
    BG_ACT_INSPECTOR_TAB,      /* param = 0..2 */

    BG_ACT_TOGGLE_GRID,
    BG_ACT_TOGGLE_OUTLINE_SEL,
    BG_ACT_TOGGLE_OUTLINE_ANIM,
    BG_ACT_TOGGLE_AUTOPAN,

    BG_ACT_TOGGLE_AUTOLOOP,
    BG_ACT_SELECT_CTRL,        /* param = ctrl index */
    BG_ACT_SELECT_SLOT,        /* param = slot index */
    BG_ACT_SLOT_PREV,
    BG_ACT_SLOT_NEXT,
    BG_ACT_SLOT_DUR_MINUS,
    BG_ACT_SLOT_DUR_PLUS,
    BG_ACT_CTRL_REV_DIR,
    BG_ACT_CTRL_TOGGLE_PLAY,

    BG_ACT_LAYERS_ALL_ON,
    BG_ACT_LAYERS_ALL_OFF,
    BG_ACT_LAYERS_PAGE_PREV,
    BG_ACT_LAYERS_PAGE_NEXT,
    BG_ACT_LAYER_SELECT,       /* param = entry index */
    BG_ACT_LAYER_TOGGLE_VIS,   /* param = entry index */
    BG_ACT_LAYER_EDIT_TILES,   /* param = entry index */

    BG_ACT_LAYER_X_M4,
    BG_ACT_LAYER_X_M1,
    BG_ACT_LAYER_X_P1,
    BG_ACT_LAYER_X_P4,
    BG_ACT_LAYER_Y_M4,
    BG_ACT_LAYER_Y_M1,
    BG_ACT_LAYER_Y_P1,
    BG_ACT_LAYER_Y_P4,
    BG_ACT_LAYER_Z_M64,
    BG_ACT_LAYER_Z_M16,
    BG_ACT_LAYER_Z_P16,
    BG_ACT_LAYER_Z_P64,
    BG_ACT_LAYER_TOGGLE_SCROLL,
    BG_ACT_LAYER_TOGGLE_PARALLAX,
    BG_ACT_LAYER_SPDX_MINUS,
    BG_ACT_LAYER_SPDX_PLUS,
    BG_ACT_LAYER_SPDY_MINUS,
    BG_ACT_LAYER_SPDY_PLUS,

    BG_ACT_PAINTER_LAYER_PREV,
    BG_ACT_PAINTER_LAYER_NEXT,
    BG_ACT_PAINTER_TILE_PREV,
    BG_ACT_PAINTER_TILE_NEXT,
    BG_ACT_TILE_TOGGLE_SEMI,
    BG_ACT_TILE_CYCLE_ABR,
    BG_ACT_TILE_X_M16,
    BG_ACT_TILE_X_P16,
    BG_ACT_TILE_Y_M16,
    BG_ACT_TILE_Y_P16,
    BG_ACT_TILE_U_P16,
    BG_ACT_TILE_V_P16,
    BG_ACT_CLUT_SELECT,        /* param = 0..15 */
    BG_ACT_CLUT_R_MINUS,
    BG_ACT_CLUT_R_PLUS,
    BG_ACT_CLUT_G_MINUS,
    BG_ACT_CLUT_G_PLUS,
    BG_ACT_CLUT_B_MINUS,
    BG_ACT_CLUT_B_PLUS,
    BG_ACT_CLUT_R_BAR,
    BG_ACT_CLUT_G_BAR,
    BG_ACT_CLUT_B_BAR,
    BG_ACT_CLUT_TOGGLE_STP,
    BG_ACT_CLUT_TOGGLE_ZERO,

    BG_ACT_PRESET_ROOM,        /* param = mapNumber */
    BG_ACT_CAM_LEFT,
    BG_ACT_CAM_RIGHT,
    BG_ACT_CAM_UP,
    BG_ACT_CAM_DOWN,
    BG_ACT_CAM_CENTER,
    BG_ACT_ROOM_RESET
} BgActionId;

typedef struct BgWidget {
    BgActionId action;
    int param;
    float x, y, w, h;
} BgWidget;

typedef struct BgEditorState {
    int active;
    int quit;
    int dirty;
    int mapNumber;          /* 1..450 */
    int viewMode;           /* BG_VIEW_* */
    int inspectorTab;       /* BG_TAB_* */
    int playing;            /* 1 = running animations */
    int autoActivateAnims;  /* 1 = force-enable all GeomCtrlEntry in group */
    int autoPanCamera;      /* 1 = oscillate camera within viewport bounds */
    int showGrid;           /* 1 = 16x16 tile grid overlay */
    int highlightSelected;  /* 1 = outline tiles of selectedEntry */
    int highlightAnimated;  /* 1 = outline tiles of animated entries */
    int tickDivider;        /* 60Hz -> 30Hz divider */
    int panPhase;

    int selectedCtrl;       /* index in GeomCtrlEntry[] */
    int selectedEntry;      /* index in GeomEntry[] */
    int selectedTile;       /* index in selectedEntry's tile array */
    int selectedClutIdx;    /* 0..15 brush color index in active CLUT */
    int entryListScroll;    /* scroll offset for GeomEntry list */

    float mouseX;           /* current mouse X in design units */
    float mouseY;           /* current mouse Y in design units */
    int hoverCanvasX;       /* 0..319 or -1 */
    int hoverCanvasY;       /* 0..223 or -1 */
    int draggingTexel;      /* 1 while painting in 16x16 tile zoom box */
    int draggingRgbChannel; /* 0..2 while dragging R/G/B bar, or -1 */
    int draggingPan;        /* 1 while right-dragging canvas camera pan */
    float panDragStartX;
    float panDragStartY;
    int panOrigCamX;
    int panOrigCamY;

    BgWidget widgets[BG_MAX_WIDGETS];
    int widgetCount;

    char statusMsg[160];
    Uint64 statusExpireTick;

    /* Lossless 16-bit BGR555 shadow of PS1 VRAM (1024 x 512 words). */
    uint16_t vram[BG_VRAM_W * BG_VRAM_H];

    /* Rendered 320x224 RGBA8888 room preview and per-pixel pick buffers. */
    uint8_t canvasRgba[BG_CANVAS_W * BG_CANVAS_H * 4];
    int16_t pickEntry[BG_CANVAS_W * BG_CANVAS_H];
    int16_t pickTile[BG_CANVAS_W * BG_CANVAS_H];
    uint16_t pickOtZ[BG_CANVAS_W * BG_CANVAS_H];

    /* Rendered 512x256 downsampled RGBA8888 VRAM atlas preview. */
    uint8_t atlasRgba[512 * 256 * 4];
} BgEditorState;

static BgEditorState s_bgEditor = {
    .mapNumber = 5,
    .playing = 1,
    .autoActivateAnims = 1,
    .highlightSelected = 1,
    .highlightAnimated = 1,
    .selectedClutIdx = 1,
    .mouseX = -1000.0f,
    .mouseY = -1000.0f,
    .hoverCanvasX = -1,
    .hoverCanvasY = -1,
    .draggingRgbChannel = -1
};

static const char *BgEditor_RoomAreaName(int map) {
    if (map >= 1 && map <= 25) return "Carnegie Hall";
    if (map >= 26 && map <= 34) return "Sewer";
    if (map >= 41 && map <= 56) return "N.Y.P.D. #1";
    if (map >= 57 && map <= 89) return "Central Park";
    if (map >= 90 && map <= 97) return "Soho";
    if (map >= 98 && map <= 124) return "N.Y.P.D. #2";
    if (map >= 125 && map <= 160) return "St. Francis Hospital";
    if (map >= 161 && map <= 165) return "Chinatown";
    if (map >= 166 && map <= 181) return "Sewer #2";
    if (map >= 182 && map <= 190) return "Subway";
    if (map >= 191 && map <= 259) return "Museum";
    if (map >= 260 && map <= 270) return "Warehouse";
    if (map == 271) return "Aircraft Carrier";
    if (map >= 272 && map <= 273) return "Statue of Liberty";
    if (map >= 274 && map <= 287) return "Cruiser";
    if (map >= 288 && map <= 318) return "Chrysler Bldg";
    if (map >= 319 && map <= 332) return "Hospital (Late)";
    if (map == 333) return "Warehouse (Late)";
    if (map == 334 || map == 348) return "Sewer (Late)";
    if ((map >= 336 && map <= 338) || (map >= 356 && map <= 357) ||
        (map >= 369 && map <= 370) || map == 376) return "N.Y.P.D. (Late)";
    if (map == 339 || (map >= 380 && map <= 381)) return "Hospital (Late)";
    if ((map >= 340 && map <= 346) || map == 364 || map == 371 || map == 379) return "Museum (Late)";
    if (map == 349 || (map >= 425 && map <= 430)) return "Cruiser";
    if (map == 358 || (map >= 374 && map <= 375)) return "Central Park (Late)";
    if (map == 359 || (map >= 367 && map <= 368) || (map >= 372 && map <= 373) ||
        (map >= 377 && map <= 378) || map == 431) return "Theater (Late)";
    if (map == 432) return "Statue of Liberty";
    if (map >= 382 && map <= 424) return "Chrysler / Scene";
    return "Special Scene";
}

static void BgEditor_SetStatus(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(s_bgEditor.statusMsg, sizeof(s_bgEditor.statusMsg), fmt, args);
    va_end(args);
    s_bgEditor.statusExpireTick = SDL_GetTicks() + 4000;
    s_bgEditor.dirty = 1;
}

static void BgEditor_WriteShadowVramRect(const RECT *rect, const uint16_t *src) {
    if (!rect || !src || rect->w <= 0 || rect->h <= 0) return;
    for (int y = 0; y < rect->h; ++y) {
        int vy = rect->y + y;
        if (vy < 0 || vy >= BG_VRAM_H) continue;
        for (int x = 0; x < rect->w; ++x) {
            int vx = rect->x + x;
            if (vx < 0 || vx >= BG_VRAM_W) continue;
            s_bgEditor.vram[vy * BG_VRAM_W + vx] = src[y * rect->w + x];
        }
    }
}

static void BgEditor_ApplyTimUploadToShadow(const TimUploadRecord *asset, const uint8_t *base) {
    RECT rect;
    unsigned int packed = asset->image_rectangle;
    rect.x = (packed >> 10) & 0x7FF;
    rect.y = packed >> 21;
    rect.w = packed & 0x3FF;
    packed = asset->image_source.bytes.height;
    rect.h = packed != 0 ? (int)(packed & 0xFF) : 0x100;
    unsigned int image_offset = asset->image_source.offsetAndHeight & 0xFFFFFFu;
    BgEditor_WriteShadowVramRect(&rect, (const uint16_t *)(base + image_offset));

    unsigned int secondary = asset->secondary_source.offsetAndHeight & 0xFFFFFFu;
    if (secondary != 0) {
        packed = asset->secondary_rectangle;
        rect.x = (packed >> 10) & 0x7FF;
        rect.y = packed >> 21;
        rect.w = packed & 0x3FF;
        rect.h = asset->secondary_source.bytes.height;
        BgEditor_WriteShadowVramRect(&rect, (const uint16_t *)(base + image_offset + secondary));
    }
}

/* Re-reads the room's two TIM sector blocks from PE.IMG into s_bgEditor.vram
 * so that all 16-bit BGR555 words (including bit 15 STP) are 100% lossless. */
static void BgEditor_LoadShadowVramForRoom(int mapIndex) {
    if (mapIndex < 0 || mapIndex >= SCENE_ROOM_COUNT) return;
    SceneRoomSectorRange range = g_RoomSectorTable[mapIndex];
    if (!range.roomSectors) return;

    u32 base = g_GameState.pe_image_base_lba;
    u32 chunks[2][2] = {
        {base + range.start, range.scratchSectors},
        {base + range.start + range.scratchSectors, range.textureSectors}
    };
    for (int c = 0; c < 2; ++c) {
        u32 lba = chunks[c][0];
        u32 sectors = chunks[c][1];
        if (!sectors || sectors > 256) continue;
        uint8_t *buf = (uint8_t *)malloc((size_t)sectors * 2048u);
        if (!buf) continue;
        if (CdRom_ReadSectorsFromLba(lba, buf, sectors) != -1) {
            while (CdRom_PollReady() > 0) {}
            SceneAssetView *view = (SceneAssetView *)buf;
            SceneRoomDirectory *dir = SceneAsset_ResolveOffset(view, view->header.directoryOffset);
            TimUploadRecord *tim = SceneAsset_ResolveOffset(view, dir->tims & 0x3FFFFFu);
            unsigned int count = dir->tims >> 22;
            for (unsigned int i = 0; i < count; ++i) {
                BgEditor_ApplyTimUploadToShadow(&tim[i], buf);
            }
        }
        free(buf);
    }
}

static inline int BgEditor_ViewCount(void) {
    if (!g_GeomState) return 0;
    u16 view_count = 0;
    memcpy(&view_count, (const u8 *)g_GeomState + 10, sizeof(view_count));
    return (int)view_count;
}

static inline GeomCtrlEntry *BgEditor_CtrlArray(int *outCount) {
    if (!g_GeomState) {
        if (outCount) *outCount = 0;
        return NULL;
    }
    if (outCount) *outCount = (int)g_GeomState->entry_count;
    return (GeomCtrlEntry *)((u8 *)g_GeomState + g_GeomState->ctrl_offset);
}

static inline GeomEntry *BgEditor_EntryArray(int *outCount) {
    if (!g_GeomState) {
        if (outCount) *outCount = 0;
        return NULL;
    }
    if (outCount) *outCount = (int)g_GeomState->entry_count06;
    return (GeomEntry *)((u8 *)g_GeomState + g_GeomState->entry_offset);
}

static inline CameraViewport *BgEditor_ViewportArray(int *outCount) {
    if (!g_GeomState) {
        if (outCount) *outCount = 0;
        return NULL;
    }
    if (outCount) *outCount = BgEditor_ViewCount();
    return (CameraViewport *)((u8 *)g_GeomState + g_GeomState->entry_offset_1C);
}

static inline GeomAnimationSlot *BgEditor_CtrlSlots(GeomCtrlEntry *ctrl, int *outFrames) {
    if (!ctrl) {
        if (outFrames) *outFrames = 0;
        return NULL;
    }
    if (outFrames) *outFrames = (int)(ctrl->head.packed >> 8);
    return (GeomAnimationSlot *)((u8 *)ctrl + ctrl->slot_offset);
}

static int BgEditor_FindControllerForEntry(int entryIndex, int group) {
    int ctrlCount = 0;
    GeomCtrlEntry *ctrls = BgEditor_CtrlArray(&ctrlCount);
    if (!ctrls) return -1;
    for (int c = 0; c < ctrlCount; ++c) {
        if ((int)ctrls[c].position.bits.group != group) continue;
        int frames = 0;
        GeomAnimationSlot *slots = BgEditor_CtrlSlots(&ctrls[c], &frames);
        for (int s = 0; s < frames; ++s) {
            if ((int)slots[s].entry == entryIndex) {
                return c;
            }
        }
    }
    return -1;
}

static int BgEditor_GroupHasAnimations(int group) {
    int ctrlCount = 0;
    GeomCtrlEntry *ctrls = BgEditor_CtrlArray(&ctrlCount);
    if (ctrls) {
        for (int c = 0; c < ctrlCount; ++c) {
            if ((int)ctrls[c].position.bits.group == group && (ctrls[c].head.packed >> 8) > 0) {
                return 1;
            }
        }
    }
    int entryCount = 0;
    GeomEntry *entries = BgEditor_EntryArray(&entryCount);
    if (entries) {
        for (int i = 0; i < entryCount; ++i) {
            if (entries[i].group == group && (entries[i].head.b.flags & 0x0C)) {
                return 1;
            }
        }
    }
    return 0;
}

static int BgEditor_ActivateGroupAnimations(int group) {
    int ctrlCount = 0;
    GeomCtrlEntry *ctrls = BgEditor_CtrlArray(&ctrlCount);
    int entryCount = 0;
    GeomEntry *entries = BgEditor_EntryArray(&entryCount);
    int activated = 0;
    if (!ctrls || !entries) return 0;

    for (int c = 0; c < ctrlCount; ++c) {
        GeomCtrlEntry *ctrl = &ctrls[c];
        if ((int)ctrl->position.bits.group != group) continue;
        int frames = 0;
        GeomAnimationSlot *slots = BgEditor_CtrlSlots(ctrl, &frames);
        if (frames <= 0 || !slots) continue;

        for (int s = 0; s < frames; ++s) {
            if (slots[s].duration <= 0) {
                slots[s].duration = 6;
            }
            if (slots[s].entry < entryCount) {
                entries[slots[s].entry].head.b.flags &= ~2u;
            }
        }
        int curSlot = (ctrl->position.bits.value >> 8);
        if (curSlot < 0 || curSlot >= frames) {
            ctrl->position.bits.value = 0;
            curSlot = 0;
        }
        if (slots[curSlot].entry < entryCount) {
            entries[slots[curSlot].entry].head.b.flags |= 2u;
        }
        if (ctrl->step == 0) {
            ctrl->step = 0x100;
        }
        ctrl->head.b.flags |= 0x26u; /* bit 1 (active) + bit 2 (play) + bit 5 (loop) */
        activated++;
    }
    return activated;
}

static void BgEditor_RebuildEntryPackets(GeomEntry *entry) {
    if (!entry || !entry->u30.prim || !entry->u34.pagePackets || !entry->prim_count) return;
    uintptr_t baseAddr = (uintptr_t)entry;
    u32 *positions = (u32 *)(baseAddr + entry->u28.pos_ptr);
    RenderTileTexture *textures = (RenderTileTexture *)(baseAddr + entry->u2C.uv_ptr);
    unsigned int count = entry->prim_count;

    for (unsigned int pass = 0; pass < 2; ++pass) {
        RenderTilePacket *sprite = (RenderTilePacket *)entry->u30.prim + pass * count;
        RenderTexturePagePacket *page = entry->u34.pagePackets + pass * count;
        for (unsigned int i = 0; i < count; ++i) {
            u32 pos = positions[i];
            RenderTileTexture *tex = &textures[i];
            sprite[i].x = entry->disp_x + (pos >> 22);
            sprite[i].y = entry->disp_y + ((pos >> 12) & 0x3FF);
            sprite[i].u = tex->bytes[4];
            sprite[i].v = tex->bytes[3];
            if (tex->words[1] & 0x10000000u)
                sprite[i].code |= 2;
            else
                sprite[i].code &= ~2;
            sprite[i].clut = GetClut((tex->words[0] >> 5) & 0x3F0, tex->words[0] & 0x1FF);
            u32 word = tex->words[0];
            page[i].command = (GetTPage(1, (word >> 22) & 3, (word >> 10) & 0x3C0, (word >> 7) & 0x100) & 0x9FF) | 0xE1000600u;
        }
    }
}

static void BgEditor_SelectDefaultEntryForGroup(int group) {
    int entryCount = 0;
    GeomEntry *entries = BgEditor_EntryArray(&entryCount);
    s_bgEditor.selectedEntry = -1;
    s_bgEditor.selectedTile = 0;
    if (!entries) return;

    for (int i = 0; i < entryCount; ++i) {
        if (entries[i].group == group && (entries[i].head.b.flags & 2) && entries[i].prim_count > 0) {
            s_bgEditor.selectedEntry = i;
            break;
        }
    }
    if (s_bgEditor.selectedEntry < 0) {
        for (int i = 0; i < entryCount; ++i) {
            if (entries[i].group == group && entries[i].prim_count > 0) {
                s_bgEditor.selectedEntry = i;
                break;
            }
        }
    }

    int ctrlCount = 0;
    GeomCtrlEntry *ctrls = BgEditor_CtrlArray(&ctrlCount);
    s_bgEditor.selectedCtrl = -1;
    if (ctrls) {
        for (int c = 0; c < ctrlCount; ++c) {
            if ((int)ctrls[c].position.bits.group == group) {
                s_bgEditor.selectedCtrl = c;
                break;
            }
        }
    }
}

static void BgEditor_SwitchGroup(int newGroup) {
    int viewCount = BgEditor_ViewCount();
    if (viewCount <= 0) return;
    while (newGroup < 0) newGroup += viewCount;
    newGroup %= viewCount;

    Render_LoadGeomState(newGroup);
    int vCount = 0;
    CameraViewport *views = BgEditor_ViewportArray(&vCount);
    if (views && newGroup < vCount) {
        CameraViewport *view = &views[newGroup];
        int cx = (view->minX + view->maxX) / 2;
        int cy = (view->minY + view->maxY) / 2;
        g_GeomScroll.state.position.x = g_GeomScroll.state.position.originX = (s16)cx;
        g_GeomScroll.state.position.y = g_GeomScroll.state.position.originY = (s16)cy;
        g_GeomScroll.state.position.boundsMinX = view->minX;
        g_GeomScroll.state.position.boundsMaxX = view->maxX;
        g_GeomScroll.state.position.boundsMinY = view->minY;
        g_GeomScroll.state.position.boundsMaxY = view->maxY;
    }
    Render_ResetAllEntries();
    if (s_bgEditor.autoActivateAnims) {
        BgEditor_ActivateGroupAnimations(newGroup);
    }
    BgEditor_SelectDefaultEntryForGroup(newGroup);
    s_bgEditor.entryListScroll = 0;
    s_bgEditor.dirty = 1;
}

static int BgEditor_LoadRoom(int mapNumber, int preserveLiveState) {
    if (mapNumber < 1) mapNumber = 1;
    if (mapNumber > SCENE_ROOM_COUNT) mapNumber = SCENE_ROOM_COUNT;

    /* If the requested map slot is empty on disc, find the nearest populated room. */
    if (!g_RoomSectorTable[mapNumber - 1].roomSectors) {
        int found = -1;
        for (int i = 0; i < SCENE_ROOM_COUNT; ++i) {
            int candidate = ((mapNumber - 1 + i) % SCENE_ROOM_COUNT) + 1;
            if (g_RoomSectorTable[candidate - 1].roomSectors) {
                found = candidate;
                break;
            }
        }
        if (found < 0) return -1;
        mapNumber = found;
    }

    if (!preserveLiveState || !g_GeomState) {
        memset(s_bgEditor.vram, 0, sizeof(s_bgEditor.vram));
        D_8009CDC8 = 'm';
        char name[16];
        snprintf(name, sizeof(name), "m%04ui", (unsigned)mapNumber);
        u32 token = (u32)Text_ParseBase32Id((signed char *)name);
        g_SceneDispatchToken = (int)token;
        if (Scene_LoadRoom(token) != 0) {
            return -2;
        }
        Scene_StreamTexturePage();
        Render_PrepareFrame();
        g_GeomState->field26 = 0x800;
    }

    BgEditor_LoadShadowVramForRoom(mapNumber - 1);
    s_bgEditor.mapNumber = mapNumber;
    s_bgEditor.entryListScroll = 0;

    int group = g_GeomScroll.state.position.groupSelect;
    /* If the initial camera group has no animations, check if another camera group in this room does! */
    if (!preserveLiveState && !BgEditor_GroupHasAnimations(group)) {
        int viewCount = BgEditor_ViewCount();
        for (int g = 0; g < viewCount; ++g) {
            if (BgEditor_GroupHasAnimations(g)) {
                BgEditor_SwitchGroup(g);
                group = g;
                break;
            }
        }
    }

    int anims = 0;
    if (!preserveLiveState && s_bgEditor.autoActivateAnims) {
        anims = BgEditor_ActivateGroupAnimations(group);
    }
    BgEditor_SelectDefaultEntryForGroup(group);
    s_bgEditor.dirty = 1;

    int ctrlCount = 0, entryCount = 0;
    BgEditor_CtrlArray(&ctrlCount);
    BgEditor_EntryArray(&entryCount);
    BgEditor_SetStatus("Loaded M%03d (%s) — Cam %d/%d, %d layers, %d anim controllers (%d active)",
                       mapNumber, BgEditor_RoomAreaName(mapNumber),
                       group + 1, BgEditor_ViewCount(), entryCount, ctrlCount, anims);
    return 0;
}

static void BgEditor_StepAdjacentRoom(int delta, int requireAnimations) {
    int cur = s_bgEditor.mapNumber;
    int step = delta >= 0 ? 1 : -1;

    for (int attempt = 0; attempt < SCENE_ROOM_COUNT; ++attempt) {
        cur += (attempt == 0) ? delta : step;
        while (cur < 1) cur += SCENE_ROOM_COUNT;
        while (cur > SCENE_ROOM_COUNT) cur -= SCENE_ROOM_COUNT;
        if (!g_RoomSectorTable[cur - 1].roomSectors) continue;
        if (BgEditor_LoadRoom(cur, 0) == 0) {
            if (!requireAnimations) return;
            int ctrlCount = 0, entryCount = 0;
            BgEditor_CtrlArray(&ctrlCount);
            GeomEntry *entries = BgEditor_EntryArray(&entryCount);
            int hasScroll = 0;
            for (int i = 0; i < entryCount; ++i) {
                if (entries[i].head.b.flags & 0x0C) {
                    hasScroll = 1;
                    break;
                }
            }
            if (ctrlCount > 0 || hasScroll) {
                return;
            }
        }
    }
}

static int BgEditor_FindLayer1Entry(int group) {
    int entryCount = 0;
    GeomEntry *entries = BgEditor_EntryArray(&entryCount);
    int bestIdx = -1;
    unsigned int bestCount = 0;
    if (!entries) return -1;
    for (int i = 0; i < entryCount; ++i) {
        if (entries[i].group == group && entries[i].prim_count > bestCount) {
            bestCount = entries[i].prim_count;
            bestIdx = i;
        }
    }
    return bestIdx;
}

typedef struct BgTileDrawItem {
    int16_t entryIdx;
    uint16_t tileIdx;
    int16_t drawX;
    int16_t drawY;
    uint16_t otIndex;
    uint8_t u, v;
    uint16_t tpageX, tpageY;
    uint16_t clutX, clutY;
    uint8_t semiTrans;
    uint8_t abr;
} BgTileDrawItem;

static inline void BgEditor_Bgr555ToRgb8(uint16_t c, uint8_t *r, uint8_t *g, uint8_t *b) {
    uint8_t r5 = c & 0x1Fu;
    uint8_t g5 = (c >> 5) & 0x1Fu;
    uint8_t b5 = (c >> 10) & 0x1Fu;
    *r = (uint8_t)((r5 << 3) | (r5 >> 2));
    *g = (uint8_t)((g5 << 3) | (g5 >> 2));
    *b = (uint8_t)((b5 << 3) | (b5 >> 2));
}

static void BgEditor_DepthColor(uint16_t otZ, uint16_t minZ, uint16_t maxZ,
                                uint8_t *r, uint8_t *g, uint8_t *b) {
    float t = (maxZ > minZ) ? (float)(otZ - minZ) / (float)(maxZ - minZ) : 0.5f;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    float nearness = 1.0f - t;
    *r = (uint8_t)(30.0f + 225.0f * nearness);
    *g = (uint8_t)(40.0f + 180.0f * (1.0f - fabsf(nearness - 0.5f) * 1.6f));
    *b = (uint8_t)(60.0f + 195.0f * t);
}

static void BgEditor_CompositeCanvas(void) {
    for (int y = 0; y < BG_CANVAS_H; ++y) {
        for (int x = 0; x < BG_CANVAS_W; ++x) {
            int idx = y * BG_CANVAS_W + x;
            uint8_t checker = (((x >> 3) ^ (y >> 3)) & 1) ? 30 : 20;
            s_bgEditor.canvasRgba[idx * 4 + 0] = (s_bgEditor.viewMode == BG_VIEW_LAYER2_ANIM) ? checker : 8;
            s_bgEditor.canvasRgba[idx * 4 + 1] = (s_bgEditor.viewMode == BG_VIEW_LAYER2_ANIM) ? checker : 10;
            s_bgEditor.canvasRgba[idx * 4 + 2] = (s_bgEditor.viewMode == BG_VIEW_LAYER2_ANIM) ? (uint8_t)(checker + 8) : 14;
            s_bgEditor.canvasRgba[idx * 4 + 3] = 255;
            s_bgEditor.pickEntry[idx] = -1;
            s_bgEditor.pickTile[idx] = -1;
            s_bgEditor.pickOtZ[idx] = 0;
        }
    }

    if (!g_GeomState) return;

    int group = g_GeomScroll.state.position.groupSelect;
    int entryCount = 0;
    GeomEntry *entries = BgEditor_EntryArray(&entryCount);
    if (!entries || entryCount <= 0) return;

    g_GeomState->out_disp_x = g_GeomState->disp_src_x - ((u16)g_GeomScroll.state.position.x - 160);
    g_GeomState->out_disp_y = g_GeomState->disp_src_y - ((u16)g_GeomScroll.state.position.y - 112);
    if (g_GeomState->field26 == 0) {
        g_GeomState->field26 = 0x800;
    }

    int layer1Idx = BgEditor_FindLayer1Entry(group);

    size_t maxTiles = 0;
    for (int i = 0; i < entryCount; ++i) {
        if ((entries[i].head.b.flags & 2) && entries[i].group == group) {
            maxTiles += entries[i].prim_count;
        }
    }
    if (maxTiles == 0) return;

    BgTileDrawItem *items = (BgTileDrawItem *)malloc(maxTiles * sizeof(BgTileDrawItem));
    if (!items) return;
    size_t itemCount = 0;
    uint16_t minOtZ = 0xFFFFu, maxOtZ = 0;

    for (int eIdx = 0; eIdx < entryCount; ++eIdx) {
        GeomEntry *entry = &entries[eIdx];
        if (!(entry->head.b.flags & 2) || entry->group != group || entry->prim_count == 0) {
            continue;
        }
        if (s_bgEditor.viewMode == BG_VIEW_LAYER1 && eIdx != layer1Idx) {
            continue;
        }
        if (s_bgEditor.viewMode == BG_VIEW_LAYER2_ANIM && eIdx == layer1Idx) {
            continue;
        }

        s32 raw_x = (s32)entry->scr_x + (u16)g_GeomState->out_disp_x;
        s32 raw_y = (s32)entry->scr_y + (u16)g_GeomState->out_disp_y;
        s32 scroll_x = raw_x;
        s32 scroll_y = raw_y;
        s32 texture_base = (s32)g_GeomState->field26 + (s32)((entry->head.packed >> 8) & 0xFFFu);
        u32 flags = entry->head.b.flags;

        if ((flags & 4) && entry->anim_mod_x != 0 && entry->anim_mod_y != 0) {
            s32 mod_x = entry->anim_mod_x;
            s32 divisor_x = mod_x & 0xFFFF;
            s32 signed_x = (s16)raw_x - 320 + divisor_x;
            scroll_x = (signed_x % divisor_x) - (mod_x - 320);

            s32 mod_y = entry->anim_mod_y;
            s32 divisor_y = mod_y & 0xFFFF;
            s32 signed_y = (s16)raw_y - 224 + divisor_y;
            scroll_y = (signed_y % divisor_y) - (mod_y - 224);
        }

        uintptr_t baseAddr = (uintptr_t)entry;
        u32 *positions = (u32 *)(baseAddr + entry->u28.pos_ptr);
        RenderTileTexture *textures = (RenderTileTexture *)(baseAddr + entry->u2C.uv_ptr);

        for (unsigned int t = 0; t < entry->prim_count; ++t) {
            u32 word = positions[t];
            s32 draw_x = scroll_x + (word >> 22);
            s32 draw_y = scroll_y + ((word >> 12) & 0x3FF);
            if ((flags & 4) && entry->anim_mod_x != 0 && entry->anim_mod_y != 0) {
                if ((s16)draw_x >= 320) draw_x -= entry->anim_mod_x;
                else if ((s16)draw_x < -15) draw_x += entry->anim_mod_x;
                if ((s16)draw_y >= 224) draw_y -= entry->anim_mod_y;
                else if ((s16)draw_y < -15) draw_y += entry->anim_mod_y;
            }
            if ((u16)(draw_x + 15) >= 0x14Fu || (u16)(draw_y + 15) >= 0xEFu) {
                continue;
            }
            s32 tile_u = texture_base + (word & 0xFFFu);
            uint16_t otIndex = (((tile_u - 8) & 0xFFFFu) < 0xFF1u) ? (uint16_t)tile_u : 0x800u;
            if (otIndex < minOtZ) minOtZ = otIndex;
            if (otIndex > maxOtZ) maxOtZ = otIndex;

            RenderTileTexture *tex = &textures[t];
            u32 w0 = tex->words[0];
            items[itemCount++] = (BgTileDrawItem){
                .entryIdx = (int16_t)eIdx,
                .tileIdx = (uint16_t)t,
                .drawX = (int16_t)draw_x,
                .drawY = (int16_t)draw_y,
                .otIndex = otIndex,
                .u = tex->bytes[4],
                .v = tex->bytes[3],
                .tpageX = (uint16_t)((w0 >> 10) & 0x3C0u),
                .tpageY = (uint16_t)((w0 >> 7) & 0x100u),
                .clutX = (uint16_t)((w0 >> 5) & 0x3F0u),
                .clutY = (uint16_t)(w0 & 0x1FFu),
                .semiTrans = (uint8_t)((tex->words[1] & 0x10000000u) != 0),
                .abr = (uint8_t)((w0 >> 22) & 3u)
            };
        }
    }

    for (size_t i = 1; i < itemCount; ++i) {
        BgTileDrawItem key = items[i];
        size_t j = i;
        while (j > 0) {
            BgTileDrawItem prev = items[j - 1];
            int swap = 0;
            if (key.otIndex > prev.otIndex) {
                swap = 1;
            } else if (key.otIndex == prev.otIndex) {
                swap = 1;
            }
            if (!swap) break;
            items[j] = prev;
            --j;
        }
        items[j] = key;
    }

    for (size_t i = 0; i < itemCount; ++i) {
        const BgTileDrawItem *it = &items[i];
        for (int ty = 0; ty < 16; ++ty) {
            int sy = it->drawY + ty;
            if (sy < 0 || sy >= BG_CANVAS_H) continue;
            int vramY = (it->tpageY + it->v + ty) & (BG_VRAM_H - 1);
            for (int tx = 0; tx < 16; ++tx) {
                int sx = it->drawX + tx;
                if (sx < 0 || sx >= BG_CANVAS_W) continue;

                int texelU = it->u + tx;
                int vramX = (it->tpageX + (texelU >> 1)) & (BG_VRAM_W - 1);
                uint16_t packed = s_bgEditor.vram[vramY * BG_VRAM_W + vramX];
                uint8_t index8 = (texelU & 1) ? (uint8_t)(packed >> 8) : (uint8_t)(packed & 0xFFu);
                int cx = (it->clutX + index8) & (BG_VRAM_W - 1);
                int cy = it->clutY & (BG_VRAM_H - 1);
                uint16_t clutWord = s_bgEditor.vram[cy * BG_VRAM_W + cx];

                if (clutWord == 0x0000u) continue;

                int pixIdx = sy * BG_CANVAS_W + sx;
                uint8_t *dst = &s_bgEditor.canvasRgba[pixIdx * 4];

                if (s_bgEditor.viewMode == BG_VIEW_ZDEPTH) {
                    BgEditor_DepthColor(it->otIndex, minOtZ, maxOtZ, &dst[0], &dst[1], &dst[2]);
                } else {
                    uint8_t fr, fg, fb;
                    BgEditor_Bgr555ToRgb8(clutWord, &fr, &fg, &fb);
                    if (it->semiTrans && (clutWord & 0x8000u)) {
                        int br = dst[0], bg = dst[1], bb = dst[2];
                        int rr = fr, rg = fg, rb = fb;
                        switch (it->abr & 3u) {
                        case 0:
                            rr = (br + fr) >> 1;
                            rg = (bg + fg) >> 1;
                            rb = (bb + fb) >> 1;
                            break;
                        case 1:
                            rr = br + fr; if (rr > 255) rr = 255;
                            rg = bg + fg; if (rg > 255) rg = 255;
                            rb = bb + fb; if (rb > 255) rb = 255;
                            break;
                        case 2:
                            rr = br - fr; if (rr < 0) rr = 0;
                            rg = bg - fg; if (rg < 0) rg = 0;
                            rb = bb - fb; if (rb < 0) rb = 0;
                            break;
                        case 3:
                            rr = br + (fr >> 2); if (rr > 255) rr = 255;
                            rg = bg + (fg >> 2); if (rg > 255) rg = 255;
                            rb = bb + (fb >> 2); if (rb > 255) rb = 255;
                            break;
                        }
                        dst[0] = (uint8_t)rr;
                        dst[1] = (uint8_t)rg;
                        dst[2] = (uint8_t)rb;
                    } else {
                        dst[0] = fr;
                        dst[1] = fg;
                        dst[2] = fb;
                    }
                }
                s_bgEditor.pickEntry[pixIdx] = it->entryIdx;
                s_bgEditor.pickTile[pixIdx] = (int16_t)it->tileIdx;
                s_bgEditor.pickOtZ[pixIdx] = it->otIndex;
            }
        }
    }

    for (size_t i = 0; i < itemCount; ++i) {
        const BgTileDrawItem *it = &items[i];
        int isSelectedLayer = s_bgEditor.highlightSelected && (it->entryIdx == s_bgEditor.selectedEntry);
        int isSelectedTile = (it->entryIdx == s_bgEditor.selectedEntry && (int)it->tileIdx == s_bgEditor.selectedTile);
        int isAnimatedLayer = s_bgEditor.highlightAnimated &&
            (BgEditor_FindControllerForEntry(it->entryIdx, group) >= 0 ||
             (entries[it->entryIdx].head.b.flags & 0x0C));

        if (!isSelectedLayer && !isSelectedTile && !isAnimatedLayer && !s_bgEditor.showGrid) {
            continue;
        }
        for (int p = 0; p < 16; ++p) {
            int coords[4][2] = {
                {it->drawX + p, it->drawY},
                {it->drawX + p, it->drawY + 15},
                {it->drawX, it->drawY + p},
                {it->drawX + 15, it->drawY + p}
            };
            for (int edge = 0; edge < 4; ++edge) {
                int sx = coords[edge][0], sy = coords[edge][1];
                if (sx < 0 || sx >= BG_CANVAS_W || sy < 0 || sy >= BG_CANVAS_H) continue;
                uint8_t *dst = &s_bgEditor.canvasRgba[(sy * BG_CANVAS_W + sx) * 4];
                if (isSelectedTile) {
                    dst[0] = 255; dst[1] = 215; dst[2] = 40;
                } else if (isSelectedLayer) {
                    dst[0] = (uint8_t)((dst[0] + 90) >> 1);
                    dst[1] = (uint8_t)((dst[1] + 180) >> 1);
                    dst[2] = (uint8_t)((dst[2] + 255) >> 1);
                } else if (isAnimatedLayer) {
                    dst[0] = (uint8_t)((dst[0] + 60) >> 1);
                    dst[1] = (uint8_t)((dst[1] + 240) >> 1);
                    dst[2] = (uint8_t)((dst[2] + 140) >> 1);
                } else if (s_bgEditor.showGrid && (edge == 0 || edge == 2)) {
                    dst[0] = (uint8_t)((dst[0] * 3 + 180) >> 2);
                    dst[1] = (uint8_t)((dst[1] * 3 + 190) >> 2);
                    dst[2] = (uint8_t)((dst[2] * 3 + 210) >> 2);
                }
            }
        }
    }

    free(items);
}

static int BgEditor_GetSelectedTileInfo(int *outTx, int *outTy, int *outU, int *outV,
                                        int *outClutX, int *outClutY,
                                        u32 **outPosWord, RenderTileTexture **outTex) {
    int entryCount = 0;
    GeomEntry *entries = BgEditor_EntryArray(&entryCount);
    if (!entries || s_bgEditor.selectedEntry < 0 || s_bgEditor.selectedEntry >= entryCount) {
        return 0;
    }
    GeomEntry *entry = &entries[s_bgEditor.selectedEntry];
    if (entry->prim_count == 0) return 0;
    if (s_bgEditor.selectedTile < 0 || s_bgEditor.selectedTile >= (int)entry->prim_count) {
        s_bgEditor.selectedTile = 0;
    }
    uintptr_t baseAddr = (uintptr_t)entry;
    u32 *positions = (u32 *)(baseAddr + entry->u28.pos_ptr);
    RenderTileTexture *textures = (RenderTileTexture *)(baseAddr + entry->u2C.uv_ptr);
    RenderTileTexture *tex = &textures[s_bgEditor.selectedTile];
    u32 w0 = tex->words[0];

    if (outTx) *outTx = (int)((w0 >> 10) & 0x3C0u);
    if (outTy) *outTy = (int)((w0 >> 7) & 0x100u);
    if (outU) *outU = tex->bytes[4];
    if (outV) *outV = tex->bytes[3];
    if (outClutX) *outClutX = (int)((w0 >> 5) & 0x3F0u);
    if (outClutY) *outClutY = (int)(w0 & 0x1FFu);
    if (outPosWord) *outPosWord = &positions[s_bgEditor.selectedTile];
    if (outTex) *outTex = tex;
    return 1;
}

static void BgEditor_RenderVramAtlas(void) {
    int selTx = 320, selTy = 256, selU = 0, selV = 0, clutX = 0, clutY = 480;
    BgEditor_GetSelectedTileInfo(&selTx, &selTy, &selU, &selV, &clutX, &clutY, NULL, NULL);

    for (int ay = 0; ay < 256; ++ay) {
        int vy = ay * 2;
        for (int ax = 0; ax < 512; ++ax) {
            int vx = ax * 2;
            uint8_t r = 0, g = 0, b = 0;
            if (vy < 480 && vx >= 320) {
                uint16_t packed = s_bgEditor.vram[vy * BG_VRAM_W + vx];
                uint8_t idx8 = (uint8_t)(packed & 0xFFu);
                uint16_t cw = s_bgEditor.vram[(clutY & 511) * BG_VRAM_W + ((clutX + idx8) & 1023)];
                BgEditor_Bgr555ToRgb8(cw, &r, &g, &b);
            } else {
                uint16_t w = s_bgEditor.vram[vy * BG_VRAM_W + vx];
                BgEditor_Bgr555ToRgb8(w, &r, &g, &b);
            }
            uint8_t *dst = &s_bgEditor.atlasRgba[(ay * 512 + ax) * 4];
            dst[0] = r; dst[1] = g; dst[2] = b; dst[3] = 255;
        }
    }
}

static int BgEditor_WriteBmp(const char *path, const uint8_t *rgba, int w, int h) {
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    int rowStride = (w * 3 + 3) & ~3;
    uint32_t dataSize = (uint32_t)(rowStride * h);
    uint32_t fileSize = 54u + dataSize;
    uint8_t header[54] = {0};
    header[0] = 'B'; header[1] = 'M';
    memcpy(header + 2, &fileSize, 4);
    uint32_t pixelOffset = 54u;
    memcpy(header + 10, &pixelOffset, 4);
    uint32_t dibSize = 40u;
    memcpy(header + 14, &dibSize, 4);
    int32_t bw = w, bh = -h;
    memcpy(header + 18, &bw, 4);
    memcpy(header + 22, &bh, 4);
    uint16_t planes = 1, bpp = 24;
    memcpy(header + 26, &planes, 2);
    memcpy(header + 28, &bpp, 2);
    memcpy(header + 34, &dataSize, 4);
    fwrite(header, 1, sizeof(header), f);

    uint8_t *row = (uint8_t *)calloc(1, (size_t)rowStride);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const uint8_t *p = &rgba[(y * w + x) * 4];
            row[x * 3 + 0] = p[2];
            row[x * 3 + 1] = p[1];
            row[x * 3 + 2] = p[0];
        }
        fwrite(row, 1, (size_t)rowStride, f);
    }
    free(row);
    fclose(f);
    return 1;
}

static void BgEditor_ExportCurrentRoomBmp(int exportAllAnimFrames) {
#ifdef _WIN32
    mkdir("bg_exports");
#else
    mkdir("bg_exports", 0755);
#endif
    int group = g_GeomScroll.state.position.groupSelect;
    if (!exportAllAnimFrames) {
        BgEditor_CompositeCanvas();
        char path[256];
        snprintf(path, sizeof(path), "bg_exports/M%03d_group%d_view%d.bmp",
                 s_bgEditor.mapNumber, group, s_bgEditor.viewMode);
        if (BgEditor_WriteBmp(path, s_bgEditor.canvasRgba, BG_CANVAS_W, BG_CANVAS_H)) {
            BgEditor_SetStatus("Exported %s", path);
        } else {
            BgEditor_SetStatus("Failed to export %s", path);
        }
        return;
    }

    int savedMode = s_bgEditor.viewMode;
    s_bgEditor.viewMode = BG_VIEW_COMPOSITE;
    for (int f = 0; f < 8; ++f) {
        BgEditor_CompositeCanvas();
        char path[256];
        snprintf(path, sizeof(path), "bg_exports/M%03d_group%d_frame%02d.bmp",
                 s_bgEditor.mapNumber, group, f);
        BgEditor_WriteBmp(path, s_bgEditor.canvasRgba, BG_CANVAS_W, BG_CANVAS_H);
        for (int step = 0; step < 6; ++step) {
            Render_StepGroupAnimations();
            Render_ScrollEntries();
        }
    }
    s_bgEditor.viewMode = savedMode;
    BgEditor_SetStatus("Exported 8 frames to bg_exports/M%03d_group%d_frame00..07.bmp",
                       s_bgEditor.mapNumber, group);
}

/* ---- Fast Scanline UI Primitives (no per-pixel sqrtf!) ---- */

static const Color kBgCard = {0.105f, 0.118f, 0.148f, 0.96f};
static const Color kBgCardInner = {0.068f, 0.076f, 0.096f, 0.96f};
static const Color kGreen = {0.22f, 0.82f, 0.52f, 1.0f};
static const Color kGold = {1.0f, 0.82f, 0.24f, 1.0f};

static void BgEditor_FastFillRect(OverlaySurface *s, float fx, float fy, float fw, float fh, Color c) {
    if (c.a <= 0.0f || fw <= 0.0f || fh <= 0.0f) return;
    int x0 = (int)floorf(fx);
    int y0 = (int)floorf(fy);
    int x1 = (int)ceilf(fx + fw);
    int y1 = (int)ceilf(fy + fh);
    int origY0 = y0, origY1 = y1;
    int origX0 = x0, origX1 = x1;

    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > s->w) x1 = s->w;
    if (y1 > s->h - 1) y1 = s->h - 1;
    if (x0 >= x1 || y0 >= y1) return;

    float a = c.a;
    float keep = 1.0f - a;
    uint8_t cr = (uint8_t)(c.r * 255.0f + 0.5f);
    uint8_t cg = (uint8_t)(c.g * 255.0f + 0.5f);
    uint8_t cb = (uint8_t)(c.b * 255.0f + 0.5f);
    uint8_t ca = (uint8_t)(a * 255.0f + 0.5f);

    for (int y = y0; y < y1; ++y) {
        /* 1px corner bevel for a clean rounded look without per-pixel SDF */
        int bevel = ((y == origY0 || y == origY1 - 1) && (origX1 - origX0 > 6) && (origY1 - origY0 > 6)) ? 1 : 0;
        int rx0 = x0 + (bevel && x0 == origX0 ? 1 : 0);
        int rx1 = x1 - (bevel && x1 == origX1 ? 1 : 0);
        uint8_t *row = s->pixels + ((size_t)y * (size_t)s->w + (size_t)rx0) * 4u;
        if (a >= 0.995f) {
            for (int x = rx0; x < rx1; ++x) {
                row[0] = cr; row[1] = cg; row[2] = cb; row[3] = 255;
                row += 4;
            }
        } else {
            float rPremul = c.r * a * 255.0f;
            float gPremul = c.g * a * 255.0f;
            float bPremul = c.b * a * 255.0f;
            float aPremul = a * 255.0f;
            for (int x = rx0; x < rx1; ++x) {
                row[0] = (uint8_t)(rPremul + row[0] * keep + 0.5f);
                row[1] = (uint8_t)(gPremul + row[1] * keep + 0.5f);
                row[2] = (uint8_t)(bPremul + row[2] * keep + 0.5f);
                row[3] = (uint8_t)(aPremul + row[3] * keep + 0.5f);
                row += 4;
            }
        }
        (void)ca;
    }
}

static void BgEditor_FastStrokeRect(OverlaySurface *s, float fx, float fy, float fw, float fh, float thick, Color c) {
    if (thick < 1.0f) thick = 1.0f;
    BgEditor_FastFillRect(s, fx, fy, fw, thick, c);
    BgEditor_FastFillRect(s, fx, fy + fh - thick, fw, thick, c);
    BgEditor_FastFillRect(s, fx, fy + thick, thick, fh - 2.0f * thick, c);
    BgEditor_FastFillRect(s, fx + fw - thick, fy + thick, thick, fh - 2.0f * thick, c);
}

static void BgEditor_DrawTextClipped(OverlaySurface *s, float x, float baseline, float maxW,
                                     float em, const char *text, Color c, TextAlign align) {
    if (!text || !*text || maxW <= 4.0f) return;
    float fullW = TextWidth(text, em);
    if (fullW <= maxW) {
        DrawText(s, x, baseline, em, text, c, align);
        return;
    }
    char clipped[160];
    size_t len = strlen(text);
    if (len >= sizeof(clipped)) len = sizeof(clipped) - 1;
    memcpy(clipped, text, len);
    clipped[len] = '\0';
    while (len > 1 && TextWidth(clipped, em) > maxW) {
        --len;
        clipped[len] = '\0';
    }
    DrawText(s, x, baseline, em, clipped, c, align);
}

static void BgEditor_BlitNearest(OverlaySurface *surface, int dstX, int dstY, int dstW, int dstH,
                                 const uint8_t *srcRgba, int srcW, int srcH) {
    if (dstW <= 0 || dstH <= 0 || srcW <= 0 || srcH <= 0) return;
    for (int y = 0; y < dstH; ++y) {
        int py = dstY + y;
        if (py < 0 || py >= surface->h - 1) continue;
        int sy = (y * srcH) / dstH;
        const uint8_t *srcRow = &srcRgba[sy * srcW * 4];
        uint8_t *dstRow = &surface->pixels[py * surface->w * 4];
        for (int x = 0; x < dstW; ++x) {
            int px = dstX + x;
            if (px < 0 || px >= surface->w) continue;
            int sx = (x * srcW) / dstW;
            const uint8_t *s = &srcRow[sx * 4];
            uint8_t *d = &dstRow[px * 4];
            d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = 255;
        }
    }
}

static int BgEditor_HitRect(float px, float py, float x, float y, float w, float h) {
    return px >= x && px < (x + w) && py >= y && py < (y + h);
}

static int BgEditor_RegisterWidget(BgActionId action, int param, float x, float y, float w, float h) {
    if (s_bgEditor.widgetCount < BG_MAX_WIDGETS) {
        s_bgEditor.widgets[s_bgEditor.widgetCount++] = (BgWidget){action, param, x, y, w, h};
    }
    return BgEditor_HitRect(s_bgEditor.mouseX, s_bgEditor.mouseY, x, y, w, h);
}

static void BgEditor_Button(OverlaySurface *surface, float s,
                            BgActionId action, int param,
                            float x, float y, float w, float h,
                            const char *label, int active, int accent) {
    int hovered = BgEditor_RegisterWidget(action, param, x, y, w, h);
    Color bg;
    if (active) {
        bg = accent ? WithAlpha(kAccent, hovered ? 0.42f : 0.30f)
                    : WithAlpha(kText, hovered ? 0.24f : 0.16f);
    } else if (hovered) {
        bg = accent ? WithAlpha(kAccent, 0.18f) : WithAlpha(kText, 0.13f);
    } else {
        bg = WithAlpha(kText, 0.06f);
    }
    Color border = active ? kAccent : (hovered ? WithAlpha(kAccent, 0.75f) : WithAlpha(kText, 0.15f));
    BgEditor_FastFillRect(surface, x * s, y * s, w * s, h * s, bg);
    BgEditor_FastStrokeRect(surface, x * s, y * s, w * s, h * s, (hovered || active) ? 1.5f * s : 1.0f * s, border);
    BgEditor_DrawTextClipped(surface, (x + w * 0.5f) * s, (y + h * 0.5f + 4.8f) * s, (w - 8.0f) * s,
                             13.0f * s, label, (active || hovered) ? kText : WithAlpha(kText, 0.86f), ALIGN_CENTRE);
}

static void BgEditor_PaintStudio(OverlaySurface *surface, const OverlayLayout *layout) {
    float s = layout->scale;
    char buf[160];

    s_bgEditor.widgetCount = 0;

    /* Outer Studio Frame */
    BgEditor_FastFillRect(surface, 0.0f, 0.0f, (float)surface->w, (float)(surface->h - 1), kPanel);
    BgEditor_FastStrokeRect(surface, 0.0f, 0.0f, (float)surface->w, (float)(surface->h - 1), 2.0f * s, kBorder);

    /* =========================================================================
     * 1. TOP NAVIGATION BAR (y = 12..52)
     * ========================================================================= */
    BgEditor_DrawTextClipped(surface, 20.0f * s, 38.0f * s, 200.0f * s, 19.0f * s,
                             "PE Background Studio", kText, ALIGN_LEFT);

    BgEditor_Button(surface, s, BG_ACT_ROOM_MINUS10, 0, 226.0f, 14.0f, 44.0f, 32.0f, "-10", 0, 0);
    BgEditor_Button(surface, s, BG_ACT_ROOM_PREV,    0, 276.0f, 14.0f, 64.0f, 32.0f, "< Prev", 0, 0);

    snprintf(buf, sizeof(buf), "M%03d — %s", s_bgEditor.mapNumber, BgEditor_RoomAreaName(s_bgEditor.mapNumber));
    BgEditor_FastFillRect(surface, 346.0f * s, 14.0f * s, 214.0f * s, 32.0f * s, WithAlpha(kAccent, 0.14f));
    BgEditor_FastStrokeRect(surface, 346.0f * s, 14.0f * s, 214.0f * s, 32.0f * s, 1.0f * s, WithAlpha(kAccent, 0.5f));
    BgEditor_DrawTextClipped(surface, 453.0f * s, 35.0f * s, 202.0f * s, 14.0f * s, buf, kText, ALIGN_CENTRE);

    BgEditor_Button(surface, s, BG_ACT_ROOM_NEXT,      0, 566.0f, 14.0f, 64.0f, 32.0f, "Next >", 0, 0);
    BgEditor_Button(surface, s, BG_ACT_ROOM_PLUS10,    0, 636.0f, 14.0f, 44.0f, 32.0f, "+10", 0, 0);
    BgEditor_Button(surface, s, BG_ACT_ROOM_NEXT_ANIM, 0, 686.0f, 14.0f, 114.0f, 32.0f, "Next Anim >>", 0, 1);

    int group = g_GeomScroll.state.position.groupSelect;
    int viewCount = BgEditor_ViewCount();
    snprintf(buf, sizeof(buf), "Cam %d/%d", group + 1, viewCount > 0 ? viewCount : 1);
    BgEditor_Button(surface, s, BG_ACT_CAM_GROUP_NEXT, 0, 810.0f, 14.0f, 94.0f, 32.0f, buf, viewCount > 1, 0);

    BgEditor_Button(surface, s, BG_ACT_PLAY_TOGGLE, 0, 910.0f, 14.0f, 82.0f, 32.0f,
                    s_bgEditor.playing ? "Playing" : "Paused", s_bgEditor.playing, 1);
    BgEditor_Button(surface, s, BG_ACT_STEP_FRAME,  0, 998.0f, 14.0f, 56.0f, 32.0f, "Step", 0, 0);
    BgEditor_Button(surface, s, BG_ACT_EXPORT_BMP,  0, 1060.0f, 14.0f, 102.0f, 32.0f, "Export BMP", 0, 1);
    BgEditor_Button(surface, s, BG_ACT_CLOSE,       0, 1168.0f, 14.0f, 92.0f, 32.0f, "Close (Esc)", 0, 0);

    BgEditor_FastFillRect(surface, 20.0f * s, 54.0f * s, (float)(BG_STUDIO_W - 40) * s, 1.0f * s, WithAlpha(kText, 0.10f));

    /* =========================================================================
     * 2. LEFT MAIN STAGE (x = 20..700, y = 64..762, w = 680)
     * ========================================================================= */
    BgEditor_FastFillRect(surface, 20.0f * s, 64.0f * s, 680.0f * s, 698.0f * s, kBgCard);
    BgEditor_FastStrokeRect(surface, 20.0f * s, 64.0f * s, 680.0f * s, 698.0f * s, 1.0f * s, WithAlpha(kText, 0.08f));

    static const char *const kViewTabs[5] = {
        "1: Composite", "2: Layer 1", "3: Layer 2/Anim", "4: Z-Depth", "5: VRAM Atlas"
    };
    for (int v = 0; v < 5; ++v) {
        BgEditor_Button(surface, s, BG_ACT_VIEW_MODE, v,
                        40.0f + v * 130.0f, 74.0f, 120.0f, 30.0f,
                        kViewTabs[v], s_bgEditor.viewMode == v, 1);
    }

    /* 2x Canvas Viewport: 320x224 -> 640x448 at (40, 112) */
    int canvasX = (int)roundf(40.0f * s);
    int canvasY = (int)roundf(112.0f * s);
    int canvasW = (int)roundf(640.0f * s);
    int canvasH = (int)roundf(448.0f * s);

    if (s_bgEditor.viewMode == BG_VIEW_VRAM_ATLAS) {
        BgEditor_RenderVramAtlas();
        BgEditor_BlitNearest(surface, canvasX, canvasY, canvasW, canvasH,
                             s_bgEditor.atlasRgba, 512, 256);
        int tx, ty, u, v, cx, cy;
        if (BgEditor_GetSelectedTileInfo(&tx, &ty, &u, &v, &cx, &cy, NULL, NULL)) {
            float rx = 40.0f + ((float)(tx + (u >> 1)) / 1024.0f) * 640.0f;
            float ry = 112.0f + ((float)(ty + v) / 512.0f) * 448.0f;
            float rw = (8.0f / 1024.0f) * 640.0f;
            float rh = (16.0f / 512.0f) * 448.0f;
            BgEditor_FastStrokeRect(surface, rx * s, ry * s, rw * s, rh * s, 2.0f * s, kGold);
            float clx = 40.0f + ((float)cx / 1024.0f) * 640.0f;
            float cly = 112.0f + ((float)cy / 512.0f) * 448.0f;
            BgEditor_FastStrokeRect(surface, clx * s, cly * s, (16.0f / 1024.0f) * 640.0f, 6.0f * s,
                                    2.0f * s, (Color){1.0f, 0.3f, 0.65f, 1.0f});
        }
    } else {
        BgEditor_CompositeCanvas();
        BgEditor_BlitNearest(surface, canvasX, canvasY, canvasW, canvasH,
                             s_bgEditor.canvasRgba, BG_CANVAS_W, BG_CANVAS_H);
    }
    BgEditor_FastStrokeRect(surface, 40.0f * s, 112.0f * s, 640.0f * s, 448.0f * s, 1.5f * s,
                            WithAlpha(kText, 0.25f));

    /* Overlay toggles right below canvas (y = 568..596) */
    BgEditor_Button(surface, s, BG_ACT_TOGGLE_GRID,         0, 40.0f,  568.0f, 152.0f, 28.0f,
                    s_bgEditor.showGrid ? "16x16 Grid: ON" : "16x16 Grid: OFF", s_bgEditor.showGrid, 0);
    BgEditor_Button(surface, s, BG_ACT_TOGGLE_OUTLINE_SEL,  0, 202.0f, 568.0f, 152.0f, 28.0f,
                    s_bgEditor.highlightSelected ? "Outline Layer: ON" : "Outline Layer: OFF", s_bgEditor.highlightSelected, 0);
    BgEditor_Button(surface, s, BG_ACT_TOGGLE_OUTLINE_ANIM, 0, 364.0f, 568.0f, 152.0f, 28.0f,
                    s_bgEditor.highlightAnimated ? "Outline Anims: ON" : "Outline Anims: OFF", s_bgEditor.highlightAnimated, 0);
    BgEditor_Button(surface, s, BG_ACT_TOGGLE_AUTOPAN,      0, 526.0f, 568.0f, 154.0f, 28.0f,
                    s_bgEditor.autoPanCamera ? "Auto-Pan Cam: ON" : "Auto-Pan Cam: OFF", s_bgEditor.autoPanCamera, 1);

    /* Animation Scrubber & Timeline Card (x = 34..686, y = 604..750) */
    BgEditor_FastFillRect(surface, 34.0f * s, 604.0f * s, 652.0f * s, 146.0f * s, kBgCardInner);
    BgEditor_FastStrokeRect(surface, 34.0f * s, 604.0f * s, 652.0f * s, 146.0f * s, 1.0f * s, WithAlpha(kText, 0.10f));

    int ctrlCount = 0;
    GeomCtrlEntry *ctrls = BgEditor_CtrlArray(&ctrlCount);
    int entryCount = 0;
    GeomEntry *entries = BgEditor_EntryArray(&entryCount);

    int groupCtrlIndices[32];
    int groupCtrlCount = 0;
    if (ctrls) {
        for (int c = 0; c < ctrlCount && groupCtrlCount < 32; ++c) {
            if ((int)ctrls[c].position.bits.group == group) {
                groupCtrlIndices[groupCtrlCount++] = c;
            }
        }
    }

    BgEditor_DrawTextClipped(surface, 46.0f * s, 628.0f * s, 190.0f * s, 13.5f * s,
                             "ANIMATION TIMELINE", kAccent, ALIGN_LEFT);

    /* Controller selector pills */
    for (int idx = 0; idx < groupCtrlCount && idx < 5; ++idx) {
        int c = groupCtrlIndices[idx];
        snprintf(buf, sizeof(buf), "Ctrl #%d", c);
        BgEditor_Button(surface, s, BG_ACT_SELECT_CTRL, c,
                        210.0f + idx * 70.0f, 610.0f, 64.0f, 24.0f,
                        buf, s_bgEditor.selectedCtrl == c, 1);
    }
    BgEditor_Button(surface, s, BG_ACT_TOGGLE_AUTOLOOP, 0, 566.0f, 610.0f, 108.0f, 24.0f,
                    s_bgEditor.autoActivateAnims ? "Auto-Loop: ON" : "Auto-Loop: OFF",
                    s_bgEditor.autoActivateAnims, 1);

    if (ctrls && s_bgEditor.selectedCtrl >= 0 && s_bgEditor.selectedCtrl < ctrlCount) {
        GeomCtrlEntry *ctrl = &ctrls[s_bgEditor.selectedCtrl];
        int frames = 0;
        GeomAnimationSlot *slots = BgEditor_CtrlSlots(ctrl, &frames);
        int curSlot = (ctrl->position.bits.value >> 8);
        if (curSlot < 0) curSlot = 0;
        if (frames > 0 && curSlot >= frames) curSlot %= frames;

        /* Draw visual slot strip (up to 8 slots across x = 46..674, y = 642..682) */
        int showSlots = frames < 8 ? frames : 8;
        if (showSlots > 0 && slots) {
            float slotW = (628.0f - (float)(showSlots - 1) * 6.0f) / (float)showSlots;
            if (slotW > 100.0f) slotW = 100.0f;
            for (int si = 0; si < showSlots; ++si) {
                snprintf(buf, sizeof(buf), "S%d: E%u (%df)", si + 1, slots[si].entry, (int)slots[si].duration);
                BgEditor_Button(surface, s, BG_ACT_SELECT_SLOT, si,
                                46.0f + si * (slotW + 6.0f), 642.0f, slotW, 34.0f,
                                buf, si == curSlot, 1);
            }
        }

        /* Timeline playback & duration buttons (y = 686..714) */
        int isPlaying = (ctrl->head.b.flags & 2) && (ctrl->head.b.flags & 0x14);
        BgEditor_Button(surface, s, BG_ACT_CTRL_TOGGLE_PLAY, 0, 46.0f,  684.0f, 84.0f, 26.0f,
                        isPlaying ? "Ctrl: ON" : "Ctrl: OFF", isPlaying, 1);
        BgEditor_Button(surface, s, BG_ACT_SLOT_PREV,      0, 136.0f, 684.0f, 68.0f, 26.0f, "< Slot", 0, 0);
        BgEditor_Button(surface, s, BG_ACT_SLOT_NEXT,      0, 210.0f, 684.0f, 68.0f, 26.0f, "Slot >", 0, 0);
        BgEditor_Button(surface, s, BG_ACT_SLOT_DUR_MINUS, 0, 284.0f, 684.0f, 78.0f, 26.0f, "Faster (-)", 0, 0);
        BgEditor_Button(surface, s, BG_ACT_SLOT_DUR_PLUS,  0, 368.0f, 684.0f, 78.0f, 26.0f, "Slower (+)", 0, 0);
        BgEditor_Button(surface, s, BG_ACT_CTRL_REV_DIR,   0, 452.0f, 684.0f, 76.0f, 26.0f, "Reverse", 0, 0);
        BgEditor_Button(surface, s, BG_ACT_EXPORT_STRIP,   0, 534.0f, 684.0f, 140.0f, 26.0f, "Export 8 Frames", 0, 1);
    } else {
        BgEditor_DrawTextClipped(surface, 46.0f * s, 666.0f * s, 620.0f * s, 13.5f * s,
                                 "No frame-by-frame GeomCtrlEntry in this camera view. Click 'Next Anim >>' or toggle Scroll.",
                                 kMuted, ALIGN_LEFT);
    }

    /* Live cursor / status line at bottom of timeline card (y = 736) */
    if (s_bgEditor.statusMsg[0] && SDL_GetTicks() < s_bgEditor.statusExpireTick) {
        BgEditor_DrawTextClipped(surface, 46.0f * s, 736.0f * s, 628.0f * s, 13.0f * s,
                                 s_bgEditor.statusMsg, kGold, ALIGN_LEFT);
    } else if (s_bgEditor.hoverCanvasX >= 0 && s_bgEditor.hoverCanvasY >= 0 &&
               s_bgEditor.hoverCanvasX < BG_CANVAS_W && s_bgEditor.hoverCanvasY < BG_CANVAS_H) {
        int hIdx = s_bgEditor.hoverCanvasY * BG_CANVAS_W + s_bgEditor.hoverCanvasX;
        int hEntry = s_bgEditor.pickEntry[hIdx];
        int hTile = s_bgEditor.pickTile[hIdx];
        uint16_t hZ = s_bgEditor.pickOtZ[hIdx];
        if (hEntry >= 0) {
            snprintf(buf, sizeof(buf),
                     "Cursor (%d, %d) — Layer E%d | Tile #%d | OT Z-Depth: %u  (Click to select & edit)",
                     s_bgEditor.hoverCanvasX, s_bgEditor.hoverCanvasY, hEntry, hTile, hZ);
        } else {
            snprintf(buf, sizeof(buf), "Cursor (%d, %d) — Empty / Transparent background",
                     s_bgEditor.hoverCanvasX, s_bgEditor.hoverCanvasY);
        }
        BgEditor_DrawTextClipped(surface, 46.0f * s, 736.0f * s, 628.0f * s, 13.0f * s,
                                 buf, kText, ALIGN_LEFT);
    } else {
        BgEditor_DrawTextClipped(surface, 46.0f * s, 736.0f * s, 628.0f * s, 13.0f * s,
                                 "Tip: Click any tile on the viewport to select its layer & paint pixels. Right-drag to pan camera.",
                                 kMuted, ALIGN_LEFT);
    }

    /* =========================================================================
     * 3. RIGHT INSPECTOR PANEL (x = 714..1260, y = 64..762, w = 546)
     * ========================================================================= */
    BgEditor_FastFillRect(surface, 714.0f * s, 64.0f * s, 546.0f * s, 698.0f * s, kBgCard);
    BgEditor_FastStrokeRect(surface, 714.0f * s, 64.0f * s, 546.0f * s, 698.0f * s, 1.0f * s, WithAlpha(kText, 0.08f));

    static const char *const kInspectorTabs[3] = {
        "Layers & Depth", "Tile & CLUT Painter", "Presets & Camera"
    };
    for (int t = 0; t < 3; ++t) {
        BgEditor_Button(surface, s, BG_ACT_INSPECTOR_TAB, t,
                        730.0f + t * 174.0f, 74.0f, 166.0f, 32.0f,
                        kInspectorTabs[t], s_bgEditor.inspectorTab == t, 1);
    }

    int groupEntries[256];
    int groupEntryCount = 0;
    int layer1Idx = BgEditor_FindLayer1Entry(group);
    if (entries) {
        for (int i = 0; i < entryCount && groupEntryCount < 256; ++i) {
            if (entries[i].group == group && entries[i].prim_count > 0) {
                groupEntries[groupEntryCount++] = i;
            }
        }
    }

    if (s_bgEditor.inspectorTab == BG_TAB_LAYERS) {
        /* ---- TAB 0: MESH LAYERS & DEPTH ---- */
        snprintf(buf, sizeof(buf), "MESH LAYERS IN CAM %d (%d layers)", group + 1, groupEntryCount);
        BgEditor_DrawTextClipped(surface, 730.0f * s, 134.0f * s, 230.0f * s, 13.5f * s, buf, kAccent, ALIGN_LEFT);

        BgEditor_Button(surface, s, BG_ACT_LAYERS_ALL_ON,    0, 968.0f,  114.0f, 64.0f, 26.0f, "All ON", 0, 0);
        BgEditor_Button(surface, s, BG_ACT_LAYERS_ALL_OFF,   0, 1038.0f, 114.0f, 64.0f, 26.0f, "All OFF", 0, 0);
        BgEditor_Button(surface, s, BG_ACT_LAYERS_PAGE_PREV, 0, 1108.0f, 114.0f, 64.0f, 26.0f, "< Up", 0, 0);
        BgEditor_Button(surface, s, BG_ACT_LAYERS_PAGE_NEXT, 0, 1178.0f, 114.0f, 66.0f, 26.0f, "Down >", 0, 0);

        int maxRows = 8;
        if (s_bgEditor.entryListScroll > groupEntryCount - maxRows) {
            s_bgEditor.entryListScroll = groupEntryCount - maxRows;
        }
        if (s_bgEditor.entryListScroll < 0) s_bgEditor.entryListScroll = 0;

        float ly = 148.0f;
        for (int row = 0; row < maxRows && (row + s_bgEditor.entryListScroll) < groupEntryCount; ++row) {
            int eIdx = groupEntries[row + s_bgEditor.entryListScroll];
            GeomEntry *e = &entries[eIdx];
            int vis = (e->head.b.flags & 2) != 0;
            int isSel = (eIdx == s_bgEditor.selectedEntry);
            int ownerCtrl = BgEditor_FindControllerForEntry(eIdx, group);

            const char *tag = "L2 OCCLUDER";
            if (eIdx == layer1Idx) tag = "L1 BASE GRID";
            else if (ownerCtrl >= 0) tag = "ANIM FRAME";
            else if (e->head.b.flags & 4) tag = "SCROLL LAYER";
            else if (e->head.b.flags & 8) tag = "PARALLAX";

            int rowHover = BgEditor_RegisterWidget(BG_ACT_LAYER_SELECT, eIdx, 730.0f, ly, 514.0f, 38.0f);
            Color rowBg = isSel ? WithAlpha(kAccent, 0.22f)
                                : (rowHover ? WithAlpha(kText, 0.09f) : kBgCardInner);
            BgEditor_FastFillRect(surface, 730.0f * s, ly * s, 514.0f * s, 38.0f * s, rowBg);
            BgEditor_FastStrokeRect(surface, 730.0f * s, ly * s, 514.0f * s, 38.0f * s,
                                    isSel ? 1.5f * s : 1.0f * s,
                                    isSel ? kAccent : WithAlpha(kText, 0.10f));

            BgEditor_Button(surface, s, BG_ACT_LAYER_TOGGLE_VIS, eIdx,
                            738.0f, ly + 5.0f, 52.0f, 28.0f, vis ? "ON" : "OFF", vis, 1);

            snprintf(buf, sizeof(buf), "E%-3d  %-12s  |  %3u tiles  |  Pos(%d,%d)  |  Z:%u",
                     eIdx, tag, e->prim_count, (s16)e->scr_x, (s16)e->scr_y,
                     (unsigned)((e->head.packed >> 8) & 0xFFFu));
            BgEditor_DrawTextClipped(surface, 800.0f * s, (ly + 24.0f) * s, 338.0f * s, 13.0f * s,
                                     buf, vis ? kText : kMuted, ALIGN_LEFT);

            BgEditor_Button(surface, s, BG_ACT_LAYER_EDIT_TILES, eIdx,
                            1146.0f, ly + 5.0f, 90.0f, 28.0f, "Paint Tile", isSel, 0);

            ly += 42.0f;
        }

        /* Selected Layer Transform & Scroll Card (y = 492..748) */
        BgEditor_FastFillRect(surface, 730.0f * s, 492.0f * s, 514.0f * s, 256.0f * s, kBgCardInner);
        BgEditor_FastStrokeRect(surface, 730.0f * s, 492.0f * s, 514.0f * s, 256.0f * s, 1.0f * s, WithAlpha(kText, 0.10f));

        if (entries && s_bgEditor.selectedEntry >= 0 && s_bgEditor.selectedEntry < entryCount) {
            GeomEntry *sel = &entries[s_bgEditor.selectedEntry];
            snprintf(buf, sizeof(buf), "SELECTED LAYER E%d — Pos(%d, %d)  Z-Depth:%u  Speed(%d, %d)",
                     s_bgEditor.selectedEntry, (s16)sel->scr_x, (s16)sel->scr_y,
                     (unsigned)((sel->head.packed >> 8) & 0xFFFu), sel->speedX, sel->speedY);
            BgEditor_DrawTextClipped(surface, 744.0f * s, 518.0f * s, 486.0f * s, 13.5f * s, buf, kAccent, ALIGN_LEFT);

            BgEditor_DrawTextClipped(surface, 744.0f * s, 552.0f * s, 110.0f * s, 13.0f * s, "Screen X / Y:", kMuted, ALIGN_LEFT);
            BgEditor_Button(surface, s, BG_ACT_LAYER_X_M4, 0, 850.0f, 532.0f, 44.0f, 28.0f, "X-4", 0, 0);
            BgEditor_Button(surface, s, BG_ACT_LAYER_X_M1, 0, 898.0f, 532.0f, 44.0f, 28.0f, "X-1", 0, 0);
            BgEditor_Button(surface, s, BG_ACT_LAYER_X_P1, 0, 946.0f, 532.0f, 44.0f, 28.0f, "X+1", 0, 0);
            BgEditor_Button(surface, s, BG_ACT_LAYER_X_P4, 0, 994.0f, 532.0f, 44.0f, 28.0f, "X+4", 0, 0);
            BgEditor_Button(surface, s, BG_ACT_LAYER_Y_M4, 0, 1050.0f, 532.0f, 44.0f, 28.0f, "Y-4", 0, 0);
            BgEditor_Button(surface, s, BG_ACT_LAYER_Y_M1, 0, 1098.0f, 532.0f, 44.0f, 28.0f, "Y-1", 0, 0);
            BgEditor_Button(surface, s, BG_ACT_LAYER_Y_P1, 0, 1146.0f, 532.0f, 44.0f, 28.0f, "Y+1", 0, 0);
            BgEditor_Button(surface, s, BG_ACT_LAYER_Y_P4, 0, 1194.0f, 532.0f, 40.0f, 28.0f, "Y+4", 0, 0);

            BgEditor_DrawTextClipped(surface, 744.0f * s, 592.0f * s, 110.0f * s, 13.0f * s, "OT Z-Depth:", kMuted, ALIGN_LEFT);
            BgEditor_Button(surface, s, BG_ACT_LAYER_Z_M64, 0, 850.0f, 572.0f, 90.0f, 28.0f, "Closer -64", 0, 0);
            BgEditor_Button(surface, s, BG_ACT_LAYER_Z_M16, 0, 946.0f, 572.0f, 90.0f, 28.0f, "Closer -16", 0, 0);
            BgEditor_Button(surface, s, BG_ACT_LAYER_Z_P16, 0, 1042.0f, 572.0f, 92.0f, 28.0f, "Farther +16", 0, 0);
            BgEditor_Button(surface, s, BG_ACT_LAYER_Z_P64, 0, 1140.0f, 572.0f, 94.0f, 28.0f, "Farther +64", 0, 0);

            BgEditor_DrawTextClipped(surface, 744.0f * s, 632.0f * s, 110.0f * s, 13.0f * s, "Motion Mode:", kMuted, ALIGN_LEFT);
            BgEditor_Button(surface, s, BG_ACT_LAYER_TOGGLE_SCROLL, 0, 850.0f, 612.0f, 186.0f, 28.0f,
                            (sel->head.b.flags & 4) ? "Continuous Scroll: ON" : "Continuous Scroll: OFF",
                            (sel->head.b.flags & 4) != 0, 1);
            BgEditor_Button(surface, s, BG_ACT_LAYER_TOGGLE_PARALLAX, 0, 1044.0f, 612.0f, 190.0f, 28.0f,
                            (sel->head.b.flags & 8) ? "Camera Parallax: ON" : "Camera Parallax: OFF",
                            (sel->head.b.flags & 8) != 0, 1);

            BgEditor_DrawTextClipped(surface, 744.0f * s, 672.0f * s, 110.0f * s, 13.0f * s, "Scroll Speed:", kMuted, ALIGN_LEFT);
            BgEditor_Button(surface, s, BG_ACT_LAYER_SPDX_MINUS, 0, 850.0f, 652.0f, 90.0f, 28.0f, "SpdX -64", 0, 0);
            BgEditor_Button(surface, s, BG_ACT_LAYER_SPDX_PLUS,  0, 946.0f, 652.0f, 90.0f, 28.0f, "SpdX +64", 0, 0);
            BgEditor_Button(surface, s, BG_ACT_LAYER_SPDY_MINUS, 0, 1042.0f, 652.0f, 92.0f, 28.0f, "SpdY -64", 0, 0);
            BgEditor_Button(surface, s, BG_ACT_LAYER_SPDY_PLUS,  0, 1140.0f, 652.0f, 94.0f, 28.0f, "SpdY +64", 0, 0);

            BgEditor_Button(surface, s, BG_ACT_LAYER_EDIT_TILES, s_bgEditor.selectedEntry,
                            744.0f, 700.0f, 236.0f, 34.0f, "Open Tile & CLUT Painter >>", 0, 1);
            BgEditor_Button(surface, s, BG_ACT_ROOM_RESET, 0,
                            994.0f, 700.0f, 240.0f, 34.0f, "Reset Room Geometry & VRAM", 0, 0);
        }
    } else if (s_bgEditor.inspectorTab == BG_TAB_PAINTER) {
        /* ---- TAB 1: TILE & CLUT PAINTER ---- */
        int tx = 0, ty = 0, u = 0, v = 0, clutX = 0, clutY = 0;
        u32 *posWord = NULL;
        RenderTileTexture *tex = NULL;
        if (BgEditor_GetSelectedTileInfo(&tx, &ty, &u, &v, &clutX, &clutY, &posWord, &tex)) {
            GeomEntry *entry = &entries[s_bgEditor.selectedEntry];
            int tileX = (int)(*posWord >> 22);
            int tileY = (int)((*posWord >> 12) & 0x3FFu);
            int tileZ = (int)(*posWord & 0xFFFu);
            int semi = (tex->words[1] & 0x10000000u) != 0;
            int abr = (int)((tex->words[0] >> 22) & 3u);
            static const char *const kAbrNames[4] = {"50%B+50%F", "100%B+100%F", "100%B-100%F", "100%B+25%F"};

            snprintf(buf, sizeof(buf), "Layer E%d — Tile #%d / %u",
                     s_bgEditor.selectedEntry, s_bgEditor.selectedTile + 1, entry->prim_count);
            BgEditor_DrawTextClipped(surface, 730.0f * s, 134.0f * s, 210.0f * s, 14.0f * s, buf, kAccent, ALIGN_LEFT);

            BgEditor_Button(surface, s, BG_ACT_PAINTER_LAYER_PREV, 0, 948.0f,  114.0f, 68.0f, 26.0f, "< Layer", 0, 0);
            BgEditor_Button(surface, s, BG_ACT_PAINTER_LAYER_NEXT, 0, 1022.0f, 114.0f, 68.0f, 26.0f, "Layer >", 0, 0);
            BgEditor_Button(surface, s, BG_ACT_PAINTER_TILE_PREV,  0, 1098.0f, 114.0f, 68.0f, 26.0f, "< Tile", 0, 0);
            BgEditor_Button(surface, s, BG_ACT_PAINTER_TILE_NEXT,  0, 1172.0f, 114.0f, 72.0f, 26.0f, "Tile >", 0, 0);

            snprintf(buf, sizeof(buf), "Tile Pos: (%d, %d)  Z-Delta: %d  |  TPage: (%d, %d)  UV: (%d, %d)  CLUT: (%d, %d)",
                     tileX, tileY, tileZ, tx, ty, u, v, clutX, clutY);
            BgEditor_DrawTextClipped(surface, 730.0f * s, 160.0f * s, 514.0f * s, 12.5f * s, buf, kMuted, ALIGN_LEFT);

            BgEditor_Button(surface, s, BG_ACT_TILE_TOGGLE_SEMI, 0, 730.0f, 170.0f, 136.0f, 26.0f,
                            semi ? "Semi-Trans: ON" : "Semi-Trans: OFF", semi, 1);
            snprintf(buf, sizeof(buf), "ABR: %s", kAbrNames[abr]);
            BgEditor_Button(surface, s, BG_ACT_TILE_CYCLE_ABR,   0, 872.0f, 170.0f, 136.0f, 26.0f, buf, semi, 0);
            BgEditor_Button(surface, s, BG_ACT_TILE_X_M16, 0, 1016.0f, 170.0f, 52.0f, 26.0f, "X-16", 0, 0);
            BgEditor_Button(surface, s, BG_ACT_TILE_X_P16, 0, 1072.0f, 170.0f, 52.0f, 26.0f, "X+16", 0, 0);
            BgEditor_Button(surface, s, BG_ACT_TILE_Y_M16, 0, 1128.0f, 170.0f, 54.0f, 26.0f, "Y-16", 0, 0);
            BgEditor_Button(surface, s, BG_ACT_TILE_Y_P16, 0, 1186.0f, 170.0f, 58.0f, 26.0f, "Y+16", 0, 0);

            /* Left side: 16x16 Zoomed Tile Pixel Grid (x = 730..986, y = 228..484, 16px per texel = 256x256) */
            BgEditor_DrawTextClipped(surface, 730.0f * s, 220.0f * s, 256.0f * s, 13.0f * s,
                                     "16x16 Tile Texels (L:Paint, R:Pick):", kText, ALIGN_LEFT);
            float gridX = 730.0f, gridY = 228.0f, cell = 16.0f;
            for (int py = 0; py < 16; ++py) {
                int vramY = (ty + v + py) & (BG_VRAM_H - 1);
                for (int px = 0; px < 16; ++px) {
                    int texU = u + px;
                    int vramX = (tx + (texU >> 1)) & (BG_VRAM_W - 1);
                    uint16_t packed = s_bgEditor.vram[vramY * BG_VRAM_W + vramX];
                    uint8_t idx8 = (texU & 1) ? (uint8_t)(packed >> 8) : (uint8_t)(packed & 0xFFu);
                    uint16_t cw = s_bgEditor.vram[(clutY & 511) * BG_VRAM_W + ((clutX + idx8) & 1023)];
                    Color c;
                    if (cw == 0) {
                        float ch = ((px ^ py) & 1) ? 0.16f : 0.10f;
                        c = (Color){ch, ch, ch + 0.03f, 1.0f};
                    } else {
                        uint8_t r8, g8, b8;
                        BgEditor_Bgr555ToRgb8(cw, &r8, &g8, &b8);
                        c = (Color){r8 / 255.0f, g8 / 255.0f, b8 / 255.0f, 1.0f};
                    }
                    BgEditor_FastFillRect(surface, (gridX + px * cell) * s, (gridY + py * cell) * s,
                                          (cell - 1.0f) * s, (cell - 1.0f) * s, c);
                }
            }
            BgEditor_FastStrokeRect(surface, gridX * s, gridY * s, 256.0f * s, 256.0f * s, 1.5f * s,
                                    WithAlpha(kText, 0.35f));

            /* Right side: 4x4 Grid of 16-Color CLUT Swatches (x = 1004..1244, y = 228..484) */
            BgEditor_DrawTextClipped(surface, 1004.0f * s, 220.0f * s, 240.0f * s, 13.0f * s,
                                     "16-Color CLUT Swatches:", kText, ALIGN_LEFT);
            for (int ci = 0; ci < 16; ++ci) {
                int r = ci / 4, col = ci % 4;
                float sx = 1004.0f + col * 61.0f;
                float sy = 228.0f + r * 54.0f;
                uint16_t cw = s_bgEditor.vram[(clutY & 511) * BG_VRAM_W + ((clutX + ci) & 1023)];
                uint8_t r8, g8, b8;
                BgEditor_Bgr555ToRgb8(cw, &r8, &g8, &b8);
                Color swatch = (cw == 0)
                    ? (Color){0.08f, 0.09f, 0.12f, 1.0f}
                    : (Color){r8 / 255.0f, g8 / 255.0f, b8 / 255.0f, 1.0f};

                int swHover = BgEditor_RegisterWidget(BG_ACT_CLUT_SELECT, ci, sx, sy, 56.0f, 48.0f);
                BgEditor_FastFillRect(surface, sx * s, sy * s, 56.0f * s, 48.0f * s, swatch);
                int isSel = (ci == s_bgEditor.selectedClutIdx);
                BgEditor_FastStrokeRect(surface, sx * s, sy * s, 56.0f * s, 48.0f * s,
                                        isSel ? 2.5f * s : (swHover ? 1.5f * s : 1.0f * s),
                                        isSel ? kGold : (swHover ? kAccent : WithAlpha(kText, 0.25f)));
                snprintf(buf, sizeof(buf), "#%d", ci);
                BgEditor_DrawTextClipped(surface, (sx + 6.0f) * s, (sy + 16.0f) * s, 44.0f * s, 11.5f * s,
                                         buf, (r8 + g8 + b8 > 380) ? (Color){0.05f, 0.05f, 0.08f, 1.0f} : kText, ALIGN_LEFT);
            }
            BgEditor_Button(surface, s, BG_ACT_TILE_U_P16, 0, 1004.0f, 452.0f, 116.0f, 32.0f, "Shift UV U+16", 0, 0);
            BgEditor_Button(surface, s, BG_ACT_TILE_V_P16, 0, 1128.0f, 452.0f, 116.0f, 32.0f, "Shift UV V+16", 0, 0);

            /* Bottom Card: BGR555 Swatch Color Editor (y = 498..748) */
            BgEditor_FastFillRect(surface, 730.0f * s, 498.0f * s, 514.0f * s, 250.0f * s, kBgCardInner);
            BgEditor_FastStrokeRect(surface, 730.0f * s, 498.0f * s, 514.0f * s, 250.0f * s, 1.0f * s, WithAlpha(kText, 0.10f));

            int ci = s_bgEditor.selectedClutIdx & 15;
            uint16_t cw = s_bgEditor.vram[(clutY & 511) * BG_VRAM_W + ((clutX + ci) & 1023)];
            int r5 = cw & 31, g5 = (cw >> 5) & 31, b5 = (cw >> 10) & 31, stp = (cw >> 15) & 1;
            snprintf(buf, sizeof(buf), "BRUSH SWATCH #%d — BGR555: 0x%04X  (R:%d  G:%d  B:%d  STP:%d)",
                     ci, cw, r5, g5, b5, stp);
            BgEditor_DrawTextClipped(surface, 744.0f * s, 524.0f * s, 486.0f * s, 13.5f * s, buf, kAccent, ALIGN_LEFT);

            const char *chNames[3] = {"Red (0..31)", "Green (0..31)", "Blue (0..31)"};
            int chVals[3] = {r5, g5, b5};
            Color chColors[3] = {
                {0.95f, 0.32f, 0.32f, 1.0f},
                {0.28f, 0.85f, 0.45f, 1.0f},
                {0.32f, 0.62f, 1.0f, 1.0f}
            };
            BgActionId minusAct[3] = {BG_ACT_CLUT_R_MINUS, BG_ACT_CLUT_G_MINUS, BG_ACT_CLUT_B_MINUS};
            BgActionId plusAct[3]  = {BG_ACT_CLUT_R_PLUS,  BG_ACT_CLUT_G_PLUS,  BG_ACT_CLUT_B_PLUS};
            BgActionId barAct[3]   = {BG_ACT_CLUT_R_BAR,   BG_ACT_CLUT_G_BAR,   BG_ACT_CLUT_B_BAR};

            for (int ch = 0; ch < 3; ++ch) {
                float ry = 540.0f + ch * 46.0f;
                snprintf(buf, sizeof(buf), "%s: %2d", chNames[ch], chVals[ch]);
                BgEditor_DrawTextClipped(surface, 744.0f * s, (ry + 21.0f) * s, 120.0f * s, 13.0f * s, buf, kText, ALIGN_LEFT);
                BgEditor_Button(surface, s, minusAct[ch], 0, 872.0f, ry, 44.0f, 32.0f, "-", 0, 0);

                /* Interactive clickable/draggable bar (x = 924..1174, w = 250) */
                BgEditor_RegisterWidget(barAct[ch], ch, 924.0f, ry, 250.0f, 32.0f);
                BgEditor_FastFillRect(surface, 924.0f * s, (ry + 6.0f) * s, 250.0f * s, 20.0f * s, WithAlpha(kText, 0.10f));
                BgEditor_FastFillRect(surface, 924.0f * s, (ry + 6.0f) * s, (250.0f * chVals[ch] / 31.0f) * s, 20.0f * s, chColors[ch]);
                BgEditor_FastStrokeRect(surface, 924.0f * s, (ry + 6.0f) * s, 250.0f * s, 20.0f * s, 1.0f * s, WithAlpha(kText, 0.25f));

                BgEditor_Button(surface, s, plusAct[ch], 0, 1182.0f, ry, 44.0f, 32.0f, "+", 0, 0);
            }

            BgEditor_Button(surface, s, BG_ACT_CLUT_TOGGLE_STP, 0, 744.0f, 690.0f, 236.0f, 36.0f,
                            stp ? "STP Semi-Trans Bit: ON" : "STP Semi-Trans Bit: OFF", stp, 1);
            BgEditor_Button(surface, s, BG_ACT_CLUT_TOGGLE_ZERO, 0, 994.0f, 690.0f, 236.0f, 36.0f,
                            cw == 0 ? "Transparent (0x0000): ON" : "Set Transparent (0x0000)", cw == 0, 0);
        }
    } else {
        /* ---- TAB 2: PRESETS & CAMERA ---- */
        BgEditor_DrawTextClipped(surface, 730.0f * s, 134.0f * s, 500.0f * s, 14.0f * s,
                                 "ICONIC ANIMATED ROOM PRESETS (1-CLICK LOAD)", kAccent, ALIGN_LEFT);

        static const struct {
            int map;
            const char *title;
        } kPresets[12] = {
            {  1, "M001 Carnegie Exterior"},
            {  5, "M005 Opera Stage"},
            { 13, "M013 Backstage Corridor"},
            { 23, "M023 Understage Boss"},
            { 27, "M027 Sewer Channels"},
            { 45, "M045 NYPD Precinct"},
            { 59, "M059 Central Park"},
            { 75, "M075 Park Save Room"},
            { 83, "M083 Park Fountain"},
            {126, "M126 Hospital Lobby"},
            {186, "M186 Subway Train"},
            {197, "M197 Museum Exhibit"}
        };
        for (int p = 0; p < 12; ++p) {
            int r = p / 2, col = p % 2;
            BgEditor_Button(surface, s, BG_ACT_PRESET_ROOM, kPresets[p].map,
                            730.0f + col * 260.0f, 148.0f + r * 44.0f, 250.0f, 36.0f,
                            kPresets[p].title, s_bgEditor.mapNumber == kPresets[p].map, 1);
        }

        /* Camera & Viewport Controls Card (y = 430..748) */
        BgEditor_FastFillRect(surface, 730.0f * s, 430.0f * s, 514.0f * s, 318.0f * s, kBgCardInner);
        BgEditor_FastStrokeRect(surface, 730.0f * s, 430.0f * s, 514.0f * s, 318.0f * s, 1.0f * s, WithAlpha(kText, 0.10f));

        BgEditor_DrawTextClipped(surface, 744.0f * s, 458.0f * s, 486.0f * s, 14.0f * s,
                                 "CAMERA VIEWPORT & SCROLL CONTROLS", kAccent, ALIGN_LEFT);

        int vCount = 0;
        CameraViewport *views = BgEditor_ViewportArray(&vCount);
        if (views && group < vCount) {
            CameraViewport *vw = &views[group];
            snprintf(buf, sizeof(buf), "Camera Position: (%d, %d)   Origin: (%d, %d)",
                     (int)g_GeomScroll.state.position.x, (int)g_GeomScroll.state.position.y,
                     (int)g_GeomScroll.state.position.originX, (int)g_GeomScroll.state.position.originY);
            BgEditor_DrawTextClipped(surface, 744.0f * s, 488.0f * s, 486.0f * s, 13.0f * s, buf, kText, ALIGN_LEFT);

            snprintf(buf, sizeof(buf), "Scroll Bounds: X [%d .. %d]   Y [%d .. %d]   Size: %ux%u   Focal: %u",
                     vw->minX, vw->maxX, vw->minY, vw->maxY, vw->width, vw->height,
                     vw->prefix.gpu.geom_screen);
            BgEditor_DrawTextClipped(surface, 744.0f * s, 514.0f * s, 486.0f * s, 13.0f * s, buf, kMuted, ALIGN_LEFT);
        }

        BgEditor_Button(surface, s, BG_ACT_CAM_LEFT,   0, 744.0f,  538.0f, 114.0f, 36.0f, "Cam Left", 0, 0);
        BgEditor_Button(surface, s, BG_ACT_CAM_RIGHT,  0, 866.0f,  538.0f, 114.0f, 36.0f, "Cam Right", 0, 0);
        BgEditor_Button(surface, s, BG_ACT_CAM_UP,     0, 988.0f,  538.0f, 114.0f, 36.0f, "Cam Up", 0, 0);
        BgEditor_Button(surface, s, BG_ACT_CAM_DOWN,   0, 1110.0f, 538.0f, 118.0f, 36.0f, "Cam Down", 0, 0);

        BgEditor_Button(surface, s, BG_ACT_CAM_CENTER, 0, 744.0f,  586.0f, 236.0f, 36.0f, "Center Camera in Viewport", 0, 0);
        BgEditor_Button(surface, s, BG_ACT_TOGGLE_AUTOPAN, 0, 992.0f, 586.0f, 236.0f, 36.0f,
                        s_bgEditor.autoPanCamera ? "Auto-Pan Camera: ON" : "Auto-Pan Camera: OFF",
                        s_bgEditor.autoPanCamera, 1);

        BgEditor_Button(surface, s, BG_ACT_EXPORT_BMP,   0, 744.0f, 638.0f, 236.0f, 38.0f, "Export Current View (.BMP)", 0, 1);
        BgEditor_Button(surface, s, BG_ACT_EXPORT_STRIP, 0, 992.0f, 638.0f, 236.0f, 38.0f, "Export 8-Frame Strip (.BMP)", 0, 1);

        BgEditor_Button(surface, s, BG_ACT_ROOM_RESET,   0, 744.0f, 690.0f, 484.0f, 38.0f, "Reset Room to Original Disc State", 0, 0);
    }

    /* Footer Shortcuts Bar (y = 782) */
    BgEditor_DrawTextClipped(surface, 24.0f * s, 784.0f * s, (float)(BG_STUDIO_W - 48) * s, 12.5f * s,
                             "Shortcuts:  [ / ] Prev/Next Room   |   N Next Animated Room   |   Tab Switch Cam View   |   Space Play/Pause   |   . Step   |   1-5 View Mode   |   E Export BMP   |   Esc/F2 Close",
                             WithAlpha(kMuted, 0.85f), ALIGN_CENTRE);

    uint8_t *solid = surface->pixels + (size_t)(surface->h - 1) * (size_t)surface->w * 4u;
    solid[3] = 255;
}

static void BgEditor_UploadWordToLiveVram(int vramX, int vramY) {
    if (vramX < 0 || vramX >= BG_VRAM_W || vramY < 0 || vramY >= BG_VRAM_H) return;
    RECT r = {(short)vramX, (short)vramY, 1, 1};
    uint32_t word = s_bgEditor.vram[vramY * BG_VRAM_W + vramX];
    LoadImage(&r, &word);
}

static void BgEditor_PaintTexelInSelectedTile(int px, int py, int pickColorInstead) {
    if (px < 0 || px >= 16 || py < 0 || py >= 16) return;
    int tx, ty, u, v;
    if (!BgEditor_GetSelectedTileInfo(&tx, &ty, &u, &v, NULL, NULL, NULL, NULL)) return;
    int vramY = (ty + v + py) & (BG_VRAM_H - 1);
    int texU = u + px;
    int vramX = (tx + (texU >> 1)) & (BG_VRAM_W - 1);
    uint16_t packed = s_bgEditor.vram[vramY * BG_VRAM_W + vramX];
    if (pickColorInstead) {
        uint8_t idx8 = (texU & 1) ? (uint8_t)(packed >> 8) : (uint8_t)(packed & 0xFFu);
        s_bgEditor.selectedClutIdx = idx8 & 15;
        s_bgEditor.dirty = 1;
        return;
    }
    uint8_t cIdx = (uint8_t)(s_bgEditor.selectedClutIdx & 15);
    if (texU & 1) {
        packed = (uint16_t)((packed & 0x00FFu) | ((uint16_t)cIdx << 8));
    } else {
        packed = (uint16_t)((packed & 0xFF00u) | cIdx);
    }
    s_bgEditor.vram[vramY * BG_VRAM_W + vramX] = packed;
    BgEditor_UploadWordToLiveVram(vramX, vramY);
    s_bgEditor.dirty = 1;
}

static void BgEditor_ModifySelectedClutColor(int dr, int dg, int db, int toggleStp, int makeZero) {
    int clutX, clutY;
    if (!BgEditor_GetSelectedTileInfo(NULL, NULL, NULL, NULL, &clutX, &clutY, NULL, NULL)) return;
    int ci = s_bgEditor.selectedClutIdx & 15;
    int vx = (clutX + ci) & (BG_VRAM_W - 1);
    int vy = clutY & (BG_VRAM_H - 1);
    uint16_t cw = s_bgEditor.vram[vy * BG_VRAM_W + vx];
    if (makeZero) {
        cw = (cw == 0) ? 0x8421u : 0x0000u;
    } else {
        int r5 = (cw & 31) + dr;
        int g5 = ((cw >> 5) & 31) + dg;
        int b5 = ((cw >> 10) & 31) + db;
        int stp = (cw >> 15) & 1;
        if (toggleStp) stp ^= 1;
        if (r5 < 0) r5 = 0; else if (r5 > 31) r5 = 31;
        if (g5 < 0) g5 = 0; else if (g5 > 31) g5 = 31;
        if (b5 < 0) b5 = 0; else if (b5 > 31) b5 = 31;
        cw = (uint16_t)(r5 | (g5 << 5) | (b5 << 10) | (stp << 15));
    }
    s_bgEditor.vram[vy * BG_VRAM_W + vx] = cw;
    BgEditor_UploadWordToLiveVram(vx, vy);
    s_bgEditor.dirty = 1;
}

static void BgEditor_SetSelectedClutChannelFromBar(int channel, float designX) {
    int clutX, clutY;
    if (!BgEditor_GetSelectedTileInfo(NULL, NULL, NULL, NULL, &clutX, &clutY, NULL, NULL)) return;
    int val = (int)roundf(((designX - 924.0f) / 250.0f) * 31.0f);
    if (val < 0) val = 0;
    if (val > 31) val = 31;
    int ci = s_bgEditor.selectedClutIdx & 15;
    int vx = (clutX + ci) & (BG_VRAM_W - 1);
    int vy = clutY & (BG_VRAM_H - 1);
    uint16_t cw = s_bgEditor.vram[vy * BG_VRAM_W + vx];
    int r5 = cw & 31, g5 = (cw >> 5) & 31, b5 = (cw >> 10) & 31, stp = (cw >> 15) & 1;
    if (channel == 0) r5 = val;
    else if (channel == 1) g5 = val;
    else if (channel == 2) b5 = val;
    cw = (uint16_t)(r5 | (g5 << 5) | (b5 << 10) | (stp << 15));
    s_bgEditor.vram[vy * BG_VRAM_W + vx] = cw;
    BgEditor_UploadWordToLiveVram(vx, vy);
    s_bgEditor.dirty = 1;
}

static void BgEditor_NudgeCamera(int dx, int dy, int resetToCenter) {
    int group = g_GeomScroll.state.position.groupSelect;
    int vCount = 0;
    CameraViewport *views = BgEditor_ViewportArray(&vCount);
    if (resetToCenter && views && group < vCount) {
        g_GeomScroll.state.position.x = (s16)((views[group].minX + views[group].maxX) / 2);
        g_GeomScroll.state.position.y = (s16)((views[group].minY + views[group].maxY) / 2);
    } else {
        g_GeomScroll.state.position.x = (s16)(g_GeomScroll.state.position.x + dx);
        g_GeomScroll.state.position.y = (s16)(g_GeomScroll.state.position.y + dy);
    }
    s_bgEditor.dirty = 1;
}

static void BgEditor_SetCtrlSlot(GeomCtrlEntry *ctrl, int newSlot) {
    if (!ctrl) return;
    int frames = 0;
    GeomAnimationSlot *slots = BgEditor_CtrlSlots(ctrl, &frames);
    int entryCount = 0;
    GeomEntry *entries = BgEditor_EntryArray(&entryCount);
    if (!slots || !entries || frames <= 0) return;

    while (newSlot < 0) newSlot += frames;
    newSlot %= frames;
    ctrl->position.bits.value = (s16)(newSlot << 8);
    for (int s = 0; s < frames; ++s) {
        if (slots[s].entry < entryCount) {
            entries[slots[s].entry].head.b.flags &= ~2u;
        }
    }
    if (slots[newSlot].entry < entryCount) {
        entries[slots[newSlot].entry].head.b.flags |= 2u;
        s_bgEditor.selectedEntry = slots[newSlot].entry;
        s_bgEditor.selectedTile = 0;
    }
    s_bgEditor.dirty = 1;
}

static void BgEditor_ExecuteAction(BgActionId action, int param, float clickX) {
    int group = g_GeomScroll.state.position.groupSelect;
    int ctrlCount = 0;
    GeomCtrlEntry *ctrls = BgEditor_CtrlArray(&ctrlCount);
    int entryCount = 0;
    GeomEntry *entries = BgEditor_EntryArray(&entryCount);

    switch (action) {
    case BG_ACT_ROOM_MINUS10:   BgEditor_StepAdjacentRoom(-10, 0); break;
    case BG_ACT_ROOM_PREV:      BgEditor_StepAdjacentRoom(-1, 0); break;
    case BG_ACT_ROOM_NEXT:      BgEditor_StepAdjacentRoom(1, 0); break;
    case BG_ACT_ROOM_PLUS10:    BgEditor_StepAdjacentRoom(10, 0); break;
    case BG_ACT_ROOM_NEXT_ANIM: BgEditor_StepAdjacentRoom(1, 1); break;
    case BG_ACT_CAM_GROUP_NEXT: BgEditor_SwitchGroup(group + 1); break;
    case BG_ACT_PLAY_TOGGLE:    s_bgEditor.playing ^= 1; s_bgEditor.dirty = 1; break;
    case BG_ACT_STEP_FRAME:
        Render_StepGroupAnimations();
        Render_ScrollEntries();
        s_bgEditor.dirty = 1;
        break;
    case BG_ACT_EXPORT_BMP:     BgEditor_ExportCurrentRoomBmp(0); break;
    case BG_ACT_EXPORT_STRIP:   BgEditor_ExportCurrentRoomBmp(1); break;
    case BG_ACT_CLOSE:          s_bgEditor.active = 0; break;

    case BG_ACT_VIEW_MODE:
        s_bgEditor.viewMode = param;
        s_bgEditor.dirty = 1;
        break;
    case BG_ACT_INSPECTOR_TAB:
        s_bgEditor.inspectorTab = param;
        s_bgEditor.dirty = 1;
        break;

    case BG_ACT_TOGGLE_GRID:         s_bgEditor.showGrid ^= 1; s_bgEditor.dirty = 1; break;
    case BG_ACT_TOGGLE_OUTLINE_SEL:  s_bgEditor.highlightSelected ^= 1; s_bgEditor.dirty = 1; break;
    case BG_ACT_TOGGLE_OUTLINE_ANIM: s_bgEditor.highlightAnimated ^= 1; s_bgEditor.dirty = 1; break;
    case BG_ACT_TOGGLE_AUTOPAN:      s_bgEditor.autoPanCamera ^= 1; s_bgEditor.dirty = 1; break;

    case BG_ACT_TOGGLE_AUTOLOOP:
        s_bgEditor.autoActivateAnims ^= 1;
        if (s_bgEditor.autoActivateAnims) {
            BgEditor_ActivateGroupAnimations(group);
        }
        s_bgEditor.dirty = 1;
        break;
    case BG_ACT_SELECT_CTRL:
        s_bgEditor.selectedCtrl = param;
        if (ctrls && param >= 0 && param < ctrlCount) {
            int frames = 0;
            GeomAnimationSlot *slots = BgEditor_CtrlSlots(&ctrls[param], &frames);
            int curSlot = (ctrls[param].position.bits.value >> 8);
            if (slots && curSlot >= 0 && curSlot < frames) {
                s_bgEditor.selectedEntry = slots[curSlot].entry;
                s_bgEditor.selectedTile = 0;
            }
        }
        s_bgEditor.dirty = 1;
        break;
    case BG_ACT_SELECT_SLOT:
        if (ctrls && s_bgEditor.selectedCtrl >= 0 && s_bgEditor.selectedCtrl < ctrlCount) {
            s_bgEditor.playing = 0;
            BgEditor_SetCtrlSlot(&ctrls[s_bgEditor.selectedCtrl], param);
        }
        break;
    case BG_ACT_SLOT_PREV:
        if (ctrls && s_bgEditor.selectedCtrl >= 0 && s_bgEditor.selectedCtrl < ctrlCount) {
            BgEditor_SetCtrlSlot(&ctrls[s_bgEditor.selectedCtrl],
                                 (ctrls[s_bgEditor.selectedCtrl].position.bits.value >> 8) - 1);
        }
        break;
    case BG_ACT_SLOT_NEXT:
        if (ctrls && s_bgEditor.selectedCtrl >= 0 && s_bgEditor.selectedCtrl < ctrlCount) {
            BgEditor_SetCtrlSlot(&ctrls[s_bgEditor.selectedCtrl],
                                 (ctrls[s_bgEditor.selectedCtrl].position.bits.value >> 8) + 1);
        }
        break;
    case BG_ACT_SLOT_DUR_MINUS:
    case BG_ACT_SLOT_DUR_PLUS:
        if (ctrls && s_bgEditor.selectedCtrl >= 0 && s_bgEditor.selectedCtrl < ctrlCount) {
            int frames = 0;
            GeomAnimationSlot *slots = BgEditor_CtrlSlots(&ctrls[s_bgEditor.selectedCtrl], &frames);
            for (int si = 0; si < frames; ++si) {
                int d = (int)slots[si].duration + (action == BG_ACT_SLOT_DUR_PLUS ? 1 : -1);
                if (d < 1) d = 1;
                if (d > 60) d = 60;
                slots[si].duration = (s8)d;
            }
            s_bgEditor.dirty = 1;
        }
        break;
    case BG_ACT_CTRL_REV_DIR:
        if (ctrls && s_bgEditor.selectedCtrl >= 0 && s_bgEditor.selectedCtrl < ctrlCount) {
            GeomCtrlEntry *ctrl = &ctrls[s_bgEditor.selectedCtrl];
            ctrl->step = (s16)(-ctrl->step);
            if ((ctrl->head.b.flags & 0x14) == 4) ctrl->head.b.flags = (ctrl->head.b.flags & ~0x14u) | 0x10u;
            else ctrl->head.b.flags = (ctrl->head.b.flags & ~0x14u) | 0x04u;
            s_bgEditor.dirty = 1;
        }
        break;
    case BG_ACT_CTRL_TOGGLE_PLAY:
        if (ctrls && s_bgEditor.selectedCtrl >= 0 && s_bgEditor.selectedCtrl < ctrlCount) {
            GeomCtrlEntry *ctrl = &ctrls[s_bgEditor.selectedCtrl];
            if ((ctrl->head.b.flags & 2) && (ctrl->head.b.flags & 0x14)) {
                ctrl->head.b.flags &= ~2u;
            } else {
                ctrl->head.b.flags |= 0x26u;
                if (ctrl->step == 0) ctrl->step = 0x100;
            }
            s_bgEditor.dirty = 1;
        }
        break;

    case BG_ACT_LAYERS_ALL_ON:
        if (entries) {
            for (int i = 0; i < entryCount; ++i) {
                if (entries[i].group == group) entries[i].head.b.flags |= 2u;
            }
            s_bgEditor.dirty = 1;
        }
        break;
    case BG_ACT_LAYERS_ALL_OFF:
        if (entries) {
            int l1 = BgEditor_FindLayer1Entry(group);
            for (int i = 0; i < entryCount; ++i) {
                if (entries[i].group == group && i != l1) entries[i].head.b.flags &= ~2u;
            }
            s_bgEditor.dirty = 1;
        }
        break;
    case BG_ACT_LAYERS_PAGE_PREV: s_bgEditor.entryListScroll -= 4; s_bgEditor.dirty = 1; break;
    case BG_ACT_LAYERS_PAGE_NEXT: s_bgEditor.entryListScroll += 4; s_bgEditor.dirty = 1; break;
    case BG_ACT_LAYER_SELECT:
        s_bgEditor.selectedEntry = param;
        s_bgEditor.selectedTile = 0;
        s_bgEditor.dirty = 1;
        break;
    case BG_ACT_LAYER_TOGGLE_VIS:
        if (entries && param >= 0 && param < entryCount) {
            entries[param].head.b.flags ^= 2u;
            s_bgEditor.selectedEntry = param;
            s_bgEditor.dirty = 1;
        }
        break;
    case BG_ACT_LAYER_EDIT_TILES:
        s_bgEditor.selectedEntry = param;
        s_bgEditor.selectedTile = 0;
        s_bgEditor.inspectorTab = BG_TAB_PAINTER;
        s_bgEditor.dirty = 1;
        break;

    case BG_ACT_LAYER_X_M4:
    case BG_ACT_LAYER_X_M1:
    case BG_ACT_LAYER_X_P1:
    case BG_ACT_LAYER_X_P4:
    case BG_ACT_LAYER_Y_M4:
    case BG_ACT_LAYER_Y_M1:
    case BG_ACT_LAYER_Y_P1:
    case BG_ACT_LAYER_Y_P4:
        if (entries && s_bgEditor.selectedEntry >= 0 && s_bgEditor.selectedEntry < entryCount) {
            GeomEntry *sel = &entries[s_bgEditor.selectedEntry];
            int dx = (action == BG_ACT_LAYER_X_M4) ? -4 : (action == BG_ACT_LAYER_X_M1) ? -1 :
                     (action == BG_ACT_LAYER_X_P1) ?  1 : (action == BG_ACT_LAYER_X_P4) ?  4 : 0;
            int dy = (action == BG_ACT_LAYER_Y_M4) ? -4 : (action == BG_ACT_LAYER_Y_M1) ? -1 :
                     (action == BG_ACT_LAYER_Y_P1) ?  1 : (action == BG_ACT_LAYER_Y_P4) ?  4 : 0;
            sel->scr_x = (u16)((s16)sel->scr_x + dx);
            sel->scr_y = (u16)((s16)sel->scr_y + dy);
            sel->base_x = sel->scr_x;
            sel->base_y = sel->scr_y;
            s_bgEditor.dirty = 1;
        }
        break;
    case BG_ACT_LAYER_Z_M64:
    case BG_ACT_LAYER_Z_M16:
    case BG_ACT_LAYER_Z_P16:
    case BG_ACT_LAYER_Z_P64:
        if (entries && s_bgEditor.selectedEntry >= 0 && s_bgEditor.selectedEntry < entryCount) {
            GeomEntry *sel = &entries[s_bgEditor.selectedEntry];
            int dz = (action == BG_ACT_LAYER_Z_M64) ? -64 : (action == BG_ACT_LAYER_Z_M16) ? -16 :
                     (action == BG_ACT_LAYER_Z_P16) ?  16 : 64;
            int z = (int)((sel->head.packed >> 8) & 0xFFFu) + dz;
            if (z < 0) z = 0;
            if (z > 0xFFF) z = 0xFFF;
            sel->head.packed = (sel->head.packed & 0xFFF000FFu) | ((u32)(z & 0xFFFu) << 8);
            s_bgEditor.dirty = 1;
        }
        break;
    case BG_ACT_LAYER_TOGGLE_SCROLL:
        if (entries && s_bgEditor.selectedEntry >= 0 && s_bgEditor.selectedEntry < entryCount) {
            GeomEntry *sel = &entries[s_bgEditor.selectedEntry];
            sel->head.b.flags ^= 4u;
            if ((sel->head.b.flags & 4u) && sel->anim_mod_x == 0) {
                sel->anim_mod_x = 320;
                sel->anim_mod_y = 224;
                if (sel->speedX == 0 && sel->speedY == 0) sel->speedX = 128;
            }
            s_bgEditor.dirty = 1;
        }
        break;
    case BG_ACT_LAYER_TOGGLE_PARALLAX:
        if (entries && s_bgEditor.selectedEntry >= 0 && s_bgEditor.selectedEntry < entryCount) {
            GeomEntry *sel = &entries[s_bgEditor.selectedEntry];
            sel->head.b.flags ^= 8u;
            if ((sel->head.b.flags & 8u) && sel->speedX == 0) sel->speedX = 128;
            s_bgEditor.dirty = 1;
        }
        break;
    case BG_ACT_LAYER_SPDX_MINUS:
    case BG_ACT_LAYER_SPDX_PLUS:
    case BG_ACT_LAYER_SPDY_MINUS:
    case BG_ACT_LAYER_SPDY_PLUS:
        if (entries && s_bgEditor.selectedEntry >= 0 && s_bgEditor.selectedEntry < entryCount) {
            GeomEntry *sel = &entries[s_bgEditor.selectedEntry];
            if (action == BG_ACT_LAYER_SPDX_MINUS) sel->speedX -= 64;
            if (action == BG_ACT_LAYER_SPDX_PLUS)  sel->speedX += 64;
            if (action == BG_ACT_LAYER_SPDY_MINUS) sel->speedY -= 64;
            if (action == BG_ACT_LAYER_SPDY_PLUS)  sel->speedY += 64;
            s_bgEditor.dirty = 1;
        }
        break;

    case BG_ACT_PAINTER_LAYER_PREV:
    case BG_ACT_PAINTER_LAYER_NEXT:
        if (entries && entryCount > 0) {
            int cur = s_bgEditor.selectedEntry;
            int step = (action == BG_ACT_PAINTER_LAYER_NEXT) ? 1 : -1;
            for (int k = 0; k < entryCount; ++k) {
                cur = (cur + step + entryCount) % entryCount;
                if (entries[cur].group == group && entries[cur].prim_count > 0) {
                    s_bgEditor.selectedEntry = cur;
                    s_bgEditor.selectedTile = 0;
                    s_bgEditor.dirty = 1;
                    break;
                }
            }
        }
        break;
    case BG_ACT_PAINTER_TILE_PREV:
    case BG_ACT_PAINTER_TILE_NEXT:
        if (entries && s_bgEditor.selectedEntry >= 0 && s_bgEditor.selectedEntry < entryCount) {
            int count = (int)entries[s_bgEditor.selectedEntry].prim_count;
            if (count > 0) {
                int step = (action == BG_ACT_PAINTER_TILE_NEXT) ? 1 : -1;
                s_bgEditor.selectedTile = (s_bgEditor.selectedTile + step + count) % count;
                s_bgEditor.dirty = 1;
            }
        }
        break;
    case BG_ACT_TILE_TOGGLE_SEMI:
    case BG_ACT_TILE_CYCLE_ABR:
    case BG_ACT_TILE_X_M16:
    case BG_ACT_TILE_X_P16:
    case BG_ACT_TILE_Y_M16:
    case BG_ACT_TILE_Y_P16:
    case BG_ACT_TILE_U_P16:
    case BG_ACT_TILE_V_P16: {
        u32 *posWord = NULL;
        RenderTileTexture *tex = NULL;
        if (BgEditor_GetSelectedTileInfo(NULL, NULL, NULL, NULL, NULL, NULL, &posWord, &tex)) {
            GeomEntry *entry = &entries[s_bgEditor.selectedEntry];
            if (action == BG_ACT_TILE_TOGGLE_SEMI) {
                tex->words[1] ^= 0x10000000u;
            } else if (action == BG_ACT_TILE_CYCLE_ABR) {
                u32 abr = ((tex->words[0] >> 22) + 1u) & 3u;
                tex->words[0] = (tex->words[0] & ~(3u << 22)) | (abr << 22);
            } else if (action == BG_ACT_TILE_U_P16) {
                tex->bytes[4] = (uint8_t)(tex->bytes[4] + 16);
            } else if (action == BG_ACT_TILE_V_P16) {
                tex->bytes[3] = (uint8_t)(tex->bytes[3] + 16);
            } else {
                int txPos = (int)(*posWord >> 22);
                int tyPos = (int)((*posWord >> 12) & 0x3FFu);
                int tzPos = (int)(*posWord & 0xFFFu);
                if (action == BG_ACT_TILE_X_M16) txPos = (txPos - 16) & 0x3FF;
                if (action == BG_ACT_TILE_X_P16) txPos = (txPos + 16) & 0x3FF;
                if (action == BG_ACT_TILE_Y_M16) tyPos = (tyPos - 16) & 0x3FF;
                if (action == BG_ACT_TILE_Y_P16) tyPos = (tyPos + 16) & 0x3FF;
                *posWord = ((u32)(txPos & 0x3FF) << 22) | ((u32)(tyPos & 0x3FF) << 12) | (u32)(tzPos & 0xFFF);
            }
            BgEditor_RebuildEntryPackets(entry);
            s_bgEditor.dirty = 1;
        }
        break;
    }
    case BG_ACT_CLUT_SELECT:
        s_bgEditor.selectedClutIdx = param & 15;
        s_bgEditor.dirty = 1;
        break;
    case BG_ACT_CLUT_R_MINUS: BgEditor_ModifySelectedClutColor(-1, 0, 0, 0, 0); break;
    case BG_ACT_CLUT_R_PLUS:  BgEditor_ModifySelectedClutColor( 1, 0, 0, 0, 0); break;
    case BG_ACT_CLUT_G_MINUS: BgEditor_ModifySelectedClutColor(0, -1, 0, 0, 0); break;
    case BG_ACT_CLUT_G_PLUS:  BgEditor_ModifySelectedClutColor(0,  1, 0, 0, 0); break;
    case BG_ACT_CLUT_B_MINUS: BgEditor_ModifySelectedClutColor(0, 0, -1, 0, 0); break;
    case BG_ACT_CLUT_B_PLUS:  BgEditor_ModifySelectedClutColor(0, 0,  1, 0, 0); break;
    case BG_ACT_CLUT_R_BAR:
    case BG_ACT_CLUT_G_BAR:
    case BG_ACT_CLUT_B_BAR:
        s_bgEditor.draggingRgbChannel = param;
        BgEditor_SetSelectedClutChannelFromBar(param, clickX);
        break;
    case BG_ACT_CLUT_TOGGLE_STP:  BgEditor_ModifySelectedClutColor(0, 0, 0, 1, 0); break;
    case BG_ACT_CLUT_TOGGLE_ZERO: BgEditor_ModifySelectedClutColor(0, 0, 0, 0, 1); break;

    case BG_ACT_PRESET_ROOM: BgEditor_LoadRoom(param, 0); break;
    case BG_ACT_CAM_LEFT:    BgEditor_NudgeCamera(-8, 0, 0); break;
    case BG_ACT_CAM_RIGHT:   BgEditor_NudgeCamera( 8, 0, 0); break;
    case BG_ACT_CAM_UP:      BgEditor_NudgeCamera(0, -8, 0); break;
    case BG_ACT_CAM_DOWN:    BgEditor_NudgeCamera(0,  8, 0); break;
    case BG_ACT_CAM_CENTER:  BgEditor_NudgeCamera(0,  0, 1); break;
    case BG_ACT_ROOM_RESET:  BgEditor_LoadRoom(s_bgEditor.mapNumber, 0); break;
    default:
        break;
    }
}

static void BgEditor_HandleMouseDown(float x, float y, int button) {
    int group = g_GeomScroll.state.position.groupSelect;

    /* Right-click or Middle-click on main viewport -> start camera pan */
    if (button == SDL_BUTTON_RIGHT || button == SDL_BUTTON_MIDDLE) {
        if (BgEditor_HitRect(x, y, 40.0f, 112.0f, 640.0f, 448.0f)) {
            s_bgEditor.draggingPan = 1;
            s_bgEditor.panDragStartX = x;
            s_bgEditor.panDragStartY = y;
            s_bgEditor.panOrigCamX = g_GeomScroll.state.position.x;
            s_bgEditor.panOrigCamY = g_GeomScroll.state.position.y;
            return;
        }
        /* Right-click in 16x16 pixel grid -> eyedropper pick color! */
        if (s_bgEditor.inspectorTab == BG_TAB_PAINTER &&
            BgEditor_HitRect(x, y, 730.0f, 228.0f, 256.0f, 256.0f)) {
            BgEditor_PaintTexelInSelectedTile((int)((x - 730.0f) / 16.0f),
                                              (int)((y - 228.0f) / 16.0f), 1);
            return;
        }
        return;
    }

    if (button != SDL_BUTTON_LEFT) return;

    /* Check registered buttons in reverse draw order so top-most buttons win! */
    for (int i = s_bgEditor.widgetCount - 1; i >= 0; --i) {
        const BgWidget *w = &s_bgEditor.widgets[i];
        if (BgEditor_HitRect(x, y, w->x, w->y, w->w, w->h)) {
            BgEditor_ExecuteAction(w->action, w->param, x);
            return;
        }
    }

    /* Check 16x16 tile pixel painter grid (x = 730..986, y = 228..484) */
    if (s_bgEditor.inspectorTab == BG_TAB_PAINTER &&
        BgEditor_HitRect(x, y, 730.0f, 228.0f, 256.0f, 256.0f)) {
        s_bgEditor.draggingTexel = 1;
        BgEditor_PaintTexelInSelectedTile((int)((x - 730.0f) / 16.0f),
                                          (int)((y - 228.0f) / 16.0f), 0);
        return;
    }

    /* Check Main 2x Canvas (x = 40..680, y = 112..560) -> select clicked tile & layer! */
    if (s_bgEditor.viewMode != BG_VIEW_VRAM_ATLAS &&
        BgEditor_HitRect(x, y, 40.0f, 112.0f, 640.0f, 448.0f)) {
        int cx = (int)((x - 40.0f) / 640.0f * (float)BG_CANVAS_W);
        int cy = (int)((y - 112.0f) / 448.0f * (float)BG_CANVAS_H);
        if (cx >= 0 && cx < BG_CANVAS_W && cy >= 0 && cy < BG_CANVAS_H) {
            int pIdx = cy * BG_CANVAS_W + cx;
            if (s_bgEditor.pickEntry[pIdx] >= 0) {
                s_bgEditor.selectedEntry = s_bgEditor.pickEntry[pIdx];
                s_bgEditor.selectedTile = s_bgEditor.pickTile[pIdx];
                int ownerCtrl = BgEditor_FindControllerForEntry(s_bgEditor.selectedEntry, group);
                if (ownerCtrl >= 0) s_bgEditor.selectedCtrl = ownerCtrl;
                s_bgEditor.dirty = 1;
            }
        }
    }
}

static void BgEditor_WindowToDesign(SDL_Window *window, float winX, float winY,
                                    float *outX, float *outY) {
    int logW = 0, logH = 0;
    if (window) {
        SDL_GetWindowSize(window, &logW, &logH);
    }
    float rx = (logW > 0 && s_overlayWindowW > 0) ? ((float)s_overlayWindowW / (float)logW) : 1.0f;
    float ry = (logH > 0 && s_overlayWindowH > 0) ? ((float)s_overlayWindowH / (float)logH) : 1.0f;
    float scale = s_overlayLayout.scale > 0.0f ? s_overlayLayout.scale : 1.0f;
    *outX = (winX * rx - (float)s_overlayLayout.x) / scale;
    *outY = (winY * ry - (float)s_overlayLayout.y) / scale;
}

static void BgEditor_ProcessEvent(const SDL_Event *event, SDL_Window *window, int *outQuit) {
    if (event->type == SDL_EVENT_QUIT ||
        (window && event->type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
         event->window.windowID == SDL_GetWindowID(window))) {
        s_bgEditor.active = 0;
        if (outQuit) *outQuit = 1;
        return;
    }
    if (event->type == SDL_EVENT_WINDOW_RESIZED ||
        event->type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED ||
        event->type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED) {
        s_bgEditor.dirty = 1;
        return;
    }
    if (event->type == SDL_EVENT_MOUSE_BUTTON_UP) {
        s_bgEditor.draggingTexel = 0;
        s_bgEditor.draggingRgbChannel = -1;
        s_bgEditor.draggingPan = 0;
        return;
    }
    if (event->type == SDL_EVENT_MOUSE_MOTION) {
        float dx = 0.0f, dy = 0.0f;
        BgEditor_WindowToDesign(window, event->motion.x, event->motion.y, &dx, &dy);
        s_bgEditor.mouseX = dx;
        s_bgEditor.mouseY = dy;
        s_bgEditor.dirty = 1;

        if (s_bgEditor.draggingPan) {
            int deltaX = (int)((s_bgEditor.panDragStartX - dx) * 0.5f);
            int deltaY = (int)((s_bgEditor.panDragStartY - dy) * 0.5f);
            g_GeomScroll.state.position.x = (s16)(s_bgEditor.panOrigCamX + deltaX);
            g_GeomScroll.state.position.y = (s16)(s_bgEditor.panOrigCamY + deltaY);
            return;
        }
        if (s_bgEditor.draggingRgbChannel >= 0) {
            BgEditor_SetSelectedClutChannelFromBar(s_bgEditor.draggingRgbChannel, dx);
            return;
        }
        if (s_bgEditor.draggingTexel && BgEditor_HitRect(dx, dy, 730.0f, 228.0f, 256.0f, 256.0f)) {
            BgEditor_PaintTexelInSelectedTile((int)((dx - 730.0f) / 16.0f),
                                              (int)((dy - 228.0f) / 16.0f), 0);
            return;
        }

        int hx = -1, hy = -1;
        if (s_bgEditor.viewMode != BG_VIEW_VRAM_ATLAS &&
            BgEditor_HitRect(dx, dy, 40.0f, 112.0f, 640.0f, 448.0f)) {
            hx = (int)((dx - 40.0f) / 640.0f * (float)BG_CANVAS_W);
            hy = (int)((dy - 112.0f) / 448.0f * (float)BG_CANVAS_H);
        }
        s_bgEditor.hoverCanvasX = hx;
        s_bgEditor.hoverCanvasY = hy;
        return;
    }
    if (event->type == SDL_EVENT_MOUSE_WHEEL) {
        if (event->wheel.y > 0) s_bgEditor.entryListScroll--;
        else if (event->wheel.y < 0) s_bgEditor.entryListScroll++;
        s_bgEditor.dirty = 1;
        return;
    }
    if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        float dx = 0.0f, dy = 0.0f;
        BgEditor_WindowToDesign(window, event->button.x, event->button.y, &dx, &dy);
        s_bgEditor.mouseX = dx;
        s_bgEditor.mouseY = dy;
        BgEditor_HandleMouseDown(dx, dy, event->button.button);
        return;
    }
    if (event->type == SDL_EVENT_KEY_DOWN) {
        switch (event->key.scancode) {
        case SDL_SCANCODE_ESCAPE:
        case SDL_SCANCODE_F2:
            if (!event->key.repeat) s_bgEditor.active = 0;
            break;
        case SDL_SCANCODE_LEFTBRACKET:
            BgEditor_StepAdjacentRoom((event->key.mod & SDL_KMOD_SHIFT) ? -10 : -1, 0);
            break;
        case SDL_SCANCODE_RIGHTBRACKET:
            BgEditor_StepAdjacentRoom((event->key.mod & SDL_KMOD_SHIFT) ? 10 : 1, 0);
            break;
        case SDL_SCANCODE_N:
            BgEditor_StepAdjacentRoom(1, 1);
            break;
        case SDL_SCANCODE_TAB:
            BgEditor_SwitchGroup(g_GeomScroll.state.position.groupSelect + 1);
            break;
        case SDL_SCANCODE_SPACE:
            if (!event->key.repeat) {
                s_bgEditor.playing ^= 1;
                s_bgEditor.dirty = 1;
            }
            break;
        case SDL_SCANCODE_PERIOD:
            Render_StepGroupAnimations();
            Render_ScrollEntries();
            s_bgEditor.dirty = 1;
            break;
        case SDL_SCANCODE_A:
            s_bgEditor.autoActivateAnims ^= 1;
            if (s_bgEditor.autoActivateAnims) {
                BgEditor_ActivateGroupAnimations(g_GeomScroll.state.position.groupSelect);
            }
            s_bgEditor.dirty = 1;
            break;
        case SDL_SCANCODE_P:
            s_bgEditor.autoPanCamera ^= 1;
            s_bgEditor.dirty = 1;
            break;
        case SDL_SCANCODE_G:
            s_bgEditor.showGrid ^= 1;
            s_bgEditor.dirty = 1;
            break;
        case SDL_SCANCODE_E:
            BgEditor_ExportCurrentRoomBmp((event->key.mod & SDL_KMOD_SHIFT) != 0);
            break;
        case SDL_SCANCODE_1: s_bgEditor.viewMode = BG_VIEW_COMPOSITE; s_bgEditor.dirty = 1; break;
        case SDL_SCANCODE_2: s_bgEditor.viewMode = BG_VIEW_LAYER1; s_bgEditor.dirty = 1; break;
        case SDL_SCANCODE_3: s_bgEditor.viewMode = BG_VIEW_LAYER2_ANIM; s_bgEditor.dirty = 1; break;
        case SDL_SCANCODE_4: s_bgEditor.viewMode = BG_VIEW_ZDEPTH; s_bgEditor.dirty = 1; break;
        case SDL_SCANCODE_5: s_bgEditor.viewMode = BG_VIEW_VRAM_ATLAS; s_bgEditor.dirty = 1; break;
        case SDL_SCANCODE_LEFT:  BgEditor_NudgeCamera(-8, 0, 0); break;
        case SDL_SCANCODE_RIGHT: BgEditor_NudgeCamera( 8, 0, 0); break;
        case SDL_SCANCODE_UP:    BgEditor_NudgeCamera(0, -8, 0); break;
        case SDL_SCANCODE_DOWN:  BgEditor_NudgeCamera(0,  8, 0); break;
        default:
            break;
        }
    }
}

static void BgEditor_RefreshGPU(SDL_GPUDevice *device, SDL_Window *window) {
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(window, &w, &h);
    if (w <= 0 || h <= 0 || !OverlayFontReady()) return;

    float scale = (float)w * 0.96f / (float)BG_STUDIO_W;
    float hLimit = (float)h * 0.96f / (float)BG_STUDIO_H;
    if (scale > hLimit) scale = hLimit;
    if (scale < 0.5f) scale = 0.5f;
    scale = floorf(scale * 16.0f) / 16.0f;

    OverlayLayout layout;
    layout.scale = scale;
    layout.w = (int)ceilf((float)BG_STUDIO_W * scale);
    layout.h = (int)ceilf((float)BG_STUDIO_H * scale);
    layout.x = (w - layout.w) / 2;
    layout.y = (h - layout.h) / 2;

    if (w != s_overlayWindowW || h != s_overlayWindowH ||
        layout.w != s_overlayLayout.w || layout.h != s_overlayLayout.h) {
        s_bgEditor.dirty = 1;
    }
    s_overlayWindowW = w;
    s_overlayWindowH = h;
    s_overlayLayout = layout;

    if (!s_bgEditor.dirty) return;
    if (!OverlaySurfaceResize(&s_overlaySurface, layout.w, layout.h)) return;

    BgEditor_PaintStudio(&s_overlaySurface, &layout);
    if (Host_EnsureOverlayPipeline(device, SDL_GetGPUSwapchainTextureFormat(device, window))) {
        Host_UploadOverlaySurfaceAndVertices(device);
    }
    s_bgEditor.dirty = 0;
}

static void Host_RunBackgroundEditor(SDL_Window *game, int initialRoom, int fromLiveGame) {
    if (!game || !s_overlayGpuDevice) return;

    int paused = Psyz_AudioIsPaused();
    Psyz_PauseHostClock_SDL3(true);
    Psyz_AudioPause();
    SDL_ShowCursor();

    u32 savedGameFlags = g_GameStateFlags;
    u32 savedStateFlags = g_GameState.flags;
    g_GameStateFlags &= ~0x104u;
    g_GameState.flags &= ~0xC00000u;

    int liveRoom = Save_GetCurrentMapNumber();
    if (fromLiveGame && g_GeomState && liveRoom >= 1 && liveRoom <= SCENE_ROOM_COUNT) {
        BgEditor_LoadRoom(liveRoom, 1);
    } else {
        BgEditor_LoadRoom(initialRoom >= 1 ? initialRoom : s_bgEditor.mapNumber, 0);
    }

    s_bgEditor.active = 1;
    s_bgEditor.quit = 0;
    s_bgEditor.dirty = 1;
    s_overlayState.open = 1;

    while (s_bgEditor.active) {
        if (s_bgEditor.playing && g_GeomState) {
            s_bgEditor.tickDivider ^= 1;
            if (s_bgEditor.tickDivider == 0) {
                Render_StepGroupAnimations();
                Render_ScrollEntries();
                if (s_bgEditor.autoPanCamera) {
                    int group = g_GeomScroll.state.position.groupSelect;
                    int vCount = 0;
                    CameraViewport *views = BgEditor_ViewportArray(&vCount);
                    if (views && group < vCount) {
                        CameraViewport *vw = &views[group];
                        s_bgEditor.panPhase = (s_bgEditor.panPhase + 24) & 4095;
                        int cx = (vw->minX + vw->maxX) / 2;
                        int cy = (vw->minY + vw->maxY) / 2;
                        int rx = (vw->maxX - vw->minX) / 2;
                        int ry = (vw->maxY - vw->minY) / 2;
                        if (rx == 0 && ry == 0) { rx = 24; ry = 12; }
                        g_GeomScroll.state.position.x = (s16)(cx + (rsin(s_bgEditor.panPhase) * rx) / 4096);
                        g_GeomScroll.state.position.y = (s16)(cy + (rcos(s_bgEditor.panPhase) * ry) / 4096);
                    }
                }
                s_bgEditor.dirty = 1;
            }
        }

        BgEditor_RefreshGPU(s_overlayGpuDevice, game);
        Psyz_VideoVSync(0);
    }

    s_overlayState.open = 0;
    g_GameStateFlags = savedGameFlags;
    g_GameState.flags = savedStateFlags;

    SDL_ResetKeyboard();
    Psyz_PauseHostClock_SDL3(false);
    if (!paused) {
        Psyz_AudioUnpause();
    }
    if (s_bgEditor.quit) {
        SDL_Event ev;
        SDL_zero(ev);
        ev.type = SDL_EVENT_QUIT;
        SDL_PushEvent(&ev);
    }
}

#endif /* PE_BG_EDITOR_H */
