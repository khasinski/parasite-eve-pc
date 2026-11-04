#include "common.h"
#include "pe1/field_textured_model.h"
#include "pe1/field_model_draw.h"
#include "pe1/geom_state.h"
#include "pe1/gte.h"
#include "pe1/render_object.h"
#include "pe1/render_prim.h"

/* Textured field models: UV scrolling, the texture page, CLUT,
 * semi-transparency and depth state the draw reads, the colour-table
 * save/restore and fades, and the draw itself. */

u16 g_FieldFxModelTpage, g_FieldFxModelClut, g_FieldFxModelSemiTrans;
s16 g_FieldFxModelDepthBias;
u32 g_FieldModelColorBackup[256];

#define MODEL_VERTEX(model, offset) ((FieldModelVertex *)&(model)->bytes[offset])
#define MODEL_COLOR(model, offset) (((FieldModelColor *)&(model)->bytes[offset])->word)

void FieldFx_ModelScrollUv(FieldTexturedModel *model, u8 xOffset, u8 yOffset) {
    FieldModelHeader *header = &model->header;
    FieldModelTri *tri = header->tris;
    FieldModelQuad *quad;
    u8 *uv;
    int i;

    if (header->uOffset != xOffset || header->vOffset != yOffset) {
        i = 0;
        header->uOffset = xOffset;
        header->vOffset = yOffset;
        if (header->flatTris + header->shadedTris != 0) {
            do {
                i++;
                uv = (u8 *)tri->uv;
                uv[0] += xOffset;
                uv[2] += xOffset;
                uv[4] += xOffset;
                uv[1] += yOffset;
                uv[3] += yOffset;
                uv[5] += yOffset;
                tri++;
            } while ((u16)i < header->flatTris + header->shadedTris);
        }
        i = 0;
        quad = (FieldModelQuad *)tri;
        if (header->flatQuads + header->shadedQuads != 0) {
            do {
                i++;
                uv = (u8 *)quad->uv;
                uv[0] += xOffset;
                uv[2] += xOffset;
                uv[4] += xOffset;
                uv[6] += xOffset;
                uv[1] += yOffset;
                uv[3] += yOffset;
                uv[5] += yOffset;
                uv[7] += yOffset;
                quad++;
            } while ((u16)i < header->flatQuads + header->shadedQuads);
        }
    }
    g_FieldFxModelDepthBias = 0;
}

void FieldFx_ModelSetTexture(int x, int y) {
    g_FieldFxModelTpage = x;
    g_FieldFxModelClut = y;
}

void FieldFx_ModelSetSemiTrans(short value) {
    g_FieldFxModelSemiTrans = value;
}

void FieldFx_ModelSetDepthBias(short value) {
    g_FieldFxModelDepthBias = value;
}

void FieldFx_ModelSaveColors(char *data) {
    FieldModelHeader *table = (FieldModelHeader *)data;
    int i = 0;
    u32 *src = (u32 *)(data + table->colorOffset);
    u32 *dst = g_FieldModelColorBackup;

    while (i < table->colorCount) {
        i++;
        *dst++ = *src++;
    }
}

void FieldFx_ModelRestoreColors(char *data) {
    FieldModelHeader *table = (FieldModelHeader *)data;
    int i = 0;
    u32 *src = g_FieldModelColorBackup;
    u32 *dst = (u32 *)(data + table->colorOffset);

    while (i < table->colorCount) {
        i++;
        *dst++ = *src++;
    }
}

void FieldFx_ModelScaleColors(char *data, u16 factor)
{
    int i;
    u32 red;
    u32 blue;
    u32 blue_product;
    u32 green;
    u32 green_product;
    FieldModelHeader *table = (FieldModelHeader *)data;
    u32 scratch; /* Original scratchpad word is local to this calculation. */
    FieldModelColor *entry = (FieldModelColor *)(data + table->colorOffset);

    for (i = 0; i < table->colorCount; i++, entry++) {
        red = entry->rgb.r;
        red *= factor;
        scratch = red;
        if (red > 0x7FFF) {
            scratch = 0x7FFF;
        }
        red = scratch >> 7;
        scratch = red;
        entry->rgb.r = red;

        green_product = entry->rgb.g;
        green_product *= factor;
        scratch = green_product;
        if (green_product > 0x7FFF) {
            scratch = 0x7FFF;
        }
        green = scratch >> 7;
        scratch = green;
        entry->rgb.g = green;

        blue_product = entry->rgb.b;
        blue_product *= factor;
        scratch = blue_product;
        if (blue_product > 0x7FFF) {
            scratch = 0x7FFF;
        }
        blue = scratch >> 7;
        scratch = blue;
        entry->rgb.b = blue;
    }
}

