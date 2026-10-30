#include <SDL3/SDL.h>
#include "pe_hooks.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <psyz/audio.h>
#include <psyz/input.h>
#include <psyz/overlay_sdl3.h>
#include <psyz/system.h>
#include <psyz/video.h>
#include <psyz/gpu.h>
#include "pe1/field_actor.h"
#include "pe1/geom_state.h"
#include "pe1/render_object.h"
#include "pe1/render_buffers.h"
/* Public LIBETC endpoint: initialize native gamepad discovery. */
void PadInit(int mode);
#include "common.h"
#include "pe1/aya.h"
#include "pe1/battle_runtime.h"
#include "pe1/battle_start.h"
#include "pe1/battle_status.h"
#include "pe1/boot.h"
#include "pe1/boot_services.h"
#include "pe1/cdrom_buffers.h"
#include "pe1/display_env.h"
#include "pe1/draw_buffers.h"
#include "pe1/field_sfx.h"
#include "pe1/font.h"
#include "pe1/game_state.h"
#include "pe1/game_state_types.h"
#include "pe1/game_timers.h"
#include "pe1/gte.h"
#include "pe1/inventory_slots.h"
#include "pe1/menu_inventory.h"
#include "pe1/menu_widget.h"
#include "pe1/pad_state.h"
#include "pe1/player_entity.h"
#include "pe1/psyq_callbacks.h"
#include "pe1/psyq_cd.h"
#include "pe1/psyq_gpu.h"
#include "pe1/psyq_spu_api.h"
#include "pe1/scene_dispatch.h"
#include "pe1/scene_flags.h"
#include "pe1/textbox.h"

#ifdef PE_NATIVE_COVERAGE
extern int __llvm_profile_write_file(void);
/* The runner intentionally uses _Exit under SDK callback stacks. LLVM's
 * atexit writer would be bypassed; write once, immediately before success. */
#define BOOT_WRITE_PROFILE()                                       \
    do {                                                           \
        if (__llvm_profile_write_file()) {                         \
            fprintf(stderr, "Cannot write execution profile\n");   \
            fflush(stderr);                                        \
            _Exit(2);                                              \
        }                                                          \
    } while (0)
#else
#define BOOT_WRITE_PROFILE() ((void)0)
#endif

/* Native backing for the original boot allocator. Overlay workspaces are
 * ordinary persistent memory; native OT/actor pointers need larger arenas. */
_Alignas(16) u8 g_BootWorkArea[0x20000];
_Alignas(16) u8 D_8010BD00[BOOT_TEXTURE_WORKSPACE_BYTES];
_Alignas(16) u8 D_80120D08[0x200000];
_Alignas(16) u8 g_SceneLoadScratchArea[0x20000];
static _Alignas(16) u8 g_BootLoadedSceneArea[0x80008];
u8 *g_StrFileDirBuffer = g_BootLoadedSceneArea + 8;
static _Alignas(16) u8 g_BootDisplayWorkArea[BOOT_DISPLAY_WORK_SIZE];
u8 *g_CdLoadAreaBase = g_BootDisplayWorkArea;

/* Shared native state; external variables have one storage definition. */

/* Boot-time system initialization: Boot_InitSystem, the subsystem and global
 * reset it is followed by, the start-up display reset, the vsync callback and
 * the pad start-up (G0341/G0342). */

void Render_ResetScene(int arg0, int arg1);
void InitGeom(void);
void SetGeomOffset(long x, long y);
void SetGeomScreen(long h);
void MemCard_InitManager(void);
void Boot_InitMemCard(void);

int g_BootHostActive;
u32 g_BootHostVBlankLimit;
u32 g_BootHostVBlankCount;
u32 g_BootHostControlAt;
u32 g_BootHostCrashAt;
int g_BootHostCrashAbort;
void (*g_BootHostHdOptions)(int, const char *);
void (*g_BootHostGraphicsOptions)(int, int, int);
const char *g_BootHostVramPath;
const char *g_BootHostFramePath;
const char *g_BootHostAudioPath;
static FILE *hostAudioFile;
BootHostPadEvent *g_BootHostPadEvents;
unsigned g_BootHostPadEventCount;
BootHostHealthEvent *g_BootHostHealthEvents;
unsigned g_BootHostHealthEventCount;
unsigned g_BootHostHealthApplied;
static unsigned hostPadEvent;
static u16 hostButtons;

/* Observe the existing playback mix; this never requests additional samples. */
static void Boot_CaptureHostAudio(const short *samples, int frames, void *userdata) {
    FILE *out = userdata;
    while (frames > 0) {
        unsigned batch = frames > 1024 ? 1024 : (unsigned)frames;
        u8 bytes[1024 * 4];
        for (unsigned i = 0; i < batch * 2; ++i) {
            u16 value = (u16)samples[i];
            bytes[i * 2] = (u8)value;
            bytes[i * 2 + 1] = (u8)(value >> 8);
        }
        if (fwrite(bytes, 4, batch, out) != batch) {
            fprintf(stderr, "Cannot write audio capture\n");
            fflush(stderr);
            _Exit(2);
        }
        samples += batch * 2;
        frames -= (int)batch;
    }
}

static void Boot_CloseHostAudioCapture(void) {
    Psyz_AudioSetCaptureCallback(NULL, NULL);
    if (hostAudioFile) {
        int closed = fclose(hostAudioFile);
        hostAudioFile = NULL;
        if (closed) {
            fprintf(stderr, "Cannot finish audio capture\n");
            fflush(stderr);
            _Exit(2);
        }
    }
}

static void Boot_ApplyScheduledPadAndHealthEvents(void) {
    if (g_BootHostPadEventCount) {
        while (hostPadEvent < g_BootHostPadEventCount &&
               g_BootHostPadEvents[hostPadEvent].vblank <= g_BootHostVBlankCount) {
            hostButtons = g_BootHostPadEvents[hostPadEvent++].buttons;
        }
    }

    while (g_BootHostHealthApplied < g_BootHostHealthEventCount &&
           g_BootHostHealthEvents[g_BootHostHealthApplied].vblank <= g_BootHostVBlankCount) {
        BootHostHealthEvent *event = &g_BootHostHealthEvents[g_BootHostHealthApplied];
        Combatant *player = g_PlayerEntity ? (Combatant *)g_PlayerEntity->state : NULL;
        if (event->vblank != g_BootHostVBlankCount ||
            !player ||
            player->curHP != event->expected ||
            event->value > player->maxHP) {
            fprintf(
                stderr,
                "Diagnostic HP precondition failed at frame %u (expected %u, actual %d, value %u)\n",
                g_BootHostVBlankCount,
                event->expected,
                player ? (int)player->curHP : -1,
                event->value
            );
            fflush(stderr);
            _Exit(2);
        }
        player->curHP = event->value;
        ++g_BootHostHealthApplied;
        printf(
            "{\"event\":\"diagnostic_hp\",\"vblank\":%u,\"expected\":%u,\"value\":%u}\n",
            g_BootHostVBlankCount,
            event->expected,
            event->value
        );
        fflush(stdout);
    }
}

