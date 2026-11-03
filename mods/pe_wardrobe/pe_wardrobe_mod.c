/* PE Enhancements: Wardrobe, Aya's costumes (bridge findings D8 §12, D9). F1 > PE Enhancements >
 * Wardrobe and a screen in Aya's menu. Until November 2026 part of the Collection (pe_collection);
 * its profile is imported from there once (pe_wardrobe_profile.c).
 *
 * The Wardrobe is a registry: costumes come from packs registered through the `costumes` service
 * (host/pe_services.h). Parasite Eve's own are the first pack (pe_pack_pe1_costumes.c). It needs no
 * other mod: buying uses Junk through the `junk` service when a mod publishes it (pe_economy). This
 * file: the mod, Junk and messages. */
#include <stdio.h>
#include <string.h>

#include "pe_game.h"
#include "pe_mod_host.h"
#include "pe_mod_menu.h"
#include "pe_pages.h"
#include "pe_wardrobe.h"

#define MOD_ID "pe_wardrobe"

static char s_said[96];

/* ---- Junk ---- */
static const PeJunkApi *pe_junk(void) { return (const PeJunkApi *)pe_service_get("junk", 1); }

int pe_ward_junk_ready(char *why, unsigned size) {
    const PeJunkApi *j = pe_junk();
    if (!pe_game_loaded()) { snprintf(why, size, "Load a game first (Junk comes from it)"); return 0; }
    if (!j || !j->enabled()) { snprintf(why, size, "Turn on Junk economy to buy"); return 0; }
    return 1;
}
unsigned pe_ward_junk(void) { const PeJunkApi *j = pe_junk(); return j && pe_game_loaded() ? (unsigned)j->junk() : 0u; }
int pe_ward_spend(unsigned n) { const PeJunkApi *j = pe_junk(); return j && j->enabled() && j->spend(n); }

void pe_ward_say(const char *text) {
    snprintf(s_said, sizeof s_said, "%s", text);
    pe_toast(text);
}
const char *pe_ward_last_said(void) { return s_said[0] ? s_said : "-"; }

/* ---- the mod ---- */
static void pe_reset(void) {
    pe_ward_profile_clear();
    pe_wardrobe_reset();
    pe_ward_profile_save();
    pe_ward_say("Wardrobe reset");
}

static int wardrobe_init(void) {
    pe_ward_profile_load();
    pe_service_publish("costumes", 1, &g_pe_costumes_api, MOD_ID);
    pe_wardrobe_register(MOD_ID);
    pe_wardrobe_menu_register(MOD_ID);
    pe_skin_register(MOD_ID);
    pe_pack_pe1_costumes_register();
    pe_packs_scan();
    pe_packs_register();
    pe_packs_page_register();
    pe_menu_register_action(PE_PAGE_DEBUG_COLLECTION, "Reset wardrobe", "Forget all costume unlocks and the costume worn.", pe_reset);
    pe_menu_register_tick(pe_ward_profile_tick);
    return 0;
}

static void wardrobe_shutdown(void) { pe_ward_profile_tick(); }

const PeMod pe_mod_pe_wardrobe = {MOD_ID, "PE Enhancements: wardrobe", PE_ENHANCEMENTS_VERSION, NULL, wardrobe_init, wardrobe_shutdown};
