/* The challenge run's live state for test tools (pe_mod_state.h): what is on, at which level,
 * whether the run is assisted, locked or underway, and its counters. Reads only. */
#include <stddef.h>
#include "pe_challenges.h"
#include "pe_mod_state.h"

static void pe_ch_state(PeStateWriter *w, void *user) {
    unsigned i;
    char names[256];
    (void)user;
    pe_ch_names(g_ch_run.active, names, sizeof names);
    pe_state_text(w, "active_names", names);
    pe_state_begin_list(w, "active");
    for (i = 0; i < g_ch_def_count; ++i) {
        const PeChallengeDef *def = g_ch_defs[i];
        if (!pe_ch_on(def->id)) continue;
        pe_state_begin_object(w, NULL);
        pe_state_int(w, "id", def->id);
        pe_state_text(w, "name", def->short_name);
        pe_state_int(w, "level", pe_ch_level(def->id));
        pe_state_end(w);
    }
    pe_state_end(w);
    pe_state_int(w, "active_mask", g_ch_run.active);
    pe_state_int(w, "ever_mask", g_ch_run.ever);
    pe_state_int(w, "dropped_mask", g_ch_run.dropped);
    pe_state_bool(w, "assisted", pe_ch_assisted());
    pe_state_bool(w, "cheats", !!(g_ch_run.flags & CH_FLAG_CHEATS));
    pe_state_bool(w, "underway", pe_ch_underway());
    pe_state_bool(w, "locked", pe_ch_run_locked());
    pe_state_int(w, "run_id", g_ch_run.run_id);
    pe_state_int(w, "battles", g_ch_run.battles);
    pe_state_int(w, "seconds", g_ch_run.seconds);
    pe_state_int(w, "rows_mask", pe_ch_rows_mask());
}

void pe_ch_state_register(void) {
    (void)pe_mod_state_provider("run", pe_ch_state, NULL);
}
