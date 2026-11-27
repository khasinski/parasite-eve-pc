/*
 * The paired glow: two paired light emitters (one announced with a sound)
 * scaled by a shared timer, and a glow sprite drawn over the owner that
 * grows and fades.
 *
 * room_m107, m111, m114, m118, m122 and scene_e11 to scene_e14 link the same
 * seventeen functions in this order, from the two class no-ops after
 * RoomLib_CloseTarget to the second particle tick, with one rotation seed
 * as their only read-only data; this unit is that object, compiled into
 * each of them. The glow sprite's record and packet and the emitters'
 * colour table are room data, named in each overlay's symbol file
 * (pe1/room_paired_glow.h).
 */

#include "common.h"
#include "pe1/field_actor.h"
#include "pe1/fieldfx_calls.h"
#include "pe1/room_floor.h"
#include "pe1/field_script_context.h"
#include "pe1/gte.h"
#include "pe1/room_effect_state.h"
#include "pe1/room_fx.h"
#include "pe1/room_paired_glow.h"

#include "render_layouts.h"
#include "room_lib.h"

/* Copied into the emitter transforms' stack frames. */
static const RoomFxSeed8 s_PairedGlowSeed = {{0x00, 0x04, 0, 0, 0, 0, 0, 0}};

void RoomLib_PairedGlowNop6(void) {
}

void RoomLib_PairedGlowNop0(void) {
}

/* Init of the glow sprite: binds the state to the owner's matrix words,
 * fills the room's sprite record and loads its packet. */
int RoomLib_InitPairedGlowSprite(void *object, void *unused, void *data) {
    RoomLibFxMatrixState *state=data;
    RoomLink *owner;
    RenderMatrix *src;

    FieldFx_SetScriptContext(state);
    owner = (RoomLink *)((FieldFxObjectPrefix *)object)->actor;
    state->link = owner;
    src = owner->p238;
    memcpy(&state->matrix,src,sizeof(state->matrix));
    state->cooldown = 0;
    state->hit = 0;
    state->reload = 0x1E;
    state->asset.index = Asset_SearchByKeyType(0x23);

    g_RoomPairedGlowSprite.offset = -0x12C;
    g_RoomPairedGlowSprite.depth = 0x80;
    g_RoomPairedGlowSprite.code = 0;
    g_RoomPairedGlowSprite.mode = 0;
    g_RoomPairedGlowSprite.r = 0x80;
    g_RoomPairedGlowSprite.g = 0x80;
    g_RoomPairedGlowSprite.b = 0x80;
    g_RoomPairedGlowSprite.zero = 0;
    g_RoomPairedGlowPacket = Asset_FindTable08ByU32Key(g_LoadedSceneAssetBlock[0], 0xCB8704);
    return 0;
}

int RoomLib_PairedGlowNopA(void *object, void *slot, void *data) {
    return 0;
}

/* Counts the state's timer down and re-arms it when the owner asks; ends
 * the object when the engine reports the effect done. */
int RoomLib_TickPairedGlowTimer(void *arg0, void *control, void *data) {
    RoomFxControl *arg1=control;
    RoomLibFxMatrixState *state=data;

    if (state->cooldown != 0) {
        state->cooldown--;
    }

    if (state->hit == 1) {
        int timer = state->cooldown;

        state->hit = 0;
        if (timer == 0) {
            state->cooldown = state->reload;
            FieldFx_TriggerActorHit(arg0);
        }
    }

    if (FieldFx_IsLastSlotHalted() == 1) {
        arg1->state = 2;
    }
    return 0;
}

