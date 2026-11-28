#include "common.h"
#include <psyz/gpu.h>
#include <psyz/video.h>
#include "pe1/render_packets.h"
#include "pe1/script_queue.h"

#include "fx_common.h"
#include "fx_common_motion.h"
#include "fx_common_render.h"

/* Original scene playback state, initialized by the overlay image and then
 * updated by InitScene/StepEffects. */
s8 D_8019C00D;
s16 D_8019C02E=1;
u8 g_FxCommonStartCameraPath=0x3e;
u8 g_FxCommonPlaying,g_FxCommonPlayAdvance,g_FxCommonReplayCamera;
s32 g_FxCommonPlayTicks,g_FxCommonPlayFrame;
FxCommonResourceState g_FxCommonResourceState={.active=-1,.current={.word=-1}};
s32 g_FxCommonPageChanged=1,g_FxCommonPageOpen;
s16 g_FxCommonExitRequested,g_FxCommonExitCountdown,g_FxCommonPageId;
s16 g_FxCommonScreenFade=-1,g_FxCommonDrawTintedCopies=1;
u16 g_FxCommonDemoFrame;
s32 g_FxCommonTailPathPosition,g_FxCommonFlyerPathPosition,g_FxCommonCameraPathPosition;
s32 g_FxCommonDemoTicks,g_FxCommonDemoExiting,g_FxCommonDemoResourceTicks;
s16 g_FxCommonDemoResource=-1,g_FxCommonInputLock,g_FxCommonTailViewEntered;
u8 g_FxCommonDemoShade,g_FxCommonDemoFadePhase,g_FxCommonResetRequested;
s32 g_FxCommonExitVariant,g_FxCommonHsyncCount;
s16 D_8019C0D0[2];

void FxCommon_MuteMusic(void)
{
    Akao_Cmd_A9(1, 0);
}

void FxCommon_FadeMusicOut(void)
{
    Akao_Cmd_A9(30, 0);
}

/* Choose the room to return to once the effect sequence ends, clear the
 * screen, fade the music out and hand control back to the scene loader. */
void FxCommon_EndSequence(void)
{
    FxCommonRect area;
    s32 room;
    u32 night;
    u32 band;
    u8 fade;
    int i;

    fade = 1;
    night = g_ScriptVariableBank[SCRIPT_VAR_FLAGS] & 0x2000;
    g_ScriptVariableBank[SCRIPT_VAR_ROOM] = 999;
    g_SceneDispatchToken = 0xA9400048;
    room = g_ScriptVariableBank[SCRIPT_VAR_AREA];
    if (room == 0x80) {
        g_SceneDispatchToken = 0xA80650C8;
    } else if (room == 0x78) {
        g_SceneDispatchToken = 0xA8003348;
        fade = 0;
    } else if (room == 0x208) {
        g_SceneDispatchToken = 0xA80032C8;
        fade = 0;
    } else if (room == 0x210) {
        if (night)
            g_SceneDispatchToken = 0xA8029148;
        else
            g_SceneDispatchToken = 0xA80290C8;
    } else {
        switch (g_FxCommonExitVariant) {
        case 0:
            g_SceneDispatchToken = 0xA80281C8;
            break;
        case 1:
            g_SceneDispatchToken = 0xA80260C8;
            break;
        case 2:
            g_SceneDispatchToken = 0xA8009348;
            break;
        case 3:
            g_SceneDispatchToken = 0xA8046048;
            break;
        case 4:
            if (room == 0x1C0) {
                g_SceneDispatchToken = 0xA80034C8;
                fade = 0;
            } else if (night) {
                g_SceneDispatchToken = 0xA80222C8;
            } else {
                g_SceneDispatchToken = 0xA80630C8;
            }
            break;
        case 5:
            g_SceneDispatchToken = 0xA8049048;
            break;
        case 6:
            g_SceneDispatchToken = 0xA8002248;
            break;
        case 7:
            if (room == 0xD0) {
                g_SceneDispatchToken = 0xA80033C8;
                fade = 0;
            } else if (room == 0x178) {
                g_SceneDispatchToken = 0xA80033C8;
                fade = 0;
            } else {
                band = room - 0x180;
                if (band < 0x88)
                    g_SceneDispatchToken = 0xA80201C8;
                else
                    g_SceneDispatchToken = 0xA8004348;
            }
            break;
        case 8:
            if (room == 0xE0) {
                g_SceneDispatchToken = 0xA80034C8;
                fade = 0;
            } else if (room >= 0x128) {
                g_SceneDispatchToken = 0xA8005448;
            } else {
                g_SceneDispatchToken = 0xA8067248;
            }
            break;
        case 9:
            if (room == 0xB8 || room == 0x148) {
                g_SceneDispatchToken = 0xA8003448;
                fade = 0;
            } else if (night) {
                g_SceneDispatchToken = 0xA8029148;
            } else {
                g_SceneDispatchToken = 0xA80290C8;
            }
            break;
        }
    }

    SetDispMask(0);
    ResetGraph(1);
    area.x = 0;
    area.y = 0;
    area.w = 0x140;
    area.h = 0x1C0;
    ClearImage(&area, 0, 0, 0);
    DrawSync(0);
    PutDispEnv(g_RenderDispEnvArray);
    if (fade == 1)
        Akao_Cmd_C1_WithSlot(g_SceneAudioSlot0Handle, 0x1E, 0);
    Akao_Cmd_A9(0x1E, 0);
    for (i = 0; i < 0x1E; i++)
        VSync(0);
    if (fade == 1) {
        g_SceneAudioSlot0Key = -1;
        g_GameState.scene_audio.tracks.banks[0] = -1;
        g_GameStateFlagsWord &= ~0x40;
        Akao_Cmd_F0();
    }
    Akao_Cmd_F1();
    Render_ConsumeFontShuffleSeed();
}

/* The effect overlay's own frame loop: run every effect system, submit the
 * ordering table, present the frame and flip the double buffer until the
 * scene asks to leave. */
int FxCommon_RunFrameLoop(void)
{
    FxCommonBuffer *next;
    int handle;
    u8 started;

    g_ActiveDrawSlot = 0;
    g_FxCommonResetRequested = 0;
    if(FxCommon_InitScene()<0)return -1;
    Psyz_GpuResetInterpolation();
    FxCommon_SetAkao3dVolumeRange(g_FxCommonViewMode != 10);
    srand(VSync(-1));
    started = 0;
    g_FxCommonFlyerPathPosition = rand() % 3000000;
    handle = 0;
    if (g_FxCommonViewMode == 10)
        g_FxCommonFlyerPathPosition = 120000;

    while (g_FxCommonExitRequested == 0) {
        Field_HandleStateTransition();
        if ((g_FieldPadBits & 0x0F000006) == 0x0F000006) {
            VSync(0);
            SetDispMask(0);
            g_FxCommonResetRequested = 1;
            g_FxCommonExitRequested = 1;
            Boot_Shutdown();
        }
        FxCommon_StepEffects();
        if(g_FxCommonActiveBuffer->error)return -2;
        if (g_FxCommonResetRequested != 0)
            continue;

        FxCommon_UpdateCameraMatrices();
        FxCommon_UpdateFrustumPlanes((void *)&g_FxCommonCameraPosition);
        if (g_FxCommonScreenFade != 0)
            g_FxCommonScreenFade = FxCommon_StepFadeIntensity(g_FxCommonScreenFade);
        FxCommon_DrawNightShadeAndMarkers();
        FxCommon_DrawSceneNodes();
        FxCommon_StepPathNodes();
        if(g_FxCommonActiveBuffer->error)return -2;
        if (!started) {
            handle = FxCommon_SendTableCommand3d(0xABE, g_FxCommonPathNodes[0]->matrix.t);
            started = 1;
        } else if (g_FxCommonDemoExiting == 0) {
            FxCommon_Apply3dPan(handle, g_FxCommonPathNodes[0]->matrix.t);
        }
        if (g_FxCommonViewMode == 10 || (g_FxCommonPlaying == 1 && g_FxCommonPlayAdvance == 0))
            Menu_DrawTextboxEntries();

        /* Native OT links retain empty entries; DMA run compression is unnecessary. */
        g_FxCommonHsyncCount = VSync(1);
        DrawSync(0);
        VSync(2);
        FxCommon_StepMotionBlend();
        ResetGraph(1);
        PutDrawEnv(&((FxCommonFrame *)g_FxCommonActiveBuffer)->drawEnv);
        PutDispEnv(&((FxCommonFrame *)g_FxCommonActiveBuffer)->dispEnv);
        /* This loop bypasses the field renderer's widescreen commit. Give
         * each city-map buffer the same centred framing before drawing it. */
        if (Psyz_VideoGetWidescreen())
            Psyz_VideoSetWidescreenPicture(((FxCommonFrame *)g_FxCommonActiveBuffer)->drawEnv.clip.y,
                                           -RENDER_WIDESCREEN_MARGIN, 320 + RENDER_WIDESCREEN_MARGIN);
        DrawOTag(&g_FxCommonActiveBuffer->allocation[0xFFF]);
        next = &g_FxCommonFrames[0].buffer;
        if (g_FxCommonActiveBuffer == next)
            next = &g_FxCommonFrames[1].buffer;
        g_FxCommonActiveBuffer = next;
        g_ActiveDrawSlot ^= 1;
        ClearOTagR(next->allocation, 0x1000);
        FxCommon_SelectBufferAllocation(g_FxCommonActiveBuffer);
    }
    if (g_FxCommonResetRequested == 0)
        FxCommon_EndSequence();
    return 0;
}

