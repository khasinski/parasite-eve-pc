/* The Junk wallet as the Mod Catalog sees it: the "junk" service (pe_economy's), asked each time.
 * Without it the Catalog behaves as with Junk economy off: paid actions are refused, never free. */
#include "pe_catalog_mod.h"
#include "pe_mod_host.h"

static const PeJunkApi *pe_catw(void) { return (const PeJunkApi *)pe_service_get("junk", 1); }

int pe_catw_enabled(void) { const PeJunkApi *j = pe_catw(); return j && j->enabled(); }
uint32_t pe_catw_junk(void) { const PeJunkApi *j = pe_catw(); return j ? j->junk() : 0u; }
int pe_catw_spend_junk(uint32_t n) { const PeJunkApi *j = pe_catw(); return j && j->spend(n); }
void pe_catw_give_junk(uint32_t n) { const PeJunkApi *j = pe_catw(); if (j) j->give(n); }
int pe_catw_armor_has(int effect) { const PeJunkApi *j = pe_catw(); return j && j->armor_has(effect); }
uint32_t pe_catw_price(uint32_t base) { const PeJunkApi *j = pe_catw(); return j ? j->price(base) : base; }
