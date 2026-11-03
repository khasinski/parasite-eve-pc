/* PE Enhancements: economy, ported from the psxrecomp build. F1 > PE Enhancements > Economy.
 * The Junk wallet (pe_loot.c: the counter, Difficulty, prices, the "junk" and "wallet" services),
 * the Junk counter display, Costume unlock rules and Smoke Grenades. The battle rewards
 * (pe_rewards), the Mod Catalog (pe_catalog) and the shops (pe_shops) were split off in November
 * 2026 (bridge findings D9, D10). One file per feature; each registers its own rows, hooks and
 * services. */
#include "pe_mod_host.h"
#include "pe_pages.h"

#define MOD_ID "pe_economy"

void pe_loot_register(const char *mod_id);    /* pe_loot.c: the Junk wallet */
void pe_junk_counter_register(const char *mod_id);
void pe_smoke_register(const char *mod_id);
void pe_costume_rules_register(const char *mod_id); /* pe_costume_rules.c: the Wardrobe's unlock rules */
void pe_smoke_shutdown(void);

static int economy_init(void) {
    pe_loot_register(MOD_ID);
    pe_junk_counter_register(MOD_ID);
    pe_costume_rules_register(MOD_ID);
    pe_smoke_register(MOD_ID);
    return 0;
}

const PeMod pe_mod_pe_economy = {MOD_ID, "PE Enhancements: economy", PE_ENHANCEMENTS_VERSION, NULL, economy_init, pe_smoke_shutdown};