static void Boot_WriteHostCaptures(void) {
    if (hostAudioFile && fflush(hostAudioFile)) {
        fprintf(stderr, "Cannot flush audio capture\n");
        fflush(stderr);
        _Exit(2);
    }
    if (g_BootHostVramPath) {
        static _Alignas(8) u16 vram[1024 * 512];
        RECT all = {0, 0, 1024, 512};
        StoreImage(&all, vram);
        DrawSync(0);
        FILE *out = fopen(g_BootHostVramPath, "wb");
        if (!out) {
            fprintf(stderr, "Cannot write VRAM capture\n");
            fflush(stderr);
            _Exit(2);
        }
        int written = fwrite(vram, 1, sizeof(vram), out) == sizeof(vram);
        if (fclose(out) != 0 || !written) {
            fprintf(stderr, "Cannot finish VRAM capture\n");
            fflush(stderr);
            _Exit(2);
        }
    }
    if (g_BootHostFramePath) {
        int width = 0;
        int height = 0;
        unsigned char *pixels = Psyz_VideoAllocRenderedFrame(&width, &height);
        if (!pixels || width <= 0 || height <= 0) {
            free(pixels);
            fprintf(stderr, "Cannot capture rendered display\n");
            fflush(stderr);
            _Exit(2);
        }
        FILE *out = fopen(g_BootHostFramePath, "wb");
        if (!out) {
            free(pixels);
            fprintf(stderr, "Cannot write rendered capture\n");
            fflush(stderr);
            _Exit(2);
        }
        size_t bytes = (size_t)width * (size_t)height * 3;
        int written = fprintf(out, "P6\n%d %d\n255\n", width, height) > 0;
        if (written) {
            written = fwrite(pixels, 1, bytes, out) == bytes;
        }
        free(pixels);
        if (fclose(out) != 0 || !written) {
            fprintf(stderr, "Cannot finish rendered capture\n");
            fflush(stderr);
            _Exit(2);
        }
    }
}

