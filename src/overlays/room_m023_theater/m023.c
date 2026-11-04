#include "common.h"
#include "pe1/geom_state.h"
#include "pe1/gte.h"
#include "pe1/room_drift_pulse.h"
#include "pe1/room_m023_beacon.h"
#include "pe1/room_m023_effects.h"
#include "pe1/particle_augment.h"

/* Drift/pulse sprite for room_m023. The glow strip's anchor is taken into
 * a local pointer first: its address load is what keeps the frame counter
 * load after the palette lookup like retail. */

int RoomM023_DriftPulseCallback(int mode, void *data, intptr_t argument) {
    RoomDriftPulseState *state = data;
    (void)argument;
    GteShortVector origin;
    GteRotation rotation;
    RenderColor color;
    RenderMatrixSlot *matrixSlot;

    switch (mode) {
    case 1:
        switch (state->phase) {
        case 0:
            state->x += state->vx;
            state->y += state->vy;
            state->z += state->vz;
            if (g_FieldFxCurrentAge >= 4) {
                return 1;
            }
        case 1:
            state->x += state->vx;
            state->y += state->vy;
            state->z += state->vz;
            if (g_FieldFxCurrentAge < 8) {
                break;
            }
            return 1;
        default:
            return 0;
        }
        break;
    case 2:
        matrixSlot = &g_ViewMatrixSlot;
        gte_ldrotmatrix(matrixSlot->value);
        gte_ldtransmatrix(matrixSlot->value);
        switch (state->phase) {
        case 0:
            {
                GteShortVector *anchor = &RoomM023_ScriptAnchor;
                int kind;
                int palette;
                int fade;
                u16 clut;

                *(int *)&color = 0x808080;
                origin.x = state->x;
                origin.y = state->y;
                origin.z = state->z;
                kind = g_FieldFxSpriteParams.palette;
                palette = g_FieldFxPaletteTable[kind];
                fade = (g_FieldFxCurrentAge - 1) << 4;
                if (kind == 4 && g_FieldFxAltPaletteFlag != 0) palette += 4;
                clut = GetClut(0, palette);
                FieldFx_DrawRotatedQuad(anchor, (GteRotation *)state, 0x3C0, 0x64, 0, fade,
                              0xFF, 0xF, clut, 0, &color, 0xBE, 1);
            }
            rotation.x = state->x;
            rotation.y = state->y;
            rotation.z = state->z;
            rotation.flags = 1;
            {
                int kind = g_FieldFxSpriteParams.palette;
                int palette = g_FieldFxPaletteTable[kind];
                u16 clut;

                if (kind == 4 && g_FieldFxAltPaletteFlag != 0) palette += 4;
                clut = GetClut(0x20, palette);
                FieldFx_DrawShapeQuads(&RoomM023_ScriptAnchor, &rotation, 0x1000, 0x1000,
                              ((s16)g_FieldFxSpriteParams.parameter02 << 1) * (g_FieldFxCurrentAge - 1) + 0x60, clut,
                              3, 0x80, 0);
            }
            break;
        case 1:
            rotation.x = 0;
            rotation.y = 0;
            rotation.flags = 0;
            rotation.z = g_FieldFxCurrentAge << 6;
            {
                int scale = rcos((g_FieldFxCurrentAge - 1) << 7) / 32;
                int kind = g_FieldFxSpriteParams.palette;
                int palette = g_FieldFxPaletteTable[kind];
                u16 clut;

                if (kind == 4 && g_FieldFxAltPaletteFlag != 0) palette += 4;
                clut = GetClut(0x10, palette);
                FieldFx_DrawShapeQuads((GteShortVector *)state, &rotation, 0x1000, 0x1000,
                              (s16)g_FieldFxSpriteParams.parameter02 * g_FieldFxCurrentAge + 0x40, clut, 1, scale, 0);
            }
            break;
        default:
            return 0;
        }
        break;
    default:
        return 0;
    }
    return 0;
}

/* Every `interval` frames emits a particle with a random velocity, either
 * from the tracked model joint or from a random point of a wide band. */
