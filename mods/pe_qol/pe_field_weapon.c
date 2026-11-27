#include <string.h>
#include <libgpu.h>
#include "pe1/scene_dispatch.h"
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

enum { MODEL_SECTORS = 9, MODEL_OFFSET = 8, TEXTURE_SECTORS = 3, TEXTURE_RECORDS = 10 };

typedef struct PeFieldWeaponBank {
    unsigned model_sector, model_sectors, texture_sector, texture_sectors, texture_records;
    uint64_t model_hash, texture_hash;
} PeFieldWeaponBank;

/* Native 29-part USA battle banks for firearm kinds 1–6. The room renderer
 * normally uploads the corresponding texture bank on story days 2?6. */
static const PeFieldWeaponBank s_weapon_banks[] = {
    {483, 6, 537, 2, 7, UINT64_C(0xedfea82fb1090b62), UINT64_C(0x7bff210bb5a251f4)},
    {489, 8, 539, 2, 7, UINT64_C(0x5eeac8b8a4501434), UINT64_C(0x9372720fc7e6d467)},
    {497, 6, 541, 3, 10, UINT64_C(0x7d74c13d9f980f85), UINT64_C(0xcca2a0da10a917df)},
    {503, 7, 544, 3, 10, UINT64_C(0xd1da63863bb758eb), UINT64_C(0xede0135c000519ef)},
    {510, 9, 547, 3, 11, UINT64_C(0xa85a3697ef0fbf19), UINT64_C(0xfc17e4a2618d8029)},
    {519, 7, 550, 3, 7, UINT64_C(0xc3aec2d2d2080d06), UINT64_C(0xe43df39fcca5e86f)},
};

static const PeFieldWeaponBank *pe_field_bank(const ItemDataRecord *weapon) {
    return weapon && weapon->kind >= 1 && weapon->kind <= 7 ?
        &s_weapon_banks[weapon->kind == 7 ? 0 : weapon->kind - 1] : NULL;
}

static int s_enabled = 1;
/* Keep the Opera prototype off until the cutscene option is ready to expose. */
static int s_cutscene_enabled;
static int s_opera_active;
static int s_opera_seen_entry;
static int s_opera_ready;
static int s_opera_pose_applied;
static RenderObjectEntity s_opera_rifle;
static _Alignas(RenderPrimHead) u8 s_opera_work[64 * 1024];
enum { OPERA_W = 19, OPERA_H = 118, OPERA_MAP = 0xA80002C8u };
static _Alignas(u_long) u16 s_opera_image[OPERA_W * OPERA_H];
static _Alignas(u_long) u16 s_opera_saved_image[OPERA_W * OPERA_H];
static _Alignas(u_long) u16 s_opera_palette[TEXTURE_RECORDS][16];
static _Alignas(u_long) u16 s_opera_saved_palette[TEXTURE_RECORDS][16];
static u16 s_opera_clut[TEXTURE_RECORDS];
static FieldActor *s_opera_actor;

static int s_prepared;
static const PeFieldWeaponBank *s_bank;
static const PeFieldWeaponBank *s_field_rifle_bank;
static RenderAnimationDataHeader *s_carry_animation;
static int s_drew_rifle;
static const RenderObjectEntity *s_field_rifle_owner;
static int s_field_handoff_logged;
static const PeFieldWeaponBank *s_reported_draw;
static RenderObjectEntity s_rifle;
static _Alignas(u32) u8 s_model[MODEL_SECTORS * 2048];
static _Alignas(RenderPrimHead) u8 s_work[64 * 1024];
static _Alignas(u32) u8 s_texture[TEXTURE_SECTORS * 2048];
static const TimUploadRecord *s_records;

static void pe_opera_release(void);

static int pe_retry_rifle(void *ctx, void *user) {
    (void)ctx;
    (void)user;
    pe_opera_release();
    s_opera_actor = NULL;
    s_opera_seen_entry = 0;
    s_field_rifle_owner = NULL;
    s_field_handoff_logged = 0;
    if (!s_prepared) s_bank = NULL;
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
    /* Bit 1 hides the stock prop. Bit 3 can be set during native locomotion. */
    return s_enabled && g_GameRegion == PE_REGION_USA && pe_game_loaded() && pe_free_field() &&
        !(g_FieldMoveLock & ~0xau) && !g_GameState.scene_init_subphase &&
        (!costumes || !costumes->worn()) && pe_field_bank(weapon);
}

