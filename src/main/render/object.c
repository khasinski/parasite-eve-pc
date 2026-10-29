#include "common.h"
#include <math.h>
#include <psyz/gpu.h>
#include "pe1/entity_frame_update.h"
#include "pe1/field_actor.h"
#include "pe1/gte.h"
#include "pe1/render_buffers.h"
#include "pe1/render_lighting.h"
#include "pe1/render_matrix_copy.h"
#include "pe1/render_object.h"
#include "pe1/render_setup.h"
#include "pe1/render_tint.h"

static void Render_RegisterInterpolationPackets(RenderObjectEntity *object,int reset);

/* Original USA resident model-rendering data. The historical "identity"
 * name denotes an X-reflecting basis: its first signed halfword is -4096. */
/* USA 0x8003E60C is a zero-initialized writable counter word. */
int g_RenderCounter[1] = {0};

s32 g_IdentityMatrixSource[8] = {61440,0,4096,0,4096,0,0,0};
s16 g_RenderTexturePageDeltas[4][4] = {
    {0,32,64,96},
    {-32,0,32,64},
    {-64,-32,0,32},
    {-96,-64,-32,0},
};
RenderVec3s g_RenderNormalTable[240] = {
    {4013,-70,818,0},
    {4013,-701,429,0},
    {4013,701,-429,0},
    {4013,70,-818,0},
    {4011,705,436,0},
    {4011,-705,-436,0},
    {3853,-1182,-730,0},
    {3853,1182,730,0},
    {3727,70,-1697,0},
    {3727,-1486,821,0},
    {3727,-70,1697,0},
    {3727,1486,-821,0},
    {3547,1182,1674,0},
    {3547,-1182,-1674,0},
    {3547,-2026,-308,0},
    {3547,2026,308,0},
    {3501,705,2005,0},
    {3501,-705,-2005,0},
    {3501,-2109,266,0},
    {3501,2109,-266,0},
    {3498,1486,-1526,0},
    {3498,701,-2012,0},
    {3498,-701,2012,0},
    {3498,-1486,1526,0},
    {3051,2547,991,0},
    {3051,2026,1835,0},
    {3051,-2547,-991,0},
    {3051,-2026,-1835,0},
    {2995,-2733,583,0},
    {2995,701,2705,0},
    {2995,2733,-583,0},
    {2995,-701,-2705,0},
    {2989,-705,2710,0},
    {2989,705,-2710,0},
    {2989,-2109,1843,0},
    {2989,2109,-1843,0},
    {2765,70,3021,0},
    {2765,-70,-3021,0},
    {2765,-2733,1288,0},
    {2765,2733,-1288,0},
    {2688,-2026,2334,0},
    {2688,-1182,2856,0},
    {2688,1182,-2856,0},
    {2688,2026,-2334,0},
    {2676,2109,2273,0},
    {2676,2976,870,0},
    {2676,-2976,-870,0},
    {2676,-2109,-2273,0},
    {2533,-3219,-52,0},
    {2533,3219,52,0},
    {2533,-1486,-2855,0},
    {2533,1486,2855,0},
    {2080,2733,2232,0},
    {2080,3219,1446,0},
    {2080,-3219,-1446,0},
    {2080,-2733,-2232,0},
    {2018,-3219,1531,0},
    {2018,70,3564,0},
    {2018,-70,-3564,0},
    {2018,3219,-1531,0},
    {1933,1486,3291,0},
    {1933,-1486,-3291,0},
    {1933,3608,-143,0},
    {1933,-3608,143,0},
    {1885,-1182,3439,0},
    {1885,1182,-3439,0},
    {1885,-2547,2595,0},
    {1885,2547,-2595,0},
    {1654,705,-3680,0},
    {1654,-2976,2277,0},
    {1654,2976,-2277,0},
    {1654,-705,3680,0},
    {1648,-701,-3684,0},
    {1648,-3608,1021,0},
    {1648,701,3684,0},
    {1648,3608,-1021,0},
    {1480,-2733,-2668,0},
    {1480,2733,2668,0},
    {1480,3608,1251,0},
    {1480,-3608,-1251,0},
    {1389,2026,-3278,0},
    {1389,-2026,3278,0},
    {1335,2109,3248,0},
    {1335,-2109,-3248,0},
    {1335,-3848,-434,0},
    {1335,3848,434,0},
    {832,3219,-2393,0},
    {832,-3219,2393,0},
    {832,701,-3949,0},
    {832,-701,3949,0},
    {829,2109,-3412,0},
    {829,-2109,3412,0},
    {825,3848,-1135,0},
    {825,-3848,1135,0},
    {825,705,3950,0},
    {825,-705,-3950,0},
    {803,-4008,-261,0},
    {803,4008,261,0},
    {803,2026,3468,0},
    {803,-2026,-3468,0},
    {733,-3608,-1794,0},
    {733,-3219,-2425,0},
    {733,3608,1794,0},
    {733,3219,2425,0},
    {496,-4008,683,0},
    {496,4008,-683,0},
    {496,1182,3890,0},
    {496,-1182,-3890,0},
    {462,70,-4069,0},
    {462,-70,4069,0},
    {462,3608,-1882,0},
    {462,-3608,1882,0},
    {371,2733,-3028,0},
    {371,1486,-3799,0},
    {371,-2733,3028,0},
    {371,-1486,3799,0},
    {0,-2976,-2814,0},
    {0,-2547,-3208,0},
    {0,2547,3208,0},
    {0,2976,2814,0},
    {0,3848,1403,0},
    {0,4008,844,0},
    {0,-4008,-844,0},
    {0,-3848,-1403,0},
    {-371,-1486,3799,0},
    {-371,1486,-3799,0},
    {-371,-2733,3028,0},
    {-371,2733,-3028,0},
    {-462,3608,-1882,0},
    {-462,-3608,1882,0},
    {-462,-70,4069,0},
    {-462,70,-4069,0},
    {-496,1182,3890,0},
    {-496,4008,-683,0},
    {-496,-4008,683,0},
    {-496,-1182,-3890,0},
    {-733,3219,2425,0},
    {-733,3608,1794,0},
    {-733,-3608,-1794,0},
    {-733,-3219,-2425,0},
    {-803,-2026,-3468,0},
    {-803,2026,3468,0},
    {-803,4008,261,0},
    {-803,-4008,-261,0},
    {-825,705,3950,0},
    {-825,-705,-3950,0},
    {-825,3848,-1135,0},
    {-825,-3848,1135,0},
    {-829,-2109,3412,0},
    {-829,2109,-3412,0},
    {-832,-701,3949,0},
    {-832,701,-3949,0},
    {-832,-3219,2393,0},
    {-832,3219,-2393,0},
    {-1335,3848,434,0},
    {-1335,-3848,-434,0},
    {-1335,2109,3248,0},
    {-1335,-2109,-3248,0},
    {-1389,-2026,3278,0},
    {-1389,2026,-3278,0},
    {-1480,3608,1251,0},
    {-1480,-3608,-1251,0},
    {-1480,2733,2668,0},
    {-1480,-2733,-2668,0},
    {-1648,-3608,1021,0},
    {-1648,3608,-1021,0},
    {-1648,701,3684,0},
    {-1648,-701,-3684,0},
    {-1654,-2976,2277,0},
    {-1654,2976,-2277,0},
    {-1654,-705,3680,0},
    {-1654,705,-3680,0},
    {-1885,-2547,2595,0},
    {-1885,2547,-2595,0},
    {-1885,-1182,3439,0},
    {-1885,1182,-3439,0},
    {-1933,3608,-143,0},
    {-1933,-3608,143,0},
    {-1933,1486,3291,0},
    {-1933,-1486,-3291,0},
    {-2018,-3219,1531,0},
    {-2018,3219,-1531,0},
    {-2018,70,3564,0},
    {-2018,-70,-3564,0},
    {-2080,2733,2232,0},
    {-2080,-2733,-2232,0},
    {-2080,-3219,-1446,0},
    {-2080,3219,1446,0},
    {-2533,1486,2855,0},
    {-2533,-3219,-52,0},
    {-2533,-1486,-2855,0},
    {-2533,3219,52,0},
    {-2676,2109,2273,0},
    {-2676,-2109,-2273,0},
    {-2676,-2976,-870,0},
    {-2676,2976,870,0},
    {-2688,-1182,2856,0},
    {-2688,2026,-2334,0},
    {-2688,-2026,2334,0},
    {-2688,1182,-2856,0},
    {-2765,70,3021,0},
    {-2765,-2733,1288,0},
    {-2765,2733,-1288,0},
    {-2765,-70,-3021,0},
    {-2989,-705,2710,0},
    {-2989,-2109,1843,0},
    {-2989,2109,-1843,0},
    {-2989,705,-2710,0},
    {-2995,-2733,583,0},
    {-2995,2733,-583,0},
    {-2995,-701,-2705,0},
    {-2995,701,2705,0},
    {-3051,-2026,-1835,0},
    {-3051,2026,1835,0},
    {-3051,2547,991,0},
    {-3051,-2547,-991,0},
    {-3498,701,-2012,0},
    {-3498,1486,-1526,0},
    {-3498,-701,2012,0},
    {-3498,-1486,1526,0},
    {-3501,705,2005,0},
    {-3501,-705,-2005,0},
    {-3501,-2109,266,0},
    {-3501,2109,-266,0},
    {-3547,2026,308,0},
    {-3547,-2026,-308,0},
    {-3547,1182,1674,0},
    {-3547,-1182,-1674,0},
    {-3727,-1486,821,0},
    {-3727,1486,-821,0},
    {-3727,70,-1697,0},
    {-3727,-70,1697,0},
    {-3853,1182,730,0},
    {-3853,-1182,-730,0},
    {-4011,705,436,0},
    {-4011,-705,-436,0},
    {-4013,-70,818,0},
    {-4013,-701,429,0},
    {-4013,701,-429,0},
    {-4013,70,-818,0},
};

/* Per-frame model transforms: bone matrices from the matrix command list,
 * the skinned hit volumes and the projected vertex buffers. */

/* Builds the object's bone matrices from its matrix command list (-1 pushes
 * the current GTE matrix, -2 pops it, any other value is a part index) and
 * transforms the hit-sphere centres of the visible parts. */


/* Places a skinned object's hit volumes and projects its origin and target
 * point; headerless objects take their transforms from their owner. */


/* Projects every vertex triple of the object's visible parts through the
 * view-composed part matrix into the screen and depth buffers. */


