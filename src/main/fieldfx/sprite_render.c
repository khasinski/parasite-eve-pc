#include "common.h"
#include <math.h>
#include "pe1/field_billboard.h"
#include "pe1/field_glow_layers.h"
#include "pe1/field_oriented_sprite.h"
#include "pe1/field_particle_chain.h"
#include "pe1/field_shaded_quad.h"
#include "pe1/field_shaded_ring.h"
#include "pe1/field_sprite_state.h"
#include "pe1/field_textured_strip.h"
#include "pe1/geom_state.h"
#include "pe1/gte.h"
#include "pe1/gte_types.h"
#include "pe1/psyq_libc.h"
#include "pe1/render_object.h"
#include "pe1/render_prim.h"
#include "pe1/render_animation_frame.h"
#include "pe1/render_matrix_slot.h"

/* Field engine sprite rendering (0x800C2EAC..0x800C5EB0): the draw state
 * (texture page, CLUT origin, blend mode, cell quad, colour ramps) and the
 * sprite draws that use it: oriented sprite, billboard, glow sprite, shaded
 * quad, shaded ring, textured strip and the particle chain. They share the
 * draw state and scratch pointers private to this range (main_tu_evidence
 * G1390). */

static union {
    FieldFxScratch sprite;
    FieldBillboardScratch billboard;
} s_FieldSpriteWorkspace;
FieldFxScratch *g_FieldFxScratch = &s_FieldSpriteWorkspace.sprite;
FieldBillboardScratch *g_FieldFxBillboardScratch = &s_FieldSpriteWorkspace.billboard;
u8 g_FieldFxSpriteColorMode, g_FieldFxSpriteBlendRate, g_FieldFxSpriteBlendMode;
u8 g_FieldFxSpriteSemiTrans, g_FieldFxSpriteCellVBase;
u8 g_FieldFxSpriteCellUMax, g_FieldFxSpriteCellVExtent;
u16 g_FieldFxSpriteTpageX, g_FieldFxSpriteTpageY, g_FieldFxSpriteTpageValue;
u16 g_FieldFxSpriteClutX, g_FieldFxSpriteClutY;
GteShortVector g_FieldFxCellQuad[4];
char g_FieldSpriteWrongColorModeMsg[] = "Wrong Color Mode\n";

void FieldFx_SetSpriteTexturePreset(u8 mode) {
    if (mode == 0) {
        g_FieldFxSpriteTpageX = 0x340;
        g_FieldFxSpriteTpageY = 0x100;
        g_FieldFxSpriteClutX = 0;
        g_FieldFxSpriteClutY = 0x1D7;
        g_FieldFxSpriteCellVBase = 0;
    }
    if (mode == 1) {
        g_FieldFxSpriteTpageX = 0x340;
        g_FieldFxSpriteTpageY = 0x160;
        g_FieldFxSpriteClutY = 0x1DB;
        g_FieldFxSpriteClutX = 0;
        g_FieldFxSpriteCellVBase = 0x60;
    }
    if (mode == 2) {
        g_FieldFxSpriteTpageX = 0x340;
        g_FieldFxSpriteTpageY = 0x100;
        g_FieldFxSpriteClutX = 0;
        g_FieldFxSpriteClutY = 0x1D6;
        g_FieldFxSpriteCellVBase = 0;
    }
    if (mode == 3) {
        g_FieldFxSpriteTpageX = 0x380;
        g_FieldFxSpriteTpageY = 0x100;
        g_FieldFxSpriteClutX = 0;
        g_FieldFxSpriteClutY = 0x1C8;
        g_FieldFxSpriteCellVBase = 0;
    }

    g_FieldFxSpriteTpageValue = GetTPage(g_FieldFxSpriteColorMode, g_FieldFxSpriteBlendRate, g_FieldFxSpriteTpageX, g_FieldFxSpriteTpageY);
}

void FieldFx_SetSpriteCellSize(int width, int height) {
    u8 widthMinus;
    u8 heightMinus;
    int x;
    int y;

    widthMinus = width - 1;
    heightMinus = height - 1;

    g_FieldFxSpriteCellUMax = width;
    x = (width & 0xFF) << 4;
    g_FieldFxSpriteCellVExtent = height;
    y = (height & 0xFF) << 4;

    g_FieldFxCellQuad[0].x = -x;
    g_FieldFxCellQuad[0].y = -y;
    g_FieldFxCellQuad[0].z = 0;
    g_FieldFxCellQuad[1].x = x;
    g_FieldFxCellQuad[1].y = -y;
    g_FieldFxCellQuad[1].z = 0;
    g_FieldFxCellQuad[2].x = -x;
    g_FieldFxCellQuad[2].y = y;
    g_FieldFxCellQuad[2].z = 0;
    g_FieldFxCellQuad[3].x = x;
    g_FieldFxCellQuad[3].y = y;
    g_FieldFxCellQuad[3].z = 0;

    g_FieldFxSpriteCellUMax = widthMinus;
    g_FieldFxSpriteCellVExtent = heightMinus;
}

void FieldFx_SetSpriteColorMode(int mode) {
    mode = (short)mode;

    switch (mode) {
    case 0x10:
        g_FieldFxSpriteColorMode = 0;
        break;
    case 0x100:
        g_FieldFxSpriteColorMode = 1;
        break;
    default:
        printf(g_FieldSpriteWrongColorModeMsg);
        break;
    }

    g_FieldFxSpriteTpageValue = GetTPage(g_FieldFxSpriteColorMode, g_FieldFxSpriteBlendRate, g_FieldFxSpriteTpageX, g_FieldFxSpriteTpageY);
}

void FieldFx_InterpolateColorRamp(u8 *table, u32 step, u8 *out) {
    u32 sum = 0;
    u32 one = 0x1000;
    u32 last = 0xFF;
    u8 *entry = table;

    while (1) {
        u32 duration = entry[3];
        u8 *next = entry + 4;

        sum += duration;
        if (step < sum) {
            u32 weight = ((sum - step) << 12) / duration;
            u32 inv = one - weight;
            u32 out0;
            u32 out1;
            u32 out2;

            out0 = ((inv * entry[4]) + (weight * entry[0])) >> 12;
            out1 = ((inv * entry[5]) + (weight * entry[1])) >> 12;
            out2 = ((inv * entry[6]) + (weight * entry[2])) >> 12;
            out[0] = out0;
            out[1] = out1;
            out[2] = out2;
            return;
        }

        if (entry[7] == last) {
            out[0] = entry[4];
            out[1] = entry[5];
            out[2] = entry[6];
            return;
        }

        entry = next;
    }
}

void FieldFx_SetSpriteBlendMode(u8 mode) {
    g_FieldFxSpriteBlendMode = mode;
    switch (mode) {
    case 0:
        g_FieldFxSpriteSemiTrans = 0;
        g_FieldFxSpriteBlendRate = 0;
        break;
    case 1:
        g_FieldFxSpriteSemiTrans = 1;
        g_FieldFxSpriteBlendRate = 0;
        break;
    case 2:
        g_FieldFxSpriteSemiTrans = 1;
        g_FieldFxSpriteBlendRate = 1;
        break;
    case 3:
        g_FieldFxSpriteSemiTrans = 1;
        g_FieldFxSpriteBlendRate = 2;
        break;
    case 4:
        g_FieldFxSpriteSemiTrans = 1;
        g_FieldFxSpriteBlendRate = 3;
        break;
    }
    g_FieldFxSpriteTpageValue = GetTPage(g_FieldFxSpriteColorMode, g_FieldFxSpriteBlendRate, g_FieldFxSpriteTpageX, g_FieldFxSpriteTpageY);
}

