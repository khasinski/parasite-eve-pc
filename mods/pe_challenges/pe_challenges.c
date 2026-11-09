/* Challenges: optional rules that make a playthrough harder. F1 > Challenges.
 *
 * This file only starts the mod's features; each lives in a file of its own (pe_challenges.h has
 * the map). Every rule is off until chosen; while every challenge is off the mod writes nothing to
 * the game.
 */
#include "pe_challenges.h"
#include "pe_mod_host.h"
#include "pe_mod_menu.h"

#define MOD_ID "pe_challenges"

static int challenges_init(void) {
    int failed = 0;
    pe_menu_begin(CH_PAGE);
    pe_menu_top_page(CH_PAGE, "Optional rules for a harder run. Saved with each game.", 45);

    /* The run first: its rows lead the page and the rules read its state. */
    if (pe_ch_run_register(MOD_ID) != 0) { pe_menu_begin(0); return -1; }
    pe_ch_tabs_register(MOD_ID);
    pe_ch_damage_register(MOD_ID);
    pe_ch_assist_register(MOD_ID);
    pe_ch_runlog_register(MOD_ID);
    failed |= pe_ch_summary_register();
    failed |= pe_ch_bundles_register();

    pe_menu_begin(0);
    return failed ? -1 : 0;
}

const PeMod pe_mod_pe_challenges = {MOD_ID, "Challenges", "0.1", NULL, challenges_init, NULL};
