#include <psyz/gpu.h>
#if PE_NATIVE_STATE_CPU_COMPONENTS
#include "pe_state_boundary.h"
#endif
#ifdef PE_PS5_GPU_NATIVE
#include "pe1/particle_augment.h"
#include "pe1/planar_shadow.h"
#endif
#include "common.h"
#include "pe1/akao_send.h"
#include "pe1/battle_runtime.h"
#include "pe1/battle_status.h"
#include "pe1/boot_disc_check.h"
#include "pe1/draw_state.h"
#include "pe1/field_movement.h"
#include "pe1/game_state.h"
#include "pe1/geom_state.h"
#include "pe1/gte.h"
#include "pe1/pad_state.h"
#include "pe1/menu_memcard_display.h"
#include "pe1/psyq_gpu.h"
#include "pe1/render_animation_frame.h"
#include "pe1/render_camera.h"
#include "pe1/render_lighting.h"
#include "pe1/render_tint.h"
#include "pe1/room_floor.h"
#include "pe1/scene_dispatch.h"
#include "pe1/scene_room_load.h"
#include "pe1/scene_transition.h"
#include "pe_hooks.h"

u32 g_FrameRngCounter;

/* Shared native state; external variables have one storage definition. */
extern int g_SceneDispatchToken;
int g_SceneDispatchCur;
u32 g_GlobalFrameCounter[1];
RenderLightingState g_RenderLightingBlock;

/* Field frame loop and its scene flush: Boot_FlushSceneFast is the exit tail
 * that Boot_RunFrame repeats after its loop. Contiguous -G8 pair. */

/* Late include: its g_GlobalFrameCounter[] would make the COMMON word above large. */
#include "pe1/boot.h"

void Field_HandleStateTransition(void);
void Scene_UpdateEntityList(void);
void Entity_FrameUpdate(void);
void Draw_ClearOnFlag(void);

void FieldFx_UpdateScriptSprites(void);
void Boot_FinishFrame(void);
void Akao_Cmd_F1(void);

int Asset_UnloadTableEntries(void);

void Boot_FlushSceneFast(void) {
    Psyz_GpuResetInterpolation();
    RECT rect;
    u32 status;
    u32 first_status;

    if (g_GameState.flags & 0x200) {
        rect.w = 0x140;
        rect.x = 0;
        rect.y = 0;
        rect.h = 0x1C0;
        ClearImage(&rect, 0, 0, 1);
    }
    DrawSync(0);
    Psyz_GpuClearInterpolationPackets();
    Akao_Cmd_F1();
    Render_Noop();
    Asset_UnloadTableEntries();
    status = g_GameState.flags | 2;
    g_GameStateFlags = (g_GameStateFlags | 0x40) & ~0x3800;
    first_status = status & ~0x800;
    g_GameState.flags = first_status;
    if (status & 0x200) {
        g_GameState.flags = (first_status | 2) & 0xFFFF7DFF;
    }
}

/* The frame loop reaches the game-state flags back from its scene flag byte. */
#define GAME_STATE_OF_SCENE_FLAGS(flagsByte) \
    ((Pe1GameState *)((flagsByte) - PE1_OFFSETOF(Pe1GameState, bank_state_12)))

/* Controller packet bit (active low) that asks to skip a story movie;
 * Enter is bound to Start by default. */
#define MOVIE_SKIP_START 0x0008u

int (*g_BootMovieSkipConfirm)(void);
void (*g_BootSceneStarted)(void);

/* The original story movie player has no skip button (the title movies stop
 * on Start in MenuCard_PlayVideo). A new Start press while one
 * plays asks the host, which keeps the movie paused until it answers. */
static int Boot_MovieSkipChosen(const Pe1GameState *state) {
    static u16 previous;
    u16 held = 0;
    int chosen = 0;

    if ((g_PadPackets[0].header & 0xFF) == 0) {
        held = (u16)~g_PadPackets[0].buttons & MOVIE_SKIP_START;
    }
    if ((held & ~previous) && !(g_GameStateFlags & 1) && !(state->flags & 0x100) &&
        g_DrawEnabled.bits >= 2 && Draw_CheckDrawStatus() != 0) {
        chosen = g_BootMovieSkipConfirm ? g_BootMovieSkipConfirm() : 1;
        /* The press only answers the question: no pause toggle, and the
         * buttons must be released before they ask again. */
        g_FieldPadPressed = 0;
        held = 0xFFFF;
    }
    previous = held;
    return chosen;
}

