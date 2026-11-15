#include "common.h"
#include "pe1/battle.h"
#include "pe1/battle_context.h"
#include "pe1/battle_entity_anim.h"
#include "pe1/battle_runtime.h"
#include "pe1/field_actor.h"
#include "pe1/gte_types.h"
#include "pe1/player_entity.h"
#include "pe1/psyq_libc.h"
#include "pe1/room_floor.h"
#include "pe1/script_anim.h"
#include "pe_hooks.h"

/* Script setup and execution share six inline animation-event records. */

void Script_SetObjAnimEntry12(ScriptAnimObj *obj, int index, int effectType,
                           int enterMode, int exitMode, int power,
                           int parameter0, int parameter1, int parameter2,
                           int parameter3, int category, int frame)
{
    index = (u8)index;
    if (!obj || !obj->core || index >= 6) return;
    EnemyActionEffect *dst = &obj->core->records[index];

    dst->state = 0;
    dst->effectType = effectType;
    dst->enterMode = enterMode;
    dst->exitMode = exitMode;
    dst->power = power;
    dst->category = category;
    dst->frame = frame;

    obj->core->parameters[index][0] = parameter0;
    obj->core->parameters[index][1] = parameter1;
    obj->core->parameters[index][2] = parameter2;
    obj->core->parameters[index][3] = parameter3;
}

void Script_SetObjAnimEntry5(ScriptAnimObj *obj, int index, int effectType, int enterMode, u8 exitMode, u16 power) {
    index = (u8)index;
    if (!obj || !obj->core || index >= 6) return;
    EnemyActionEffect *dst = &obj->core->records[index];

    dst->state = 0;
    dst->effectType = effectType;
    dst->enterMode = enterMode;
    dst->exitMode = exitMode;
    dst->power = power;
}

void Battle_SetEntryCoords(ScriptAnimObj *arg0, unsigned char arg1, int arg2, int arg3) {
    if (!arg0 || !arg0->core || arg1 >= 6) return;
    EnemyActionEffect *entry = &arg0->core->records[arg1];

    entry->enterStep = arg2;
    entry->exitStep = arg3;
}

int Entity_TriggerAnimEvent(BattleEntity *entity, u8 slot)
{
    int result = 0;
    if (!entity) return 0;
    EntityAnimEventCore *initial = entity->core;
    EntityAnimEventCore *core;
    BattleEntity *actor;
    u32 nextFrame;
    if (!initial || !(g_GameStateFlags & 2))
        return 1;
    core = initial;
    if (core->kind == 2 || (actor = entity, core->kind == 4)) {
        BattleEntity *parent = entity->parent;
        actor = parent;
        if (!parent)
            actor = entity;
    } else {
        actor = entity;
    }
    if (slot >= 6)
        return result;
    {
        u32 previous = actor->animPrev.fixed;
        u32 frame = actor->animFrame;

        if (previous > frame)
            nextFrame = (frame >> 16) + actor->animLastFrame + 1;
        else
            nextFrame = frame >> 16;
    }
    switch (actor->actionMode) {
    case 0:
    case 1:
        break;
    case 6: case 8: case 10: case 12: case 14:
        if (actor->animLastFrame >= actor->animPrev.parts.integer &&
            actor->animLastFrame < nextFrame) {
            Entity_SetActionMode(actor, core->active ? core->active->exitMode : 0);
            Asset_Find08w(core->assetId, 0,
                         actor->renderObject.target_x,
                         actor->renderObject.target_y,
                         actor->renderObject.target_z);
            actor->animStep = core->active ? core->active->exitStep : 0;
            if (core->active)
                core->active->state = 1;
        }
        break;
    case 7: case 9: case 11: case 13: case 15:
        if (actor->animLastFrame >= actor->animPrev.parts.integer &&
            actor->animLastFrame < nextFrame && !(core->flags.raw & 0x40000000)) {
            Entity_SetActionMode(actor, (u16)(s8)core->baseMode);
            actor->animStep = 0x10000;
            if (slot < 3) {
                result = 1;
                if (core->active)
                    core->active->state = 4;
            }
        }
        if (core->kind == 0 && (core->flags.raw & 0x6000)) {
            if (core->active && core->active->state < 2)
                core->active->state = 4;
            result = 2;
        }
        if (slot < 3 && core->active &&
            (core->active->category == 1 || core->active->category == 3)) {
            u32 frame = actor->animFrame;

            if (core->active->frame < (frame >> 16) &&
                core->active->frame >= actor->animPrev.parts.integer)
                core->active->state = 3;
        }
        break;
    default: {
        unsigned index = slot;
        if (core->records[index].state == 0) {
            core->active = &core->records[index];
            core->flags.bits.selectedRecord = index;
            core->flags.bits.bit30 = 0;
            core->flags.bits.bit31 = 0;
            Entity_SetActionMode(actor, core->active->enterMode);
            actor->animStep = core->active->enterStep;
        }
        break;
    }
    }
    /* A completed inline record can outlive the active event (notably on
     * a child using its parent's animation). PS1 reads/writes address zero
     * here; native code keeps the completion without touching that address. */
    if ((core->active && core->active->state == 4) || core->records[slot].state == 4) {
        Entity_SetActionMode(actor, (u16)(s8)core->baseMode);
        actor->animStep = 0x10000;
        result = 1;
        if (core->active)
            core->active->state = 0;
        core->active = 0;
    }
    return result;
}

