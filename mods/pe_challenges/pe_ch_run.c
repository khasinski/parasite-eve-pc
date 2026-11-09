/* Challenges: the F1 rows and the run.
 *
 * Rows come from the challenge definitions (g_ch_defs). Which challenges are on, at what level, is
 * saved with the game (record "pe_challenges.run"), so loading a save brings back its own set and
 * the rows show it. At the title the rows are the player's pick for the next New Game (kept in
 * pe_mods.ini like every row).
 *
 *   Any time        a challenge can be turned on or raised. Turned off or lowered after the run's
 *                   first minute or first won battle, the run is marked "assisted".
 *   New Game only   (CH_DEF_NEW_GAME_ONLY) turned on in a loaded game, the row goes back off.
 *   Locks the run   (CH_DEF_LOCKS_RUN) while one is on, rows can't go below the run's levels.
 * Nothing is ever blocked otherwise.
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

/* The rows, by id: 0 off, else the level (1 for an on / off row). */
static int s_on[CH_COUNT];

static int pe_max_level(const PeChallengeDef *d) {
    return d && d->levels && d->level_count > 1 ? (int)d->level_count - 1 : 1;
}

/* ---- the save record ---------------------------------------------------------------------- */

static int pe_run_check(void *data) {
    PeChallengeRun *r = data;
    if (r->active & ~CH_ALL || r->ever & ~CH_ALL || r->dropped & ~CH_ALL) return 1;
    for (int id = 0; id < (int)sizeof r->level; ++id) {
        const PeChallengeDef *d = id < CH_COUNT ? pe_ch_def(id) : NULL;
        if (!d || !(r->active & CH_BIT(id))) { r->level[id] = 0; continue; }
        if (r->level[id] < 1) r->level[id] = 1;
        if (r->level[id] > pe_max_level(d)) r->level[id] = (uint8_t)pe_max_level(d);
    }
    r->ever |= r->active;
    return 0;
}

static const PeSaveRecord s_record = {
    "pe_challenges.run", 2, &g_ch_run, sizeof g_ch_run, pe_run_check, NULL, NULL, NULL
};

/* ---- helpers for the other files ---------------------------------------------------------- */

unsigned pe_ch_rows_mask(void) {
    unsigned m = 0;
    for (int i = 0; i < CH_COUNT; ++i)
        if (s_on[i]) m |= CH_BIT(i);
    return m;
}

void pe_ch_rows_turn_on(unsigned mask, int level) {
    for (int id = 0; id < CH_COUNT; ++id) {
        const PeChallengeDef *d = pe_ch_def(id);
        int want;
        if (!(mask & CH_BIT(id)) || !d) continue;
        want = level < 1 ? 1 : level;
        if (want > pe_max_level(d)) want = pe_max_level(d);
        if (s_on[id] < want) s_on[id] = want;
    }
}

void pe_ch_rows_clear(void) { memset(s_on, 0, sizeof s_on); }

int pe_ch_underway(void) {
    return g_ch_run.run_id && (g_ch_run.battles > 0 || g_ch_run.seconds >= CH_GRACE_SECONDS);
}

int pe_ch_run_locked(void) {
    for (unsigned i = 0; i < g_ch_def_count; ++i)
        if ((g_ch_defs[i]->flags & CH_DEF_LOCKS_RUN) && pe_ch_on(g_ch_defs[i]->id)) return 1;
    return 0;
}

