#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pe1/akao.h"
#include "pe1/asset_tim.h"
#include "pe1/boot_disc_check.h"
#include "pe1/cdrom.h"
#include "pe1/entity_animation.h"
#include "pe1/entity_frame_update.h"
#include "pe1/field_actor.h"
#include "pe1/field_bg_load.h"
#include "pe1/game_state.h"
#include "pe1/geom_state.h"
#include "pe1/gpu_image.h"
#include "pe1/menu_memcard_display.h"
#include "pe1/menu_state.h"
#include "pe1/pe_image.h"
#include "pe1/player_entity.h"
#include "pe1/psyq_gpu.h"
#include "pe1/render_camera.h"
#include "pe1/render_prim.h"
#include "pe1/render_shadow.h"
#include "pe1/resident_tables.h"
#include "pe1/room_drop_field.h"
#include "pe1/room_effect_sequence.h"
#include "pe1/room_ember_burst.h"
#include "pe1/room_falling_burst.h"
#include "pe1/room_flare.h"
#include "pe1/room_floor_walker.h"
#include "pe1/room_fx_model.h"
#include "pe1/room_glow_orb.h"
#include "pe1/room_grab_attack.h"
#include "pe1/room_ground_eruption.h"
#include "pe1/room_homing_flash.h"
#include "pe1/room_homing_model.h"
#include "pe1/room_line_burst.h"
#include "pe1/room_m005.h"
#include "pe1/room_m005_beacon.h"
#include "pe1/room_m023_beacon.h"
#include "pe1/room_m023_effects.h"
#include "pe1/room_m086_seeker.h"
#include "pe1/room_m089_model_pulse.h"
#include "pe1/room_m123.h"
#include "pe1/room_m123_beacon.h"
#include "pe1/room_m123_glow_burst.h"
#include "pe1/room_m256.h"
#include "pe1/room_m349_effects.h"
#include "pe1/room_m350_sweep_trap.h"
#include "pe1/room_m404.h"
#include "pe1/room_model_pulse.h"
#include "pe1/room_module.h"
#include "pe1/room_motion_init.h"
#include "pe1/room_orbit_set.h"
#include "pe1/room_paired_glow.h"
#include "pe1/room_shake_burst.h"
#include "pe1/room_sound_burst.h"
#include "pe1/room_spark_attacks.h"
#include "pe1/room_spark_barrage.h"
#include "pe1/room_spark_ring_burst.h"
#include "pe1/room_three_particle_class.h"
#include "pe1/room_trail_burst.h"
#include "pe1/room_twelve_effect.h"
#include "pe1/save_tail_state.h"
#include "pe1/scene_assets.h"
#include "pe1/scene_e20_hover_orb.h"
#include "pe1/scene_room.h"
#include "pe1/scene_room_load.h"
#include "pe1/scene_transition.h"
#include "../../overlays/room_lib/room_lib.h"
#include "../../overlays/room_m272_liberty/room_m272.h"
#include "../../overlays/room_m273_liberty/room_m273.h"
#include "../../overlays/room_m318_chrysler/m318_externs.h"
#include "../../overlays/room_m350_cruiser/room_m350_data.h"
#include "../../overlays/scene_e08_chrysler/scene_e08_modules.h"
#include "../../overlays/scene_e18_cruiser/e18_emitters_externs.h"
#include "../../overlays/scene_e19_cruiser/e19_externs.h"
#include "../../overlays/scene_e22_liberty/scene_e22_shared.h"

/* Shared native state; external variables have one storage definition. */
u8 D_8009CDC8;

_Alignas(4) u8 g_StartupResourceBlob[0x10A50];

/* File images cannot recover through PS1 drive retries. Report the failed
 * stage to the original boot caller instead of spinning or using stale data. */
int Scene_LoadSceneData(void) {
    RECT rect = {0, 0, 0x3FF, 0x1FF};
    Pe1GameState *state = &g_GameState;
    u32 lba = g_PeImageBaseLba;
    int status;
    const unsigned stages[] = {2, 3, 6, 7};

    if (!state->scene_load_scratch || !state->bank_work_far_end) return -10;
    for (unsigned i = 0; i < 4; ++i) {
        unsigned slot = stages[i];
        unsigned first = g_PeImageSectorTable[slot];
        unsigned last = g_PeImageSectorTable[slot + 1];
        if (last <= first || first > UINT32_MAX - lba) return -10;
        size_t bytes = (size_t)(last - first) * 2048;
        if ((i == 0 && bytes > sizeof(g_ResidentDataPack)) ||
            (i != 0 && bytes > 0x20000) ||
            (i == 1 && bytes < sizeof(g_StartupResourceBlob)) ||
            (i == 3 && bytes < 0x1400)) return -10;
    }
    ClearImage(&rect, 0, 0, 1);
    state->startup_resource_blob = NULL;
    state->startup_resource_tables[0] = NULL;
    state->startup_resource_tables[1] = NULL;

    if (CdRom_ReadSectorsFromLba(lba + g_PeImageSectorTable[2], &g_ResidentDataPack,
            g_PeImageSectorTable[3] - g_PeImageSectorTable[2]) < 0) return -1;
    while ((status = CdRom_PollReady()) > 0) {}
    if (status < 0) return -2;

    if (CdRom_ReadSectorsFromLba(lba + g_PeImageSectorTable[3], state->scene_load_scratch,
            g_PeImageSectorTable[4] - g_PeImageSectorTable[3]) < 0) return -3;
    Menu_FullInit();
    while ((status = CdRom_PollReady()) > 0) {}
    if (status < 0) return -4;
    memcpy(g_StartupResourceBlob, state->scene_load_scratch, sizeof(g_StartupResourceBlob));
    state->startup_resource_blob = g_StartupResourceBlob;
    state->startup_resource_tables[0] = Asset_FindTable08ByU32Key(g_StartupResourceBlob, 0x57D40D84);
    state->startup_resource_tables[1] = Asset_FindTable08ByU32Key(g_StartupResourceBlob, 0x57D41D84);

    if (CdRom_ReadSectorsFromLba(lba + g_PeImageSectorTable[6], state->scene_load_scratch,
            g_PeImageSectorTable[7] - g_PeImageSectorTable[6]) < 0) return -5;
    while ((status = CdRom_PollReady()) > 0) {}
    if (status < 0) return -6;
    if (Spu_UploadSampleBlockBlocking(state->scene_load_scratch, 1) < 0) return -9;

    if (CdRom_ReadSectorsFromLba(lba + g_PeImageSectorTable[7], state->scene_load_scratch,
            g_PeImageSectorTable[8] - g_PeImageSectorTable[7]) < 0) return -7;
    while ((status = CdRom_PollReady()) > 0) {}
    if (status < 0) return -8;
    memcpy(state->bank_work_far_end, state->scene_load_scratch, 0x1400);
    return 0;
}

/* The adjacent field background loader shares the scene resource state. */

/* Uploads every TIM listed in a scene blob's directory. */
#define SCENE_BLOB_LOAD_TIMS(source)                                         \
    {                                                                        \
        unsigned int i;  \
                                                                             \
        blob = (source);                                                     \
        directory = SceneAsset_ResolveOffset(blob, blob->directoryOffset);   \
        tim = SceneAsset_ResolveOffset(blob, directory->timEntries & 0x3fffff); \
        for (i = 0; i < directory->timEntries >> 22; i++)                    \
            Draw_LoadTimAsset(&tim[i], blob);                                 \
    }

int Scene_LoadFieldBg(void)
{
    Pe1GameState *state = &g_GameState;
    u32 lba;
    unsigned int slot;
    FieldBgImageRecord *image;
    SceneAssetBlob *blob;
    SceneAssetDirectory *directory;
    TimUploadRecord *tim;
    int status;
    int done;

    if (!(state->flags & 1))
        return 0;

    lba = g_PeImageBaseLba;

    for (;;) {
        while (CdRom_ReadSectorsFromLba(lba + g_PeImageSectorTable[9], state->shared_buffers.bg_texture_blob,
                   g_PeImageSectorTable[10] - g_PeImageSectorTable[9]) == -1)
            ;
        while ((status = CdRom_PollReady()) > 0)
            ;
        if (status == 0)
            break;
    }

    done = 0;
    for (;;) {
        while ((status = CdRom_ReadSectorsFromLba(lba + g_PeImageSectorTable[10], state->bg_tim,
                   g_PeImageSectorTable[11] - g_PeImageSectorTable[10])) == -1)
            ;
        status = 1;
        do {
            if (!done) {
                SCENE_BLOB_LOAD_TIMS(state->shared_buffers.bg_texture_blob);
                for (slot = 2; slot < 4; slot++) {
                    g_FieldBgTextureSlots[slot].tpage = BG_SLOT_TPAGE(g_FieldBgTextureSlots[slot].x, g_FieldBgTextureSlots[slot].y);
                    g_FieldBgTextureSlots[slot].clut = BG_SLOT_CLUT(g_FieldBgTextureSlots[slot].clut_x, g_FieldBgTextureSlots[slot].clut_y);
                }
                image = Asset_FindTable08ByU32Key(blob, 0xABADC06C);
                while (image->length != 0) {
                    LoadImage(&image->rect, image->pixels);
                    image = (FieldBgImageRecord *)(&image->length + (image->length >> 2));
                }
                done = 1;
            }
            if (status == -1)
                break;
            status = CdRom_PollReady();
        } while (status != 0);
        if (status == 0)
            break;
    }

    done = 0;
    for (;;) {
        while ((status = CdRom_ReadSectorsFromLba(lba + g_PeImageSectorTable[11], state->hud_tim,
                   g_PeImageSectorTable[12] - g_PeImageSectorTable[11])) == -1)
            ;
        status = 1;
        do {
            if (!done) {
                Asset_LoadTimImage(state->bg_tim);
                for (slot = 0; slot < 2; slot++) {
                    g_FieldBgTextureSlots[slot].tpage = BG_SLOT_TPAGE(g_FieldBgTextureSlots[slot].x, g_FieldBgTextureSlots[slot].y);
                    g_FieldBgTextureSlots[slot].clut = BG_SLOT_CLUT(g_FieldBgTextureSlots[slot].clut_x, g_FieldBgTextureSlots[slot].clut_y);
                }
                done = 1;
            }
            if (status == -1)
                break;
            status = CdRom_PollReady();
        } while (status != 0);
        if (status == 0)
            break;
    }

    done = 0;
    for (;;) {
        while ((status = CdRom_ReadSectorsFromLba(lba + g_PeImageSectorTable[12], state->bank_asset_source,
                   g_PeImageSectorTable[13] - g_PeImageSectorTable[12])) == -1)
            ;
        status = 1;
        do {
            if (!done) {
                done = 1;
                Asset_LoadTimImage(state->hud_tim);
                Battle_DrawHPBar();
            }
            if (status == -1)
                break;
            status = CdRom_PollReady();
        } while (status != 0);
        if (status == 0)
            break;
    }

    done = 0;
    for (;;) {
        while ((status = CdRom_ReadSectorsFromLba(lba + g_PeImageSectorTable[4], state->object_texture_blob,
                   g_PeImageSectorTable[5] - g_PeImageSectorTable[4])) == -1)
            ;
        status = 1;
        do {
            if (!done) {
                state->scene_object_model =
                    Asset_FindTable08ByU32Key(state->bank_asset_source, 0xC4B5BA04);
                done = 1;
                state->room_geometry_table =
                    Asset_FindTable08ByU32Key(state->bank_asset_source, 0xCAAD0704);
                state->bank_asset_table =
                    Asset_FindTable08ByU32Key(state->bank_asset_source, 0x5EAF6804);
            }
            if (status == -1)
                break;
            status = CdRom_PollReady();
        } while (status != 0);
        if (status == 0)
            break;
    }

    done = 0;
    for (;;) {
        while ((status = CdRom_ReadSectorsFromLba(lba + g_PeImageSectorTable[39], state->scene_process_slots,
                   g_PeImageSectorTable[40] - g_PeImageSectorTable[39])) == -1)
            ;
        status = 1;
        do {
            if (!done) {
                SCENE_BLOB_LOAD_TIMS(state->object_texture_blob);
                done = 1;
            }
            if (status == -1)
                break;
            status = CdRom_PollReady();
        } while (status != 0);
        if (status == 0)
            break;
    }

    {
        unsigned int i;

        blob = (SceneAssetBlob *)state->scene_process_slots;
        directory = SceneAsset_ResolveOffset(blob, blob->directoryOffset);
        tim = SceneAsset_ResolveOffset(blob, directory->timEntries & 0x3fffff);
        i = 0;
        if (directory->timEntries >> 22) {
            do {
                Draw_LoadTimAsset(&tim[i], blob);
                i++;
            } while (i < directory->timEntries >> 22);
        }
    }

    Akao_Cmd_F1();
    DrawSync(0);
    ResetGraph(1);
    VSync(0);
    PutDispEnv(&g_RenderDispEnvArray[g_ActiveDrawSlot]);
    SetDispMask(1);
    state->field_bg_cache_id = -1;
    state->loaded_entity_bank = 0;
    state->current_story_day = -1;
    state->field_bg_cache_bank = -1;
    state->pending_sample_bank = -1;
    state->pending_stream_banks[1] = 0;
    state->pending_stream_banks[0] = 0;
    if (!(state->flags & 0x40)) {
        state->scene_audio.tracks.banks[0] = -1;
        state->scene_audio.tracks.keys[0][1] = -1;
        state->scene_audio.tracks.keys[0][0] = -1;
    }
    if (!(state->flags & 0x80)) {
        state->scene_audio.tracks.banks[1] = -1;
        state->scene_audio.tracks.keys[1][1] = -1;
        state->scene_audio.tracks.keys[1][0] = -1;
    }
    state->flags &= ~1;
    return 0;
}