static int pe_opera_context(void) {
    const PeCostumesApi *costumes = pe_service_get("costumes", 1);
    const ItemDataRecord *weapon = pe_equipped(0);
    return s_cutscene_enabled && g_GameRegion == PE_REGION_USA &&
        (unsigned)g_SceneDispatchToken == OPERA_MAP && g_GameState.current_story_day == 1 &&
        pe_game_loaded() && !(g_GameStateFlags & 2) && !g_GameState.scene_init_subphase &&
        g_PlayerEntity && g_PlayerEntity->render_object.header &&
        g_PlayerEntity->render_object.header->part_count == 31 &&
        (!costumes || !costumes->worn()) && weapon &&
        (weapon->itemId == ITEM_M16A1 || weapon->itemId == ITEM_M16A2);
}

static RECT pe_opera_texture_rect(int i) {
    if (i == -1) return (RECT){736, 64, OPERA_W, OPERA_H};
    return (RECT){768 + (i % 4) * 16, 259 + i / 4, 16, 1};
}

static void pe_opera_release(void) {
    if (!s_opera_active) return;
    for (int i = -1; i < TEXTURE_RECORDS; i++) {
        RECT rect = pe_opera_texture_rect(i);
        u16 *saved = i < 0 ? s_opera_saved_image : s_opera_saved_palette[i];
        const u16 *uploaded = i < 0 ? s_opera_image : s_opera_palette[i];
        _Alignas(u_long) u16 current[OPERA_W * OPERA_H];
        StoreImage(&rect, (void *)current);
        /* If the engine has already repurposed a region, preserve its writes. */
        if (!memcmp(current, uploaded, (size_t)rect.w * rect.h * sizeof(u16)))
            LoadImage(&rect, (void *)saved);
    }
    s_opera_active = 0;
    s_opera_pose_applied = 0;
    pe_log("cutscene_weapon", "Opera texture scope ended");
}

static int pe_opera_load(void) {
    /* Admit the traced slots only while they still match the empty baseline. */
    for (int i = -1; i < TEXTURE_RECORDS; i++) {
        RECT rect = pe_opera_texture_rect(i);
        u16 *saved = i < 0 ? s_opera_saved_image : s_opera_saved_palette[i];
        if (StoreImage(&rect, (void *)saved) < 0) return 0;
        for (int word = 0; word < rect.w * rect.h; word++)
            if (saved[word]) return 0;
    }
    for (int i = -1; i < TEXTURE_RECORDS; i++) {
        RECT rect = pe_opera_texture_rect(i);
        u16 *pixels = i < 0 ? s_opera_image : s_opera_palette[i];
        LoadImage(&rect, (void *)pixels);
    }
    s_opera_active = 1;
    pe_log("cutscene_weapon", "Opera texture scope started");
    return 1;
}

static int pe_opera_palette_slot(u16 clut) {
    for (int i = 0; i < TEXTURE_RECORDS; i++)
        if (s_opera_clut[i] == clut) return i;
    return -1;
}

static int pe_opera_remap(u16 *clut, u16 page, u8 *u, u8 *v, int vertices) {
    const int slot = pe_opera_palette_slot(*clut);
    if (slot < 0 || (page & 0x19f) != 11) return 0;
    for (int i = 0; i < vertices; i++) {
        if (u[i] < 56 || u[i] > 131 || v[i] < 138) return 0;
        u[i] += 72;
        v[i] -= 74;
    }
    const RECT rect = pe_opera_texture_rect(slot);
    *clut = GetClut(rect.x, rect.y);
    return 1;
}

