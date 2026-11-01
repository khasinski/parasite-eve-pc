#include "pe1/geom_state.h"
/*
 * The mirror room controller: seven script handlers (three of them empty)
 * that find an actor by kind, bind a mirror model to it across a guide
 * line and, every frame, reflect the actor's pose into the mirror model
 * and draw it.
 *
 * room_m017, room_m018, room_m021, room_m045, room_m102, room_m151 and
 * room_m319 are the same program: these ten functions in this order are
 * all of their code, and their data starts with the same handler table and
 * mirror axes. This unit is that object, compiled into each of them; the
 * room's model and images follow in its own data.
 */

#include "pe1/entity_frame_update.h"
#include <stdlib.h>
#include <string.h>
#include "pe1/gte.h"
#include "pe1/render_matrix_slot.h"
#include "pe1/render_object.h"
#include "pe1/render_prim.h"
#include "pe1/render_tint.h"
#include "pe1/room_fx_model.h"

void RoomEffect_ModelDraw(RenderObjectEntity *entity);

PmCommand g_RoomFxMirrorHandlers = {
    RoomEffect_MirrorNoOp0, RoomEffect_MirrorReset, RoomEffect_MirrorSelect, RoomEffect_MirrorTick,
    RoomEffect_MirrorNoOp4, RoomEffect_MirrorKill, RoomEffect_MirrorNoOp6,
};

/* Mirror across the YZ plane: x negated. */
GteMatrix g_RoomFxMirrorAxes = {
    { { -0x1000, 0, 0 }, { 0, 0x1000, 0 }, { 0, 0, 0x1000 } },
    { 0, 0, 0 },
};

void RoomEffect_MirrorNoOp0(void) {}

void RoomEffect_MirrorReset(PmSlotHeader *slot) {
    RoomFxMirror *mirror = (RoomFxMirror *)slot;
    mirror->target = 0;
    mirror->lineZ0 = 0;
    mirror->lineX0 = 0;
    mirror->lineZ1 = 0;
    mirror->lineX1 = 0;
    mirror->bound = 0;
    mirror->active = 0;
}

/* Script entry: mode 0 finds the first idle actor of the given kind,
 * modes 1 and 2 set the guide line's ends, mode 3 starts or stops the
 * mirror. */
int RoomEffect_MirrorSelect(PmSlotHeader *slot, int cancel, int mode, intptr_t a, intptr_t b, intptr_t unused) {
    RoomFxMirror *mirror = (RoomFxMirror *)slot;
    (void)unused;
    FieldActor **target = &mirror->target;

    switch (mode) {
    case 0:
        if (cancel != 0) {
            break;
        }
        *target = g_FieldActorListHead;
        while (*target != 0) {
            FieldActor *node = *target;
            if (node->type_id == a && node->sub_id == b && !(node->flags & 0x10)) {
                break;
            }
            *target = (*target)->next;
        }
        break;
    case 1:
        mirror->lineX0 = a;
        mirror->lineZ0 = b;
        break;
    case 2:
        mirror->lineX1 = a;
        mirror->lineZ1 = b;
        break;
    case 3:
        mirror->active = a;
        break;
    default:
        return -6;
    }
    return 0;
}

int RoomEffect_MirrorTick(PmSlotHeader *slot) {
    RoomFxMirror *mirror = (RoomFxMirror *)slot;
    FieldActor *object = mirror->header.owner;
    if (mirror->bound == 0) {
        RoomEffect_ModelBind(object, mirror->target, mirror->lineX0,
                         mirror->lineZ0, mirror->lineX1, mirror->lineZ1);
        mirror->bound = 1;
    }
    if (mirror->active != 0) {
        RoomEffect_ModelUpdate(object);
        Render_TransformVertices(&object->render_object);
        Render_TransformMorphVertices(&object->render_object,
                                      (u32 *)g_ViewMatrixSlot.value);
        RoomEffect_ModelDraw(&object->render_object);
    }
    return 0;
}

int RoomEffect_MirrorNoOp4(PmSlotHeader *slot) {
    (void)slot;
    return 0;
}

int RoomEffect_MirrorKill(PmSlotHeader *slot) {
    slot->state = 4;
    return 0;
}

void RoomEffect_MirrorNoOp6(void) {}