void FxCommon_DrawNightShadeAndMarkers(void)
{
    if (g_FxCommonNight == 1) {
        unsigned slot=g_FxCommonActiveBuffer==&g_FxCommonFrames[0].buffer?0:1;
        FxCommonGradientQuad *quad=&g_FxCommonShadeQuads[slot];
        RenderNativeTag *entry=&g_FxCommonActiveBuffer->allocation[4095];
        quad->tag.address=entry->address;
        entry->address=(uintptr_t)quad;
    }
    if (g_FxCommonMarkerFade != 0)
        g_FxCommonMarkerFade = FxCommon_DrawEffectMarkers(g_FxCommonMarkerFade);
}

#define DRAW(node, pass, force, mirrored) \
    FxCommon_DrawTransformNodeCulled((node), g_FxCommonActiveBuffer, (pass), (force), (mirrored))
#define DRAW_PAIR(node, pass, mode) \
    FxCommon_DrawLodNode((node), g_FxCommonActiveBuffer, (pass), (mode))

/* Draw every scene node for this frame, each with its depth bias: the
 * backdrop, the turning fans, the static props, the camera path nodes and
 * their echoes, the effect node pairs and the overlay nodes. */
void FxCommon_DrawSceneNodes(void)
{
    int i;

    if (g_FxCommonViewMode != 0) {
        g_FxCommonDepthBias = 0xFA1;
        DRAW(g_FxCommonType54Node, 1, 1, 0);
    }
    g_FxCommonDepthBias = 0xFA0;
    DRAW(g_FxCommonRuntime.node25, 1, 1, 0);
    g_FxCommonDepthBias = 0xF9F;
    DRAW(g_FxCommonType26Nodes[0], 1, 1, 0);
    DRAW(g_FxCommonType26Nodes[1], 1, 1, 0);
    DRAW(g_FxCommonType26Nodes[2], 1, 1, 0);
    DRAW(g_FxCommonType26Nodes[3], 1, 1, 0);
    g_FxCommonType26Nodes[0]->matrix.t[0] += 10;
    g_FxCommonType26Nodes[1]->matrix.t[0] += 2;
    g_FxCommonType26Nodes[2]->matrix.t[0] += 40;
    g_FxCommonType26Nodes[3]->matrix.t[0] += 30;
    if (g_FxCommonType26Nodes[0]->matrix.t[0] > 0x6EE0)
        g_FxCommonType26Nodes[0]->matrix.t[0] = -0x6328;
    if (g_FxCommonType26Nodes[1]->matrix.t[0] > 0x6EE0)
        g_FxCommonType26Nodes[1]->matrix.t[0] = -0x6328;
    if (g_FxCommonType26Nodes[2]->matrix.t[0] > 0x6EE0)
        g_FxCommonType26Nodes[2]->matrix.t[0] = -0x6328;
    if (g_FxCommonType26Nodes[3]->matrix.t[0] > 0x6EE0)
        g_FxCommonType26Nodes[3]->matrix.t[0] = -0x6328;

    g_FxCommonDepthBias = 1000;
    DRAW(g_FxCommonSceneNodes[1], 0, 1, 0);
    DRAW(g_FxCommonSceneNodes[3], 0, 1, 0);
    g_FxCommonDepthBias = 0xF3B;
    DRAW(g_FxCommonSceneNodes[0], 1, 1, 0);
    DRAW(g_FxCommonSceneNodes[2], 1, 1, 0);
    g_FxCommonDepthBias = 0xF3A;
    DRAW(g_FxCommonSceneNodes[4], 1, 1, 0);
    g_FxCommonDepthBias = 0xF39;
    DRAW(g_FxCommonSceneNodes[5], 1, 1, 0);
    g_FxCommonDepthBias = 0;
    DRAW(g_FxCommonSceneNodes[6], 0, 1, 0);
    DRAW(g_FxCommonSceneNodes[7], 0, 1, 0);
    DRAW(g_FxCommonSceneNodes[8], 0, 1, 0);
    DRAW(g_FxCommonSceneNodes[9], 0, 1, 0);
    DRAW(g_FxCommonSceneNodes[10], 0, 1, 0);
    DRAW(g_FxCommonType51Node, 0, 1, 0);

    if (g_FxCommonPathNodes[0]->kind == 0x24) {
        g_FxCommonPathNodes[0]->kind = 0x52;
        g_FxCommonPathNodes[1]->kind = 0x52;
        g_FxCommonPathNodes[2]->kind = 0x52;
        g_FxCommonPathNodes[3]->kind = 0x52;
    } else {
        g_FxCommonPathNodes[0]->kind = 0x24;
        g_FxCommonPathNodes[1]->kind = 0x24;
        g_FxCommonPathNodes[2]->kind = 0x24;
        g_FxCommonPathNodes[3]->kind = 0x24;
    }
    g_FxCommonDepthBias = 0;
    DRAW(g_FxCommonPathNodes[0], 0, 0, 0);
    if (g_FxCommonViewMode != 2)
        DRAW(g_FxCommonPathNodes[2], 0, 0, 0);
    g_FxCommonDepthBias = 1000;
    DRAW(g_FxCommonPathNodes[1], 0, 0, 1);
    if (g_FxCommonViewMode != 2)
        DRAW(g_FxCommonPathNodes[3], 0, 0, 1);

    g_FxCommonDepthBias = 0;
    DRAW_PAIR(g_FxCommonLodNodes[0], 1, 0);
    DRAW_PAIR(g_FxCommonPairedNodes[0], 1, 0);
    g_FxCommonDepthBias = 1000;
    DRAW_PAIR(g_FxCommonLodNodes[1], 1, 2);
    DRAW_PAIR(g_FxCommonPairedNodes[1], 1, 2);

    g_FxCommonDepthBias = 0;
    DRAW(g_FxCommonPathTailNodes[0], 0, 0, 0);
    DRAW(g_FxCommonPathTailNodes[1], 0, 0, 0);
    g_FxCommonDepthBias = 10;
    DRAW(g_FxCommonPathTailNodes[0], 0, 0, 1);
    DRAW(g_FxCommonPathTailNodes[1], 0, 0, 1);
    g_FxCommonDepthBias = 0;
    DRAW(g_FxCommonType21Nodes[0], 0, 1, 0);
    g_FxCommonDepthBias = 800;
    DRAW(g_FxCommonType21Nodes[1], 0, 1, 1);

    if (g_FxCommonNight == 1) {
        D_8019C0D0[0] += (s16)(rand() % 40 + 20);
        D_8019C0D0[1] += (s16)(rand() % 52 + 26);
        g_FxCommonEchoNodes[0]->matrix = g_FxCommonPathNodes[0]->matrix;
        g_FxCommonEchoNodes[0]->seed = g_FxCommonPathNodes[0]->seed;
        g_FxCommonEchoNodes[0]->kind = 0x53;
        g_FxCommonEchoNodes[2]->matrix = g_FxCommonPathNodes[2]->matrix;
        g_FxCommonEchoNodes[2]->seed = g_FxCommonPathNodes[2]->seed;
        g_FxCommonEchoNodes[2]->kind = 0x53;
        g_FxCommonEchoNodes[1]->matrix = g_FxCommonPathNodes[0]->matrix;
        g_FxCommonEchoNodes[1]->seed = g_FxCommonPathNodes[0]->seed;
        g_FxCommonEchoNodes[1]->kind = 0x53;
        g_FxCommonEchoNodes[3]->matrix = g_FxCommonPathNodes[2]->matrix;
        g_FxCommonEchoNodes[3]->seed = g_FxCommonPathNodes[2]->seed;
        g_FxCommonEchoNodes[3]->kind = 0x53;
        g_FxCommonDepthBias = 0;
        DRAW(g_FxCommonEchoNodes[0], 0, 0, 0);
        g_FxCommonDepthBias = 800;
        DRAW(g_FxCommonEchoNodes[1], 0, 0, 1);
    }

    for (i = 0; i < g_FxCommonRouteNodeCount >> 1; i++) {
        g_FxCommonDepthBias = 0;
        if (g_ScriptVariableBank[SCRIPT_VAR_FLAGS] & 0x2000) {
            g_FxCommonFlatColor[0] = 60;
            g_FxCommonFlatColor[1] = 60;
            g_FxCommonFlatColor[2] = 40;
        } else {
            g_FxCommonFlatColor[0] = 20;
            g_FxCommonFlatColor[1] = 40;
            g_FxCommonFlatColor[2] = 40;
        }
        DRAW_PAIR(g_FxCommonRouteNodes[i * 2], 0, 0);
        if (g_FxCommonDrawTintedCopies == 1 && (i & 3)) {
            g_FxCommonDepthBias = 800;
            DRAW_PAIR(g_FxCommonRouteNodes[i * 2 + 1], 0, 1);
        }
    }

    g_FxCommonDepthBias = 0;
    for (i = 0; i < 8; i++)
        DRAW_PAIR(g_FxCommonRuntime.pairedNodes[i], 0, 0);
    g_FxCommonDepthBias = 0x23A;
    DRAW_PAIR(g_FxCommonRuntime.pairedNodes[8], 0, 0);
    g_FxCommonDepthBias = 0;
    DRAW_PAIR(g_FxCommonRuntime.pairedNodes[9], 0, 0);
    if (g_FxCommonDrawTintedCopies == 1) {
        g_FxCommonDepthBias = 800;
        if (g_FxCommonPlaying) {
            DRAW_PAIR(g_FxCommonRuntime.pairedNodes[11], 0, 1);
            DRAW_PAIR(g_FxCommonRuntime.pairedNodes[12], 0, 1);
            DRAW_PAIR(g_FxCommonRuntime.pairedNodes[13], 0, 1);
            DRAW_PAIR(g_FxCommonRuntime.pairedNodes[14], 0, 1);
            DRAW_PAIR(g_FxCommonRuntime.pairedNodes[16], 0, 1);
            DRAW_PAIR(g_FxCommonRuntime.pairedNodes[17], 0, 1);
            DRAW_PAIR(g_FxCommonRuntime.pairedNodes[18], 0, 1);
            DRAW_PAIR(g_FxCommonRuntime.pairedNodes[19], 0, 1);
        }
        DRAW_PAIR(g_FxCommonRuntime.pairedNodes[15], 0, 1);
    }
    g_FxCommonDepthBase = 500;
    FxCommon_DrawTransformNode(g_FxCommonRuntime.node0, g_FxCommonActiveBuffer);
}

