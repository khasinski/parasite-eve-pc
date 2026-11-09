/* Challenges: what the run summary counts besides the save record.
 *
 *   Knockouts  Aya's HP reaching 0 in a battle with a challenge on. A knockout usually ends in a
 *              game over and a reload, which would lose a count kept in the save, so the counts live
 *              beside the preferences in pe_challenges.ini, one line per run ("<run id> <count>",
 *              the newest 64 runs).
 *   Time       wall-clock play time with a challenge on, in the run's save record. Only frames the
 *              game runs count (F1 pauses the game); a gap over a second (a load, a stall) is left out.
 */
#include <stdio.h>
#include <string.h>
#include <SDL3/SDL.h>

#include "pe_challenges.h"
#include "pe_game.h"
#include "pe_mod_host.h"

#define CH_FILE "pe_challenges.ini"
#define CH_RUNS_MAX 64

static struct { uint32_t id, count; } s_runs[CH_RUNS_MAX];
static int s_run_count, s_loaded;
static int s_prev_hp = -1;
static uint64_t s_last_ms;
static unsigned s_ms;

static void pe_runs_load(void) {
    char path[512], line[96];
    FILE *f;
    if (s_loaded) return;
    s_loaded = 1;
    if (pe_mod_path(path, sizeof path, CH_FILE) != 0 || !(f = fopen(path, "r"))) return;
    while (s_run_count < CH_RUNS_MAX && fgets(line, sizeof line, f)) {
        unsigned id, count;
        if (line[0] == '#' || sscanf(line, "%x %u", &id, &count) != 2 || !id) continue;
        s_runs[s_run_count].id = id;
        s_runs[s_run_count].count = count;
        s_run_count++;
    }
    fclose(f);
}

static void pe_runs_save(void) {
    char path[512], temp[520];
    FILE *f;
    if (pe_mod_path(path, sizeof path, CH_FILE) != 0) return;
    snprintf(temp, sizeof temp, "%s.tmp", path);
    if (!(f = fopen(temp, "w"))) return;
    fputs("# Challenges: knockouts per run (run id, count), newest last\n", f);
    for (int i = 0; i < s_run_count; ++i) fprintf(f, "%08x %u\n", (unsigned)s_runs[i].id, (unsigned)s_runs[i].count);
    if (fclose(f) != 0) { remove(temp); return; }
    remove(path);
    if (rename(temp, path) != 0) remove(temp);
}

static uint32_t *pe_runs_slot(uint32_t id, int create) {
    pe_runs_load();
    for (int i = 0; i < s_run_count; ++i)
        if (s_runs[i].id == id) return &s_runs[i].count;
    if (!create) return NULL;
    if (s_run_count == CH_RUNS_MAX) {               /* forget the oldest run */
        memmove(&s_runs[0], &s_runs[1], sizeof s_runs[0] * (CH_RUNS_MAX - 1));
        s_run_count--;
    }
    s_runs[s_run_count].id = id;
    s_runs[s_run_count].count = 0;
    return &s_runs[s_run_count++].count;
}

unsigned pe_ch_knockouts(void) {
    const uint32_t *n;
    if (!g_ch_run.run_id) return 0;
    n = pe_runs_slot(g_ch_run.run_id, 0);
    return n ? *n : 0;
}

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
    if (s_prev_hp > 0 && hp <= 0) {
        uint32_t *n = pe_runs_slot(g_ch_run.run_id, 1);
        ++*n;
        pe_runs_save();
        pe_log("challenge_ko", "run=%08x knockouts=%u", (unsigned)g_ch_run.run_id, (unsigned)*n);
    }
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