int Battle_GetContextField(int arg0) {
    Combatant *ctx;
    int ret;
    u32 word;

    arg0 = (u8)arg0;
    ctx = ((BattleEntity *)g_PlayerEntity)->core;
    ret = -1000;

    switch (arg0) {
    case 0:
        ret = ctx->coreFlags;
        break;
    case 1:
        ret = ctx->field04.fieldId04;
        break;
    case 2:
        ret = (u16)ctx->fieldId06;
        break;
    case 3:
        ret = ctx->exp_or_acc;
        break;
    case 4:
        ret = (s16)ctx->curHP;
        break;
    case 6:
        ret = ctx->hpAlive;
        break;
    case 7:
        ret = ctx->actionMode12;
        break;
    case 10:
        ret = (s16)ctx->maxHP;
        break;
    case 11:
        ret = (u16)ctx->stat1E;
        break;
    case 12:
        ret = (u16)ctx->stat20;
        break;
    case 13:
        ret = ctx->maxAtk;
        break;
    case 14:
        ret = ctx->stat22;
        break;
    case 20:
        word = ctx->stateFlags;
        ret = word >> 9;
        ret &= 1;
        break;
    case 21:
        word = ctx->stateFlags;
        ret = word >> 6;
        ret &= 3;
        break;
    case 22:
        word = ctx->stateFlags;
        ret = word >> 29;
        ret &= 1;
        break;
    }

    return ret;
}

void Battle_SetContextField(int arg0, int arg1) {
    Combatant *ctx;

    arg0 = (u8)arg0;
    /* Room scripts set the floor before the new player has been spawned. */
    if (arg0 == 255) {
        g_RoomFloorY->y = arg1;
        return;
    }
    ctx = ((BattleEntity *)g_PlayerEntity)->core;

    switch (arg0) {
    case 0:
        ctx->coreFlags = arg1;
        break;
    case 1:
        ctx->field04.fieldId04 = arg1;
        break;
    case 2:
        ctx->fieldId06 = arg1;
        break;
    case 3:
        ctx->exp_or_acc = arg1;
        break;
    case 4:
        ctx->curHP = arg1;
        break;
    case 5:
        ctx->hpMirror = arg1;
        break;
    case 6:
        ctx->hpAlive = arg1;
        break;
    case 10:
        ctx->maxHP = arg1;
        break;
    case 11:
        ctx->stat1E = arg1;
        break;
    case 12:
        ctx->stat20 = arg1;
        break;
    case 14:
        ctx->stat22 = arg1;
        break;
    case 18:
        ctx->stat26 = arg1;
        break;
    case 30:
        ctx->panelA_val = arg1;
        break;
    case 31:
        ctx->panelA_timer = arg1;
        break;
    case 32:
        ctx->panelA_scale = arg1;
        break;
    case 33:
        ctx->panelB_val = arg1;
        break;
    case 34:
        ctx->panelB_timer = arg1;
        break;
    }
}