#undef DRAW
#undef DRAW_PAIR

/* Move the path-following nodes one step along their recorded paths, bank
 * the two flyers into their turns, and copy each flyer onto its shadow. */
void FxCommon_StepPathNodes(void)
{
    GteShortVector bank0;
    GteShortVector bank1;

    FxCommon_SamplePath(g_FxCommonTailPathPosition, 0x4A, Draw_GetTimTableEntry(g_FxCommonTimTable, 2),
                  (FxCommonVec3 *)g_FxCommonPathNodes[4]->matrix.t,
                  &g_FxCommonPathNodes[4]->seed.angles);
    FxCommon_SamplePath(g_FxCommonTailPathPosition, 0x4B, Draw_GetTimTableEntry(g_FxCommonTimTable, 2),
                  (FxCommonVec3 *)g_FxCommonPathNodes[5]->matrix.t,
                  &g_FxCommonPathNodes[5]->seed.angles);
    FxCommon_SamplePath(g_FxCommonFlyerPathPosition, 0x4C, Draw_GetTimTableEntry(g_FxCommonTimTable, 2),
                  (FxCommonVec3 *)g_FxCommonPathNodes[0]->matrix.t, &bank0);
    FxCommon_SamplePath(g_FxCommonFlyerPathPosition, 0x4D, Draw_GetTimTableEntry(g_FxCommonTimTable, 2),
                  (FxCommonVec3 *)g_FxCommonPathNodes[2]->matrix.t, &bank1);

    g_FxCommonPathNodes[0]->seed.component[0] = bank0.x;
    g_FxCommonPathNodes[0]->seed.component[1] = bank0.y;
    g_FxCommonPathNodes[0]->seed.component[2] += bank0.z;
    if (g_FxCommonPathNodes[0]->seed.component[2] > 0x200)
        g_FxCommonPathNodes[0]->seed.component[2] = 0x200;
    if (g_FxCommonPathNodes[0]->seed.component[2] < -0x200)
        g_FxCommonPathNodes[0]->seed.component[2] = -0x200;
    if (g_FxCommonPathNodes[0]->seed.component[2] > 0)
        g_FxCommonPathNodes[0]->seed.component[2] -= 8;
    if (g_FxCommonPathNodes[0]->seed.component[2] < 0)
        g_FxCommonPathNodes[0]->seed.component[2] += 8;

    g_FxCommonPathNodes[2]->seed.component[0] = bank1.x;
    g_FxCommonPathNodes[2]->seed.component[1] = bank1.y;
    g_FxCommonPathNodes[2]->seed.component[2] += bank1.z;
    if (g_FxCommonPathNodes[2]->seed.component[2] > 0x180)
        g_FxCommonPathNodes[2]->seed.component[2] = 0x180;
    if (g_FxCommonPathNodes[2]->seed.component[2] < -0x180)
        g_FxCommonPathNodes[2]->seed.component[2] = -0x180;
    if (g_FxCommonPathNodes[2]->seed.component[2] > 0)
        g_FxCommonPathNodes[0]->seed.component[2] -= 8;
    if (g_FxCommonPathNodes[2]->seed.component[2] < 0)
        g_FxCommonPathNodes[0]->seed.component[2] += 8;

    g_FxCommonPathNodes[4]->seed.component[1] -= 0x400;
    g_FxCommonPathNodes[5]->seed.component[1] -= 0x400;
    g_FxCommonPathNodes[0]->seed.component[1] -= 0x400;
    g_FxCommonPathNodes[2]->seed.component[1] -= 0x400;

    g_FxCommonPathNodes[1]->matrix = g_FxCommonPathNodes[0]->matrix;
    g_FxCommonPathNodes[1]->seed = g_FxCommonPathNodes[0]->seed;
    g_FxCommonPathNodes[3]->matrix = g_FxCommonPathNodes[2]->matrix;
    g_FxCommonPathNodes[3]->seed = g_FxCommonPathNodes[2]->seed;
    g_FxCommonTailPathPosition = (s32)((u32)g_FxCommonTailPathPosition + 4u);
    g_FxCommonFlyerPathPosition = (s32)((u32)g_FxCommonFlyerPathPosition + 0x10u);
}

void FxCommon_SnapMotionWhole(s16 base)
{
    FxCommonMotionRecord sample;
    GteShortVector extra;
    unsigned int i;

    g_FxCommonMotionBlendFrames = 0;
    for (i = 0; i < 10; i++) {
        FxCommon_SamplePath(i << 8, base + 0x40,
                      Draw_GetTimTableEntry(g_FxCommonTimTable, 2), &sample.vector, &extra);
        g_FxCommonMotionPositions[i].vector.x = (s32)((u32)sample.vector.x << 16);
        g_FxCommonMotionPositions[i].vector.y = (s32)((u32)sample.vector.y << 16);
        g_FxCommonMotionPositions[i].vector.z = (s32)((u32)sample.vector.z << 16);
    }
}

void FxCommon_BeginMotionBlend(s16 base)
{
    FxCommonMotionRecord sample;
    GteShortVector extra;
    unsigned int i;

    g_FxCommonMotionBlendFrames = 0x20;
    for (i = 0; i < 10; i++) {
        FxCommon_SamplePath(i << 8, base + 0x40,
                      Draw_GetTimTableEntry(g_FxCommonTimTable, 2), &sample.vector, &extra);
        g_FxCommonMotionDeltas[i].vector.x =
            (s32)(((u32)sample.vector.x << 16) - (u32)g_FxCommonMotionPositions[i].vector.x) >> 5;
        g_FxCommonMotionDeltas[i].vector.y =
            (s32)(((u32)sample.vector.y << 16) - (u32)g_FxCommonMotionPositions[i].vector.y) >> 5;
        g_FxCommonMotionDeltas[i].vector.z =
            (s32)(((u32)sample.vector.z << 16) - (u32)g_FxCommonMotionPositions[i].vector.z) >> 5;
    }
}