void RoomEffect_ModelBind(FieldActor *actor, FieldActor *owner, int x0, int z0, int x1, int z1)
{
    /* M319's script gives Aya's reflection a line tilted about 2.5 degrees
     * (z -902 to -1004) in front of a straight painted mirror, so her
     * reflection slides sideways the further she stands from it. Straighten
     * it at its middle depth; no other room uses this line. */
    if (x0 == 1123 && z0 == -902 && x1 == -1169 && z1 == -1004) {
        z0 = z1 = -953;
    }
    actor->render_object.animation_state = 5;
    actor->render_object.reflection_owner = owner;
    actor->mirror_line.x0 = (u16)x0;
    actor->mirror_line.z0 = (u16)z0;
    actor->render_object.flags_9C |= 0x1000;
    actor->mirror_line.x1 = (u16)x1;
    actor->mirror_line.z1 = (u16)z1;
}

/* Mirror the owner's model across the guide line: reflect its position
 * through the line, flip its rotation and point the model along the line,
 * then rebuild the animation matrices. */
void RoomEffect_ModelUpdate(FieldActor *actor) {
    RenderObjectEntity *geom = &actor->render_object;
    FieldActor *owner = geom->reflection_owner;
    RenderObjectEntity *parent = &owner->render_object;
    s16 dx = -actor->mirror_line.x0 + actor->mirror_line.x1;
    s16 dz = -actor->mirror_line.z0 + actor->mirror_line.z1;
    int slope;
    int x, z;
    int distance;
    int angle;
    int sx, sz;
    int yaw;
    int nz, nx;
    int intercept;
    int adx;
    int rx;
    int rz;

    if (dx == 0) {
        dx = 1;
    }
    slope = dz / dx;
    intercept = (s16)actor->mirror_line.z1 - slope * (s16)actor->mirror_line.x1;
    x = owner->render_object.model_matrix.translation[0];
    z = owner->render_object.model_matrix.translation[2];
    distance = slope * x - z + intercept;
    if (distance < 0) {
        distance = -distance;
    }
    distance /= SquareRoot0(slope * slope + 1);
    angle = ratan2(dz, dx);
    sx = (rsin(angle) * distance) >> 12;
    sz = (rcos(angle) * distance) >> 12;
    nx = x - sx * 2;
    geom->model_matrix.translation[0] = nx;
    geom->model_matrix.translation[1] = owner->render_object.model_matrix.translation[1];
    nz = z + sz * 2;
    geom->model_matrix.translation[2] = nz;
    adx = abs(dx);
    rx = -(s16)owner->render_object.table_value2c;
    if (dz >= 0 ? dz < adx : -dz < adx) {
        if (z < nz) {
            nz = angle + 0x800;
            yaw = (s16)parent->table_value2e + nz;
        } else {
            yaw = (s16)parent->table_value2e + angle;
        }
    } else if (nx < x) {
        nz = angle + 0xC00;
        yaw = (s16)parent->table_value2e + nz;
    } else {
        nz = angle + 0x400;
        yaw = (s16)parent->table_value2e + nz;
    }
    nz = -yaw;
    rz = -(s16)parent->table_value30;
    geom->table_value2c = (u16)rx;
    geom->table_value2e = (u16)nz;
    geom->table_value30 = (u16)rz;
    GteShortVector angles = {(s16)rx, (s16)nz, (s16)rz, 0};
    GteMatrix rotated;
    RotMatrix(&angles, &rotated);
    memcpy(geom->model_matrix.rotation, rotated.m, sizeof(rotated.m));
    gte_ldrotmatrix(&geom->model_matrix);
    gte_ldclmv(&g_RoomFxMirrorAxes.m[0][0]);
    gte_rtir();
    gte_stclmv(&geom->model_matrix.rotation[0][0]);
    gte_ldclmv(&g_RoomFxMirrorAxes.m[0][1]);
    gte_rtir();
    gte_stclmv(&geom->model_matrix.rotation[0][1]);
    gte_ldclmv(&g_RoomFxMirrorAxes.m[0][2]);
    gte_rtir();
    gte_stclmv(&geom->model_matrix.rotation[0][2]);
    Anim_BuildRotationMatrices(geom, owner->action_data, (s16)owner->anim.parts.integer);
}

/* Native PsyZ packet links retain the complete host address. */
#define ROOMFX_SETADDR(p, a) (((RenderNativeTag *)(p))->address = (uintptr_t)(a))
#define ROOMFX_GETADDR(p) (((RenderNativeTag *)(p))->address)
#define ROOMFX_ADDPRIM(ot, p) \
    ROOMFX_SETADDR(p, ROOMFX_GETADDR(ot)), ROOMFX_SETADDR(ot, p)

