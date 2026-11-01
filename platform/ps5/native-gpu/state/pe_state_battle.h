#ifndef PE_STATE_BATTLE_H
#define PE_STATE_BATTLE_H
#include "pe_state.h"
typedef struct PeBattleNumeric {uint64_t session,scene,binding,assetGeneration;uint32_t entitySerialCounter;uint16_t entityLiveCount;const void *playerAction,*playerAttributes;const void *enemyAction[7],*enemyAttributes[7],*enemyField70[7];int active[7];uint32_t playerActionRole,playerAttributesRole,targetMode,targetMask,targetCores[14];} PeBattleNumeric;
/* Read-only target parser: same immutable static pointer roles, independent live cohort. */
int PeBattleNumeric_PreflightTarget(PeBattleNumeric*,const unsigned char*,size_t,uint32_t,const uint32_t coreRoles[14]);
/* Explicit internal graph transaction context, never a user availability toggle. */
int PeBattleNumeric_SetTargetGraph(PeBattleNumeric*,uint32_t,const uint32_t coreRoles[14]);
int PeBattleNumeric_IsCurrent(const PeBattleNumeric*);
int PeBattleNumeric_Bind(PeBattleNumeric *);
int PeState_RegisterBattleNumeric(PeStateRegistry *,PeBattleNumeric *);
#endif
