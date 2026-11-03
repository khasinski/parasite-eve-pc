/* Costume unlock rules: the Economy's say over the Wardrobe (bridge findings D8 §12, D6 §15.5).
 * F1 > Economy > Costume unlock rules, on by default: costumes are bought with Junk (Wayne's
 * Workshop, F1) and only owned or free ones can be worn, as before the Wardrobe became its own mod.
 * Off, or with the Economy off, the Wardrobe offers every installed costume without prices. The
 * switch never buys, unlocks or resets anything; costumes already bought stay owned. Published as
 * `costume_rules` v1 (host/pe_services.h). */
#include "pe_mod_host.h"
#include "pe_mod_menu.h"
#include "pe_pages.h"
#include "pe_services.h"

static int s_rules = 1;

static int pe_rules_active(void) { return s_rules; }

static const PeCostumeRulesApi s_api = { pe_rules_active };

void pe_costume_rules_register(const char *mod_id) {
    pe_menu_register_toggle(PE_PAGE_ECONOMY, "Costume unlock rules",
        "On: Wardrobe costumes are bought with Junk before they can be worn. Off: every costume can be worn; "
        "costumes already bought stay yours.", &s_rules, NULL);
    pe_service_publish("costume_rules", 1, &s_api, mod_id);
}
