#ifndef PE1_BATTLE_RUNTIME_H
#define PE1_BATTLE_RUNTIME_H

#include "pe1/game_state.h"
#include "pe1/save_slot_metadata.h"

#include "pe1/game_state_types.h"

#include "pe1/asset_find.h"
#include "pe1/entity_animation.h"
#include "pe1/pm.h"
#include "pe1/scene_transition.h"

extern Combatant *g_ActiveActor;
#include "pe1/player_entity.h"
extern s16 g_MenuFrameCommand;
extern int g_BattleAttackPmSlot;
extern u8 g_BattleHitPending;

typedef struct BattleEvasionOutcome {
    s8 status;   /* -1: invalid target, 0: unresolved, 1: resolved */
    s8 reaction; /* effect code, 0..4 */
} BattleEvasionOutcome;

PE1_STATIC_ASSERT(sizeof(BattleEvasionOutcome) == 2, battle_evasion_outcome_size);

typedef struct BattleEnemySlot {
    s32 active;
    EnemyCombatant combatant;
} BattleEnemySlot;

extern int g_BattleImpactPmSlot;
extern int g_BattleEffectPmSlot;
extern s8 g_BattleTargetCycleMode; /* item target mode: 0, or 4..7 while picking */
/* Actions left in the battle turn (Save_ResetGlobalFlags clears it). */
extern unsigned char g_BattleRemainingActions;
extern u8 g_BattleActionAnimModes[4];
/* Message overlay step of Menu_SaveOverlayDraw (0 idle, 1 show, 2 wait),
 * and the frames left on the actor effect that flag 0x100 marks. */
extern u8 g_BattleSaveOverlayActive;
extern u16 g_ActorEffectFlag100Timer;
extern u8 g_BattleSlotCancelPending;
extern u8 g_BattleActionIndex;
extern u8 g_BattleActionQueueCount;
extern int g_BattleModeState;
/* Action record the battle menu fills in (Menu_RunFrameWithArg). Its packed
 * words are also declared on their own: retail reads each of them through
 * its own absolute address. */
extern BattleAction g_BattleMenuActionRecord;
extern s8 g_BattleTargetCount;
extern struct FieldActor *g_FieldActorListHead; /* actor list head */
extern u8 g_EscapeFailedTexts[34];
extern u8 g_EscapeBlockedTexts[28];
extern const EnemyCombatant g_EnemyCombatantInitial;
extern BattleEnemySlot g_BattleEnemySlots[7];
extern u8 g_EntityInstanceCounter;
extern s8 g_BattleLiveEnemyCount;
extern u8 g_BattleStepIndex;
extern u8 g_BattleEnemyEscapeFlag;
/* Frames since the player halted at battle end; Menu_MainUpdate counts it up
 * to 0x5A. */
extern s8 g_BattleHaltTimer;

/* Per-action tables Battle_SetupEnemyAnims copies to the stack: four facing
 * animation modes plus the action frame word, and the offense scale (x/10). */
typedef struct EnemyAnimTable {
    u8 rows[9][5];
} EnemyAnimTable;

typedef struct EnemyOffenseScaleTable {
    u8 values[9];
} EnemyOffenseScaleTable;

extern const EnemyAnimTable g_BattleActionAnimRows;
extern const EnemyOffenseScaleTable g_BattleActionOffenseScale;

void Battle_FinalizeAttackResult(void);
void Battle_Init(void);
void Battle_StepScriptEntry(void);
void Battle_FlushScriptSounds(void);
int Battle_GetAgilityBonus(void);

int Pm_SendCmd(int command, int arg1, int arg2, intptr_t arg3, intptr_t arg4, intptr_t arg5);

void Draw_ResetTextboxes(void);
extern s8 g_BattleTargetIndex;
extern s8 g_BattleActiveTurnSlot, g_BattleItemMenuState;
extern u8 g_BattleGaugeCapLatch;
extern unsigned int g_FieldPadPressed;

