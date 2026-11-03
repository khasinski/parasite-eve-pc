#ifndef PE_ECONOMY_H
#define PE_ECONOMY_H

/* The economy mod's own calls, shared by its files. Other mods use the services in
 * host/pe_services.h instead. */

#include <stddef.h>
#include <stdint.h>
#include "pe_services.h"   /* PE_FX_*, PeJunkApi, PeCatalogApi, PeKeysApi, PeWardrobeApi */

/* Direct calls inside the economy mod (the Junk wallet). The Mod Catalog is its own mod since
 * November 2026 (mods/pe_catalog); it reaches the wallet through the "junk" service. */
int pe_loot_enabled(void);
uint32_t pe_loot_junk(void);
int pe_loot_spend_junk(uint32_t n);
void pe_loot_give_junk(uint32_t n);
int pe_loot_armor_has(int effect);
uint32_t pe_loot_price(uint32_t base);
int pe_loot_wayne_waiting(void);
int pe_loot_handin_blocked(void);

#endif
