/* Project original typed combat-HP admission; not a frame refill. */
#ifndef PE1_HP_LOSS_HOOK_H
#define PE1_HP_LOSS_HOOK_H
#include "pe_hooks.h"
static inline int Pe_FilterCombatHpLoss(const void *actor,int hp,int loss,unsigned source){
    if(hp>0&&loss>0){PeHookHpLoss query={actor,hp,&loss,source};PE_HOOK(PE_HOOK_HP_LOSS,&query);}
    return loss;
}
#endif