void Akao_LoadVoiceBankAlt(void)
{
    Pe1GameState *state = &g_GameState;
    int i, j;
    u8 *base;
    AkaoVoiceBankEntry *pair;

    for (i = 9; i >= 0; --i)
        state->bank_slots[i] = 0;
    for (i = 0; i < 10; ++i)
        for (j = 47; j >= 0; --j)
            state->bank_rows[i][j] = 0;
    for (i = 0; i < 1; ++i)
        state->bank_reset_940[i] = 0;
    for (i = 0; i < 1; ++i)
        state->bank_reset_944[i] = 0;
    for (i = 0; i < 1; ++i)
        state->bank_reset_948[i] = 0;
    for (i = 0; i < 1; ++i)
        state->bank_reset_94c[i] = 0;
    for (i = 1; i >= 0; --i)
        state->bank_reset_950[i] = 0;
    for (i = 0; i < 1; ++i)
        state->bank_reset_958[i] = 0;

    base = state->voice_bank_base;
    state->bank_work_base = base;
    state->bank_work_end = base + 0x1400;
    state->bank_asset_table =
        Asset_FindTable08ByU32Key(state->bank_asset_source, 0x5EAF6804);
    state->scene_audio.bytes[8] = 0x27;
    state->scene_audio.bytes[9] = 13;
    state->bank_state_11 = 0;
    state->bank_state_12 = 0;
    state->flags &= ~0x400000;

    for (i = 0, pair = g_AkaoVoiceBankEntries; i < 4; ++i, ++pair) {
        pair->ids[1] = 0;
        g_AkaoVoiceBankEntries[i].ids[0] = 0;
    }
    state->bank_value_f6 = 0x30;
    state->bank_value_f7 = 0x7F;
    state->bank_value_f8 = 0x100;
    state->bank_value_fa = 0x800;
}

/* Shared native state; external variables have one storage definition. */

