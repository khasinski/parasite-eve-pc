/* PE Enhancements: economy, ported from the psxrecomp build. F1 > PE Enhancements > Economy.
 * One file per feature; each registers its own rows, hooks and services. */
#include "pe_mod_host.h"
#include "pe_pages.h"
#include "pe_requisition.h"

#define MOD_ID "pe_economy"

void pe_loot_register(const char *mod_id);    /* pe_loot.c: Junk, battle rewards, rare drops */
void pe_catalog_register(const char *mod_id); /* pe_loot_catalog.c: Mod Catalog, Dismantle, Rebuild */
void pe_catalog_menu_register(const char *mod_id); /* pe_catalog_menu.c: the Catalog in Aya's menu */
void pe_discard_dismantle_register(const char *mod_id); /* pe_discard_dismantle.c: Discard on gear dismantles */

void pe_wayne_refund_register(const char *mod_id); /* failed vanilla crafts */

void pe_armor_effect_display_register(const char *mod_id);
void pe_junk_counter_register(const char *mod_id);
void pe_smoke_register(const char *mod_id);
void pe_wayne_menu_register(const char *mod_id); /* pe_wayne_menu.c: the Workshop in Wayne's menu */
void pe_workshop_screen_register(const char *mod_id); /* pe_workshop_screen.c: the Workshop as a menu screen */
void pe_costume_rules_register(const char *mod_id); /* pe_costume_rules.c: the Wardrobe's unlock rules */
void pe_field_wayne_register(const char *mod_id); /* pe_field_wayne.c: Warner's services */
void pe_smoke_shutdown(void);

static int economy_init(void) {
    pe_loot_register(MOD_ID);
    pe_armor_effect_display_register(MOD_ID);
    pe_junk_counter_register(MOD_ID);
    pe_catalog_register(MOD_ID);
    pe_requisition_register(MOD_ID);
    pe_requisition_menu_register(MOD_ID);
    pe_catalog_menu_register(MOD_ID);
    pe_discard_dismantle_register(MOD_ID);
    pe_wayne_refund_register(MOD_ID);
    pe_workshop_screen_register(MOD_ID);
    pe_costume_rules_register(MOD_ID);
    pe_smoke_register(MOD_ID);
    pe_wayne_menu_register(MOD_ID);
    pe_field_wayne_register(MOD_ID);
    return 0;
}

const PeMod pe_mod_pe_economy = {MOD_ID, "PE Enhancements: economy", PE_ENHANCEMENTS_VERSION, NULL, economy_init, pe_smoke_shutdown};