void FieldFx_ModelSetColors(char *data, u8 r, u8 g, u8 b) {
    FieldModelHeader *table = (FieldModelHeader *)data;
    FieldModelColor *entry = (FieldModelColor *)(data + table->colorOffset);
    int i;

    for (i = 0; i < table->colorCount;) {
        i++;
        entry->rgb.r = r;
        entry->rgb.g = g;
        entry->rgb.b = b;
        entry++;
    }
}

void FieldFx_ModelShiftColors(char *data, int rDelta, int gDelta, int bDelta) {
    FieldModelHeader *table = (FieldModelHeader *)data;
    FieldModelColor *entry = (FieldModelColor *)(data + table->colorOffset);
    int i;
    s16 value;
    s16 blue;

    for (i = 0; i < table->colorCount; i++, entry++) {
        value = entry->rgb.r;
        value += rDelta;
        if (value >= 0x100) {
            value = 0xFF;
        }
        if (value < 0) {
            value = 0;
        }
        entry->rgb.r = value;
        value = entry->rgb.g;
        value += gDelta;
        if (value >= 0x100) {
            value = 0xFF;
        }
        if (value < 0) {
            value = 0;
        }
        entry->rgb.g = value;
        blue = entry->rgb.b;
        blue += bDelta;
        if (blue >= 0x100) {
            blue = 0xFF;
        }
        if (blue < 0) {
            blue = 0;
        }
        entry->rgb.b = blue;
    }
}

/* Composes the placement onto the view matrix, then walks the flat and
 * gouraud textured triangles and quads, culling back faces by NCLIP and
 * linking the visible ones at their averaged depth. */