void FieldFx_DrawOrientedSprite(FieldOrientedSprite *sprite)
{
    FieldStripPacket *packet;
    FieldStripLink link;

    packet = (FieldStripPacket *)(g_RenderAnimBufferBase[g_ActiveDrawSlot] + g_DrawPacketOffset);
    g_FieldFxScratch = &s_FieldSpriteWorkspace.sprite;
    g_FieldFxScratch->v = sprite->cell & 0xF0;
    g_FieldFxScratch->u = (sprite->cell - g_FieldFxScratch->v) << 4;
    g_FieldFxScratch->clutX = sprite->clut << 4;
    g_FieldFxScratch->clutY = sprite->clut >> 4;
    if (sprite->brightness != 0x80) {
        g_FieldFxScratch->channel = sprite->rgb[0];
        g_FieldFxScratch->channel *= sprite->brightness;
        if (g_FieldFxScratch->channel > 0x7FFF) {
            g_FieldFxScratch->channel = 0x7FFF;
        }
        g_FieldFxScratch->channel >>= 7;
        packet->r0 = g_FieldFxScratch->channel;
        g_FieldFxScratch->channel = sprite->rgb[1];
        g_FieldFxScratch->channel *= sprite->brightness;
        if (g_FieldFxScratch->channel > 0x7FFF) {
            g_FieldFxScratch->channel = 0x7FFF;
        }
        g_FieldFxScratch->channel >>= 7;
        packet->g0 = g_FieldFxScratch->channel;
        g_FieldFxScratch->channel = sprite->rgb[2];
        g_FieldFxScratch->channel *= sprite->brightness;
        if (g_FieldFxScratch->channel > 0x7FFF) {
            g_FieldFxScratch->channel = 0x7FFF;
        }
        g_FieldFxScratch->channel >>= 7;
        packet->b0 = g_FieldFxScratch->channel;
    } else {
        packet->r0 = sprite->rgb[0];
        packet->g0 = sprite->rgb[1];
        packet->b0 = sprite->rgb[2];
    }
    RotMatrixYXZ(&sprite->rotation, &g_FieldFxScratch->local);
    ScaleMatrix(&g_FieldFxScratch->local, &sprite->scale);
    g_FieldFxScratch->local.t[0] = 0;
    g_FieldFxScratch->local.t[1] = 0;
    g_FieldFxScratch->local.t[2] = 0;
    g_FieldFxScratch->matrix = *g_ViewMatrixSlot.value;
    g_FieldFxScratch->matrix.t[0] += (g_FieldFxScratch->matrix.m[0][2] * sprite->position.z +
                                g_FieldFxScratch->matrix.m[0][1] * sprite->position.y +
                                g_FieldFxScratch->matrix.m[0][0] * sprite->position.x) / 4096;
    g_FieldFxScratch->matrix.t[1] += (g_FieldFxScratch->matrix.m[1][2] * sprite->position.z +
                                g_FieldFxScratch->matrix.m[1][1] * sprite->position.y +
                                g_FieldFxScratch->matrix.m[1][0] * sprite->position.x) / 4096;
    g_FieldFxScratch->matrix.t[2] += (g_FieldFxScratch->matrix.m[2][2] * sprite->position.z +
                                g_FieldFxScratch->matrix.m[2][1] * sprite->position.y +
                                g_FieldFxScratch->matrix.m[2][0] * sprite->position.x) / 4096;
    gte_CompMatrix(&g_FieldFxScratch->matrix, &g_FieldFxScratch->local, &g_FieldFxScratch->matrix);
    gte_ldrotmatrix(&g_FieldFxScratch->matrix);
    gte_ldtransmatrix(&g_FieldFxScratch->matrix);
    Psyz_GteSetPgxpEffectMode(1);
    gte_ldv3(g_FieldFxCellQuad, &g_FieldFxCellQuad[1], &g_FieldFxCellQuad[2]);
    gte_rtpt_padded();
    packet->tag.length = 9;
    packet->code = 0x2C;
    packet->u0 = g_FieldFxScratch->u;
    packet->v0 = g_FieldFxScratch->v;
    packet->u1 = g_FieldFxScratch->u + g_FieldFxSpriteCellUMax;
    packet->v1 = g_FieldFxScratch->v;
    packet->u2 = g_FieldFxScratch->u;
    packet->v2 = g_FieldFxScratch->v + g_FieldFxSpriteCellVExtent;
    packet->u3 = g_FieldFxScratch->u + g_FieldFxSpriteCellUMax;
    packet->v3 = g_FieldFxScratch->v + g_FieldFxSpriteCellVExtent;
    gte_avsz3_padded();
    /* PSY-Q setSemiTrans(packet, g_FieldFxSpriteSemiTrans). */
    if (g_FieldFxSpriteSemiTrans) {
        packet->code = packet->code | 2;
    } else {
        packet->code = packet->code & ~2;
    }
    gte_stotz(&g_FieldFxScratch->depth);
    gte_stsxy3(&packet->x0, &packet->x1, &packet->x2);
    gte_ldv0(&g_FieldFxCellQuad[3]);
    gte_rtps();
    packet->tpage = g_FieldFxSpriteTpageValue;
    packet->clut = GetClut(g_FieldFxSpriteClutX + g_FieldFxScratch->clutX,
                           g_FieldFxSpriteClutY + g_FieldFxScratch->clutY);
    gte_stsxy2(&packet->x3);
    Psyz_GteSetPgxpEffectMode(0);
    packet->tag.address = STRIP_OT(g_FieldFxScratch->depth + sprite->depth)->address;
    link.tag = &packet->tag;
    STRIP_OT(g_FieldFxScratch->depth + sprite->depth)->address = link.word;
    g_DrawPacketOffset += sizeof(FieldStripPacket);
}