static void Boot_PrintHostDiagnosticJson(int terminal) {
    DISPENV display;
    GetDispEnv(&display);
    printf(
        "{\"event\":\"%s\",\"vblanks\":%u,\"scene\":%u,\"flags\":%u,\"draw_slot\":%d,\"movie\":%d,"
        "\"pad\":%u,\"game_flags\":%u,\"video_state\":%u,\"game_frame\":%u,\"movie_frame\":%d,"
        "\"move_lock\":%d,\"display\":{\"x\":%d,\"y\":%d,\"w\":%d,\"h\":%d,\"rgb24\":%d},\"dialogue\":[",
        terminal ? "frame_limit" : "control",
        g_BootHostVBlankCount,
        (u32)g_SceneDispatchToken,
        g_GameStateFlags,
        g_ActiveDrawSlot,
        g_GameState.scene_audio.tracks.movie_index,
        g_FieldPadBits,
        g_GameState.flags,
        g_GameState.scene_audio.tracks.draw_enabled.bits,
        g_GlobalFrameCounter[0],
        (s16)g_GameState.scene_audio.tracks.movie_frame,
        g_FieldMoveLock,
        display.disp.x,
        display.disp.y,
        display.disp.w,
        display.disp.h,
        display.isrgb24
    );
    for (unsigned i = 0; i < 4; ++i) {
        printf("%s{\"state\":%u,\"page\":%d}", i ? "," : "", g_TextboxEntries[i].state, g_TextboxEntries[i].page_id);
    }
    printf("],\"actors\":[");

    unsigned actor_index = 0;
    for (FieldActor *actor = g_FieldActorListHead; actor; actor = actor->next) {
        printf(
            "%s{\"type\":%u,\"sub\":%u,\"player\":%s,\"flags\":%u,\"position\":[%d,%d,%d],\"yaw\":%d,"
            "\"animation\":{\"mode\":%u,\"frame_raw\":%d,\"previous_raw\":%d,\"step_raw\":%d,\"last_frame\":%u,\"stop_frame\":%u},"
            "\"script_locals\":[",
            actor_index++ ? "," : "",
            actor->type_id,
            actor->sub_id,
            actor == g_PlayerEntity ? "true" : "false",
            actor->flags,
            actor->pos_x,
            actor->pos_y,
            actor->pos_z,
            actor->rot_y,
            (unsigned)actor->mode,
            actor->anim.fixed,
            actor->anim_prev,
            actor->anim_step,
            (unsigned)actor->action,
            (unsigned)actor->anim_frame_target
        );
        for (unsigned i = 0; i < sizeof(actor->pad_0AC) / sizeof(actor->pad_0AC[0]); ++i) {
            printf("%s%u", i ? "," : "", actor->pad_0AC[i]);
        }
        printf("]}");
    }

    printf("],\"menu\":");
    if (g_MenuWidgetCurrentNode) {
        MenuWidgetNode *node = g_MenuWidgetCurrentNode;
        printf(
            "{\"mode\":%d,\"selection\":%d,\"cursor\":[%d,%d]}",
            node->mode,
            node->selected_base,
            node->cursor_x,
            node->cursor_y
        );
    } else {
        printf("null");
    }

    printf(",\"combat\":{\"mode\":%d,\"live_enemies\":%d", g_BattleModeState, (int)g_BattleLiveEnemyCount);
    if (g_PlayerEntity && g_PlayerEntity->state) {
        Combatant *player = (Combatant *)g_PlayerEntity->state;
        printf(",\"player_hp\":%u,\"player_max_hp\":%u", (unsigned)player->curHP, (unsigned)player->maxHP);
    }
    printf(
        "},\"battle_rewards\":{\"result_phase\":%s,\"exp\":%u,\"pe_bonus\":%u,\"rolled\":[",
        g_BattleModeState == 2 ? "true" : "false",
        g_BattleExpReward,
        (unsigned)g_BattlePeBonusReward
    );
    for (unsigned i = 0; i < 10; ++i) {
        printf(
            "%s{\"id\":%u,\"extra\":%d}",
            i ? "," : "",
            (unsigned)(u16)g_BattleRewardSlots[i].id,
            (int)g_BattleRewardSlots[i].extra
        );
    }
    printf("],\"remaining\":");

    /* The reward menu consumes this original override list. Observe its
     * holes and ammo records without selecting or rebuilding either list. */
    if (g_BattleModeState == 2 &&
        g_MenuWidgetCurrentNode &&
        g_InvActiveListOverride == g_InvActiveListItems &&
        g_InvOverrideSlotLimit >= 0 &&
        g_InvOverrideSlotLimit <= 10) {
        printf("[");
        for (int i = 0; i < g_InvOverrideSlotLimit; ++i) {
            unsigned value = (u16)g_InvActiveListItems[i];
            const ItemDataRecord *item = Inv_LookupItemRecord(value);
            printf("%s{\"value\":%u,\"item\":", i ? "," : "", value);
            if (item) {
                printf(
                    "{\"id\":%u,\"kind\":%u,\"ammo\":%u}",
                    (unsigned)item->itemId,
                    (unsigned)item->kind,
                    (unsigned)item->ammo
                );
            } else {
                printf("null");
            }
            printf("}");
        }
        printf("]");
    } else {
        printf("null");
    }

    printf("},\"graphics\":{\"resolution\":%u,\"geometry\":%d,\"antialiasing\":%d,\"filtering\":%d},\"inventory\":{\"capacity\":%u,\"slots\":[",
           Psyz_VideoGetInternalResolution(), Psyz_VideoGetGeometryCorrection(),
           Psyz_VideoGetAntialiasing(), Psyz_VideoGetModelFiltering(), (unsigned)g_AyaSaveState.inventory_slot_count);
    for (unsigned slot = 0; slot < 50; ++slot) {
        printf("%s%u", slot ? "," : "", (unsigned)(u16)g_AyaSaveState.inventory_items[slot]);
    }
    printf("],\"equipment_records\":\"");
    const u8 *equipment = (const u8 *)g_SaveRuntimeState.player.runtime.inventory.equipment;
    for (unsigned byte = 0; byte < sizeof(g_SaveRuntimeState.player.runtime.inventory.equipment); ++byte) {
        printf("%02x", (unsigned)equipment[byte]);
    }
    printf("\",\"special_records\":\"");

    /* The nine special records include reserve ammunition. The preceding
     * fallback record overlaps the transient packed-list workspace. */
    const u8 *special = (const u8 *)g_KeyItemRecords;
    for (unsigned byte = 0; byte < 9 * sizeof(*g_KeyItemRecords); ++byte) {
        printf("%02x", (unsigned)special[byte]);
    }
    printf("\",\"category_counts\":[");
    for (unsigned i = 0; i < 6; ++i) {
        printf("%s%u", i ? "," : "", (unsigned)g_SaveRuntimeState.categoryCounts[i]);
    }
    printf("],\"storage\":[");
    for (unsigned i = 0; i < 100; ++i) {
        printf("%s%u", i ? "," : "", (unsigned)(u16)g_InventoryRuntime.storage[i]);
    }
    printf("],\"special_storage\":[");
    for (unsigned i = 0; i < 82; ++i) {
        printf("%s%u", i ? "," : "", (unsigned)(u16)g_InventoryRuntime.specialStorage[i]);
    }
    printf(
        "],\"weapon_slot\":%d,\"armor_slot\":%d},\"character\":{\"level_index\":%u,\"level\":%u,"
        "\"total_exp\":%u,\"saved_hp\":%u,\"saved_max_hp\":%u,\"bonus_points\":%u,\"new_game_count\":%u,"
        "\"spell_flags\":%u,\"pe_bonus_pool\":%u,\"stat_allocation\":[",
        g_AyaSaveState.equipped_weapon_slot,
        g_AyaSaveState.equipped_armor_slot,
        (unsigned)g_AyaSaveState.level,
        (unsigned)g_AyaSaveState.level + 1,
        g_AyaSaveState.total_exp,
        (unsigned)g_AyaSaveState.current_hp,
        (unsigned)g_AyaSaveState.max_hp,
        g_AyaSaveState.bonus_points,
        (unsigned)g_AyaSaveState.new_game_count,
        g_AyaSaveState.parasite_spell_flags,
        (unsigned)g_AyaSaveState.pe_bonus_pool
    );
    for (unsigned i = 0; i < 7; ++i) {
        printf("%s%u", i ? "," : "", (unsigned)g_AyaSaveState.stats.levels[i]);
    }
    printf("]},\"story_stage\":%u,\"timers\":[", g_ScriptVariableBank[74]);
    for (unsigned timer = 0; timer < 4; ++timer) {
        printf(
            "%s{\"flags\":%u,\"current\":%d,\"limit\":%d}",
            timer ? "," : "",
            g_GameTimers[timer].flags,
            g_GameTimers[timer].current,
            g_GameTimers[timer].limit
        );
    }
    printf("],\"script_variables\":[");
    for (unsigned variable = 0; variable < 0x200; ++variable) {
        printf("%s%u", variable ? "," : "", g_ScriptVariableBank[variable]);
    }
    printf("]");

    /* The shuffled room selector can change without changing the scene
     * token. Expose its table at the same read-only diagnostic boundary. */
    printf(
        ",\"glyph_selection\":{\"active\":%u,\"code_index\":%u,\"code\":%u,\"selected\":%u,"
        "\"load_failed\":%u,\"seed\":%u,\"table\":",
        (unsigned)g_FontSelectionState.active,
        (unsigned)g_FontSelectionState.codeIndex,
        (unsigned)g_FontSelectionState.code,
        (unsigned)g_FontSelectionState.selected,
        (unsigned)g_FontSelectionState.loadFailed,
        g_FontSelectionState.seed
    );
    if (g_FontSelectionState.active &&
        g_FontSelectionState.table &&
        g_FontSelectionState.table->slots.count <= 100) {
        const FontGlyphTable *table = g_FontSelectionState.table;
        printf("{\"count\":%u,\"codes\":[", (unsigned)table->slots.count);
        for (unsigned i = 0; i < 24; ++i) {
            printf("%s%u", i ? "," : "", (unsigned)table->groups.codes[i]);
        }
        printf("],\"indices\":[");
        for (unsigned i = 0; i < table->slots.count; ++i) {
            printf("%s%u", i ? "," : "", (unsigned)table->slots.indices[i]);
        }
        printf("],\"keys\":[");
        for (unsigned i = 0; i < table->slots.count; ++i) {
            printf("%s%u", i ? "," : "", (unsigned)table->slots.groupKeys[i]);
        }
        printf("]}");
    } else {
        printf("null");
    }
    printf("}");

    /* Observe live combat resources at the diagnostic boundary. Keep this
     * separate from the existing summary, including inactive scene actors. */
    printf(",\"battle_detail\":{\"frame\":%d,\"rng_frame\":%u,\"player\":", g_BattleFrameCounter, g_FrameRngCounter);
    if ((g_GameStateFlags & 2) && g_PlayerEntity && g_PlayerEntity->state) {
        const Combatant *player = (const Combatant *)g_PlayerEntity->state;
        printf(
            "{\"pe_raw\":%d,\"pe_max_raw\":%d,\"atb\":%u,\"state_flags\":%u,\"ammo\":",
            player->currentPe,
            player->maxPe,
            (unsigned)player->atb,
            (u32)player->stateFlags
        );
        if (player->action) {
            printf("%u", player->action->attackWord & 0x3FFu);
        } else {
            printf("null");
        }
        printf(
            ",\"pe_recovery_step\":%d,\"pe_recovery_divisor\":%d,\"pe_full_guard_frames\":%d,\"action_mode\":%u}",
            player->peRecoveryStep,
            player->peRecoveryDivisor,
            player->peFullGuardFrames,
            (unsigned)g_PlayerEntity->mode
        );
    } else {
        printf("null");
    }
    printf(",\"enemies\":[");
    unsigned combatant_index = 0;
    if (g_GameStateFlags & 2) {
        for (const FieldActor *actor = g_FieldActorListHead; actor; actor = actor->next) {
            if (actor == g_PlayerEntity || !actor->state) {
                continue;
            }
            const EnemyCombatant *enemy = (const EnemyCombatant *)actor->state;
            printf(
                "%s{\"type\":%u,\"sub\":%u,\"mode\":%u,\"hp\":%d,\"hp_limit\":%d,\"defense\":%u,"
                "\"core_flags\":%u,\"state_flags\":%u,\"status_flags\":%u,\"atb\":%u,\"atb_step\":%u}",
                combatant_index++ ? "," : "",
                actor->type_id,
                actor->sub_id,
                actor->mode,
                enemy->hp,
                enemy->maxHp,
                (unsigned)enemy->defense,
                enemy->coreFlags,
                enemy->stateFlags,
                enemy->statusFlags2,
                (unsigned)enemy->atbCharge,
                (unsigned)enemy->chargeStep
            );
        }
    }
    printf("]}");

    if (g_BootHostControlAt) {
        printf(
            ",\"control\":{\"item_state\":%d,\"target_cycle\":%d,\"target_index\":%d,\"turn_slot\":%d,"
            "\"remaining_actions\":%u,\"queue_count\":%u,\"range\":",
            g_BattleItemMenuState,
            g_BattleTargetCycleMode,
            g_BattleTargetIndex,
            g_BattleActiveTurnSlot,
            (unsigned)g_BattleRemainingActions,
            (unsigned)g_BattleActionQueueCount
        );
        const Combatant *player = (g_GameStateFlags & 2) && g_PlayerEntity
            ? (const Combatant *)g_PlayerEntity->state
            : NULL;
        if (player && player->action) {
            printf("%d", (int)player->action->range);
        } else {
            printf("null");
        }
        printf(",\"bonus_points\":%u,\"targets\":[", (unsigned)g_AyaSaveState.bonus_points);
        int count = (g_GameStateFlags & 2) ? g_BattleTargetCount : 0;
        if (count < 0) {
            count = 0;
        }
        if (count > BATTLE_TARGET_CAPACITY) {
            count = BATTLE_TARGET_CAPACITY;
        }
        for (int i = 0; i < count; ++i) {
            const FieldActor *target = (const FieldActor *)g_BattleTargetList[i].actor;
            const FieldActor *found = NULL;
            for (const FieldActor *actor = g_FieldActorListHead; actor; actor = actor->next) {
                if (actor == target) {
                    found = actor;
                    break;
                }
            }
            if (i) {
                printf(",");
            }
            if (found) {
                const BattleEntity *aim = g_BattleTargetList[i].actor;
                /* The original targeting code uses this rendered aim point,
                 * which can differ from the actor origin for articulated bosses. */
                printf(
                    "{\"type\":%u,\"sub\":%u,\"distance\":%d,\"angle\":%d,\"aim_position\":[%d,%d,%d]}",
                    found->type_id,
                    found->sub_id,
                    g_BattleTargetList[i].dist,
                    g_BattleTargetList[i].angle,
                    aim->renderObject.target_x,
                    aim->renderObject.target_y,
                    aim->renderObject.target_z
                );
            } else {
                printf("null");
            }
        }
        printf("]}");
    }

    if (g_BootHostHealthEventCount) {
        printf(
            ",\"coverage_only\":true,\"ordinary_campaign_proof\":false,"
            "\"diagnostic_hp\":{\"scheduled\":%u,\"applied\":%u}",
            g_BootHostHealthEventCount,
            g_BootHostHealthApplied
        );
    }
    printf("}\n");
    fflush(stdout);
}