void FxCommon_StepMotionBlend(void)
{
    if(g_FxCommonMotionBlendFrames<=0)return;
    --g_FxCommonMotionBlendFrames;
    for(unsigned i=0;i<10;++i){
        g_FxCommonMotionPositions[i].vector.x=(s32)((u32)g_FxCommonMotionPositions[i].vector.x+(u32)g_FxCommonMotionDeltas[i].vector.x);
        g_FxCommonMotionPositions[i].vector.y=(s32)((u32)g_FxCommonMotionPositions[i].vector.y+(u32)g_FxCommonMotionDeltas[i].vector.y);
        g_FxCommonMotionPositions[i].vector.z=(s32)((u32)g_FxCommonMotionPositions[i].vector.z+(u32)g_FxCommonMotionDeltas[i].vector.z);
    }
}

#define FX_MODE_PACKET                                          \
    (g_FxCommonActiveBuffer == &g_FxCommonFrames[0].buffer ? &g_FxCommonDrawModes1[1] \
                                               : &g_FxCommonDrawModes1[0])

/* Fade the effect markers in or out by eight steps and draw, for every
 * enabled effect, a line from its projected position to a two-part label
 * (the selected effect uses the brighter palette). */
s16 FxCommon_DrawEffectMarkers(s16 value)
{
    s16 level;
    FxCommonDrawModePacket *mode;
    FxCommonBuffer *buffer;
    FxCommonBuffer *leftBuffer;
    FxCommonBuffer *rightBuffer;
    RenderNativeTag *ot;
    FxCommonLinePacket *line;
    FxCommonMarkerQuad *quad;
    GteShortVector rotation;
    GteShortVector position;
    GteShortVector unused={0};
    s32 screen0;
    s32 screen1;
    s32 screen2;
    s32 depth;
    s32 flag;
    u32 xy;
    u32 *vertex;
    u8 u;
    s16 x;
    s16 y;
    int i;

    level = 0;
    if (value > 0) {
        if (value < 0xFF)
            value += 8;
        if (value > 0xFF)
            value = 0xFF;
        level = value;
    }
    if (value < 0) {
        if (value >= -0xFE)
            value -= 8;
        if (value < -0xFF)
            value = -0xFF;
        level = value + 0xFF;
    }

    unsigned markerCount=0;
    if(level)for(unsigned marker=0;marker<10;++marker)if(g_FxCommonEffectSetup[marker].enabled==1)++markerCount;
    size_t required=markerCount*(sizeof(FxCommonLinePacket)+2*sizeof(FxCommonMarkerQuad));
    if(required && (!g_FxCommonActiveBuffer->end || (uintptr_t)g_FxCommonActiveBuffer->data>(uintptr_t)g_FxCommonActiveBuffer->end ||
       required>(uintptr_t)g_FxCommonActiveBuffer->end-(uintptr_t)g_FxCommonActiveBuffer->data)){
        g_FxCommonActiveBuffer->error=-1;return value;
    }

    mode = FX_MODE_PACKET;
    ot = &g_FxCommonActiveBuffer->allocation[10];
    mode->tag.address = ot->address;
    ot->address = (uintptr_t)mode;

    for (i = 0; i < 10; i++) {
        position.x = (s16)((u32)g_FxCommonMotionPositions[i].vector.x >> 16);
        position.y = (s16)((u32)g_FxCommonMotionPositions[i].vector.y >> 16);
        position.z = (s16)((u32)g_FxCommonMotionPositions[i].vector.z >> 16);
        if (g_FxCommonEffectSetup[i].enabled != 1 || level == 0)
            continue;

        rotation.x = g_FxCommonEffectSetup[i].position[0];
        rotation.y = g_FxCommonEffectSetup[i].position[1];
        rotation.z = g_FxCommonEffectSetup[i].position[2];
        RotTransPers3(&rotation, &position, &unused, &screen0, &screen1,
                      &screen2, &depth, &flag);

        line = (FxCommonLinePacket *)g_FxCommonActiveBuffer->data;
        line->tag.length = 3;
        line->code = 0x40;
        SetSemiTrans(line, 1);
        vertex = &line->xy0;
        *vertex++ = screen0;
        *vertex = screen1;
        if (g_FxCommonRoomVariant == i) {
            line->r = level >> 1;
            line->g = level >> 1;
            line->b = level >> 1;
        } else {
            line->r = level >> 3;
            line->g = level >> 3;
            line->b = level >> 3;
        }
        buffer = g_FxCommonActiveBuffer;
        line->tag.address = buffer->allocation[10].address;
        buffer->allocation[10].address = (uintptr_t)line;
        line++;
        buffer->data = (u8 *)line;
        quad = (FxCommonMarkerQuad *)buffer->data;

        xy = screen1;
        x = xy;
        y = xy >> 16;
        u = 0;
        SetPolyGT4(quad);
        SetSemiTrans(quad, 1);
        quad->r0 = level >> 1;
        quad->g0 = level >> 1;
        quad->b0 = level >> 1;
        quad->r1 = level >> 1;
        quad->g1 = level >> 1;
        quad->b1 = level >> 1;
        quad->r2 = level >> 1;
        quad->g2 = level >> 1;
        quad->b2 = level >> 1;
        quad->r3 = level >> 1;
        quad->g3 = level >> 1;
        quad->b3 = level >> 1;
        quad->x0 = x;
        quad->y0 = y;
        quad->x1 = x + 0x7F;
        quad->y1 = y;
        quad->x2 = x;
        quad->y2 = y + 12;
        quad->x3 = x + 0x7F;
        quad->y3 = y + 13;
        quad->u0 = u;
        quad->v0 = i * 13;
        quad->u1 = u + 0x7F;
        quad->v1 = i * 13;
        quad->u2 = u;
        quad->v2 = i * 13 + 12;
        quad->u3 = u + 0x7F;
        quad->v3 = i * 13 + 12;
        if (g_FxCommonRoomVariant == i)
            quad->clut = GetClut(0x3C0, 0x90);
        else
            quad->clut = GetClut(0x3C0, 0x8F);
        quad->tpage = GetTPage(0, 1, 0x3C0, 0);
        leftBuffer = g_FxCommonActiveBuffer;
        quad->tag.address = leftBuffer->allocation[9].address;
        leftBuffer->allocation[9].address = (uintptr_t)quad;
        quad++;

        u = 0x80;
        SetPolyGT4(quad);
        SetSemiTrans(quad, 1);
        quad->r0 = u;
        quad->g0 = u;
        quad->b0 = u;
        quad->r1 = u;
        quad->g1 = u;
        quad->b1 = u;
        quad->r2 = u;
        quad->g2 = u;
        quad->b2 = u;
        quad->r3 = u;
        quad->g3 = u;
        quad->b3 = u;
        quad->x0 = x;
        quad->y0 = y;
        quad->x1 = x + 0x7F;
        quad->y1 = y;
        quad->x2 = x;
        quad->y2 = y + 12;
        quad->x3 = x + 0x7F;
        quad->y3 = y + 13;
        quad->u0 = u;
        quad->v0 = i * 13;
        quad->u1 = u + 0x7F;
        quad->v1 = i * 13;
        quad->u2 = u;
        quad->v2 = i * 13 + 12;
        quad->u3 = u + 0x7F;
        quad->v3 = i * 13 + 12;
        quad->clut = GetClut(0x3C0, 0x91);
        quad->tpage = GetTPage(0, 2, 0x3C0, 0);
        rightBuffer = g_FxCommonActiveBuffer;
        quad->tag.address = rightBuffer->allocation[9].address;
        rightBuffer->allocation[9].address = (uintptr_t)quad;
        quad++;
        rightBuffer->data = (u8 *)quad;
    }
    return value;
}

#undef FX_MODE_PACKET

s16 FxCommon_StepFadeIntensity(s16 value)
{
    s16 result;

    result = value;
    if (value > 0) {
        if (value < 0xFF) {
            FxCommon_DrawFullScreenFade((u8)result);
            result = value + 0x10;
        } else {
            FxCommon_DrawFullScreenFade(0xFF);
        }
    }

    if (result < 0) {
        if (result >= -0xFE) {
            FxCommon_DrawFullScreenFade((u8)(result - 1));
            result -= 0x10;
        } else {
            FxCommon_DrawFullScreenFade(0);
            result = 0;
        }
    }

    return result;
}

