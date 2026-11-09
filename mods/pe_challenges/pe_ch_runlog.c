/* Challenges: what the run summary counts besides the save record, and the per-run file.
 *
 *   Battles    won with a challenge on (PE_HOOK_BATTLE_REWARDS), in the save record.
 *   Time       wall-clock play time with a challenge on, in the save record. Only frames the game
 *              runs count (F1 pauses the game); a gap over a second (a load, a stall) is left out.
 *   Knockouts  Aya's HP reaching 0 in a battle with a challenge on. A knockout usually ends in a
 *              game over and a reload, which would lose a count kept in the save, so it lives in
 *              the per-run file.
 *   Dead runs  Hardcore runs that ended in a game over (pe_ch_hardcore.c), in the same file.
 *
 * The per-run file: pe_challenges.ini beside the preferences, one line per run,
 * "<run id> <knockouts> [dead]", the newest 128 runs. When it is full the oldest live run goes
 * first, so a dead run is forgotten only when every remembered run is dead.
 */
#include <stdio.h>
#include <string.h>
#include <SDL3/SDL.h>

#include "pe_challenges.h"
#include "pe_game.h"
#include "pe_mod_host.h"

#define CH_FILE "pe_challenges.ini"
#define CH_RUNS_MAX 128

typedef struct PeRunLine { uint32_t id, knockouts, dead; } PeRunLine;

static PeRunLine s_runs[CH_RUNS_MAX];
static int s_run_count, s_loaded;
static int s_prev_hp = -1;
static uint64_t s_last_ms;
static unsigned s_ms;

/* ---- the file ----------------------------------------------------------------------------- */

static void pe_runs_load(void) {
    char path[512], line[96];
    FILE *f;
    if (s_loaded) return;
    s_loaded = 1;
    if (pe_mod_path(path, sizeof path, CH_FILE) != 0 || !(f = fopen(path, "r"))) return;
    while (s_run_count < CH_RUNS_MAX && fgets(line, sizeof line, f)) {
        unsigned id, knockouts;
        char word[16] = "";
        if (line[0] == '#' || sscanf(line, "%x %u %15s", &id, &knockouts, word) < 2 || !id) continue;
        s_runs[s_run_count].id = id;
        s_runs[s_run_count].knockouts = knockouts;
        s_runs[s_run_count].dead = strcmp(word, "dead") == 0;
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
    fputs("# Challenges: per run (run id, knockouts, dead = a Hardcore run that ended), newest last\n", f);
    for (int i = 0; i < s_run_count; ++i)
        fprintf(f, "%08x %u%s\n", (unsigned)s_runs[i].id, (unsigned)s_runs[i].knockouts,
                s_runs[i].dead ? " dead" : "");
    if (fclose(f) != 0) { remove(temp); return; }
    remove(path);
    if (rename(temp, path) != 0) remove(temp);
}

static PeRunLine *pe_runs_find(uint32_t id, int create) {
    int drop = 0;
    pe_runs_load();
    for (int i = 0; i < s_run_count; ++i)
        if (s_runs[i].id == id) return &s_runs[i];
    if (!create || !id) return NULL;
    if (s_run_count == CH_RUNS_MAX) {               /* forget the oldest live run (else the oldest) */
        for (int i = 0; i < s_run_count; ++i)
            if (!s_runs[i].dead) { drop = i; break; }
        memmove(&s_runs[drop], &s_runs[drop + 1], sizeof s_runs[0] * (size_t)(s_run_count - drop - 1));
        s_run_count--;
    }
    memset(&s_runs[s_run_count], 0, sizeof s_runs[0]);
    s_runs[s_run_count].id = id;
    return &s_runs[s_run_count++];
}

unsigned pe_ch_knockouts(void) {
    const PeRunLine *r = g_ch_run.run_id ? pe_runs_find(g_ch_run.run_id, 0) : NULL;
    return r ? r->knockouts : 0;
}

int pe_ch_run_is_dead(uint32_t run_id) {
    const PeRunLine *r = run_id ? pe_runs_find(run_id, 0) : NULL;
    return r && r->dead;
}

void pe_ch_run_mark_dead(uint32_t run_id) {
    PeRunLine *r = pe_runs_find(run_id, 1);
    if (!r || r->dead) return;
    r->dead = 1;
    pe_runs_save();
}

/* ---- time, knockouts, battles ------------------------------------------------------------- */

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
        PeRunLine *r = pe_runs_find(g_ch_run.run_id, 1);
        if (r) {
            r->knockouts++;
            pe_runs_save();
            pe_log("challenge_ko", "run=%08x knockouts=%u", (unsigned)g_ch_run.run_id, (unsigned)r->knockouts);
        }
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