void Render_DrawEntity(RenderObjectEntity *object, void *viewMatrix) {
    u16 flags;
    if (object->header == 0 || object->draw_count == 0) return;
    if (object->flags_9C & 0x800) {
        Render_SetObjectTexturePage(object, 0);
        Render_SetEntityBlendMode(object, 0);
        object->flags_9C &= ~0x800;
    }
    if (object->flags_9C & 0x10)
        Render_DrawObjectVariant(object, object->script_value9a, viewMatrix);
    if (object->variant_visible == 1)
        Render_DrawTexturedQuads(object);
    flags = object->flags_9C;
    if (flags & 0x20) {
        Render_BuildEntityMatrix(object, g_ActiveDrawSlot);
        object->reserved9f = g_ActiveDrawSlot == 0;
        object->flags_9C = (object->flags_9C & ~0x20) | 0x40;
    } else if (flags & 0x40) {
        Render_BuildEntityMatrix(object, object->reserved9f);
        object->flags_9C &= ~0x40;
    }
    flags = object->flags_9C;
    if (flags & 2) {
        if ((s16)Render_ColorEntity(object))
            object->flags_9C = (object->flags_9C & ~2) | 0x200;
        if (object->flags_9C & 8)
            Render_DrawObjectAlt(object, object->script_value9a, object->script_param97,
                                 object->script_param98, object->script_param99);
    } else if (flags & 4) {
        if ((s16)Render_TickObject(object))
            object->flags_9C &= ~4;
        if (object->flags_9C & 8)
            Render_DrawObjectAlt(object, object->script_value9a, object->script_param97,
                                 object->script_param98, object->script_param99);
    } else if (flags & 8) {
        Render_DrawObject(object, &g_RenderLightingState);
        Render_DrawObjectAlt(object, object->script_value9a, object->script_param97,
                             object->script_param98, object->script_param99);
    } else if (flags & 1) {
        Render_DrawObject(object, &g_RenderLightingState);
        Render_UpdateClutTable(object, 0, g_ActiveDrawSlot);
    }
}

/* A GTE matrix copied as its eight 32-bit words. */
typedef union RenderMatrixWords {
    RenderMatrix matrix;
    s32 words[8];
} RenderMatrixWords;

void Render_BuildEntityMatrix(RenderObjectEntity *object, s16 bufferIndex)
{
    s32 saved[8];
    RenderMatrixWords *model;
    FieldActor *actor;
    s32 first;
    void *animation;
    RenderMatrixWords *player;

    if (object != &g_PlayerBattleEntity) {
        model = (RenderMatrixWords *)&object->model_matrix;
        first = model->words[0];
        saved[1] = model->words[1];
        saved[2] = model->words[2];
        saved[3] = model->words[3];
        saved[4] = model->words[4];
        saved[5] = model->words[5];
        saved[6] = model->words[6];
        saved[7] = model->words[7];
        animation = object->animation_data;
        saved[0] = first;
        model->words[0] = g_IdentityMatrixSource[0];
        model->words[1] = g_IdentityMatrixSource[1];
        model->words[2] = g_IdentityMatrixSource[2];
        model->words[3] = g_IdentityMatrixSource[3];
        model->words[4] = g_IdentityMatrixSource[4];
        model->words[5] = g_IdentityMatrixSource[5];
        model->words[6] = g_IdentityMatrixSource[6];
        model->words[7] = g_IdentityMatrixSource[7];
        Anim_BuildRotationMatrices(object, animation, 0);
        Render_TransformVertices(object);
        Render_InitRoomPrimState(object);
        Render_DrawObject(object, &g_RenderLightingState);
        Render_UpdateClutTable(object, 1, bufferIndex);
        model->words[0] = saved[0];
        model->words[1] = saved[1];
        model->words[2] = saved[2];
        model->words[3] = saved[3];
        model->words[4] = saved[4];
        model->words[5] = saved[5];
        model->words[6] = saved[6];
        model->words[7] = saved[7];
        actor = (FieldActor *)((char *)object - PE1_OFFSETOF(FieldActor, render_object));
        Anim_BuildRotationMatrices(object, actor->action_data, (s16)actor->anim.parts.integer);
        Render_TransformVertices(object);
        return;
    }
    player = (RenderMatrixWords *)g_PlayerBattleEntity.matrices;
    player->words[0] = g_IdentityMatrixSource[0];
    player->words[1] = g_IdentityMatrixSource[1];
    player->words[2] = g_IdentityMatrixSource[2];
    player->words[3] = g_IdentityMatrixSource[3];
    player->words[4] = g_IdentityMatrixSource[4];
    player->words[5] = g_IdentityMatrixSource[5];
    player->words[6] = g_IdentityMatrixSource[6];
    player->words[7] = g_IdentityMatrixSource[7];
    Render_InitRoomPrimState(object);
    Render_DrawObject(object, &g_RenderLightingState);
    Render_UpdateClutTable(object, 1, bufferIndex);
}

/* Colours the vertices of an object's visible parts that lie below a height
 * limit. GTE operations and their explicit nop hazard slots use individual
 * macros. */

#define Render_LoadObjectMatrix(matrix) \
    { \
        gte_ldrotmatrix((const GteMatrixWords *)(matrix)); \
        gte_ldtransmatrix((const GteMatrixWords *)(matrix)); \
    }

#define Render_TransformVertex(src, dst)                                                           \
    {                                                                                              \
        gte_ldv0(src);                                                                             \
        gte_rt();                                                                                  \
        gte_stsv(dst);                                                                             \
    }

void Render_DrawObjectAlt(RenderObjectEntity *entity, s16 limit, u8 red, u8 green, u8 blue) {
    RenderVec3s scratchStorage;
    RenderVec3s *scratch = &scratchStorage;
    RenderMatrix *matrix;
    int i, j;

    if (!entity->header || !entity->draw_count)
        return;
    if(Psyz_GpuGetFrameInterpolation())Render_RegisterInterpolationPackets(entity,0);
    matrix = entity->matrices;
    for (i = 0; i < entity->header->part_count; i++, matrix++) {
        RenderObjectPart *part;
        s32 y;

        if (entity->parts[i].visible != 1)
            continue;
        Render_LoadObjectMatrix(matrix);
        Render_TransformVertex(&entity->bounds_vertices[i * 2], scratch);
        part = &entity->parts[i];
        y = scratch->y;
        if (limit < y || limit < y - entity->bounds_vertices[i * 2].pad ||
            limit < y + entity->bounds_vertices[i * 2].pad) {
            RenderVec3s *vertex = &entity->vertices[part->vertex_start];
            RenderColor *colour =
                (RenderColor *)&g_RenderClutLookupTable[part->vertex_start];

            for (j = 0; j < part->vertex_count; colour++) {
                /* Retail advances the counters while the GTE works. */
                gte_ldv0(vertex);
                gte_rt();
                j++;
                vertex++;
                gte_stsv(scratch);
                if (limit < scratch->y) {
                    colour->r = red;
                    colour->g = green;
                    colour->b = blue;
                }
            }
        }
    }
    Render_UpdateClutTable(entity, 0, g_ActiveDrawSlot);
}
#undef Render_LoadObjectMatrix
#undef Render_TransformVertex

#define Render_LoadObjectMatrix(matrix) \
    { \
        gte_ldrotmatrix((const GteMatrixWords *)(matrix)); \
        gte_ldtransmatrix((const GteMatrixWords *)(matrix)); \
    }

#define Render_TransformVertex(src, dst)                                                           \
    {                                                                                              \
        gte_ldv0(src);                                                                             \
        gte_rt();                                                                                  \
        gte_stsv(dst);                                                                             \
    }

void Render_DrawObjectVariant(RenderObjectEntity *input, s16 limit, s32 *projectionMatrix) {
    RenderObjectEntity *entity = input;
    s32 *projectMatrix = projectionMatrix;
    s16 savedLimit = limit;
    s32 changed;
    RenderVec3s scratchStorage;
    RenderVec3s *scratch = &scratchStorage;
    u32 *projected;
    s32 *matrix;
    s32 i;
    s32 offset;
    s32 threshold;
    RenderObjectPart *part;
    RenderObjectPart *probe;
    s32 boundsOffset;
    RenderVec3s *bounds;
    s32 y;
    s32 radius;
    s32 sum;
    RenderVec3s *vertices;
    u32 *clut;
    s32 vertexIndex;
    s32 limitShift;
    changed = 0;
    u32 projectedStorage;
    projected = &projectedStorage;
    if (!entity->header || !entity->draw_count)
        return;
    if(Psyz_GpuGetFrameInterpolation())Render_RegisterInterpolationPackets(entity,0);
    matrix = (s32 *)entity->matrices;
    i = 0;
    if (changed < entity->header->part_count) {
        limitShift = (s32)((u32)(s32)limit << 16);
        threshold = limitShift >> 16;
        offset = 0;
        do {
            {
                RenderObjectPart *base = entity->parts;
                probe = (RenderObjectPart *)((u8 *)base + offset);
            }
            if (probe->visible == 1) {
                part = probe;
                Render_LoadObjectMatrix(matrix);
                boundsOffset = i * 16;
                bounds = (RenderVec3s *)((u8 *)entity->bounds_vertices + boundsOffset);
                Render_TransformVertex(bounds, scratch);
                {
                    u16 yy = (u16)scratch->y;
                    y = (s16)yy;
                }
                if (threshold < y ||
                    (bounds = (RenderVec3s *)((u8 *)entity->bounds_vertices + boundsOffset),
                     radius = bounds->pad,
                     threshold < y - radius || threshold < (sum = y + radius))) {
                    scratch->y = savedLimit;
                    Render_LoadObjectMatrix(projectMatrix);
                    gte_lwc2_0_0(scratch);
                    gte_lwc2_1_4(scratch);
                    gte_cop2_hazard_slot();
                    gte_cop2_hazard_slot();
                    gte_rtps_command();
                    gte_stsxy2(projected);
                    vertexIndex = 0;
                    {
                        s32 first = part->vertex_start;
                        RenderVec3s *base = entity->vertices;
                        s32 vertexOffset = first * 8;
                        vertices = (RenderVec3s *)((u8 *)base + vertexOffset);
                        first <<= 2;
                        clut = (u32 *)((u8 *)D_800B1640 + first);
                    }
                    Render_LoadObjectMatrix(matrix);
                    for (; vertexIndex < part->vertex_count; vertices++) {
                        gte_lwc2_0_0(vertices);
                        gte_lwc2_1_4(vertices);
                        gte_cop2_hazard_slot();
                        gte_cop2_hazard_slot();
                        gte_mvmva_rotation_v0_translation_sf12();
                        vertexIndex++;
                        clut++;
                        gte_stsv(scratch);
                        if (threshold < scratch->y) {
                            *clut = *projected;
                            changed++;
                        }
                    }
                }
            }
            offset += sizeof(RenderObjectPart);
            i++;
            matrix += 8;
        } while (i < entity->header->part_count);
    }
    if (changed == entity->header->visible_part_count || (entity->flags_9C & 0x200))
        entity->variant_visible = 0;
    else
        entity->variant_visible = 1;
}



/* Adjacent object updates use the same animation and colour state. */


