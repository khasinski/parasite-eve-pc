#include "common.h"
#include "pe1/battle.h"
#include "pe1/battle_main.h"
#include "pe1/battle_runtime.h"
#include "pe1/field_movement.h"
#include "pe1/gte_types.h"
#include "pe1/item_ids.h"
#include <stdio.h>
#include <stdlib.h>
#include "pe1/aya.h"
#include "pe1/battle_status.h"
#include "pe1/battle_update.h"
#include "pe1/draw_state.h"
#include "pe1/game_state.h"
#include "pe1/render_packets.h"
#include "pe1/render_prim.h"
#include "pe1/scene_dispatch.h"
#include <psyz/video.h>
void SetPolyF3(void *);

enum {
    MELEE_IDLE, MELEE_OFFERED, MELEE_ACCEPTED, MELEE_REPLAY, MELEE_SPENT,
    MELEE_WINDOW = 24,         /* 0.8 seconds at 30 battle updates per second */
    MELEE_BACKHAND_STEP = 0x28000,  /* 2.5 animation frames per update */
    MELEE_SPARK_UPDATES = 7,
    MELEE_CROSS = 0x200,
    MELEE_SIDE_MIN = 683,  /* 60 degrees on the 4096-step turn */
    MELEE_SIDE_LIMIT = 1366,  /* first step at or above 120 degrees */
    MELEE_CUE_UPDATES = 30,   /* how long "OUT OF REACH" stays up */
};

static const unsigned amphitheaterScene = 0xA80072C8u;

static struct {
    BattleEntity *target;
    int phase;
    int impactFrame;
    int firstDamage;
    int strikeMode;
    int backhand;
    BattleInitSlot slot;
} melee;

static struct {
    int frame;
    int x;
    int y;
} meleeSpark = { -MELEE_SPARK_UPDATES, 0, 0 };

/* The cue outlives the attack, so Battle_MeleeReset leaves it alone. */
static struct {
    int start;
    int until;
} meleeCue;

/* Target distance over the last few battle updates while the follow-up is pending. */
enum { MELEE_HISTORY = 3 };
static struct {
    int distance[MELEE_HISTORY];
    int count;
} meleeTrack;

static void Melee_TrackDistance(void)
{
    int i;

    for (i = MELEE_HISTORY - 1; i > 0; i--)
        meleeTrack.distance[i] = meleeTrack.distance[i - 1];
    meleeTrack.distance[0] = Battle_MeleeClubDistance(melee.target, D_8009D254);
    if (meleeTrack.count < MELEE_HISTORY)
        meleeTrack.count++;
}

/* Predict the distance when the replayed swing's hit resolves, hitTrigger updates from now, by
 * extending the target's recent movement, then apply the reach rule Battle_CheckEvasion uses
 * for a club swing: past 1000 per mille it misses. The target's aim point moves with its
 * animation, so the reach at swing start alone lets drifting targets slip out before contact. */
static int Melee_PredictInReach(BattleEntity *target, int *predicted)
{
    int range = AYA_DARKNESS_RANGE;
    int distance = Battle_MeleeClubDistance(target, D_8009D254);
    int steps = g_ActiveActor->action->hitTrigger;

    if ((g_ActiveActor->stateFlags & AYA_DARKNESS_MASK) != AYA_DARKNESS)
        range = g_ActiveActor->action->range;
    *predicted = distance;
    if (meleeTrack.count > 1) {
        int oldest = meleeTrack.distance[meleeTrack.count - 1];
        *predicted = distance + (distance - oldest) * steps / (meleeTrack.count - 1);
    }
    return range > 0 && *predicted * 1000 / range < 1001;
}

void Battle_MeleeReset(void)
{
    if (melee.backhand && D_8009D254->actionMode == melee.strikeMode)
        D_8009D254->entityFlags &= ~0x100;
    melee.backhand = 0;
    melee.target = NULL;
    melee.phase = MELEE_IDLE;
}

