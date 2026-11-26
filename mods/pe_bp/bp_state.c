/* BP Overhaul's live state for test tools (pe_mod_state.h): unspent BP, the ranks bought, the
 * run data and the EX Game carryover. Reads only. */
#include <stddef.h>
#include "pe_bp_internal.h"
#include "pe_mod_state.h"

static void bp_state(PeStateWriter *w, void *user) {
    const BpRecord *r = bp_store_record();
    int i;
    (void)user;
    pe_state_bool(w, "readable", bp_store_readable());
    pe_state_int(w, "bp", bp_game_bp());
    pe_state_int(w, "features", bp_api_features());
    if (!r) return;
    pe_state_begin_object(w, "defense");
    for (i = 0; i < BP_DEFENSE_TRACKS; ++i) pe_state_int(w, bp_defense_def(i)->id, r->defense[i]);
    pe_state_end(w);
    pe_state_begin_object(w, "mastery");
    for (i = 0; i < BP_MASTERY_LINES; ++i) pe_state_int(w, bp_line_def(i)->id, r->mastery[i]);
    pe_state_end(w);
    pe_state_text(w, "fortitude_mode", r->fortitude_mode == BP_FORT_SAVE_FOR_HP1 ? "save_for_hp1" : "both");
    pe_state_int(w, "full_resets", r->full_resets);
    pe_state_int(w, "active_steps", r->active_steps);
    pe_state_int(w, "active_points", r->active_points);
    pe_state_int(w, "carried_pool", r->carried_pool);
    pe_state_begin_object(w, "carried");
    for (i = 0; i < BP_DEFENSE_TRACKS; ++i)
        if (r->carried_defense[i]) pe_state_int(w, bp_defense_def(i)->id, r->carried_defense[i]);
    for (i = 0; i < BP_MASTERY_LINES; ++i)
        if (r->carried_mastery[i]) pe_state_int(w, bp_line_def(i)->id, r->carried_mastery[i]);
    pe_state_end(w);
}

void bp_state_register(void) {
    (void)pe_mod_state_provider("bp", bp_state, NULL);
}