int RoomLib_InitPairedEmitterWithSound(void *arg0, void *arg1, void *data) {
    RoomFxPairedEmitterState *state=data;
    RoomFxEmitterParams *primary;
    RoomFxEmitterParams *secondary;
    struct SceneAssetBlob *volatile *soundOwnerSlot;

    state->phase = 0;
    state->timer = 0;
    state->intensity = 0x80;
    primary = &state->primary;
    primary->mode = 0x10;
    primary->offset = 0;
    primary->intensity = 0x80;
    primary->color1[0] = *FieldFx_RegPtr(1);
    primary->color1[1] = *FieldFx_RegPtr(2);
    primary->color1[2] = *FieldFx_RegPtr(3);
    primary->color0[0] = *FieldFx_RegPtr(4);
    primary->color0[1] = *FieldFx_RegPtr(5);
    primary->color0[2] = *FieldFx_RegPtr(6);
    primary->extent0 = 0x4B0;
    primary->extent1 = 0x258;
    primary->source = state->sourceData;
    FieldFx_BuildRingGeometry(primary);

    secondary = &state->secondary;
    secondary->mode = 0x10;
    secondary->offset = 0;
    secondary->intensity = 0x80;
    secondary->color1[0] = *FieldFx_RegPtr(1) >> 1;
    secondary->color1[1] = *FieldFx_RegPtr(2) >> 1;
    secondary->color1[2] = *FieldFx_RegPtr(3) >> 1;
    secondary->color0[0] = *FieldFx_RegPtr(4) >> 1;
    secondary->color0[1] = *FieldFx_RegPtr(5) >> 1;
    secondary->color0[2] = *FieldFx_RegPtr(6) >> 1;
    secondary->extent0 = 0x4B0;
    secondary->extent1 = 0x12C;
    secondary->source = state->sourceData;
    FieldFx_BuildRingGeometry(secondary);

    soundOwnerSlot = g_LoadedSceneAssetBlock;
    if (*soundOwnerSlot != 0) {
        Akao_SendTableCommand(*soundOwnerSlot, 0x57E, 0, 0x80, 0x7F);
    }
    return 0;
}

typedef struct RoomPairedEmitterStack {
    RoomFxSeed8 seed;
    RoomSpriteMatrix base_matrix;
    RoomSpriteMatrix matrix;
    RoomFxVec4 transformed_scale;
    RoomFxVec4 scale;
} RoomPairedEmitterStack;

int RoomLib_UpdatePairedEmitter(void *arg0, void *control, void *data) {
    RoomFxControl *arg1=control;
    RoomFxPairedEmitterState *state=data;
    RoomPairedEmitterStack stack;
    RoomLibFxMatrixState *owner;
    RenderMatrix *view;
    unsigned char *table;

    owner = FieldFx_GetScriptContext();
    stack.seed = s_PairedGlowSeed;
    stack.base_matrix.m[2][2] = 0x1000;
    stack.base_matrix.m[1][1] = 0x1000;
    stack.base_matrix.m[0][0] = 0x1000;
    stack.base_matrix.t[2] = 0;
    stack.base_matrix.t[1] = 0;
    stack.base_matrix.t[0] = 0;
    stack.base_matrix.m[2][1] = 0;
    stack.base_matrix.m[2][0] = 0;
    stack.base_matrix.m[1][2] = 0;
    stack.base_matrix.m[1][0] = 0;
    stack.base_matrix.m[0][2] = 0;
    stack.base_matrix.m[0][1] = 0;

    memset(&stack.scale, 0, 0x10);
    stack.scale.x = state->timer * 2;
    stack.scale.y = state->timer * 2;
    stack.scale.z = state->timer * 2;
    stack.transformed_scale = stack.scale;
    ScaleMatrix(&stack.base_matrix, &stack.transformed_scale);
    RotMatrix(&stack.seed.vector, &stack.matrix);

    /* PSY-Q gte_CompMatrix shape: MulMatrix0 in place, then the translation
     * through RTV0TR into the long-vector slot. */
    gte_ldrotmatrix(&stack.base_matrix);
    gte_ldclmv(&stack.matrix);
    gte_rtir();
    gte_stclmv(&stack.matrix);
    gte_ldclmv(stack.matrix.m[0] + 1);
    gte_rtir();
    gte_stclmv(stack.matrix.m[0] + 1);
    gte_ldclmv(stack.matrix.m[0] + 2);
    gte_rtir();
    gte_stclmv(stack.matrix.m[0] + 2);
    gte_ldtransmatrix(&stack.base_matrix);
    gte_ldlv0(stack.matrix.t);
    gte_rtv0tr_mac();
    gte_stlvl(stack.matrix.t);

    FieldFx_SetSpriteBlendMode(2);

    table = g_RoomPairedGlowColorTable;
    view = owner->link->p238;
    FieldFx_InterpolateColorRamp(table, arg1->frame, (unsigned char *)&state->primary.color0);
    FieldFx_InterpolateColorRamp(table, arg1->frame, (unsigned char *)&state->secondary.color0);
    state->primary.intensity = state->intensity;
    stack.matrix.t[0] = view->translation[0];
    stack.matrix.t[1] = view->translation[1] - 0x64;
    stack.matrix.t[2] = view->translation[2];
    FieldFx_DrawShadedRing(&state->primary, &stack.matrix, 0);

    state->secondary.intensity = state->intensity;
    stack.matrix.t[0] = view->translation[0];
    stack.matrix.t[1] =
        g_RoomFloorY->y;
    stack.matrix.t[2] = view->translation[2];
    FieldFx_DrawShadedRing(&state->secondary, &stack.matrix, 0);
    return 0;
}