void Battle_SetupEntityTarget(BattleEntity *actor);
void Battle_DispatchEntityEffect(void);
void Battle_CycleTarget(s8 mode);
void Battle_SyncEnemyAttributes(void);
void Battle_MarkActiveEntities(BattleTarget *targets, int index);
void Battle_StepPlayerTurn(BattleTarget *targets, int index, int mode);
void Battle_DrawStatusOverlay(RenderObjectEntity *object, int mode, int outOfRange, int entityId);
void Battle_InitFadeVars(void);
void Battle_UpdatePlayerTurn(void);
void Battle_ApplyPlayerHit(void);
int Battle_CalcDistToPlayer(BattleEntity *actor, BattleEntity *player);
int Battle_CalcRelativeAngle(BattleEntity *target, BattleEntity *viewer);
void Battle_CheckEvasion(BattleEntity *actor, BattleEvasionOutcome *outcome,
                         int attackType);
void Battle_AdvancePhase(void);
void BattleCmd_UndoPending(void);
void Battle_CopyPadStateToRecord(void);
int Battle_HandleItemMenu(void);
int Battle_StepEnemyTurn(s8 mode);
void Battle_FillActionQueue(BattleTarget *target);
void Battle_AdvanceTurnSlot(void);
void Entity_AllocSlot(BattleEntity *entity);
void Battle_StepEntityDeath(void);
void Anim_SetInterpRate(RenderObjectEntity *object, int rate);
int CdRom_StepReadState(int mode);
void Battle_ClearMotionTable(void);
void Battle_SetupPlayerPalette(void);
void Battle_ResetEnemyStats(int mode);
void Battle_SetupEnemyAnims(void);
void Battle_StartEnemyAttackEffect(BattleEntity *entity);
void Battle_DrawATBGauge(void);
int Inv_GetWeaponCategoryAmmoBase(unsigned int category);
s8 Pad_GetMenuPressedBitOrDisabled(void);

PE1_STATIC_ASSERT(PE1_OFFSETOF(BattleEnemySlot, active) == 0, battle_enemy_slot_active_offset);
PE1_STATIC_ASSERT(PE1_OFFSETOF(BattleEnemySlot, combatant) % _Alignof(EnemyCombatant) == 0, battle_enemy_slot_combatant_offset);
PE1_STATIC_ASSERT(sizeof(BattleEnemySlot) == PE1_OFFSETOF(BattleEnemySlot, combatant) + sizeof(EnemyCombatant), battle_enemy_slot_size);

/* Aya action sequencer state and its render/scene dependencies. */
extern u8 g_AyaActionStep;
extern s8 g_AyaAttackStep;
/* The BattleEvasionOutcome Battle_CheckEvasion fills for the current hit
 * (status and reaction); Battle_StepAyaAction writes the record itself. */
extern BattleEvasionOutcome g_EvasionOutcome;
/* --test-ignore-hit-reactions: every hit lands as a plain hit (no critical, graze or splash factor),
 * as before the 0.12 fix. Only for recorded route replays made with that behaviour: criticals
 * shorten battles, which moves the random encounters later recordings walk past. */
extern u8 g_BattleTestIgnoreHitReactions;
extern s16 g_AyaActionTimer;
/* Aya's world position (16.16) saved before an action moves her. */
typedef struct BattleSavedPosition {
    s32 x, y, z;
} BattleSavedPosition;
extern BattleSavedPosition g_AyaSavedPos;
/* Aya's rotation saved with the position. */
typedef struct BattleSavedRotation {
    u16 x, facing, z;
} BattleSavedRotation;
extern BattleSavedRotation g_AyaSavedRotation;
/* Set when Aya's action reaches its recovery pose (Battle_StepAyaAction case
 * 12); the cross-flash effect in the task program area clears it when it
 * starts and stops spawning its ember particles once it is set. */
extern u16 g_BattleCrossFlashStop;
int Scene_InitEntityPlayer(int mode);

s16 Battle_CalcAngleToTarget(RenderObjectEntity *object, void *target);
int rsin(int);
int rcos(int);

int Battle_StepAyaAction(void);

/* Enemy update and animation lifecycle. */
void Battle_StepEntityAnimState(BattleEntity *entity);
void Battle_StepEnemyMovement(BattleEntity *entity);
void Battle_UpdateEntityFacing(BattleEntity *entity);
void Battle_DrawStatusPanel(int mode, BattleStatusPanel *panel);
int Battle_ProcessActionSlot(BattleEntity *entity);

void Entity_TickAnimSequences(BattleEntity *entity);

#endif