static int pe_opera_prepare(void) {
    for (int i = 0; i < TEXTURE_RECORDS; i++) {
        const TimUploadRecord *tim = &s_records[i];
        const u32 packed = tim->image_rectangle;
        const int x = (packed >> 10) & 0x7ff, y = packed >> 21, w = packed & 0x3ff;
        const int height = tim->image_source.bytes.height;
        const u8 *image = s_texture + (tim->image_source.offsetAndHeight & 0xffffff);
        const int left = x < 718 ? 718 : x, right = x + w > 737 ? 737 : x + w;
        const int top = y < 138 ? 138 : y, bottom = y + height > 256 ? 256 : y + height;
        for (int row = top; row < bottom && left < right; row++)
            memcpy(&s_opera_image[(row - 138) * OPERA_W + left - 718],
                   image + 2 * ((row - y) * w + left - x), 2 * (right - left));
        const u32 palette = tim->secondary_rectangle;
        s_opera_clut[i] = GetClut((palette >> 10) & 0x7ff, palette >> 21);
        memcpy(s_opera_palette[i], image + (tim->secondary_source.offsetAndHeight & 0xffffff), 32);
    }

    s8 *texture_setup;
    Render_SetupEntityPrims(&s_opera_rifle, (RenderObjectHeader *)(s_model + MODEL_OFFSET),
                            s_opera_work, 0x2c0, 0x80, 0, 0x1c2, 0, &texture_setup, 1);
    s_opera_rifle.shade = 0x80;
    s_opera_rifle.lightNegativeY = 0xc;
    s_opera_rifle.lightPositiveY = 0x18;
    Render_InitRoomPrimState(&s_opera_rifle);
    RenderPacket34 *quad = (RenderPacket34 *)s_opera_rifle.primitive_buffer;
    for (int i = 0; i < 2 * s_opera_rifle.header->packet34_count; i++, quad++) {
        u8 u[] = {quad->u0, quad->u1, quad->u2, quad->u3};
        u8 v[] = {quad->v0, quad->v1, quad->v2, quad->v3};
        if (!pe_opera_remap(&quad->clut, quad->page_bits, u, v, 4)) return 0;
        quad->u0 = u[0]; quad->u1 = u[1]; quad->u2 = u[2]; quad->u3 = u[3];
        quad->v0 = v[0]; quad->v1 = v[1]; quad->v2 = v[2]; quad->v3 = v[3];
    }
    RenderPacket28 *tri = (RenderPacket28 *)quad;
    for (int i = 0; i < 2 * s_opera_rifle.header->packet28_count; i++, tri++) {
        u8 u[] = {tri->u0, tri->u1, tri->u2};
        u8 v[] = {tri->v0, tri->v1, tri->v2};
        if (!pe_opera_remap(&tri->clut, tri->page_bits, u, v, 3)) return 0;
        tri->u0 = u[0]; tri->u1 = u[1]; tri->u2 = u[2];
        tri->v0 = v[0]; tri->v1 = v[1]; tri->v2 = v[2];
    }
    return 1;
}