ROOMLIB_PARTICLE_TICK_A(RoomLib_TickPairedGlowA)

int RoomLib_PairedGlowNopB(void *object, void *slot, void *data) {
    return 0;
}

int RoomLib_PairedGlowNopC(void *object, void *slot, void *data) {
    return 0;
}

int RoomLib_PairedGlowNopD(void *object, void *slot, void *data) {
    return 0;
}

int RoomLib_ResetPairedGlowScale(void *object, void *slot, void *data) {
    RoomDoubleSpriteFxParams *state=data;
    state->scale=0x100;
    state->seed.vector.x=0;state->seed.vector.y=0;state->seed.vector.z=0;
    state->depth=1;
    return 0;
}

typedef struct RoomScaledSpriteStack {
    RoomSpriteMatrix matrix;
    RoomFxVec4 scale;
    RoomFxVec4 sourceScale;
} RoomScaledSpriteStack;

int RoomLib_DrawPairedGlowSprite(void *object, void *unused, void *data) {
    RoomDoubleSpriteFxParams *state=data;
    RoomScaledSpriteStack stack;
    RoomEnt *entity;
    RoomDoubleSpriteFxParams *workState;
    RoomLibFxMatrixState *clock;
    int handleA;
    int handleB;
    int textureWord;

    /* A mod can install this effect in a room whose assets lack the model. */
    if (g_RoomPairedGlowPacket == NULL) {
        return 0;
    }
    entity = (RoomEnt *)object;
    workState = state;
    clock = FieldFx_GetScriptContext();
    RotMatrix(&workState->seed.vector, &stack.matrix);

    stack.matrix.t[0] = (s16)(entity->link->pos[0] >> 16);
    stack.matrix.t[1] = (s16)(entity->link->pos[1] >> 16);
    stack.matrix.t[2] = (s16)(entity->link->pos[2] >> 16);

    memset(&stack.sourceScale, 0, 0x10);
    stack.sourceScale.x = workState->scale;
    stack.sourceScale.y = 0xC8;
    stack.sourceScale.z = workState->scale;
    stack.scale = stack.sourceScale;
    ScaleMatrix(&stack.matrix, &stack.scale);

    FieldFx_ModelScrollUv(g_RoomPairedGlowPacket, 0, 0);
    textureWord = 0;
    if (clock->asset.index == 0) {
        handleA = GetTPage(textureWord, 1, 0x340, 0x100);
        handleA = (u16)handleA;
        handleB = GetClut(0, 0x1D7);
        FieldFx_ModelSetTexture((u16)handleA, (u16)handleB);
    }
    if (clock->asset.index == 1) {
        handleA = GetTPage(textureWord, 1, 0x340, 0x160);
        textureWord = (u16)handleA;
        handleB = GetClut(0, 0x1DB);
        FieldFx_ModelSetTexture(textureWord, (u16)handleB);
    }
    FieldFx_ModelSetSemiTrans(1);
    FieldFx_ModelSaveColors(g_RoomPairedGlowPacket);
    FieldFx_ModelSetColors(g_RoomPairedGlowPacket, 0x60, 0x10, 0x80);
    FieldFx_ModelScaleColors(g_RoomPairedGlowPacket, workState->depth);
    FieldFx_ModelDraw(g_RoomPairedGlowPacket, &stack.matrix);
    FieldFx_ModelRestoreColors(g_RoomPairedGlowPacket);
    return 0;
}

/* Grows the glow sprite and fades it in for sixteen frames, then out. */
int RoomLib_UpdatePairedGlowSprite(void *unused, void *control, void *data) {
    RoomFxControl *state=control;
    RoomDoubleSpriteFxParams *work=data;
    work->scale += 0xF;

    if (state->frame < 0x10) {
        work->depth += 8;
    } else {
        work->depth -= 4;
    }

    work->seed.vector.y += 0x64;
    if ((s16)work->depth <= 0) {
        state->state = 2;
    }
    return 0;
}