/* Read-only editor capture of the original posed model, not a replacement
 * actor or gameplay renderer. Bone matrices are already in room coordinates. */
static int Boot_ExportLightingModel(const char *path) {
    FieldActor *aya=g_PlayerEntity;
    if(!aya || !aya->render_object.header || !aya->render_object.matrices)return 0;
    RenderObjectEntity *obj=&aya->render_object;
    if(!obj->parts||!obj->vertices||!obj->vertex_colours||!obj->primitive_descriptors||!obj->primitive_buffer||!obj->header->part_count)return 0;
    unsigned count=obj->header->vertex_count;
    if(!count || count>8192)return 0;
    const unsigned char *known;
    const unsigned short *vram=Psyz_GpuGetVramCopy(&known);
    if(!vram)return 0;
    FILE *file=fopen(path,"w");if(!file)return 0;
    double origin[]={aya->pos_x/65536.0,aya->pos_y/65536.0,aya->pos_z/65536.0};
    fprintf(file,"{\"format\":1,\"origin\":[%.8g,%.8g,%.8g],\"vertices\":[",origin[0],origin[1],origin[2]);
    for(unsigned index=0;index<count;++index){
        unsigned bone=0;
        for(unsigned b=0;b<obj->header->part_count;++b)
            if(index>=obj->parts[b].vertex_start && index<obj->parts[b].vertex_start+obj->parts[b].vertex_count){bone=b;break;}
        RenderMatrix *matrix=obj->matrices+bone;
        RenderVec3s *vertex=obj->vertices+index;
        const GteShortVector *normal=vertex->pad>=0 && vertex->pad<240?(const GteShortVector *)&g_RenderNormalTable[vertex->pad]:NULL;
        double position[3],direction[3];
        for(unsigned r=0;r<3;++r){
            position[r]=matrix->translation[r]-origin[r]+(matrix->rotation[r][0]*(double)vertex->x+matrix->rotation[r][1]*(double)vertex->y+matrix->rotation[r][2]*(double)vertex->z)/4096.0;
            direction[r]=normal?(matrix->rotation[r][0]*(double)normal->x+matrix->rotation[r][1]*(double)normal->y+matrix->rotation[r][2]*(double)normal->z)/16777216.0:(r==1?-1.0:0.0);
        }
        fprintf(file,"%s[%.8g,%.8g,%.8g,%.8g,%.8g,%.8g]",index?",":"",position[0],position[1],position[2],direction[0],direction[1],direction[2]);
    }
    fprintf(file,"],\"faces\":[");
    unsigned char *packet=obj->primitive_buffer;
    const unsigned counts[]={obj->header->packet34_count,obj->header->packet28_count,obj->header->packet24_count,obj->header->packet1c_count};
    const size_t sizes[]={sizeof(RenderPacket34),sizeof(RenderPacket28),sizeof(RenderPacket24),sizeof(RenderPacket1C)};
    RenderPrimitiveDescriptor *desc=obj->primitive_descriptors;unsigned total=0;
    for(unsigned kind=0;kind<4;++kind)for(unsigned face=0;face<counts[kind];++face,++desc,packet+=2*sizes[kind]){
        unsigned vertices=(kind==0||kind==2)?4:3;
        int valid=1;for(unsigned v=0;v<vertices;++v){
            unsigned index=desc->lookup_indices[v];int visible=0;
            if(index>=count){valid=0;continue;}
            for(unsigned b=0;b<obj->header->part_count;++b)
                if(index>=obj->parts[b].vertex_start && index<obj->parts[b].vertex_start+obj->parts[b].vertex_count && obj->parts[b].visible==1)visible=1;
            if(!visible)valid=0;
        }
        if(!valid)continue;
        RenderPacket34 *poly=(void *)packet;
        fprintf(file,"%s{\"indices\":[",total++?",":"");
        for(unsigned v=0;v<vertices;++v)fprintf(file,"%s%u",v?",":"",desc->lookup_indices[v]);
        fprintf(file,"],\"uv\":[");
        for(unsigned v=0;v<vertices;++v){const unsigned char *uv=(const unsigned char *)&poly->u0+v*12;
            fprintf(file,"%s[%u,%u]",v?",":"",kind<2?uv[0]:0,kind<2?uv[1]:0);}
        fprintf(file,"],\"page\":%d,\"clut\":%u,\"colors\":[",kind<2?poly->page_bits:-1,kind<2?poly->clut:0);
        for(unsigned v=0;v<vertices;++v){u32 color=obj->vertex_colours[desc->lookup_indices[v]];
            fprintf(file,"%s[%u,%u,%u]",v?",":"",color&255,(color>>8)&255,(color>>16)&255);}
        fprintf(file,"]}");
    }
    fprintf(file,"]}\n");int success=!ferror(file);if(fclose(file))success=0;
    char raw[512];int n=snprintf(raw,sizeof(raw),"%s.vram",path);
    if(n<0||(size_t)n>=sizeof(raw))return 0;
    file=fopen(raw,"wb");if(!file)return 0;
    if(fwrite(vram,2,1024*512,file)!=1024*512)success=0;
    if(fclose(file))success=0;
    return success;
}

