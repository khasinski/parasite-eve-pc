#ifndef PE_API_NATIVE_H
#define PE_API_NATIVE_H
/* Host-private native boundary. The API modules have no link dependency on retail globals.
 * Native boot supplies this once; independent host fixtures leave it unavailable or bind their
 * isolated state. This is not an API for providers to replace the game's inventory. */
#include "pe1/inventory_runtime_types.h"
#include "pe1/inventory.h"
typedef struct {
    InventoryRuntime *inventory;
    int (*loaded)(void), (*in_battle)(void), (*bag_limit)(void);
    ItemDataRecord *(*base_item)(unsigned);
    int *selection, *from_storage, *override_limit;
    short **override_items;
} PeApiNative;
int pe_api_native_bind(const PeApiNative *binding);
const PeApiNative *pe_api_native(void);
int pe_api_native_loaded(void);
int pe_api_native_in_battle(void);
#define PE_API_INV (*pe_api_native()->inventory)
#endif
