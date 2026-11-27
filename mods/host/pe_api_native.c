#include "pe_api_native.h"
#include "pe_registry.h"
static PeApiNative s_native;
int pe_api_native_bind(const PeApiNative *b) {
    if (pe_reg_registration_access() != PE_REG_OK || !b || !b->inventory || !b->loaded || !b->in_battle ||
        !b->bag_limit || !b->base_item || !b->selection || !b->from_storage || !b->override_limit ||
        !b->override_items)
        return 0;
    if (s_native.inventory) return 0;
    s_native = *b;
    return 1;
}
const PeApiNative *pe_api_native(void) {
    return &s_native;
}
int pe_api_native_loaded(void) {
    return s_native.loaded && s_native.inventory && s_native.loaded();
}
int pe_api_native_in_battle(void) {
    return !s_native.in_battle || s_native.in_battle();
}