/* Room build of the main binary's Render_DrawTexturedQuads: NCLIP-tests each
 * face on the projected vertices and links the front-facing ones (negative
 * winding) into the active ordering table at their average depth; culled
 * faces get a null link. Quads and triangles of both packet classes are
 * walked in turn through one shared descriptor cursor. */
void RoomEffect_ModelDraw(RenderObjectEntity *entity) {
    int i;
    s32 area_value;
    s32 *area = &area_value;
    u32 *xy = D_800B1644;
    RenderPrimitiveDescriptor *record = entity->primitive_descriptors;
    u8 *packets = entity->primitive_buffer;
    int slot = g_ActiveDrawSlot;
    RenderNativeTag *ordering = D_800B0E38.ordering[slot];
    s32 *z = D_800A636C;
    u32 xy0, xy1, xy2;
    int a, b, c, d;
    u8 *packet;

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
        packet = (u8 *)((RenderPacket34 *)packets + slot);
        d = record->lookup_indices[3];
        gte_stmac0(area);
        if (Psyz_GtePgxpNclip4(&xy[a], xy0, &xy[b], xy1, &xy[c], xy2, &xy[d], xy[d], *area) < 0) {
            int depth = z[a] + z[b];
            depth += z[c];
            depth += z[d];
            depth >>= 4;
            if (depth >= 0 && depth < 0x1000) {
                RenderNativeTag *entry = ordering + depth;
                RenderPacket34 *pkt = (RenderPacket34 *)packet;
                u32 xy3 = xy[d];
                ROOMFX_ADDPRIM(entry, packet);
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
            ROOMFX_SETADDR(packet, 0);
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
        packet = (u8 *)((RenderPacket28 *)packets + slot);
        gte_stmac0(area);
        if (Psyz_GtePgxpNclip3(&xy[a], xy0, &xy[b], xy1, &xy[c], xy2, *area) < 0) {
            int depth = z[a] + z[b] + z[c];
            depth = depth / 3 >> 2;
            if (depth >= 0 && depth < 0x1000) {
                RenderNativeTag *entry = ordering + depth;
                RenderPacket28 *pkt = (RenderPacket28 *)packet;
                ROOMFX_ADDPRIM(entry, packet);
                pkt->sxy0 = xy0;
                pkt->sxy1 = xy1;
                pkt->sxy2 = xy2;
                Psyz_GteCopyPgxpSxy(&xy[a], &pkt->sxy0, xy0);
                Psyz_GteCopyPgxpSxy(&xy[b], &pkt->sxy1, xy1);
                Psyz_GteCopyPgxpSxy(&xy[c], &pkt->sxy2, xy2);
            }
        } else {
            ROOMFX_SETADDR(packet, 0);
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
        packet = (u8 *)((RenderPacket24 *)packets + slot);
        d = record->lookup_indices[3];
        gte_stmac0(area);
        if (Psyz_GtePgxpNclip4(&xy[a], xy0, &xy[b], xy1, &xy[c], xy2, &xy[d], xy[d], *area) < 0) {
            int depth = z[a] + z[b];
            depth += z[c];
            depth += z[d];
            depth >>= 4;
            if (depth >= 0 && depth < 0x1000) {
                RenderNativeTag *entry = ordering + depth;
                RenderPacket24 *pkt = (RenderPacket24 *)packet;
                u32 xy3 = xy[d];
                ROOMFX_ADDPRIM(entry, packet);
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
            ROOMFX_SETADDR(packet, 0);
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
        packet = (u8 *)((RenderPacket1C *)packets + slot);
        gte_stmac0(area);
        if (Psyz_GtePgxpNclip3(&xy[a], xy0, &xy[b], xy1, &xy[c], xy2, *area) < 0) {
            int depth = z[a] + z[b] + z[c];
            depth = depth / 3 >> 2;
            if (depth >= 0 && depth < 0x1000) {
                RenderNativeTag *entry = ordering + depth;
                RenderPacket1C *pkt = (RenderPacket1C *)packet;
                ROOMFX_ADDPRIM(entry, packet);
                pkt->sxy0 = xy0;
                pkt->sxy1 = xy1;
                pkt->sxy2 = xy2;
                Psyz_GteCopyPgxpSxy(&xy[a], &pkt->sxy0, xy0);
                Psyz_GteCopyPgxpSxy(&xy[b], &pkt->sxy1, xy1);
                Psyz_GteCopyPgxpSxy(&xy[c], &pkt->sxy2, xy2);
            }
        } else {
            ROOMFX_SETADDR(packet, 0);
        }
        packets += 2 * sizeof(RenderPacket1C);
        i++;
        record++;
    }
}