/* Project a camera-facing billboard and link it into the depth ordering table. */
void FieldFx_DrawBillboard(FieldBillboard *board)
{
    FieldStripPacket *packet;
    int width;
    int height;
    int halfWidth;
    int halfHeight;
    int distance;
    int projectedWidth;
    int projectedHeight;
    FieldStripLink link;

    g_FieldFxBillboardScratch = &s_FieldSpriteWorkspace.billboard;
    g_FieldFxBillboardScratch->v = board->cell & 0xF0;
    g_FieldFxBillboardScratch->u = (board->cell - g_FieldFxBillboardScratch->v) << 4;
    g_FieldFxBillboardScratch->clutX = board->clut << 4;
    g_FieldFxBillboardScratch->clutY = board->clut >> 4;
    width = (g_FieldFxSpriteCellUMax * board->scaleX) >> 12;
    height = (g_FieldFxSpriteCellVExtent * board->scaleY) >> 12;
    if (board->brightness != 0x80) {
        g_FieldFxBillboardScratch->channel = board->rgb[0];
        g_FieldFxBillboardScratch->channel *= board->brightness;
        if (g_FieldFxBillboardScratch->channel > 0x7FFF) {
            g_FieldFxBillboardScratch->channel = 0x7FFF;
        }
        g_FieldFxBillboardScratch->channel >>= 7;
        g_FieldFxBillboardScratch->rgb[0] = g_FieldFxBillboardScratch->channel;
        g_FieldFxBillboardScratch->channel = board->rgb[1];
        g_FieldFxBillboardScratch->channel *= board->brightness;
        if (g_FieldFxBillboardScratch->channel > 0x7FFF) {
            g_FieldFxBillboardScratch->channel = 0x7FFF;
        }
        g_FieldFxBillboardScratch->channel >>= 7;
        g_FieldFxBillboardScratch->rgb[1] = g_FieldFxBillboardScratch->channel;
        g_FieldFxBillboardScratch->channel = board->rgb[2];
        g_FieldFxBillboardScratch->channel *= board->brightness;
        if (g_FieldFxBillboardScratch->channel > 0x7FFF) {
            g_FieldFxBillboardScratch->channel = 0x7FFF;
        }
        g_FieldFxBillboardScratch->channel >>= 7;
        g_FieldFxBillboardScratch->rgb[2] = g_FieldFxBillboardScratch->channel;
    } else {
        g_FieldFxBillboardScratch->rgb[0] = board->rgb[0];
        g_FieldFxBillboardScratch->rgb[1] = board->rgb[1];
        g_FieldFxBillboardScratch->rgb[2] = board->rgb[2];
    }
    {
        GteMatrix **slot;
        slot = &g_ViewMatrixSlot.value;
        gte_ldrotmatrix((const GteMatrixWords *)*slot);
        gte_ldtransmatrix((const GteMatrixWords *)*slot);
    }
    Psyz_GteSetPgxpEffectMode(1);
    g_FieldFxBillboardScratch->depth = RotTransPers(&board->position, &g_FieldFxBillboardScratch->screen.word,
                                     &g_FieldFxBillboardScratch->p, &g_FieldFxBillboardScratch->flag);
    float f_cx = 0.0f, f_cy = 0.0f, f_cw = 1.0f;
    int has_pgxp = Psyz_GteGetLastPgxpVertex(&f_cx, &f_cy, &f_cw);
    g_FieldFxBillboardScratch->centerX = g_FieldFxBillboardScratch->screen.word;
    g_FieldFxBillboardScratch->centerY = g_FieldFxBillboardScratch->screen.xy.y;
    distance = g_GeomScroll.state.position.projectionDistance[0];
    projectedWidth = (width << 5) * distance / (g_FieldFxBillboardScratch->depth * 4 + distance);
    projectedHeight =
        (height << 5) * distance / (g_FieldFxBillboardScratch->depth * 4 + distance);
    halfWidth = projectedWidth >> 1;
    halfHeight = projectedHeight >> 1;
    packet = (FieldStripPacket *)(g_RenderAnimBufferBase[g_ActiveDrawSlot] + g_DrawPacketOffset);
    packet->tag.length = 9;
    packet->code = 0x2C;
    /* PSY-Q setSemiTrans(packet, g_FieldFxSpriteSemiTrans). */
    if (g_FieldFxSpriteSemiTrans) {
        packet->code = packet->code | 2;
    } else {
        packet->code = packet->code & ~2;
    }
    if (board->angle != 0) {
        g_FieldFxBillboardScratch->corner[0].x = -halfWidth;
        g_FieldFxBillboardScratch->corner[0].y = -halfHeight;
        g_FieldFxBillboardScratch->corner[1].x = halfWidth;
        g_FieldFxBillboardScratch->corner[1].y = -halfHeight;
        g_FieldFxBillboardScratch->corner[2].x = -halfWidth;
        g_FieldFxBillboardScratch->corner[2].y = halfHeight;
        g_FieldFxBillboardScratch->corner[3].x = halfWidth;
        g_FieldFxBillboardScratch->corner[3].y = halfHeight;
        g_FieldFxBillboardScratch->rotation.m[2][2] = 0x1000;
        g_FieldFxBillboardScratch->rotation.m[1][1] = 0x1000;
        g_FieldFxBillboardScratch->rotation.m[0][0] = 0x1000;
        g_FieldFxBillboardScratch->rotation.t[2] = 0;
        g_FieldFxBillboardScratch->rotation.t[1] = 0;
        g_FieldFxBillboardScratch->rotation.t[0] = 0;
        g_FieldFxBillboardScratch->rotation.m[2][1] = 0;
        g_FieldFxBillboardScratch->rotation.m[2][0] = 0;
        g_FieldFxBillboardScratch->rotation.m[1][2] = 0;
        g_FieldFxBillboardScratch->rotation.m[1][0] = 0;
        g_FieldFxBillboardScratch->rotation.m[0][2] = 0;
        g_FieldFxBillboardScratch->rotation.m[0][1] = 0;
        RotMatrixZ(board->angle, &g_FieldFxBillboardScratch->rotation);
        ApplyMatrixSV(&g_FieldFxBillboardScratch->rotation, &g_FieldFxBillboardScratch->corner[0],
                      &g_FieldFxBillboardScratch->turned[0]);
        ApplyMatrixSV(&g_FieldFxBillboardScratch->rotation, &g_FieldFxBillboardScratch->corner[1],
                      &g_FieldFxBillboardScratch->turned[1]);
        ApplyMatrixSV(&g_FieldFxBillboardScratch->rotation, &g_FieldFxBillboardScratch->corner[2],
                      &g_FieldFxBillboardScratch->turned[2]);
        ApplyMatrixSV(&g_FieldFxBillboardScratch->rotation, &g_FieldFxBillboardScratch->corner[3],
                      &g_FieldFxBillboardScratch->turned[3]);
        packet->x0 = g_FieldFxBillboardScratch->centerX + g_FieldFxBillboardScratch->turned[0].x;
        packet->y0 = g_FieldFxBillboardScratch->centerY + g_FieldFxBillboardScratch->turned[0].y;
        packet->x1 = g_FieldFxBillboardScratch->centerX + g_FieldFxBillboardScratch->turned[1].x;
        packet->y1 = g_FieldFxBillboardScratch->centerY + g_FieldFxBillboardScratch->turned[1].y;
        packet->x2 = g_FieldFxBillboardScratch->centerX + g_FieldFxBillboardScratch->turned[2].x;
        packet->y2 = g_FieldFxBillboardScratch->centerY + g_FieldFxBillboardScratch->turned[2].y;
        packet->x3 = g_FieldFxBillboardScratch->centerX + g_FieldFxBillboardScratch->turned[3].x;
        packet->y3 = g_FieldFxBillboardScratch->centerY + g_FieldFxBillboardScratch->turned[3].y;
    } else {
        packet->x0 = g_FieldFxBillboardScratch->centerX - halfWidth;
        packet->y0 = g_FieldFxBillboardScratch->centerY - halfHeight;
        packet->x1 = g_FieldFxBillboardScratch->centerX + halfWidth;
        packet->y1 = g_FieldFxBillboardScratch->centerY - halfHeight;
        packet->x2 = g_FieldFxBillboardScratch->centerX - halfWidth;
        packet->y2 = g_FieldFxBillboardScratch->centerY + halfHeight;
        packet->x3 = g_FieldFxBillboardScratch->centerX + halfWidth;
        packet->y3 = g_FieldFxBillboardScratch->centerY + halfHeight;
    }
    if (has_pgxp) {
        double f_w = (double)g_FieldFxSpriteCellUMax * (double)board->scaleX / 4096.0;
        double f_h = (double)g_FieldFxSpriteCellVExtent * (double)board->scaleY / 4096.0;
        double f_denom = (double)(g_FieldFxBillboardScratch->depth * 4 + distance);
        if (f_denom > 1e-4) {
            float f_hw = (float)(f_w * 16.0 * (double)distance / f_denom);
            float f_hh = (float)(f_h * 16.0 * (double)distance / f_denom);
            u32 sxy0, sxy1, sxy2, sxy3;
            memcpy(&sxy0, &packet->x0, sizeof(sxy0));
            memcpy(&sxy1, &packet->x1, sizeof(sxy1));
            memcpy(&sxy2, &packet->x2, sizeof(sxy2));
            memcpy(&sxy3, &packet->x3, sizeof(sxy3));
            if (board->angle != 0) {
                double ang = (double)board->angle * (6.28318530717958647692 / 4096.0);
                float ca = (float)cos(ang);
                float sa = (float)sin(ang);
                /* RotMatrixZ has m[0][0]=cos, m[0][1]=-sin, m[1][0]=sin, m[1][1]=cos */
                float x0_off = -f_hw * ca + f_hh * sa;
                float y0_off = -f_hw * sa - f_hh * ca;
                float x1_off =  f_hw * ca + f_hh * sa;
                float y1_off =  f_hw * sa - f_hh * ca;
                float x2_off = -f_hw * ca - f_hh * sa;
                float y2_off = -f_hw * sa + f_hh * ca;
                float x3_off =  f_hw * ca - f_hh * sa;
                float y3_off =  f_hw * sa + f_hh * ca;
                Psyz_GteStorePgxpVertex(&packet->x0, sxy0, f_cx + x0_off, f_cy + y0_off, f_cw);
                Psyz_GteStorePgxpVertex(&packet->x1, sxy1, f_cx + x1_off, f_cy + y1_off, f_cw);
                Psyz_GteStorePgxpVertex(&packet->x2, sxy2, f_cx + x2_off, f_cy + y2_off, f_cw);
                Psyz_GteStorePgxpVertex(&packet->x3, sxy3, f_cx + x3_off, f_cy + y3_off, f_cw);
            } else {
                Psyz_GteStorePgxpVertex(&packet->x0, sxy0, f_cx - f_hw, f_cy - f_hh, f_cw);
                Psyz_GteStorePgxpVertex(&packet->x1, sxy1, f_cx + f_hw, f_cy - f_hh, f_cw);
                Psyz_GteStorePgxpVertex(&packet->x2, sxy2, f_cx - f_hw, f_cy + f_hh, f_cw);
                Psyz_GteStorePgxpVertex(&packet->x3, sxy3, f_cx + f_hw, f_cy + f_hh, f_cw);
            }
        }
    }
    Psyz_GteSetPgxpEffectMode(0);
    packet->r0 = g_FieldFxBillboardScratch->rgb[0];
    packet->g0 = g_FieldFxBillboardScratch->rgb[1];
    packet->b0 = g_FieldFxBillboardScratch->rgb[2];
    packet->tpage = g_FieldFxSpriteTpageValue;
    packet->clut = GetClut(g_FieldFxSpriteClutX + g_FieldFxBillboardScratch->clutX,
                           g_FieldFxSpriteClutY + g_FieldFxBillboardScratch->clutY);
    packet->u0 = g_FieldFxBillboardScratch->u + 1;
    packet->v0 = g_FieldFxBillboardScratch->v + 1;
    packet->u1 = g_FieldFxBillboardScratch->u + g_FieldFxSpriteCellUMax;
    packet->v1 = g_FieldFxBillboardScratch->v + 1;
    packet->u2 = g_FieldFxBillboardScratch->u + 1;
    packet->v2 = g_FieldFxBillboardScratch->v + g_FieldFxSpriteCellVExtent;
    packet->u3 = g_FieldFxBillboardScratch->u + g_FieldFxSpriteCellUMax;
    packet->v3 = g_FieldFxBillboardScratch->v + g_FieldFxSpriteCellVExtent;
    packet->tag.address = STRIP_OT(g_FieldFxBillboardScratch->depth + board->depth)->address;
    link.tag = &packet->tag;
    STRIP_OT(g_FieldFxBillboardScratch->depth + board->depth)->address = link.word;
    g_DrawPacketOffset += sizeof(FieldStripPacket);
}