int RoomM023_ScatterEmitter(int mode, void *data, intptr_t argument) {
    RoomM023ScatterState *state = data;
    (void)argument;
    RoomM023Template template = g_RoomM023JointAnchor;
    GteShortVector position;
    RoomM023Particle *particle;

    switch (mode) {
    case 0:
        state->unused08 = 0;
        state->unused0A = 0;
        state->timer = 0;
        switch (FX_TASK_AS(RoomM023EventState)->variant) {
        case 0:
            state->joint = 8;
            break;
        case 1:
            state->joint = 16;
            break;
        }
        return FieldFx_InitCallbackList(g_FieldFxActiveTaskSlot->end, 16, 12,
                             RoomM023_DriftPulseCallback);
    case 1:
        FieldFx_TransformMatrixPoint(ANIM_OWNER_AS(RoomM023Channel)->pool, state->joint, &template.vector, &position);
        state->timer++;
        if (FX_TASK_AS(RoomM023EventState)->interval == 0) return 1;
        if (FX_TASK_AS(RoomM023EventState)->interval < state->timer) {
            state->timer = 0;
            FieldFx_GetActorPosition(ANIM_OWNER_AS(RoomM023Channel)->pool, 0, (s16 *)&position);
            particle = FieldFx_AcquireCallbackSlot(g_FieldFxActiveTaskSlot->end);
            if (particle == 0) return 0;
            if (rand() & 1) {
                particle->x = (rand() & 0x7FF) - 0x400;
                particle->y = rand();
                particle->z = 0;
                particle->attached = 0;
            } else {
                particle->x = position.x;
                particle->y = position.y;
                particle->z = position.z;
                particle->attached = 1;
            }
            particle->vx = rand() % 70 - 35;
            particle->vy = rand() % 70 - 35;
            particle->vz = rand() % 70 - 35;
        }
        break;
    case 2:
        FieldFx_TransformMatrixPoint(ANIM_OWNER_AS(RoomM023Channel)->pool, state->joint, &template.vector, &RoomM023_ScriptAnchor);
        g_FieldFxSpriteParams.parameter00 = 32;
        g_FieldFxSpriteParams.parameter02 = 2;
        g_FieldFxSpriteParams.extent_x = 32;
        g_FieldFxSpriteParams.extent_y = 32;
        g_FieldFxSpriteParams.tpage = g_FieldFxTpageTable[g_FieldFxTpageSlots[3]];
        g_FieldFxSpriteParams.palette = 3;
        g_FieldFxSpriteParams.parameter06 = 0;
        g_FieldFxSpriteParams.parameter0A = 0;
        g_FieldFxSpriteParams.depth = 12;
        break;
    }
    return 0;
}