int RoomLib_InitPairedEmitter(void *unused0, void *unused1, void *data) {
    RoomFxPairedEmitterState *state=data;
    int *value;
    unsigned char *source;

    state->phase = 0;
    state->timer = 0;
    state->intensity = 0x80;
    state->primary.mode = 0x10;
    state->primary.offset = 0;
    state->primary.intensity = 0x80;

    value = FieldFx_RegPtr(1);
    state->primary.color1[0] = *value;
    value = FieldFx_RegPtr(2);
    state->primary.color1[1] = *value;
    value = FieldFx_RegPtr(3);
    state->primary.color1[2] = *value;
    value = FieldFx_RegPtr(4);
    state->primary.color0[0] = *value;
    value = FieldFx_RegPtr(5);
    state->primary.color0[1] = *value;
    value = FieldFx_RegPtr(6);
    state->primary.color0[2] = *value;

    source = state->sourceData;
    state->primary.extent0 = 0x4B0;
    state->primary.extent1 = 0x258;
    state->primary.source = source;
    FieldFx_BuildRingGeometry(&state->primary);

    state->secondary.mode = 0x10;
    state->secondary.offset = 0;
    state->secondary.intensity = 0x80;

    value = FieldFx_RegPtr(1);
    state->secondary.color1[0] = *value >> 1;
    value = FieldFx_RegPtr(2);
    state->secondary.color1[1] = *value >> 1;
    value = FieldFx_RegPtr(3);
    state->secondary.color1[2] = *value >> 1;
    value = FieldFx_RegPtr(4);
    state->secondary.color0[0] = *value >> 1;
    value = FieldFx_RegPtr(5);
    state->secondary.color0[1] = *value >> 1;
    value = FieldFx_RegPtr(6);
    state->secondary.color0[2] = *value >> 1;

    state->secondary.extent0 = 0x4B0;
    state->secondary.extent1 = 0x12C;
    state->secondary.source = source;
    FieldFx_BuildRingGeometry(&state->secondary);
    return 0;
}

/* Retail marks this function handwritten; only its COP2 windows remain ASM. */
int RoomLib_TransformPairedEmitter(void *unused, void *control, void *data) {
    RoomFxControl *params=control;
    RoomFxPairedEmitterState *state=data;
    RoomLibSpriteTransformStack stack;
    RoomSpriteMatrix *scaleMatrix;
    unsigned char *table;
    RoomFxPairedEmitterState *workState;
    RoomSpriteMatrix *matrix;
    FieldActor **root;
    RoomFxControl *renderParams;
    RenderMatrix *owner;

    workState = state;
    root = FieldFx_GetScriptContext();
    stack.seed = s_PairedGlowSeed;

    stack.scaleMatrix.m[2][2] = 0x1000;
    stack.scaleMatrix.m[1][1] = 0x1000;
    stack.scaleMatrix.m[0][0] = 0x1000;
    stack.scaleMatrix.t[2] = 0;
    stack.scaleMatrix.t[1] = 0;
    stack.scaleMatrix.t[0] = 0;
    stack.scaleMatrix.m[2][1] = 0;
    stack.scaleMatrix.m[2][0] = 0;
    stack.scaleMatrix.m[1][2] = 0;
    stack.scaleMatrix.m[1][0] = 0;
    stack.scaleMatrix.m[0][2] = 0;
    stack.scaleMatrix.m[0][1] = 0;
    memset(&stack.sourceScale, 0, 0x10);

    stack.sourceScale.x = workState->timer * 2;
    stack.sourceScale.y = workState->timer * 2;
    stack.sourceScale.z = workState->timer * 2;
    stack.scale = stack.sourceScale;
    scaleMatrix = &stack.scaleMatrix;
    ScaleMatrix(scaleMatrix, &stack.scale);
    RotMatrix(&stack.seed.vector, &stack.matrix);
    matrix = &stack.matrix;

    gte_ldrotmatrix(scaleMatrix);
    gte_ldrtir12_matrix_column(&stack.matrix.m[0][0]);
    gte_stir123_column(matrix);
    gte_ldrtir12_matrix_column(&stack.matrix.m[0][1]);
    gte_stir123_column_at(&stack.matrix.m[0][1]);
    gte_ldrtir12_matrix_column(&stack.matrix.m[0][2]);
    gte_stir123_column_at(&stack.matrix.m[0][2]);
    gte_ldtransmatrix(scaleMatrix);
    {
        s32 *translation = stack.matrix.t;
        gte_ldv0_word3(translation);
        gte_cop2_hazard_slot();
        gte_cop2_hazard_slot();
        gte_rtv0tr_sf0();
        gte_swc2_9_0(translation);
        gte_swc2_10_4(translation);
        gte_swc2_11_8(translation);
    }

    renderParams = params;
    FieldFx_SetSpriteBlendMode(2);
    table = g_RoomPairedGlowColorTable;
    owner = (*root)->render_object.matrices;
    FieldFx_InterpolateColorRamp(table, renderParams->frame, workState->primary.color0);
    FieldFx_InterpolateColorRamp(table, renderParams->frame, workState->secondary.color0);
    workState->primary.intensity = workState->intensity;
    stack.matrix.t[0] = owner->translation[0];
    stack.matrix.t[1] = owner->translation[1] - 0x64;
    stack.matrix.t[2] = owner->translation[2];
    FieldFx_DrawShadedRing(&workState->primary, matrix, 0);
    return 0;
}