void FxCommon_DrawFullScreenFade(int intensity)
{
    FxCommonBuffer *buffer=g_FxCommonActiveBuffer;
    size_t required=sizeof(FxCommonGradientQuad)+sizeof(FxCommonDrawModePacket);
    if(!buffer->end || (uintptr_t)buffer->data>(uintptr_t)buffer->end || required>(uintptr_t)buffer->end-(uintptr_t)buffer->data){
        buffer->error=-1;return;
    }
    FxCommonGradientQuad *quad=(FxCommonGradientQuad *)buffer->data;
    SetPolyG4(quad);
    SetSemiTrans(quad, 1);

    quad->color0[0] = intensity;
    quad->color0[1] = intensity;
    quad->color0[2] = intensity;
    quad->color1[0] = intensity;
    quad->color1[1] = intensity;
    quad->color1[2] = intensity;
    quad->color2[0] = intensity;
    quad->color2[1] = intensity;
    quad->color2[2] = intensity;
    quad->color3[0] = intensity;
    quad->color3[1] = intensity;
    quad->color3[2] = intensity;
    quad->x0 = 0;
    quad->y0 = 0;
    quad->x1 = 0x140;
    quad->y1 = 0;
    quad->x2 = 0;
    quad->y2 = 0xF0;
    quad->x3 = 0x140;
    quad->y3 = 0xF0;

    quad->tag.address=buffer->allocation[0].address;
    buffer->allocation[0].address=(uintptr_t)quad;
    FxCommonDrawModePacket *mode=(FxCommonDrawModePacket *)(quad+1);
    SetDrawTPage(mode,0,0,0x40);
    mode->tag.address=buffer->allocation[0].address;
    buffer->allocation[0].address=(uintptr_t)mode;
    buffer->data=(u8 *)(mode+1);
}