int Render_TickObject(RenderObjectEntity *object) {
    int state;
    u8 r, g, b;
    if (!object->header || !object->draw_count) return 1;
    state = object->fade_remaining;
    if (!state) {
        object->fade_remaining = -1;
        Render_SetObjectTexturePage(object, 0);
        Render_InitRoomPrimState(object);
        Render_SetEntityBlendMode(object, 0);
        object->flags_9C |= 0x820;
        return 1;
    }
    if (state < 0) {
        object->variant_visible = 1;
        object->flags_9C &= ~0x200;
        Render_SetObjectTexturePage(object, 1);
        Render_SetEntityBlendMode(object, 1);
        r = object->primitive_red;
        g = object->primitive_green;
        b = object->primitive_blue;
        object->fade_red = 0;
        object->fade_green = 0;
        object->fade_blue = 0;
        Render_FadeEntityColor(object, 0, 0, 0);
        object->primitive_red = r;
        object->primitive_green = g;
        object->primitive_blue = b;
        object->fade_remaining = object->fade_duration;
    } else if (state == object->fade_duration - 1) {
        Render_SetObjectTexturePage(object, 1);
        Render_SetEntityBlendMode(object, 1);
    }
    g_RenderFadeColour = (object->fade_blue << 16) | (object->fade_green << 8) | object->fade_red;
    Render_DrawObject(object, &g_RenderLightingState);
    if (!(object->flags_9C & 8)) Render_UpdateClutTable(object, 0, (s16)g_ActiveDrawSlot);
    g_RenderFadeColour = 0x808080;
    object->fade_red += object->fade_red_step;
    object->fade_green += object->fade_green_step;
    object->fade_blue += object->fade_blue_step;
    object->fade_remaining--;
    return 0;
}

int Render_ColorEntity(RenderObjectEntity *object) {
    int state;
    if (!object->header || !object->draw_count) return 1;
    state = object->fade_remaining;
    if (!state) {
        object->fade_remaining = -1;
        g_RenderFadeColour = 0x808080;
        return 1;
    }
    if (state == 1) {
        object->variant_visible = 0;
    } else if (state < 0) {
        object->fade_remaining = object->fade_duration;
        Render_SetEntityBlendMode(object, 1);
        Render_SetObjectTexturePage(object, 1);
        if (object->primitive_red > 128) object->primitive_red = 128;
        if (object->primitive_green > 128) object->primitive_green = 128;
        if (object->primitive_blue > 128) object->primitive_blue = 128;
        object->fade_red_step = object->primitive_red / object->fade_duration;
        object->fade_green_step = object->primitive_green / object->fade_duration;
        object->fade_blue_step = object->primitive_blue / object->fade_duration;
        object->fade_red = object->primitive_red;
        object->fade_green = object->primitive_green;
        object->fade_blue = object->primitive_blue;
    } else if (state == object->fade_duration - 1) {
        Render_SetEntityBlendMode(object, 1);
        Render_SetObjectTexturePage(object, 1);
    } else {
        object->fade_red -= object->fade_red_step;
        if (object->fade_red > 128) object->fade_red = 0;
        object->fade_green -= object->fade_green_step;
        if (object->fade_green > 128) object->fade_green = 0;
        object->fade_blue -= object->fade_blue_step;
        if (object->fade_blue > 128) object->fade_blue = 0;
    }
    g_RenderFadeColour = (object->fade_blue << 16) | (object->fade_green << 8) | object->fade_red;
    Render_DrawObject(object, &g_RenderLightingState);
    if (!(object->flags_9C & 8)) Render_UpdateClutTable(object, 0, (s16)g_ActiveDrawSlot);
    g_RenderFadeColour = 0x808080;
    object->fade_remaining--;
    return 0;
}

/* Object fade and blend commands update the same colour packet state. */

void Render_FadeEntityColor(RenderObjectEntity *object, int r, int g, int b) {
    u8 *prim;
    u32 color;
    int i;
    int slot = g_ActiveDrawSlot;
    u8 opcode;

    if (!object->header || !object->draw_count) {
        return;
    }
    color = ((b & 255) << 16) | ((g & 255) << 8) | (r & 255);
    object->primitive_red = r;
    object->primitive_green = g;
    object->primitive_blue = b;
    prim = object->primitive_buffer;
    for (i = -1; ++i < object->header->packet34_count;
         prim += 2 * sizeof(RenderPacket34)) {
        RenderPacket34 *packet = (RenderPacket34 *)prim + slot;
        opcode = packet->values0.bytes.command;
        packet->values0.value = color;
        packet->value1 = color;
        packet->value2 = color;
        packet->value3 = color;
        packet->values0.bytes.command = opcode;
    }
    for (i = -1; ++i < object->header->packet28_count;
         prim += 2 * sizeof(RenderPacket28)) {
        RenderPacket28 *packet = (RenderPacket28 *)prim + slot;
        opcode = packet->values0.bytes.command;
        packet->values0.value = color;
        packet->value1 = color;
        packet->value2 = color;
        packet->values0.bytes.command = opcode;
    }
    for (i = -1; ++i < object->header->packet24_count;
         prim += 2 * sizeof(RenderPacket24)) {
        RenderPacket24 *packet = (RenderPacket24 *)prim + slot;
        opcode = packet->values0.bytes.command;
        packet->values0.value = color;
        packet->value1 = color;
        packet->value2 = color;
        packet->value3 = color;
        packet->values0.bytes.command = opcode;
    }
    for (i = -1; ++i < object->header->packet1c_count;
         prim += 2 * sizeof(RenderPacket1C)) {
        RenderPacket1C *packet = (RenderPacket1C *)prim + slot;
        opcode = packet->values0.bytes.command;
        packet->values0.value = color;
        packet->value1 = color;
        packet->value2 = color;
        packet->values0.bytes.command = opcode;
    }
}

static inline int primitiveBlendMode(int kind, int forcedKind, s16 mode) {
    if (kind == forcedKind) {
        return 1;
    }
    return mode;
}

static inline void setPrimitiveBlend(u8 *command, int enabled) {
    if (enabled) {
        *command |= 2;
    } else {
        *command &= ~2;
    }
}

void Render_SetEntityBlendMode(RenderObjectEntity *object, int mode) {
    int slot = g_ActiveDrawSlot;
    s16 savedMode = mode;
    int i;
    u8 *prim;
    RenderPrimitiveDescriptor *descriptor;

    if (!object->header || !object->draw_count) {
        return;
    }
    descriptor = object->primitive_descriptors;
    prim = object->primitive_buffer;
    for (i = 0; i < object->header->packet34_count;
         prim += 2 * sizeof(RenderPacket34), ++i, ++descriptor) {
        RenderPacket34 *packet;
        int enabled = primitiveBlendMode(descriptor->kind, 11, savedMode);
        packet = (RenderPacket34 *)prim + slot;
        setPrimitiveBlend(&packet->values0.bytes.command, enabled);
    }
    for (i = 0; i < object->header->packet28_count;
         prim += 2 * sizeof(RenderPacket28), ++i, ++descriptor) {
        RenderPacket28 *packet;
        int enabled = primitiveBlendMode(descriptor->kind, 16, savedMode);
        packet = (RenderPacket28 *)prim + slot;
        setPrimitiveBlend(&packet->values0.bytes.command, enabled);
    }
    for (i = 0; i < object->header->packet24_count;
         prim += 2 * sizeof(RenderPacket24), ++i, ++descriptor) {
        RenderPacket24 *packet;
        int enabled = primitiveBlendMode(descriptor->kind, 21, savedMode);
        packet = (RenderPacket24 *)prim + slot;
        setPrimitiveBlend(&packet->values0.bytes.command, enabled);
    }
    for (i = 0; i < object->header->packet1c_count;
         prim += 2 * sizeof(RenderPacket1C), ++i, ++descriptor) {
        RenderPacket1C *packet;
        int enabled = primitiveBlendMode(descriptor->kind, 26, savedMode);
        packet = (RenderPacket1C *)prim + slot;
        setPrimitiveBlend(&packet->values0.bytes.command, enabled);
    }
}


/* Texture-page transition deltas, indexed by old and requested page. */

/* Moves every packet of the object to texture page `page`: the current page
 * is read from the first packet and the per-page delta added to all. */
void Render_SetObjectTexturePage(RenderObjectEntity *obj, s32 page) {
    int slot = g_ActiveDrawSlot;
    int current = 0;
    RenderPrimCursor prim;
    int delta;
    int i;

    if (obj->header == NULL || obj->draw_count == 0)
        return;
    prim.bytes = obj->primitive_buffer;
    if (obj->header->packet34_count != 0)
        current = (prim.p34[slot].page_bits & 0x7F) >> 5;
    else if (obj->header->packet28_count != 0)
        current = (prim.p28[slot].page_bits & 0x7F) >> 5;
    if ((s16)page == current)
        return;
    delta = g_RenderTexturePageDeltas[current][(s16)page];
    for (i = 0; i < obj->header->packet34_count; i++) {
        RenderPacket34 *packet = &prim.p34[slot];

        packet->page_bits += delta;
        prim.p34 += 2;
    }
    for (i = 0; i < obj->header->packet28_count; i++) {
        RenderPacket28 *packet = &prim.p28[slot];

        packet->page_bits += delta;
        prim.p28 += 2;
    }
}

/* Parses a model block into the render object: section pointers, the
 * double-buffered GPU packets (command codes and texture coordinates), the
 * animated part lookup entries and one identity matrix per part. */


int Render_ReturnTrue(void *unused);

int Render_ReturnTrue(void *unused) {
    return 1;
}

/* Draws an object with an optional animation frame applied on top of its
 * saved model matrix, then refreshes the packets of both draw buffers. */
void Render_DrawWithAnim(RenderObjectEntity *obj, void *animation, s16 frame,
                         union RenderLightingMatrix *light) {
    if (animation != 0) {
        obj->animation_data = animation;
        Render_CopyMatrixBlock(&obj->model_matrix, (RenderMatrix *)g_RenderClutLookupTable, 1);
        Render_CopyMatrixBlock((RenderMatrix *)g_IdentityMatrixSource, &obj->model_matrix, 1);
        Anim_BuildRotationMatrices(obj, animation, frame);
    }
    Render_TransformVertices(obj);
    Render_CopyMatrixBlock((RenderMatrix *)g_RenderClutLookupTable, &obj->model_matrix, 1);
    Render_DrawObject(obj, light);
    Render_UpdateClutTable(obj, 1, g_ActiveDrawSlot);
    g_ActiveDrawSlot ^= 1;
    Render_UpdateClutTable(obj, 1, g_ActiveDrawSlot);
    g_ActiveDrawSlot ^= 1;
}

/* Equivalent branches preserve the stock compiler\'s allocation.
 * No packet operation depends on which of these identical branches is taken. */







/* Bounds vectors come in pairs per table entry; the first one stores the
 * hit radius in its pad word. */
void Render_InitObjectFromTable(RenderObjectEntity *obj, RenderObjectEntity *owner, int index) {
    obj->table_index = index;
    obj->header = 0;
    obj->animation_source = owner;
    obj->hit_cylinder.radius = owner->bounds_vertices[(short)index * 2].pad;
    obj->table_value2c = owner->bounds_vertices[(short)index * 2].x;
    obj->table_value2e = owner->bounds_vertices[(short)index * 2].y;
    obj->table_value30 = owner->bounds_vertices[(short)index * 2].z + obj->hit_cylinder.radius;
}

void Render_Noop(void) {
}

int Render_ReturnZero(void) {
    return 0;
}