static int Melee_HasClub(void)
{
    int slot = g_AyaSaveState.equipped_weapon_slot;
    unsigned value;

    if (slot < 0 || slot >= (int)(sizeof(g_InventoryRuntime.slots) / sizeof(g_InventoryRuntime.slots[0])))
        return 0;
    value = g_InventoryRuntime.slots[slot];
    if (value - 0x100u < 0x80u)
        value = g_InventoryRuntime.equipment[value - 0x100u].itemId;
    return value == ITEM_CLUB_1;
}

static int Melee_IsAttack(const BattleEntity *target)
{
    return g_BattleActionQueueCount == 1 && g_BattleActionIndex == 0
        && g_BattleActionQueue[0].actor == target && g_BattleActionQueue[0].command == 1
        && g_ActiveActor->action->actionCode.actionId == 8
        && !(g_ActiveActor->stateFlags & 0x180000)
        && !(g_ActiveActor->action->turnWord & 0x6000);
}

int Battle_MeleeOnDamage(BattleEntity *target, int damage)
{
    const EnemyCombatant *enemy = target->core;
    int angle;

    if (melee.phase == MELEE_REPLAY && melee.target == target && Melee_IsAttack(target)) {
        int bonus = melee.firstDamage / 4;
        melee.phase = MELEE_SPENT;
        fprintf(stderr, "club_flank hit frame=%d damage=%d\n", g_BattleFrameCounter, bonus ? bonus : 1);
        return bonus ? bonus : 1;
    }
    if (!Battle_MeleeFeatureEnabled(BATTLE_MELEE_FOLLOW_UP)
        || (unsigned)g_SceneDispatchToken != amphitheaterScene
        || target->modelIndex != 5 || enemy->field04.fieldId04 != 12
        || melee.phase != MELEE_IDLE || damage >= enemy->hpAlive
        || !Melee_IsAttack(target) || !Melee_HasClub())
        return damage;
    if (target->posX.fixed == D_8009D254->posX.fixed && target->posZ.fixed == D_8009D254->posZ.fixed)
        return damage;

    angle = Battle_CalcRelativeAngle(D_8009D254, target) & 0xfff;
    if (angle > 2048)
        angle = 4096 - angle;
    fprintf(stderr, "club_flank first frame=%d angle=%d damage=%d distance=%d\n", g_BattleFrameCounter, angle,
            damage, Battle_MeleeClubDistance(target, D_8009D254));
    if (angle < MELEE_SIDE_MIN || angle >= MELEE_SIDE_LIMIT)
        return damage;

    melee.target = target;
    melee.phase = MELEE_OFFERED;
    melee.impactFrame = g_BattleFrameCounter;
    melee.firstDamage = damage;
    melee.strikeMode = g_ActiveActor->followUpAngleActionModes[1];
    melee.slot = g_BattleActionQueue[0];
    meleeTrack.count = 0;
    fprintf(stderr, "club_flank offer frame=%d\n", g_BattleFrameCounter);
    return damage;
}

/* Opt-in per-update trace of Aya's club attack (set PE_MELEE_TRACE=1). */
static void Melee_Trace(void)
{
    static int enabled = -1;
    BattleEntity *aya = D_8009D254;

    if (enabled < 0)
        enabled = getenv("PE_MELEE_TRACE") != NULL;
    if (!enabled || !aya || !g_ActiveActor || !g_ActiveActor->action
        || g_ActiveActor->action->actionCode.actionId != 8)
        return;
    fprintf(stderr, "club_trace frame=%d mode=%d anim=%d last=%d prev=%d modes=%d,%d,%d,%d hit=%d/%d queue=%d/%d"
            " state=%x atb=%d hp=%d\n",
            g_BattleFrameCounter, aya->actionMode, (int)(aya->animFrame >> 16), aya->animLastFrame,
            aya->animPrev.parts.integer, g_BattleActionAnimModes[0], g_BattleActionAnimModes[1],
            g_BattleActionAnimModes[2], g_BattleActionAnimModes[3], g_BattleHitTimer,
            g_ActiveActor->action->hitTrigger, g_BattleActionIndex, g_BattleActionQueueCount,
            g_ActiveActor->stateFlags, g_ActiveActor->hpAlive, g_ActiveActor->curHP);
}