s32 Battle_GetEnemyContextField(FieldActor *arg0, s32 arg1) {
    static EnemyCombatant zeroEnemy;
    EnemyCombatant *ctx = (arg0 && arg0->state) ? (EnemyCombatant *)arg0->state : &zeroEnemy;
    s32 ret = -1000;
    u32 word;
    s32 flags;

    switch (((u8)arg1) - 0x29) {
    case 0:
        ret = (s8)ctx->field04.bytes.rank;
        break;
    case 2:
        ret = ctx->curHP;
        break;
    case 3:
        ret = ctx->hpAlive;
        if (ret < 0) {
            ret = 0;
        }
        break;
    case 7:
        {
            EntityAnimEventCore *events = (EntityAnimEventCore *)ctx;
            ret = events->records[events->flags.bits.selectedRecord].state;
        }
        break;
    case 19:
        ret = ctx->hpAliveCap;
        if (ret < 0) {
            ret = 0;
        }
        break;
    case 20:
        ret = ctx->defense;
        break;
    case 36:
        word = ctx->coreFlags;
        ret = word >> 13;
        ret &= 3;
        break;
    case 41:
        ret = ctx->coreFlags >> 24;
        ret &= 0x3F;
        break;
    case 89:
        flags = ctx->statusFlags2;
        if (flags & 0x1000000) {
            if ((ctx->coreFlags & 0xC0000) == 0xC0000) {
                ctx->statusFlags2 = flags & 0xFEFFFFFF;
                ret = 1;
            } else {
                ret = 0;
            }
        } else {
            ret = 0;
        }
        break;
    }
    return ret;
}

