/* PE Enhancements: Collection, the Bestiary (psxrecomp build's pe_collection.c; bridge findings
 * pe-skins-bestiary-design-2026-10-19.md). F1 > PE Enhancements > Collection. The Wardrobe was part
 * of it until November 2026 and is its own mod now (mods/pe_wardrobe; findings D8 §12, D9); the
 * mod id stays pe_collection, so players' settings and profile keep their names.
 *
 * The Collection is a registry: creatures come from packs registered through the `bestiary`
 * service (host/pe_services.h). Parasite Eve's own are the first pack (pe_pack_pe1.c); an import
 * from another game or a randomizer registers the same way. This file: the mod, Junk and
 * messages. */
#include <stdio.h>
#include <string.h>

#include "pe_collection.h"
#include "pe_game.h"
#include "pe_mod_host.h"
#include "pe_mod_menu.h"
#include "pe_pages.h"

#define MOD_ID "pe_collection"

static char s_said[96];

/* ---- Junk ---- */
static const PeJunkApi *pe_junk(void) { return (const PeJunkApi *)pe_service_get("junk", 1); }

int pe_col_junk_ready(char *why, unsigned size) {
    const PeJunkApi *j = pe_junk();
    if (!pe_game_loaded()) { snprintf(why, size, "Load a game first (Junk comes from it)"); return 0; }
    if (!j || !j->enabled()) { snprintf(why, size, "Turn on Junk economy to buy"); return 0; }
    return 1;
}
unsigned pe_col_junk(void) { const PeJunkApi *j = pe_junk(); return j && pe_game_loaded() ? (unsigned)j->junk() : 0u; }
int pe_col_spend(unsigned n) { const PeJunkApi *j = pe_junk(); return j && j->enabled() && j->spend(n); }

void pe_col_say(const char *text) {
    snprintf(s_said, sizeof s_said, "%s", text);
    pe_toast(text);
}
const char *pe_col_last_said(void) { return s_said[0] ? s_said : "-"; }

/* ---- testing Junk, beside the Bestiary's and the Wardrobe's testing rows ---- */
static int pe_junk_get(void) { return (int)pe_col_junk(); }
static int pe_junk_set(int n) {
    const PeJunkApi *j = pe_junk();
    const unsigned have = pe_col_junk();
    if (!j || !pe_game_loaded() || n < 0) return 0;
    if ((unsigned)n > have) j->give((unsigned)n - have);
    else if ((unsigned)n < have) (void)j->spend(have - (unsigned)n);
    return 1;
}
static void pe_junk_give(void) {
    char msg[64];
    if (!pe_col_junk_ready(msg, sizeof msg)) { pe_col_say(msg); return; }
    pe_junk()->give(1000);
    snprintf(msg, sizeof msg, "Junk %u", pe_col_junk());
    pe_col_say(msg);
}

/* ---- the mod ---- */
static void pe_reset(void) {
    pe_profile_clear();
    pe_bestiary_reset();
    pe_profile_save();
    pe_col_say("Collection reset");
}

static int collection_init(void) {
    pe_profile_load();
    pe_service_publish("bestiary", 1, &g_pe_bestiary_api, MOD_ID);
    pe_menu_register_live_int(PE_PAGE_DEBUG_COLLECTION, "Junk", "Testing: Junk for buying Bestiary tiers.",
                              0, 99999, 100, pe_junk_get, pe_junk_set);
    pe_menu_register_action(PE_PAGE_DEBUG_COLLECTION, "Give 1000 Junk", "Testing: add 1000 Junk (Junk economy on).", pe_junk_give);
    pe_bestiary_register(MOD_ID);
    pe_bestiary_menu_register(MOD_ID);
    pe_pack_pe1_register();
    pe_menu_register_action(PE_PAGE_DEBUG_COLLECTION, "Reset collection", "Forget all Bestiary unlocks (the Wardrobe has its own reset).", pe_reset);
    pe_menu_register_tick(pe_profile_tick);
    return 0;
}

static void collection_shutdown(void) { pe_profile_tick(); }

const PeMod pe_mod_pe_collection = {MOD_ID, "PE Enhancements: collection", PE_ENHANCEMENTS_VERSION, NULL, collection_init, collection_shutdown};
