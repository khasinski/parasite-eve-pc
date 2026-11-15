/* Enemy effect admission (PE_HOOK_ENEMY_EFFECT); without mods it always admits. */
#ifndef PE1_ENEMY_EFFECT_HOOK_H
#define PE1_ENEMY_EFFECT_HOOK_H
#include "pe_hooks.h"
static inline int Pe_EnemyEffectCancelled(int effect,int stage,const void *target){
    int cancel=0;
    PeHookEnemyEffect query={effect,stage,target,&cancel};
    (void)query;
    PE_HOOK(PE_HOOK_ENEMY_EFFECT,&query);
    return cancel;
}
#endif