static void Boot_ReadHostControlCommand(void) {
    char line[1024];
    char *end = NULL;
    char *mask_end = NULL;
    size_t length = 0;

    for (;;) {
        errno = 0;
        int byte = fgetc(stdin);
        if (byte == EOF) {
            if (ferror(stdin)) {
                /* Attaching a debugger can interrupt the blocked read.
                 * Keep both the paused frame and any partial command. */
                if (errno == EINTR) {
                    clearerr(stdin);
                    continue;
                }
                fprintf(stderr, "Cannot read control command: %s\n", strerror(errno ? errno : EIO));
                fflush(stderr);
                _Exit(2);
            }
            if (!length) {
                BOOT_WRITE_PROFILE();
                _Exit(EXIT_SUCCESS);
            }
            break;
        }
        line[length++] = (char)byte;
        if (byte == '\n') {
            break;
        }
        if (length == sizeof(line) - 1) {
            fprintf(stderr, "Control command too long\n");
            fflush(stderr);
            _Exit(2);
        }
    }

    line[length] = '\0';
    if (!strchr(line, '\n') && !feof(stdin)) {
        fprintf(stderr, "Control command too long\n");
        fflush(stderr);
        _Exit(2);
    }
    line[strcspn(line, "\r\n")] = '\0';
    if(!strncmp(line,"lighting-export ",16)){
        if(!Boot_ExportLightingModel(line+16)){fprintf(stderr,"Lighting model export failed\n");fflush(stderr);_Exit(2);}
        g_BootHostControlAt=g_BootHostVBlankCount;
        return;
    }
    if (!strcmp(line, "quit")) {
        BOOT_WRITE_PROFILE();
        _Exit(EXIT_SUCCESS);
    }
    if (!strncmp(line,"backgrounds ",12)) {
        const char *backend=SDL_GetEnvironmentVariable(SDL_GetEnvironment(),"PSYZ_HEADLESS");
        if (!backend || strcmp(backend,"gpu") || !g_BootHostHdOptions ||
            (line[12]!='0' && line[12]!='1') || line[13]!=' ' || !line[14] ||
            g_BootHostVBlankLimit-g_BootHostVBlankCount<=8) {
            fprintf(stderr,"Invalid backgrounds control command\n");fflush(stderr);_Exit(2);
        }
        g_BootHostHdOptions(line[12]-'0',line+14);
        g_BootHostControlAt=g_BootHostVBlankCount+8;
        return;
    }
    if (!strncmp(line, "graphics ", 9)) {
        int geometry, antialiasing, filtering;
        char extra;
        const char *backend=SDL_GetEnvironmentVariable(SDL_GetEnvironment(), "PSYZ_HEADLESS");
        if (!backend || strcmp(backend, "gpu") || !g_BootHostGraphicsOptions ||
            sscanf(line + 9, "%d %d %d %c", &geometry, &antialiasing, &filtering, &extra) != 3 ||
            (unsigned)geometry > 1 || (unsigned)antialiasing > 1 || (unsigned)filtering > 1 ||
            g_BootHostVBlankLimit - g_BootHostVBlankCount <= 8) {
            fprintf(stderr, "Invalid graphics control command\n");
            fflush(stderr);
            _Exit(2);
        }
        g_BootHostGraphicsOptions(geometry, antialiasing, filtering);
        g_BootHostControlAt = g_BootHostVBlankCount + 8;
        return;
    }
    if (strncmp(line, "advance ", 8)) {
        fprintf(stderr, "Invalid control command\n");
        fflush(stderr);
        _Exit(2);
    }

    errno = 0;
    unsigned long frames = strtoul(line + 8, &end, 10);
    if (end == line + 8 || !isspace((unsigned char)*end)) {
        fprintf(stderr, "Invalid control duration\n");
        fflush(stderr);
        _Exit(2);
    }
    while (isspace((unsigned char)*end)) {
        ++end;
    }
    unsigned long buttons = strtoul(end, &mask_end, 0);
    if (mask_end == end) {
        fprintf(stderr, "Missing control buttons\n");
        fflush(stderr);
        _Exit(2);
    }
    while (isspace((unsigned char)*mask_end)) {
        ++mask_end;
    }
    if (errno || *mask_end || !frames || frames > g_BootHostVBlankLimit - g_BootHostVBlankCount || buttons > 65535) {
        fprintf(stderr, "Invalid control advance/buttons\n");
        fflush(stderr);
        _Exit(2);
    }
    hostButtons = (u16)buttons;
    g_BootHostControlAt = g_BootHostVBlankCount + (u32)frames;
}

static void Boot_TestCrash(void) {
    static int *volatile target;

    if (g_BootHostCrashAbort) {
        fprintf(stderr, "Test crash: abort at VBlank %u\n", (unsigned)g_BootHostVBlankCount);
        abort();
    }
    fprintf(stderr, "Test crash: write to address 0 at VBlank %u\n", (unsigned)g_BootHostVBlankCount);
    *target = 0;
}

static void Boot_PollHost(void) {
    ++g_BootHostVBlankCount;
    if (g_BootHostCrashAt && g_BootHostVBlankCount == g_BootHostCrashAt) {
        Boot_TestCrash();
    }
    Boot_ApplyScheduledPadAndHealthEvents();

    if (Psyz_QuitRequested()) {
        exit(EXIT_SUCCESS);
    }

    int terminal = g_BootHostVBlankLimit && g_BootHostVBlankCount >= g_BootHostVBlankLimit;
    if (!terminal && (!g_BootHostControlAt || g_BootHostVBlankCount < g_BootHostControlAt)) {
        return;
    }

    Boot_WriteHostCaptures();
    Boot_PrintHostDiagnosticJson(terminal);

    if (!terminal) {
        Boot_ReadHostControlCommand();
        return;
    }

    /* A diagnostic cap is a forced process boundary, even inside a menu's
     * VSync callback. Do not unwind through the locked SDK callback stack. */
    BOOT_WRITE_PROFILE();
    _Exit(EXIT_SUCCESS);
}

void Boot_InitSystem(void) {
    if (g_BootHostActive && g_BootHostAudioPath) {
        hostAudioFile = fopen(g_BootHostAudioPath, "wb");
        if (!hostAudioFile) {
            fprintf(stderr, "Cannot open audio capture\n");
            fflush(stderr);
            _Exit(2);
        }
        if (atexit(Boot_CloseHostAudioCapture)) {
            fclose(hostAudioFile);
            hostAudioFile = NULL;
            fprintf(stderr, "Cannot register audio capture cleanup\n");
            fflush(stderr);
            _Exit(2);
        }
        Psyz_AudioSetCaptureCallback(Boot_CaptureHostAudio, hostAudioFile);
    }
    ResetCallback();
    if (g_BootHostActive) {
        VSyncCallbacks(0, Boot_PollHost);
    }
    Render_ResetScene(0x140, 0xE0);
    SpuInit();
    if (g_BootHostActive && Psyz_AudioInit() < 0) {
        fprintf(stderr, "Cannot initialize host audio\n");
        fflush(stderr);
        _Exit(2);
    }
    InitGeom();
    SetGeomOffset(0xA0, 0x70);
    SetGeomScreen(0xF0);
    MemCard_InitManager();
    Boot_InitMemCard();
}

u32 Draw_FlushPrimQueue(void);
void Save_InitSystem(void);
void Script_InitNodePool(void);
void Entity_ResetStateGlobals(void);
void Entity_ResetAllPools(void);
void Menu_SetEquipSlotIndex(int index);
void Boot_BuildRenderFlagTable(void);
void Save_PostInitStub(void);
int CdRom_InitScreenState(void);

