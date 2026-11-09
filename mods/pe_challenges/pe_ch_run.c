/* Challenges: the challenge table, the F1 rows and the run.
 *
 * Which challenges are on is saved with the game (record "pe_challenges.run"), so loading a save
 * brings back its own set and the rows show it. At the title the rows are the player's pick for the
 * next New Game (kept in pe_mods.ini like every row). Challenges can be turned on or off at any
 * time; one turned off (or Tough crowd lowered) after the run's first minute or first won battle
 * marks the run "assisted". Nothing is ever blocked.
 */
#include <stdio.h>
#include <string.h>
#include <SDL3/SDL.h>

#include "pe_challenges.h"
#include "pe_game.h"
#include "pe_mod_host.h"
#include "pe_mod_menu.h"
#include "pe_save.h"

#define CH_GRACE_SECONDS 60

PeChallengeRun g_ch_run;

/* ---- the table ---------------------------------------------------------------------------- */

typedef struct PeChallengeInfo {
    const char *label;          /* the F1 row */
    const char *short_name;     /* lists in the summary and toasts */
    const char *description;
    const char *const *levels;  /* NULL: an on / off row; else a choice, index 0 = off */
    unsigned level_count;
} PeChallengeInfo;

static const char *const kToughLevels[] = {"Off", "1.5x HP", "2x HP"};

static const PeChallengeInfo kChallenges[CH_COUNT] = {
    [CH_NO_PE]     = {"No Parasite Energy", "No PE",
                      "No PE tab in battle, and every power is dimmed in the field too.", NULL, 0},
    [CH_NO_ITEMS]  = {"No items in battle", "No items",
                      "No Item tab in battle. Items still work in the field.", NULL, 0},
    [CH_NO_ESCAPE] = {"No escape", "No escape",
                      "No Escape tab in battle: every fight must be won.", NULL, 0},
    [CH_GLASS]     = {"Glass cannon", "Glass cannon",
                      "Aya takes double damage from hits and contact, and deals 1.5x.", NULL, 0},
    [CH_TOUGH]     = {"Tough crowd", "Tough crowd",
                      "Enemies take less damage, as if they had 1.5x or 2x HP. EXP and drops unchanged.",
                      kToughLevels, 3},
};

/* The rows: 0 off, else on (a level for a level row). */
static int s_on[CH_COUNT];

/* ---- the save record ---------------------------------------------------------------------- */

static int pe_run_check(void *data) {
    PeChallengeRun *r = data;
    if (r->active & ~CH_ALL || r->ever & ~CH_ALL || r->dropped & ~CH_ALL || r->tough > 2 || r->tough_max > 2)
        return 1;
    if (!(r->active & CH_BIT(CH_TOUGH))) r->tough = 0;
    else if (!r->tough) r->active &= ~CH_BIT(CH_TOUGH);
    if (r->tough_max < r->tough) r->tough_max = r->tough;
    r->ever |= r->active;
    return 0;
}

static const PeSaveRecord s_record = {
    "pe_challenges.run", 1, &g_ch_run, sizeof g_ch_run, pe_run_check, NULL, NULL, NULL
};

/* ---- helpers for the other files ---------------------------------------------------------- */

unsigned pe_ch_rows_mask(void) {
    unsigned m = 0;
    for (int i = 0; i < CH_COUNT; ++i)
        if (s_on[i]) m |= CH_BIT(i);
    return m;
}

void pe_ch_rows_turn_on(unsigned mask, int tough) {
    for (int i = 0; i < CH_COUNT; ++i)
        if ((mask & CH_BIT(i)) && !s_on[i]) s_on[i] = 1;
    if (tough > s_on[CH_TOUGH]) s_on[CH_TOUGH] = tough;
}

void pe_ch_rows_clear(void) { memset(s_on, 0, sizeof s_on); }

int pe_ch_underway(void) {
    return g_ch_run.run_id && (g_ch_run.battles > 0 || g_ch_run.seconds >= CH_GRACE_SECONDS);
}

void pe_ch_names(unsigned mask, char *out, unsigned size) {
    size_t used = 0;
    if (!size) return;
    out[0] = 0;
    for (int i = 0; i < CH_COUNT && used + 1 < size; ++i) {
        if (!(mask & CH_BIT(i))) continue;
        used += (size_t)snprintf(out + used, size - used, "%s%s", used ? ", " : "", kChallenges[i].short_name);
        if (used >= size) used = size - 1;
    }
    if (!out[0]) snprintf(out, size, "None");
}

