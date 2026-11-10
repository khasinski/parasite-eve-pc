#include "common.h"
#include <string.h>
#include "pe1/battle.h"
#include "pe1/battle_cmd.h"
#include "pe1/battle_entity_anim.h"
#include "pe1/battle_reset_stats.h"
#include "pe1/battle_runtime.h"
#include "pe1/battle_setup.h"
#include "pe1/battle_status.h"
#include "pe1/game_state.h"
#include "pe1/gte_types.h"
#include "pe1/inventory.h"
#include "pe1/menu_flags.h"
#include "pe1/player_entity.h"
#include "pe1/pm.h"
#include "pe1/random.h"
#include "pe1/textbox.h"

s8 g_BattleHaltTimer;
BattleEnemySlot g_BattleEnemySlots[7];
/* Original 45-entry action queue, widened only for native actor pointers. */
BattleInitSlot g_BattleActionQueue[45];
BattleTarget g_BattleTargetList[BATTLE_TARGET_CAPACITY + 1];
u16 g_BattleHitActionPending;
extern u8 g_BattleActionAnimModes[4];
s8 g_BattleTargetCycleMode;
u8 g_BattleSlotCancelPending;
extern u8 g_BattleRemainingActions;
extern int g_BattleAttackPmSlot;
s32 g_BattleAilmentPmSlot;
extern s32 g_BattleEffectPmSlot;
extern u8 g_BattleHitPending;
extern unsigned char g_BattleActionQueueCount;
s8 g_BattleTargetIndex;

void Battle_BeginPlayerAction(void) {
    BattleEntity *player;
    Combatant *actor;
    int mask;
    int flags;
    u32 *entityFlags;
    s32 *stateFlags;

    mask = -0x101;
    D_8009D2E8 |= 1;
    player = D_8009D254;
    entityFlags = &player->entityFlags;
    flags = *entityFlags;
    actor = g_ActiveActor;
    flags &= mask;
    *entityFlags = flags;
    stateFlags = &actor->stateFlags;
    *stateFlags |= 0x10000;
    g_BattleHitActionPending = 0;
    Entity_SetActionMode(player, 0x12);
    Battle_FlushScriptSounds();
}

void Battle_ReturnToIdle(void) {
    Combatant *state;
    BattleEntity *actor;
    int mask_state;
    int flags;
    s32 *stateFlags;

    mask_state = 0xFFFEFFFF;
    D_8009D2E8 &= -2;
    state = g_ActiveActor;
    actor = (BattleEntity *)g_PlayerEntity;
    stateFlags = &state->stateFlags;
    flags = *stateFlags;
    flags &= mask_state;
    *stateFlags = flags;
    actor->motionX = 0;
    actor->motionY = 0;
    actor->motionZ = 0;
    Entity_SetActionMode(actor, state->actionMode12);
}

void Battle_HaltOnPositiveX(void) {
    Combatant *state;
    BattleEntity *actor;
    Combatant *state2;
    BattleEntity *actor2;
    int flags;

    state = g_ActiveActor;
    if ((s16)state->curHP > 0) {
        actor = (BattleEntity *)g_PlayerEntity;
        actor->motionX = 0;
        actor->motionY = 0;
        actor->motionZ = 0;
        Entity_SetActionMode(actor, 0x12);

        state2 = g_ActiveActor;
        actor2 = (BattleEntity *)g_PlayerEntity;
        flags = state2->stateFlags;
        g_BattleHaltTimer = 0;
        flags |= 0x2000;
        state2->stateFlags = flags;
        actor2->entityFlags &= ~0x100;
        Battle_FlushScriptSounds();
    }
}