ROOMLIB_PARTICLE_TICK_A(RoomLib_TickPairedGlowB)

/* Original M107/M114 lists, script and colour ramp. The other instances
 * are registered only after their complete module and tables are audited. */
RoomFxSpritePacket g_RoomPairedGlowSprite;
void *g_RoomPairedGlowPacket;
u8 g_RoomPairedGlowColorTable[24]={100,50,220,10,170,40,250,14,100,60,200,8,10,0,170,30,0,0,0,200,0,0,0,255};
FieldFxHandler g_RoomInitList[]={RoomLib_InitPairedGlowSprite,RoomLib_InitPairedEmitterWithSound,
    RoomLib_PairedGlowNopB,RoomLib_ResetPairedGlowScale,RoomLib_InitPairedEmitter,(FieldFxHandler)(intptr_t)-1};
FieldFxHandler g_RoomDrawList[]={RoomLib_PairedGlowNopA,RoomLib_UpdatePairedEmitter,
    RoomLib_PairedGlowNopC,RoomLib_DrawPairedGlowSprite,RoomLib_TransformPairedEmitter,(FieldFxHandler)(intptr_t)-1};
FieldFxHandler g_RoomUpdateList[]={RoomLib_TickPairedGlowTimer,RoomLib_TickPairedGlowA,
    RoomLib_PairedGlowNopD,RoomLib_UpdatePairedGlowSprite,RoomLib_TickPairedGlowB,(FieldFxHandler)(intptr_t)-1};
/* Every allocation must preserve alignment for the next pointer-bearing
 * emitter/context. The sprite uses its original 20 bytes through depth. */
#define PAIRED_SIZE(bytes) (((bytes)+sizeof(void *)-1)&~(sizeof(void *)-1))
int g_RoomSpawnLayout[]={PAIRED_SIZE(sizeof(RoomLibFxMatrixState))|(PAIRED_SIZE(sizeof(RoomFxPairedEmitterState))<<16),
    PAIRED_SIZE(0x12c)|(PAIRED_SIZE(0x14)<<16),PAIRED_SIZE(sizeof(RoomFxPairedEmitterState))|0xffff0000u};
#undef PAIRED_SIZE
int g_RoomScript[]={0x00100019,0x00500000,0x0011003c,0x0012003c,0x00130040,0x001400aa,
    0x00150028,0x001600fa,0x00010000,0x00100000,0x1006000e,0x100b0010,0x100a0018,
    0x100b001e,0x00200001,0x00020000,0x30f90000,0x00010001,0x00020003,0x00010004,
    0x30f90000,0x00010002,0x30f70000,0x00010003,0x30f50000,-1};
PmCommand g_RoomPairedGlowHandlers={RoomLib_PairedGlowNop0,RoomLib_PlantScript,RoomLib_Spawn6,
    RoomLib_RegisterDrawList,RoomLib_RegisterPairedTables,RoomLib_CloseTarget,RoomLib_PairedGlowNop6};