/* Compose/project a glow quad and link it into the depth ordering table.
 * The GTE operands are the camera slot and the corner vectors themselves:
 * reload loads them into t1..t3 in turn, as retail does. */
void FieldFx_DrawGlowSprite(FieldGlowSprite *sprite, GteMatrix *placement, u8 mode)
{
    s16 *xy0;
    s16 *xy1;
    s16 *xy2;
    FieldStripPacket *packet;
    GteShortVector position;
    int swap[4];
    s32 flag;
    FieldStripLink link;

    packet = (FieldStripPacket *)(g_RenderAnimBufferBase[g_ActiveDrawSlot] + g_DrawPacketOffset);
    g_FieldFxScratch = &s_FieldSpriteWorkspace.sprite;
    if (mode == 0) {
        /* Compose camera rotation with each placement column. */
        gte_ldrotmatrix(g_ViewMatrixSlot.value);
        gte_ldclmv(placement);
        gte_rtir();
        gte_stclmv(&g_FieldFxScratch->matrix);
        gte_ldclmv((u16 *)placement + 1);
        gte_rtir();
        gte_stclmv((u16 *)&g_FieldFxScratch->matrix + 1);
        gte_ldclmv((u16 *)placement + 2);
        gte_rtir();
        gte_stclmv((u16 *)&g_FieldFxScratch->matrix + 2);
        /* Transform placement translation with the camera matrix. */
        gte_ldtransmatrix(g_ViewMatrixSlot.value);
        gte_ldlv0(placement->t);
        gte_rt();
        gte_stlvl(g_FieldFxScratch->matrix.t);
        gte_ldrotmatrix(&g_FieldFxScratch->matrix);
        gte_ldtransmatrix(&g_FieldFxScratch->matrix);
    } else {
        gte_ldrotmatrix(g_ViewMatrixSlot.value);
        gte_ldtransmatrix(g_ViewMatrixSlot.value);
        position.x = placement->t[0];
        position.y = placement->t[1];
        position.z = placement->t[2];
        RotTrans(&position, (GteVector *)placement->t, &flag);
        gte_ldrotmatrix((const GteMatrixWords *)placement);
        gte_ldtransmatrix((const GteMatrixWords *)placement);
    }
    FieldFx_ScaleColor((s16)sprite->depth, &sprite->r, &packet->r0);
    Psyz_GteSetPgxpEffectMode(1);
    gte_ldv3(g_FieldFxCellQuad, &g_FieldFxCellQuad[1], &g_FieldFxCellQuad[2]);
    gte_rtpt_padded();
    g_FieldFxScratch->v = g_FieldFxSpriteCellVBase + (sprite->cell & 0xF0);
    /* The row base can exceed cell; retain signed UV arithmetic without UB. */
    g_FieldFxScratch->u = (sprite->cell - g_FieldFxScratch->v) * 16;
    g_FieldFxScratch->clutX = sprite->clut << 4;
    g_FieldFxScratch->clutY = sprite->clut >> 4;
    packet->tag.length = 9;
    packet->code = 0x2C;
    packet->u0 = g_FieldFxScratch->u;
    packet->v0 = g_FieldFxScratch->v;
    packet->u1 = g_FieldFxScratch->u + g_FieldFxSpriteCellUMax;
    packet->v1 = g_FieldFxScratch->v;
    packet->u2 = g_FieldFxScratch->u;
    packet->v2 = g_FieldFxScratch->v + g_FieldFxSpriteCellVExtent;
    packet->u3 = g_FieldFxScratch->u + g_FieldFxSpriteCellUMax;
    packet->v3 = g_FieldFxScratch->v + g_FieldFxSpriteCellVExtent;
    if (sprite->flip & 1) {
        swap[0] = packet->u0;
        swap[1] = packet->u1;
        swap[2] = packet->u2;
        swap[3] = packet->u3;
        packet->u0 = swap[1];
        packet->u1 = swap[0];
        packet->u2 = swap[3];
        packet->u3 = swap[2];
    }
    if (sprite->flip & 2) {
        swap[0] = packet->v0;
        swap[1] = packet->v1;
        swap[2] = packet->v2;
        swap[3] = packet->v3;
        packet->v0 = swap[2];
        packet->v1 = swap[3];
        packet->v2 = swap[0];
        packet->v3 = swap[1];
    }
    gte_cop2_hazard_slot();
    gte_cop2_hazard_slot();
    gte_avsz3_command();
    /* PSY-Q setSemiTrans(packet, g_FieldFxSpriteSemiTrans). */
    if (g_FieldFxSpriteSemiTrans) {
        packet->code = packet->code | 2;
    } else {
        packet->code = packet->code & ~2;
    }
    gte_stotz(&g_FieldFxScratch->depth);
    g_FieldFxScratch->depth += sprite->offset;
    if (g_FieldFxScratch->depth - 1 < 0xFFFU) {
        xy0 = &packet->x0;
        xy1 = &packet->x1;
        xy2 = &packet->x2;
        gte_stsxy3(xy0, xy1, xy2);
        gte_ldv0(&g_FieldFxCellQuad[3]);
        gte_cop2_hazard_slot();
        gte_cop2_hazard_slot();
        gte_rtps_command();
        packet->tpage = g_FieldFxSpriteTpageValue;
        packet->clut = GetClut(g_FieldFxSpriteClutX + g_FieldFxScratch->clutX,
                               g_FieldFxSpriteClutY + g_FieldFxScratch->clutY);
        gte_stsxy2(&packet->x3);
        packet->tag.address = STRIP_OT(g_FieldFxScratch->depth)->address;
        link.tag = &packet->tag;
        STRIP_OT(g_FieldFxScratch->depth)->address = link.word;
        g_DrawPacketOffset += sizeof(FieldStripPacket);
    }
    Psyz_GteSetPgxpEffectMode(0);
}