static int pe_prepare_rifle(void *ctx, void *user) {
    (void)ctx;
    (void)user;
    if (s_drew_rifle && s_reported_draw != s_field_rifle_bank) {
        pe_log("field_weapon", "USA battle attachment drawn with resident textures (kind %u)",
               (unsigned)(s_field_rifle_bank - s_weapon_banks) + 1);
        s_reported_draw = s_field_rifle_bank;
    }
    s_opera_pose_applied = 0;
    const int opera = pe_opera_context();
    if (!opera || g_PlayerEntity != s_opera_actor || g_PlayerEntity->mode != 31)
        pe_opera_release();
    if (!opera || g_PlayerEntity != s_opera_actor) {
        s_opera_actor = opera ? g_PlayerEntity : NULL;
        s_opera_seen_entry = 0;
    }
    if (opera && g_PlayerEntity->mode == 21) s_opera_seen_entry = 1;
    const PeFieldWeaponBank *bank = opera || pe_field_context() ? pe_field_bank(pe_equipped(0)) : NULL;
    if (!bank) return 0;
    if (s_bank == bank) {
        if (s_prepared && opera && !s_opera_ready)
            s_opera_ready = pe_opera_prepare() ? 1 : -1;
        return 0;
    }
    s_bank = bank;
    s_prepared = 0;
    s_opera_ready = 0;
    s_carry_animation = NULL;
    const size_t model_bytes = bank->model_sectors * 2048u;
    const size_t texture_bytes = bank->texture_sectors * 2048u;
    if (!CdRom_CopyArchiveSectors(bank->model_sector, s_model, bank->model_sectors) ||
        pe_asset_hash(s_model, model_bytes) != bank->model_hash ||
        !CdRom_CopyArchiveSectors(bank->texture_sector, s_texture, bank->texture_sectors) ||
        pe_asset_hash(s_texture, texture_bytes) != bank->texture_hash)
        return 0;

    const SceneAssetView *view = (const SceneAssetView *)s_texture;
    const SceneAssetDirectory *directory = SCENE_ASSET_AT(view, view->header.directoryOffset);
    s_records = SCENE_ASSET_AT(view, directory->timEntries & 0x3fffff);
    view = (const SceneAssetView *)s_model;
    directory = SCENE_ASSET_AT(view, view->header.directoryOffset);
    const SceneBankAssetRecord *rows = SCENE_ASSET_AT(view, directory->bankRowEntries & 0x3fffff);
    for (unsigned i = 0; i < directory->bankRowEntries >> 22; i++) {
        if (rows[i].source.bytes.id == 5) {
            s_carry_animation = SCENE_ASSET_AT(view, rows[i].source.offsetAndId & 0xffffff);
            break;
        }
    }
    s8 *texture_setup;
    Render_SetupEntityPrims(&s_rifle, (RenderObjectHeader *)(s_model + MODEL_OFFSET),
                            s_work, 0x2c0, 0x80, 0, 0x1c2, 0, &texture_setup, 1);
    s_rifle.shade = 0x80;
    s_rifle.lightNegativeY = 0xc;
    s_rifle.lightPositiveY = 0x18;
    Render_InitRoomPrimState(&s_rifle);
    s_prepared = 1;
    if (opera) s_opera_ready = pe_opera_prepare() ? 1 : -1;
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
    for (unsigned i = 0; i < s_bank->texture_records; i++) {
        const TimUploadRecord *tim = &s_records[i];
        const u8 *image = s_texture + (tim->image_source.offsetAndHeight & 0xffffff);
        const u8 *palette = image + (tim->secondary_source.offsetAndHeight & 0xffffff);
        u32 image_rectangle = tim->image_rectangle;
        unsigned image_height = tim->image_source.bytes.height;
        /* Rocket upload 3 overwrites the first eight rows of upload 2.
         * Its final pixels are checked by upload 3's own rectangle. */
        if (s_bank == &s_weapon_banks[5] && i == 2) {
            image_rectangle += 8u << 21;
            image_height -= 8;
            image += 8 * 2 * (image_rectangle & 0x3ff);
        }
        int palette_replaced = 0;
        for (unsigned later = i + 1; later < s_bank->texture_records; later++) {
            const TimUploadRecord *next = &s_records[later];
            if (next->secondary_rectangle == tim->secondary_rectangle &&
                next->secondary_source.bytes.height == tim->secondary_source.bytes.height)
                palette_replaced = 1;
        }
        if (!pe_rectangle_matches(vram, known, image_rectangle, image_height, image) ||
            (!palette_replaced && !pe_rectangle_matches(vram, known, tim->secondary_rectangle,
                tim->secondary_source.bytes.height, palette))) return 0;
    }
    return 1;
}

static int pe_field_carry_pose(void *ctx, void *user) {
    (void)user;
    RenderObjectEntity *actor = ctx;
    if (!s_prepared || s_bank != pe_field_bank(pe_equipped(0)) || !pe_field_context() || !actor->variant_visible ||
        !actor->header || actor->header->part_count != 29 ||
        (g_PlayerEntity->flags & 0x20000040) || (g_GameState.flags & 0xc0000) ||
        actor->rotation_overrides[0].matrix_index || actor->rotation_overrides[1].matrix_index)
        return 0;
    const unsigned row = g_PlayerEntity->mode;
    if (s_bank == &s_weapon_banks[0] || (row != 22 && row != 23)) return 0;
    RenderAnimationDataHeader *carry = s_carry_animation;
    const RenderAnimationDataHeader *move = g_PlayerEntity->action_data;
    if (!carry || !move->frame_count || carry->frame_count != 18 ||
        carry->last_bone_index != 28 ||
        !pe_rifle_textures_ready()) return 0;
    const unsigned frame = g_PlayerEntity->anim.parts.integer % move->frame_count;
    Anim_ApplyAyaArmPose(actor, carry, frame * carry->frame_count / move->frame_count);
    static unsigned reported;
    const unsigned bit = 1u << (row - 22);
    if (!(reported & bit)) {
        pe_log("field_weapon", "native carry pose applied to field %s", row == 22 ? "walk" : "run");
        reported |= bit;
    }
    return 1;
}