static const void *SceneRoom_ResolveScriptDescriptor(int room_number, unsigned int slot, u32 address)
{
    const void *native = NULL;
    int actor_library = 0;
    int orbit_library = 0;
    int homing_flash_library = 0;
    int twelve_library = 0;
    int twelve_orbit_library = 0;
    u32 shared_actor = 0, walker_class = 0, walker_task = 0;
    u32 chrysler_particles = 0, chrysler_falling = 0, chrysler_line = 0;

    twelve_orbit_library = room_number == 186 || room_number == 385 ||
        room_number == 388 || room_number == 389;
    /* Exact complete USA module, including class rows and authored data. */
    twelve_library = room_number == 167 || room_number == 168 || room_number == 169 ||
        room_number == 173 || room_number == 178 || room_number == 295 ||
        room_number == 299 || room_number == 309 || room_number == 310;
    homing_flash_library = room_number == 162 || room_number == 163 ||
        room_number == 398 || room_number == 400 || room_number == 403 ||
        room_number == 434 || room_number == 435;
    /* Eight identical USA spark modules; two add the original SoundBurst unit. */
    const int spark_combined = room_number == 231 || room_number == 234;
    const int spark_library = spark_combined || room_number == 203 || room_number == 221 ||
        room_number == 223 || room_number == 224 || room_number == 229 ||
        room_number == 233 || room_number == 242 || room_number == 246;

    /* These USA rooms link the same complete actor library and class table. */
    switch (room_number) {
    case 22: case 32: case 59: case 61: case 62: case 66: case 67:
    case 78: case 81: case 197: case 198: case 199: case 201: case 206:
    case 207: case 210: case 214: case 217: case 218: case 219: case 220:
    case 226: case 227: case 232: case 386: case 405: case 407: case 408:
    case 409: case 436:
        actor_library = 1;
        break;
    }

    /* These rooms share the entire 17060-byte actor/OrbitFlare module. */
    switch (room_number) {
    case 14: case 16: case 28: case 100: case 110: case 121:
    case 298: case 308: case 311:
        orbit_library = 1;
        break;
    }

    switch (room_number) {
    case 145: case 332:
        shared_actor = 0x801931f4; walker_class = 0x80193264; walker_task = 0x801932c0;
        break;
    case 152:
        shared_actor = 0x80193edc; walker_class = 0x80193f4c; walker_task = 0x80194004;
        break;
    case 153: case 154:
        shared_actor = 0x801955bc; walker_class = 0x8019562c; walker_task = 0x80195688;
        break;
    case 391: case 394: case 395:
        shared_actor = 0x801943fc; chrysler_particles = 0x80194360; chrysler_line = 0x8019448c;
        break;
    case 392:
        shared_actor = 0x80194578; chrysler_falling = 0x801944d8; chrysler_line = 0x80194608;
        break;
    case 393: case 396:
        shared_actor = 0x801947c4; chrysler_particles = 0x8019466c; chrysler_falling = 0x80194724;
        break;
    case 399: case 402:
        shared_actor = 0x80194288; chrysler_particles = 0x801941ec;
        walker_class = 0x801942f8; walker_task = 0x80194354;
        break;
    case 401:
        shared_actor = 0x80195498; chrysler_particles = 0x80195340; chrysler_falling = 0x801953f8;
        walker_class = 0x80195508; walker_task = 0x80195564;
        break;
    }

    if (room_number == 418) {
        if (slot == 75 && address == 0x80198b1cu) native = &g_RoomActorClassTable[2];
        if (slot == 70 && address == 0x80198ae4u) native = &g_RoomActorClassTable[0];
        const u32 addresses[4] = {0x801986acu, 0x80198760u, 0x80198914u, 0x80198a48u};
        if (slot >= 46 && slot <= 49 && address == addresses[slot - 46])
            native = &g_SceneE08Classes[slot - 46];
    }
    if (shared_actor) {
        if (chrysler_particles && slot == 10 && address == chrysler_particles) native = &g_RoomThreeParticleHandlers;
        if (chrysler_falling && slot == 40 && address == chrysler_falling) native = &g_RoomFallingBurstHandlers;
        if (chrysler_line && slot == 136 && address == chrysler_line) native = &g_RoomLineBurstProgram;
        if (slot == 75 && address == shared_actor) native = &g_RoomActorClassTable[2];
        if (slot == 79 && address == walker_class) native = &g_RoomFloorWalkerHandlers;
        if (slot == 187 && address == walker_task) native = &g_RoomTimedSpawnProgram;
        if (room_number == 152 && slot == 122 && address == 0x80193f8cu) native = &g_RoomDroppedFlareProgram;
        if (room_number == 153 || room_number == 154) {
            if (slot == 37 && address == 0x80195520u) native = &g_RoomGroundEruptionHandlers;
            if (slot == 166 && address == 0x80195700u) native = &g_RoomHomingProgram;
        }
    }
    if (room_number == 156 || room_number == 291 || room_number == 380) {
        if (slot == 129 && address == 0x80192b08u) native = &g_RoomBurstOrbProgram;
        if (slot == 130 && address == 0x80192b64u) native = &g_RoomBeamSparkProgram;
        if (room_number == 380 && slot == 126 && address == 0x80192bc0u) native = &g_RoomJitterSparkProgram;
    }
    /* Seven USA rooms share the original mirror module. */
    if (slot == 69 && address == 0x8018fb44u) {
        switch (room_number) {
        case 17: case 18: case 21: case 45: case 102: case 151: case 319:
            native = &g_RoomFxMirrorHandlers;
            break;
        }
    }
    if ((room_number == 65 || room_number == 85) && slot == 10 && address == 0x80190024u)
        native = &g_RoomThreeParticleHandlers;
    if (room_number == 87) {
        if (slot == 10 && address == 0x80193518u) native = &g_RoomThreeParticleHandlers;
        if (slot == 75 && address == 0x801935b4u) native = &g_RoomActorClassTable[2];
    }
    if (room_number == 272 && slot == 15 && address == 0x8018f1a8u)
        native = &g_RoomM272Handlers;
    if ((room_number == 63 || room_number == 83) && slot == 74 && address == 0x8018fddcu)
        native = &g_RoomWindowClassHandlers;
    if (room_number == 358 || room_number == 384 || room_number == 387) {
        if (slot == 74 && address == 0x80193378u) native = &g_RoomWindowClassHandlers;
        if (slot == 75 && address == 0x80193308u) native = &g_RoomActorClassTable[2];
    }
    if (room_number == 75 || room_number == 80 || room_number == 82) {
        if (slot == 75 && address == 0x80194048u) native = &g_RoomActorClassTable[2];
        if (slot == 12 && address == 0x80193f74u) native = &g_RoomMotionHandlers;
    }
    if (actor_library && slot == 70 && address == 0x801924e8u) native = &g_RoomActorClassTable[0];
    if (actor_library && slot == 75 && address == 0x80192520u) native = &g_RoomActorClassTable[2];
    if (orbit_library && slot == 70 && address == 0x8019316cu) native = &g_RoomActorClassTable[0];
    if (orbit_library && slot == 75 && address == 0x801931a4u) native = &g_RoomActorClassTable[2];
    if (orbit_library && slot == 73 && address == 0x801931dcu) native = &g_RoomActorClassTable[4];
    if (actor_library && slot == 73 && address == 0x80192558u) native = &g_RoomActorClassTable[4];
    if (room_number == 188 || room_number == 390) {
        if (slot == 73 && address == 0x80195e08u) native = &g_RoomActorClassTable[4];
        if (slot == 75 && address == 0x80195dd0u) native = &g_RoomActorClassTable[2];
        if (slot == 70 && address == 0x80195d98u) native = &g_RoomActorClassTable[0];
        if (slot == 124 && address == 0x80195e84u) native = &g_RoomSparkBarrageProgram124;
        if (slot == 125 && address == 0x80195f28u) native = &g_RoomSparkBarrageProgram125;
        if (slot == 127 && address == 0x80195f98u) native = &g_RoomSparkBarrageProgram127;
    }
    if (twelve_orbit_library) {
        if (slot == 36 && address == 0x80194214u) native = &g_RoomOrbitSetHandlers;
        if (slot == 11 && address == 0x80194164u) native = &g_RoomTwelveElementHandlers;
        if (slot == 75 && address == 0x801942dcu) native = &g_RoomActorClassTable[2];
        if (slot == 70 && address == 0x801942a4u) native = &g_RoomActorClassTable[0];
    }
    if (room_number == 187 && slot == 36 && address == 0x8018fff0u)
        native = &g_RoomOrbitSetHandlers;
    if (room_number == 174 || room_number == 383) {
        if (slot == 75 && address == 0x80196f58u) native = &g_RoomActorClassTable[2];
        if (slot == 8 && address == 0x80196cc4u) native = &g_RoomGlowOrbHandlers;
        if (slot == 9 && address == 0x80196e30u) native = &g_RoomDropFieldHandlers;
    }
    if (room_number == 34 && slot == 8 && address == 0x8018ff7cu)
        native = &g_RoomGlowOrbHandlers;
    if (slot == 14 && address == 0x80192bccu) {
        switch (room_number) {
        case 49: case 50: case 90: case 344: case 374:
            native = &g_RoomEffectSequenceHandlers;
            break;
        }
    }
    if (room_number == 348 && slot == 9 && address == 0x8019290cu)
        native = &g_RoomDropFieldHandlers;
    if (room_number == 23 && slot == 118 && address == 0x80190720u)
        native = &g_RoomM023Effects;
    if (room_number == 5 && slot == 117 && address == 0x80190b44u)
        native = &g_RoomM005Effects;
    if ((room_number == 12 || room_number == 13 || room_number == 27 || room_number == 30) &&
        slot == 115 && address == 0x8018fcb4u)
        native = &g_RoomOrbitFlareEffects;
    if (orbit_library && slot == 115 && address == 0x80193250u)
        native = &g_RoomOrbitFlareEffects;
    if (room_number == 412) {
        if (slot == 37 && address == 0x801954d0u) native = &g_RoomGroundEruptionHandlers;
        if (slot == 75 && address == 0x8019556cu) native = &g_RoomActorClassTable[2];
        if (slot == 115 && address == 0x80195618u) native = &g_RoomOrbitFlareEffects;
        if (slot == 166 && address == 0x80195690u) native = &g_RoomHomingProgram;
    }
    if (room_number == 419 || room_number == 420) {
        if (slot == 75 && address == 0x80194d30u) native = &g_RoomActorClassTable[2];
        if (slot == 12 && address == 0x80194c5cu) native = &g_RoomMotionHandlers;
        if (slot == 122 && address == 0x80194dc4u) native = &g_RoomDroppedFlareProgram;
    }
    if (room_number >= 421 && room_number <= 423) {
        if (slot == 35 && address == 0x80191a54u) native = &g_RoomPairedGlowHandlers;
        if (slot == 115 && address == 0x80191b2cu) native = &g_RoomOrbitFlareEffects;
        if (slot == 122 && address == 0x80191b88u) native = &g_RoomDroppedFlareProgram;
    }
    if (room_number == 424) {
        if (slot == 35 && address == 0x80190d6cu) native = &g_RoomPairedGlowHandlers;
        if (slot == 115 && address == 0x80190e44u) native = &g_RoomOrbitFlareEffects;
    }
    if (room_number == 413 || room_number == 416 || room_number == 417 || room_number == 437) {
        if (slot == 70 && address == 0x801930f0u) native = &g_RoomActorClassTable[0];
        if (slot == 75 && address == 0x80193128u) native = &g_RoomActorClassTable[2];
        if (slot == 135 && address == 0x801931b8u) native = &g_RoomJointSprayProgram;
    }
    if (room_number == 414 || room_number == 415) {
        if (slot == 37 && address == 0x80191fdcu) native = &g_RoomGroundEruptionHandlers;
        if (slot == 115 && address == 0x8019207cu) native = &g_RoomOrbitFlareEffects;
        if (slot == 166 && address == 0x801920f4u) native = &g_RoomHomingProgram;
    }
    if (room_number == 141 || room_number == 328) {
        if (slot == 37 && address == 0x80192040u) native = &g_RoomGroundEruptionHandlers;
        if (slot == 122 && address == 0x801920c8u) native = &g_RoomDroppedFlareProgram;
        if (slot == 166 && address == 0x80192140u) native = &g_RoomHomingProgram;
    }
    if (room_number == 146) {
        if (slot == 37 && address == 0x801924acu) native = &g_RoomGroundEruptionHandlers;
        if (slot == 40 && address == 0x80192564u) native = &g_RoomFallingBurstHandlers;
        if (slot == 166 && address == 0x8019260cu) native = &g_RoomHomingProgram;
    }
    if (room_number == 137 || room_number == 140 || room_number == 147 || room_number == 324 || room_number == 327) {
        if (slot == 122 && address == 0x80190f10u) native = &g_RoomDroppedFlareProgram;
        if (slot == 40 && address == 0x80190e84u) native = &g_RoomFallingBurstHandlers;
    }
    if ((room_number == 126 || room_number == 129 || room_number == 139 || room_number == 149 || room_number == 326) &&
        slot == 122 && address == 0x8018fd00u)
        native = &g_RoomDroppedFlareProgram;
    if ((room_number == 107 || room_number == 114) && slot == 35 && address == 0x801935dcu)
        native = &g_RoomPairedGlowHandlers;
    if ((room_number == 111 || room_number == 118) && slot == 35 && address == 0x801900e8u)
        native = &g_RoomPairedGlowHandlers;
    if (room_number == 122 && slot == 35 && address == 0x80190f30u)
        native = &g_RoomPairedGlowHandlers;
    if (room_number == 122 && slot == 136 && address == 0x80190fecu)
        native = &g_RoomLineBurstProgram;
    if ((room_number == 104 || room_number == 115 || room_number == 159) && slot == 136 && address == 0x8018fe5cu)
        native = &g_RoomLineBurstProgram;
    if ((room_number == 116 || room_number == 120) && slot == 75 && address == 0x80193368u)
        native = &g_RoomActorClassTable[2];
    if ((room_number == 116 || room_number == 120) && slot == 136 && address == 0x801933f8u)
        native = &g_RoomLineBurstProgram;
    if (room_number == 113 && slot == 115 && address == 0x80190afcu)
        native = &g_RoomOrbitFlareEffects;
    if (room_number == 113 && slot == 136 && address == 0x80190b54u)
        native = &g_RoomLineBurstProgram;
    if (spark_library) {
        if (slot == 75 && address == (spark_combined ? 0x80194758u : 0x80194008u)) native = &g_RoomActorClassTable[2];
        if (!spark_combined && slot == 70 && address == 0x80193fd0u) native = &g_RoomActorClassTable[0];
        if (slot == 142 && address == (spark_combined ? 0x80194864u : 0x801940b8u)) native = &g_RoomSparkRingProgram;
        if (slot == 180 && address == (spark_combined ? 0x801948bcu : 0x80194110u)) native = &g_RoomBurstSparkProgram;
        if (spark_combined && slot == 116 && address == 0x801947ecu) native = &g_RoomSoundBurstProgram;
    }
    if (room_number == 245 || room_number == 397) {
        if (slot == 75 && address == 0x8019563cu) native = &g_RoomActorClassTable[2];
        if (slot == 70 && address == 0x80195604u) native = &g_RoomActorClassTable[0];
        if (slot == 140 && address == 0x80195720u) native = &g_RoomSeekingTrailProgram;
        if (slot == 141 && address == 0x801957e4u) native = &g_RoomScatterTrailProgram;
    }
    if (room_number == 430 && slot == 188 && address == 0x80190824u)
        native = &g_SceneE20HoverOrbProgram;
    if (room_number == 429) {
        if (slot == 75 && address == 0x8019b28cu) native = &g_RoomActorClassTable[2];
        if (slot == 80 && address == 0x8019b324u) native = &g_SceneE19GrabClass;
        if (slot == 179 && address == 0x8019b630u) native = &g_SceneE19JitterSparkProgram;
        if (slot == 177 && address == 0x8019b578u) native = &g_SceneE19GlintRibbonProgram;
        if (slot == 178 && address == 0x8019b5e4u) native = &g_SceneE19TwinGlowProgram;
        if (slot == 146 && address == 0x8019b42cu) native = &g_SceneE19HomingProgram;
        if (slot == 145 && address == 0x8019b380u) native = &g_SceneE19BlastProgram;
        if (slot == 148 && address == 0x8019b524u) native = &g_SceneE19OrbitProgram;
        if (slot == 147 && address == 0x8019b4acu) native = &g_SceneE19EmberProgram;
    }
    if (room_number == 428) {
        if (slot == 75 && address == 0x8019403cu) native = &g_RoomActorClassTable[2];
        if (slot == 78 && address == 0x80194090u) native = &g_RoomActorClassTable[0];
        if (slot == 152 && address == 0x801940d0u) native = &g_SceneE18ShellProgram;
        if (slot == 153 && address == 0x80194124u) native = &g_SceneE18PulseProgram;
        if (slot == 154 && address == 0x80194188u) native = &g_SceneE18WaveProgram;
    }
    if (room_number == 432) {
        if (slot == 75 && address == 0x801990f8u) native = &g_RoomActorClassTable[2];
        if (slot == 149 && address == 0x801991f0u) native = &g_SceneE22LinkBeamProgramA;
        if (slot == 165 && address == 0x801992b0u) native = &g_SceneE22RingBurstProgram;
        if (slot == 163 && address == 0x80199254u) native = &g_SceneE22LinkBeamProgramB;
        if (slot == 150 && address == 0x80199330u) native = &g_SceneE22EmberDriftProgram;
        if (slot == 151 && address == 0x801993b8u) native = &g_SceneE22EmberColumnProgram;
        if (slot == 162 && address == 0x801994b4u) native = &g_SceneE22SwirlBoltProgram;
    }
    if (room_number == 273) {
        if (slot == 82 && address == 0x8019a900u) native = &g_RoomM273OscillationClass;
        if (slot == 81 && address == 0x8019a8e4u) native = &g_RoomM273GrabClass;
        if (slot == 75 && address == 0x8019a848u) native = &g_RoomActorClassTable[2];
        if (slot == 73 && address == 0x8019a880u) native = &g_RoomActorClassTable[4];
        if (slot == 72 && address == 0x8019a864u) native = &g_RoomActorClassTable[3];
        if (slot == 167 && address == 0x8019ab94u) native = &g_RoomM273GrabPlacementEffects;
        if (slot == 183 && address == 0x8019ac40u) native = &g_RoomM273GrabEffects;
        if (slot == 184 && address == 0x8019ace4u) native = &g_RoomM273HandTrailEffects;
        if (slot == 185 && address == 0x8019ad98u) native = &g_RoomM273SweepEffects;
        if (slot == 186 && address == 0x8019ae24u) native = &g_RoomM273SwayEffects;
    }
    if (room_number == 205 || room_number == 243 || room_number == 406 || room_number == 410) {
        if (slot == 70 && address == 0x80192c38u) native = &g_RoomActorClassTable[0];
        if (slot == 73 && address == 0x80192ca8u) native = &g_RoomActorClassTable[4];
        if (slot == 75 && address == 0x80192c70u) native = &g_RoomActorClassTable[2];
        if (slot == 116 && address == 0x80192d04u) native = &g_RoomSoundBurstProgram;
    }
    if (room_number == 269 || room_number == 411) {
        if (slot == 77 && address == 0x801921c8u) native = &g_RoomGrabHandlers;
        if (slot == 137 && address == 0x80192228u) native = &g_RoomGroundSweepProgram;
        if (slot == 138 && address == 0x801922a8u) native = &g_RoomOrbitTrailProgram;
        if (slot == 139 && address == 0x801922f8u) native = &g_RoomRisingSprayProgram;
    }
    if (room_number == 263 || room_number == 265 || room_number == 333) {
        if (slot == 70 && address == 0x80193fb4u) native = &g_RoomActorClassTable[0];
        if (slot == 75 && address == 0x80193fecu) native = &g_RoomActorClassTable[2];
        if (slot == 115 && address == 0x80194098u) native = &g_RoomOrbitFlareEffects;
        if (slot == 136 && address == 0x801940f0u) native = &g_RoomLineBurstProgram;
    }
    if (room_number == 107 && slot == 75 && address == 0x801936b0u)
        native = &g_RoomActorClassTable[2];
    if (room_number == 114 && slot == 72 && address == 0x801936ccu)
        native = &g_RoomActorClassTable[3];
    if (room_number == 89) {
        if (slot == 75 && address == 0x8019405cu) native = &g_RoomActorClassTable[2];
        if (slot == 119 && address == 0x80194128u) native = &g_RoomM089Program;
    }
    if (room_number == 123) {
        if (slot == 73 && address == 0x80195454u) native = &g_RoomActorClassTable[4];
        if (slot == 128 && address == 0x8019553cu) native = &g_RoomM123JointEffects;
        if (slot == 143 && address == 0x801955d4u) native = &g_RoomM123PulseRingEffects;
        if (slot == 144 && address == 0x8019564cu) native = &g_RoomM123WaveBurstEffects;
    }
    if (room_number == 256) {
        if (slot == 75 && address == 0x80195d38u) native = &g_RoomActorClassTable[2];
        if (slot == 76 && address == 0x80195da8u) native = &g_RoomM256SwingClass;
        if (slot == 131 && address == 0x80195de8u) native = &g_RoomM256ScatterEffects;
        if (slot == 132 && address == 0x80195eb4u) native = &g_RoomM256TrailEffects;
    }
    if (room_number == 318) {
        if (slot == 75 && address == 0x801993d4u) native = &g_RoomActorClassTable[2];
        if (slot == 70 && address == 0x8019939cu) native = &g_RoomActorClassTable[0];
        if (slot == 168 && address == 0x801994ecu) native = &g_RoomM318FanSweepEffects;
        if (slot == 169 && address == 0x80199548u) native = &g_RoomM318ShakeSweepEffects;
        if (slot == 170 && address == 0x80199624u) native = &g_RoomM318ShakeBurstEffects;
        if (slot == 171 && address == 0x8019969cu) native = &g_RoomM318EmberBurstEffects;
        if (slot == 172 && address == 0x8019970cu) native = &g_RoomM318RisingColumnEffects;
        if (slot == 173 && address == 0x801998b8u) native = &g_RoomM318EmberFountainEffects;
        if (slot == 174 && address == 0x801997c0u) native = &g_RoomM318FlashEffects;
        if (slot == 175 && address == 0x8019980cu) native = &g_RoomM318TwinRibbonEffects;
    }
    if (room_number == 404) {
        if (slot == 75 && address == 0x80193e6cu) native = &g_RoomActorClassTable[2];
        if (slot == 133 && address == 0x80193f1cu) native = &g_RoomM404SinkingDropEffects;
        if (slot == 134 && address == 0x80193f88u) native = &g_RoomM404DebrisBurstEffects;
    }
    if (room_number == 350) {
        const struct {
            unsigned int slot;
            u32 address;
            const void *program;
        } bindings[] = {
            {75, 0x8019a350u, &g_RoomActorClassTable[2]},
            {70, 0x8019a318u, &g_RoomActorClassTable[0]},
            {73, 0x8019a388u, &g_RoomActorClassTable[4]},
            {155, 0x8019a468u, &g_RoomM350HandEffects},
            {156, 0x8019a508u, &g_RoomM350OrbEffects},
            {158, 0x8019a5a0u, &g_RoomM350AreaEffects},
            {159, 0x8019a638u, &g_RoomM350DropEffects},
            {160, 0x8019a6a0u, &g_RoomM350RushEffects},
            {161, 0x8019a73cu, &g_RoomM350BodyEffects}
        };
        for (unsigned j = 0; j < sizeof(bindings) / sizeof(*bindings); ++j) {
            if (slot == bindings[j].slot && address == bindings[j].address)
                native = bindings[j].program;
        }
    }
    if (room_number == 349 && slot == 181 && address == 0x8019015cu)
        native = &g_RoomM349FlareEffects;
    if (room_number == 86) {
        if (slot == 120 && address == 0x80190aecu) native = &g_RoomM086SeekerProgram;
        if (slot == 121 && address == 0x80190b3cu) native = &g_RoomM086GlintProgram;
    }
    if (homing_flash_library) {
        if (slot == 75 && address == 0x80193174u) native = &g_RoomActorClassTable[2];
        if (slot == 70 && address == 0x8019313cu) native = &g_RoomActorClassTable[0];
        if (slot == 123 && address == 0x80193254u) native = &g_RoomHomingFlashProgram;
    }
    if (twelve_library) {
        if (slot == 75 && address == 0x80193250u) native = &g_RoomActorClassTable[2];
        if (slot == 70 && address == 0x80193218u) native = &g_RoomActorClassTable[0];
        if (slot == 73 && address == 0x80193288u) native = &g_RoomActorClassTable[4];
        if (slot == 11 && address == 0x801931b0u) native = &g_RoomTwelveElementHandlers;
    }
    return native;
}

