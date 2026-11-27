#include <string.h>
#include <psyz/gpu.h>

#include "pe_game.h"
#include "pe_mod_host.h"
#include "pe_mod_menu.h"
#include "pe_pages.h"
#include "pe_services.h"
#include "pe1/battle.h"
#include "pe1/cdrom.h"
#include "pe1/entity_frame_update.h"
#include "pe1/game_state.h"
#include "pe1/item_ids.h"
#include "pe1/pe_image.h"
#include "pe1/player_entity.h"
#include "pe1/render_buffers.h"
#include "pe1/render_setup.h"
#include "pe1/scene_assets.h"

enum {
    MODEL_SECTOR = 448, MODEL_SECTORS = 8, MODEL_OFFSET = 8,
    TEXTURE_SECTOR = 544, TEXTURE_SECTORS = 3, TEXTURE_RECORDS = 10
};
static int s_enabled;
static int s_prepared;
static int s_attempted;
static int s_drew_rifle;
static int s_reported_draw;
static RenderObjectEntity s_rifle;
static _Alignas(u32) u8 s_model[MODEL_SECTORS * 2048];
static _Alignas(RenderPrimHead) u8 s_work[64 * 1024];
static _Alignas(u32) u8 s_texture[TEXTURE_SECTORS * 2048];
static const TimUploadRecord *s_records;

static int pe_retry_rifle(void *ctx, void *user) {
    (void)ctx;
    (void)user;
    s_attempted = s_prepared;
    return 0;
}

static uint64_t pe_asset_hash(const u8 *data, size_t size) {
    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t i = 0; i < size; i++) {
        hash ^= data[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static int pe_field_context(void) {
    const PeCostumesApi *costumes = pe_service_get("costumes", 1);
    const ItemDataRecord *weapon = pe_equipped(0);
    /* Bit 1 hides the stock field prop. Other bits can indicate scripted restrictions. */
    return s_enabled && g_GameRegion == PE_REGION_USA && pe_game_loaded() && pe_free_field() &&
        !(g_FieldMoveLock & ~2u) && !g_GameState.scene_init_subphase &&
        (!costumes || !costumes->worn()) && weapon &&
        (weapon->itemId == ITEM_M16A1 || weapon->itemId == ITEM_M16A2);
}

static int pe_prepare_rifle(void *ctx, void *user) {
    (void)ctx;
    (void)user;
    if (s_drew_rifle && !s_reported_draw) {
        pe_log("field_weapon", "USA M16 attachment drawn with resident textures");
        s_reported_draw = 1;
    }
    if (s_attempted || !pe_field_context()) return 0;
    s_attempted = 1;
    /* The native setup parser assumes a trusted model. Admit only the verified,
     * identical USA bank bytes from the two original discs. */
    if (!CdRom_CopyArchiveSectors(MODEL_SECTOR, s_model, MODEL_SECTORS) ||
        pe_asset_hash(s_model, sizeof s_model) != UINT64_C(0x963ddaecb2b271f5) ||
        !CdRom_CopyArchiveSectors(TEXTURE_SECTOR, s_texture, TEXTURE_SECTORS) ||
        pe_asset_hash(s_texture, sizeof s_texture) != UINT64_C(0xede0135c000519ef))
        return 0;

    const SceneAssetView *view = (const SceneAssetView *)s_texture;
    const SceneAssetDirectory *directory = SCENE_ASSET_AT(view, view->header.directoryOffset);
    s_records = SCENE_ASSET_AT(view, directory->timEntries & 0x3fffff);
    s8 *texture_setup;
    Render_SetupEntityPrims(&s_rifle, (RenderObjectHeader *)(s_model + MODEL_OFFSET),
                            s_work, 0x2c0, 0x80, 0, 0x1c2, 0, &texture_setup, 1);
    s_rifle.shade = 0x80;
    s_rifle.lightNegativeY = 0xc;
    s_rifle.lightPositiveY = 0x18;
    Render_InitRoomPrimState(&s_rifle);
    s_prepared = 1;
    return 0;
}

static int pe_rectangle_matches(const u16 *vram, const u8 *known, u32 rectangle,
                                unsigned height, const u8 *expected) {
    const unsigned x = (rectangle >> 10) & 0x7ff;
    const unsigned y = rectangle >> 21;
    const unsigned width = rectangle & 0x3ff;
    for (unsigned row = 0; row < height; row++) {
        const size_t start = (y + row) * 1024u + x;
        for (unsigned column = 0; column < width; column++)
            if (!known[start + column]) return 0;
        if (memcmp(vram + start, expected + 2 * row * width, 2 * width)) return 0;
    }
    return 1;
}

static int pe_rifle_textures_ready(void) {
    const u8 *known;
    const u16 *vram = Psyz_GpuGetVramCopy(&known);
    if (!vram || !known) return 0;
    for (int i = 0; i < TEXTURE_RECORDS; i++) {
        const TimUploadRecord *tim = &s_records[i];
        const u8 *image = s_texture + (tim->image_source.offsetAndHeight & 0xffffff);
        const u8 *palette = image + (tim->secondary_source.offsetAndHeight & 0xffffff);
        if (!pe_rectangle_matches(vram, known, tim->image_rectangle,
                                  tim->image_source.bytes.height, image) ||
            !pe_rectangle_matches(vram, known, tim->secondary_rectangle,
                                  tim->secondary_source.bytes.height, palette)) return 0;
    }
    return 1;
}

static int pe_draw_rifle(void *ctx, void *user) {
    (void)user;
    RenderObjectEntity *actor = ctx;
    if (!s_prepared || !pe_field_context() || !actor->header ||
        actor->header->part_count <= 0x12 || !pe_rifle_textures_ready()) return 0;

    s_rifle.animation_source = actor;
    s_rifle.animation_state = 3;
    s_rifle.animation_id = 0x12;
    Render_SetGteScreenOffset();
    Render_TransformVertices(&s_rifle);
    Render_TransformMorphVertices(&s_rifle, g_EntityRenderScratch);
    Render_DrawEntity(&s_rifle, g_EntityRenderScratch);
    Render_ResetGteScreenOffset();
    s_drew_rifle = 1;
    return 1;
}

void pe_field_weapon_register(const char *mod_id) {
    pe_menu_register_toggle(PE_PAGE_MOVEMENT, "Visible field weapons",
        "Show the equipped M16 in compatible field scenes.", &s_enabled, NULL);
    pe_hook_add(PE_HOOK_FRAME, pe_prepare_rifle, NULL, PE_PRIORITY_NORMAL, mod_id);
    pe_hook_add(PE_HOOK_SCENE_START, pe_retry_rifle, NULL, PE_PRIORITY_NORMAL, mod_id);
    pe_hook_add(PE_HOOK_FIELD_WEAPON_DRAW, pe_draw_rifle, NULL, PE_PRIORITY_NORMAL, mod_id);
}