void FieldFx_ModelDraw(FieldTexturedModel *model, GteMatrix *placement)
{
    FieldModelHeader *header;
    FieldModelTri *tri;
    FieldModelQuad *quad;
    FieldModelVertex *v0;
    FieldModelVertex *v1;
    FieldModelVertex *v2;
    FieldModelVertex *v3;
    FieldModelFt3Packet *ft3;
    FieldModelGt3Packet *gt3;
    FieldModelFt4Packet *ft4;
    FieldModelGt4Packet *gt4;
    FieldModelScratch *sp;
    int i;

    FieldModelScratch workspace;
    sp = &workspace;
    sp->tpage = g_FieldFxModelTpage;
    sp->clut = g_FieldFxModelClut;
    sp->semiTrans = g_FieldFxModelSemiTrans;
    CompMatrix(g_GeomScroll.words.viewMatrix, placement, &sp->matrix);
    SetTransMatrix(&sp->matrix);
    SetRotMatrix(&sp->matrix);
    Psyz_GteSetPgxpEffectMode(1);
    {
        const GteMatrix *view_mat = (const GteMatrix *)g_GeomScroll.words.viewMatrix;
        double pgxp_rot[3][3];
        double pgxp_trans[3];
        int r, c;
        for (r = 0; r < 3; ++r) {
            pgxp_trans[r] = (double)view_mat->t[r] +
                ((double)view_mat->m[r][0] * (double)placement->t[0] +
                 (double)view_mat->m[r][1] * (double)placement->t[1] +
                 (double)view_mat->m[r][2] * (double)placement->t[2]) / 4096.0;
            for (c = 0; c < 3; ++c) {
                pgxp_rot[r][c] =
                    ((double)view_mat->m[r][0] * (double)placement->m[0][c] +
                     (double)view_mat->m[r][1] * (double)placement->m[1][c] +
                     (double)view_mat->m[r][2] * (double)placement->m[2][c]) /
                    (4096.0 * 4096.0);
            }
        }
        Psyz_GteSetPgxpMatrix(pgxp_rot, pgxp_trans);
    }
    header = &model->header;
    tri = header->tris;
    ft3 = (FieldModelFt3Packet *)(D_800B0E38.packets[g_ActiveDrawSlot] + g_DrawPacketOffset);
    for (i = 0; i < header->flatTris; i++, tri++) {
        v0 = MODEL_VERTEX(model, tri->vertex[0]);
        v1 = MODEL_VERTEX(model, tri->vertex[1]);
        v2 = MODEL_VERTEX(model, tri->vertex[2]);
        gte_ldv3(v0, v1, v2);
        ft3->uv0 = tri->uv[0];
        gte_rtpt_padded();
        ft3->uv1 = tri->uv[1];
        ft3->uv2 = tri->uv[2];
        gte_nclip();
        gte_stmac0(&sp->winding);
        if (sp->winding > 0) {
            gte_avsz3_padded();
            gte_stotz(&sp->depth);
            sp->depth += g_FieldFxModelDepthBias;
            ft3->tpage = sp->tpage;
            ft3->clut = sp->clut;
            if (sp->depth > 0 && sp->depth < 4096) {
                FieldModelLink link;

                ft3->color0.word = MODEL_COLOR(model, v0->color);
                gte_stsxy3(&ft3->x0, &ft3->x1, &ft3->x2);
                ft3->tag.length = 7;
                ft3->color0.rgb.code = 0x24;
                if (sp->semiTrans) {
                    ft3->color0.rgb.code |= 2;
                } else {
                    ft3->color0.rgb.code &= ~2;
                }
                ft3->tag.address = TEXTURED_MODEL_OT(sp->depth)->address;
                link.tag = &ft3->tag;
                g_DrawPacketOffset += sizeof(FieldModelFt3Packet);
                TEXTURED_MODEL_OT(sp->depth)->address = link.word;
                ft3++;
            }
        }
    }
    gt3 = (FieldModelGt3Packet *)ft3;
    for (i = 0; i < header->shadedTris; i++, tri++) {
        v0 = MODEL_VERTEX(model, tri->vertex[0]);
        v1 = MODEL_VERTEX(model, tri->vertex[1]);
        v2 = MODEL_VERTEX(model, tri->vertex[2]);
        gte_ldv3(v0, v1, v2);
        gt3->uv0 = tri->uv[0];
        gte_rtpt_padded();
        gt3->uv1 = tri->uv[1];
        gt3->uv2 = tri->uv[2];
        gte_nclip();
        gte_stmac0(&sp->winding);
        if (sp->winding > 0) {
            gte_avsz3_padded();
            gt3->tpage = sp->tpage;
            gt3->clut = sp->clut;
            gte_stotz(&sp->depth);
            sp->depth += g_FieldFxModelDepthBias;
            if (sp->depth > 0 && sp->depth < 4096) {
                FieldModelLink link;

                gt3->color0.word = MODEL_COLOR(model, v0->color);
                gt3->color1.word = MODEL_COLOR(model, v1->color);
                gt3->color2.word = MODEL_COLOR(model, v2->color);
                gte_stsxy3(&gt3->x0, &gt3->x1, &gt3->x2);
                gt3->tag.length = 9;
                gt3->color0.rgb.code = 0x34;
                if (sp->semiTrans) {
                    gt3->color0.rgb.code |= 2;
                } else {
                    gt3->color0.rgb.code &= ~2;
                }
                gt3->tag.address = TEXTURED_MODEL_OT(sp->depth)->address;
                link.tag = &gt3->tag;
                g_DrawPacketOffset += sizeof(FieldModelGt3Packet);
                TEXTURED_MODEL_OT(sp->depth)->address = link.word;
                gt3++;
            }
        }
    }
    quad = (FieldModelQuad *)tri;
    ft4 = (FieldModelFt4Packet *)gt3;
    for (i = 0; i < header->flatQuads; i++, quad++) {
        v0 = MODEL_VERTEX(model, quad->vertex[0]);
        v1 = MODEL_VERTEX(model, quad->vertex[1]);
        v2 = MODEL_VERTEX(model, quad->vertex[2]);
        gte_ldv3(v0, v1, v2);
        ft4->uv0 = quad->uv[0];
        gte_rtpt_padded();
        ft4->uv1 = quad->uv[1];
        ft4->uv2 = quad->uv[2];
        gte_nclip();
        ft4->uv3 = quad->uv[3];
        gte_stmac0(&sp->winding);
        if (sp->winding > 0) {
            gte_avsz3_padded();
            ft4->tpage = sp->tpage;
            ft4->clut = sp->clut;
            gte_stotz(&sp->depth);
            sp->depth += g_FieldFxModelDepthBias;
            if (sp->depth > 0 && sp->depth < 4096) {
                FieldModelLink link;

                ft4->color0.word = MODEL_COLOR(model, v0->color);
                gte_stsxy3(&ft4->x0, &ft4->x1, &ft4->x2);
                v3 = MODEL_VERTEX(model, quad->vertex[3]);
                gte_ldv0(v3);
                gte_rtps();
                gte_stsxy2(&ft4->x3);
                ft4->tag.length = 9;
                ft4->color0.rgb.code = 0x2C;
                if (sp->semiTrans) {
                    ft4->color0.rgb.code |= 2;
                } else {
                    ft4->color0.rgb.code &= ~2;
                }
                ft4->tag.address = TEXTURED_MODEL_OT(sp->depth)->address;
                link.tag = &ft4->tag;
                g_DrawPacketOffset += sizeof(FieldModelFt4Packet);
                TEXTURED_MODEL_OT(sp->depth)->address = link.word;
                ft4++;
            }
        }
    }
    gt4 = (FieldModelGt4Packet *)ft4;
    for (i = 0; i < header->shadedQuads; i++, quad++) {
        v0 = MODEL_VERTEX(model, quad->vertex[0]);
        v1 = MODEL_VERTEX(model, quad->vertex[1]);
        v2 = MODEL_VERTEX(model, quad->vertex[2]);
        gte_ldv3(v0, v1, v2);
        gt4->uv0 = quad->uv[0];
        gte_rtpt_padded();
        gt4->uv1 = quad->uv[1];
        gt4->uv2 = quad->uv[2];
        gte_nclip();
        gte_stmac0(&sp->winding);
        if (sp->winding > 0) {
            gte_avsz3_padded();
            gt4->uv3 = quad->uv[3];
            gte_stotz(&sp->depth);
            sp->depth += g_FieldFxModelDepthBias;
            if (sp->depth > 0 && sp->depth < 4096) {
                FieldModelLink link;

                gt4->color0.word = MODEL_COLOR(model, v0->color);
                gte_stsxy3(&gt4->x0, &gt4->x1, &gt4->x2);
                v3 = MODEL_VERTEX(model, quad->vertex[3]);
                gt4->color1.word = MODEL_COLOR(model, v1->color);
                gte_ldv0(v3);
                gt4->color2.word = MODEL_COLOR(model, v2->color);
                gte_rtps();
                gt4->color3.word = MODEL_COLOR(model, v3->color);
                gte_stsxy2(&gt4->x3);
                gt4->tpage = sp->tpage;
                gt4->clut = sp->clut;
                gt4->tag.length = 12;
                gt4->color0.rgb.code = 0x3C;
                if (sp->semiTrans) {
                    gt4->color0.rgb.code = 0x3E;
                } else {
                    gt4->color0.rgb.code = 0x3C;
                }
                gt4->tag.address = TEXTURED_MODEL_OT(sp->depth)->address;
                link.tag = &gt4->tag;
                g_DrawPacketOffset += sizeof(FieldModelGt4Packet);
                TEXTURED_MODEL_OT(sp->depth)->address = link.word;
                gt4++;
            }
        }
    }
    Psyz_GteSetPgxpEffectMode(0);
    Psyz_GteClearPgxpMatrix();
}

void FieldFx_TransformPackedVertex(RenderPackedGeometry *geometry,
                                   GteMatrix *matrix, u16 index,
                                   GteShortVector *out) {
    u16 vertex_offset;
    u32 translation;

    /* The packed format deliberately truncates the byte offset to 16 bits. */
    vertex_offset = 16
        + (geometry->counts.record12_counts[0] + geometry->counts.record12_counts[1]) * 12
        + (geometry->counts.record16_counts[0] + geometry->counts.record16_counts[1]) * 16;
    ApplyMatrixSV(matrix,
                  (GteShortVector *)&geometry->bytes[vertex_offset] + index,
                  out);
    translation = matrix->t[0];
    out->x += translation;
    translation = matrix->t[1];
    out->y += translation;
    translation = matrix->t[2];
    out->z += translation;
}