/* Club 1's windup (action modes 6, 8 and 10, picked by target height) takes 29 updates, so a
 * swing lands about 34 updates after commit: longer than the amphitheater mutant's 24-update
 * recovery. Advance the windup animation by MELEE_WINDUP_SPEED frames per update instead of 1.
 * The last two frames play normally, so the native last-frame check in
 * Battle_StepCharacterAction still sees the final frame and starts the strike. A hurt flinch
 * interrupts the windup with its own mode and is left at native speed.
 * PE_MELEE_WINDUP_SPEED=1 restores native timing for A/B harness runs. */
enum { MELEE_WINDUP_SPEED = 2 };

static void Melee_SpeedWindup(void)
{
    static int speed = 0;
    BattleEntity *aya = D_8009D254;
    int mode, frame, extra;

    if (!Battle_MeleeFeatureEnabled(BATTLE_MELEE_FAST_WINDUP))
        return;
    if (!speed) {
        const char *value = getenv("PE_MELEE_WINDUP_SPEED");
        speed = value ? atoi(value) : MELEE_WINDUP_SPEED;
        if (speed < 1 || speed > 4)
            speed = MELEE_WINDUP_SPEED;
    }
    if (speed == 1 || !aya || !g_ActiveActor || !g_ActiveActor->action || g_ActiveActor->action->actionCode.actionId != 8)
        return;
    mode = aya->actionMode;
    if ((mode != 6 && mode != 8 && mode != 10) || g_BattleActionQueueCount == 0)
        return;
    if ((unsigned)g_SceneDispatchToken != amphitheaterScene || !Melee_HasClub())
        return;
    frame = (int)(aya->animFrame >> 16);
    for (extra = speed - 1; extra > 0 && frame + 1 < aya->animLastFrame; extra--)
        frame++;
    aya->animFrame = (aya->animFrame & 0xFFFF) | ((s32)frame << 16);
}

/* The amphitheater mutant (model bank 5, identity 12), for the enemy trace. */
static BattleEntity *Melee_FindMutant(void)
{
    BattleEntity *entity;

    for (entity = (BattleEntity *)g_FieldActorListHead; entity; entity = entity->next) {
        const EnemyCombatant *enemy = entity->core;
        if (entity != D_8009D254 && enemy && entity->modelIndex == 5 && enemy->field04.fieldId04 == 12
            && enemy->hpAlive > 0)
            return entity;
    }
    return NULL;
}

/* Distance on the ground from Aya's position to the nearest surface of `enemy`'s body: its hit
 * cylinder and every body hit sphere (the ones Scene_UpdateEntityPositions tests for contact),
 * radii scaled by moveSpeed / 4096. Negative when Aya is inside one. Native club reach is instead
 * measured to a single aim point (Battle_CalcDistToPlayer), which on a long body can be far from
 * the part Aya is standing at. Returns a large value when the enemy has no body data. */
static int Melee_BodyDistance(BattleEntity *enemy, BattleEntity *aya)
{
    const RenderObjectEntity *ro = &enemy->renderObject;
    const RenderAnimationLookupEntry *part;
    int best = 0x7FFF, i, n;

    for (i = -1, n = ro->header && ro->animation_entries ? ro->header->animation_entry_count : 0; i < n; i++) {
        int dx, dz, d;
        part = i < 0 ? &ro->hit_cylinder : &ro->animation_entries[i];
        if (part->radius <= 0)
            continue;
        dx = part->value0 - aya->posX.parts.integer;
        dz = part->value2 - aya->posZ.parts.integer;
        d = Math_IntSqrt(dx * dx + dz * dz) - part->radius * enemy->moveSpeed / 4096;
        if (d < best)
            best = d;
    }
    return best;
}