int RoomM023_JointGlow(int mode, void *data, intptr_t argument) {
    RoomM023JointGlow *glow = data;
    (void)argument;
    GteShortVector position;
    GteRotation rotation;
    RoomM023Template template = g_RoomM023JointAnchor;
    RenderColor ring = g_RoomM023RingBandColor;
    RenderColor flare = g_RoomM023StarFanColor;
    int size;
    int intensity;
    s16 frame;
    int kind;
    int palette;

    switch (mode) {
    case 0:
        switch (FX_TASK_AS(RoomM023EventState)->variant) {
        case 0:
            glow->joint = 8;
            FieldFx_PlaySoundAtOwner(0x5AE, FieldFx_GetOwnerProgress());
            FieldFx_PlaySoundAtOwner(0x5AF, 0x80);
            break;
        case 1:
            glow->joint = 16;
            break;
        }
        glow->state = 0;
        glow->frame = 0;
        return 0;
    case 1:
        glow->frame++;
        if (glow->state >= 3) return 1;
        break;
    case 2:
        /* The PS1 ring read the previous contents of its stack position.
         * Resolve the joint before every draw on the host. */
        FieldFx_TransformMatrixPoint(ANIM_OWNER_AS(RoomM023Channel)->pool, glow->joint, &template.vector, &position);
        size = 0;
        intensity = 0;
        switch (glow->state) {
        case 0:
            frame = glow->frame;
            FieldFx_DrawRingBand(&position, 0x258, 0x320, 0x20, 0, frame, frame, &ring, 0,
                          rsin((u16)frame * 128) / 32, 1);
            size = (s32)((u32)(s32)glow->frame << 8);
            intensity = 0x50;
            if (glow->frame >= 8) {
                glow->state = 1;
                glow->frame = 0;
            }
            break;
        case 1:
            frame = glow->frame;
            size = frame * 64 + 0x800;
            intensity = frame * 48 / 32 + 0x50;
            if (frame >= 0x20) {
                glow->state = 2;
                glow->frame = 0;
            }
            break;
        case 2:
            intensity = 0xA0;
            size = rcos((u16)glow->frame * 128) / 2 + 0x1000;
            if (glow->frame >= 8) {
                glow->state = 3;
                glow->frame = 0;
            }
            break;
        }
        g_FieldFxSpriteParams.parameter00 = 0x40;
        g_FieldFxSpriteParams.parameter02 = 4;
        g_FieldFxSpriteParams.extent_x = 0x40;
        g_FieldFxSpriteParams.extent_y = 0x40;
        g_FieldFxSpriteParams.tpage = g_FieldFxTpageTable[g_FieldFxTpageSlot3];
        g_FieldFxSpriteParams.palette = 3;
        g_FieldFxSpriteParams.parameter06 = 0;
        g_FieldFxSpriteParams.parameter0A = 0;
        g_FieldFxSpriteParams.depth = 0x18;
        if (g_FieldFxCurrentAge & 1) {
            size = size * 5 / 4;
        }
        rotation.x = 0;
        rotation.y = 0;
        rotation.z = (s16)(0u - ((u32)g_FieldFxCurrentAge << 6));
        rotation.flags = 0;
        {
            kind = g_FieldFxSpriteParams.palette;
            palette = g_FieldFxPaletteTable[kind];
            if (kind == 4 && g_FieldFxAltPaletteFlag) palette += 4;
            FieldFx_BeginAuthoredFlareGlow(118,position.x,position.y,position.z,(unsigned)g_FieldFxCurrentAge,(unsigned)glow->joint);
            FieldFx_DrawShapeQuads(&position, &rotation, size * 3 / 2, size * 3 / 2, 0x80,
                          GetClut(0x30, palette), 1, intensity / ((g_FieldFxCurrentAge & 1) + 1), 0);
            FieldFx_EndAuthoredFlareGlow();
        }
        {
            kind = g_FieldFxSpriteParams.palette;
            palette = g_FieldFxPaletteTable[kind];
            if (kind == 4 && g_FieldFxAltPaletteFlag) palette += 4;
            FieldFx_DrawShapeQuads(&position, &rotation, size * 3 / 2, size * 3 / 2, 0x84,
                          GetClut(0x40, palette), 1, intensity, 0);
        }
        FieldFx_DrawStarFan(&position, 0x258, 0x258, 0xC, 0, size, size, &flare, 0, 0x40, 1);
        break;
    }
    return 0;
}

/* Joint beacon: rides a model joint, swells in, pulses, flickers and
 * collapses, tagging the player once on contact; draws a glow sprite pair
 * with two star fans and a loaded model scaled by the beacon size. */