void Render_CopyMatrixBlock(RenderMatrix *src, RenderMatrix *dst, s16 count) {
    int i;

    for (i = 0; i < count; i++, src++, dst++) {
        dst->rotation[0][0] = src->rotation[0][0];
        dst->rotation[0][1] = src->rotation[0][1];
        dst->rotation[0][2] = src->rotation[0][2];
        dst->rotation[1][0] = src->rotation[1][0];
        dst->rotation[1][1] = src->rotation[1][1];
        dst->rotation[1][2] = src->rotation[1][2];
        dst->rotation[2][0] = src->rotation[2][0];
        dst->rotation[2][1] = src->rotation[2][1];
        dst->rotation[2][2] = src->rotation[2][2];
        dst->translation[0] = src->translation[0];
        dst->translation[1] = src->translation[1];
        dst->translation[2] = src->translation[2];
    }
}

void Render_SetObjectAnim(RenderObjectEntity *arg0, RenderObjectEntity *arg1, short arg2) {
    if (arg0->header->part_count == 2) {
        arg0->animation_source = arg1;
        arg0->animation_state = 3;
    } else {
        arg0->animation_source = arg1;
        arg0->animation_state = 1;
    }
    arg0->animation_id = arg2;
}

void Render_ClearObjectAnim(RenderObjectEntity *arg0) {
    arg0->animation_source = 0;
    if (arg0->header->part_count == 2) {
        arg0->animation_state = 2;
    } else {
        arg0->animation_state = 0;
    }
}

int Render_FindAnimEntry(RenderObjectEntity *obj, int id, s32 *out) {
    RenderAnimationLookupEntry *entry = obj->animation_entries;
    int i;

    for (i = 0; i < obj->header->animation_entry_count; i++, entry++) {
        if (entry->animation_id == (s16)id) {
            out[0] = entry->value0;
            out[1] = entry->value1;
            out[2] = entry->value2;
            return 1;
        }
    }
    return 0;
}

void Render_OffsetObjectTextureCoordinates(RenderObjectEntity *object, int du, int dv, int clutOffset) {
    u8 savedClut = clutOffset;
    RenderPacket34 *quad = (RenderPacket34 *)object->primitive_buffer;
    RenderPacket28 *tri;
    int i, j;
    u8 triClut;
    for (i = 0; i < object->header->packet34_count; i++) {
        for (j = 0; j < 2; j++, quad++) {
            quad->v0 += dv;
            quad->u0 += du;
            quad->v1 += dv;
            quad->u1 += du;
            quad->v2 += dv;
            quad->u2 += du;
            /* Retail leaves u3 unchanged. */
            quad->v3 += dv;
            quad->clut += (s8)clutOffset;
        }
    }
    triClut = savedClut;
    /* Byte-sized copies preserve the two signed CLUT conversions in plain C. */
    tri = (RenderPacket28 *)quad;
    for (i = 0; i < object->header->packet28_count; i++) {
        for (j = 0; j < 2; j++, tri++) {
            tri->v0 += dv;
            tri->u0 += du;
            tri->v1 += dv;
            tri->u1 += du;
            tri->v2 += dv;
            tri->u2 += du;
            tri->clut += (s8)triClut;
        }
    }
}

/* Increment the original 32-bit resident counter with PS1 wraparound. */
void Render_IncrementCounter(void) {
    int *ptr;
    int value;

    ptr = g_RenderCounter;
    value = *ptr;
    value = (int)((u32)value + 1u);
    *ptr = value;
}

void Anim_SetInterpRate(RenderObjectEntity *obj, int arg1) {
    short value = arg1;
    int divisor;

    if ((short)arg1 == 0) {
        value = 1;
    }
    divisor = 0x80 / value;

    obj->fade_duration = value;
    obj->fade_red_step = divisor;
    obj->fade_green_step = divisor;
    obj->fade_blue_step = divisor;
}

/* Presentation metadata only; surviving actors can acquire fresh history
 * after a room flush without rebuilding or modifying their native packets. */
static void Render_RegisterInterpolationPackets(RenderObjectEntity *object,int reset) {
    if(!object->header||!object->primitive_buffer)return;
    unsigned char *packet=object->primitive_buffer;
    const RenderObjectHeader *header=object->header;
    const unsigned counts[]={header->packet34_count,header->packet28_count,header->packet24_count,header->packet1c_count};
    const size_t sizes[]={sizeof(RenderPacket34),sizeof(RenderPacket28),sizeof(RenderPacket24),sizeof(RenderPacket1C)};
    for(unsigned kind=0;kind<4;++kind)for(unsigned face=0;face<counts[kind];++face){
        if(reset)Psyz_GpuRegisterInterpolationPair(packet+sizeof(RenderNativeTag),packet+sizes[kind]+sizeof(RenderNativeTag));
        else Psyz_GpuEnsureInterpolationPair(packet+sizeof(RenderNativeTag),packet+sizes[kind]+sizeof(RenderNativeTag));
        packet+=2*sizes[kind];
    }
}

int Render_SetupEntityPrims(RenderObjectEntity *obj, RenderObjectHeader *model,
                            u8 *packetBuffer, s16 x, s16 y, s16 unused,
                            s16 paletteRow, s16 initCount, s8 **textureOut,
                            int setup)
{
    RenderModelCursor src;
    RenderPrimitiveDescriptor *desc;
    RenderPrimCursor prim;
    RenderModelCursor cursor;
    RenderObjectHeader *header;
    RenderAnimationLookupEntry *entry;
    RenderMatrix *matrix;
    int bytes, words;
    s16 skipped;
    s16 i, j;

    cursor.header = model;
    obj->header = model;
    header = model;
    i = 0;
    skipped = 0;
    cursor.header++;
    obj->parts = cursor.parts;
    cursor.parts += model->part_count;
    obj->vertices = cursor.vectors;
    cursor.vectors += header->vertex_count;
    obj->vertex_colours = cursor.colours;
    cursor.colours += header->vertex_count;
    obj->primitive_descriptors = cursor.descriptors;
    prim.bytes = packetBuffer;
    obj->primitive_buffer = packetBuffer;
    desc = cursor.descriptors;
    obj->draw_count = setup;

    if (setup) {
        for (; i < header->packet34_count; i++, desc++) {
            prim.head->length = 12;
            prim.head->colour.bytes.command = 0x3C;
            if (desc->kind == 0x0B)
                prim.head->colour.bytes.command = 0x3E;
            prim.p34++;
            prim.head->length = 12;
            prim.head->colour.bytes.command = 0x3C;
            if (desc->kind == 0x0B)
                prim.head->colour.bytes.command = 0x3E;
            prim.p34++;
        }
        for (i = 0; i < header->packet28_count; i++, desc++) {
            prim.head->length = 9;
            prim.head->colour.bytes.command = 0x34;
            if (desc->kind == 0x10)
                prim.head->colour.bytes.command = 0x36;
            prim.p28++;
            prim.head->length = 9;
            prim.head->colour.bytes.command = 0x34;
            if (desc->kind == 0x10)
                prim.head->colour.bytes.command = 0x36;
            prim.p28++;
        }
        for (i = 0; i < header->packet24_count; i++, desc++) {
            prim.head->length = 8;
            prim.head->colour.bytes.command = 0x38;
            if (desc->kind == 0x15)
                prim.head->colour.bytes.command = 0x3A;
            prim.p24++;
            prim.head->length = 8;
            prim.head->colour.bytes.command = 0x38;
            if (desc->kind == 0x15)
                prim.head->colour.bytes.command = 0x3A;
            prim.p24++;
        }
        for (i = 0; i < header->packet1c_count; i++, desc++) {
            prim.head->length = 6;
            prim.head->colour.bytes.command = 0x30;
            if (desc->kind == 0x1A)
                prim.head->colour.bytes.command = 0x32;
            prim.p1c++;
            prim.head->length = 6;
            prim.head->colour.bytes.command = 0x30;
            if (desc->kind == 0x1A)
                prim.head->colour.bytes.command = 0x32;
            prim.p1c++;
        }
    } else {
        desc += header->packet34_count + header->packet28_count
              + header->packet24_count + header->packet1c_count;
    }
    cursor.descriptors = desc;

    obj->model_section14 = cursor.vectors;
    cursor.vectors += 2;
    obj->bounds_vertices = cursor.vectors;
    cursor.vectors += header->part_count * 2;
    obj->projection_origin = cursor.vectors;
    cursor.vectors++;
    obj->matrix_commands = cursor.commands;
    /* Matrix command block, rounded up to whole words. */
    bytes = header->matrix_command_bytes;
    words = (u16)bytes;
    words >>= 2;
    if ((bytes & 3) > 0) {
        bytes = words;
        bytes++;
        bytes *= 4;
    } else {
        bytes = words;
        bytes *= 4;
    }
    cursor.commands += bytes;

    if (setup) {
        Render_RegisterInterpolationPackets(obj,1);
        RenderPrimCursor out;

        src = cursor;
        *textureOut = src.commands;
        out.bytes = obj->primitive_buffer;
        for (i = 0; i < header->packet34_count; i++) {
            src.quad = cursor.quad;

            for (j = 0; j < 2; j++) {
                out.p34->u0 = src.quad->u0;
                out.p34->v0 = src.quad->v0;
                out.p34->u1 = src.quad->u1;
                out.p34->v1 = src.quad->v1;
                out.p34->u2 = src.quad->u2;
                out.p34->v2 = src.quad->v2;
                out.p34->u3 = src.quad->u3;
                out.p34->v3 = src.quad->v3;
                out.p34->clut = src.quad->clut;
                out.p34->page_bits = src.quad->page_bits;
                out.p34++;
            }
            cursor.quad++;
        }
        for (i = 0; i < header->packet28_count; i++) {
            src.tri = cursor.tri;

            for (j = 0; j < 2; j++) {
                out.p28->u0 = src.tri->u0;
                out.p28->v0 = src.tri->v0;
                out.p28->u1 = src.tri->u1;
                out.p28->v1 = src.tri->v1;
                out.p28->u2 = src.tri->u2;
                out.p28->v2 = src.tri->v2;
                out.p28->clut = src.tri->clut;
                out.p28->page_bits = src.tri->page_bits;
                out.p28++;
            }
            cursor.tri++;
        }
        for (i = 0; i < header->packet24_count; i++) {
        }
        for (i = 0; i < header->packet1c_count; i++) {
        }
        if (initCount > 0)
            Render_InitPrimBlock(obj, x, y, unused, paletteRow);
    }

    {
        RenderVec3s *section = obj->model_section14;

        obj->hit_cylinder.radius = section->pad;
    }
    obj->hit_cylinder.bounds_value1 = obj->projection_origin->pad;
    obj->animation_entries = (RenderAnimationLookupEntry *)prim.head;
    entry = (RenderAnimationLookupEntry *)prim.head;
    for (i = 0; i < obj->header->part_count; i++) {
        if (obj->parts[i].visible == 1) {
            RenderVec3s *bounds = &obj->bounds_vertices[i * 2];

            if (bounds[1].pad >= 0) {
                entry->radius = bounds[0].pad;
                entry->bounds_value1 = bounds[1].pad;
                entry->animation_id = i;
                entry++;
            }
        } else {
            skipped++;
        }
    }

    obj->matrices = (RenderMatrix *)entry;
    /* A new model must not retain a decoded pose in an old allocation. */
    obj->active_matrix = NULL;
    matrix = (RenderMatrix *)entry;
    for (i = 0; i < obj->header->part_count;) {
        i++;
        matrix->rotation[0][0] = 0x1000;
        matrix->rotation[0][1] = 0;
        matrix->rotation[0][2] = 0;
        matrix->rotation[1][0] = 0;
        matrix->rotation[1][1] = 0x1000;
        matrix->rotation[1][2] = 0;
        matrix->rotation[2][0] = 0;
        matrix->rotation[2][1] = 0;
        matrix->rotation[2][2] = 0x1000;
        matrix->translation[0] = 0;
        matrix->translation[1] = 0;
        matrix->translation[2] = 0;
        matrix++;
    }

    obj->header->shadow_radius = 0;
    obj->table_value2c = 0;
    obj->table_value2e = 0;
    obj->table_value30 = 0;
    obj->table_index = 1;
    obj->table_value2c = 0;
    obj->table_value2e = 0;
    obj->table_value30 = 0;
    RotMatrix((GteShortVector *)&obj->table_value2c, (GteMatrix *)&obj->model_matrix);
    obj->fade_remaining = -1;
    Anim_SetInterpRate(obj, 0x32);
    obj->primitive_red = 0x80;
    obj->primitive_green = 0x80;
    obj->primitive_blue = 0x80;
    obj->flags_9C = 0;
    obj->variant_visible = 1;
    obj->reserved9f = g_ActiveDrawSlot;
    obj->animation_state = 0;
    obj->animation_source = 0;
    obj->animation_id = 0;
    obj->header->visible_part_count = obj->header->vertex_count - skipped;
    obj->hit_cylinder.animation_id = obj->projection_origin->y + (obj->hit_cylinder.bounds_value1 >> 4);
    for (i = 0; i < 2; i++) {
        obj->rotation_overrides[i].matrix_index = 0;
        obj->rotation_overrides[i].flags = 0;
    }
    obj->animation_data = 0;
    return 1;
}