void Battle_InitEntityColors(void) {
    s32 flags;
    s32 *stateFlags;

    Entity_SetActionMode(D_8009D254, g_ActiveActor->actionMode12);
    stateFlags = &g_ActiveActor->stateFlags;
    flags = *stateFlags;
    g_BattleHaltTimer = 0;
    *stateFlags = flags & ~0x2000;
    g_BattleHitActionPending = 0;
    g_BattlePeGauges[0][1].r0 = 0xFF;
    g_BattlePeGauges[0][1].g0 = 0x3D;
    g_BattlePeGauges[0][1].b0 = 0x81;
    g_BattlePeGauges[0][1].r1 = 0x83;
    g_BattlePeGauges[0][1].g1 = 0x13;
    g_BattlePeGauges[0][1].b1 = 1;
    g_BattlePeGauges[0][1].r2 = 0xFF;
    g_BattlePeGauges[0][1].g2 = 0x3D;
    g_BattlePeGauges[0][1].b2 = 0x81;
    g_BattlePeGauges[0][1].r3 = 0x83;
    g_BattlePeGauges[0][1].g3 = 0x13;
    g_BattlePeGauges[0][1].b3 = 1;
    g_BattlePeGauges[1][1].r0 = 0xFF;
    g_BattlePeGauges[1][1].g0 = 0x3D;
    g_BattlePeGauges[1][1].b0 = 0x81;
    g_BattlePeGauges[1][1].r1 = 0x83;
    g_BattlePeGauges[1][1].g1 = 0x13;
    g_BattlePeGauges[1][1].b1 = 1;
    g_BattlePeGauges[1][1].r2 = 0xFF;
    g_BattlePeGauges[1][1].g2 = 0x3D;
    g_BattlePeGauges[1][1].b2 = 0x81;
    g_BattlePeGauges[1][1].r3 = 0x83;
    g_BattlePeGauges[1][1].g3 = 0x13;
    g_BattlePeGauges[1][1].b3 = 1;
}

extern unsigned char g_BattleActionQueueCount;
extern unsigned char g_BattleActionIndex;
extern unsigned char g_BattleRemainingActions;
signed char g_BattleActiveTurnSlot;
signed char g_BattleItemMenuState;

void Save_ResetGlobalFlags(void) {
    g_BattleActionQueueCount = 0;
    g_BattleActionIndex = 0;
    g_BattleRemainingActions = 0;
    g_BattleActiveTurnSlot = 0;
    g_BattleItemMenuState = 0;
}

void Battle_Init(void)
{
    u8 index;
    BattleInitSlot *records;
    BattleAction **action;
    BattleAction *config0;
    BattleAction *config1;
    BattleAction *config2;
    BattleAction *config3;
    BattleAction *config4;
    BattleAction *config5;

    Pm_StopAll();
    index = 0;
    records = g_BattleActionQueue;
    g_BattleAttackPmSlot = -1;
    g_BattleImpactPmSlot = -1;
    g_BattleEffectPmSlot = -1;
    g_BattleAilmentPmSlot = -1;

    do {
        g_BattleActionQueue[index].actor = 0;
        records[index].actionGeneration = 0;
        g_BattleActionQueue[index].command = 0;
        index++;
    } while (index < 45);

    action = &g_ActiveActor->action;
    config0 = *action;
    g_BattleTargetIndex = 0;
    g_BattleTargetCycleMode = 0;
    g_BattleHitPending = 0;
    config1 = *action;
    g_BattleActiveTurnSlot = (config0->turnWord >> 4) & 3;
    config2 = *action;
    g_BattleRemainingActions = config1->turnWord & 0xF;
    g_BattleActionAnimModes[0] = config2->animMode[0];
    config3 = *action;
    g_BattleActionAnimModes[1] = config3->animMode[1];
    config4 = *action;
    g_BattleActionAnimModes[2] = config4->animMode[2];
    config5 = *action;
    g_BattleActionAnimModes[3] = config5->animMode[3];

    Draw_ResetTextboxes();
    g_BattleSaveOverlayActive = 0;
    g_BattleResultFlags &= ~0x300;
    Battle_InitFadeVars();
    g_BattleSlotCancelPending = 0;
}

extern unsigned char g_BattleActionQueueCount;

signed char Pad_GetMenuPressedBitOrDisabled(void) {
    if (g_ActiveActor->stateFlags & 0x10000) {
        return -1;
    }
    return (signed char)g_BattleActionQueueCount;
}

int Battle_IsActive(void) {
    if (g_BattleActionQueueCount != 0) {
        return 0;
    }
    if ((g_GameStateFlags & 2) == 0 || (g_ActiveActor->stateFlags & 0x10000) == 0) {
        return 1;
    }
    return 0;
}

int Pad_IsMenuConfirmAvailable(void) {
    if (g_BattleActionQueueCount != 0) {
        return 0;
    }
    if ((g_GameStateFlags & 2) == 0 || (g_ActiveActor->stateFlags & 0x10000) == 0) {
        return 1;
    }
    return 0;
}

extern int g_BattleAttackPmSlot;