void Boot_InitSubsystems(void) {
    u32 var_s0;

    g_SceneDispatchCur = 0;
    g_SceneDispatchToken = 0;
    g_GameStateFlags = 0;
    g_FrameRngCounter = 0;
    g_ActiveDrawSlot = 0;
    Draw_InitGpuHwRegs();
    for (var_s0 = 0; var_s0 < 0x7D0U; var_s0++) {
        Draw_FlushPrimQueue();
    }
    Boot_BuildRenderFlagTable();
    GameTime_InitCounters();
    VSyncCallback(NULL);
    VSyncCallback(&Boot_VsyncCallback);
    Menu_SetEquipSlotIndex(0);
    Save_InitSystem();
    Save_PostInitStub();
    CdRom_InitScreenState();
    Script_InitNodePool();
    Entity_ResetStateGlobals();
    Entity_ResetAllPools();
    Script_ClearSfxTable();
    Menu_ConsumeEquipSlotFlag();
}

void ResetGraph(int arg0);
void SetGraphDebug(int level);
void ClearImage(RECT *rect, int r, int g, int b);

void Render_ResetScene(int width, int height) {
    RECT rect;

    ResetGraph(0);
    SetGraphDebug(0);
    SetDispMask(0);

    rect.w = 0x3FF;
    rect.h = 0x200;
    rect.x = 0;
    rect.y = 0;
    ClearImage(&rect, 0, 0, 1);

    g_RenderDispEnvArray[0].isrgb24 = 1;
    g_RenderDispEnvArray[1].isrgb24 = 1;
    SetDefDispEnv(&g_RenderDispEnvArray[0], 0, height, width, height);
    SetDefDispEnv(&g_RenderDispEnvArray[1], 0, 0, width, height);

    g_RenderDispEnvArray[1].screen.y = 8;
    g_RenderDispEnvArray[0].screen.y = 8;
    g_RenderDispEnvArray[1].screen.x = 0;
    g_RenderDispEnvArray[0].screen.x = 0;
    g_RenderDispEnvArray[0].screen.h = g_RenderDispEnvArray[1].screen.h = height;
    SetDefDrawEnv(&g_RenderDrawEnvArray[0], 0, 0, width, height);
    SetDefDrawEnv(&g_RenderDrawEnvArray[1], 0, height, width, height);

    g_RenderDrawEnvArray[1].isbg = 1;
    g_RenderDrawEnvArray[0].isbg = 1;
    g_RenderDrawEnvArray[1].dtd = 1;
    g_RenderDrawEnvArray[0].dtd = 1;
    g_RenderDrawEnvArray[1].dfe = 0;
    g_RenderDrawEnvArray[0].dfe = 0;
    g_RenderDrawEnvArray[1].tpage = 0;
    g_RenderDrawEnvArray[0].tpage = 0;
    g_RenderDrawEnvArray[1].r0 = 0;
    g_RenderDrawEnvArray[0].r0 = 0;
    g_RenderDrawEnvArray[1].g0 = 0;
    g_RenderDrawEnvArray[0].g0 = 0;
    g_RenderDrawEnvArray[1].b0 = 0;
    g_RenderDrawEnvArray[0].b0 = 0;
    g_ActiveDrawSlot = 0;

    PutDispEnv(&g_RenderDispEnvArray[0]);
}

void Scene_TickTimers(void);

void Boot_VsyncCallback(void) {
    Draw_FlushPrimQueue();
    Scene_TickTimers();
}

void Boot_InitMemCard(void) {
    Boot_ResetPadInput();
}

u32 g_FieldPadReleased;
u32 g_FieldPadPressed;
u32 g_FieldPadLastFrame;
u32 g_FieldPadBits;
u32 g_FieldPadBitsPrevious;

void Scene_CheckFlagBits(void *ctx, SceneFlagRecord **table, int *index_ptr) {
    SceneFlagRecord *record;
    u32 flags;
    u32 mask_copy;
    int triggered;

    record = table[*index_ptr];
    triggered = 0;
    if (record != 0) {
        flags = record->flags;
    } else {
        flags = 0;
    }

    if (flags != 0) {
        do {
            void (*callback)(struct BattleEntity *, int *);
            u32 mask;
            u32 value;

            callback = record->callback;
            if (callback == 0) {
                return;
            }

            if (flags & 0x20) {
                callback(ctx, index_ptr);
                triggered = 1;
            } else if (flags & 1) {
                value = g_FieldPadBits;
                mask = record->mask_all;
                if ((value & mask) == mask) {
                    if (flags & 4) {
                        if (g_FieldPadPressed & record->mask_any) {
                            callback(ctx, index_ptr);
                            triggered = 1;
                        }
                    } else if (flags & 8) {
                        if (g_FieldPadReleased & record->mask_any) {
                            callback(ctx, index_ptr);
                            triggered = 1;
                        }
                    } else if (flags & 2) {
                        if (value & record->mask_any) {
                            callback(ctx, index_ptr);
                            triggered = 1;
                        }
                    } else {
                        callback(ctx, index_ptr);
                        triggered = 1;
                    }
                }
            } else if (flags & 2) {
                u32 current;

                current = g_FieldPadBits;
                mask = record->mask_any;
                mask_copy = mask;
                if (current & mask_copy) {
                    if (flags & 4) {
                        if (g_FieldPadPressed & mask) {
                            callback(ctx, index_ptr);
                            triggered = 1;
                        }
                    } else if (flags & 8) {
                        if (g_FieldPadReleased & mask_copy) {
                            callback(ctx, index_ptr);
                            triggered = 1;
                        }
                    } else {
                        callback(ctx, index_ptr);
                        triggered = 1;
                    }
                }
            } else if (flags & 4) {
                if (g_FieldPadPressed & record->mask_any) {
                    callback(ctx, index_ptr);
                    triggered = 1;
                }
            } else if (flags & 8) {
                if (g_FieldPadReleased & record->mask_any) {
                    callback(ctx, index_ptr);
                    triggered = 1;
                }
            }

            if (((flags & 0x10) == 0) && (triggered != 0)) {
                return;
            }

            record++;
            flags = record->flags;
        } while (flags != 0);
    }

    if ((record != 0) && (record->callback != 0)) {
        record->callback(ctx, index_ptr);
    }
}

PadPacket g_PadPackets[2];
int g_SceneDispatchToken;
s32 g_FieldRenderFlagTable[32];
u32 g_FieldPadHeldFrameCounts[32];
u32 g_PadComboSequence[9] = {0x10000000, 0x20000000, 0x40000000, 0x40, 8, 0x10, 0x20, 2, 4};
extern u32 g_FieldPadBitsPrevious;

/* Field render-flag table setup and the helper that stores one table entry
 * at the bit index of a single-bit mask. The helper's only caller is the
 * table builder that precedes it (main_tu_evidence G0344). */

void Boot_StoreTableEntry(u32 mask, int value);

void Boot_BuildRenderFlagTable(void) {
    s32 *entry;
    u32 i;

    i = 0;
    entry = g_FieldRenderFlagTable;
    g_FieldPadReleased = 0;
    g_FieldPadPressed = 0;
    g_FieldPadBitsPrevious = 0;
    g_FieldPadLastFrame = 0;
    g_FieldPadBits = 0;
    do {
        *entry = 0;
        i += 1;
        entry += 1;
    } while (i < 0x20U);
    Boot_StoreTableEntry(1, 0x4000);
    Boot_StoreTableEntry(0x80, 0x1000);
    Boot_StoreTableEntry(0x100, 0x2000);
    Boot_StoreTableEntry(8, 0x10);
    Boot_StoreTableEntry(0x20, 0x40);
    Boot_StoreTableEntry(0x40, 0x80);
    Boot_StoreTableEntry(0x10, 0x20);
    Boot_StoreTableEntry(2, 1);
    Boot_StoreTableEntry(4, 8);
    Boot_StoreTableEntry(0x200, 0x2000);
    Boot_StoreTableEntry(0x400, 0x4000);
    Boot_StoreTableEntry(0x2000, 0x8000);
    Boot_StoreTableEntry(0x01000000, 0x100);
    Boot_StoreTableEntry(0x02000000, 0x200);
    Boot_StoreTableEntry(0x04000000, 0x400);
    Boot_StoreTableEntry(0x08000000, 0x800);
    Boot_StoreTableEntry(0x10000000, 0x1000);
    Boot_StoreTableEntry(0x20000000, 0x2000);
    Boot_StoreTableEntry(0x40000000, 0x4000);
    Boot_StoreTableEntry(0x80000000, 0x8000);
    g_GameStateFlags = g_GameStateFlags | 0x4000;
}

