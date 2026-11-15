#ifndef PE1_INVENTORY_QUERIES_H
#define PE1_INVENTORY_QUERIES_H

/* Inventory list helpers (item/active_list.c). */

/* Number of item slots Aya currently has. */
int Inv_GetAyaSlotLimit(void);
/* Port addition: slots past Aya's bag that still hold an item (a mod gave one on a full bag: 11/10).
 * Counted in Inv_GetAyaSlotLimit, not in the bag size the item menu header shows; mods/host shrinks it. */
extern int g_InvOverflowSlotCount;
/* Selects the active item list; nonzero useOverride picks the override list. */
void Inv_SelectActiveList(int useOverride);
short Inv_GetActiveListItem(int index);

#endif /* PE1_INVENTORY_QUERIES_H */