static uint32_t pe_new_run_id(void) {
    const uint64_t t = SDL_GetPerformanceCounter() ^ SDL_GetTicksNS();
    const uint32_t id = (uint32_t)(t ^ (t >> 32)) * 2654435761u;
    return id ? id : 1;
}

/* ---- rows <-> run ------------------------------------------------------------------------- */

/* The rows changed (F1 or a bundle): the run follows. */
static void pe_sync(void) {
    const unsigned want = pe_ch_rows_mask();
    const int tough = s_on[CH_TOUGH];
    unsigned dropped = 0;
    if (!pe_game_loaded()) return;   /* the title (even after a game): the rows are the next pick */
    if (want == g_ch_run.active && tough == g_ch_run.tough) return;
    if (pe_ch_underway()) {
        dropped = g_ch_run.active & ~want;
        if (tough && tough < g_ch_run.tough) dropped |= CH_BIT(CH_TOUGH);
    }
    if (want && !g_ch_run.run_id) g_ch_run.run_id = pe_new_run_id();
    if (dropped & ~g_ch_run.dropped) {
        char names[96], msg[160];
        pe_ch_names(dropped, names, sizeof names);
        snprintf(msg, sizeof msg, "Challenge lowered (%s): run marked assisted", names);
        pe_toast(msg);
    }
    g_ch_run.dropped |= dropped;
    g_ch_run.active = want;
    g_ch_run.ever |= want;
    g_ch_run.tough = (uint8_t)tough;
    if (g_ch_run.tough_max < tough) g_ch_run.tough_max = (uint8_t)tough;
    pe_log("challenge_set", "run=%08x active=%x tough=%d dropped=%x", (unsigned)g_ch_run.run_id,
           (unsigned)want, tough, (unsigned)g_ch_run.dropped);
}

static int pe_on_tick(void *ctx, void *user) {
    (void)ctx; (void)user;
    pe_sync();
    return 0;
}

/* The records are back before any mod's SAVE_READ: the rows show the loaded run. */
static int pe_on_save_read(void *ctx, void *user) {
    (void)ctx; (void)user;
    for (int i = 0; i < CH_COUNT; ++i) s_on[i] = (int)((g_ch_run.active >> i) & 1u);
    s_on[CH_TOUGH] = g_ch_run.tough;
    pe_ch_assist_reset();
    return 0;
}

/* The host reset the record: the rows picked before are the new run's. */
static int pe_on_new_game(void *ctx, void *user) {
    const unsigned want = pe_ch_rows_mask();
    (void)ctx; (void)user;
    memset(&g_ch_run, 0, sizeof g_ch_run);
    pe_ch_assist_reset();
    if (!want) return 0;
    g_ch_run.run_id = pe_new_run_id();
    g_ch_run.active = g_ch_run.ever = want;
    g_ch_run.tough = g_ch_run.tough_max = (uint8_t)s_on[CH_TOUGH];
    pe_log("challenge_new_run", "run=%08x active=%x tough=%d", (unsigned)g_ch_run.run_id, (unsigned)want,
           s_on[CH_TOUGH]);
    return 0;
}

int pe_ch_run_register(const char *mod_id) {
    int failed = 0;
    if (pe_save_register(&s_record) != 0) return -1;
    failed |= pe_hook_add(PE_HOOK_FRAME, pe_on_tick, NULL, PE_PRIORITY_NORMAL, mod_id);
    failed |= pe_hook_add(PE_HOOK_BATTLE_UPDATE, pe_on_tick, NULL, PE_PRIORITY_NORMAL, mod_id);
    failed |= pe_hook_add(PE_HOOK_SAVE_READ, pe_on_save_read, NULL, PE_PRIORITY_EARLY, mod_id);
    failed |= pe_hook_add(PE_HOOK_NEW_GAME, pe_on_new_game, NULL, PE_PRIORITY_EARLY, mod_id);
    for (int i = 0; i < CH_COUNT; ++i) {
        const PeChallengeInfo *c = &kChallenges[i];
        failed |= c->levels
            ? pe_menu_register_choice(0, c->label, c->description, &s_on[i], c->levels, c->level_count)
            : pe_menu_register_toggle(0, c->label, c->description, &s_on[i], NULL);
    }
    return failed ? -1 : 0;
}