/* Club reach measured to the enemy's body instead of its aim point. Native reach is the distance
 * to the aim point (Battle_CalcDistToPlayer), which sits inside one body sphere, usually at the
 * front (the amphitheater mutant's is its chest sphere, 276 ahead of its root). From behind a long
 * body, Aya can touch its back and still be ~650 from that point against a reach of 474: a miss
 * that looks like a hit. Here the distance is the nearest body surface plus the aim sphere's
 * radius, or the native distance if smaller. From the front the aim sphere is the nearest part,
 * so nothing changes there; on every other side the club gets the same reach past contact. It
 * never exceeds the native distance. Critical hit checks still use the native aim distance.
 * Applies only while Aya's action is a club swing. */
int Battle_MeleeClubDistance(BattleEntity *target, BattleEntity *player)
{
    const RenderObjectEntity *ro = &target->renderObject;
    const int aim = Battle_CalcDistToPlayer(target, player);
    int i, n, aimRadius = -1, best = 0x7FFFFFFF, body;

    if (!Battle_MeleeFeatureEnabled(BATTLE_MELEE_BODY_REACH)
        || !g_ActiveActor || !g_ActiveActor->action || g_ActiveActor->action->actionCode.actionId != 8
        || !ro->header || !ro->animation_entries)
        return aim;
    /* The sphere that holds the aim point; with none, keep the native distance. */
    for (i = 0, n = ro->header->animation_entry_count; i < n; i++) {
        const RenderAnimationLookupEntry *part = &ro->animation_entries[i];
        const int r = part->radius * target->moveSpeed / 4096;
        const int dx = part->value0 - ro->target_x, dz = part->value2 - ro->target_z;
        const int d = Math_IntSqrt(dx * dx + dz * dz);
        if (r > 0 && d <= r && d < best) {
            best = d;
            aimRadius = r;
        }
    }
    if (aimRadius < 0)
        return aim;
    body = Melee_BodyDistance(target, player) + aimRadius;
    if (body < 0)
        body = 0;
    return body < aim ? body : aim;
}

/* Opt-in per-update trace of the amphitheater mutant's attack (PE_MELEE_TRACE=1): its mode,
 * the attack effect's state (1 = claws live), position, facing, and distance to Aya. */
static void Melee_TraceEnemy(void)
{
    static int enabled = -1;
    BattleEntity *enemy, *aya = D_8009D254;
    const EnemyCombatant *core;
    int dx, dz;

    if (enabled < 0)
        enabled = getenv("PE_MELEE_TRACE") != NULL;
    if (!enabled || !aya || !g_ActiveActor || (unsigned)g_SceneDispatchToken != amphitheaterScene
        || (enemy = Melee_FindMutant()) == NULL)
        return;
    {   /* Once a while: the body's shape (root, facing, aim point, hit cylinder and spheres). */
        static int last = -1000;
        if (g_BattleFrameCounter - last >= 60 || g_BattleFrameCounter < last) {
            const RenderObjectEntity *ro = &enemy->renderObject;
            int i, n = ro->header && ro->animation_entries ? ro->header->animation_entry_count : 0;
            last = g_BattleFrameCounter;
            fprintf(stderr, "enemy_body frame=%d root=%d,%d facing=%d aim=%d,%d scale=%d cyl=%d,%d,%d",
                    g_BattleFrameCounter, enemy->posX.parts.integer, enemy->posZ.parts.integer,
                    enemy->facingAngle & 0xfff, ro->target_x, ro->target_z, enemy->moveSpeed,
                    ro->hit_cylinder.value0, ro->hit_cylinder.value2, ro->hit_cylinder.radius);
            for (i = 0; i < n; i++)
                fprintf(stderr, " part%d=%d,%d,%d", ro->animation_entries[i].animation_id,
                        ro->animation_entries[i].value0, ro->animation_entries[i].value2,
                        ro->animation_entries[i].radius);
            fprintf(stderr, "\n");
        }
    }
    core = enemy->core;
    if (enemy->actionMode == 2 && !(core->effect && core->effect->state == 1))
        return;  /* idle */
    dx = enemy->posX.parts.integer - aya->posX.parts.integer;
    dz = enemy->posZ.parts.integer - aya->posZ.parts.integer;
    fprintf(stderr, "enemy_trace frame=%d mode=%d anim=%d/%d effect=%d pos=%d,%d facing=%d aya=%d,%d dist=%d"
            " aya_hit=%d aya_mode=%d contact=%d aim=%d,%d aim_dist=%d body_dist=%d\n",
            g_BattleFrameCounter, enemy->actionMode, (int)(enemy->animFrame >> 16), enemy->animLastFrame,
            core->effect ? core->effect->state : -1, enemy->posX.parts.integer, enemy->posZ.parts.integer,
            enemy->facingAngle & 0xfff, aya->posX.parts.integer, aya->posZ.parts.integer,
            Math_IntSqrt(dx * dx + dz * dz), (g_ActiveActor->stateFlags & 0x4000) != 0, aya->actionMode,
            (enemy->entityFlags & 0x02000000) != 0, enemy->renderObject.target_x, enemy->renderObject.target_z,
            Battle_CalcDistToPlayer(enemy, aya), Melee_BodyDistance(enemy, aya));
}

