#ifndef PE1_ROOM_FX_MODEL_H
#define PE1_ROOM_FX_MODEL_H

#include "common.h"
#include "pe1/entity_frame_update.h"
#include "pe1/gte_types.h"

#include "pe1/field_actor.h"
#include "pe1/pm.h"

/* The mirror room controller (src/overlays/room_lib/mirror_model.c),
 * linked by room_m017, room_m018, room_m021, room_m045, room_m102,
 * room_m151 and room_m319: it finds an actor by kind, binds a mirror model
 * to it across a guide line and redraws the reflection every frame. */
typedef struct RoomFxMirror {
    PmSlotHeader header; /* owner is the actor carrying the mirror model */
    FieldActor *target;
    s16 lineX0, lineZ0, lineX1, lineZ1;
    u8 bound, active;
} RoomFxMirror;

extern PmCommand g_RoomFxMirrorHandlers;
extern GteMatrix g_RoomFxMirrorAxes;
void RoomEffect_MirrorNoOp0(void);
void RoomEffect_MirrorReset(PmSlotHeader *slot);
int RoomEffect_MirrorSelect(PmSlotHeader *slot, int cancel, int mode, intptr_t a, intptr_t b, intptr_t unused);
int RoomEffect_MirrorTick(PmSlotHeader *slot);
int RoomEffect_MirrorNoOp4(PmSlotHeader *slot);
int RoomEffect_MirrorKill(PmSlotHeader *slot);
void RoomEffect_MirrorNoOp6(void);
void RoomEffect_ModelBind(FieldActor *actor, FieldActor *owner, int x0, int z0, int x1, int z1);
int RoomEffect_CopyMirrorModel(FieldActor *actor, RenderObjectEntity *copy);
void RoomEffect_ModelUpdate(FieldActor *actor);
void RoomEffect_ModelDraw(RenderObjectEntity *entity);

#endif