void Boot_StoreTableEntry(u32 mask, int value) {
    int index;

    gte_ldlzcs(mask);
    index = 31;
    if (mask != 0x80000000) {
        gte_stlzcr(&mask);
        index -= mask;
    }
    g_FieldRenderFlagTable[index] = value;
}

u32 g_PadComboProgress;
unsigned g_FieldAnalogRunThreshold = 85;

static void Boot_PollConfiguredPad(void);
static bool native_pad_poll;

void Field_HandleStateTransition(void) {
    if (g_BootHostActive && native_pad_poll) {
        Boot_PollConfiguredPad();
    }
    /* Apply scheduled host input after SDK polling, before the unchanged
     * original button mapping and repeat/combo handling below. */
    if (g_BootHostActive && g_BootHostPadEventCount) {
        g_PadPackets[0].header = 0x4000;
        g_PadPackets[0].buttons = (u16)~hostButtons;
    }
    PE_HOOK(PE_HOOK_PAD_READ, NULL);

    u32 flags;
    u32 pad_bits;
    u32 base_bits;
    u32 old_pad_bits;
    u32 pad_mask;
    u32 active_pad;
    u32 save_pad;
    u32 final_pad;
    u32 final_old;
    u32 changed;
    u32 mask;
    u32 rawInverted;
    u32 sequence_mask;
    u32 menu_open;
    u32 pad;
    u32 inverted;
    int mode;
    u16 i;
    u8 analog;

    if ((g_PadPackets[0].header & 0xFF) != 0) {
        u32 initial_flags = g_GameStateFlags;
        if ((initial_flags & 0x4001) == 0) {
            g_FieldPadPressed = 4;
            g_FieldPadBits = 4;
            g_FieldPadReleased = 0;
            g_GameStateFlags = initial_flags | 0x4000;
            return;
        }
        g_GameStateFlags = initial_flags | 0x4000;
        g_FieldPadBits = g_FieldPadPressed = g_FieldPadReleased = 0;
        return;
    }

    mode = g_PadPackets[0].header & 0xF000;
    if (!((mode != 0x4000) && (mode != 0x7000))) {
        /* The host supplies a ready packet; PS1 actuator/mode negotiation is
         * unnecessary. A nonzero receive-status byte means disconnected. */
        if ((g_PadPackets[0].header & 0xFF) == 0) {
            g_GameStateFlags &= ~0x4000;
        }

        pad = g_PadPackets[0].buttons;
        rawInverted = ~pad;
        pad_mask = 0xFFFF9FFF;
        base_bits = rawInverted & pad_mask;
        old_pad_bits = g_FieldPadBits;
        g_FieldPadLastFrame = old_pad_bits;
        inverted = ~(u16)pad;
        pad_bits = base_bits;
        if (inverted & 0x2000) {
            pad_bits = base_bits | 0x4000;
        }
        if (inverted & 0x4000) {
            pad_bits |= 0x2000;
        }

        int run_mask = g_FieldRenderFlagTable[0];
        PE_HOOK(PE_HOOK_PAD_MAP, (&(PeHookPadMap){pad_bits, &run_mask}));
        g_FieldPadBits = 0;
        /* Decoded input is saved in pad_bits; reuse the scratch for key indices. */
        for (i = 0; i < 0x20; i++) {
            base_bits = i;
            if (((u16)pad_bits & (base_bits == 0 ? run_mask : g_FieldRenderFlagTable[base_bits])) != 0) {
                g_FieldPadBits |= 1u << base_bits;
                g_FieldPadHeldFrameCounts[base_bits]++;
            } else {
                g_FieldPadHeldFrameCounts[base_bits] = 0;
            }
        }

        flags = g_GameStateFlags;
        if ((flags & 1) != 0) {
            active_pad = g_FieldPadBits;
            if ((int)active_pad < 0) {
                mask = ((active_pad ^ g_FieldPadBitsPrevious) & active_pad) & 0x7000007E;
                if (mask != 0) {
                    sequence_mask = g_PadComboSequence[g_PadComboProgress];
                    if ((mask & sequence_mask) == sequence_mask) {
                        g_PadComboProgress++;
                        if (g_PadComboProgress == 9) {
                            g_SceneDispatchToken = 0xAA108448;
                            g_GameStateFlags = flags | 0x12000;
                        }
                    } else {
                        g_PadComboProgress = 0;
                    }
                }
            } else {
                g_PadComboProgress = 0;
            }
            save_pad = g_FieldPadBits;
            g_FieldPadBitsPrevious = save_pad;
        }

        menu_open = g_GameState.flags;
        if (menu_open & 0x400) {
            g_FieldPadBits &= 0x40FFDC7F;
        }
        if (menu_open & 0x200) {
            g_FieldPadBits &= 0x60FFDB04;
            if (g_GameState.scene_audio.tracks.movie_index != 1) {
                g_FieldPadBits &= 0xDFFFFCFF;
            }
        }
        if (g_FieldPadBits & 0x10000080) {
            g_FieldPadBits &= 0x7FFFFFFF;
        }
        if (g_FieldPadBits & 0x40000401) {
            g_FieldPadBits &= 0xEFFFFF7F;
            g_FieldPadBits &= 0x7FFFFFFF;
        }
        if (g_FieldPadBits & 0x20000300) {
            g_FieldPadBits &= 0xBFFFFBFE;
            g_FieldPadBits &= 0xEFFFFF7F;
            g_FieldPadBits &= 0x7FFFFFFF;
        }

        if ((g_PadPackets[0].header & 0xF000) == 0x7000) {
            g_FieldPadBits &= ~0x79;
            menu_open = MenuWidget_FindByModeAndSelectedBase(1, 0) != 0;

            if (menu_open) {
                analog = g_PadPackets[0].leftY;
                if (analog < 0x14) {
                    g_FieldPadBits |= 0x8;
                } else if (analog >= 0xE7) {
                    g_FieldPadBits |= 0x20;
                }

                analog = g_PadPackets[0].leftX;
                if (analog < 0x14) {
                    g_FieldPadBits |= 0x40;
                } else if (analog >= 0xE7) {
                    g_FieldPadBits |= 0x10;
                }
            } else {
                analog = g_PadPackets[0].leftY;
                if (analog < 0x5A) {
                    g_FieldPadBits |= 0x8;
                    if (analog < 0x14) {
                        g_FieldPadBits |= 0x1;
                    }
                } else if (analog >= 0xA1) {
                    g_FieldPadBits |= 0x20;
                    if (analog >= 0xE7) {
                        g_FieldPadBits |= 0x1;
                    }
                }

                analog = g_PadPackets[0].leftX;
                if (analog < 0x5A) {
                    g_FieldPadBits |= 0x40;
                    if (analog < 0x14) {
                        g_FieldPadBits |= 0x1;
                    }
                } else if (analog >= 0xA1) {
                    g_FieldPadBits |= 0x10;
                    if (analog >= 0xE7) {
                        g_FieldPadBits |= 0x1;
                    }
                }
            }
        }

        /* Keep original menu/battle rules; field running follows radial stick
         * magnitude, independent of facing direction or diagonal movement. */
        if ((g_PadPackets[0].header & 0xF000) == 0x7000 &&
            !(g_GameStateFlags & 2) && !menu_open) {
            int x = (int)g_PadPackets[0].leftX - 128;
            int y = (int)g_PadPackets[0].leftY - 128;
            if (x < -127) x = -127;
            if (y < -127) y = -127;
            unsigned radius2 = (unsigned)(x*x + y*y);
            unsigned threshold = g_FieldAnalogRunThreshold;
            g_FieldPadBits &= ~1u;
            if ((g_FieldPadBits & 0x78) &&
                radius2 * 10000u >= 127u * 127u * threshold * threshold) {
                g_FieldPadBits |= 1;
            }
        }

        final_pad = g_FieldPadBits;
        final_old = g_FieldPadLastFrame;
        changed = final_pad ^ final_old;
        g_FieldPadPressed = changed & final_pad;
        g_FieldPadReleased = changed & final_old;
        return;
    }

    g_FieldPadBits = 0;
    g_FieldPadPressed = 0;
    g_FieldPadReleased = 0;
}

