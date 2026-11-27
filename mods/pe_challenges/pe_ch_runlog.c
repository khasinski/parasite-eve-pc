/* Challenges: what a run counts while it is played.
 *
 *   Battles    won with a challenge on (PE_HOOK_BATTLE_REWARDS), in the save record.
 *   Time       wall-clock play time with a challenge on, in the save record. Only frames the game
 *              runs count (F1 pauses the game); a gap over a second (a load, a stall) is left out.
 *   Knockouts  Aya's HP reaching 0 in a battle with a challenge on, counted in the run history
 *              (pe_ch_history.c) because the reload that follows would undo a count in the save.
 */
#include <SDL3/SDL.h>

#include "pe_challenges.h"
#include "pe_game.h"
#include "pe_mod_host.h"

static int s_prev_hp = -1;
static uint64_t s_last_ms;
static unsigned s_ms;

static void pe_tick_time(void) {
    const uint64_t now = SDL_GetTicks();
    if (!g_ch_run.active || !pe_game_loaded()) { s_last_ms = 0; return; }
    if (s_last_ms && now > s_last_ms && now - s_last_ms < 1000) {
        s_ms += (unsigned)(now - s_last_ms);
        if (s_ms >= 1000) { g_ch_run.seconds += s_ms / 1000; s_ms %= 1000; }
    }
    s_last_ms = now;
}

static void pe_watch_knockout(void) {
    const Combatant *aya = pe_aya_core();
    int hp;
    if (!g_ch_run.active || !g_ch_run.run_id || !aya) { s_prev_hp = -1; return; }
    hp = (s16)aya->curHP;
    if (s_prev_hp > 0 && hp <= 0) pe_ch_history_knockout();
    s_prev_hp = hp;
}

static int pe_runlog_battle(void *ctx, void *user) {
    (void)ctx; (void)user;
    pe_tick_time();
    pe_watch_knockout();
    return 0;
}

static int pe_runlog_frame(void *ctx, void *user) {
    (void)ctx; (void)user;
    pe_tick_time();
    if (!pe_in_battle()) s_prev_hp = -1;
    return 0;
}

static int pe_runlog_reset(void *ctx, void *user) {
    (void)ctx; (void)user;
    s_prev_hp = -1;
    s_last_ms = 0;
    s_ms = 0;
    return 0;
}

static int pe_runlog_won(void *ctx, void *user) {
    (void)ctx; (void)user;
    if (g_ch_run.active) g_ch_run.battles++;
    return 0;
}

void pe_ch_runlog_register(const char *mod_id) {
    pe_hook_add(PE_HOOK_BATTLE_UPDATE, pe_runlog_battle, NULL, PE_PRIORITY_LATE, mod_id);
    pe_hook_add(PE_HOOK_FRAME, pe_runlog_frame, NULL, PE_PRIORITY_LATE, mod_id);
    pe_hook_add(PE_HOOK_SAVE_READ, pe_runlog_reset, NULL, PE_PRIORITY_NORMAL, mod_id);
    pe_hook_add(PE_HOOK_NEW_GAME, pe_runlog_reset, NULL, PE_PRIORITY_NORMAL, mod_id);
    pe_hook_add(PE_HOOK_BATTLE_REWARDS, pe_runlog_won, NULL, PE_PRIORITY_NORMAL, mod_id);
}