static int Melee_StartBackhand(void)
{
    BattleEntity *aya = D_8009D254;
    int predicted = Battle_MeleeClubDistance(melee.target, aya);

    melee.phase = MELEE_SPENT;
    if (Battle_MeleeFeatureEnabled(BATTLE_MELEE_REACH_CANCEL)
        && !Melee_PredictInReach(melee.target, &predicted)) {
        fprintf(stderr, "club_flank cancel frame=%d distance=%d predicted=%d range=%d\n",
                g_BattleFrameCounter, Battle_MeleeClubDistance(melee.target, aya),
                predicted, g_ActiveActor->action->range);
        meleeCue.start = g_BattleFrameCounter;
        meleeCue.until = g_BattleFrameCounter + MELEE_CUE_UPDATES;
        return 0;
    }

    /* Reverse the existing club strike from its follow-through. Hold the native
     * animation clock while we step it backward, including for the held club. */
    Entity_SetActionMode(aya, melee.strikeMode);
    aya->animFrame = (s32)aya->animLastFrame << 16;
    aya->entityFlags |= 0x100;
    melee.backhand = 1;
    melee.phase = MELEE_REPLAY;
    g_BattleHitTimer = 0;
    Battle_FinalizeAttackResult();
    fprintf(stderr, "club_flank swing frame=%d distance=%d predicted=%d\n",
            g_BattleFrameCounter, Battle_MeleeClubDistance(melee.target, aya), predicted);
    return 1;
}

void Battle_MeleeUpdate(void)
{
    BattleEntity *aya = D_8009D254;
    int originalAttack;

    Melee_TraceEnemy();
    Melee_Trace();
    Melee_SpeedWindup();
    if (melee.phase == MELEE_IDLE)
        return;

    originalAttack = Melee_IsAttack(melee.target);
    if (melee.backhand) {
        if (!originalAttack || aya->actionMode != melee.strikeMode || !g_ActiveActor->curHP) {
            g_BattleHitPending = 0;
            Battle_MeleeReset();
        } else {
            int frame = aya->animFrame - MELEE_BACKHAND_STEP;
            aya->animFrame = frame > 0 ? frame : 0;
        }
        return;
    }
    if (melee.phase == MELEE_SPENT || !Battle_MeleeFeatureEnabled(BATTLE_MELEE_FOLLOW_UP)) {
        Battle_MeleeReset();
        return;
    }
    if ((unsigned)(g_BattleFrameCounter - melee.impactFrame) >= MELEE_WINDOW) {
        fprintf(stderr, "club_flank expire frame=%d\n", g_BattleFrameCounter);
        Battle_MeleeReset();
        return;
    }
    if (g_BattleModeState != 0 || !g_ActiveActor->curHP
        || (g_ActiveActor->stateFlags & 0x194000)
        || (unsigned)g_SceneDispatchToken != amphitheaterScene
        || Melee_FindMutant() != melee.target || (melee.target->entityFlags & 0x4000)
        || (!originalAttack && (g_BattleActionIndex < g_BattleActionQueueCount
                                || aya->actionMode < 4 || aya->actionMode > 5))) {
        Battle_MeleeReset();
        return;
    }

    if (Battle_MeleeFeatureEnabled(BATTLE_MELEE_REACH_CANCEL))
        Melee_TrackDistance();
    else
        meleeTrack.count = 0;
    if (melee.phase == MELEE_OFFERED && (g_FieldPadPressed & MELEE_CROSS)) {
        g_FieldPadPressed &= ~MELEE_CROSS;
        melee.phase = MELEE_ACCEPTED;
        fprintf(stderr, "club_flank accept frame=%d\n", g_BattleFrameCounter);
        if (!originalAttack && Melee_StartBackhand()) {
            /* Recovery already released movement. Requeue only this strike;
             * bypass targeting and leave the current ATB charge intact. */
            g_BattleActionQueue[0] = melee.slot;
            g_BattleActionQueueCount = 1;
            g_BattleActionIndex = 0;
            g_ActiveActor->stateFlags &= ~0x200000;
        }
    }
}

