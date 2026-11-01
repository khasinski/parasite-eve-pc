#ifndef PE_STATE_BATTLE_H
#define PE_STATE_BATTLE_H
#include "pe_state.h"
typedef struct PeBattleNumeric {uint64_t session,scene,binding,assetGeneration;uint32_t entitySerialCounter;uint16_t entityLiveCount;const void *playerAction,*playerAttributes;const void *enemyAction[7],*enemyAttributes[7],*enemyField70[7];int active[7];} PeBattleNumeric;
int PeBattleNumeric_Bind(PeBattleNumeric *);
int PeState_RegisterBattleNumeric(PeStateRegistry *,PeBattleNumeric *);
#endif
