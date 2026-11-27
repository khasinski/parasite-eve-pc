/* Challenge: No Parasite Energy. The PE battle tab goes (pe_ch_tabs.c), and every power is dimmed,
 * in battle and in the field (PE_HOOK_POWER_AVAILABLE: the game refuses it with its error sound). */
#include "pe_challenges.h"
#include "pe_game.h"
#include "pe_mod_host.h"

static int pe_no_pe_power(void *ctx, void *user) {
    PeHookPowerAvailable *p = ctx;
    (void)user;
    if (pe_ch_on(CH_NO_PE) && pe_save_loaded() && p && p->enabled) *p->enabled = 0;
    return 0;
}

static void pe_no_pe_register(const char *mod_id) {
    pe_hook_add(PE_HOOK_POWER_AVAILABLE, pe_no_pe_power, NULL, PE_PRIORITY_LATE, mod_id);
}

const PeChallengeDef g_ch_no_pe = {
    CH_NO_PE, "No Parasite Energy", "No PE",
    "No PE tab in battle, and every power is dimmed in the field too.",
    NULL, 0, CH_TAB_PE, 0, pe_no_pe_register, CH_CAT_RESTRICTIONS
};