static void Melee_PrintCue(u8 *text, int left)
{
    int x, y, mode, dim, advance;
    unsigned color;

    x = g_TextCursorX;
    y = g_TextCursorY;
    mode = g_TextRenderMode;
    dim = g_DrawTextDimmed;
    color = g_DrawPrimColor;
    advance = g_DrawGlyphAdvance;
    g_TextCursorX = left;
    g_TextCursorY = 200;
    g_TextRenderMode = 0;
    g_DrawTextDimmed = 0;
    g_DrawPrimColor = 0x808080;
    Draw_PrintRawText(text);
    g_DrawPrimColor = color;
    g_DrawGlyphAdvance = advance;
    g_DrawTextDimmed = dim;
    g_TextRenderMode = mode;
    g_TextCursorX = x;
    g_TextCursorY = y;
}

/* A PlayStation Cross button (the HUD font has no button symbols): a light rim and a mid-gray
 * face (8-triangle fans, radius 6 and 5, corners rounded to the nearest pixel) and a light-blue
 * X. Above native resolution the X is two slanted triangle bars, which scale cleanly; at native
 * 320x240 (where triangle edges come out lumpy at this size) it is two 2-pixel line diagonals,
 * which stay crisp there but render hairline-thin when scaled up. Same switch as the native
 * dialogue font (render/dialogue_alpha.c). Checked on screen at 1x and 4x. Packets are
 * double-buffered per draw slot like the HUD's own; a lower ordering depth draws in front. */
enum { MELEE_ICON_TRIS = 8 + 8 + 4, MELEE_ICON_LINES = 4, MELEE_ICON_R = 6 };
static BattleStatusPointerPrim meleeIcon[2][MELEE_ICON_TRIS];
static RenderLinePacket meleeIconLines[2][MELEE_ICON_LINES];

static void Melee_IconLine(RenderLinePacket *l, int depth, unsigned rgb, int x0, int y0, int x1, int y1)
{
    SetLineF2(l);
    l->r = (u8)(rgb >> 16); l->g = (u8)(rgb >> 8); l->b = (u8)rgb;
    l->x0 = (s16)x0; l->y0 = (s16)y0; l->x1 = (s16)x1; l->y1 = (s16)y1;
    AddPrim(D_800B0E38.ordering[g_ActiveDrawSlot] + depth, l);
}

static void Melee_IconTri(BattleStatusPointerPrim *t, int depth, unsigned rgb,
                          int x0, int y0, int x1, int y1, int x2, int y2)
{
    SetPolyF3(t);
    t->r = (u8)(rgb >> 16); t->g = (u8)(rgb >> 8); t->b = (u8)rgb;
    t->x0 = (s16)x0; t->y0 = (s16)y0; t->x1 = (s16)x1; t->y1 = (s16)y1; t->x2 = (s16)x2; t->y2 = (s16)y2;
    AddPrim(D_800B0E38.ordering[g_ActiveDrawSlot] + depth, t);
}

