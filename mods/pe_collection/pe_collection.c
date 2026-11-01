/* PE Enhancements: Collection, the Bestiary and the Wardrobe (psxrecomp build's pe_collection.c;
 * bridge findings pe-skins-bestiary-design-2026-10-19.md). F1 > PE Enhancements > Collection.
 *
 * The Collection is a registry: creatures and costumes come from packs registered through the
 * `bestiary` and `costumes` services (host/pe_services.h). Parasite Eve's own are the first pack
 * (pe_pack_pe1.c); an import from another game or a randomizer registers the same way. This file:
 * the mod, Junk and messages. */
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
    pe_wardrobe_reset();
    pe_profile_save();
    pe_col_say("Collection reset");
}

static int collection_init(void) {
    pe_profile_load();
    pe_service_publish("bestiary", 1, &g_pe_bestiary_api, MOD_ID);
    pe_service_publish("costumes", 1, &g_pe_costumes_api, MOD_ID);
    pe_menu_register_live_int(PE_PAGE_DEBUG_COLLECTION, "Junk", "Testing: Junk for buying Bestiary tiers and costumes.",
                              0, 99999, 100, pe_junk_get, pe_junk_set);
    pe_menu_register_action(PE_PAGE_DEBUG_COLLECTION, "Give 1000 Junk", "Testing: add 1000 Junk (Junk economy on).", pe_junk_give);
    pe_bestiary_register(MOD_ID);
    pe_wardrobe_register(MOD_ID);
    pe_wardrobe_menu_register(MOD_ID);
    pe_bestiary_menu_register(MOD_ID);
    pe_skin_register(MOD_ID);
    pe_pack_pe1_register();
    pe_menu_register_action(PE_PAGE_DEBUG_COLLECTION, "Reset collection", "Forget all Bestiary and Wardrobe unlocks.", pe_reset);
    pe_menu_register_tick(pe_profile_tick);
    return 0;
}

static void collection_shutdown(void) { pe_profile_tick(); }

const PeMod pe_mod_pe_collection = {MOD_ID, "PE Enhancements: collection", PE_ENHANCEMENTS_VERSION, NULL, collection_init, collection_shutdown};

#if PE_NATIVE_STATE_CPU_COMPONENTS
#include "pe_checkpoint_collection.h"
const char*pe_collection_checkpoint_blocker(void){unsigned char b[256];if(!pe_bestiary_menu_checkpoint_walk(0,b,0))return pe_bestiary_menu_checkpoint_blocker();if(!pe_wardrobe_menu_checkpoint_walk(0,b,0))return "Close the Wardrobe screen first";if(!pe_skin_checkpoint_walk(0,b,0))return "Custom skin or animation resources are not checkpoint-owned; restart with native clothing";if(!pe_wardrobe_checkpoint_walk(0,b,0))return "Finish the costume reload; external costume providers are not checkpoint-owned";return "Collection resource identity is not checkpoint-compatible";}
int pe_collection_checkpoint_ready(void){unsigned char b[256];if(!pe_profile_checkpoint_walk(0,b,0))return 0;if(!pe_bestiary_checkpoint_walk(0,b,0))return 0;if(!pe_wardrobe_checkpoint_walk(0,b,0))return 0;if(!pe_skin_checkpoint_walk(0,b,0))return 0;if(!pe_bestiary_menu_checkpoint_walk(0,b,0))return 0;if(!pe_wardrobe_menu_checkpoint_walk(0,b,0))return 0;return 1;}
int pe_collection_checkpoint_walk(const unsigned char*in,unsigned char*out,size_t bytes,int apply){if(bytes!=PE_COLLECTION_STATE_BYTES||(!in&&!out)||!pe_collection_checkpoint_ready())return 0;if(apply&&(!in||!pe_collection_checkpoint_walk(in,0,bytes,0)))return 0;if(out){memset(out,0,bytes);memcpy(out,"PECOL001",8);}else{if(memcmp(in,"PECOL001",8))return 0;for(unsigned i=8;i<16;i++)if(in[i])return 0;}size_t at=16;if(!pe_profile_checkpoint_walk(in?in+at:0,out?out+at:0,apply))return 0;at+=64;if(!pe_bestiary_checkpoint_walk(in?in+at:0,out?out+at:0,apply))return 0;at+=132;if(!pe_wardrobe_checkpoint_walk(in?in+at:0,out?out+at:0,apply))return 0;at+=16;if(!pe_skin_checkpoint_walk(in?in+at:0,out?out+at:0,apply))return 0;at+=24;if(!pe_bestiary_menu_checkpoint_walk(in?in+at:0,out?out+at:0,apply))return 0;at+=0;if(!pe_wardrobe_menu_checkpoint_walk(in?in+at:0,out?out+at:0,apply))return 0;at+=0;if(at>bytes)return 0;if(in)for(;at<bytes;at++)if(in[at])return 0;return 1;}
#endif