/* Draw a quad with four independently shaded corners. */
void FieldFx_DrawShadedQuad(FieldShadedQuadColors *colors, GteMatrix *placement,
                   u8 mode) {
    s16 *xy0;
    s16 *xy1;
    s16 *xy2;
    FieldShadedQuadPacket *packet;
    GteShortVector position;
    s32 flag;
    FieldShadedQuadLink link;

    packet = (FieldShadedQuadPacket *)(g_RenderAnimBufferBase[g_ActiveDrawSlot] + g_DrawPacketOffset);
    g_FieldFxScratch = &s_FieldSpriteWorkspace.sprite;
    if (mode == 0) {
        /* Compose camera rotation with each placement column. */
        gte_ldrotmatrix(g_ViewMatrixSlot.value);
        gte_ldclmv(placement);
        gte_rtir();
        gte_stclmv(&g_FieldFxScratch->matrix);
        gte_ldclmv((u16 *)placement + 1);
        gte_rtir();
        gte_stclmv((u16 *)&g_FieldFxScratch->matrix + 1);
        gte_ldclmv((u16 *)placement + 2);
        gte_rtir();
        gte_stclmv((u16 *)&g_FieldFxScratch->matrix + 2);
        /* Transform placement translation with the camera matrix. */
        gte_ldtransmatrix(g_ViewMatrixSlot.value);
        gte_ldlv0(placement->t);
        gte_rt();
        gte_stlvl(g_FieldFxScratch->matrix.t);
        gte_ldrotmatrix(&g_FieldFxScratch->matrix);
        gte_ldtransmatrix(&g_FieldFxScratch->matrix);
    } else {
        gte_ldrotmatrix(g_ViewMatrixSlot.value);
        gte_ldtransmatrix(g_ViewMatrixSlot.value);
        position.x = placement->t[0];
        position.y = placement->t[1];
        position.z = placement->t[2];
        RotTrans(&position, (GteVector *)placement->t, &flag);
        gte_ldrotmatrix((const GteMatrixWords *)placement);
        gte_ldtransmatrix((const GteMatrixWords *)placement);
    }
    FieldFx_ScaleColor(colors->brightness, colors->rgb[0], &packet->r0);
    FieldFx_ScaleColor(colors->brightness, colors->rgb[1], &packet->r1);
    FieldFx_ScaleColor(colors->brightness, colors->rgb[2], &packet->r2);
    FieldFx_ScaleColor(colors->brightness, colors->rgb[3], &packet->r3);
    Psyz_GteSetPgxpEffectMode(1);
    gte_ldv3(g_FieldFxCellQuad, &g_FieldFxCellQuad[1], &g_FieldFxCellQuad[2]);
    gte_rtpt_padded();
    packet->tag.length = 8;
    packet->code = 0x38;
    gte_cop2_hazard_slot();
    gte_cop2_hazard_slot();
    gte_avsz3_command();
    /* PSY-Q setSemiTrans(packet, g_FieldFxSpriteSemiTrans). */
    if (g_FieldFxSpriteSemiTrans) {
        packet->code = packet->code | 2;
    } else {
        packet->code = packet->code & ~2;
    }
    gte_stotz(&g_FieldFxScratch->depth);
    g_FieldFxScratch->depth += colors->depth;
    if (g_FieldFxScratch->depth - 1 < 0xFFFU) {
        xy0 = &packet->x0;
        xy1 = &packet->x1;
        xy2 = &packet->x2;
        gte_stsxy3(xy0, xy1, xy2);
        gte_ldv0(&g_FieldFxCellQuad[3]);
        gte_cop2_hazard_slot();
        gte_cop2_hazard_slot();
        gte_rtps_command();
        gte_stsxy2(&packet->x3);
        packet->tag.address = SHADED_QUAD_OT(g_FieldFxScratch->depth)->address;
        link.tag = &packet->tag;
        SHADED_QUAD_OT(g_FieldFxScratch->depth)->address = link.word;
        g_DrawPacketOffset += sizeof(FieldShadedQuadPacket);
    }
    Psyz_GteSetPgxpEffectMode(0);
}

/* Lay out the inner and outer vertices of an annulus. */
void FieldFx_BuildRingGeometry(FieldRingGeometry *data) {
    int angle;
    u32 i;
    int step;
    GteShortVector *verts;

    angle = 0;
    step = 0x1000 / data->mode;
    verts = data->source;

    for (i = 0; i < data->mode; i++, verts++) {
        verts->x = (rsin(angle) * data->extent1) >> 12;
        verts->y = (rcos(angle) * data->extent1) >> 12;
        verts->z = 0;
        angle += step;
    }

    angle = 0;
    for (i = 0; i < data->mode; i++, verts++) {
        verts->x = (rsin(angle) * data->extent0) >> 12;
        verts->y = (rcos(angle) * data->extent0) >> 12;
        verts->z = 0;
        angle += step;
    }
}

