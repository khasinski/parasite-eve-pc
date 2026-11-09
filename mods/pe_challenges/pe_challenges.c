/* Challenges: optional rules that make a playthrough harder. F1 > Challenges.
 *
 * This file only lists the challenges and starts the mod's parts; each lives in a file of its own
 * (pe_challenges.h has the map). Every rule is off until chosen; while every challenge is off the
 * mod writes nothing to the game.
 */
#include <stddef.h>

#include "pe_challenges.h"
#include "pe_mod_host.h"
#include "pe_mod_menu.h"

#define MOD_ID "pe_challenges"

/* The categories, in F1 and Records order. */
const PeChallengeCategory g_ch_categories[CH_CATEGORIES] = {
    [CH_CAT_RESTRICTIONS] = {"Restrictions", "What Aya may use in battle: PE, items, escape, gear, guns."},
    [CH_CAT_ENEMIES]      = {"Enemies", "How hard the enemies are: their HP and how fast they act."},
    [CH_CAT_AYA]          = {"Aya", "How fragile Aya is and what her powers and battles give her."},
    [CH_CAT_RUN_RULES]    = {"Run rules", "How the run itself works: Hardcore permadeath."},
};

/* Row order within each category. */
const PeChallengeDef *const g_ch_defs[] = {
    &g_ch_no_pe,
    &g_ch_no_items,
    &g_ch_no_escape,
    &g_ch_locked_loadout,
    &g_ch_club_only,
    &g_ch_tough,
    &g_ch_fast_enemies,
    &g_ch_glass,
    &g_ch_one_hit,
    &g_ch_blood_price,
    &g_ch_no_exp,
    &g_ch_hardcore,
};
const unsigned g_ch_def_count = sizeof g_ch_defs / sizeof g_ch_defs[0];
_Static_assert(sizeof g_ch_defs / sizeof g_ch_defs[0] == CH_COUNT, "every challenge id needs a definition");

const PeChallengeDef *pe_ch_def(int id) {
    for (unsigned i = 0; i < g_ch_def_count; ++i)
        if (g_ch_defs[i]->id == id) return g_ch_defs[i];
    return NULL;
}

static int challenges_init(void) {
    int failed = 0;
    pe_menu_begin(CH_PAGE);
    pe_menu_top_page(CH_PAGE, "Optional rules for a harder run. Saved with each game.", 45);

    /* Event names first (anything may announce), then the run: its rows lead the page and every
     * rule reads its state. */
    pe_ch_events_register();
    if (pe_ch_run_register(MOD_ID) != 0) { pe_menu_begin(0); return -1; }
    for (unsigned i = 0; i < g_ch_def_count; ++i)
        if (g_ch_defs[i]->register_rules) g_ch_defs[i]->register_rules(MOD_ID);
    pe_ch_tabs_register(MOD_ID);
    pe_ch_assist_register(MOD_ID);
    pe_ch_runlog_register(MOD_ID);
    pe_ch_milestones_register(MOD_ID);
    pe_ch_records_register(MOD_ID);
    pe_ch_notice_register(MOD_ID);
    pe_ch_screen_register(MOD_ID);
    pe_ch_card_marks_register(MOD_ID);
    failed |= pe_ch_summary_register();
    failed |= pe_ch_bundles_register();

    pe_menu_begin(0);
    return failed ? -1 : 0;
}

const PeMod pe_mod_pe_challenges = {MOD_ID, "Challenges", "0.3", NULL, challenges_init, NULL};
