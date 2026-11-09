/* Challenges: Hardcore (permadeath).
 *
 *   Death      the game's own game over, not a knockout. When Aya falls the battle goes to phase 3
 *              (Battle_Update): with Preraise (state flag 0x800) or a Revive (item 0x12) in the bag
 *              she gets up again (Battle_StepVictory); otherwise Battle_StepPostBattle plays the
 *              game over and ends in Boot_Shutdown (the title). The branch is decided on the phase's
 *              first frame, so the run is written to the per-run file as dead right then, before the
 *              game-over animation: closing the game during it doesn't save the run. Quick load
 *              (pe_quicksave) only works in free field, so it can't step in either.
 *   Its saves  every save of the run carries its run id (card slots, quick saves, copies). Loading
 *              one of a dead run: on the first frame the game allows a soft reset (the condition the
 *              game's own reset combo checks), a message, then the title by the game's own
 *              Boot_Shutdown. Nothing is deleted. The load itself can't be refused from a mod: the
 *              load-admission code is in host files the PS5 port pins.
 *   Locks      New Game only, and nothing lowered in its run: its definition's flags (pe_ch_run.c).
 */
#include "pe_challenges.h"
#include "pe_game.h"
#include "pe_mod_host.h"
#include "pe1/game_state.h"

#define PE_PHASE_AYA_DOWN    3       /* g_BattleModeState: Aya's HP reached 0 */
#define PE_STATE_PRERAISE    0x800   /* AYA_PRERAISE */
#define PE_ITEM_REVIVE       0x12
#define PE_SOFT_RESET_BLOCK  0x4200u /* g_GameStateFlagsWord bits that hold off the reset combo */

int Inv_CountByValue(int);

static int s_marked;   /* this battle's game over is already written */
static int s_kick;     /* a dead run's save was loaded: back to the title */

static int pe_hardcore_battle(void *ctx, void *user) {
    const Combatant *aya;
    (void)ctx; (void)user;
    if (!pe_ch_on(CH_HARDCORE) || s_marked || g_BattleModeState != PE_PHASE_AYA_DOWN) return 0;
    aya = pe_aya_core();
    if (!aya || (s16)aya->curHP > 0 || (aya->stateFlags & PE_STATE_PRERAISE) || Inv_CountByValue(PE_ITEM_REVIVE) != 0)
        return 0;
    s_marked = 1;
    pe_ch_run_mark_dead(g_ch_run.run_id);
    pe_log("challenge_hardcore_dead", "run=%08x battles=%u seconds=%u", (unsigned)g_ch_run.run_id,
           (unsigned)g_ch_run.battles, (unsigned)g_ch_run.seconds);
    pe_toast("Hardcore: the run has ended");
    return 0;
}

static int pe_hardcore_frame(void *ctx, void *user) {
    (void)ctx; (void)user;
    if (!pe_in_battle()) s_marked = 0;
    if (!s_kick || !pe_game_loaded() || pe_in_battle() || (g_GameStateFlagsWord & PE_SOFT_RESET_BLOCK)) return 0;
    s_kick = 0;
    pe_log("challenge_hardcore_refused", "run=%08x", (unsigned)g_ch_run.run_id);
    pe_toast("This Hardcore run has ended: back to the title");
    Boot_Shutdown();
    return 0;
}

static int pe_hardcore_save_read(void *ctx, void *user) {
    (void)ctx; (void)user;
    s_marked = 0;
    s_kick = pe_ch_on(CH_HARDCORE) && pe_ch_run_is_dead(g_ch_run.run_id);
    return 0;
}

static int pe_hardcore_new_game(void *ctx, void *user) {
    (void)ctx; (void)user;
    s_marked = 0;
    s_kick = 0;
    return 0;
}

static void pe_hardcore_register(const char *mod_id) {
    pe_hook_add(PE_HOOK_BATTLE_UPDATE, pe_hardcore_battle, NULL, PE_PRIORITY_LATE, mod_id);
    pe_hook_add(PE_HOOK_FRAME, pe_hardcore_frame, NULL, PE_PRIORITY_LATE, mod_id);
    pe_hook_add(PE_HOOK_SAVE_READ, pe_hardcore_save_read, NULL, PE_PRIORITY_LATE, mod_id);
    pe_hook_add(PE_HOOK_NEW_GAME, pe_hardcore_new_game, NULL, PE_PRIORITY_NORMAL, mod_id);
}

const PeChallengeDef g_ch_hardcore = {
    CH_HARDCORE, "Hardcore", "Hardcore",
    "Permadeath: a game over ends the run for good. New Game only; locks every challenge on.",
    NULL, 0, 0, CH_DEF_NEW_GAME_ONLY | CH_DEF_LOCKS_RUN, pe_hardcore_register
};
