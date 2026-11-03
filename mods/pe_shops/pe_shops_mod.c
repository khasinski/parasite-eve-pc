/* PE Enhancements: shops. Requisition (goods and economy effects for Junk, F1 and Wayne's
 * counter), Wayne's Workshop in his menu and as real screens, Warner's services on Day 4, and
 * refunds of failed vanilla crafts. Until November 2026 part of the economy mod: its save record
 * keeps the name "pe_economy.shop" and its F1 rows their pages and labels, so saves and settings
 * carry over. Needs the economy mod (the Junk wallet). */
#include "pe_mod_host.h"
#include "pe_pages.h"
#include "pe_requisition.h"
#include "pe_shops.h"

#define MOD_ID "pe_shops"

void pe_wayne_refund_register(const char *mod_id);     /* pe_wayne_refund.c: failed vanilla crafts */
void pe_workshop_screen_register(const char *mod_id);  /* pe_workshop_screen.c: the Workshop as a menu screen */
void pe_wayne_menu_register(const char *mod_id);       /* pe_wayne_menu.c: the Workshop in Wayne's menu */
void pe_field_wayne_register(const char *mod_id);      /* pe_field_wayne.c: Warner's services */

static int shops_init(void) {
    pe_requisition_register(MOD_ID);
    pe_requisition_menu_register(MOD_ID);
    pe_wayne_refund_register(MOD_ID);
    pe_workshop_screen_register(MOD_ID);
    pe_wayne_menu_register(MOD_ID);
    pe_field_wayne_register(MOD_ID);
    return 0;
}

static const char *const s_requires[] = { "pe_economy", NULL };   /* the Junk wallet */
const PeMod pe_mod_pe_shops = {MOD_ID, "PE Enhancements: shops", PE_ENHANCEMENTS_VERSION, s_requires, shops_init, NULL};
