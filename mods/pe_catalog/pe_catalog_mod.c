/* PE Enhancements: Mod Catalog. Effects to install in guns' and armor's mod slots, Dismantle,
 * Rebuild and Tool salvage, the Catalog screen in Aya's menu and the F1 pages. Until November 2026
 * part of the economy mod: its save record keeps the name "pe_economy.catalog" and its F1 rows
 * their pages and labels, so saves and settings carry over. Junk comes from the wallet's "junk"
 * service (pe_catalog_wallet.c); it publishes "catalog" (unlocks, for the battle rewards and
 * Requisition) and "rebuild" (the Rebuild list, for Wayne's Workshop). */
#include "pe_catalog_mod.h"
#include "pe_mod_host.h"
#include "pe_pages.h"

#define MOD_ID "pe_catalog"

static int catalog_init(void) {
    pe_catalog_register(MOD_ID);
    pe_catalog_menu_register(MOD_ID);
    pe_discard_dismantle_register(MOD_ID);
    pe_armor_effect_display_register(MOD_ID);
    return 0;
}

const PeMod pe_mod_pe_catalog = {MOD_ID, "PE Enhancements: Mod Catalog", PE_ENHANCEMENTS_VERSION, NULL, catalog_init, NULL};