void Render_InitPrimBlock(RenderObjectEntity *object, s16 x, s16 y, int unused, unsigned int palette_row)
{
    unsigned int page;
    int page_offset;
    unsigned int scaled;
    u16 signed_page;
    unsigned int palette_offset;
    unsigned int row_copy;
    s16 half_page;
    int flag_copy;
    RenderPacket34 *quad;
    RenderPacket28 *tri;
    s16 i;
    s16 side;
    half_page = y == 128;
    page = ((((x >> 6) - 8) * 2) + (y >> 7)) + ((y >> 8) * 30);
    signed_page = page;
    row_copy = palette_row;
    quad = (RenderPacket34 *) object->primitive_buffer;
    if (x != 960)
    {
        page_offset = page >> 1;
        scaled = palette_row * 64;
    }
    else
    {
        if (quad->page_bits == 31)
        {
            return;
        }
        if (quad)
        {
            page_offset = ((int) (((unsigned int) signed_page) << 16)) >> 17;
        }
        else
        {
            page_offset = ((int) (((unsigned int) signed_page) << 16)) >> 17;
        }
        if (quad)
        {
            scaled = row_copy * 64;
        }
        else
        {
            scaled = row_copy * 64;
        }
    }
    signed_page = page;
    if (signed_page)
    {
        palette_offset = scaled - 0x7080;
    }
    else
    {
        palette_offset = scaled - 0x7080;
    }
    for (i = 0; i < object->header->packet34_count; i++)
    {
        flag_copy = half_page;
        for (side = 0; side < 2; side++, quad++)
        {
            if (flag_copy)
            {
                u8 v = quad->v0;
                if (v < 128)
                {
                    quad->v0 = v + 128;
                    quad->v1 += 128;
                    quad->v2 += 128;
                    quad->v3 += 128;
                    quad->page_bits += page_offset;
                }
                else
                    if (object)
                {
                    quad->v0 = v + 128;
                    quad->v1 += 128;
                    quad->v2 += 128;
                    quad->v3 += 128;
                    quad->page_bits += page_offset + 1;
                }
                else
                {
                    quad->v0 = v + 128;
                    quad->v1 += 128;
                    quad->v2 += 128;
                    quad->v3 += 128;
                    quad->page_bits += page_offset + 1;
                }
            }
            else
            {
                quad->page_bits += page_offset;
            }
            quad->clut += palette_offset;
        }

    }

    tri = (RenderPacket28 *) quad;
    for (i = 0; i < object->header->packet28_count; i++)
    {
        for (side = 0; side < 2; side++, tri++)
        {
            if (half_page)
            {
                u8 v = tri->v0;
                if (v < 128)
                {
                    tri->v0 = v + 128;
                    tri->v1 += 128;
                    tri->v2 += 128;
                    scaled = palette_row * 64;
                    tri->page_bits += page_offset;
                }
                else
                {
                    tri->v0 = v + 128;
                    tri->v1 = tri->v1 + 128;
                    tri->v2 += 128;
                    tri->page_bits += page_offset + 1;
                }
            }
            else
            {
                tri->page_bits += page_offset;
            }
            tri->clut += palette_offset;
        }

    }

}


/* Original ordering-table insertion using native PsyZ packet links. */
/* A packet seen as bytes and as the address the DMA chain links through. */
typedef union RenderPacketAddress {
    u8 *bytes;
    uintptr_t word;
} RenderPacketAddress;

#define RENDER_TAG(p) ((RenderNativeTag *)(p))
#define RENDER_ADDPRIM(ot, packet) \
    (RENDER_TAG((packet).bytes)->address = RENDER_TAG(ot)->address, \
     RENDER_TAG(ot)->address = (packet).word)

/* NCLIP-tests each model face on the projected vertices and links the
 * faces with positive winding into the active ordering table at their
 * average depth; culled faces get a null link. Quads and triangles of both
 * packet classes are walked in turn through one shared descriptor cursor.
 * The room overlays carry a copy that keeps the negative winding instead. */
void Render_DrawTexturedQuads(RenderObjectEntity *entity) {
    int i;
    s32 clip_area;
    s32 *area = &clip_area;
    u32 *xy = D_800B1644;
    RenderPrimitiveDescriptor *record = entity->primitive_descriptors;
    u8 *packets = entity->primitive_buffer;
    int slot = g_ActiveDrawSlot;
    RenderNativeTag *ordering = D_800B0E38.ordering[slot];
    s32 *z = D_800A636C;
    u32 xy0, xy1, xy2;
    int a, b, c, d;
    RenderPacketAddress packet;

    i = 0;
    while (i < entity->header->packet34_count) {
        a = record->lookup_indices[0];
        b = record->lookup_indices[1];
        c = record->lookup_indices[2];
        xy0 = xy[a];
        xy1 = xy[b];
        xy2 = xy[c];
        gte_ldsxy0(xy0);
        gte_ldsxy2(xy2);
        gte_ldsxy1(xy1);
        gte_cop2_hazard_slot();
        gte_cop2_hazard_slot();
        gte_nclip_now();
        packet.bytes = (u8 *)((RenderPacket34 *)packets + slot);
        d = record->lookup_indices[3];
        gte_stmac0(area);
        if (Psyz_GtePgxpNclip4(&xy[a], xy0, &xy[b], xy1, &xy[c], xy2, &xy[d], xy[d], *area) > 0) {
            int depth = z[a] + z[b];
            depth += z[c];
            depth += z[d];
            depth >>= 4;
            if (depth < 0x1000) {
                RenderPacket34 *pkt = (RenderPacket34 *)packet.bytes;
                u32 xy3 = xy[d];
                RENDER_ADDPRIM(&ordering[depth], packet);
                pkt->sxy0 = xy0;
                pkt->sxy1 = xy1;
                pkt->sxy2 = xy2;
                pkt->sxy3 = xy3;
                Psyz_GteCopyPgxpSxy(&xy[a], &pkt->sxy0, xy0);
                Psyz_GteCopyPgxpSxy(&xy[b], &pkt->sxy1, xy1);
                Psyz_GteCopyPgxpSxy(&xy[c], &pkt->sxy2, xy2);
                Psyz_GteCopyPgxpSxy(&xy[d], &pkt->sxy3, xy3);
            }
        } else {
            RENDER_TAG(packet.bytes)->address = 0;
        }
        packets += 2 * sizeof(RenderPacket34);
        i++;
        record++;
    }

    i = 0;
    while (i < entity->header->packet28_count) {
        a = record->lookup_indices[0];
        b = record->lookup_indices[1];
        c = record->lookup_indices[2];
        xy0 = xy[a];
        xy1 = xy[b];
        xy2 = xy[c];
        gte_ldsxy0(xy0);
        gte_ldsxy2(xy2);
        gte_ldsxy1(xy1);
        gte_cop2_hazard_slot();
        gte_cop2_hazard_slot();
        gte_nclip_now();
        packet.bytes = (u8 *)((RenderPacket28 *)packets + slot);
        gte_stmac0(area);
        if (Psyz_GtePgxpNclip3(&xy[a], xy0, &xy[b], xy1, &xy[c], xy2, *area) > 0) {
            int depth = z[a] + z[b] + z[c];
            depth = depth / 3 >> 2;
            if (depth < 0x1000) {
                RenderPacket28 *pkt = (RenderPacket28 *)packet.bytes;
                RENDER_ADDPRIM(&ordering[depth], packet);
                pkt->sxy0 = xy0;
                pkt->sxy1 = xy1;
                pkt->sxy2 = xy2;
                Psyz_GteCopyPgxpSxy(&xy[a], &pkt->sxy0, xy0);
                Psyz_GteCopyPgxpSxy(&xy[b], &pkt->sxy1, xy1);
                Psyz_GteCopyPgxpSxy(&xy[c], &pkt->sxy2, xy2);
            }
        } else {
            RENDER_TAG(packet.bytes)->address = 0;
        }
        packets += 2 * sizeof(RenderPacket28);
        i++;
        record++;
    }

    i = 0;
    while (i < entity->header->packet24_count) {
        a = record->lookup_indices[0];
        b = record->lookup_indices[1];
        c = record->lookup_indices[2];
        xy0 = xy[a];
        xy1 = xy[b];
        xy2 = xy[c];
        gte_ldsxy0(xy0);
        gte_ldsxy2(xy2);
        gte_ldsxy1(xy1);
        gte_cop2_hazard_slot();
        gte_cop2_hazard_slot();
        gte_nclip_now();
        packet.bytes = (u8 *)((RenderPacket24 *)packets + slot);
        d = record->lookup_indices[3];
        gte_stmac0(area);
        if (Psyz_GtePgxpNclip4(&xy[a], xy0, &xy[b], xy1, &xy[c], xy2, &xy[d], xy[d], *area) > 0) {
            int depth = z[a] + z[b];
            depth += z[c];
            depth += z[d];
            depth >>= 4;
            if (depth < 0x1000) {
                RenderPacket24 *pkt = (RenderPacket24 *)packet.bytes;
                u32 xy3 = xy[d];
                RENDER_ADDPRIM(&ordering[depth], packet);
                pkt->sxy0 = xy0;
                pkt->sxy1 = xy1;
                pkt->sxy2 = xy2;
                pkt->sxy3 = xy3;
                Psyz_GteCopyPgxpSxy(&xy[a], &pkt->sxy0, xy0);
                Psyz_GteCopyPgxpSxy(&xy[b], &pkt->sxy1, xy1);
                Psyz_GteCopyPgxpSxy(&xy[c], &pkt->sxy2, xy2);
                Psyz_GteCopyPgxpSxy(&xy[d], &pkt->sxy3, xy3);
            }
        } else {
            RENDER_TAG(packet.bytes)->address = 0;
        }
        packets += 2 * sizeof(RenderPacket24);
        i++;
        record++;
    }

    i = 0;
    while (i < entity->header->packet1c_count) {
        a = record->lookup_indices[0];
        b = record->lookup_indices[1];
        c = record->lookup_indices[2];
        xy0 = xy[a];
        xy1 = xy[b];
        xy2 = xy[c];
        gte_ldsxy0(xy0);
        gte_ldsxy2(xy2);
        gte_ldsxy1(xy1);
        gte_cop2_hazard_slot();
        gte_cop2_hazard_slot();
        gte_nclip_now();
        packet.bytes = (u8 *)((RenderPacket1C *)packets + slot);
        gte_stmac0(area);
        if (Psyz_GtePgxpNclip3(&xy[a], xy0, &xy[b], xy1, &xy[c], xy2, *area) > 0) {
            int depth = z[a] + z[b] + z[c];
            depth = depth / 3 >> 2;
            if (depth < 0x1000) {
                RenderPacket1C *pkt = (RenderPacket1C *)packet.bytes;
                RENDER_ADDPRIM(&ordering[depth], packet);
                pkt->sxy0 = xy0;
                pkt->sxy1 = xy1;
                pkt->sxy2 = xy2;
                Psyz_GteCopyPgxpSxy(&xy[a], &pkt->sxy0, xy0);
                Psyz_GteCopyPgxpSxy(&xy[b], &pkt->sxy1, xy1);
                Psyz_GteCopyPgxpSxy(&xy[c], &pkt->sxy2, xy2);
            }
        } else {
            RENDER_TAG(packet.bytes)->address = 0;
        }
        packets += 2 * sizeof(RenderPacket1C);
        i++;
        record++;
    }
}