static void SceneRoom_ResetRoomOverlayState(int room_number)
{
    if (room_number == 272) {
        g_RoomM272Phase = 0;
        memset(RoomM272_Rec, 0, sizeof(RoomM272_Rec));
    }
    if (room_number == 75 || room_number == 80 || room_number == 82 || room_number == 419 || room_number == 420) {
        memset(&g_RoomMotionGlyph, 0, sizeof(g_RoomMotionGlyph));
        memset(g_RoomMotionGlyphs, 0, sizeof(g_RoomMotionGlyphs));
        memset(g_RoomMotionTrailA__motion, 0, sizeof(g_RoomMotionTrailA__motion));
        memset(g_RoomMotionTrailB, 0, sizeof(g_RoomMotionTrailB));
    }
    if (room_number == 188 || room_number == 390) {
        g_RoomScriptArgA = g_RoomScriptArgB = 0;
        memset(&g_RoomCometSparkTrail, 0, sizeof(g_RoomCometSparkTrail));
        struct {
            u8 *track;
            size_t bytes;
        } tracks[] = {
            {g_RoomBouncingSparkColors, 32},
            {g_RoomWaveSparkColors, 32},
            {g_RoomCometSparkColors, 40},
            {g_RoomLiftedSparkColors, 32}
        };
        for (unsigned track = 0; track < 4; ++track) {
            memset(tracks[track].track, 0, 8);
            for (size_t offset = 12; offset + 4 <= tracks[track].bytes; offset += 8)
                memset(tracks[track].track + offset, 0, 4);
        }
    }
    if (room_number == 418) {
        SceneE08_ResetModule();
    }
    if (room_number == 318) {
        memset(g_RoomM318ActorParams, 0, sizeof(g_RoomM318ActorParams));
        memset(g_RoomM318EmberBurstParams, 0, sizeof(g_RoomM318EmberBurstParams));
        memset(g_RoomM318RisingColumnParams, 0, sizeof(g_RoomM318RisingColumnParams));
        g_RoomScriptArg = g_RoomM318ScriptArg2 = g_RoomM318StarFlarePhase = 0;
        g_RoomM318FanSweepSparkPool = g_RoomM318EmberSpiralModel = NULL;
        g_RoomM318FanSweepAnchor = g_RoomM318ShakeSweepAnchor = g_RoomM318RisingColumnAnchor =
            g_RoomM318CrossFlashAnchor = g_RoomM318EmberFountainAnchor = (GteShortVector){0};
    }
}