static int pe_opera_pose(void *ctx, void *user) {
    (void)user;
    RenderObjectEntity *actor = ctx;
    if (!pe_opera_context() || g_PlayerEntity->mode != 31 || g_FieldMoveLock != 1 ||
        !actor->variant_visible || (g_PlayerEntity->flags & 0x20000040) ||
        (g_GameState.flags & 0xc0000) || actor->rotation_overrides[0].matrix_index ||
        actor->rotation_overrides[1].matrix_index) {
        pe_opera_release();
        return 0;
    }
    if (!s_opera_active) {
        const int eligible = s_opera_seen_entry && s_opera_actor == g_PlayerEntity &&
            s_opera_ready == 1 && g_PlayerEntity->anim.parts.integer == 0;
        s_opera_seen_entry = 0;
        if (!eligible || !pe_opera_load()) return 0;
    }
    /* Initial support-arm adjustment for the Opera camera, on the native clock. */
    const unsigned frame = g_PlayerEntity->anim.parts.integer;
    const unsigned progress = frame < 15 ? frame : 15;
    actor->rotation_overrides[0] = (RenderRotationOverride){
        .matrix_index = 5, .flags = 1, .x = (u16)(128 * progress / 15)
    };
    s_opera_pose_applied = 1;
    return 1;
}

static int pe_draw_rifle(void *ctx, void *user) {
    (void)user;
    RenderObjectEntity *actor = ctx;
    const int opera = s_opera_active && s_opera_pose_applied && s_opera_actor == g_PlayerEntity &&
        g_PlayerEntity->mode == 31 && pe_opera_context();
    /* Native battle-entry opcode 85 starts streaming while the field flag
     * is still active. The draw hook only sees the field prop in battle. */
    const unsigned entry_phase = g_GameState.memcard_init_phase;
    const int battle_handoff = (g_GameStateFlags & 2) != 0 ||
        (entry_phase >= 0x37 && entry_phase <= 0x3b);
    if (battle_handoff) {
        const ItemDataRecord *weapon = pe_equipped(0);
        if (!s_enabled || actor != s_field_rifle_owner ||
            !s_field_rifle_bank || pe_field_bank(weapon) != s_field_rifle_bank) return 0;
        if (!s_field_handoff_logged) {
            pe_log("field_weapon", "suppressed stale field prop during battle entry");
            s_field_handoff_logged = 1;
        }
        /* Borrowed textures may be overwritten as battle assets stream in.
         * Leave the prop hidden until the native battle attachment is ready. */
        return 1;
    }
    if (!opera && (!s_prepared || s_bank != pe_field_bank(pe_equipped(0)) || !pe_field_context() || !actor->header ||
        actor->header->part_count <= 0x12 || !pe_rifle_textures_ready())) return 0;
    RenderObjectEntity *rifle = opera ? &s_opera_rifle : &s_rifle;
    rifle->animation_source = actor;
    rifle->animation_state = 3;
    rifle->animation_id = 0x12;
    Render_SetGteScreenOffset();
    Render_TransformVertices(rifle);
    Render_TransformMorphVertices(rifle, g_EntityRenderScratch);
    Render_DrawEntity(rifle, g_EntityRenderScratch);
    Render_ResetGteScreenOffset();
    if (opera && g_PlayerEntity->anim.parts.integer == 0)
        pe_log("cutscene_weapon", "Opera M16 and pose drawn at frame 0");
    if (!opera) {
        s_drew_rifle = 1;
        s_field_rifle_owner = actor;
        s_field_rifle_bank = s_bank;
        s_field_handoff_logged = 0;
    }
    return 1;
}

void pe_field_weapon_register(const char *mod_id) {
    pe_menu_register_toggle(PE_PAGE_MOVEMENT, "Visible field weapons",
        "Show the equipped weapon model when its textures are available.", &s_enabled, NULL);
    pe_hook_add(PE_HOOK_FIELD_WEAPON_POSE, pe_field_carry_pose, NULL, PE_PRIORITY_NORMAL, mod_id);
    pe_hook_add(PE_HOOK_CUTSCENE_WEAPON_POSE, pe_opera_pose, NULL, PE_PRIORITY_NORMAL, mod_id);
    pe_hook_add(PE_HOOK_FRAME, pe_prepare_rifle, NULL, PE_PRIORITY_NORMAL, mod_id);
    pe_hook_add(PE_HOOK_SCENE_START, pe_retry_rifle, NULL, PE_PRIORITY_NORMAL, mod_id);
    pe_hook_add(PE_HOOK_FIELD_WEAPON_DRAW, pe_draw_rifle, NULL, PE_PRIORITY_NORMAL, mod_id);
}