void Battle_DispatchEntityEffect(void) {
    int action_id;
    BattleAction *action;
    BattleAction *action2;
    int action_id2;
    BattleEntity *next_entity;
    int next_id;
    int result;
    u32 word10;

    action = g_ActiveActor->action;
    action_id = action->actionCode.actionId;

    if (action_id == 6) {
        if ((action->attackWord & 0x3FF) == 0) {
            BattleCmd_CommitAmmoAndUpdate(action);
        }
        g_BattleAttackPmSlot = Scene_LoadRoomAssets(3, D_8009D254);
        return;
    }

    if (action_id == 8) {
        g_BattleImpactPmSlot = Scene_LoadRoomAssets(6, D_8009D254);
        return;
    }

    if ((action->attackWord & 0x3FF) == 0) {
        BattleCmd_CommitAmmoAndUpdate(action);
    }

    action2 = g_ActiveActor->action;
    action_id2 = action2->actionCode.actionId;

    if (action_id2 == 5 || ((word10 = action2->turnWord) & 0x1F00) != 0) {
        result = Scene_LoadRoomAssets(0, D_8009D254);
        next_entity = D_8009D254;
        next_id = 5;
    } else {
        if ((action_id2 == 2) || ((word10 & 0xC0) == 0x80)) {
            result = Scene_LoadRoomAssets(1, D_8009D254);
        } else {
            result = Scene_LoadRoomAssets(2, D_8009D254);
        }
        next_entity = D_8009D254;
        next_id = 4;
    }

    g_BattleAttackPmSlot = result;
    g_BattleImpactPmSlot = Scene_LoadRoomAssets(next_id, next_entity);
}

void Battle_CheckEvasion(BattleEntity *actor, BattleEvasionOutcome *outcome,
                         int attackType)
{
    BattleAction *action;
    int range;
    int distance;
    int roll;
    int threshold;
    int special;
    int hit;

    outcome->reaction = 0;
    if (!(actor->entityFlags & 0x4000)) {
    if (g_ActiveActor->stateFlags & 0x100000) {
        outcome->status = 1;
        outcome->reaction = 2;
        return;
    }

    outcome->status = 0;
    range = AYA_DARKNESS_RANGE;
    if ((g_ActiveActor->stateFlags & AYA_DARKNESS_MASK) != AYA_DARKNESS) {
        range = g_ActiveActor->action->range;
    }
    distance = Battle_CalcDistToPlayer(actor, D_8009D254) * 1000 / range;
    {
        int random = rand();
        int quotient = random / 100;
        roll = random - quotient * 100;
    }

    action = g_ActiveActor->action;
    if (action->actionCode.actionId == 8) {
#ifdef PE_MELEE_PROTOTYPE
        /* Reach is measured to the body (Battle_MeleeClubDistance); the crit bands below keep the
         * native aim distance, so club crit rates (and the balance sized on them) don't change. */
        if (Battle_MeleeClubDistance(actor, D_8009D254) * 1000 / range >= 1001) return;
#else
        if (distance >= 1001) return;
#endif
        threshold = ((action->turnWord >> 17) & 1) * 20;
        if (distance >= 801) threshold += 5;
        else if (distance >= 501) threshold += 20;
        else threshold += 50;
        if ((s16)roll < threshold) outcome->reaction = 3;
        outcome->status = 1;
        return;
    }

    if (action->actionCode.actionId == 6 && attackType != 0) {
        outcome->status = 1;
        switch (attackType) {
        case 2:
            outcome->reaction = 1;
            break;
        case 3:
            outcome->reaction = 2;
            break;
        default:
            outcome->reaction = 4;
            break;
        }
        return;
    }

    if (distance >= 1801) return;
    if (distance >= 1501) {
    hit = (s16)roll < 25;
    if (!hit) return;
    outcome->status = 1;
    outcome->reaction = 1;
    return;
    }
below_1501:
    if (distance >= 1201) {
    hit = (s16)roll < 50;
    if (!hit) return;
    outcome->status = 1;
    outcome->reaction = 1;
    return;
    }
below_1201:
    if (distance >= 1001) {
    hit = (s16)roll < 80;
    if (!hit) return;
    outcome->status = 1;
    outcome->reaction = 1;
    return;

    }
close_range:
    special = (g_ActiveActor->action->turnWord >> 17) & 1;
    threshold = special * 20;
    if (distance >= 801) threshold += 5;
    else if (distance >= 501) threshold += 20;
    else threshold += 50;
    outcome->reaction = (s16)roll < threshold ? 3 : 2;
    outcome->status = 1;
    return;

    }
invalid:
    outcome->status = -1;
done:
    return;
}