static void SceneRoom_InstallAndResetScriptDescriptor(int room_number, unsigned int slot, const void *native)
{
    if (slot < 85) {
        g_PmCmdHandlerTable[slot] = (void *)native;
    } else {
        g_FieldFxTaskPrograms[slot - 85] = (void *)native;
    }

    if (room_number == 429 && slot == 75) {
        SceneE19_ResetModule();
    } else if (room_number == 428 && slot == 75) {
        SceneE18_ResetModule();
    } else if (room_number == 432 && slot == 75) {
        SceneE22_ResetModule();
    } else if (room_number == 273 && slot == 82) {
        RoomM273_ResetModule();
    } else if (room_number == 256 && slot == 75) {
        g_RoomM256ScatterConfig = g_RoomM256TrailConfig = g_RoomM256TrailTurnScale = 0;
        g_RoomM256TrailModel = g_RoomM256PulseModel = NULL;
        memset(g_RoomM256TrailHistory, 0, sizeof(g_RoomM256TrailHistory));
        g_RoomM256PulseBrightness = 0;
        g_RoomM256PulseAngles = (GteShortVector){0};
        g_RoomM256PulseCenter = (RoomModelPulseAnchor){0};
    } else if (room_number == 404 && slot == 75) {
        g_RoomScriptArg = 0;
    } else if (room_number == 350 && slot == 75) {
        /* A cold room-bank load restored the original mutable data. */
        g_RoomM350HandActor = NULL;
        memset(g_RoomM350HandPoints, 0, sizeof(g_RoomM350HandPoints));
        g_RoomM350HandSweepRepeats = 0;
        g_RoomM350HandSweepDone = g_RoomM350HandSweepHit = 0;
        memset(&g_RoomM350OrbData, 0, sizeof(g_RoomM350OrbData));
        memset(&g_RoomM350AreaData, 0, sizeof(g_RoomM350AreaData));
        memset(&g_RoomEffectTrailPositions, 0, sizeof(g_RoomEffectTrailPositions));
        memset(&g_RoomM350RushData, 0, sizeof(g_RoomM350RushData));
        memcpy(
            g_RoomM350HandSweepTrapCorners,
            (GteShortVector[]){{-128, 0, 0, 0}, {128, 0, 0, 0}, {128, 0, -4096, 0}, {-128, 0, -4096, 0}},
            4 * sizeof(GteShortVector)
        );
    } else if (room_number == 123 && slot == 73) {
        g_RoomScriptRecordWords[0] = 0;
        g_RoomScriptArg = 0;
        g_RoomM123SelectedColorTrack = NULL;
        g_RoomM123BeaconModel0 = g_RoomM123BeaconModel1 = NULL;
        g_RoomM123GlowBurstAngles = (RoomM123JointAngles){0};
        g_RoomM123GlowBurstCenter = (GteShortVector){0};
        g_RoomM123SwirlBrightness = g_RoomM123SwirlSpeed = 0;
        memset(g_RoomM123SwirlAngles, 0, 8);
        memset(g_RoomM123SwirlOrigin, 0, 6);
        struct {
            void *track;
            size_t bytes;
        } tracks[] = {
            {g_RoomM123GlowColorTrack, 40},
            {g_RoomM123GlowBurstColorTrack, 40},
            {g_RoomM123BeaconColorTrack, 48},
            {g_RoomM123PulseColorTrack0, 32},
            {g_RoomM123PulseColorTrack1, 32},
            {g_RoomM123WaveColorTrack, 40}
        };
        for (unsigned track = 0; track < 6; ++track) {
            memset(tracks[track].track, 0, 8);
            for (size_t offset = 12; offset + 4 <= tracks[track].bytes; offset += 8)
                memset((u8 *)tracks[track].track + offset, 0, 4);
        }
    }

    if (native == &g_RoomTwelveElementHandlers) {
        memset(&RoomLib_TwelveEffectPrimaryPacket, 0, sizeof(RoomLib_TwelveEffectPrimaryPacket));
        memset(&RoomLib_TwelveEffectSecondaryPacket, 0, sizeof(RoomLib_TwelveEffectSecondaryPacket));
    } else if (native == &g_RoomOrbitSetHandlers) {
        memset(&g_RoomOrbitSpritePacket, 0, sizeof(g_RoomOrbitSpritePacket));
        memset(&g_RoomOrbitParticlePacket, 0, sizeof(g_RoomOrbitParticlePacket));
        memset(&g_RoomOrbitBurstPacket, 0, sizeof(g_RoomOrbitBurstPacket));
        memset(&g_RoomOrbitShadowPacket, 0, sizeof(g_RoomOrbitShadowPacket));
    } else if (native == &g_RoomHomingFlashProgram ||
               native == &g_RoomOrbitFlareEffects ||
               native == &g_RoomSoundBurstProgram) {
        memset(g_RoomScriptArgs, 0, sizeof(g_RoomScriptArgs));
    } else if (native == &g_RoomM005Effects) {
        memset(g_RoomM005ScriptAnchorView - 4, 0, 20);
        g_RoomM005BeaconModel0 = g_RoomM005BeaconModel1 = NULL;
        g_RoomM005BeaconOffset = (GteShortVector){0};
        g_RoomM005BeaconCenter = (GteShortVector){0};
    } else if (native == &g_RoomM023Effects) {
        memset(g_RoomM005ScriptAnchorView - 4, 0, 20);
        g_RoomM023BeaconModel = NULL;
    } else if (native == &g_RoomJointSprayProgram) {
        memset(g_RoomJointSprayArgs, 0, sizeof(g_RoomJointSprayArgs));
    } else if (native == &g_RoomM349FlareEffects) {
        memset(&g_RoomM349FlareParams, 0, sizeof(g_RoomM349FlareParams));
    } else if (native == &g_RoomBurstOrbProgram ||
               native == &g_RoomBeamSparkProgram ||
               native == &g_RoomJitterSparkProgram) {
        g_RoomScriptArg = 0;
        g_RoomBurstOrbHeading = 0;
        g_RoomBeamSparkAsset = NULL;
        memset(RoomLib_SlotTable, 0, 8 * sizeof(*RoomLib_SlotTable));
        memset(&g_RoomSparkCapture, 0, sizeof(g_RoomSparkCapture));
    } else if (native == &g_RoomTimedSpawnProgram) {
        memset(RoomLib_TimedRenderTable, 0, 8);
        for (unsigned offset = 12; offset < 32; offset += 8)
            memset(RoomLib_TimedRenderTable + offset, 0, 4);
    } else if (native == &g_RoomDroppedFlareProgram) {
        memset(g_RoomDroppedFlareArgs, 0, sizeof(g_RoomDroppedFlareArgs));
    } else if (native == &g_RoomPairedGlowHandlers) {
        memset(&g_RoomPairedGlowSprite, 0, sizeof(g_RoomPairedGlowSprite));
        g_RoomPairedGlowPacket__paired = NULL;
    } else if (native == &g_RoomLineBurstProgram) {
        memset(RoomLib_SlotTable, 0, 8 * sizeof(RoomSlotRec));
    } else if (native == &g_RoomHomingProgram) {
        memset(&g_RoomHomingLaunch, 0, sizeof(g_RoomHomingLaunch));
    } else if (native == &g_RoomGroundEruptionHandlers) {
        memset(&g_RoomEruptionGlowPacket, 0, sizeof(g_RoomEruptionGlowPacket));
        memset(&g_RoomEruptionDebrisPacket, 0, sizeof(g_RoomEruptionDebrisPacket));
        memset(&g_RoomEruptionShadowPacket, 0, sizeof(g_RoomEruptionShadowPacket));
        memset(&g_RoomEruptionRingPacket, 0, sizeof(g_RoomEruptionRingPacket));
    } else if (native == &g_RoomFallingBurstHandlers) {
        memset(&g_RoomFallingBurstShadow, 0, sizeof(g_RoomFallingBurstShadow));
        memset(&g_RoomFallingBurstShimmer, 0, sizeof(g_RoomFallingBurstShimmer));
        memset(&g_RoomFallingBurstSprite, 0, sizeof(g_RoomFallingBurstSprite));
        memset(&g_RoomFallingBurstPulse, 0, sizeof(g_RoomFallingBurstPulse));
        memset(&g_RoomFallingBurstParticle, 0, sizeof(g_RoomFallingBurstParticle));
    } else if (native == &g_RoomSparkRingProgram || native == &g_RoomBurstSparkProgram) {
        g_RoomSparkSlot = 0;
    } else if (native == &g_RoomSeekingTrailProgram) {
        memset(g_RoomSeekingTrailHistory, 0, sizeof(g_RoomSeekingTrailHistory));
        memset(g_RoomScatterTrailHistory, 0, sizeof(g_RoomScatterTrailHistory));
        memset(g_RoomModelBurstLayer, 0, sizeof(g_RoomModelBurstLayer));
        memset(g_RoomScatterBurstLayer, 0, sizeof(g_RoomScatterBurstLayer));
        memset(&g_RoomModelBurstAnchor, 0, sizeof(g_RoomModelBurstAnchor));
        memset(&g_RoomScatterBurstAnchor, 0, sizeof(g_RoomScatterBurstAnchor));
        g_RoomModelBurstAsset = g_RoomScatterBurstAsset = NULL;
        memset(g_RoomModelBurstColors, 0, 8);
        memset(g_RoomScatterBurstColors, 0, 8);
        for (unsigned key = 8; key < 40; key += 8) {
            memset(g_RoomModelBurstColors + key + 4, 0, 4);
            memset(g_RoomScatterBurstColors + key + 4, 0, 4);
        }
    } else if (native == &g_RoomGrabHandlers) {
        g_RoomGrabSweepSide = 0;
        g_RoomGrabSlot = 0;
        memset(&g_RoomGrabOrbitCentre, 0, sizeof(g_RoomGrabOrbitCentre));
        RoomLib_PairA = RoomLib_PairB = 0;
        memset(g_RoomGrabTrailTrack, 0, 8);
        for (unsigned idx = 12; idx < 40; idx += 8)
            memset(g_RoomGrabTrailTrack + idx, 0, 4);
    } else if (native == &g_RoomM089Program) {
        memset(g_RoomM089Anchor, 0, 6);
        g_RoomM089CoreModel = g_RoomM089PulseModel = NULL;
        memset(g_RoomM089PulseColorTrack, 0, 8);
        for (unsigned key = 0; key < 5; ++key)
            memset(g_RoomM089PulseColorTrack + 12 + key * 8, 0, 4);
    } else if (native == &g_RoomM086SeekerProgram || native == &g_RoomM086GlintProgram) {
        memset(g_RoomM086ControllerParams, 0, sizeof(int) * 4);
        memset(g_RoomM086BounceGlintParams, 0, sizeof(int) * 3);
        g_RoomM086SeekerModelA = g_RoomM086SeekerModelB = NULL;
    } else if (native == &g_RoomGlowOrbHandlers) {
        g_RoomGlowOrbOwner = NULL;
        memset(&g_RoomGlowOrbAxes, 0, sizeof(g_RoomGlowOrbAxes));
        memset(&g_RoomGlowOrbOrigin, 0, sizeof(g_RoomGlowOrbOrigin));
        memset(&g_RoomGlowOrbSprite, 0, sizeof(g_RoomGlowOrbSprite));
        memset(&g_RoomGlowOrbFlashSprite, 0, sizeof(g_RoomGlowOrbFlashSprite));
    } else if (native == &g_RoomEffectSequenceHandlers) {
        g_EffectSequenceModel = NULL;
        memset(&g_EffectSequenceGlowSprite, 0, sizeof(g_EffectSequenceGlowSprite));
        memset(&g_EffectSequenceGlowSpriteSecondary, 0, sizeof(g_EffectSequenceGlowSpriteSecondary));
        memset(&g_EffectSequenceFinalGlowSprite, 0, sizeof(g_EffectSequenceFinalGlowSprite));
        memset(&g_EffectSequenceSparkleSprite, 0, sizeof(g_EffectSequenceSparkleSprite));
    } else if (native == &g_RoomThreeParticleHandlers) {
        memset(&RoomLib_ThreeParticlePacket, 0, sizeof(RoomLib_ThreeParticlePacket));
        memset(&RoomLib_ThreeParticleFloorPacket, 0, sizeof(RoomLib_ThreeParticleFloorPacket));
    } else if (native == &g_RoomDropFieldHandlers) {
        g_RoomDropFieldModel = NULL;
        memset(g_RoomDropFieldRings, 0, sizeof(g_RoomDropFieldRings));
        memset(&g_RoomDropFieldSpinSprite, 0, sizeof(g_RoomDropFieldSpinSprite));
        memset(&g_RoomDropFieldModelSprite, 0, sizeof(g_RoomDropFieldModelSprite));
        memset(&g_RoomDropFieldDoubleSprite, 0, sizeof(g_RoomDropFieldDoubleSprite));
        memset(&g_RoomDropFieldParticleSprite, 0, sizeof(g_RoomDropFieldParticleSprite));
        memset(&g_RoomDropFieldQuadSprite, 0, sizeof(g_RoomDropFieldQuadSprite));
        memset(&g_RoomDropFieldDropSprite, 0, sizeof(g_RoomDropFieldDropSprite));
        memset(&g_RoomDropFieldMotionSprite, 0, sizeof(g_RoomDropFieldMotionSprite));
    } else if (native == &g_SceneE20HoverOrbProgram) {
        memset(g_SceneE20EffectSlots, 0, sizeof(g_SceneE20EffectSlots));
        g_SceneE20HoverOrbHitCooldown = 0;
        g_SceneE20HoverOrbModel = NULL;
    }
}

