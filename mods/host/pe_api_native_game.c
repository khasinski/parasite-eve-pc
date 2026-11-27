/* The only extension-API unit linked against native game state. */
#include "pe_api_native.h"
#include "pe_game.h"
#include "pe1/inventory_state.h"
#include "pe_hooks.h"
void pe_mod_bind_native(void) {
    const PeApiNative b = {&g_InventoryRuntime,
                           pe_game_loaded,
                           pe_in_battle,
                           Inv_GetAyaSlotLimit,
                           pe_base_item,
                           g_InvSavedSelectionIndex,
                           g_InvSavedSelectionFromStorage,
                           &g_InvOverrideSlotLimit,
                           &g_InvActiveListOverride};
    (void)pe_api_native_bind(&b);
}
