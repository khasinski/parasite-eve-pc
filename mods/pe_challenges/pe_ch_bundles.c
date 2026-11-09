/* Challenges: F1 > Challenges > Bundles. A bundle only turns challenges on (it never lowers one,
 * so it can't mark a run assisted); Clear all turns every one off and is press-twice. */
#include <stdio.h>
#include <SDL3/SDL.h>

#include "pe_challenges.h"
#include "pe_mod_host.h"
#include "pe_mod_menu.h"

#define CH_CONFIRM_MS 5000

static uint64_t s_clear_armed_ms;   /* Clear all's first press */

/* level: for level rows in mask (on / off rows just turn on). */
static void pe_bundle(unsigned mask, int level, const char *name) {
    char msg[96];
    pe_ch_rows_turn_on(mask, level);
    snprintf(msg, sizeof msg, "%s challenges on", name);
    pe_toast(msg);
}

static void pe_bundle_purist(void) {
    pe_bundle(CH_BIT(CH_NO_PE) | CH_BIT(CH_NO_ITEMS) | CH_BIT(CH_NO_ESCAPE), 0, "Purist");
}

/* Hard, but still a normal run: Hardcore, One-hit KO and No EXP are picked on their own. */
static void pe_bundle_nightmare(void) {
    pe_bundle(CH_BIT(CH_NO_PE) | CH_BIT(CH_NO_ITEMS) | CH_BIT(CH_NO_ESCAPE) | CH_BIT(CH_LOCKED_LOADOUT) |
              CH_BIT(CH_GLASS) | CH_BIT(CH_TOUGH), 2, "Nightmare");
}

static void pe_clear_all(void) {
    const uint64_t now = SDL_GetTicks();
    if (pe_ch_run_locked()) {
        pe_toast("Locked run: challenges can't be cleared");
        return;
    }
    if (!s_clear_armed_ms || now - s_clear_armed_ms > CH_CONFIRM_MS) {
        s_clear_armed_ms = now ? now : 1;
        pe_toast(pe_ch_underway() && g_ch_run.active ? "Clear all: press again (the run will be marked assisted)"
                                                    : "Clear all: press again");
        return;
    }
    s_clear_armed_ms = 0;
    pe_ch_rows_clear();
    pe_toast("Every challenge off");
}

int pe_ch_bundles_register(void) {
    int failed = 0;
    pe_menu_page(CH_PAGE_BUNDLES, "One-press sets of challenges. They only turn challenges on.", 10);
    failed |= pe_menu_register_action(CH_PAGE_BUNDLES, "Purist",
        "Turns on No Parasite Energy, No items and No escape.", pe_bundle_purist);
    failed |= pe_menu_register_action(CH_PAGE_BUNDLES, "Nightmare",
        "Purist plus Locked loadout, Glass cannon and Tough crowd at 2x HP.", pe_bundle_nightmare);
    failed |= pe_menu_register_action(CH_PAGE_BUNDLES, "Clear all",
        "Press twice. Every challenge off; mid-run the run is marked assisted. Not in a locked run.",
        pe_clear_all);
    return failed ? -1 : 0;
}