int Scene_LoadRoom(unsigned int roomId)
{
    static GeomState *geometry_owner;
    GeomState *next_geometry = NULL;
    u32 next_geometry_disk_bytes = 0;
    s32 next_geometry_disk_entry_offset = 0;
    char name[8];
    unsigned int base;
    int map;
    int ready;
    int loaded;
    unsigned int i;
    unsigned int slot;
    int flags;
    SceneAssetView *room;
    SceneRoomDirectory *directory;
    TimUploadRecord *tim;
    SceneRoomRecord *record;
    SceneRoomShortRecord *shortRecord;
    Pe1GameState *state;

    name[0] = D_8009CDC8;
    memset(name + 1, 0, 6);
    flags = 0;
    state = &g_GameState;
    base = g_GameState.pe_image_base_lba;
    Text_EncodeBase32(name, roomId);

    {
        map = Text_ParseMapNumber((signed char *)name) - 1;
        if (map < 0 || map >= SCENE_ROOM_COUNT || !g_RoomSectorTable[map].roomSectors) return -1;
        if ((u64)base + g_RoomSectorTable[map].start + g_RoomSectorTable[map].scratchSectors +
            g_RoomSectorTable[map].textureSectors + g_RoomSectorTable[map].roomSectors >
            UINT64_C(0x100000000)) return -1;
        if ((ready = CdRom_ReadSectorsFromLba(base + g_RoomSectorTable[map].start,
                                        state->scene_load_scratch,
                                        g_RoomSectorTable[map].scratchSectors)) == -1) return -1;
        for (ready = 1; ready != 0; ready = CdRom_PollReady()) {
            if (ready == -1) {
                return -1;
            }
        }
    }

    loaded = 0;
    {
        if ((ready = CdRom_ReadSectorsFromLba(base + g_RoomSectorTable[map].start +
                                            g_RoomSectorTable[map].scratchSectors,
                                        state->texture_load_scratch,
                                        g_RoomSectorTable[map].textureSectors)) == -1) return -1;
        for (ready = 1; ready != 0; ready = CdRom_PollReady()) {
            if (!loaded) {
                room = (SceneAssetView *)state->scene_load_scratch;
                directory = SceneAsset_ResolveOffset(room, room->header.directoryOffset);
                tim = SceneAsset_ResolveOffset(room, directory->tims & 0x3FFFFF);
                for (i = 0; i < directory->tims >> 22; i++) {
                    Draw_LoadTimAsset(&tim[i], room);
                }
                loaded = 1;
            }
            if (ready == -1) {
                return -1;
            }
        }
    }

    loaded = 0;
    {
        if ((ready = CdRom_ReadSectorsFromLba(base + g_RoomSectorTable[map].start +
                                            g_RoomSectorTable[map].scratchSectors +
                                            g_RoomSectorTable[map].textureSectors,
                                        state->loaded_scene_assets,
                                        g_RoomSectorTable[map].roomSectors)) == -1) return -1;
        for (ready = 1; ready != 0; ready = CdRom_PollReady()) {
            if (!loaded) {
                room = state->texture_load_scratch;
                directory = SceneAsset_ResolveOffset(room, room->header.directoryOffset);
                tim = SceneAsset_ResolveOffset(room, directory->tims & 0x3FFFFF);
                for (i = 0; i < directory->tims >> 22; i++) {
                    Draw_LoadTimAsset(&tim[i], room);
                }
                loaded = 1;
            }
            if (ready == -1) {
                return -1;
            }
        }
    }

    /* Room code is linked statically on the host; no PS1 cache flush. */
    room = state->loaded_scene_assets;
    directory = SceneAsset_ResolveOffset(room, room->header.directoryOffset);

    /* Validate all native bindings before mutating the scene banks. */
    record = SceneAsset_ResolveOffset(room, directory->scripts & 0x3FFFFF);
    for (i = 0; i < directory->scripts >> 22; i++) {
        slot = record[i].source.bytes.slot;
        const void *native = SceneRoom_ResolveScriptDescriptor(map + 1, slot, record[i].u.handlerAddress);
        if (!native) {
            fprintf(stderr, "Scene_LoadRoom: native descriptor missing for M%03d slot %u (PS1 %08x)\n",
                    map + 1, slot, record[i].u.handlerAddress);
            return -2;
        }
    }

    /* Expand disk mesh records to native pointer width. Keep the original
     * byte arrays and view/control offsets, and append the native entries. */
    {
        SceneRoomRecord *aux = SceneRoom_FirstRecord(room, directory->taskAux);
        size_t offset = aux->source.offsetAndSlot & 0xffffff;
        u32 payload_bytes, declared;
        memcpy(&payload_bytes, aux, 4); payload_bytes &= 0xffffff;
        size_t room_bytes = (size_t)g_RoomSectorTable[map].roomSectors * 2048;
        if (offset > room_bytes || payload_bytes > room_bytes - offset || payload_bytes < sizeof(GeomState))
            goto invalid_geometry;
        const u8 *source = (const u8 *)room + offset;
        GeomState header;
        memcpy(&header, source, sizeof(header));
        memcpy(&declared, source, 4);
        if (declared < sizeof(GeomState) || declared > payload_bytes || header.entry_offset < 0 ||
            (size_t)header.entry_offset > declared ||
            header.entry_count06 > (declared - (size_t)header.entry_offset) / 0x38)
            goto invalid_geometry;
        u16 view_count;
        memcpy(&view_count, source + 10, sizeof(view_count));
        if (!view_count || header.entry_offset_1C < (s32)sizeof(GeomState) ||
            (size_t)header.entry_offset_1C > declared ||
            view_count > (declared - (size_t)header.entry_offset_1C) / sizeof(CameraViewport) ||
            header.ctrl_offset < 0 || (size_t)header.ctrl_offset > declared ||
            header.entry_count > (declared - (size_t)header.ctrl_offset) / sizeof(GeomCtrlEntry))
            goto invalid_geometry;
        for (unsigned control_index = 0; control_index < header.entry_count; ++control_index) {
            size_t control_offset = (size_t)header.ctrl_offset + control_index * sizeof(GeomCtrlEntry);
            GeomCtrlEntry control;
            memcpy(&control, source + control_offset, sizeof(control));
            s64 slots_offset = (s64)control_offset + control.slot_offset;
            u32 slots = control.head.packed >> 8;
            if (!slots) slots = 1; /* Reset reads the first slot even for an idle control. */
            if (slots_offset < 0 || (u64)slots_offset > declared ||
                slots > (declared - (size_t)slots_offset) / sizeof(GeomAnimationSlot))
                goto invalid_geometry;
            for (u32 slot_index = 0; slot_index < slots; ++slot_index)
                if (source[slots_offset + slot_index * sizeof(GeomAnimationSlot)] >= header.entry_count06)
                    goto invalid_geometry;
        }
        size_t entries_offset = (declared + _Alignof(GeomEntry) - 1) & ~((size_t)_Alignof(GeomEntry) - 1);
        size_t native_bytes = entries_offset + (size_t)header.entry_count06 * sizeof(GeomEntry);
        if (native_bytes > INT32_MAX) goto invalid_geometry;
        next_geometry = calloc(1, native_bytes);
        if (!next_geometry) goto invalid_geometry;
        memcpy(next_geometry, source, declared);
        next_geometry->entry_offset = (s32)entries_offset;
        u32 native_size = (u32)native_bytes;
        memcpy(next_geometry, &native_size, 4);
        GeomEntry *entries = (GeomEntry *)((u8 *)next_geometry + entries_offset);
        for (unsigned entry = 0; entry < header.entry_count06; ++entry) {
            size_t old_offset = (size_t)header.entry_offset + entry * 0x38;
            GeomEntry *native = &entries[entry];
            memcpy(native, source + old_offset, 0x30);
            if (native->prim_count) {
                s64 position = (s64)old_offset + native->u28.pos_ptr;
                s64 texture = (s64)old_offset + native->u2C.uv_ptr;
                if (position < 0 || texture < 0 || (position & 3) || (texture & 3) ||
                    (u64)position > declared || (u64)texture > declared ||
                    native->prim_count > (declared - (size_t)position) / sizeof(u32) ||
                    native->prim_count > (declared - (size_t)texture) / 8)
                    goto invalid_geometry;
                native->u28.pos_ptr = (s32)(position - ((u8 *)native - (u8 *)next_geometry));
                native->u2C.uv_ptr = (s32)(texture - ((u8 *)native - (u8 *)next_geometry));
            }
        }
        next_geometry_disk_bytes = declared;
        next_geometry_disk_entry_offset = header.entry_offset;
    }

    /* Task offsets are serialized 32-bit values, never native pointers.
     * Bound both their record and payload before committing scene state. */
    size_t room_bytes = (size_t)g_RoomSectorTable[map].roomSectors * 2048;
    size_t task_table = directory->taskBlock & 0x3FFFFF;
    if (!(directory->taskBlock >> 22) || task_table % _Alignof(SceneRoomRecord) || task_table > room_bytes ||
        sizeof(SceneRoomRecord) > room_bytes - task_table) goto invalid_tasks;
    SceneRoomRecord *task = (SceneRoomRecord *)((u8 *)room + task_table);
    u32 task_bytes;
    memcpy(&task_bytes, task, 4); task_bytes &= 0xFFFFFF;
    size_t task_offset = task->source.offsetAndSlot & 0xFFFFFF;
    if (task_offset > room_bytes || task_bytes > room_bytes - task_offset)
        goto invalid_tasks;
    RelocBlock *tasks = Script_RelocBlock((u8 *)room + task_offset, task_bytes);
    if (!tasks) goto invalid_tasks;

    state->requested_entity_bank = directory->entityBank;
    state->room_type = directory->roomType;

    record = SceneAsset_ResolveOffset(room, directory->bankRoots & 0x3FFFFF);
    i = 0;
    if (directory->bankRoots >> 22) do {
        state->bank_slots[record[i].source.bytes.slot] =
            SCENE_ROOM_PAYLOAD(room, &record[i]);
    } while (++i < directory->bankRoots >> 22);

    record = SceneAsset_ResolveOffset(room, directory->bankRows & 0x3FFFFF);
    for (i = 0; i < directory->bankRows >> 22; i++) {
        state->bank_rows[record[i].u.row.group][record[i].source.bytes.slot] =
            SCENE_ROOM_PAYLOAD(room, &record[i]);
    }

    state->bank_reset_944[0] = tasks;
    state->bank_reset_948[0] = SCENE_ROOM_PAYLOAD(room, SceneRoom_FirstRecord(room, directory->taskData));
    free(geometry_owner);
    geometry_owner = next_geometry;
    next_geometry = NULL;
    state->geometry_state = geometry_owner;
    state->geometry_disk_bytes = next_geometry_disk_bytes;
    state->geometry_disk_entry_offset = next_geometry_disk_entry_offset;

    shortRecord = SceneAsset_ResolveOffset(room, directory->taskSlots & 0x3FFFFF);
    for (i = 0; i < directory->taskSlots >> 22; i++) {
        state->field_resource_slots[shortRecord[i].source.bytes.slot] =
            SCENE_ROOM_PAYLOAD(room, &shortRecord[i]);
    }

    SceneRoom_ResetRoomOverlayState(map + 1);
    record = SceneAsset_ResolveOffset(room, directory->scripts & 0x3FFFFF);
    for (i = 0; i < directory->scripts >> 22; i++) {
        slot = record[i].source.bytes.slot;
        const void *native = SceneRoom_ResolveScriptDescriptor(map + 1, slot, record[i].u.handlerAddress);
        SceneRoom_InstallAndResetScriptDescriptor(map + 1, slot, native);
    }

    if (directory->samples & 0xFFC00000) {
        SceneRoomRecord *sample = SceneAsset_ResolveOffset(room, directory->samples & 0x3FFFFF);
        for (i = 0; i < directory->samples >> 22; i++) {
            if (sample[i].flags & 0x20) {
                int key = sample[i].u.track.key;

                /* The id words of the voice-bank entries, indexed as one
                 * halfword run from entry 1. */
                if ((u16)key & 1) {
                    g_AkaoVoiceBankEntries[1].ids[sample[i].source.bytes.slot] = key;
                    g_AkaoVoiceBankEntries[1].ids[sample[i].source.bytes.slot + 8] = sample[i].u.track.key;
                } else {
                    g_AkaoVoiceBankEntries[1].ids[sample[i].source.bytes.slot - 4] = key;
                    g_AkaoVoiceBankEntries[1].ids[sample[i].source.bytes.slot + 4] = sample[i].u.track.key;
                }
            }
        }
    }

    if (directory->tracks & 0xFFC00000) {
        SceneRoomRecord *track = SceneAsset_ResolveOffset(room, directory->tracks & 0x3FFFFF);
        for (i = 0; i < directory->tracks >> 22; i++) {
            if (track[i].flags & 0x10) {
                state->scene_audio.tracks.pending_key = track[i].u.track.key;
                state->scene_audio.tracks.pending_bank = track[i].u.track.bank;
                state->bank_asset_table =
                    SceneAsset_ResolveOffset(state->loaded_scene_assets, track[i].source.offsetAndSlot & 0xFFFFFF);
            }
        }
    }

    if (directory->streams & 0xFFC00000) {
        SceneRoomStreamRecord *stream = SceneAsset_ResolveOffset(room, directory->streams & 0x3FFFFF);

        for (i = 0; i < directory->streams >> 22; i++) {
            if (stream[i].flags & 0x10) {
                continue;
            }
            if (stream[i].flags & 0x20) {
                if (stream[i].channel == 0) {
                    flags |= 0x10;
                    if (state->pending_stream_banks[0] != stream[i].bank.value) {
                        flags |= 1;
                        state->pending_stream_banks[0] = stream[i].bank.low;
                    }
                } else if (stream[i].channel == 1) {
                    flags |= 0x20;
                    if (state->pending_stream_banks[1] != stream[i].bank.value) {
                        flags |= 2;
                        state->pending_stream_banks[1] = stream[i].bank.low;
                    }
                }
            } else {
                u16 bank = stream[i].bank.value;

                flags |= 0x40;
                if (state->pending_sample_bank != bank) {
                    flags |= 4;
                    state->pending_sample_bank = bank;
                }
            }
        }
    }

    loaded = 1;
    if ((flags & 1) && state->pending_stream_banks[0] != 0) {
        CdRom_ReadAssetSectors(2, state->pending_stream_banks[0], 0,
                       state->scene_load_scratch, 0x21, loaded);
    } else if (!(flags & 0x10)) {
        state->pending_stream_banks[0] = 0;
    }
    if ((flags & 2) && state->pending_stream_banks[1] != 0) {
        CdRom_ReadAssetSectors(2, state->pending_stream_banks[1], 1,
                       state->scene_load_scratch, 0x21, 1);
    } else if (!(flags & 0x20)) {
        state->pending_stream_banks[1] = 0;
    }
    if ((flags & 4) && state->pending_sample_bank != -1) {
        CdRom_ReadAssetSectors(1, state->pending_sample_bank, 0,
                       state->scene_load_scratch, 0x21, 1);
    } else if (!(flags & 0x40)) {
        state->pending_sample_bank = -1;
    }
    return 0;

invalid_tasks:
    free(next_geometry);
    fprintf(stderr, "Scene_LoadRoom: invalid task block for M%03d\n", map + 1);
    return -3;

invalid_geometry:
    free(next_geometry);
    fprintf(stderr, "Scene_LoadRoom: invalid native geometry for M%03d\n", map + 1);
    return -3;
}

