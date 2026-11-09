/* Challenge: No EXP (a low-level run). Enemy deaths add their EXP to g_BattleExpReward; after a won
 * battle Battle_Update's phase 2 (Battle_StepLevelUp) pays it through Aya_SetTotalExp. The battle
 * hook runs at Battle_Update's entry, so in phase 2 the total is set to 0 just before it is paid.
 * Bonus points, drops and items are untouched. Mods that scale rewards with EXP (battle Junk) see 0.
 */
#include "pe_challenges.h"
#include "pe_game.h"
#include "pe_mod_host.h"
#include "pe1/battle_level_up.h"

#define PE_PHASE_REWARDS 2   /* g_BattleModeState: Battle_StepLevelUp */

static int pe_no_exp_battle(void *ctx, void *user) {
    (void)ctx; (void)user;
    if (pe_ch_on(CH_NO_EXP) && g_BattleModeState == PE_PHASE_REWARDS && g_BattleExpReward)
        g_BattleExpReward = 0;
    return 0;
}

static void pe_no_exp_register(const char *mod_id) {
    pe_hook_add(PE_HOOK_BATTLE_UPDATE, pe_no_exp_battle, NULL, PE_PRIORITY_EARLY, mod_id);
}

const PeChallengeDef g_ch_no_exp = {
    CH_NO_EXP, "No EXP", "No EXP",
    "Battles give no EXP: Aya stays at her level. Items and bonus points as usual.",
    NULL, 0, 0, 0, pe_no_exp_register
};