void pe_ch_names(unsigned mask, char *out, unsigned size) {
    size_t used = 0;
    if (!size) return;
    out[0] = 0;
    for (unsigned i = 0; i < g_ch_def_count && used + 1 < size; ++i) {
        const PeChallengeDef *d = g_ch_defs[i];
        if (!(mask & CH_BIT(d->id))) continue;
        used += (size_t)snprintf(out + used, size - used, "%s%s", used ? ", " : "", d->short_name);
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

/* New Game only and run locks: rows that ask otherwise go back to what the run has. */
static void pe_apply_run_rules(void) {
    const PeChallengeDef *late = NULL;
    int refused = 0;
    for (unsigned i = 0; i < g_ch_def_count; ++i) {
        const PeChallengeDef *d = g_ch_defs[i];
        if ((d->flags & CH_DEF_NEW_GAME_ONLY) && s_on[d->id] && !pe_ch_on(d->id)) {
            s_on[d->id] = 0;
            late = d;
        }
    }
    if (late) {
        char msg[128];
        snprintf(msg, sizeof msg, "%s starts with a New Game: pick it at the title", late->label);
        pe_toast(msg);
    }
    if (!pe_ch_run_locked()) return;
    for (int id = 0; id < CH_COUNT; ++id)
        if (s_on[id] < pe_ch_level(id)) { s_on[id] = pe_ch_level(id); refused = 1; }
    if (refused) pe_toast("Locked run: challenges can be added, never lowered");
}

/* The rows changed (F1 or a bundle): the run follows. */
static void pe_sync(void) {
    unsigned want, dropped = 0;
    int changed = 0;
    if (!pe_game_loaded()) return;   /* the title (even after a game): the rows are the next pick */
    pe_apply_run_rules();
    want = pe_ch_rows_mask();
    for (int id = 0; id < CH_COUNT; ++id) {
        if (s_on[id] == pe_ch_level(id)) continue;
        changed = 1;
        if (s_on[id] < pe_ch_level(id) && pe_ch_underway()) dropped |= CH_BIT(id);
    }
    if (!changed) return;
    if (want && !g_ch_run.run_id) g_ch_run.run_id = pe_new_run_id();
    if (dropped & ~g_ch_run.dropped) {
        char names[128], msg[192];
        const int first = !pe_ch_assisted();
        pe_ch_names(dropped, names, sizeof names);
        snprintf(msg, sizeof msg, "Challenge lowered (%s): run marked assisted", names);
        pe_toast(msg);
        g_ch_run.dropped |= dropped;
        if (first) {
            snprintf(msg, sizeof msg, "lowered: %s", names);
            pe_ch_emit_assisted(msg);
        }
    }
    g_ch_run.dropped |= dropped;
    g_ch_run.active = want;
    g_ch_run.ever |= want;
    for (int id = 0; id < CH_COUNT; ++id) g_ch_run.level[id] = (uint8_t)s_on[id];
    pe_log("challenge_set", "run=%08x active=%x dropped=%x", (unsigned)g_ch_run.run_id, (unsigned)want,
           (unsigned)g_ch_run.dropped);
    pe_ch_emit_changed();
}

static int pe_on_tick(void *ctx, void *user) {
    (void)ctx; (void)user;
    pe_sync();
    return 0;
}

/* The records are back before any mod's SAVE_READ: the rows show the loaded run. */
static int pe_on_save_read(void *ctx, void *user) {
    (void)ctx; (void)user;
    for (int id = 0; id < CH_COUNT; ++id) s_on[id] = pe_ch_level(id);
    pe_ch_assist_reset();
    pe_ch_emit_changed();
    return 0;
}

/* The host reset the record: the rows picked at the title are the new run's. */
static int pe_on_new_game(void *ctx, void *user) {
    const unsigned want = pe_ch_rows_mask();
    (void)ctx; (void)user;
    memset(&g_ch_run, 0, sizeof g_ch_run);
    pe_ch_assist_reset();
    if (want) {
        g_ch_run.run_id = pe_new_run_id();
        g_ch_run.active = g_ch_run.ever = want;
        for (int id = 0; id < CH_COUNT; ++id) g_ch_run.level[id] = (uint8_t)s_on[id];
        pe_log("challenge_new_run", "run=%08x active=%x", (unsigned)g_ch_run.run_id, (unsigned)want);
    }
    pe_ch_emit_changed();
    return 0;
}

int pe_ch_run_register(const char *mod_id) {
    int failed = 0;
    for (int id = 0; id < CH_COUNT; ++id)
        if (!pe_ch_def(id)) { pe_mod_log(mod_id, "challenge id %d has no definition", id); return -1; }
    if (pe_save_register(&s_record) != 0) return -1;
    failed |= pe_hook_add(PE_HOOK_FRAME, pe_on_tick, NULL, PE_PRIORITY_NORMAL, mod_id);
    failed |= pe_hook_add(PE_HOOK_BATTLE_UPDATE, pe_on_tick, NULL, PE_PRIORITY_NORMAL, mod_id);
    failed |= pe_hook_add(PE_HOOK_SAVE_READ, pe_on_save_read, NULL, PE_PRIORITY_EARLY, mod_id);
    failed |= pe_hook_add(PE_HOOK_NEW_GAME, pe_on_new_game, NULL, PE_PRIORITY_EARLY, mod_id);
    /* One F1 sub-page per category, in category order; each challenge on its category's page. */
    for (int c = 0; c < CH_CATEGORIES; ++c)
        pe_menu_page(g_ch_categories[c].name, g_ch_categories[c].description, 10 * (c + 1));
    for (unsigned i = 0; i < g_ch_def_count; ++i) {
        const PeChallengeDef *d = g_ch_defs[i];
        const char *sub = g_ch_categories[d->category >= 0 && d->category < CH_CATEGORIES ? d->category : 0].name;
        char key[128], legacy[128];
        /* Rows are saved by page and label: carry each pick over from before the categories. */
        snprintf(key, sizeof key, "%s/%s/%s", CH_PAGE, sub, d->label);
        snprintf(legacy, sizeof legacy, "%s/%s", CH_PAGE, d->label);
        pe_menu_alias_setting(key, legacy);
        failed |= d->levels
            ? pe_menu_register_choice(sub, d->label, d->description, &s_on[d->id], d->levels, d->level_count)
            : pe_menu_register_toggle(sub, d->label, d->description, &s_on[d->id], NULL);
    }
    return failed ? -1 : 0;
}