int Boot_RunFrame(void)
{
    RECT clear_rect;
    u32 flags;
    u32 status;
    Pe1GameState *state;
    u8 *scene_flags;
    RenderTintTargetView *fade;
    int skip_movie;

    int startup=Draw_InitPipeline();
    if (startup < 0) return startup;
    if (g_SceneDispatchCur == g_SceneDispatchToken) {
        scene_flags = &g_GameState.bank_state_12;
        state = &g_GameState;
        fade = (RenderTintTargetView *)&g_ScreenTint.targetR;
        PE_HOOK(PE_HOOK_SCENE_START, NULL);
        Psyz_GpuResetInterpolation();
#if PE_NATIVE_STATE_CPU_COMPONENTS
        PeStateBoundary_SceneChanged();
#endif
        if (g_BootSceneStarted) g_BootSceneStarted();
        do {
            /* Geometry belongs to this game frame, not to each VBlank.
             * Draw_RenderFrame waits after the OT has already been built. */
#if PE_NATIVE_STATE_CPU_COMPONENTS
            PeStateBoundary_FrameBegin();
#endif
            Psyz_PgxpNextFrame();
            g_DrawPacketOffset = 0;
#ifdef PE_PS5_GPU_NATIVE
            FieldFx_ParticleBeginFrame();PeShadow_BeginFrame();
#endif
            *scene_flags = 0;
            Field_HandleStateTransition();
            PE_HOOK(PE_HOOK_FRAME, NULL);
            flags = g_GameStateFlags;
            g_GameStateFlags = flags & ~0x30u;
            skip_movie = Boot_MovieSkipChosen(state);

            if (!(GAME_STATE_OF_SCENE_FLAGS(scene_flags)->flags & 0x8000) && g_GlobalFrameCounter[0] &&
                (g_FieldPadPressed & 4) &&
                !(g_FieldPadLastFrame & 0xB0002380u)) {
                g_GameStateFlags =
                    ((flags & 1) ? (flags & ~0x30u) | 0x20u
                                 : (flags & ~0x30u) | 0x10u) ^ 1u;
            }

            if (!(g_GameStateFlags & 1)) {
            if (!(state->flags & 0x200)) {
                ClearOTagR((RenderNativeTag *)state->shared_buffers.ordering_tables[g_ActiveDrawSlot],
                           0x1000);
            }
            g_FrameRngCounter = g_FrameRngCounter + 1;
            Scene_UpdateEntityList();
            Entity_FrameUpdate();

            if (!(state->flags & 0x100)) {
                if ((s8)Draw_CheckDrawStatus() != 0) {
                    s8 playing = (s8)MenuCardFirst_StepVideo();

                    /* A chosen skip ends the movie as its last frame would. */
                    if (playing && skip_movie) {
                        MenuCardFirst_StopVideo();
                        Script_StopMovieMusic();
                        playing = 0;
                    }
                    if (playing == 0) {
                        MenuCardFirst_ResetDisplayState();
                        Draw_ClearOnFlag();
                        continue;
                    }
                } else if (!(state->flags & 0x200)) {
                    Render_Update();
                    Menu_DrawTextboxEntries();
                    Render_SetGteScreenOffset();
                    FieldFx_UpdateScriptSprites();
                    Render_ResetGteScreenOffset();
                    Render_SetCDDCSlot();
                }
                Draw_RenderFrame();
                /* A finished fade to white reloads fade colour 15. */
                if (!g_GlobalFrameCounter[0] && fade->packed.red_green == 0xFF00FFu &&
                    fade->packed.blue == 0xFF) {
                    if (fade->colour.fadeMode & 0x40) {
                        Render_SetFadeColour(15);
                    }
                }
            }
            } else {
            VSync(2);
            }

            if (!(g_GameStateFlagsWord & 0x4200) &&
                (g_FieldPadBits & 0x0F000006u) == 0x0F000006u) {
                Boot_Shutdown();
            }
            if (!(state->flags & 0x100)) {
                Boot_FinishFrame();
            }
            g_GlobalFrameCounter[0]++;
            if ((g_GameStateFlags & 0x2000) && (state->flags & 0x800)) {
                break;
            }
loop_end_check:
#ifdef PE_PS5_GPU_NATIVE
            FieldFx_ParticleEndFrame();PeShadow_EndFrame();
#endif
#if PE_NATIVE_STATE_CPU_COMPONENTS
            /* Final CPU point: after scripts/actors/frame cleanup, never inside a VSync handler. */
            PeStateBoundary_Service(g_SceneDispatchCur==g_SceneDispatchToken &&
                !(g_GameStateFlags & 0x2000) && !(state->flags & 0x300) && Draw_CheckDrawStatus()==0);
            extern void CPE_StateUi_Poll(void);CPE_StateUi_Poll();
#endif
            ;
        } while (g_SceneDispatchCur == g_SceneDispatchToken);
    }

    if (g_GameStateFlagsWord & 0x200) {
        clear_rect.x = 0;
        clear_rect.y = 0;
        clear_rect.w = 0x140;
        clear_rect.h = 0x1C0;
        ClearImage(&clear_rect, 0, 0, 1);
    }
    DrawSync(0);
    Psyz_GpuClearInterpolationPackets();
    Akao_Cmd_F1();
    Render_Noop();
    Asset_UnloadTableEntries();
    g_GameStateFlags = (g_GameStateFlags | 0x40) & ~0x3800u;
    status = g_GameStateFlagsWord | 2;
    {
        u32 first_status = status & ~0x800u;
        g_GameStateFlagsWord = first_status;
        if (status & 0x200)
            g_GameStateFlagsWord = (first_status | 2u) & ~0x8200u;
    }
    return 0;
}

/* Load the GTE back colour (scaled by 16) and clear the nine-halfword
 * caller block that mirrors it. */
void Boot_SetBackColor(u16 *dst, int r, int g, int b) {
    gte_SetBackColor(r, g, b);

    dst[0] = 0;
    dst[1] = 0;
    dst[2] = 0;
    dst[3] = 0;
    dst[4] = 0;
    dst[5] = 0;
    dst[6] = 0;
    dst[7] = 0;
    dst[8] = 0;
}

/* Store one light's colour in the light colour matrix column and the 8-bit
 * colour table, then reload the GTE light colour matrix. */
void Boot_SetLightColor(RenderLightingState *lighting, int index, int r, int g, u16 b) {
    if ((unsigned int)index < 3) {
        lighting->color.matrix.m[0][index] = r;
        lighting->color.matrix.m[1][index] = g;
        lighting->color.matrix.m[2][index] = b;
    }

    lighting->light_colors[index][0] = r;
    lighting->light_colors[index][1] = g;
    lighting->light_colors[index][2] = b;

    gte_SetColorMatrix(&lighting->color);
}
