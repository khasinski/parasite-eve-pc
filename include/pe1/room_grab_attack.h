#ifndef PE1_ROOM_GRAB_ATTACK_H
#define PE1_ROOM_GRAB_ATTACK_H

#include "common.h"
#include "pe1/pm_types.h"
#include "pe1/akao_send.h"
#include "pe1/battle_context.h"
#include "pe1/battle_setup.h"
#include "pe1/field_collision.h"
#include "pe1/field_sprite_draw.h"
#include "pe1/field_vector_math.h"
#include "pe1/geom_state.h"
#include "pe1/gte.h"
#include "pe1/room_fx.h"
#include "pe1/room_ground_sweep.h"
#include "pe1/room_interface.h"
#include "pe1/room_motion_trigger.h"
#include "pe1/room_orbit_trail.h"
#include "pe1/room_rising_spray.h"
#include "pe1/room_sound_slot.h"
#include "pe1/room_spark.h"

/* The grab attack set (src/overlays/room_lib/grab_attack.c, in
 * room_m269 and scene_e01). */

/* The class's state words from 0x0C on, as the player reset receives them. */
typedef struct RoomGrabStateTail {
    /* 0x00 */ void (*callback)(RoomMotionTrigger *);
    /* 0x04 */ s32 *completion_state;
    /* 0x08 */ u8 reserved08[8];
    /* 0x10 */ FieldActor *source_actor;
    /* 0x14 */ s32 saved_x;
    /* 0x18 */ s32 saved_z;
    /* 0x1C */ u8 activated;
} RoomGrabStateTail;

/* A 16.16 world point. */
typedef struct RoomGrabPoint {
    s32 x, y, z;
} RoomGrabPoint;

/* The spray controller's parameters: base launch speed and heading spread. */
typedef struct RoomRisingSprayParams {
    s32 speed;
    s32 spread;
} RoomRisingSprayParams;

/* Per-overlay data, named in each symbol file: the side the sweep starts
 * on, the orbit centre the trails sweep out from, the trails' track, the
 * slot and the pair of attack arguments the setters fill. */
extern s32 g_RoomGrabSweepSide;
extern GteShortVector g_RoomGrabOrbitCentre;
extern u8 g_RoomGrabTrailTrack[];
extern int g_RoomGrabSlot;
extern int g_RoomGrabPair[2];
#define RoomLib_PairA g_RoomGrabPair[0]
#undef RoomLib_PairB
#define RoomLib_PairB g_RoomGrabPair[1]

void RoomEffect_GrabNop0(void);
void RoomEffect_GrabInit(PmSlotHeader *raw);
int RoomEffect_GrabConfigure(PmSlotHeader *obj, int arg1, int op, intptr_t arg3, intptr_t arg4, intptr_t arg5);
int RoomEffect_GrabNop3(PmSlotHeader *raw);
int RoomEffect_GrabUpdate(PmSlotHeader *raw);
void RoomEffect_GrabAwaitTarget(RoomMotionTrigger *obj);
void RoomEffect_GrabStartOnProximity(RoomMotionTrigger *arg);
void RoomEffect_GrabHold(RoomMotionTrigger *arg0);
void RoomEffect_GrabResetPlayer(FieldActor *unused, RoomGrabStateTail *state);
int RoomEffect_GrabRelease(PmSlotHeader *raw);
void RoomEffect_GrabNop6(void);
int RoomEffect_GroundSweepMark(int mode, void *data, intptr_t argument);
int RoomEffect_GroundSweepController(int mode, void *data, intptr_t argument);
int RoomEffect_RisingSprayController(int mode, void *data, intptr_t argument);
int RoomEffect_OrbitTrailParticle(int mode, void *data, intptr_t argument);
int RoomEffect_RisingSprayParticle(int mode, void *data, intptr_t argument);

int Render_FindAnimEntry(RenderObjectEntity *object, int count, s32 *out);
void Akao_Cmd_21(int handle, int arg);
#include "pe1/field_glow_line.h"

extern PmCommand g_RoomGrabHandlers;
extern FieldFxTaskProgram g_RoomGroundSweepProgram, g_RoomOrbitTrailProgram, g_RoomRisingSprayProgram;
#endif
