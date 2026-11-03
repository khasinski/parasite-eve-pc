/* The Junk wallet as the shops see it: the "junk" service (pe_economy's), asked each time. The
 * shops mod needs the economy mod, so the wallet is there whenever a shop runs; these still
 * refuse rather than give for free if it ever isn't. */
#include "pe_mod_host.h"
#include "pe_shops.h"

static const PeJunkApi *pe_shopw(void) { return (const PeJunkApi *)pe_service_get("junk", 1); }

int pe_shopw_enabled(void) { const PeJunkApi *j = pe_shopw(); return j && j->enabled(); }
uint32_t pe_shopw_junk(void) { const PeJunkApi *j = pe_shopw(); return j ? j->junk() : 0u; }
int pe_shopw_spend_junk(uint32_t n) { const PeJunkApi *j = pe_shopw(); return j && j->spend(n); }
void pe_shopw_give_junk(uint32_t n) { const PeJunkApi *j = pe_shopw(); if (j) j->give(n); }
uint32_t pe_shopw_price(uint32_t base) { const PeJunkApi *j = pe_shopw(); return j ? j->price(base) : base; }
int pe_shopw_wayne_waiting(void) { const PeJunkApi *j = pe_shopw(); return j && j->wayne_waiting(); }
int pe_shopw_handin_blocked(void) { const PeJunkApi *j = pe_shopw(); return j && j->handin_blocked(); }