/* Host keyboard input feeds the original controller packet decoder. */
static bool held[SDL_SCANCODE_COUNT];
static bool focused;

static void publish(void) {
    unsigned buttons = 0;
    if (focused) {
        for (unsigned i = 0; i < 16; ++i) {
            int key = Psyz_GetKeyboardBinding_SDL3(i);
            if (key > SDL_SCANCODE_UNKNOWN && key < SDL_SCANCODE_COUNT && held[key]) {
                buttons |= 1u << ((i + 8) & 15); /* SDK PAD bits -> wire packet bytes */
            }
        }
        /* Preserve the desktop movement/action aliases only while their
         * canonical action is unchanged and the alias is not explicitly bound. */
        static const struct {
            unsigned button;
            SDL_Scancode canonical;
            SDL_Scancode alias;
        } aliases[] = {
            {12, SDL_SCANCODE_UP, SDL_SCANCODE_W},
            {13, SDL_SCANCODE_RIGHT, SDL_SCANCODE_D},
            {14, SDL_SCANCODE_DOWN, SDL_SCANCODE_S},
            {15, SDL_SCANCODE_LEFT, SDL_SCANCODE_A},
            {5, SDL_SCANCODE_Z, SDL_SCANCODE_LSHIFT},
            {5, SDL_SCANCODE_Z, SDL_SCANCODE_RSHIFT},
            {6, SDL_SCANCODE_X, SDL_SCANCODE_SPACE},
            {11, SDL_SCANCODE_RETURN, SDL_SCANCODE_KP_ENTER}
        };
        for (unsigned i = 0; i < sizeof(aliases) / sizeof(aliases[0]); ++i) {
            if (!held[aliases[i].alias] ||
                Psyz_GetKeyboardBinding_SDL3(aliases[i].button) != aliases[i].canonical) {
                continue;
            }
            int assigned = 0;
            for (unsigned j = 0; j < 16; ++j) {
                if (Psyz_GetKeyboardBinding_SDL3(j) == aliases[i].alias) {
                    assigned = 1;
                }
            }
            if (!assigned) {
                buttons |= 1u << ((aliases[i].button + 8) & 15);
            }
        }
    }
    g_PadPackets[0].header = 0x4100;
    g_PadPackets[0].buttons = (u16)~buttons;
    g_PadPackets[0].leftX = g_PadPackets[0].leftY = 128;
    g_PadPackets[0].rightX = g_PadPackets[0].rightY = 128;
}

static void Boot_PollConfiguredPad(void) {
    publish();
    if (!focused) {
        return;
    }
    unsigned char packet[PSYZ_PAD_BUF_LEN];
    Psyz_PadsGet(0, (char *)packet, sizeof(packet));
    if (packet[0] == 0 && (packet[1] == PSYZ_CTRL_DIGITAL_PAD ||
                           packet[1] == PSYZ_CTRL_ANALOG_PAD)) {
        g_PadPackets[0].buttons &= (u16)(packet[2] | ((u16)packet[3] << 8));
        /* Digital movement takes priority, so keyboard/D-pad input never
         * borrows a stale stick angle. Preserve the original stick dead zone. */
        if (packet[1] == PSYZ_CTRL_ANALOG_PAD &&
            !(~g_PadPackets[0].buttons & 0x00f0) &&
            (packet[6] < 0x5a || packet[6] >= 0xa1 ||
             packet[7] < 0x5a || packet[7] >= 0xa1)) {
            g_PadPackets[0].header = 0x7300;
            g_PadPackets[0].rightX = packet[4];
            g_PadPackets[0].rightY = packet[5];
            g_PadPackets[0].leftX = packet[6];
            g_PadPackets[0].leftY = packet[7];
        }
    }
}

static bool Boot_WatchPadEvent(void *userdata, SDL_Event *event) {
    (void)userdata;
    Boot_HandlePadEvent(event);
    return true;
}

void Boot_InitHostBindings(void) {
    static bool initialized;
    if (initialized) {
        return;
    }
    static const SDL_Scancode defaults[16] = {
        SDL_SCANCODE_1, SDL_SCANCODE_2, SDL_SCANCODE_Q, SDL_SCANCODE_E,
        SDL_SCANCODE_C, SDL_SCANCODE_Z, SDL_SCANCODE_X, SDL_SCANCODE_V,
        SDL_SCANCODE_TAB, SDL_SCANCODE_F1, SDL_SCANCODE_F2, SDL_SCANCODE_RETURN,
        SDL_SCANCODE_UP, SDL_SCANCODE_RIGHT, SDL_SCANCODE_DOWN, SDL_SCANCODE_LEFT
    };
    for (unsigned i = 0; i < 16; ++i) {
        Psyz_SetKeyboardBinding_SDL3(i, defaults[i]);
    }
    initialized = true;
}

void Boot_ResetPadInput(void) {
    Boot_InitHostBindings();
    const char *headless = SDL_GetEnvironmentVariable(SDL_GetEnvironment(), "PSYZ_HEADLESS");
    native_pad_poll = !headless || !*headless;
    if (g_BootHostActive && native_pad_poll) {
        PadInit(0);
        Psyz_PadsSetKind(0, 0, PSYZ_CTRL_ANALOG_PAD);
    }
    SDL_RemoveEventWatch(Boot_WatchPadEvent, NULL);
    SDL_AddEventWatch(Boot_WatchPadEvent, NULL);
    memset(held, 0, sizeof(held));
    focused = true;
    memset(g_PadPackets, 0, sizeof(g_PadPackets));
    g_PadPackets[1].header = 0x00ff;
    publish();
}

void Boot_HandlePadEvent(const SDL_Event *event) {
    if (event->type == SDL_EVENT_WINDOW_FOCUS_LOST || event->type == SDL_EVENT_QUIT) {
        focused = false;
        memset(held, 0, sizeof(held));
    } else if (event->type == SDL_EVENT_WINDOW_FOCUS_GAINED) {
        focused = true;
    } else if (focused && (event->type == SDL_EVENT_KEY_DOWN || event->type == SDL_EVENT_KEY_UP)) {
        SDL_Scancode key = event->key.scancode;
        if (key > SDL_SCANCODE_UNKNOWN && key < SDL_SCANCODE_COUNT) {
            held[key] = event->type == SDL_EVENT_KEY_DOWN;
        }
    }
    publish();
}