int Battle_BuildTargetList(void) {
    s32 entityFlags;
    s32 nextIndex;
    s32 finalCount;
    s32 distance;
    s16 angle;
    EnemyCombatant *candidateCore;
    BattleEntity *player;
    BattleEntity *candidate;

    candidate = (BattleEntity *)g_FieldActorListHead;
    g_BattleTargetIndex = 0;
    if (!D_8009D254) { g_BattleTargetList[0].actor = NULL; return 0; }
    if (candidate != NULL) {
        do {
            player = D_8009D254;
            if (candidate != player) {
                candidateCore = candidate->core;
                if (candidateCore != NULL) {
                    entityFlags = candidate->entityFlags;
                    if (((entityFlags & 0x2040) != 0x40) && !(entityFlags & 0x4000) &&
                        (candidateCore->hpAlive > 0) && g_BattleTargetIndex < BATTLE_TARGET_CAPACITY) {
                        g_BattleTargetList[g_BattleTargetIndex].actor = candidate;
#ifdef PE_MELEE_PROTOTYPE
                        distance = Battle_MeleeClubDistance(candidate, player);   /* = native unless a club */
#else
                        distance = Battle_CalcDistToPlayer(candidate, player);
#endif
                        g_BattleTargetList[g_BattleTargetIndex].dist = distance;
                        angle = ratan2(
                            candidate->renderObject.target_x -
                                D_8009D254->posX.parts.integer,
                            candidate->renderObject.target_z -
                                D_8009D254->posZ.parts.integer);
                        nextIndex = g_BattleTargetIndex + 1;
                        g_BattleTargetList[g_BattleTargetIndex].angle = angle;
                        angle = nextIndex;
                        g_BattleTargetIndex = angle;
                    }
                }
            }
            candidate = candidate->next;
        } while (candidate != NULL);
    }
    if (g_BattleTargetIndex >= 2) {
        Battle_SortTargets(g_BattleTargetList, 0, (s8) (g_BattleTargetIndex - 1));
    }
    finalCount = g_BattleTargetIndex;
    g_BattleTargetList[finalCount].actor = NULL;
    /* USA's seventh terminator cleared only the shared saved-X word. Keep
     * that observable store without an eight-byte pointer overwriting Y. */
    if (finalCount == BATTLE_TARGET_CAPACITY) g_AyaSavedPos.x = 0;
    return finalCount;
}

void Battle_SortTargets(BattleTarget *records, signed char first, signed char last)
{
    signed char pivot;
    signed char scan;

    if (first < last) {
        Battle_SwapRecords(records, first, (signed char)((first + last) / 2));
        pivot = first;
        for (scan = pivot; scan <= last; scan++) {
            if (g_BattleTargetList[scan].dist < g_BattleTargetList[first].dist) {
                pivot++;
                Battle_SwapRecords(records, pivot, scan);
            }
        }
        Battle_SwapRecords(records, first, pivot);
        Battle_SortTargets(records, first, (signed char)(pivot - 1));
        Battle_SortTargets(records, (signed char)(pivot + 1), last);
    }
}

void Battle_SwapRecords(BattleTarget *records, int from_arg, int to_arg) {
    BattleTarget tmp;
    int from_index;
    int to_index;

    from_index = (signed char)from_arg;
    to_index = (signed char)to_arg;

    tmp = records[from_index];
    records[from_index] = records[to_index];
    records[to_index] = tmp;
}

/* Copy the action the battle menu chose into the player's action record. */
void Battle_CopyPadStateToRecord(void) {
    BattleAction *record = g_ActiveActor->action;
    BattleAttackBits attack_value, source_attack;
    BattleTurnBits turn_value, source_turn;
    memcpy(&attack_value, &record->attackWord, sizeof(attack_value));
    memcpy(&turn_value, &record->turnWord, sizeof(turn_value));
    memcpy(&source_attack, &g_BattleMenuActionRecord.attackWord, sizeof(source_attack));
    memcpy(&source_turn, &g_BattleMenuActionRecord.turnWord, sizeof(source_turn));
    BattleAttackBits *attack = &attack_value;
    BattleTurnBits *turn = &turn_value;

    record->powerBonus = g_BattleMenuActionRecord.powerBonus;
    record->range = g_BattleMenuActionRecord.range;
    record->field04 = g_BattleMenuActionRecord.field04;
    record->actionCode.actionId = g_BattleMenuActionRecord.actionCode.actionId;
    record->hitTrigger = g_BattleMenuActionRecord.hitTrigger;
    attack->count = source_attack.count;
    attack->power = source_attack.power;
    attack->category = source_attack.category;
    turn->actions = source_turn.actions;
    turn->slot = source_turn.slot;
    turn->mode = source_turn.mode;
    turn->element8 = source_turn.element8;
    turn->element9 = source_turn.element9;
    turn->element10 = source_turn.element10;
    turn->element11 = source_turn.element11;
    turn->element12 = source_turn.element12;
    turn->hitState = source_turn.hitState;
    turn->flag15 = source_turn.flag15;
    turn->flag16 = source_turn.flag16;
    turn->flag17 = source_turn.flag17;
    memcpy(&record->attackWord, &attack_value, sizeof(attack_value));
    memcpy(&record->turnWord, &turn_value, sizeof(turn_value));
}