s32 Scene_StreamTexturePage(void)
{
    Pe1GameState *state=&g_GameState;
    int page=(s8)state->room_type;
    if(page==g_LoadedTexturePageId)return 0;
    int index=page+0x2b;
    if((unsigned)index>=80 || !state->scene_load_scratch ||
       (uintptr_t)state->scene_load_scratch%_Alignof(TimFile))return -3;
    u32 first=g_PeImageSectorTable[index],last=g_PeImageSectorTable[index+1];
    if(last<=first || last-first>0x20000/2048 || first>UINT32_MAX-state->pe_image_base_lba)return -3;
    DrawSync(0);
    if(CdRom_ReadSectorsFromLba(state->pe_image_base_lba+first,state->scene_load_scratch,last-first)<0)return -1;
    int ready;
    while((ready=CdRom_PollReady())>0){}
    if(ready<0)return -2;
    /* Bound the authored TIM blocks before the existing uploader follows
     * their relative lengths. Invalid input must not commit the cache. */
    size_t bytes=(size_t)(last-first)*2048,at=8;
    TimFile *tim=(TimFile *)state->scene_load_scratch;
    if(tim->magic!=16 || (tim->flags&~11) || (tim->flags&7)>3)return -4;
    for(unsigned block=0;block<((tim->flags&8)?2u:1u);++block){
        u32 length;RECT rect;
        if(bytes-at<12)return -4;
        memcpy(&length,(u8 *)tim+at,4);memcpy(&rect,(u8 *)tim+at+4,sizeof(rect));
        if(length<12 || (length&3) || length>bytes-at || rect.x<0 || rect.y<0 || rect.w<=0 || rect.h<=0 ||
           rect.x+rect.w>1024 || rect.y+rect.h>512 || (u64)rect.w*rect.h*2>length-12)return -4;
        at+=length;
    }
    Asset_LoadTimImage(tim);
    state->field_bg_cache_bank=(s8)page;
    return 0;
}

/* Disc-change gating for scene-area transitions. */

int CdRom_DetectDiscChange(void) {
    unsigned int *state = &g_GameState.flags;
    int offset = g_SceneAreaType - 0xA;

    if ((unsigned int)offset < 5) {
        int value = g_SavedSceneAreaType;

        if (g_SceneAreaType != (unsigned char)value) {
            *state |= 0x200000;
        }

        if (((unsigned int)offset >> 1) != ((value - 0xA) / 2)) {
            g_DiscChangeFlags |= 4;
        }
    }

    return 0;
}

/* Load the area textures and model/action bank through the original phases. */
int Scene_LoadEntityTexture(void)
{
    int bank = g_SceneAreaType;
    u32 lba = g_GameState.pe_image_base_lba;
    Pe1GameState *state = &g_GameState;
    SceneAssetBlob *blob;
    SceneAssetDirectory *directory;
    TimUploadRecord *tim;
    TimUploadRecord *firstTim;
    u32 timOffset;
    SceneBankAssetRecord *record;
    SceneBankAssetRecord *firstRecord;
    unsigned i;
    u32 packed;
    u32 offsetMask;
    int index;
    int result;
retry:
    switch (state->entity_texture_phase) {
    case 0:
        if (state->flags & 0x200000)
            state->entity_texture_phase = 1;
        else
            state->entity_texture_phase = 6;
        break;
    case 1:
        index = bank + 3;
        if (CdRom_ReadSectorsFromLba(lba + g_PeImageSectorTable[index],
                state->scene_load_scratch,
                g_PeImageSectorBoundsNext[index] - g_PeImageSectorTable[index]) == -1) return -1;
        state->entity_texture_phase = 2;
        return 1;
    case 2:
        result = CdRom_PollReady();
        if (result == -1) {
            state->entity_texture_phase = 1;
            return -1;
        }
        if (result != 0)
            return 1;
        if (state->flags & 0x20000) {
            if (!(state->flags & 0x80000) && g_AyaActionStep < 2)
                return 1;
            state->flags |= 0x40000;
        }
        state->entity_texture_phase = 3;
        break;
    case 3:
        blob = (SceneAssetBlob *)state->scene_load_scratch;
        directory = SceneAsset_ResolveOffset(blob, blob->directoryOffset);
        timOffset = directory->timEntries & 0x3fffff;
        firstTim = SceneAsset_ResolveOffset(blob, timOffset);
        i = 0;
        if (directory->timEntries >> 22) {
            tim = firstTim;
            do {
                Draw_LoadTimAsset(tim, blob);
                packed = directory->timEntries;
                ++i;
                ++tim;
            } while (i < (packed >> 22));
        }
        state->entity_texture_phase = 4;
        break;
    case 4:
        index = bank + 8;
        if (CdRom_ReadSectorsFromLba(lba + g_PeImageSectorTable[index],
                state->voice_bank_base_1400,
                g_PeImageSectorBoundsNext[index] - g_PeImageSectorTable[index]) == -1) return -1;
        state->entity_texture_phase = 5;
        return 1;
    case 5:
        result = CdRom_PollReady();
        if (result == -1) {
            state->entity_texture_phase = 4;
            return -1;
        }
        /* Reuse the counter scratch for readiness before the next loop phase. */
        i = result != 0;
        if (i)
            return 1;
        state->entity_texture_phase = 6;
        break;
    case 6:
        blob = (SceneAssetBlob *)state->voice_bank_base_1400;
        directory = SceneAsset_ResolveOffset(blob, blob->directoryOffset);
        {
            SceneBankAssetRecord *root = SceneAsset_ResolveOffset(blob, directory->bankRootEntries & 0x3fffff);
            state->bank_slots[0] = SceneAsset_ResolveOffset(blob, root->source.offsetAndId & 0xffffff);
        }
        firstRecord = SceneAsset_ResolveOffset(blob, directory->bankRowEntries & 0x3fffff);
        i = 0;
        if (directory->bankRowEntries >> 22) {
            offsetMask = 0xffffff;
            record = firstRecord;
            do {
                unsigned id;
                id = record->source.bytes.id;
                /* The row id indexes the flattened bank row table. */
                size_t columns = sizeof(state->bank_rows[0]) / sizeof(state->bank_rows[0][0]);
                state->bank_rows[id / columns][id % columns] = SceneAsset_ResolveOffset(
                    blob, record->source.offsetAndId & offsetMask);
                packed = directory->bankRowEntries;
                ++i;
                ++record;
            } while (i < (packed >> 22));
        }
        state->entity_texture_phase = 0;
        state->loaded_entity_bank = state->requested_entity_bank;
        state->flags &= ~0x200000;
        return 0;
    default:
        return 0;
    }
    goto retry;
}

/* Scene start-up steps 0x20..0x27: area type and disc swap bookkeeping,
 * the entity texture banks, then the player's model and render object. */
s32 Scene_InitEntityPlayer(s32 resume) {
    Pe1GameState *gameState = &g_GameState;
    s8 *renderSetup;
    u32 area;
    u8 saved;
    int result;

    switch (g_GameState.scene_init_phase) {
    case 32:
        g_GameState.flags |= 0x20000;
        if (resume) {
            saved = g_SceneAreaType;
            g_SceneAreaType = 0xE;
            g_GameState.scene_area_type_backup = saved;
        } else {
            g_SceneAreaType = g_GameState.scene_area_type_backup;
        }
        gameState->scene_init_phase = 0x21;
        return 1;
    case 33:
        gameState->scene_init_phase = 0x22;
        return 1;
    case 34:
        gameState->scene_init_phase = 0x23;
        return 1;
    case 35:
        area = g_SceneAreaType - 0xA;
        if (area < 5) {
            saved = g_SavedSceneAreaType;
            if (g_SceneAreaType != saved) {
                g_GameStateFlagsWord = g_GameState.flags | 0x200000;
            }
            if ((area >> 1) != ((saved - 0xA) / 2)) {
                g_DiscChangeFlags |= 4;
            }
        }
        gameState->scene_init_phase = 0x24;
        return 1;
    case 36:
        result = Scene_LoadEntityTexture();
        if (result < 0) return result;
        if (result == 1) {
            return 1;
        }
        if (resume) {
            gameState->scene_init_phase = 0x27;
            return 1;
        }
        gameState->scene_init_phase = 0x25;
        return 1;
    case 37:
        g_GameState.story_day_flags |= 4;
        Scene_SetStoryDay(g_CurrentStoryDay[0]);
        gameState->scene_init_phase = 0x26;
        return 1;
    case 38:
        result = Scene_LoadEntityTextures();
        if (result < 0) return result;
        if (result == 1 && gameState->scene_init_subphase < 0xB) {
            return 1;
        }
        gameState->scene_init_phase = 0x27;
        return 1;
    case 39:
        {
            FieldActor *player = g_PlayerEntity;
            player->model_state.model_header = g_GameState.bank_slots[0];
            player->action_data = g_GameState.bank_rows[0][0x15];
            Entity_SetActionMode((BattleEntity *)player, 0x15);
        }
        {
            FieldActor *player = g_PlayerEntity;
            Render_SetupEntityPrims(&player->render_object, player->model_state.model_header,
                                    (u8 *)player->allocation_block + 2 * sizeof(RenderShadowQuad), 0x3C0, 0x100, 0,
                                    0x1C0, 2, &renderSetup, 1);
        }
        {
            FieldActor *player = g_PlayerEntity;
            Render_InitRoomPrimState(&player->render_object);
        }
        {
            FieldActor *player = g_PlayerEntity;
            Render_DrawWithAnim(&player->render_object, player->action_data, 0,
                                &g_RenderLightingState);
        }
        {
            FieldActor *player = g_PlayerEntity;
            player->render_object.header->shadow_radius = player->render_object.hit_cylinder.radius * 2;
        }
        g_GameStateFlagsWord = g_GameState.flags & ~0x60000;
        if (resume) {
            gameState->flags |= 0x80000;
        } else {
            gameState->flags &= ~0x80000;
        }
        gameState->scene_init_phase = 0x20;
        /* fallthrough */
    default:
        return 0;
    }
}