u32 g_RenderFadeColour = 0x808080;

/* Lights an object's visible parts: each part's light matrix is the view
 * light matrix times the part matrix, every vertex triple gets an NCCT colour
 * from the base colour, and the first coded vertex colour of the triple gets
 * a second NCCT into the alternate colour table. */
void Render_DrawObject(RenderObjectEntity *entity, union RenderLightingMatrix *view) {
    u32 baseColourStorage;
    GteMatrix lightMatrixStorage = {0};
    u32 *baseColour = &baseColourStorage;
    s16 *lightMatrix = &lightMatrixStorage.m[0][0];
    RenderMatrix *matrix;
    int i, j;

    if (!entity->header || !entity->draw_count)
        return;
    if(Psyz_GpuGetFrameInterpolation())Render_RegisterInterpolationPackets(entity,0);
    *baseColour = g_RenderFadeColour;
    matrix = entity->matrices;
    gte_SetBackColor(entity->shade, entity->shade, entity->shade);
    for (i = 0; i < entity->header->part_count; matrix++, i++) {
        RenderObjectPart *part = &entity->parts[i];
        int first;
        u32 *mainColours;
        u32 *altColours;
        RenderVec3s *vertex;

        if (part->visible != 1)
            continue;
        gte_MulMatrix0(view, matrix, lightMatrix);
        gte_SetLightMatrix(lightMatrix);
        first = part->vertex_start;
        mainColours = &g_RenderClutLookupTable[first];
        altColours = &g_RenderVertexWorkWords[first];
        vertex = &entity->vertices[first];
        for (j = 0; j < part->vertex_count; j += 3, mainColours += 3, altColours += 3) {
            RenderColor *colours;
            int k;

            /* NCCT works in triples. The last part can end with one or two
             * vertices; retail reads the following model section as padding.
             * Repeat the last real normal in the unused lanes. RGB outputs
             * for the real vertices are independent of those extra lanes. */
            int remaining = entity->header->vertex_count - (first + j);
            const RenderVec3s *normal0 = &g_RenderNormalTable[vertex[0].pad];
            const RenderVec3s *normal1 = remaining > 1 ? &g_RenderNormalTable[vertex[1].pad] : normal0;
            const RenderVec3s *normal2 = remaining > 2 ? &g_RenderNormalTable[vertex[2].pad] : normal1;
            gte_ldv3(normal0, normal1, normal2);
            gte_ldrgb(baseColour);
            gte_ncct();
            vertex += 3;
            gte_strgb3(mainColours, mainColours + 1, mainColours + 2);
            k = first + j;
            colours = (RenderColor *)entity->vertex_colours;
            if (colours[k].code)
                gte_ldrgb(&colours[k]);
            else if (colours[k + 1].code)
                gte_ldrgb(&colours[k + 1]);
            else if (colours[k + 2].code)
                gte_ldrgb(&colours[k + 2]);
            else
                continue;
            gte_ncct();
            gte_strgb3(altColours, altColours + 1, altColours + 2);
        }
    }
}


void Render_UpdateClutTable(RenderObjectEntity *arg0, s16 arg1, s16 arg2) {
    RenderObjectHeader *hdr;
    u32 *clutTbl;
    u32 *tpageTbl;
    RenderPrimitiveDescriptor *src;
    u8 *prim;
    u8 *p;
    s32 i;
    u8 cmd;

    hdr = arg0->header;
    if ((hdr != NULL) && (arg0->draw_count != 0)) {
        clutTbl = g_RenderClutLookupTable;
        tpageTbl = g_RenderVertexWorkWords;
        src = arg0->primitive_descriptors;
        prim = arg0->primitive_buffer;
        for (i = 0; i < arg0->header->packet34_count; src++) {
            p = prim + (arg2 * sizeof(RenderPacket34));
            if ((((RenderPacket34 *)p)->tag) != 0 || (arg1 != 0)) {
                cmd = ((RenderPacket34 *)p)->values0.bytes.command;
                ((RenderPacket34 *)p)->values0.value = clutTbl[src->lookup_indices[0]];
                ((RenderPacket34 *)p)->value1 = clutTbl[src->lookup_indices[1]];
                ((RenderPacket34 *)p)->value2 = clutTbl[src->lookup_indices[2]];
                ((RenderPacket34 *)p)->value3 = clutTbl[src->lookup_indices[3]];
                ((RenderPacket34 *)p)->values0.bytes.command = cmd;
            }
            prim += 2 * sizeof(RenderPacket34);
            i++;
        }
        for (i = 0; i < arg0->header->packet28_count; src++) {
            p = prim + (arg2 * sizeof(RenderPacket28));
            if ((((RenderPacket28 *)p)->tag) != 0 || (arg1 != 0)) {
                cmd = ((RenderPacket28 *)p)->values0.bytes.command;
                ((RenderPacket28 *)p)->values0.value = clutTbl[src->lookup_indices[0]];
                ((RenderPacket28 *)p)->value1 = clutTbl[src->lookup_indices[1]];
                ((RenderPacket28 *)p)->value2 = clutTbl[src->lookup_indices[2]];
                ((RenderPacket28 *)p)->values0.bytes.command = cmd;
            }
            prim += 2 * sizeof(RenderPacket28);
            i++;
        }
        for (i = 0; i < arg0->header->packet24_count; src++) {
            p = prim + (arg2 * sizeof(RenderPacket24));
            if ((((RenderPacket24 *)p)->tag) != 0 || (arg1 != 0)) {
                cmd = ((RenderPacket24 *)p)->values0.bytes.command;
                ((RenderPacket24 *)p)->values0.value = tpageTbl[src->lookup_indices[0]];
                ((RenderPacket24 *)p)->value1 = tpageTbl[src->lookup_indices[1]];
                ((RenderPacket24 *)p)->value2 = tpageTbl[src->lookup_indices[2]];
                ((RenderPacket24 *)p)->value3 = tpageTbl[src->lookup_indices[3]];
                ((RenderPacket24 *)p)->values0.bytes.command = cmd;
            }
            prim += 2 * sizeof(RenderPacket24);
            i++;
        }
        for (i = 0; i < arg0->header->packet1c_count; src++) {
            p = prim + (arg2 * sizeof(RenderPacket1C));
            if ((((RenderPacket1C *)p)->tag) != 0 || (arg1 != 0)) {
                cmd = ((RenderPacket1C *)p)->values0.bytes.command;
                ((RenderPacket1C *)p)->values0.value = tpageTbl[src->lookup_indices[0]];
                ((RenderPacket1C *)p)->value1 = tpageTbl[src->lookup_indices[1]];
                ((RenderPacket1C *)p)->value2 = tpageTbl[src->lookup_indices[2]];
                ((RenderPacket1C *)p)->values0.bytes.command = cmd;
            }
            prim += 2 * sizeof(RenderPacket1C);
            i++;
        }
    }
}


static inline int ClampLight(int value)
{
    if (value < 0) return 0;
    if (value > 255) return 255;
    return value;
}

/* Build two opposite Y lights and upload their color matrix to the GTE. */
int Render_InitRoomPrimState(void *objectArg)
{
    RenderObjectEntity *object = objectArg;
    int intensity, r, g, b;

    g_RenderLightingState.matrix.m[0][0] = 0;
    g_RenderLightingState.matrix.m[0][1] = 4096;
    g_RenderLightingState.matrix.m[0][2] = 0;
    intensity = object->shade + object->lightPositiveY;
    r = g_RoomLightTint.r;
    g = g_RoomLightTint.g;
    b = g_RoomLightTint.b;
    intensity = ClampLight(intensity) << 4;
    g_RenderLightColorMatrix.matrix.m[0][0] = intensity * r / 256;
    g_RenderLightColorMatrix.matrix.m[1][0] = intensity * g / 256;
    g_RenderLightColorMatrix.matrix.m[2][0] = intensity * b / 256;

    g_RenderLightingState.matrix.m[1][0] = 0;
    g_RenderLightingState.matrix.m[1][1] = -4096;
    g_RenderLightingState.matrix.m[1][2] = 0;
    intensity = object->shade + object->lightNegativeY;
    intensity = ClampLight(intensity) << 4;
    g_RenderLightColorMatrix.matrix.m[0][1] = intensity * r / 256;
    g_RenderLightColorMatrix.matrix.m[1][1] = intensity * g / 256;
    g_RenderLightColorMatrix.matrix.m[2][1] = intensity * b / 256;
    g_RenderLightingState.matrix.m[2][0] = 0;
    g_RenderLightingState.matrix.m[2][1] = 0;
    g_RenderLightingState.matrix.m[2][2] = 0;
    g_RenderLightColorMatrix.matrix.m[0][2] = 0;
    g_RenderLightColorMatrix.matrix.m[1][2] = 0;
    g_RenderLightColorMatrix.matrix.m[2][2] = 0;
    gte_SetColorMatrix(g_RenderLightColorMatrix.words);
    return 0;
}