void FieldFx_DrawShadedRing(FieldShadedRing *ring, GteMatrix *placement, u8 mode)
{
    FieldRingPacket *packet;
    char *drawMode;
    u8 outer[4] = {0};
    u8 inner[4] = {0};
    GteShortVector position;
    s32 flag;
    s32 p;
    GteShortVector *points;
    u32 i;
    u32 next;
    u16 a;
    u16 c;
    u16 d;
    FieldRingLink link;

    g_FieldFxScratch = &s_FieldSpriteWorkspace.sprite;
    if (mode == 0) {
        /* Compose camera rotation with each placement column. */
        gte_ldrotmatrix(g_ViewMatrixSlot.value);
        gte_ldclmv(placement);
        gte_rtir();
        gte_stclmv(&g_FieldFxScratch->matrix);
        gte_ldclmv((u16 *)placement + 1);
        gte_rtir();
        gte_stclmv((u16 *)&g_FieldFxScratch->matrix + 1);
        gte_ldclmv((u16 *)placement + 2);
        gte_rtir();
        gte_stclmv((u16 *)&g_FieldFxScratch->matrix + 2);
        /* Transform placement translation with the camera matrix. */
        gte_ldtransmatrix(g_ViewMatrixSlot.value);
        gte_ldlv0(placement->t);
        gte_rt();
        gte_stlvl(g_FieldFxScratch->matrix.t);
        gte_ldrotmatrix(&g_FieldFxScratch->matrix);
        gte_ldtransmatrix(&g_FieldFxScratch->matrix);
    } else {
        gte_ldrotmatrix(g_ViewMatrixSlot.value);
        gte_ldtransmatrix(g_ViewMatrixSlot.value);
        position.x = placement->t[0];
        position.y = placement->t[1];
        position.z = placement->t[2];
        RotTrans(&position, (GteVector *)placement->t, &flag);
        gte_ldrotmatrix((const GteMatrixWords *)placement);
        gte_ldtransmatrix((const GteMatrixWords *)placement);
    }
    FieldFx_ScaleColor(ring->intensity, ring->color1, outer);
    FieldFx_ScaleColor(ring->intensity, ring->color0, inner);
    points = ring->source;
    Psyz_GteSetPgxpEffectMode(1);
    for (i = 0; i < ring->mode; i++) {
        packet = (FieldRingPacket *)(g_RenderAnimBufferBase[g_ActiveDrawSlot] + g_DrawPacketOffset);
        g_DrawPacketOffset += sizeof(FieldRingPacket);
        drawMode = g_RenderAnimBufferBase[g_ActiveDrawSlot] + g_DrawPacketOffset;
        g_DrawPacketOffset += sizeof(RenderTintMode);
        SetDrawTPage(drawMode, 0, 0, (g_FieldFxSpriteBlendRate & 3) << 5);
        next = (i + 1) % ring->mode;
        a = i;
        c = ring->mode + i;
        d = ring->mode + next;
        g_FieldFxScratch->depth = RotTransPers4(&points[a], &points[next], &points[c],
                                          &points[d], &packet->x0, &packet->x1,
                                          &packet->x2, &packet->x3, &p, &flag);
        /* Copy complete colour payloads without alignment or aliasing assumptions. */
        memcpy(&packet->c0, outer, sizeof(packet->c0));
        memcpy(&packet->c1, outer, sizeof(packet->c1));
        memcpy(&packet->c2, inner, sizeof(packet->c2));
        memcpy(&packet->c3, inner, sizeof(packet->c3));
        packet->tag.length = 8;
        packet->c0.rgb[3] = 0x38;
        /* PSY-Q setSemiTrans(packet, g_FieldFxSpriteSemiTrans). */
        if (g_FieldFxSpriteSemiTrans) {
            packet->c0.rgb[3] = packet->c0.rgb[3] | 2;
        } else {
            packet->c0.rgb[3] = packet->c0.rgb[3] & ~2;
        }
        packet->tag.address = RING_OT(g_FieldFxScratch->depth + ring->offset)->address;
        link.tag = &packet->tag;
        RING_OT(g_FieldFxScratch->depth + ring->offset)->address = link.word;
        link.bytes = drawMode;
        link.tag->address = RING_OT(g_FieldFxScratch->depth + ring->offset)->address;
        RING_OT(g_FieldFxScratch->depth + ring->offset)->address = link.word;
    }
    Psyz_GteSetPgxpEffectMode(0);
}

void FieldFx_BuildParticleChain(FieldChainRecord *record)
{
    GteShortVector tailIn;
    GteShortVector tail;
    GteShortVector headIn;
    GteShortVector head;
    GteMatrix matrix;
    GteShortVector angles;
    GteMatrix rotation;
    GteVector previous;
    GteVector toPlayer;
    GteVector cross;
    FieldChainLink *link;
    unsigned int i;
    int run;
    int turn;

    link = record->links;
    memset(&tail, 0, sizeof(tail));
    tail.x = -record->length;
    tail.y = 0;
    tail.z = 0;
    tailIn = tail;
    memset(&head, 0, sizeof(head));
    head.x = record->length;
    head.y = 0;
    head.z = 0;
    headIn = head;
    matrix = record->matrix;
    record->field0E = 0;
    for (i = 0; i < record->count; i++, link++) {
        ApplyMatrixSV(&matrix, &tailIn, &tail);
        ApplyMatrixSV(&matrix, &headIn, &head);
        link->edgeA.x = head.x + matrix.t[0];
        link->edgeA.y = head.y + matrix.t[1];
        link->edgeA.z = head.z + matrix.t[2];
        link->edgeB.x = tail.x + matrix.t[0];
        link->edgeB.y = tail.y + matrix.t[1];
        link->edgeB.z = tail.z + matrix.t[2];
        if (i >= 2)
            record->bend = 1;
        else
            record->bend = 0;
        if (record->bend == 1) {
            previous.x = link[-1].matrix.t[0] - link[-2].matrix.t[0];
            previous.y = 0;
            previous.z = link[-1].matrix.t[2] - link[-2].matrix.t[2];
            toPlayer.x = ((RoomSparkBattleEntity *)D_8009D254)->x.part.integer - link[-2].matrix.t[0];
            toPlayer.y = 0;
            toPlayer.z = ((RoomSparkBattleEntity *)D_8009D254)->z.part.integer - link[-2].matrix.t[2];
            OuterProduct0(&previous, &toPlayer, &cross);
            run = SquareRoot0(previous.x * previous.x + previous.z * previous.z);
            ratan2(run, SquareRoot0(toPlayer.x * toPlayer.x + toPlayer.z * toPlayer.z));
            if (cross.y > 0)
                turn = rand() % 512 + 0x40;
            else
                turn = -(rand() % 512 + 0x40);
            angles.y = turn;
            angles.x = 0;
            angles.z = 0;
        } else {
            angles.x = link->tiltX;
            angles.y = link->tiltY;
            angles.z = 0;
        }
        if (i >= 6) {
            angles.x = 0;
            angles.y = 0;
            angles.z = 0;
        }
        RotMatrix(&angles, &rotation);
        rotation.t[0] = 0;
        rotation.t[1] = 0;
        rotation.t[2] = -record->depth;
        gte_CompMatrix(&matrix, &rotation, &matrix);
        link->matrix = matrix;
    }
}