/* Advance effect selection, resource transitions and the two motion blends. */
void FxCommon_StepEffects(void)
{
    FxCommonMotionRecord mode1Sample;
    FxCommonMotionRecord mode1NextSample;
    FxCommonMotionRecord mode2Sample;
    FxCommonMotionRecord mode2NextSample;
    GteShortVector shortA;
    GteShortVector shortB;
    GteShortVector sampleExtra;
    FxCommonMotionRecord motionSample;
    GteShortVector motionExtra;
    s32 copyX;
    s32 copyY;
    s32 copyZ;
    s32 currentX;
    s32 currentY;
    s32 currentZ;
    s16 resourceValues[5];
    s32 rowIndex;
    /* Raw halfword is loaded before the blend duration, then sign-extended. */
    u16 phase0;
    u16 phase1;
    s16 nextMode;
    s16 remaining0;
    s16 remaining1;
    s16 motionBase0;
    s16 motionBase1;
    s16 countdown;
    s16 mode;
    s16 transitionMode;
    s32 accumX0;
    s32 accumX1;
    s32 accumZ0;
    s32 accumZ1;
    s32 nextFrame;
    s32 elapsed;
    s32 resourceTicks;
    s32 accumY1;
    s32 accumY0;
    s32 motionSlot0;
    s32 motionSlot1;
    u32 motionIndex0;
    u32 motionIndex1;
    s16 nextIndex0;
    s16 nextIndex1;

    memset(resourceValues,0,sizeof(resourceValues));
    if((unsigned)g_FxCommonRoomVariant>=10){g_FxCommonActiveBuffer->error=-3;return;}
    if (g_FxCommonExitCountdown != 0) {
        countdown = g_FxCommonExitCountdown - 1;
        g_FxCommonExitCountdown = countdown;
        if (countdown == 1) {
            g_FxCommonExitRequested = 1;
        }
    }
    if (g_FxCommonPlayAdvance == 1) {
        FxCommon_StartCameraMove(g_FxCommonRoomVariant, 0, 0, (u32)g_FxCommonPlayFrame << 8);
        rowIndex = g_FxCommonRoomVariant;
        if (g_FxCommonPlayFrame >= g_FxCommonEffectSetup[rowIndex].value30) {
            g_FxCommonDrawTintedCopies = 0;
        }
        if (g_FxCommonPlayFrame < (g_FxCommonEffectSetup[rowIndex].value0c - 1)) {
            g_FxCommonPlayFrame = (s32)((u32)g_FxCommonPlayFrame + 1u);
        } else {
            g_FxCommonPlayAdvance = 0;
        }
    }
    if ((g_FxCommonExitCountdown != 0) && (g_FxCommonPlayAdvance == 0) && (g_FxCommonViewMode != 0xA) && (g_FxCommonReplayCamera == 0) && (g_FxCommonAutoPlay == -1)) {
        nextFrame = (s32)((u32)g_FxCommonPlayFrame + 1u);
        g_FxCommonPlayFrame = nextFrame;
        FxCommon_StartCameraMove(g_FxCommonRoomVariant, 0, 0, (u32)nextFrame << 8);
    }
    if ((g_FxCommonPlaying == 1) && (elapsed = (s32)((u32)g_FxCommonPlayTicks + 1u), g_FxCommonPlayTicks = elapsed, ((elapsed < 0xC9) == 0)) && (g_FxCommonCameraPathRemaining == 0)) {
        if (g_FxCommonReplayCamera == 0) {
            g_FxCommonCameraPathPosition = 0;
        }
        g_FxCommonReplayCamera = 1;
    } else {
        g_FxCommonReplayCamera = 0;
    }
    mode = g_FxCommonViewMode;
    if (mode != 0xA) {
        if ((g_FxCommonExitCountdown == 0) && (g_FieldPadBits != 0)) {
            if ((g_FieldPadPressed & 2) && (g_FxCommonAutoPlay == -1)) {
                nextMode = (s16)(mode + 1) % 3;
                g_FxCommonViewMode = nextMode;
                if (nextMode == 0) {
                    g_FxCommonMarkerFade = 1;
                    g_FxCommonPlayAdvance = 0;
                    FxCommon_StartCameraMove(g_FxCommonRoomVariant, 4, 5, 0);
                    if ((g_FxCommonExitCountdown == 0) && (g_AkaoBgmHandle[0] != 0)) {
                        Akao_SendTableCommand(g_AkaoBgmHandle[0], 0x44D, 0, 0x80, 0x7F);
                    }
                }
                transitionMode = g_FxCommonViewMode;
                if (transitionMode == 1) {
                    if (g_FieldPadBits & 0x20) {
                        if ((g_FxCommonExitCountdown == 0) && (g_AkaoBgmHandle[0] != 0)) {
                            Akao_SendTableCommand(g_AkaoBgmHandle[0], 0x44C, 0, 0x80, 0x7F);
                        }
                        g_FxCommonMarkerFade = -1;
                        g_FxCommonDrawTintedCopies = 1;
                        g_FxCommonPlayTicks = 0;
                        g_FxCommonPlaying = 0;
                        g_FxCommonPlayAdvance = 0;
                        g_FxCommonInputLock = 0;
                        FxCommon_SamplePath((s32)((u32)g_FxCommonTailPathPosition + 0x20u), 0x4A, Draw_GetTimTableEntry((g_FxCommonTimTable), 2), &mode1Sample.vector, &sampleExtra);
                        FxCommon_SamplePath((s32)((u32)g_FxCommonTailPathPosition + 0x20u), 0x4B, Draw_GetTimTableEntry((g_FxCommonTimTable), 2), &mode1NextSample.vector, &sampleExtra);
                        copyX = mode1Sample.vector.x;
                        copyY = mode1Sample.vector.y;
                        copyZ = mode1Sample.vector.z;
                        currentX = g_FxCommonCameraTarget.x;
                        currentY = g_FxCommonCameraTarget.y;
                        currentZ = g_FxCommonCameraTarget.z;
                        shortA.x = (s16)copyX;
                        shortA.y = copyY - 0x64;
                        shortA.z = (s16)copyZ;
                        shortB.x = (s16)currentX;
                        shortB.y = (s16)currentY;
                        shortB.z = (s16)currentZ;
                        FxCommon_SetMotionEndpoints((FxCommonShortVec3 *)&shortA, (FxCommonShortVec3 *)&shortB, 4, 5);
                        g_FxCommonTailViewEntered = 1;
                    } else {
                        g_FxCommonViewMode = 2;
                        g_FxCommonTailViewEntered = 0;
                    }
                }
                if (g_FxCommonViewMode == 2) {
                    if ((g_FxCommonExitCountdown == 0) && (g_AkaoBgmHandle[0] != 0)) {
                        Akao_SendTableCommand(g_AkaoBgmHandle[0], 0x44C, 0, 0x80, 0x7F);
                    }
                    if (g_FxCommonTailViewEntered == 0) {
                        g_FxCommonMarkerFade = -1;
                    }
                    if (g_FxCommonTailViewEntered == 1) {
                        g_FxCommonMarkerFade = 0;
                    }
                    g_FxCommonDrawTintedCopies = 1;
                    g_FxCommonPlayTicks = 0;
                    g_FxCommonPlaying = 0;
                    g_FxCommonPlayAdvance = 0;
                    g_FxCommonInputLock = 0;
                    FxCommon_SamplePath((s32)((u32)g_FxCommonFlyerPathPosition + 0x100u), 0x4C, Draw_GetTimTableEntry((g_FxCommonTimTable), 2), &mode2Sample.vector, &sampleExtra);
                    FxCommon_SamplePath((s32)((u32)g_FxCommonFlyerPathPosition + 0x100u), 0x4D, Draw_GetTimTableEntry((g_FxCommonTimTable), 2), &mode2NextSample.vector, &sampleExtra);
                    shortA.x = (s16) mode2NextSample.vector.x;
                    shortA.y = (s16) mode2NextSample.vector.y;
                    shortA.z = (s16) mode2NextSample.vector.z;
                    shortB.x = (s16) mode2Sample.vector.x;
                    shortB.y = (s16) mode2Sample.vector.y;
                    shortB.z = (s16) mode2Sample.vector.z;
                    FxCommon_SetMotionEndpoints((FxCommonShortVec3 *)&shortA, (FxCommonShortVec3 *)&shortB, 4, 5);
                }
            }
            if ((g_FieldPadPressed & 0x50) && (g_FxCommonViewMode != 0)) {
                g_FxCommonViewMode = 0;
                g_FxCommonMarkerFade = 1;
                g_FxCommonPlayAdvance = 0;
                FxCommon_StartCameraMove(g_FxCommonRoomVariant, 4, 5, 0);
                if ((g_FxCommonExitCountdown == 0) && (g_AkaoBgmHandle[0] != 0)) {
                    Akao_SendTableCommand(g_AkaoBgmHandle[0], 0x44D, 0, 0x80, 0x7F);
                }
            }
            if ((g_FxCommonCameraPathRemaining < 2U) && (g_FxCommonViewMode == 0) && (g_FxCommonInputLock == 0)) {
                if ((g_FieldPadPressed & 0x20000000) && (g_FxCommonExitCountdown == 0)) {
                    if (g_FxCommonCameraPathRemaining == 1) {
                        g_FxCommonScreenFade = 1;
                        g_FxCommonExitCountdown = 0x10;
                        g_FxCommonExitVariant = g_FxCommonRoomVariant;
                    }
                    if (g_FxCommonPlaying == 0) {
                        if (g_AkaoBgmHandle[0] != 0) {
                            Akao_SendTableCommand(g_AkaoBgmHandle[0], 0x44C, 0, 0x80, 0x7F);
                        }
                        g_FxCommonMarkerFade = -1;
                        g_FxCommonPlayAdvance = 1;
                        g_FxCommonPlaying = 1;
                        g_FxCommonPlayFrame = 0;
                        g_FxCommonPlayTicks = 1;
                    } else {
                        if (g_AkaoBgmHandle[0] != 0) {
                            Akao_SendTableCommand(g_AkaoBgmHandle[0], 0x44C, 0, 0x80, 0x7F);
                        }
                        g_FxCommonScreenFade = 1;
                        g_FxCommonExitCountdown = 0x10;
                        g_FxCommonExitVariant = g_FxCommonRoomVariant;
                    }
                    if ((g_FxCommonConfirmExit == 1) && (g_FxCommonRoomVariant == g_FxCommonConfirmExitVariant)) {
                        g_FxCommonMarkerFade = -1;
                        g_FxCommonScreenFade = 1;
                        g_FxCommonPlayAdvance = 0;
                        g_FxCommonPlaying = 0;
                        g_FxCommonPlayFrame = 0;
                        g_FxCommonPlayTicks = 0;
                        g_FxCommonExitCountdown = 0x10;
                        g_FxCommonExitVariant = g_FxCommonRoomVariant;
                    }
                }
                if ((g_FieldPadPressed & 0x40000000) && (g_FxCommonAutoPlay == -1) && (g_FxCommonExitCountdown == 0)) {
                    g_FxCommonDrawTintedCopies = 1;
                    if (g_FxCommonPlaying == 1) {
                        if (g_AkaoBgmHandle[0] != 0) {
                            Akao_SendTableCommand(g_AkaoBgmHandle[0], 0x44D, 0, 0x80, 0x7F);
                        }
                        FxCommon_StartCameraMove(g_FxCommonRoomVariant, 4, 5, 0);
                        g_FxCommonMarkerFade = 1;
                        g_FxCommonPlaying = 0;
                        g_FxCommonPlayAdvance = 0;
                    }
                }
                if ((g_FieldPadPressed & 0x40) && (g_FxCommonAutoPlay == -1)) {
                    if (g_FxCommonPlaying == 1) {
                        g_FxCommonMarkerFade = 1;
                    }
                    g_FxCommonPlayAdvance = 0;
                    g_FxCommonPlaying = 0;
                    g_FxCommonPlayFrame = 0;
                    g_FxCommonDrawTintedCopies = 1;
                    if (g_FxCommonEffectSetup[g_FxCommonRoomVariant].value22 != 0xFF) {
                        unsigned choices=0;
                        do {
                            nextIndex0 = g_FxCommonEffectSetup[g_FxCommonRoomVariant].value22;
                            if((unsigned)nextIndex0>=10 || ++choices>10){g_FxCommonActiveBuffer->error=-3;return;}
                            g_FxCommonRoomVariant = nextIndex0;
                        } while (g_FxCommonEffectSetup[nextIndex0].enabled == 0);
                        g_FxCommonPageChanged = 1;
                        g_FxCommonPlayFrame = g_FxCommonEffectSetup[nextIndex0].value0c;
                        FxCommon_StartCameraMove(nextIndex0, 4, 5, 0);
                        motionIndex0 = 0;
                        motionSlot0 = 0;
                        phase0 = g_FxCommonRoomVariant;
                        g_FxCommonMotionBlendFrames = 0x20;
                        motionBase0 = (s16)phase0;
                        do {
                            FxCommon_SamplePath(motionIndex0 << 8, motionBase0 + 0x40, Draw_GetTimTableEntry((g_FxCommonTimTable), 2), &motionSample.vector, &motionExtra);
                            motionIndex0 += 1;
                            g_FxCommonMotionDeltas[motionSlot0].vector.x = (s32)(((u32)motionSample.vector.x << 16) - (u32)g_FxCommonMotionPositions[motionSlot0].vector.x) >> 5;
                            g_FxCommonMotionDeltas[motionSlot0].vector.y = (s32)(((u32)motionSample.vector.y << 16) - (u32)g_FxCommonMotionPositions[motionSlot0].vector.y) >> 5;
                            g_FxCommonMotionDeltas[motionSlot0].vector.z = (s32)(((u32)motionSample.vector.z << 16) - (u32)g_FxCommonMotionPositions[motionSlot0].vector.z) >> 5;
                            motionSlot0 += 1;
                        } while (motionIndex0 < 0xAU);
                    }
                }
                if ((g_FieldPadPressed & 0x10) && (g_FxCommonAutoPlay == -1)) {
                    if (g_FxCommonPlaying == 1) {
                        g_FxCommonMarkerFade = 1;
                    }
                    g_FxCommonPlayAdvance = 0;
                    g_FxCommonPlaying = 0;
                    g_FxCommonPlayFrame = 0;
                    g_FxCommonDrawTintedCopies = 1;
                    if (g_FxCommonEffectSetup[g_FxCommonRoomVariant].value23 != 0xFF) {
                        unsigned choices=0;
                        do {
                            nextIndex1 = g_FxCommonEffectSetup[g_FxCommonRoomVariant].value23;
                            if((unsigned)nextIndex1>=10 || ++choices>10){g_FxCommonActiveBuffer->error=-3;return;}
                            g_FxCommonRoomVariant = nextIndex1;
                        } while (g_FxCommonEffectSetup[nextIndex1].enabled == 0);
                        g_FxCommonPageChanged = 1;
                        g_FxCommonPlayFrame = g_FxCommonEffectSetup[nextIndex1].value0c;
                        FxCommon_StartCameraMove(nextIndex1, 4, 5, 0);
                        motionIndex1 = 0;
                        motionSlot1 = 0;
                        phase1 = g_FxCommonRoomVariant;
                        g_FxCommonMotionBlendFrames = 0x20;
                        motionBase1 = (s16)phase1;
                        do {
                            FxCommon_SamplePath(motionIndex1 << 8, motionBase1 + 0x40, Draw_GetTimTableEntry((g_FxCommonTimTable), 2), &motionSample.vector, &motionExtra);
                            motionIndex1 += 1;
                            g_FxCommonMotionDeltas[motionSlot1].vector.x = (s32)(((u32)motionSample.vector.x << 16) - (u32)g_FxCommonMotionPositions[motionSlot1].vector.x) >> 5;
                            g_FxCommonMotionDeltas[motionSlot1].vector.y = (s32)(((u32)motionSample.vector.y << 16) - (u32)g_FxCommonMotionPositions[motionSlot1].vector.y) >> 5;
                            g_FxCommonMotionDeltas[motionSlot1].vector.z = (s32)(((u32)motionSample.vector.z << 16) - (u32)g_FxCommonMotionPositions[motionSlot1].vector.z) >> 5;
                            motionSlot1 += 1;
                        } while (motionIndex1 < 0xAU);
                    }
                }
            }
        }
    }
    if ((g_FxCommonViewMode == 1) && (g_FxCommonMotionRemaining0 == 0) && (g_FxCommonMotionRemaining1 == 0)) {
        ((FxCommonVec3 *)&g_FxCommonCameraPosition)->x = g_FxCommonPathTailNodes[1]->matrix.t[0];
        g_FxCommonCameraPosition.y = g_FxCommonPathTailNodes[1]->matrix.t[1] - 0x12C;
        g_FxCommonCameraPosition.z = g_FxCommonPathTailNodes[1]->matrix.t[2];
        ((FxCommonVec3 *)&g_FxCommonCameraTarget)->x = g_FxCommonPathNodes[0]->matrix.t[0];
        g_FxCommonCameraTarget.y = g_FxCommonPathNodes[0]->matrix.t[1];
        g_FxCommonCameraTarget.z = g_FxCommonPathNodes[0]->matrix.t[2];
    }
    if ((g_FxCommonViewMode == 2) && (g_FxCommonMotionRemaining0 == 0) && (g_FxCommonMotionRemaining1 == 0)) {
        ((FxCommonVec3 *)&g_FxCommonCameraPosition)->x = g_FxCommonPathNodes[2]->matrix.t[0];
        g_FxCommonCameraPosition.y = g_FxCommonPathNodes[2]->matrix.t[1];
        g_FxCommonCameraPosition.z = g_FxCommonPathNodes[2]->matrix.t[2];
        ((FxCommonVec3 *)&g_FxCommonCameraTarget)->x = g_FxCommonPathNodes[0]->matrix.t[0];
        g_FxCommonCameraTarget.y = g_FxCommonPathNodes[0]->matrix.t[1];
        g_FxCommonCameraTarget.z = g_FxCommonPathNodes[0]->matrix.t[2];
    }
    if (g_FxCommonReplayCamera == 1) {
        FxCommon_SamplePath(g_FxCommonCameraPathPosition, g_FxCommonEffectSetup[g_FxCommonRoomVariant].motionIndices[3], Draw_GetTimTableEntry((g_FxCommonTimTable), 2), &g_FxCommonCameraPosition, &sampleExtra);
        FxCommon_SamplePath(g_FxCommonCameraPathPosition, g_FxCommonEffectSetup[g_FxCommonRoomVariant].motionIndices[2], Draw_GetTimTableEntry((g_FxCommonTimTable), 2), &g_FxCommonCameraTarget, &sampleExtra);
        g_FxCommonCameraPathPosition = (s32)((u32)g_FxCommonCameraPathPosition + 0x80u);
    }
    if (g_FxCommonViewMode == 0xA) {
        g_FxCommonDemoFrame += 1;
        FxCommon_SamplePath(g_FxCommonCameraPathPosition, 0x4E, Draw_GetTimTableEntry((g_FxCommonTimTable), 2), &g_FxCommonCameraPosition, &sampleExtra);
        FxCommon_SamplePath(g_FxCommonCameraPathPosition, 0x4F, Draw_GetTimTableEntry((g_FxCommonTimTable), 2), &g_FxCommonCameraTarget, &sampleExtra);
        g_FxCommonCameraPathPosition = (s32)((u32)g_FxCommonCameraPathPosition + 0x10u);
        if ((s16) g_FxCommonDemoFrame == 0x4B) {
            g_FxCommonDemoResource = FxCommon_SwitchResource(0xB, 0U);
            g_FxCommonDemoResourceTicks = 0;
        }
        if ((s16)g_FxCommonDemoFrame == 0x11D) {
            FxCommon_SwitchResource(0xE, 1U);
        }
        if ((s16)g_FxCommonDemoFrame == 0x13B) {
            g_FxCommonDemoResource = FxCommon_SwitchResource(0xC, 0U);
            g_FxCommonDemoResourceTicks = 0;
        }
        if ((s16)g_FxCommonDemoFrame == 0x20D) {
            FxCommon_SwitchResource(0xE, 1U);
        }
        if ((s16)g_FxCommonDemoFrame == 0x22B) {
            g_FxCommonDemoResource = FxCommon_SwitchResource(0xD, 0U);
            g_FxCommonDemoResourceTicks = 0;
        }
        if ((s16)g_FxCommonDemoFrame == 0x2FD) {
            FxCommon_SwitchResource(0xE, 1U);
        }
        if ((s16)g_FxCommonDemoFrame == 0x31B) {
            g_FxCommonDemoResource = FxCommon_SwitchResource(0xE, 0U);
            g_FxCommonDemoResourceTicks = 0;
        }
        if ((s16)g_FxCommonDemoFrame == 0x3ED) {
            FxCommon_SwitchResource(0xE, 1U);
        }
        if ((s16)g_FxCommonDemoFrame == 0x40B) {
            g_FxCommonDemoResource = FxCommon_SwitchResource(0xE, 1U);
        }
        if (g_FxCommonDemoResourceTicks == 0) {
            g_FxCommonDemoFadePhase = 0;
            g_FxCommonDemoShade = 0;
        }
        if (g_FxCommonDemoResourceTicks == 0x18) {
            g_FxCommonDemoFadePhase = 1;
        }
        if (g_FxCommonDemoResourceTicks == 0xBA) {
            g_FxCommonDemoFadePhase = 2;
        }
        g_FxCommonDemoResourceTicks = (s32)((u32)g_FxCommonDemoResourceTicks + 1u);
        if ((g_FxCommonDemoFadePhase == 0) && (g_FxCommonDemoShade < 0x78U)) {
            g_FxCommonDemoShade += 5;
        }
        if ((g_FxCommonDemoFadePhase == 2) && (g_FxCommonDemoShade >= 0xBU)) {
            g_FxCommonDemoShade -= 5;
        }
        Menu_SetHighlightColor(g_FxCommonDemoResource, g_FxCommonDemoShade, g_FxCommonDemoShade, g_FxCommonDemoShade);
        resourceTicks = (s32)((u32)g_FxCommonDemoTicks + 1u);
        g_FxCommonDemoTicks = resourceTicks;
        if (resourceTicks == 0x4B1) {
            g_FxCommonScreenFade = 1;
            g_FxCommonExitCountdown = 0x10;
            g_FxCommonExitVariant = g_FxCommonRoomVariant;
            Akao_Cmd_A9(0x12C, 0);
            g_FxCommonDemoExiting = 1;
        }
        if (g_FxCommonDemoTicks == 0x41A) {
            Akao_Cmd_C1_WithSlot(g_SceneAudioSlot0Handle, 0xDC, 0);
        }
    }
    if(FxCommon_StepCameraPath()<0){g_FxCommonActiveBuffer->error=-3;return;}
    if (g_FxCommonPageChanged != 0) {
        g_FxCommonPageChanged = 0;
        if (g_FxCommonPageOpen == 1) {
            Draw_CloseTextboxPage(g_FxCommonPageId);
        }
        Menu_SetHighlightColor(g_FxCommonEffectSetup[g_FxCommonRoomVariant].value25, 0x80, 0x80, 0x80);
        if (g_FxCommonViewMode != 0xA) {
            Render_SetupColorTable(g_FxCommonEffectSetup[g_FxCommonRoomVariant].value25, 3, resourceValues);
        }
        g_FxCommonPageId = (s16) g_FxCommonEffectSetup[g_FxCommonRoomVariant].value25;
        g_FxCommonPageOpen = 1;
    }
    if (g_FxCommonMotionRemaining0 != 0) {
        remaining0 = g_FxCommonMotionRemaining0 - 1;
        g_FxCommonMotionRemaining0 = remaining0;
        accumX0 = (s32)((u32)g_FxCommonMotionAccum0.x + (u32)g_FxCommonMotionStep0X);
        g_FxCommonMotionAccum0.x = accumX0;
        accumY0 = (s32)((u32)g_FxCommonMotionAccum0.y + (u32)g_FxCommonMotionStep0Y);
        accumZ0 = (s32)((u32)g_FxCommonMotionAccum0.z + (u32)g_FxCommonMotionStep0Z);
        g_FxCommonMotionAccum0.y = accumY0;
        g_FxCommonMotionAccum0.z = accumZ0;
        g_FxCommonCameraTarget.x = (s32)((u32)g_FxCommonMotionOrigin0.x + (u32)(accumX0 >> (g_FxCommonMotionShift0 & 31)));
        g_FxCommonCameraTarget.y = (s32)((u32)g_FxCommonMotionOrigin0.y + (u32)(accumY0 >> (g_FxCommonMotionShift0 & 31)));
        g_FxCommonCameraTarget.z = (s32)((u32)g_FxCommonMotionOrigin0.z + (u32)(accumZ0 >> (g_FxCommonMotionShift0 & 31)));
        if ((u16)remaining0 == 0) {
            g_FxCommonCameraTarget.x = g_FxCommonMotionTarget0.x;
            g_FxCommonCameraTarget.y = g_FxCommonMotionTarget0.y;
            g_FxCommonCameraTarget.z = g_FxCommonMotionTarget0.z;
        }
    }
    if (g_FxCommonMotionRemaining1 != 0) {
        remaining1 = g_FxCommonMotionRemaining1 - 1;
        g_FxCommonMotionRemaining1 = remaining1;
        accumX1 = (s32)((u32)g_FxCommonMotionAccum1.x + (u32)g_FxCommonMotionStep1X);
        g_FxCommonMotionAccum1.x = accumX1;
        accumY1 = (s32)((u32)g_FxCommonMotionAccum1.y + (u32)g_FxCommonMotionStep1Y);
        accumZ1 = (s32)((u32)g_FxCommonMotionAccum1.z + (u32)g_FxCommonMotionStep1Z);
        g_FxCommonMotionAccum1.y = accumY1;
        g_FxCommonMotionAccum1.z = accumZ1;
        g_FxCommonCameraPosition.x = (s32)((u32)g_FxCommonMotionOrigin1.x + (u32)(accumX1 >> (g_FxCommonMotionShift1 & 31)));
        g_FxCommonCameraPosition.y = (s32)((u32)g_FxCommonMotionOrigin1.y + (u32)(accumY1 >> (g_FxCommonMotionShift1 & 31)));
        g_FxCommonCameraPosition.z = (s32)((u32)g_FxCommonMotionOrigin1.z + (u32)(accumZ1 >> (g_FxCommonMotionShift1 & 31)));
        if ((u16)remaining1 == 0) {
            g_FxCommonCameraPosition.x = g_FxCommonMotionTarget1.x;
            g_FxCommonCameraPosition.y = g_FxCommonMotionTarget1.y;
            g_FxCommonCameraPosition.z = g_FxCommonMotionTarget1.z;
        }
    }
}