void Entity_WriteFieldByCmd(FieldActor *arg0, s32 arg1, s32 arg2) {
    s32 temp_a1;
    FieldActorState *temp_a0;

    if (!arg0 || !arg0->state)
        return;
    temp_a0 = arg0->state;
    temp_a1 = arg1 & 0xFF;
#ifdef PE_MODS
    {
        PeHookEnemyField hook = { arg0, temp_a1, arg2 };
        PE_HOOK(PE_HOOK_ENEMY_FIELD, &hook);
        arg2 = hook.value;
    }
#endif
    switch (temp_a1) {                              /* switch 1 */
    case 0x28:                                      /* switch 1 */
        temp_a0->amount_mirror = arg2;
        return;
    case 0x29:                                      /* switch 1 */
        temp_a0->id04.bytes.low = arg2;
        return;
    case 0x2A:                                      /* switch 1 */
        temp_a0->id04.bytes.high = arg2;
        return;
    case 0x2C:                                      /* switch 1 */
        temp_a0->control10.command_value = arg2;
        return;
    case 0x2D:                                      /* switch 1 */
        temp_a0->action_value14 = arg2;
        return;
    case 0x2B:                                      /* switch 1 */
        temp_a0->amount = arg2;
        return;
    case 0x2E:                                      /* switch 1 */
        temp_a0->id06.bytes.high = arg2;
        return;
    case 0x2F:                                      /* switch 1 */
        temp_a0->id06.bytes.low = arg2;
        return;
    case 0x3C:                                      /* switch 1 */
        temp_a0->script_value88 = arg2;
        return;
    case 0x3D:                                      /* switch 1 */
        temp_a0->script_value8c = arg2;
        return;
    case 0x3E:                                      /* switch 1 */
        temp_a0->script_value8e = arg2;
        return;
    case 0x3F:                                      /* switch 1 */
        temp_a0->effect_chance = arg2;
        return;
    case 0x40:                                      /* switch 1 */
        temp_a0->effect_param91 = arg2;
        return;
    case 0x41:                                      /* switch 1 */
        temp_a0->effect_param92 = arg2;
        return;
    case 0x42:                                      /* switch 1 */
        temp_a0->effect_param93 = arg2;
        return;
    case 0x43:                                      /* switch 1 */
        temp_a0->effect_level = arg2;
        return;
    case 0x44:                                      /* switch 1 */
        temp_a0->effect_duration = arg2;
        return;
    case 0x45:                                      /* switch 1 */
        temp_a0->script_value96 = arg2;
        return;
    case 0x4A:                                      /* switch 1 */
        temp_a0->core_flags = (temp_a0->core_flags & ~0x10) | ((arg2 & 1) * 0x10);
        return;
    case 0x50:                                      /* switch 1 */
        temp_a0->behavior_mode = arg2;
        switch (arg2) {                             /* switch 2; irregular */
        case 1:                                     /* switch 2 */
            temp_a0->behavior_timer = 0x190;
            return;
        case 2:                                     /* switch 2 */
            temp_a0->behavior_timer = 0x46;
            return;
        case 3:                                     /* switch 2 */
            temp_a0->behavior_timer = 0x14;
            return;
        }
        break;
    case 0x51:                                      /* switch 1 */
        temp_a0->core_flags = (temp_a0->core_flags & 0xFFEFFFFF) | ((arg2 & 1) << 0x14);
        return;
    case 0x5A:                                      /* switch 1 */
        temp_a0->script_value98 = arg2;
        return;
    case 0x5B:                                      /* switch 1 */
        temp_a0->script_value9a = arg2;
        return;
    case 0x5C:                                      /* switch 1 */
        temp_a0->script_value9e = arg2;
        return;
    case 0x5D:                                      /* switch 1 */
        temp_a0->script_value9f = arg2;
        return;
    case 0x5E:                                      /* switch 1 */
        temp_a0->script_valueae = arg2;
        return;
    case 0x5F:                                      /* switch 1 */
        temp_a0->script_valueaf = arg2;
        return;
    case 0x60:                                      /* switch 1 */
        temp_a0->script_valuebc = arg2;
        return;
    case 0x32:                                      /* switch 1 */
        temp_a0->script_values_b0[0] = arg2;
        return;
    case 0x33:                                      /* switch 1 */
        temp_a0->script_values_b0[1] = arg2;
        return;
    case 0x34:                                      /* switch 1 */
        temp_a0->script_values_b0[2] = arg2;
        return;
    case 0x35:                                      /* switch 1 */
        temp_a0->script_values_b0[3] = arg2;
        return;
    case 0x36:                                      /* switch 1 */
        temp_a0->script_values_b0[4] = arg2;
        return;
    case 0x37:                                      /* switch 1 */
        temp_a0->script_values_b0[5] = arg2;
        return;
    case 0x64:
        temp_a0->status_flags2.bits.field64 = arg2;
        return;
    case 0x65:
        temp_a0->status_flags2.bits.field65 = arg2;
        return;
    case 0x66:
        temp_a0->status_flags2.bits.field66 = arg2;
        return;
    case 0x67:
        temp_a0->status_flags2.bits.field67 = arg2;
        return;
    case 0x68:
        temp_a0->status_flags2.bits.field68 = arg2;
        return;
    case 0x69:
        temp_a0->status_flags2.bits.field69 = arg2;
        return;
    case 0x6A:
        temp_a0->status_flags2.bits.field6a = arg2;
        return;
    case 0x6B:
        temp_a0->status_flags2.bits.field6b = arg2;
        return;
    case 0x6C:
        temp_a0->status_flags2.bits.field6c = arg2;
        return;
    case 0x6D:
        temp_a0->status_flags2.bits.field6d = arg2;
        return;
    case 0x6E:
        temp_a0->status_flags2.bits.field6e = arg2;
        return;
    case 0x78:                                      /* switch 1 */
        temp_a0->panel_c_value = arg2;
        return;
    case 0x79:                                      /* switch 1 */
        temp_a0->panel_c_timer = arg2;
        return;
    case 0x7A:                                      /* switch 1 */
        temp_a0->panel_c_mode = arg2;
        return;
    case 0x7B:                                      /* switch 1 */
        temp_a0->loot_item_id = arg2;
        return;
    case 0x7C:                                      /* switch 1 */
        temp_a0->loot_item_aux = arg2;
        /* fallthrough */
    default:                                        /* switch 1 */
        return;
    }
}

