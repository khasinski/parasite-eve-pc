#ifndef PE_SHOPS_H
#define PE_SHOPS_H

/* The shops mod's calls, shared by its files (mods/pe_shops): Requisition, Wayne's menu and
 * Workshop, Warner, refunds of failed vanilla crafts. Until November 2026 part of the economy mod;
 * its save record keeps its name, "pe_economy.shop". It needs the economy mod (the Junk wallet)
 * and reaches it only through the "junk" service; the Mod Catalog through "catalog" and
 * "rebuild", the Wardrobe through "wardrobe", each while its mod runs. */

#include <stddef.h>
#include <stdint.h>
#include "pe_services.h"

/* The Junk wallet, through the "junk" service (pe_shops_wallet.c). */
int pe_shopw_enabled(void);
uint32_t pe_shopw_junk(void);
int pe_shopw_spend_junk(uint32_t n);
void pe_shopw_give_junk(uint32_t n);
uint32_t pe_shopw_price(uint32_t base);
int pe_shopw_wayne_waiting(void);
int pe_shopw_handin_blocked(void);

/* Mod Permit Card: one more slot on a gun or armor in the bag (pe_permit.c). */
struct ItemDataRecord;
struct ItemDataRecord *pe_permit_record(int bag_slot);      /* the gun or armor there, else NULL */
int pe_permit_gear(int *bag_slots);                         /* bag slots holding guns and armor; how many */
unsigned pe_permit_count(void);                             /* Mod Permits in the bag and Wayne's storage */
unsigned pe_permit_slot_max(const struct ItemDataRecord *rec);
const char *pe_permit_blocked(int bag_slot);                /* why no slot can be added now, or NULL */
int pe_permit_add_slot(int bag_slot, char *msg, size_t size);   /* uses one permit; 1 if added */

#endif