#undef DRAW
#undef DRAW_PAIR
#undef FX_MODE_PACKET

s16 FxCommon_SwitchResource(s16 resourceId, u8 action)
{
    s16 values[5]={0};
    s16 id;

    id = resourceId;
    if (action == 0) {
        if (id != g_FxCommonResourceState.active) {
            Draw_CloseTextboxPage(g_FxCommonResourceState.current.half);
            Menu_SetHighlightColor(id, 0x80, 0x80, 0x80);
            Render_SetupColorTable(id, 3, values);
            g_FxCommonResourceState.current.word = id;
        }
    }

    if (action == 1) {
        Draw_CloseTextboxPage(g_FxCommonResourceState.current.half);
    }

    return resourceId;
}

/* Sample target and eye endpoints, then initialize both original motion lanes. */
void FxCommon_StartCameraMove(s32 index, s32 first, s32 second, u32 source) {
    FxCommonMotionRecord point;
    GteShortVector extra;
    s32 secondMask;
    s16 setupIndex;
    s32 firstMask;
    s32 firstValue;
    s32 secondValue;
    TimRelativeTable *table = g_FxCommonTimTable;
    GteShortVector *extraPtr;
    FxCommonShortVec3 *origin0 = &g_FxCommonMotionOrigin0;
    FxCommonShortVec3 *target0 = &g_FxCommonMotionTarget0;
    FxCommonShortVec3 *origin1 = &g_FxCommonMotionOrigin1;
    FxCommonShortVec3 *target1 = &g_FxCommonMotionTarget1;
    u32 sourceValue;
    TimRelativeTable *lookupTable;
    s32 lookupCount;
    void *base;
    s32 bx;
    s32 by;
    s32 bz;
    s32 ax;
    s32 ay;
    s32 az;
    s32 px;
    s32 py;
    s32 pz;
    s32 qx;
    s32 qy;
    s32 qz;

    lookupTable = table;
    firstValue = first;
    lookupCount = 2;
    sourceValue = source;
    /* Capture the second lane index after preparing the lookup arguments. */
    secondValue = second;
    base = Draw_GetTimTableEntry(lookupTable, lookupCount);
    setupIndex = (s16)index;
    extraPtr = &extra;
    FxCommon_SamplePath(sourceValue, g_FxCommonEffectSetup[setupIndex].motionIndices[0], base, &point.vector, extraPtr);

    lookupTable = table;
    firstValue &= 0xff;
    px = point.vector.x;
    bx = g_FxCommonCameraTarget.x;
    py = point.vector.y;
    by = g_FxCommonCameraTarget.y;
    pz = point.vector.z;
    bz = g_FxCommonCameraTarget.z;
    g_FxCommonMotionShift0 = firstValue;
    firstMask = (s32)(1u << (firstValue & 31));
    g_FxCommonMotionAccum0X = 0;
    g_FxCommonMotionAccum0Y = 0;
    g_FxCommonMotionAccum0Z = 0;
    g_FxCommonMotionRemaining0 = firstMask;
    g_FxCommonMotionStep0X = (s32)((u32)px - (u32)bx);
    g_FxCommonMotionStep0Y = (s32)((u32)py - (u32)by);
    g_FxCommonMotionStep0Z = (s32)((u32)pz - (u32)bz);
    g_FxCommonMotionOrigin0X = bx;
    origin0->y = by;
    origin0->z = bz;
    g_FxCommonMotionTarget0X = px;
    target0->y = py;
    target0->z = pz;

    base = Draw_GetTimTableEntry(lookupTable, 2);
    FxCommon_SamplePath(sourceValue, g_FxCommonEffectSetup[setupIndex].motionIndices[1], base, &point.vector, extraPtr);

    secondValue &= 0xff;
    qx = point.vector.x;
    ax = g_FxCommonCameraPosition.x;
    qy = point.vector.y;
    ay = g_FxCommonCameraPosition.y;
    qz = point.vector.z;
    az = g_FxCommonCameraPosition.z;
    secondMask = (s32)(1u << (secondValue & 31));
    g_FxCommonMotionAccum1X = 0;
    g_FxCommonMotionAccum1Y = 0;
    g_FxCommonMotionAccum1Z = 0;
    g_FxCommonMotionShift1 = secondValue;
    g_FxCommonMotionRemaining1 = secondMask;
    g_FxCommonMotionStep1X = (s32)((u32)qx - (u32)ax);
    g_FxCommonMotionStep1Y = (s32)((u32)qy - (u32)ay);
    g_FxCommonMotionStep1Z = (s32)((u32)qz - (u32)az);
    g_FxCommonMotionOrigin1X = ax;
    origin1->y = ay;
    origin1->z = az;
    g_FxCommonMotionTarget1X = qx;
    target1->y = qy;
    target1->z = qz;
}

/* One native owner for the original shared motion records and marker state. */
FxCommonMotionRecord g_FxCommonMotionPositions[10],g_FxCommonMotionDeltas[10];
s16 g_FxCommonMotionBlendFrames,g_FxCommonMarkerFade;