static void Melee_DrawCrossIcon(int cx, int cy)
{
    /* Unit octagon, x and y in 1/16 units. */
    static const s8 ring[8][2] = { {16, 0}, {11, 11}, {0, 16}, {-11, 11}, {-16, 0}, {-11, -11}, {0, -16}, {11, -11} };
    BattleStatusPointerPrim *t = meleeIcon[g_ActiveDrawSlot & 1];
    const int a = 3;
    int i, k, x[2][8], y[2][8];

    for (k = 0; k < 2; k++) {   /* k 0: rim (radius 6), k 1: face (radius 5); round to nearest */
        const int r = MELEE_ICON_R - k;
        for (i = 0; i < 8; i++) {
            x[k][i] = cx + (ring[i][0] * r + (ring[i][0] < 0 ? -8 : 8)) / 16;
            y[k][i] = cy + (ring[i][1] * r + (ring[i][1] < 0 ? -8 : 8)) / 16;
        }
    }
    for (i = 0; i < 8; i++) {
        const int j = (i + 1) & 7;
        Melee_IconTri(t++, 6, 0xC8C8C8, cx, cy, x[0][i], y[0][i], x[0][j], y[0][j]);
        Melee_IconTri(t++, 5, 0x484850, cx, cy, x[1][i], y[1][i], x[1][j], y[1][j]);
    }
    if (Psyz_VideoGetInternalResolution() < 2) {
        RenderLinePacket *l = meleeIconLines[g_ActiveDrawSlot & 1];
        Melee_IconLine(l++, 4, 0x78B8FF, cx - a, cy - a, cx + a, cy + a);
        Melee_IconLine(l++, 4, 0x78B8FF, cx - a + 1, cy - a, cx + a + 1, cy + a);
        Melee_IconLine(l++, 4, 0x78B8FF, cx + a, cy - a, cx - a, cy + a);
        Melee_IconLine(l++, 4, 0x78B8FF, cx + a + 1, cy - a, cx - a + 1, cy + a);
        return;
    }
    /* Bar from top-left to bottom-right, then top-right to bottom-left: parallelograms with
     * horizontal edges 2 pixels long at the top and bottom. */
    Melee_IconTri(t++, 4, 0x78B8FF, cx - a - 1, cy - a, cx - a + 1, cy - a, cx + a + 1, cy + a);
    Melee_IconTri(t++, 4, 0x78B8FF, cx - a - 1, cy - a, cx + a + 1, cy + a, cx + a - 1, cy + a);
    Melee_IconTri(t++, 4, 0x78B8FF, cx + a - 1, cy - a, cx + a + 1, cy - a, cx - a + 1, cy + a);
    Melee_IconTri(t++, 4, 0x78B8FF, cx + a - 1, cy - a, cx - a + 1, cy + a, cx - a - 1, cy + a);
}

static void Melee_DrawBackhandSpark(void)
{
    static BattleStatusPointerPrim spark[2][4];
    static const s8 directions[4][2] = {{2, -1}, {-2, 1}, {1, 2}, {-1, -2}};
    int age = g_BattleFrameCounter - meleeSpark.frame;
    int i, x = meleeSpark.x, y = meleeSpark.y;

    if (age < 0 || age >= MELEE_SPARK_UPDATES) {
        meleeSpark.frame = -MELEE_SPARK_UPDATES;
        return;
    }
    for (i = 0; i < 4; i++) {
        int dx = directions[i][0], dy = directions[i][1];
        int inner = 1 + age;
        int outer = inner + (i < 2 ? 10 : 5) - age / 2;
        unsigned color = age < 2 ? 0xE0FBFF : 0x65C9FF;
        Melee_IconTri(&spark[g_ActiveDrawSlot & 1][i], 4, color,
                      x + dx * inner - dy, y + dy * inner + dx,
                      x + dx * inner + dy, y + dy * inner - dx,
                      x + dx * outer, y + dy * outer);
    }
}