int Battle_CalcDistToPlayer(BattleEntity *actor, BattleEntity *player)
{
    int x;
    int y;

    x = actor->renderObject.target_x - player->posX.parts.integer;
    y = actor->renderObject.target_z - player->posZ.parts.integer;
    return Math_IntSqrt((u32)x * (u32)x + (u32)y * (u32)y);
}

s16 Battle_CalcAngleToTarget(RenderObjectEntity *object, void *target)
{
    int *position = target;
    int x;
    int y;
    int angle;

    x = (s32)((u32)object->target_x << 16);
    y = (s32)((u32)object->target_z << 16);
    angle = ratan2((s32)((u32)x - (u32)position[0]), (s32)((u32)y - (u32)position[2]));
    return angle + 0x800;
}

int Battle_CalcRelativeAngle(BattleEntity *target, BattleEntity *viewer)
{
    s32 x = (s32)((u32)target->posX.fixed - (u32)viewer->posX.fixed);
    s32 z = (s32)((u32)target->posZ.fixed - (u32)viewer->posZ.fixed);
    int angle = (s16)(0x800 - ratan2(x, z));
    angle += viewer->facingAngle;
    return (s16)(angle % 0x1000);
}

void Battle_CheckDropChance(void)
{
    Combatant *ctx;
    int flags;
    u16 chance;

    ctx = g_ActiveActor;
    flags = ctx->action->turnWord;
    if (flags & 0x10000) {
        chance = ctx->stat22;
        if ((rand() % 100) < chance) {
            g_ActiveActor->hpAlive = 0x2328;
        }
    }
}

void Battle_RollEnemySpawn(void *arg0)
{
    EnemyCombatant *ctx;
    int diff;
    int threshold;
    int bonus;

    if (!arg0) {
        return;
    }
    ctx = ((BattleEntity *)arg0)->core;
    if (!ctx || ctx->hpAlive <= 0) {
        return;
    }

    diff = (s8)(g_ActiveActor->field04.bytes.rank -
                ctx->field04.bytes.rank);
    if (diff > 0) {
        threshold = Battle_GetAgilityBonus();
        if ((rand() % 100) * 10 < threshold) {
            Battle_SetupEntityTarget((BattleEntity *)arg0);
        }
    } else if (diff == 0) {
        bonus = Battle_GetAgilityBonus();
        threshold = bonus * 2;
        threshold += bonus;
        if ((rand() % 100) * 50 < threshold) {
            Battle_SetupEntityTarget((BattleEntity *)arg0);
        }
    } else if (diff == -1) {
        bonus = Battle_GetAgilityBonus();
        threshold = bonus * 2;
        threshold += bonus;
        if ((rand() % 100) * 100 < threshold) {
            Battle_SetupEntityTarget((BattleEntity *)arg0);
        }
    } else if (diff < -1) {
        threshold = Battle_GetAgilityBonus();
        if ((rand() % 100) * 50 < threshold) {
            Battle_SetupEntityTarget((BattleEntity *)arg0);
        }
    }
}