void FieldFx_DrawTexturedStrip(FieldTexturedStrip *strip)
{
    FieldStripNode *node;
    FieldStripPacket *packet;
    GteShortVector quad[4];
    s32 p;
    s32 flag;
    u32 i;
    u8 cell;
    int step;
    u32 depth;
    FieldStripLink link;

    node = strip->nodes;
    i = 0;
    g_FieldFxScratch = &s_FieldSpriteWorkspace.sprite;
    packet = (FieldStripPacket *)(g_RenderAnimBufferBase[g_ActiveDrawSlot] + g_DrawPacketOffset);
    g_FieldFxScratch->clutX = strip->clut << 4;
    g_FieldFxScratch->clutY = strip->clut >> 4;
    Psyz_GteSetPgxpEffectMode(1);
    while (i < strip->count - 1) {
        cell = strip->cell;
        step = node->cellStep;
        g_FieldFxScratch->v = cell & 0xF0;
        g_FieldFxScratch->u = (strip->cell - g_FieldFxScratch->v) << 4;
        cell += step;
        g_FieldFxScratch->v = cell & 0xF0;
        g_FieldFxScratch->u = (cell - g_FieldFxScratch->v) << 4;
        quad[0] = node->edgeA;
        quad[1] = node->edgeB;
        quad[2] = node[1].edgeA;
        quad[3] = node[1].edgeB;
        gte_ldrotmatrix(g_ViewMatrixSlot.value);
        gte_ldtransmatrix(g_ViewMatrixSlot.value);
        depth = RotTransPers4(&quad[0], &quad[1], &quad[2], &quad[3],
                              &packet->x0, &packet->x1, &packet->x2,
                              &packet->x3, &p, &flag);
        FieldFx_ScaleColor(node->brightness, node->rgb, &packet->r0);
        packet->u0 = g_FieldFxScratch->u;
        packet->v0 = g_FieldFxScratch->v;
        packet->u1 = g_FieldFxScratch->u + g_FieldFxSpriteCellUMax;
        packet->v1 = g_FieldFxScratch->v;
        packet->u2 = g_FieldFxScratch->u;
        packet->v2 = g_FieldFxScratch->v + g_FieldFxSpriteCellVExtent;
        packet->u3 = g_FieldFxScratch->u + g_FieldFxSpriteCellUMax;
        packet->v3 = g_FieldFxScratch->v + g_FieldFxSpriteCellVExtent;
        packet->tpage = g_FieldFxSpriteTpageValue;
        packet->clut = GetClut(g_FieldFxSpriteClutX + g_FieldFxScratch->clutX,
                               g_FieldFxSpriteClutY + g_FieldFxScratch->clutY);
        packet->tag.length = 9;
        packet->code = 0x2C;
        /* PSY-Q setSemiTrans(packet, g_FieldFxSpriteSemiTrans). */
        if (g_FieldFxSpriteSemiTrans) {
            packet->code = packet->code | 2;
        } else {
            packet->code = packet->code & ~2;
        }
        if (depth != 0 && depth < 0x1000 && node->visible) {
            packet->tag.address = STRIP_OT(depth)->address;
            link.tag = &packet->tag;
            STRIP_OT(depth)->address = link.word;
        }
        i++;
        node++;
        packet++;
        g_DrawPacketOffset += sizeof(FieldStripPacket);
    }
    Psyz_GteSetPgxpEffectMode(0);
}

/* Original script sprite code embedded at USA E0060..E0808. Ordinary memory
 * replaces the descending list below the animation buffer and COP2 scratch. */
static FieldScriptSprite s_ScriptSprites[22];
FieldScriptSprite *g_FieldFxScriptSpriteList;
s16 g_FieldFxScriptSpriteCount;
static struct {
    u8 r, g, b, u, v;
    u16 clut, tpage;
    GteShortVector position;
    int size;
} s_ScriptSpriteDraw;

void FieldFx_ClearScriptSprites(void)
{
    g_FieldFxScriptSpriteList = &s_ScriptSprites[21];
    int cleared = 0;
    for (unsigned i = 0; i < 22 && cleared < g_FieldFxScriptSpriteCount; ++i) {
        FieldScriptSprite *sprite = g_FieldFxScriptSpriteList - i;
        if (sprite->active) { sprite->active = 0; ++cleared; }
    }
    g_FieldFxScriptSpriteCount = 0;
}

void FieldFx_SpawnScriptSprite(const void *position, int mode, int size, int delay, int r, int g, int b)
{
    if (!position || !g_FieldFxScriptSpriteList) return;
    int index = 0;
    while (index <= g_FieldFxScriptSpriteCount && index < 21 &&
           g_FieldFxScriptSpriteList[-index].active) ++index;
    if (index > g_FieldFxScriptSpriteCount) index = 0;
    FieldScriptSprite *sprite = g_FieldFxScriptSpriteList - index;
    const s16 *point = position;
    sprite->mode = mode;
    sprite->x = point[0];sprite->y = point[1];sprite->z = point[2];
    sprite->delay = sprite->remaining = delay;
    sprite->cell = 0;sprite->active = 1;
    sprite->r = r;sprite->g = g;sprite->b = b;
    sprite->size = size;sprite->fade = 0;
    if (g_FieldFxScriptSpriteCount < 20) ++g_FieldFxScriptSpriteCount;
}

/* USA E051C: emit the original FT4 from the shared sprite parameters. */
static void FieldFx_EmitScriptSpriteQuad(void)
{
    if ((unsigned)g_ActiveDrawSlot >= 2 || !g_ViewMatrixSlot.value ||
        !g_GeomScroll.state.position.projectionDistance ||
        !D_800B0E38.ordering[g_ActiveDrawSlot] || !g_RenderAnimBufferBase[g_ActiveDrawSlot] ||
        g_DrawPacketOffset < 0 || g_DrawPacketOffset > RENDER_ANIM_BUFFER_BYTES - (int)sizeof(RenderTexturedQuad)) return;
    RenderTexturedQuad *packet = (RenderTexturedQuad *)(g_RenderAnimBufferBase[g_ActiveDrawSlot] + g_DrawPacketOffset);
    g_DrawPacketOffset += sizeof(*packet);
    Psyz_GteSetPgxpEffectMode(1);
    gte_ldrotmatrix(g_ViewMatrixSlot.value);gte_ldtransmatrix(g_ViewMatrixSlot.value);
    gte_ldv0(&s_ScriptSpriteDraw.position);gte_rtps();
    u32 screen;gte_stsxy2(&screen);
    float f_cx = 0.0f, f_cy = 0.0f, f_cw = 1.0f;
    int has_pgxp = Psyz_GteGetLastPgxpVertex(&f_cx, &f_cy, &f_cw);
    Psyz_GteSetPgxpEffectMode(0);
    u32 depth = Psyz_GteDataRead(19);
    s32 projection = *g_GeomScroll.state.position.projectionDistance;
    s32 numerator = (s32)(((u32)s_ScriptSpriteDraw.size << 5) * (u32)projection);
    s32 denominator = (s32)((depth << 4) + (u32)projection);
    if (!denominator || (numerator == INT32_MIN && denominator == -1)) return;
    s32 half = (numerator / denominator) >> 1;
    u16 x = screen, y = screen >> 16;
    packet->tag.length = 9;
    packet->color.bytes.r=s_ScriptSpriteDraw.r;packet->color.bytes.g=s_ScriptSpriteDraw.g;packet->color.bytes.b=s_ScriptSpriteDraw.b;
    packet->color.bytes.code=0x2E;
    packet->clut=s_ScriptSpriteDraw.clut;packet->tpage=s_ScriptSpriteDraw.tpage;
    packet->u0=packet->u2=s_ScriptSpriteDraw.u;packet->u1=packet->u3=s_ScriptSpriteDraw.u+16;
    packet->v0=packet->v1=s_ScriptSpriteDraw.v;packet->v2=packet->v3=s_ScriptSpriteDraw.v+16;
    packet->x0=packet->x2=(u32)x-(u32)half;packet->x1=packet->x3=(u32)x+(u32)half;
    packet->y0=packet->y1=(u32)y-(u32)half;packet->y2=packet->y3=(u32)y+(u32)half;
    if (has_pgxp) {
        float f_half = (float)((double)numerator / ((double)denominator * 2.0));
        u32 sxy0, sxy1, sxy2, sxy3;
        memcpy(&sxy0, &packet->x0, sizeof(sxy0));
        memcpy(&sxy1, &packet->x1, sizeof(sxy1));
        memcpy(&sxy2, &packet->x2, sizeof(sxy2));
        memcpy(&sxy3, &packet->x3, sizeof(sxy3));
        Psyz_GteSetPgxpEffectMode(1);
        Psyz_GteStorePgxpVertex(&packet->x0, sxy0, f_cx - f_half, f_cy - f_half, f_cw);
        Psyz_GteStorePgxpVertex(&packet->x1, sxy1, f_cx + f_half, f_cy - f_half, f_cw);
        Psyz_GteStorePgxpVertex(&packet->x2, sxy2, f_cx - f_half, f_cy + f_half, f_cw);
        Psyz_GteStorePgxpVertex(&packet->x3, sxy3, f_cx + f_half, f_cy + f_half, f_cw);
        Psyz_GteSetPgxpEffectMode(0);
    }
    int layer = (int)(depth >> 2) - 8;
    if ((unsigned)layer < 4096) AddPrim(D_800B0E38.ordering[g_ActiveDrawSlot]+layer,packet);
}