int RoomM023_JointBeacon(int mode, void *data, intptr_t argument) {
    RoomM023Beacon *state = data;
    (void)argument;
    GteVector scale;
    GteShortVector position;
    GteShortVector rotation;
    RoomM023Template offset = g_RoomM023JointAnchor;
    GteMatrix matrix;
    GteMatrix spin;
    RoomM023BeaconColor color;
    int size;
    int page;

    switch (mode) {
    case 0:
        switch (FX_TASK_AS(RoomM023BeaconEvent)->variant) {
        case 0:
            state->joint = 8;
            break;
        case 1:
            state->joint = 0x10;
            break;
        }
        state->phase = 0;
        state->timer = 0;
        state->height = 0;
        state->glow = 0;
        if (FX_TASK_AS(RoomM023BeaconEvent)->active) {
            RoomM023BeaconPool *pool = ANIM_OWNER_AS(RoomM023BeaconChannel)->pool;
            if (pool && pool->node) {
                u8 *flag = pool->node->state;
                if (flag && *flag == 1) *flag = 2;
            }
        }
        state->armed = 1;
        g_RoomM023BeaconModel = Asset_FindTable08ByU32Key(g_LoadedSceneAssetBlock[0], 0xC5463704);
        FieldFx_ModelScrollUv((union FieldTexturedModel *)g_RoomM023BeaconModel, 0, 0);
        return 0;
    case 1:
        FieldFx_TransformMatrixPoint((struct RoomFxTransformOwner *)ANIM_OWNER_AS(RoomM023BeaconChannel)->pool, state->joint, &offset.vector, &state->position);
        FieldFx_GetTransformLookAngles((struct RoomFxTransformOwner *)ANIM_OWNER_AS(RoomM023BeaconChannel)->pool, 0,
                      &state->rotation);
        switch (state->phase) {
        case 0:
            state->timer++;
            state->width = 0x400;
            state->height += 0x19A;
            state->glow = 0x1000;
            state->shade = state->timer * 8 / 5;
            if (state->timer >= 5) {
                state->phase = 1;
                state->timer = 0;
            }
            break;
        case 1:
            state->timer++;
            state->width = rcos(state->timer << 7) + 0x1000;
            state->glow = rcos(state->timer << 7) + 0x1000;
            state->shade = state->timer + 8;
            if (state->timer >= 8) {
                state->phase = 2;
                state->timer = 0;
            }
            break;
        case 2:
            state->timer++;
            state->glow = 0x1000;
            state->shade = 0x10;
            state->width = ((g_FieldFxCurrentAge & 1) << 8) + 0x1000;
            if (state->timer >= 0x20) {
                state->phase = 3;
                state->timer = 0;
            }
            break;
        case 3:
            state->glow = 0x1000;
            state->timer++;
            state->width = 0x1000 - (state->timer << 8);
            state->shade = state->timer + 0x10;
            if (state->timer >= 0x10) return 1;
            break;
        }
        FieldFx_AnglesToVector(&state->rotation, state->height, &position);
        state->target.x = state->position.x;
        state->target.y = state->position.y;
        state->target.z = state->position.z;
        state->target.x += position.x;
        state->target.y += position.y;
        state->target.z += position.z;
        if (FieldFx_IsPlayerInSegmentBand(state, &state->target, 0x50) == 0) return 0;
        if (state->armed == 0) return 0;
        if (FX_TASK_AS(RoomM023BeaconEvent)->active) {
            RoomM023BeaconChannel *channel = ANIM_OWNER_AS(RoomM023BeaconChannel);
            if ((channel->pool->node->flags & 0x3F000000) == 0x01000000) {
                ((RoomM023BeaconEntity *)D_8009D254)->actor->flags |= 0x4000;
                channel->pool->node->flags =
                    (channel->pool->node->flags & 0xC0FFFFFF) | 0x11000000;
                channel->pool->node->flags |= 0x80000000;
            }
        }
        state->armed = 0;
        break;
    case 2:
        g_FieldFxSpriteParams.depth = 0x18;
        g_FieldFxSpriteParams.parameter00 = 0x40;
        g_FieldFxSpriteParams.parameter02 = 4;
        g_FieldFxSpriteParams.extent_x = 0x40;
        g_FieldFxSpriteParams.extent_y = 0x40;
        g_FieldFxSpriteParams.tpage = g_FieldFxTpageTable[g_FieldFxTpageSlot3];
        g_FieldFxSpriteParams.palette = 3;
        g_FieldFxSpriteParams.parameter06 = 0;
        g_FieldFxSpriteParams.parameter0A = 0;
        if (state->glow != 0) {
            rotation.x = 0;
            rotation.y = 0;
            rotation.pad = 0;
            rotation.z = g_FieldFxCurrentAge * 0x60;
            size = state->glow;
            size = size * 2 / 3;
            if (g_FieldFxCurrentAge & 1) size = size * 9 / 8;
            FieldFx_SampleColorTrack(g_RoomM023BeaconColorTrack, &color, state->shade);
            {
                int kind = g_FieldFxSpriteParams.palette;
                int palette = g_FieldFxPaletteTable[kind];
                if (kind == 4 && g_FieldFxAltPaletteFlag) palette += 4;
                FieldFx_DrawShapeQuads(&state->position, (GteRotation *)&rotation, size, size, 0x80,
                              GetClut(0x30, palette), 1, 0x80, (struct RenderColor *)&color);
            }
            FieldFx_DrawStarFan(&state->position, 0x190, 0x190, 0xC, 0, size, size, (struct RenderColor *)&color, 0, 0x40, 1);
            FieldFx_DrawStarFan(&state->position, 0xBE, 0xBE, 8, 0, size, size, (struct RenderColor *)&color, 0, 0x80, 1);
            position.x = state->position.x;
            position.y = state->position.y;
            position.z = state->position.z;
            position.y = g_RoomFloorY->y;
            rotation.x = 0x400;
            rotation.pad = 1;
            {
                int kind = g_FieldFxSpriteParams.palette;
                int palette = g_FieldFxPaletteTable[kind];
                if (kind == 4 && g_FieldFxAltPaletteFlag) palette += 4;
                FieldFx_DrawShapeQuads(&position, (GteRotation *)&rotation, size, size, 0x80,
                              GetClut(0x30, palette), 1, 0x40, (struct RenderColor *)&color);
            }
        }
        rotation.x = 0;
        rotation.y = 0;
        rotation.z = g_FieldFxCurrentAge << 7;
        RotMatrix(&rotation, &spin);
        matrix = *ANIM_OWNER_AS(RoomM023BeaconChannel)->pool->matrix;
        MulMatrix0(&matrix, &spin, &matrix);
        scale.x = scale.y = state->width / 16;
        scale.z = (state->height << 12) / 8000;
        ScaleMatrix(&matrix, &scale);
        matrix.t[0] = state->position.x;
        matrix.t[1] = state->position.y;
        matrix.t[2] = state->position.z;
        {
            RenderMatrixSlot *slot;
            u16 *index = &g_FieldFxTpageSlot3;
            g_FieldFxSpriteParams.tpage = g_FieldFxTpageTable[*index];
            g_FieldFxSpriteParams.palette = 3;
            g_FieldFxSpriteParams.parameter06 = 0;
            slot = &g_ViewMatrixSlot;
            gte_ldrotmatrix(slot->value);
            gte_ldtransmatrix(slot->value);
            page = (u16)(g_FieldFxTpageTable[*index] | GetTPage(0, 1, 0, 0));
        }
        {
            int kind = g_FieldFxSpriteParams.palette;
            int palette = g_FieldFxPaletteTable[kind];
            if (kind == 4 && g_FieldFxAltPaletteFlag) palette += 4;
            FieldFx_ModelSetTexture(page, GetClut(0x50, palette));
        }
        FieldFx_ModelSetSemiTrans(1);
        FieldFx_ModelSaveColors(g_RoomM023BeaconModel);
        FieldFx_ModelDraw((union FieldTexturedModel *)g_RoomM023BeaconModel, &matrix);
        FieldFx_ModelRestoreColors(g_RoomM023BeaconModel);
        FieldFx_ModelSaveColors(g_RoomM023BeaconModel);
        scale.z = 0x1000;
        scale.y = 0x1800;
        scale.x = 0x1800;
        ScaleMatrix(&matrix, &scale);
        FieldFx_ModelShiftColors(g_RoomM023BeaconModel, -0xFF, -0x8C, -0xFF);
        FieldFx_ModelDraw((union FieldTexturedModel *)g_RoomM023BeaconModel, &matrix);
        FieldFx_ModelRestoreColors(g_RoomM023BeaconModel);
        FieldFx_ModelSaveColors(g_RoomM023BeaconModel);
        scale.z = 0x1000;
        scale.y = 0x800;
        scale.x = 0x800;
        matrix.t[1] = g_RoomFloorY->y;
        ScaleMatrix(&matrix, &scale);
        FieldFx_ModelScaleColors(g_RoomM023BeaconModel, 0x19);
        FieldFx_ModelDraw((union FieldTexturedModel *)g_RoomM023BeaconModel, &matrix);
        FieldFx_ModelRestoreColors(g_RoomM023BeaconModel);
        break;
    }
    return 0;
}