/* The serialized command-byte count is u16; this ordinary memory buffer
 * replaces the shared PS1 scratchpad stack for every possible valid depth. */
static RenderMatrix g_RenderTransformStack[65536];

typedef struct {
    double rot[3][3];
    double trans[3];
} PgxpBoneMatrix;

typedef struct {
    const RenderMatrix *addr;
    RenderMatrix int_copy;
    PgxpBoneMatrix mat;
} PgxpBoneCacheEntry;

#define PGXP_BONE_CACHE_SETS 1024u
#define PGXP_BONE_CACHE_WAYS 4u
static PgxpBoneCacheEntry g_PgxpBoneCache[PGXP_BONE_CACHE_SETS][PGXP_BONE_CACHE_WAYS];
static PgxpBoneMatrix g_PgxpTransformStack[65536];

static void Pgxp_InitFromRenderMatrix(const RenderMatrix *src, PgxpBoneMatrix *dst) {
    int r, c;
    for (r = 0; r < 3; ++r) {
        for (c = 0; c < 3; ++c) {
            dst->rot[r][c] = (double)src->rotation[r][c];
        }
        dst->trans[r] = (double)src->translation[r];
    }
}

static unsigned Pgxp_HashMatrixAddr(const RenderMatrix *addr) {
    uintptr_t v = (uintptr_t)addr;
    return (unsigned)(((v >> 5) ^ (v >> 15)) & (PGXP_BONE_CACHE_SETS - 1u));
}

static void Pgxp_StoreBoneMatrix(const RenderMatrix *addr, const PgxpBoneMatrix *mat) {
    PgxpBoneCacheEntry *set = g_PgxpBoneCache[Pgxp_HashMatrixAddr(addr)];
    unsigned w, replace = PGXP_BONE_CACHE_WAYS - 1u;
    for (w = 0; w < PGXP_BONE_CACHE_WAYS; ++w) {
        if (set[w].addr == addr || set[w].addr == NULL) {
            replace = w;
            break;
        }
    }
    for (w = replace; w > 0; --w) {
        set[w] = set[w - 1u];
    }
    set[0].addr = addr;
    set[0].int_copy = *addr;
    set[0].mat = *mat;
}

static void Pgxp_LoadBoneMatrix(const RenderMatrix *addr, PgxpBoneMatrix *out) {
    const PgxpBoneCacheEntry *set = g_PgxpBoneCache[Pgxp_HashMatrixAddr(addr)];
    unsigned w;
    for (w = 0; w < PGXP_BONE_CACHE_WAYS; ++w) {
        if (set[w].addr == addr &&
            memcmp(&set[w].int_copy, addr, sizeof(RenderMatrix)) == 0) {
            *out = set[w].mat;
            return;
        }
    }
    Pgxp_InitFromRenderMatrix(addr, out);
}

static void Pgxp_CopyBoneMatrix(const RenderMatrix *dst, const RenderMatrix *src) {
    PgxpBoneMatrix mat;
    Pgxp_LoadBoneMatrix(src, &mat);
    Pgxp_StoreBoneMatrix(dst, &mat);
}

void Pgxp_StoreBoneRotYXZ(const RenderMatrix *matrix, s16 rx, s16 ry, s16 rz) {
    const double k = 6.28318530717958647692 / 4096.0;
    double ax = (double)rx * k;
    double ay = (double)ry * k;
    double az = (double)rz * k;
    double sx = sin(ax), cx = cos(ax);
    double sy = sin(ay), cy = cos(ay);
    double sz = sin(az), cz = cos(az);
    double sysx = sy * sx;
    double cysx = cy * sx;
    PgxpBoneMatrix mat;

    mat.rot[0][0] = (cy * cz + sysx * sz) * 4096.0;
    mat.rot[0][1] = (sysx * cz - cy * sz) * 4096.0;
    mat.rot[0][2] = (sy * cx) * 4096.0;
    mat.rot[1][0] = (cx * sz) * 4096.0;
    mat.rot[1][1] = (cx * cz) * 4096.0;
    mat.rot[1][2] = (-sx) * 4096.0;
    mat.rot[2][0] = (cysx * sz - sy * cz) * 4096.0;
    mat.rot[2][1] = (sy * sz + cysx * cz) * 4096.0;
    mat.rot[2][2] = (cy * cx) * 4096.0;
    mat.trans[0] = (double)(s16)matrix->translation[0];
    mat.trans[1] = (double)(s16)matrix->translation[1];
    mat.trans[2] = (double)matrix->translation[2];
    Pgxp_StoreBoneMatrix(matrix, &mat);
}

void Pgxp_StoreModelMatrix(const RenderMatrix *matrix, s16 rx, s16 ry, s16 rz, s16 scale) {
    const double k = 6.28318530717958647692 / 4096.0;
    double s = (double)scale;
    double ax = (double)rx * k;
    double ay = (double)ry * k;
    double az = (double)rz * k;
    double sx = sin(ax), cx = cos(ax);
    double sy = sin(ay), cy = cos(ay);
    double sz = sin(az), cz = cos(az);
    double nsy = -sy;
    double t0 = cz * nsy;
    double t1 = sz * nsy;
    PgxpBoneMatrix mat;

    mat.rot[0][0] = (cz * cy) * s;
    mat.rot[0][1] = (-sz * cy) * s;
    mat.rot[0][2] = (sy) * s;
    mat.rot[1][0] = (sz * cx - t0 * sx) * s;
    mat.rot[1][1] = (cz * cx + t1 * sx) * s;
    mat.rot[1][2] = (-cy * sx) * s;
    mat.rot[2][0] = (sz * sx + t0 * cx) * s;
    mat.rot[2][1] = (cz * sx - t1 * cx) * s;
    mat.rot[2][2] = (cy * cx) * s;
    mat.trans[0] = (double)(s16)matrix->translation[0];
    mat.trans[1] = (double)(s16)matrix->translation[1];
    mat.trans[2] = (double)matrix->translation[2];
    Pgxp_StoreBoneMatrix(matrix, &mat);
}

static void Pgxp_ComposeBoneMatrix(const PgxpBoneMatrix *parent,
                                   const PgxpBoneMatrix *source,
                                   double tx, double ty, double tz,
                                   PgxpBoneMatrix *out) {
    int r, c;
    PgxpBoneMatrix next;
    for (r = 0; r < 3; ++r) {
        next.trans[r] = parent->trans[r] +
            (parent->rot[r][0] * tx +
             parent->rot[r][1] * ty +
             parent->rot[r][2] * tz) / 4096.0;
        for (c = 0; c < 3; ++c) {
            next.rot[r][c] =
                (parent->rot[r][0] * source->rot[0][c] +
                 parent->rot[r][1] * source->rot[1][c] +
                 parent->rot[r][2] * source->rot[2][c]) / 4096.0;
        }
    }
    *out = next;
}

void Render_TransformVertices(RenderObjectEntity *actor) {
    /* An actor without an animation bank can reach opcode155 before any
     * decoder supplies active_matrix (notably after Continue). Use the
     * neutral bone rotations initialized by Render_SetupEntityPrims, kept
     * separate from the output so repeated draws cannot compound them. */
    static const RenderMatrix rest = {.rotation={{4096,0,0},{0,4096,0},{0,0,4096}}};
    s32 origin[3];
    s32 *originY = &origin[1];
    s32 *originZ = &origin[2];
    RenderMatrix *stack = g_RenderTransformStack;
    PgxpBoneMatrix *f_stack = g_PgxpTransformStack;
    PgxpBoneMatrix f_cur;
    s8 *commands;
    RenderMatrix *out;
    RenderAnimationLookupEntry *centres;
    RenderMatrix *current;
    int mode;
    int i;

    mode = actor->animation_state;
    if (mode == 1) {
        Render_CopyFrameData(actor, actor->animation_source, actor->animation_id);
        current = &actor->model_matrix;
    } else if (mode == 3) {
        if (actor->flags_9C & 0x400) {
            actor->matrices[1].rotation[0][0] = actor->model_matrix.rotation[0][0];
            actor->matrices[1].rotation[0][1] = actor->model_matrix.rotation[0][1];
            actor->matrices[1].rotation[0][2] = actor->model_matrix.rotation[0][2];
            actor->matrices[1].rotation[1][0] = actor->model_matrix.rotation[1][0];
            actor->matrices[1].rotation[1][1] = actor->model_matrix.rotation[1][1];
            actor->matrices[1].rotation[1][2] = actor->model_matrix.rotation[1][2];
            actor->matrices[1].rotation[2][0] = actor->model_matrix.rotation[2][0];
            actor->matrices[1].rotation[2][1] = actor->model_matrix.rotation[2][1];
            actor->matrices[1].rotation[2][2] = actor->model_matrix.rotation[2][2];
            actor->matrices[1].translation[0] = actor->model_matrix.translation[0];
            actor->matrices[1].translation[1] = actor->model_matrix.translation[1];
            actor->matrices[1].translation[2] = actor->model_matrix.translation[2];
            Pgxp_CopyBoneMatrix(&actor->matrices[1], &actor->model_matrix);
        } else {
            Render_CopyFrameDataDouble(actor, actor->animation_source, actor->animation_id);
        }
        return;
    } else {
        if (mode == 4) {
            const RenderMatrix *parent_mat =
                &actor->animation_source->matrices[actor->animation_id];
            PgxpBoneMatrix f_parent, f_self;
            Pgxp_LoadBoneMatrix(parent_mat, &f_parent);
            Pgxp_LoadBoneMatrix(&actor->model_matrix, &f_self);
            Pgxp_ComposeBoneMatrix(&f_parent, &f_self,
                                   f_self.trans[0], f_self.trans[1], f_self.trans[2],
                                   &f_cur);
            gte_CompMatrix(parent_mat,
                           &actor->model_matrix, &actor->model_matrix);
            Pgxp_StoreBoneMatrix(&actor->model_matrix, &f_cur);
        }
        current = &actor->model_matrix;
    }

    Pgxp_LoadBoneMatrix(current, &f_cur);
    *origin = 0;
    *originY = 0;
    out = actor->matrices;
    commands = actor->matrix_commands;
    centres = actor->animation_entries;
    gte_ldrotmatrix(current);
    gte_ldtransmatrix(current);
    for (i = 0; i < actor->header->matrix_command_bytes; i++, commands++) {
        int command = *commands;
        const RenderMatrix *source;

        if (command == -1) {
            gte_ReadMatrix(stack);
            stack++;
            *f_stack++ = f_cur;
            continue;
        }
        if (command == -2) {
            stack--;
            f_cur = *--f_stack;
            gte_ldrotmatrix(stack);
            gte_ldtransmatrix(stack);
            continue;
        }
        source = actor->active_matrix ? &actor->active_matrix[command] : &rest;
        *originZ = actor->parts[command].translation_z;
        {
            PgxpBoneMatrix f_src;
            Pgxp_LoadBoneMatrix(source, &f_src);
            if (*commands == 0) {
                Pgxp_ComposeBoneMatrix(&f_cur, &f_src,
                                       f_src.trans[0], f_src.trans[1], f_src.trans[2],
                                       &f_cur);
            } else {
                Pgxp_ComposeBoneMatrix(&f_cur, &f_src,
                                       0.0, 0.0, (double)*originZ,
                                       &f_cur);
            }
        }
        gte_ldclmv(source);
        gte_rtir();
        gte_stclmv(out);
        gte_ldclmv((u16 *)source + 1);
        gte_rtir();
        gte_stclmv((s16 *)out + 1);
        gte_ldclmv((u16 *)source + 2);
        gte_rtir();
        gte_stclmv((s16 *)out + 2);
        if (*commands == 0)
            gte_ldlv0(source->translation);
        else
            gte_ldlv0(origin);
        gte_rt();
        gte_stlvnl(out->translation);
        Pgxp_StoreBoneMatrix(out, &f_cur);
        gte_ldrotmatrix(out);
        gte_ldtransmatrix(out);
        out++;
        if (actor->parts[*commands].visible == 1 &&
            actor->bounds_vertices[*commands * 2 + 1].pad >= 0) {
            gte_ldv0(&actor->bounds_vertices[*commands * 2]);
            gte_rt();
            gte_stsv(centres);
            centres++;
        }
    }
}

