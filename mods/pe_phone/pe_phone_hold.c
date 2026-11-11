/* Keeping Aya on the phone through the question and the call.
 *
 * Seen headless in M087: when the save is written (save.committed, the save screen still up), the
 * phone's task is already past the save opcode (231) and waits at opcode 48 (wait for the animation)
 * for Aya's put-down: animation 0x26 from frame 0x3C (receiver at her ear) to 0x5F, then the
 * hang-up sound and the rest of it, then the field unlock. The field doesn't run while the save
 * screen is up, so she is still at frame 0x3C when it closes.
 *
 * So the phone's task is left alone: while held, Aya's animation step is 0 (PE_HOOK_ACTOR_STEP, a
 * local value; her actor is never written), she keeps the receiver at her ear, the task keeps
 * waiting, and the phone's own field lock stays on. Released, her animation runs on and the game's
 * own put-down and hang-up sound play, then the phone unlocks the field as always.
 *
 * Found by shape, not by room: a live task on Aya's actor waiting at opcode 48. Where none is found
 * (a phone scripted another way), the call runs after the phone is done with her, and a phone
 * listed in k_poses puts her back in its own pose for the call, as its script's opcodes 46 / 78 / 47
 * do: the Day 1 sewer's emergency phone plays animation 30 to frame 14 for "Use the phone" and
 * snaps her to idle (0x15) after the save; the call replays that reach and holds it, and she goes
 * back to idle when the call ends. */
#include <stdint.h>

#include "pe_game.h"
#include "pe_mod_host.h"
#include "pe_phone.h"
#include "pe1/entity_animation.h"
#include "pe1/field_actor.h"
#include "pe1/script_node.h"

#define PE_NODE_FREE   0x10
#define PE_SCRIPT_SPAN 0x10000   /* how far past an actor's script base a cursor is believed */
#define PE_OP_WAIT_ANIM 48

#define PE_ANIM_IDLE    0x15
#define PE_ACTOR_FROZEN 0x100      /* FieldActor.flags: animation not advanced (op 46 clears it) */
#define PE_ACTOR_TARGET 0x200      /* FieldActor.flags: stop at anim_frame_target (op 47 sets it) */

static const void *s_actor;      /* Aya, while held */
static FieldActor *s_posed;      /* Aya, while put in a phone's pose */

/* Phones whose script lets go of Aya before the save ends: the pose its script used. */
static const struct { short room; unsigned char mode, frame; } k_poses[] = {
    {32, 30, 14},                  /* Day 1 sewer, emergency phone: the reach for the receiver */
};

static int s_pose_room = -1, s_pose_wait;   /* a pose asked for, put on after the lock settles */

int pe_phone_hold_pose(int room) {
    unsigned i;
    if (s_actor || s_posed) return 0;
    for (i = 0; i < sizeof k_poses / sizeof k_poses[0]; ++i)
        if (k_poses[i].room == room) {
            s_pose_room = room;
            s_pose_wait = 3;      /* the field lock queued with it puts her in idle when it runs */
            return 1;
        }
    return 0;
}

static void pe_pose_now(int room) {
    FieldActor *aya = (FieldActor *)pe_player();
    unsigned i;
    if (s_actor || s_posed || !aya) return;
    for (i = 0; i < sizeof k_poses / sizeof k_poses[0]; ++i) {
        unsigned frame = k_poses[i].frame;
        if (k_poses[i].room != room) continue;
        Entity_SetActionMode((BattleEntity *)aya, k_poses[i].mode);   /* op 46 */
        aya->flags &= ~PE_ACTOR_FROZEN;
        aya->anim.fixed = 0;                                          /* op 78: from frame 0 */
        if (frame > aya->action) frame = aya->action;                 /* op 47: to the pose, and stop */
        aya->anim_frame_target = (unsigned short)frame;
        aya->flags |= PE_ACTOR_TARGET;
        s_posed = aya;
        pe_log("phone_hold", "posed=%d frame=%u", k_poses[i].mode, frame);
        return;
    }
}

void pe_phone_hold_tick(void) {
    if (s_pose_room < 0 || --s_pose_wait > 0) return;
    pe_pose_now(s_pose_room);
    s_pose_room = -1;
}

static void pe_unpose(void) {
    FieldActor *aya = (FieldActor *)pe_player();
    s_pose_room = -1;
    if (s_posed && aya == s_posed) {                                  /* as the phone's script ends */
        Entity_SetActionMode((BattleEntity *)aya, PE_ANIM_IDLE);
        aya->flags &= ~PE_ACTOR_FROZEN;
        pe_log("phone_hold", "unposed=1");
    }
    s_posed = NULL;
}

static int pe_waits_for_anim(const FieldActor *a) {
    const uint8_t *base = a->script_base;
    int list, guard;
    if (!base) return 0;
    for (list = 0; list < 3; ++list) {
        const ScriptNode *n = a->script_node_lists[list];
        for (guard = 0; n && guard < 32; ++guard, n = n->next) {
            const uint32_t *c = n->current.script;
            const uint8_t *at = (const uint8_t *)c;
            if ((n->flags & PE_NODE_FREE) || !c || ((uintptr_t)c & 3) || at < base || at >= base + PE_SCRIPT_SPAN) continue;
            if ((c[0] & 0x1FFF) == PE_OP_WAIT_ANIM) return 1;
        }
    }
    return 0;
}

int pe_phone_hold_begin(void) {
    const FieldActor *aya = (const FieldActor *)pe_player();
    s_actor = NULL;
    if (!aya || !pe_waits_for_anim(aya)) {
        pe_log("phone_hold", "held=0");
        return 0;
    }
    s_actor = aya;
    pe_log("phone_hold", "held=1");
    return 1;
}

int pe_phone_hold_active(void) { return s_actor != NULL; }

void pe_phone_hold_release(void) {
    if (s_actor) pe_log("phone_hold", "released=1");
    s_actor = NULL;
    pe_unpose();
}

void pe_phone_hold_forget(void) {    /* a room load or a loaded save: the actor is gone or new */
    s_actor = NULL;
    s_posed = NULL;
    s_pose_room = -1;
}

static int pe_on_actor_step(void *ctx, void *user) {
    PeHookActorStep *step = ctx;
    (void)user;
    if (s_actor && step && step->anim && step->actor == s_actor) *step->anim = 0;
    return 0;
}

void pe_phone_hold_register(const char *mod_id) {
    pe_hook_add(PE_HOOK_ACTOR_STEP, pe_on_actor_step, NULL, PE_PRIORITY_LATE, mod_id);   /* after speed mods */
}