/* Existing byte symbols for the shared state's pending/current day and flags.
 * Preserve the unsigned pending-byte read before its signed-day conversion. */

int Scene_SetStoryDay(s32 storyDay) {
    Pe1GameState *gameState = &g_GameState;
    u8 flags;

    if (storyDay == -1) {
        u8 pending;
        u8 storyFlags;
        pending = g_PendingStoryDay;
        storyFlags = g_DiscChangeFlags;
        storyDay = (s8)pending;
        g_CurrentStoryDay[0] = pending;
        g_DiscChangeFlags = storyFlags | 3;
    }

    if (((g_GameStateFlags & 2) != 0) || ((gameState->flags & 2) != 0)) {
        g_DiscChangeFlags |= 2;
        g_FieldMoveLock &= ~2U;
    }

    flags = gameState->story_day_flags;
    if ((flags & 4) != 0) {
        gameState->story_day_flags = (flags | 3) & ~4;
    }

    if ((storyDay - 1U) < 8U) {
        if (storyDay != gameState->pending_story_day) {
            s8 newStoryDay;

            newStoryDay = storyDay;
            gameState->pending_story_day = newStoryDay;
            gameState->current_story_day = newStoryDay;
            gameState->story_day_flags |= 1;
        }
    }

    return 0;
}


/* Streams the area entity bank and rebuilds the scene render object. */
int Scene_LoadEntityTextures(void)
{
    int area = g_SceneAreaType;
    int day = g_CurrentStoryDay[0];
    u32 lba = g_PeImageBaseLba;
    Pe1GameState *state = &g_GameState;
    SceneAssetView *view;
    SceneAssetDirectory *directory;
    TimUploadRecord *tim;
    TimUploadRecord *firstTim;
    SceneBankAssetRecord *firstRecord;
    SceneBankAssetRecord *root;
    RenderObjectHeader *model;
    u32 streamEntries, packed;
    u32 i;
    int result;
    int sectorIndex;
    s8 *textureData;

    if (area - 10U >= 5)
        return 0;
retry:
    switch (state->scene_init_subphase) {
    case 0:
        if (state->story_day_flags & 1) {
            state->scene_init_subphase = 1;
            break;
        }
        if (state->story_day_flags & 2) {
            state->scene_init_subphase = 11;
            break;
        }
        Scene_UpdateBgDraw();
        return 0;
    case 1:
        if (day - 2U >= 5) {
            state->scene_init_subphase = 4;
            break;
        }
        sectorIndex = day + 0x26;
        result = CdRom_ReadSectorsFromLba(lba + g_PeImageSectorTable[sectorIndex],
            state->scene_load_scratch,
            g_PeImageSectorBoundsNext[sectorIndex] - g_PeImageSectorTable[sectorIndex]);
        Scene_UpdateBgDraw();
        if (result == -1) return -1;
        state->scene_init_subphase = 2;
        return 1;
    case 2:
        result = CdRom_PollReady();
        if (result == -1) {
            state->scene_init_subphase = 1;
            return -1;
        }
        if (result != 0) {
            if (!(g_GameStateFlags & 2))
                Scene_UpdateBgDraw();
            return 1;
        }
        state->scene_init_subphase = 3;
        break;
    case 3:
        view = (SceneAssetView *)state->scene_load_scratch;
        directory = SCENE_ASSET_AT(view, view->header.directoryOffset);
        firstTim = SCENE_ASSET_AT(view, directory->timEntries & 0x3fffff);
        i = 0;
        if (directory->timEntries >> 22) {
            tim = firstTim;
            do {
                Draw_LoadTimAsset(tim, view);
                packed = directory->timEntries;
                i++;
                tim++;
            } while (i < packed >> 22);
        }
        state->scene_init_subphase = 4;
        break;
    case 4:
        sectorIndex = (state->requested_entity_bank - 10) / 2 * 8 + day + 0x16;
        if (CdRom_ReadSectorsFromLba(lba + g_PeImageSectorTable[sectorIndex],
                state->entity_texture_blob,
                g_PeImageSectorBoundsNext[sectorIndex] - g_PeImageSectorTable[sectorIndex]) == -1) return -1;
        state->scene_init_subphase = 5;
        if (!(g_GameStateFlags & 2))
            Scene_UpdateBgDraw();
        return 1;
    case 5:
        result = CdRom_PollReady();
        if (result == -1) {
            state->scene_init_subphase = 4;
            return -1;
        }
        if (result != 0) {
            if (!(g_GameStateFlags & 2))
                Scene_UpdateBgDraw();
            return 1;
        }
        state->scene_init_subphase = 6;
        break;
    case 6:
        view = state->entity_texture_blob;
        directory = SCENE_ASSET_AT(view, view->header.directoryOffset);
        firstRecord = SCENE_ASSET_AT(view, directory->bankRowEntries & 0x3fffff);
        i = 0;
        if (directory->bankRowEntries >> 22) {
            do {
                state->bank_rows[0][firstRecord[i].source.bytes.id] = SCENE_ASSET_AT(view, firstRecord[i].source.offsetAndId & 0xffffff);
                packed = directory->bankRowEntries;
                i++;
            } while (i < packed >> 22);
        }
        state->cd_range_read_mode = 0;
        for (i = 0; i < 3; i++)
            state->scene_object_tables[i] = 0;
        streamEntries = directory->streamEntries;
        if (streamEntries & 0xffc00000) {
            SceneCdStreamRecord *first = SCENE_ASSET_AT(view, streamEntries & 0x3fffff);
            i = 0;
            if (streamEntries >> 22) {
                do {
                    state->scene_object_tables[i] = SCENE_ASSET_AT(view, first[i].offset & 0xffffff);
                    i++;
                } while (i < directory->streamEntries >> 22);
            }
            state->cd_range_read_mode = first->cdIndex;
        }
        if (g_GameStateFlags & 2) {
            state->scene_init_subphase = 7;
            return 1;
        }
        if (state->story_day_flags & 2) {
            state->scene_init_subphase = 11;
            break;
        }
        Scene_UpdateBgDraw();
        state->scene_init_subphase = 0;
        state->story_day_flags &= ~3;
        return 0;
    case 7:
        if (CdRom_ReadAssetSectors(1, state->cd_range_read_mode, 0,
                state->scene_load_scratch, 0x21, 0) == 0) {
            state->scene_init_subphase = 11;
            break;
        }
        return 1;
    case 11:
        state->story_day_flags |= 1;
        state->scene_init_subphase = 12;
        return 1;
    case 12:
        state->scene_init_subphase = 13;
        return 1;
    case 13:
        view = state->entity_texture_blob;
        directory = SCENE_ASSET_AT(view, view->header.directoryOffset);
        firstRecord = SCENE_ASSET_AT(view, directory->bankRowEntries & 0x3fffff);
        i = 0;
        if (directory->bankRowEntries >> 22) {
            do {
                state->bank_rows[0][firstRecord[i].source.bytes.id] = SCENE_ASSET_AT(view, firstRecord[i].source.offsetAndId & 0xffffff);
                packed = directory->bankRowEntries;
                i++;
            } while (i < packed >> 22);
        }
        state->cd_range_read_mode = 0;
        for (i = 0; i < 3; i++)
            state->scene_object_tables[i] = 0;
        streamEntries = directory->streamEntries;
        if (streamEntries & 0xffc00000) {
            SceneCdStreamRecord *first = SCENE_ASSET_AT(view, streamEntries & 0x3fffff);
            i = 0;
            if (streamEntries >> 22) {
                do {
                    state->scene_object_tables[i] = SCENE_ASSET_AT(view, first[i].offset & 0xffffff);
                    i++;
                } while (i < directory->streamEntries >> 22);
            }
            state->cd_range_read_mode = first->cdIndex;
        }
        if (g_PlayerEntity == 0) {
            u32 sceneFlag = state->flags & 2;
            return sceneFlag == 0;
        }
        if (g_GameStateFlags & 2) {
            root = SCENE_ASSET_AT(view, directory->bankRootEntries & 0x3fffff);
            model = SCENE_ASSET_AT(view, root->source.offsetAndId & 0xffffff);
        } else {
            model = state->scene_object_model;
        }
        Render_SetupEntityPrims(&state->scene_object, model,
            state->scene_object_work, 0x2C0, 0x80, 0, 0x1C2, 0, &textureData, 1);
        if (state->scene_object.animation_source == 0 && g_PlayerEntity != 0) {
            state->scene_object.animation_source = &g_PlayerEntity->render_object;
            state->scene_object.animation_state = 3;
            state->scene_object.animation_id = 0x12;
        }
        state->scene_object.shade = 0x80;
        state->scene_object.lightNegativeY = 0xC;
        state->scene_object.lightPositiveY = 0x18;
        Render_InitRoomPrimState(&state->scene_object);
        Render_DrawWithAnim(&state->scene_object, 0, 0, &g_RenderLightingState);
        state->scene_init_subphase = 0;
        state->story_day_flags &= ~3;
        return 1;
    default:
        return 0;
    }
    goto retry;
}

/* Draws the scene's background object animated from the player's render
 * object. */
int Scene_UpdateBgDraw(void) {
    Pe1GameState *state = &g_GameState;
    FieldActor *entity;

    if (state->flags & 0xC0000) {
        return 0;
    }

    entity = g_PlayerEntity;
    if (entity == 0) {
        return 0;
    }

    if (entity->flags & 0x20000040) {
        return 0;
    }

    if (!(g_GameStateFlags & 2) && (g_FieldMoveLock & 2)) {
        return 0;
    }

    if (g_GameState.scene_object.animation_source == 0) {
        g_GameState.scene_object.animation_source = &entity->render_object;
        g_GameState.scene_object.animation_state = 3;
        g_GameState.scene_object.animation_id = 0x12;
    }

    Render_SetGteScreenOffset();
    Render_TransformVertices(&state->scene_object);
    Render_TransformMorphVertices(&state->scene_object, g_EntityRenderScratch);

    if ((g_GameStateFlags & 2) || g_PlayerEntity->render_object.variant_visible != 0) {
        Render_DrawEntity(&state->scene_object, g_EntityRenderScratch);
    }

    Render_ResetGteScreenOffset();
    return 0;
}