/* Copy the enemy attributes the menu record carries into the active actor. */
void Battle_SyncEnemyAttributes(void) {
    BattleAttributes source_value;
    memcpy(&source_value, &g_BattleMenuActionRecord, sizeof(source_value));
    BattleAttributes *source = &source_value;
    BattleAttributes *attributes = g_ActiveActor->attributes;
    BattleEffectBits effect_value, source_effect;
    memcpy(&effect_value, &attributes->effectFlags, sizeof(effect_value));
    memcpy(&source_effect, (u8 *)&g_BattleMenuActionRecord + 4, sizeof(source_effect));
    BattleEffectBits *effects = &effect_value;

    attributes->parameterWord.fields.first = source->parameterWord.fields.first;
    attributes->parameterWord.fields.second = source->parameterWord.fields.second;
    attributes->parameterWord.fields.third = source->parameterWord.fields.third;
    attributes->parameterWord.fields.reserved = source->parameterWord.fields.reserved;
    effects->resistPoison = source_effect.resistPoison;
    effects->resistConfusion = source_effect.resistConfusion;
    effects->resistDarkness = source_effect.resistDarkness;
    effects->resistStiffness = source_effect.resistStiffness;
    effects->resistSteal = source_effect.resistSteal;
    effects->armorMode = source_effect.armorMode;
    effects->mod14 = source_effect.mod14;
    effects->mod16 = source_effect.mod16;
    effects->mod11 = source_effect.mod11;
    effects->mod17 = source_effect.mod17;
    effects->mod12 = source_effect.mod12;
    effects->autoMedicine = source_effect.autoMedicine;
    effects->autoCure = source_effect.autoCure;
    effects->mod13 = source_effect.mod13;
    effects->mod15 = source_effect.mod15;
    memcpy(&attributes->effectFlags, &effect_value, sizeof(effect_value));
}

extern u8 g_BattleActionIndex;
extern u8 g_BattleActionQueueCount;

void Battle_FlushScriptSounds(void) {
    unsigned int index;
    int flags;
    int mask;
    int item;

    index = g_BattleActionIndex;
    while ((index & 0xFF) < g_BattleActionQueueCount) {
        item = (u16)g_BattleActionQueue[index & 0xFF].command;
        if ((unsigned int)(item - 3) < 0x180) {
            Inv_AddItem((short)item - 3);
        }

        index++;
    }

    flags = g_GameStateFlags;
    mask = -0x101;
    g_BattleActionQueueCount = 0;
    g_BattleActionIndex = 0;
    g_GameStateFlags = flags & mask;
}

void Battle_StepScriptEntry(void)
{
    BattleInitSlot *entry;
    BattleEntity *entity;
    int command;

    if (g_BattleActionIndex < g_BattleActionQueueCount) {
        entry = &g_BattleActionQueue[g_BattleActionIndex];
        entity = D_8009D254;
        entity->motionX = 0;
        entity->motionY = 0;
        entity->motionZ = 0;
        if (entity->actionMode >= 4) {
            command = entry->command;
            if (command < 3) {
                Battle_UpdatePlayerTurn();
            } else if (command < 0x183) {
                Battle_ApplyPlayerHit();
            } else if (command < 0x197) {
                Battle_AdvancePhase();
            } else if (command < 0x199) {
                Entity_SetActionMode(entity, 13);
                D_8009D2E8 |= 1;
                g_BattleActionIndex++;
                D_8009D254->entityFlags |= 0x100;
            } else {
                if ((s8)Battle_RollEscapeChance() == 1) {
                    Entity_SetActionMode(D_8009D254, g_ActiveActor->actionMode12);
                    g_BattleModeState = 4;
                }
                g_BattleActionIndex++;
            }
        }
    } else {
        g_BattleActionIndex = 0;
        g_BattleActionQueueCount = 0;
    }
}
