/*
 * Mod Permit Card at Wayne's Workshop: one Mod Permit (item 0x37, from the bag first, else his
 * storage) adds one modification slot to a gun or armor in the bag, up to what the item can hold
 * (its tailCapacity, at most 10) -- vanilla's Torres tune-up without the risk. The new slot starts
 * empty. The Workshop screen (pe_workshop_screen.c) shows and asks; this file holds the rules.
 */
#include <stdio.h>

#include "pe_shops.h"
#include "pe_mod_host.h"
#include "pe_services.h"
#include "pe1/inventory.h"
#include "pe1/inventory_queries.h"
#include "pe1/inventory_runtime_types.h"
#include "pe1/inventory_slots.h"

#define PE_ITEM_MOD_PERMIT  0x37
#define PE_BAG_SLOTS        50
#define PE_STORAGE_SLOTS    100
#define PE_MAX_MOD_SLOTS    10

ItemDataRecord *pe_permit_record(int bag_slot) {
    int value;
    ItemDataRecord *rec;
    if (bag_slot < 0 || bag_slot >= Inv_GetAyaSlotLimit() || bag_slot >= PE_BAG_SLOTS) return NULL;
    value = g_AyaInventoryItems[bag_slot];
    if (value < 0x100 || value >= 0x180) return NULL;
    rec = &g_InventoryRuntime.equipment[value - 0x100];
    return rec->kind >= 1 && rec->kind <= 9 ? rec : NULL;    /* guns 1-8, armor 9 */
}

int pe_permit_gear(int *bag_slots) {
    int n = 0, i;
    for (i = 0; i < PE_BAG_SLOTS; ++i) if (pe_permit_record(i)) bag_slots[n++] = i;
    return n;
}

unsigned pe_permit_count(void) {
    const PeStacksApi *stacks = pe_service_get("stacks", 1);
    unsigned n = 0;
    int i;
    if (stacks && stacks->total)
        return (unsigned)(stacks->total(0, PE_ITEM_MOD_PERMIT) + stacks->total(1, PE_ITEM_MOD_PERMIT));
    for (i = 0; i < PE_BAG_SLOTS; ++i) n += g_AyaInventoryItems[i] == PE_ITEM_MOD_PERMIT;
    for (i = 0; i < PE_STORAGE_SLOTS; ++i) n += g_WayneStorageItems[i] == PE_ITEM_MOD_PERMIT;
    return n;
}

unsigned pe_permit_slot_max(const ItemDataRecord *rec) {
    return rec->tailCapacity < PE_MAX_MOD_SLOTS ? rec->tailCapacity : PE_MAX_MOD_SLOTS;
}

const char *pe_permit_blocked(int bag_slot) {
    const ItemDataRecord *rec = pe_permit_record(bag_slot);
    if (!rec) return "Not a gun or armor";
    if (rec->tailCount >= pe_permit_slot_max(rec)) return "It has all its slots";
    if (!pe_permit_count()) return "No Mod Permit";
    return NULL;
}

static int pe_take_permit(void) {
    const PeStacksApi *stacks = pe_service_get("stacks", 1);
    int i;
    for (i = 0; i < PE_BAG_SLOTS; ++i)
        if (g_AyaInventoryItems[i] == PE_ITEM_MOD_PERMIT) {
            if (stacks && stacks->take) stacks->take(0, i);
            else g_AyaInventoryItems[i] = 0;
            return 1;
        }
    for (i = 0; i < PE_STORAGE_SLOTS; ++i)
        if (g_WayneStorageItems[i] == PE_ITEM_MOD_PERMIT) {
            if (stacks && stacks->take) stacks->take(1, i);
            else g_WayneStorageItems[i] = 0;
            return 1;
        }
    return 0;
}

int pe_permit_add_slot(int bag_slot, char *msg, size_t size) {
    ItemDataRecord *rec = pe_permit_record(bag_slot);
    const char *why = pe_permit_blocked(bag_slot);
    if (why) { snprintf(msg, size, "%s", why); return 0; }
    if (!pe_take_permit()) { snprintf(msg, size, "No Mod Permit"); return 0; }
    rec->tailData[rec->tailCount] = 0;                       /* the new slot starts empty */
    ++rec->tailCount;
    snprintf(msg, size, "Slot added, now %u", (unsigned)rec->tailCount);
    pe_log("permit_slot", "item=0x%02X slots=%u permits_left=%u", rec->itemId, (unsigned)rec->tailCount, pe_permit_count());
    return 1;
}