void Battle_MeleeDraw(void)
{
    /* Game glyphs: A is 0x10, letters follow in order, 0x0f is a space, 0xff ends. */
    static u8 prompt[] = {0x15, 0x1e, 0x1b, 0x1b, 0x1e, 0x26, 0x0f, 0x24,   /* FOLLOW U */
                          0x1f, 0xff};                                      /* P, then the Cross icon */
    static u8 outOfReach[] = {0x1e, 0x24, 0x23, 0x0f, 0x1e, 0x15, 0x0f,    /* OUT OF  */
                              0x21, 0x14, 0x10, 0x12, 0x17, 0xff};          /* REACH   */

    if (Battle_MeleeFeatureEnabled(BATTLE_MELEE_SPARK))
        Melee_DrawBackhandSpark();
    else
        meleeSpark.frame = -MELEE_SPARK_UPDATES;
    if (!Battle_MeleeFeatureEnabled(BATTLE_MELEE_FOLLOW_UP))
        return;
    if (melee.phase == MELEE_OFFERED) {
        /* The HUD font advances 8 pixels a glyph (measured on screen; Draw_PrintRawText leaves
         * the cursor where it started). "FOLLOW UP" is 9 glyphs, 72 pixels, then a 6-pixel gap
         * and the icon: about 92 pixels, centred on the 320-pixel screen. */
        const int left = (320 - (9 * 8 + 6 + 2 * MELEE_ICON_R)) / 2;
        Melee_PrintCue(prompt, left);
        Melee_DrawCrossIcon(left + 9 * 8 + 6 + MELEE_ICON_R, 203);
        return;
    }
    /* A frame counter that went backwards means a new battle: drop a stale cue. */
    if (g_BattleFrameCounter < meleeCue.start || g_BattleFrameCounter >= meleeCue.until) {
        meleeCue.until = meleeCue.start = 0;
        return;
    }
    Melee_PrintCue(outOfReach, 116);  /* 12 glyphs, centred */
}

int Battle_MeleeStepStrike(BattleEntity *aya, BattleEntity *target)
{
    if (melee.backhand) {
        if (aya->animFrame != 0 || g_BattleHitPending)
            return 1;
    } else if (aya->animLastFrame != aya->animPrev.parts.integer) {
        return 1;
    }
    if (melee.target == target) {
        /* A hurt reaction may have changed modes at the first contact.
         * Save the actual strike pose that Aya recovers from. */
        melee.strikeMode = aya->actionMode;
        if (melee.phase == MELEE_ACCEPTED && Melee_StartBackhand())
            return 1;
        fprintf(stderr, "club_flank end frame=%d\n", g_BattleFrameCounter + 1);
    }
    if (melee.backhand)
        Battle_MeleeReset();
    return 0;
}

void Battle_MeleeOnResolve(BattleEntity *target, int status)
{
    int replay = melee.phase == MELEE_REPLAY && melee.target == target;

    if (!replay && !Battle_MeleeFeatureEnabled(BATTLE_MELEE_FOLLOW_UP)
        && !Battle_MeleeFeatureEnabled(BATTLE_MELEE_BODY_REACH))
        return;
    fprintf(stderr, "club_flank resolve frame=%d replay=%d status=%d distance=%d range=%d aim=%d body=%d angle=%d\n",
                g_BattleFrameCounter,
                replay,
                status,
                Battle_MeleeClubDistance(target, D_8009D254),
                g_ActiveActor->action->range, Battle_CalcDistToPlayer(target, D_8009D254),
                Melee_BodyDistance(target, D_8009D254),
                Battle_CalcRelativeAngle(D_8009D254, target) & 0xfff);
    if (status == 1 && replay && Battle_MeleeFeatureEnabled(BATTLE_MELEE_SPARK)) {
        meleeSpark.frame = g_BattleFrameCounter;
        meleeSpark.x = target->renderObject.projected_target_x;
        meleeSpark.y = target->renderObject.projected_target_y;
        fprintf(stderr, "club_flank spark frame=%d\n", g_BattleFrameCounter);
    }
}