/* USA E026C: the four-cell strip; prepare UV before advancing its timer. */
static void FieldFx_UpdateAnimatedScriptSprite(FieldScriptSprite *sprite)
{
    if (!sprite->active) return;
    s_ScriptSpriteDraw.clut=0x77D3;s_ScriptSpriteDraw.tpage=0x34;
    s_ScriptSpriteDraw.r=s_ScriptSpriteDraw.g=s_ScriptSpriteDraw.b=128;
    s_ScriptSpriteDraw.u=sprite->cell;s_ScriptSpriteDraw.v=0xD0;
    s_ScriptSpriteDraw.position=(GteShortVector){sprite->x,sprite->y,sprite->z,0};
    s_ScriptSpriteDraw.size=sprite->size;
    if (sprite->active!=1) return;
    if (!sprite->remaining) {
        sprite->cell+=16;sprite->remaining=sprite->delay;
        if (sprite->cell>=49) {
            sprite->active=0;sprite->cell=0;--g_FieldFxScriptSpriteCount;return;
        }
    } else --sprite->remaining;
    if (sprite->active==1) FieldFx_EmitScriptSpriteQuad();
}

/* USA E03A0: RGB fade rises to 255 then returns below zero. */
static void FieldFx_UpdateFadingScriptSprite(FieldScriptSprite *sprite)
{
    if (!sprite->active) return;
    s_ScriptSpriteDraw.clut=0x7713;s_ScriptSpriteDraw.tpage=0x34;
    s_ScriptSpriteDraw.u=0xC0;s_ScriptSpriteDraw.v=0xCA;
    s_ScriptSpriteDraw.position=(GteShortVector){sprite->x,sprite->y,sprite->z,0};
    s_ScriptSpriteDraw.size=sprite->size;
    int r=sprite->r-sprite->fade,g=sprite->g-sprite->fade,b=sprite->b-sprite->fade;
    s_ScriptSpriteDraw.r=r<0?0:r;s_ScriptSpriteDraw.g=g<0?0:g;s_ScriptSpriteDraw.b=b<0?0:b;
    if (sprite->active!=1) {
        sprite->fade=(s16)((u16)sprite->fade-sprite->delay);
        if (sprite->fade<0) {sprite->active=0;sprite->fade=0;--g_FieldFxScriptSpriteCount;return;}
    } else {
        sprite->fade=(s16)((u16)sprite->fade+sprite->delay);
        if (sprite->fade>=256) {sprite->fade=255;sprite->active=2;}
    }
    FieldFx_EmitScriptSpriteQuad();
}

void FieldFx_UpdateScriptSprites(void)
{
    if (!g_FieldFxScriptSpriteList) return;
    int processed=0;
    for (unsigned i=0;i<22 && processed<g_FieldFxScriptSpriteCount;++i) {
        FieldScriptSprite *sprite=g_FieldFxScriptSpriteList-i;
        if (!sprite->active) continue;
        if (sprite->mode==0) FieldFx_UpdateAnimatedScriptSprite(sprite);
        else if (sprite->mode==1) FieldFx_UpdateFadingScriptSprite(sprite);
        ++processed;
    }
}

#if PE_NATIVE_STATE_CPU_COMPONENTS
#include "pe_state_script_sprites.h"
static uint32_t StateSpriteGet(const unsigned char *p){return(uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void StateSpritePut(unsigned char *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(v>>(i*8));}
int PeStateScriptSprites_Walk(const unsigned char *in,unsigned char *out,int apply){if(!out&&(!in||memcmp(in,"PESPR001",8)))return 0;if(g_FieldFxScriptSpriteList&&g_FieldFxScriptSpriteList!=&s_ScriptSprites[21])return 0;uint32_t initialized=out?(g_FieldFxScriptSpriteList!=0):StateSpriteGet(in+8),count=out?(uint32_t)g_FieldFxScriptSpriteCount:StateSpriteGet(in+12);if(initialized>1||count>20||(!initialized&&count))return 0;if(out){memcpy(out,"PESPR001",8);StateSpritePut(out+8,initialized);StateSpritePut(out+12,count);}size_t at=16;
#define F(value,kind) do{uint32_t v=out?(uint32_t)(value):StateSpriteGet(in+at);if(kind==1&&v>255)return 0;if(kind==2&&v>65535)return 0;if(kind==3&&((int32_t)v< -32768||(int32_t)v>32767))return 0;if(out)StateSpritePut(out+at,v);else if(apply)(value)=(__typeof__(value))v;at+=4;}while(0)
F(g_FieldFxSpriteColorMode,1);
F(g_FieldFxSpriteBlendRate,1);
F(g_FieldFxSpriteBlendMode,1);
F(g_FieldFxSpriteSemiTrans,1);
F(g_FieldFxSpriteTpageX,2);
F(g_FieldFxSpriteTpageY,2);
F(g_FieldFxSpriteTpageValue,2);
F(g_FieldFxSpriteClutX,2);
F(g_FieldFxSpriteClutY,2);
F(g_FieldFxSpriteCellVBase,1);
F(g_FieldFxSpriteCellUMax,1);
F(g_FieldFxSpriteCellVExtent,1);
for(unsigned i=0;i<22;i++){FieldScriptSprite *record=&s_ScriptSprites[i];if(!out&&StateSpriteGet(in+at)>2)return 0;F(record->active,1);
F(record->delay,1);
F(record->remaining,1);
F(record->cell,1);
F(record->mode,1);
F(record->reserved,1);
F(record->r,1);
F(record->g,1);
F(record->b,1);
F(record->padding,1);
F(record->x,3);
F(record->y,3);
F(record->z,3);
F(record->fade,3);
F(record->size,3);}
F(s_ScriptSpriteDraw.r,1);
F(s_ScriptSpriteDraw.g,1);
F(s_ScriptSpriteDraw.b,1);
F(s_ScriptSpriteDraw.u,1);
F(s_ScriptSpriteDraw.v,1);
F(s_ScriptSpriteDraw.clut,2);
F(s_ScriptSpriteDraw.tpage,2);
F(s_ScriptSpriteDraw.position.x,3);
F(s_ScriptSpriteDraw.position.y,3);
F(s_ScriptSpriteDraw.position.z,3);
F(s_ScriptSpriteDraw.position.pad,3);
F(s_ScriptSpriteDraw.size,0);
#undef F
if(at!=PE_STATE_SCRIPT_SPRITES_BYTES)return 0;if(apply&&!out){g_FieldFxScriptSpriteCount=(s16)count;g_FieldFxScriptSpriteList=initialized?&s_ScriptSprites[21]:0;}return 1;}
#endif