RoomM023Template g_RoomM023JointAnchor = {.vector={0,0,-70,0}};
RenderColor g_RoomM023RingBandColor = {100,200,10,0};
RenderColor g_RoomM023StarFanColor = {100,200,100,0};
char *g_RoomM023BeaconModel;
u8 g_RoomM023BeaconColorTrack[48] = {
    0,0,0,0,0,0,0,0, 96,255,96,8,0,0,0,0,
    255,255,255,8,0,0,0,0, 128,255,128,8,0,0,0,0,
    0,128,0,8,0,0,0,0, 0,0,0,0,0,0,0,0,
};
/* Original USA descriptor 80190720 and task script 80190670. */
u16 g_RoomM023EffectScript[63] = {
    5,0,0, 1,6,33, 1,1,4, 1,0,0, 2,0,0, 1,0,1, 2,0,0,
    4,12,0, 1,1,2, 1,0,0, 2,1,0, 1,0,1, 2,1,0, 4,38,0,
    1,1,1, 1,0,0, 2,2,0, 1,0,1, 2,2,0, 1,1,0, 0,0,0,
};
FieldFxTaskProgram g_RoomM023Effects = {
    .table={.callbacks={RoomM023_ScatterEmitter,RoomM023_JointGlow,RoomM023_JointBeacon},
            .sizes={sizeof(RoomM023ScatterState),8,sizeof(RoomM023Beacon)}},
    .initialize=RoomLib_SetTaskVar0,
    .script=g_RoomM023EffectScript,
};