void Render_CopyFrameData(RenderObjectEntity *dst, RenderObjectEntity *src, s32 frame) {
    s16 index = frame;

    dst->model_matrix.rotation[0][0] = src->matrices[index].rotation[0][0];
    dst->model_matrix.rotation[0][1] = src->matrices[index].rotation[0][1];
    dst->model_matrix.rotation[0][2] = src->matrices[index].rotation[0][2];
    dst->model_matrix.rotation[1][0] = src->matrices[index].rotation[1][0];
    dst->model_matrix.rotation[1][1] = src->matrices[index].rotation[1][1];
    dst->model_matrix.rotation[1][2] = src->matrices[index].rotation[1][2];
    dst->model_matrix.rotation[2][0] = src->matrices[index].rotation[2][0];
    dst->model_matrix.rotation[2][1] = src->matrices[index].rotation[2][1];
    dst->model_matrix.rotation[2][2] = src->matrices[index].rotation[2][2];
    dst->model_matrix.translation[0] = src->matrices[index].translation[0];
    dst->model_matrix.translation[1] = src->matrices[index].translation[1];
    dst->model_matrix.translation[2] = src->matrices[index].translation[2];
    Pgxp_CopyBoneMatrix(&dst->model_matrix, &src->matrices[index]);
}

void Render_CopyFrameDataDouble(RenderObjectEntity *dst, RenderObjectEntity *src, s32 frame) {
    s16 index = frame;

    dst->matrices[0].rotation[0][0] = src->matrices[index].rotation[0][0];
    dst->matrices[0].rotation[0][1] = src->matrices[index].rotation[0][1];
    dst->matrices[0].rotation[0][2] = src->matrices[index].rotation[0][2];
    dst->matrices[0].rotation[1][0] = src->matrices[index].rotation[1][0];
    dst->matrices[0].rotation[1][1] = src->matrices[index].rotation[1][1];
    dst->matrices[0].rotation[1][2] = src->matrices[index].rotation[1][2];
    dst->matrices[0].rotation[2][0] = src->matrices[index].rotation[2][0];
    dst->matrices[0].rotation[2][1] = src->matrices[index].rotation[2][1];
    dst->matrices[0].rotation[2][2] = src->matrices[index].rotation[2][2];
    dst->matrices[0].translation[0] = src->matrices[index].translation[0];
    dst->matrices[0].translation[1] = src->matrices[index].translation[1];
    dst->matrices[0].translation[2] = src->matrices[index].translation[2];

    dst->matrices[1].rotation[0][0] = src->matrices[index].rotation[0][0];
    dst->matrices[1].rotation[0][1] = src->matrices[index].rotation[0][1];
    dst->matrices[1].rotation[0][2] = src->matrices[index].rotation[0][2];
    dst->matrices[1].rotation[1][0] = src->matrices[index].rotation[1][0];
    dst->matrices[1].rotation[1][1] = src->matrices[index].rotation[1][1];
    dst->matrices[1].rotation[1][2] = src->matrices[index].rotation[1][2];
    dst->matrices[1].rotation[2][0] = src->matrices[index].rotation[2][0];
    dst->matrices[1].rotation[2][1] = src->matrices[index].rotation[2][1];
    dst->matrices[1].rotation[2][2] = src->matrices[index].rotation[2][2];
    dst->matrices[1].translation[0] = src->matrices[index].translation[0];
    dst->matrices[1].translation[1] = src->matrices[index].translation[1];
    dst->matrices[1].translation[2] = src->matrices[index].translation[2];
    Pgxp_CopyBoneMatrix(&dst->matrices[0], &src->matrices[index]);
    Pgxp_CopyBoneMatrix(&dst->matrices[1], &src->matrices[index]);
}

/* Shared original render work buffers. Vertex indices/counts are u16;
 * RTPT stores a whole triplet at part tails, including two extra slots after a three-slot output bias. */
u32 g_RenderClutLookupTable[65541];
u32 g_RenderVertexWorkWords[65541];

/* Original projection procedure with host matrix workspace. */
void Render_TransformMorphVertices(RenderObjectEntity *entity, u32 *view) {
    GteMatrix workspace;
    GteMatrix *scratch = &workspace;
    RenderMatrix *matrix = entity->matrices;
    const GteMatrix *view_mat = (const GteMatrix *)view;
    int i;

    for (i = 0; i < entity->header->part_count; i++) {
        RenderObjectPart *part = &entity->parts[i];
        const RenderMatrix *part_mat = matrix;

        gte_CompMatrixMac(view, matrix, scratch);
        gte_ldrotmatrix(scratch);
        gte_ldtransmatrix(scratch);
        matrix++;
        if (part->visible == 1) {
            RenderVec3s *vertices = &entity->vertices[part->vertex_start];
            u32 *screen = &g_RenderClutLookupTable[part->vertex_start];
            u32 *depth = &g_RenderVertexWorkWords[part->vertex_start];
            PgxpBoneMatrix f_bone;
            double eye_rot[3][3];
            double eye_trans[3];
            int r, c, j;

            Pgxp_LoadBoneMatrix(part_mat, &f_bone);
            for (r = 0; r < 3; ++r) {
                eye_trans[r] = (double)view_mat->t[r] +
                    ((double)view_mat->m[r][0] * f_bone.trans[0] +
                     (double)view_mat->m[r][1] * f_bone.trans[1] +
                     (double)view_mat->m[r][2] * f_bone.trans[2]) / 4096.0;
                for (c = 0; c < 3; ++c) {
                    eye_rot[r][c] =
                        ((double)view_mat->m[r][0] * f_bone.rot[0][c] +
                         (double)view_mat->m[r][1] * f_bone.rot[1][c] +
                         (double)view_mat->m[r][2] * f_bone.rot[2][c]) /
                        (4096.0 * 4096.0);
                }
            }
            Psyz_GteSetPgxpMatrix(eye_rot, eye_trans);

            for (j = 0; j < part->vertex_count; ) {
                gte_ldv3c(vertices);
                gte_rtpt_padded();
                vertices += 3;
                j += 3;
                screen += 3;
                depth += 3;
                gte_stsxy3c(screen);
                gte_stsz3c(depth);
            }
            Psyz_GteClearPgxpMatrix();
        }
    }
}

/* Shared anchor: Y is a view into the original zero vector. */
GteShortVector g_SkinAnchor = {0};

void Render_TransformSkinnedVertices(RenderObjectEntity *actor, u32 *view) {
    u16 offset[4] = {0};
    GteMatrix workspace;
    GteMatrix *scratch = &workspace;
    RenderObjectHeader *header = actor->header;

    if (!header) {
        Render_SetupBoneTransforms(actor, (s32 *)view);
        return;
    }
    gte_ldrotmatrix(&actor->matrices[header->anchor_matrix_index]);
    gte_ldtransmatrix(&actor->matrices[header->anchor_matrix_index]);
    g_SkinAnchorY = header->anchor_y;
    gte_ldv0(g_SkinAnchorVector);
    gte_rt();
    gte_stsv(&actor->hit_cylinder.value0);
    gte_ldrotmatrix(actor->matrices);
    gte_ldtransmatrix(actor->matrices);
    offset[0] = ((u16 *)actor->model_section14)[0] + (u16)actor->hit_body.value0;
    offset[1] = ((u16 *)actor->model_section14)[1] + (u16)actor->hit_body.value1;
    offset[2] = ((u16 *)actor->model_section14)[2] + (u16)actor->hit_body.value2;
    gte_ldv0(offset);
    gte_rt();
    gte_stsv(&actor->hit_body.value0);
    gte_ldrotmatrix(&actor->matrices[(s16)actor->table_index]);
    gte_ldtransmatrix(&actor->matrices[(s16)actor->table_index]);
    gte_ldv0(&actor->bounds_vertices[(s16)actor->table_index * 2]);
    gte_rt();
    gte_stsv(&actor->target_x);
    gte_CompMatrixMac(view, actor->matrices, scratch);
    gte_ldrotmatrix(scratch);
    gte_ldtransmatrix(scratch);
    gte_ldv0(actor->projection_origin);
    gte_rtps();
    gte_stsxy2(&actor->projected_x);
    gte_CompMatrixMac(view, &actor->matrices[(s16)actor->table_index], scratch);
    gte_ldrotmatrix(scratch);
    gte_ldtransmatrix(scratch);
    gte_ldv0(&actor->bounds_vertices[(s16)actor->table_index * 2]);
    gte_rtps();
    gte_stsxy2(&actor->projected_target_x);
}

void Render_SetupBoneTransforms(RenderObjectEntity *actor, s32 *view) {
    RenderMatrix workspace;
    RenderMatrix *scratch = &workspace;
    RenderObjectEntity *source = actor->animation_source;
    int index = (s16)actor->table_index;
    RenderMatrix *matrix = &source->matrices[index];

    gte_ldrotmatrix(matrix);
    gte_ldtransmatrix(matrix);
    gte_ldv0(&source->bounds_vertices[index * 2]);
    gte_rt();
    /* Headerless objects reuse the first override XYZ as a position output. */
    gte_stsv(&actor->rotation_overrides[0].x);
    gte_stsv(&actor->hit_cylinder.value0);
    gte_stsv(&actor->target_x);
    gte_CompMatrixMac(view, matrix, scratch);
    gte_ldrotmatrix(scratch);
    gte_ldtransmatrix(scratch);
    /* In this mode the halfwords at +0x2C form the projection input vector. */
    gte_ldv0(&actor->table_value2c);
    gte_rtps();
    actor->hit_body.radius = source->hit_body.radius;
    gte_stsxy2(&actor->projected_target_x);
    actor->table_value2c += actor->hit_cylinder.radius;
    gte_ldv0(&actor->table_value2c);
    gte_rtps();
    actor->hit_body.value0 = source->hit_body.value0;
    actor->hit_body.value1 = source->hit_body.value1;
    actor->hit_body.value2 = source->hit_body.value2;
    gte_stsxy2(&actor->projected_x);
    actor->table_value2c -= actor->hit_cylinder.radius;
}
