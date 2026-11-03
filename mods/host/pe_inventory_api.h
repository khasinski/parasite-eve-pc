#ifndef PE_INVENTORY_API_H
#define PE_INVENTORY_API_H
#include "pe_content_api.h"
enum {
    PE_INSTANCE_ACTIVE = 1,
    PE_INSTANCE_UNAVAILABLE = 2,
    PE_INSTANCE_UNCERTAIN = 3,
    PE_INSTANCE_ORPHANED = 4
};
typedef struct {
    uint32_t id;
    char key[48];
    uint16_t revision;
    uint8_t pool, state;
} PeInventoryInstance;
/* Inventory actions are game-thread-only, only in a loaded game outside battle/save dispatch.
 * Native-compatible equipment only in v1; unsupported consumable identity is refused.
 * A successful grant occupies one native bag slot + pool record and a persistent ledger identity.
 */
PeRegResult pe_inventory_adopt(int native_value, uint32_t *instance);
PeRegResult pe_inventory_grant(const char *content, uint32_t *instance);
PeRegResult pe_inventory_inspect(uint32_t instance, PeInventoryInstance *out);
PeRegResult pe_inventory_destroy(uint32_t instance);
PeRegResult pe_equipment_effect_apply(uint32_t instance, const char *effect, unsigned slot);
PeRegResult pe_equipment_effect_remove(uint32_t instance, unsigned slot);
/* Host integration, not provider calls. */
void pe_inventory_api_install(void);
void pe_inventory_api_loaded(void);
void pe_inventory_api_before_save(void);
int pe_inventory_native_guard(int native_value);
typedef struct {
    uint32_t struct_size;
    PeRegResult (*grant)(const char *, uint32_t *);
    PeRegResult (*inspect)(uint32_t, PeInventoryInstance *);
    PeRegResult (*destroy)(uint32_t);
    PeRegResult (*adopt)(int, uint32_t *);
} PeInventoryActionsApi;
typedef struct {
    uint32_t struct_size;
    PeRegResult (*add)(const char *, const PeEffectDef *);
    const PeEffectDef *(*find)(const char *);
    int (*count)(void);
    const PeEffectDef *(*at)(int);
    PeRegResult (*apply)(uint32_t, const char *, unsigned);
    PeRegResult (*remove)(uint32_t, unsigned);
} PeEquipmentEffectsApi;
extern const PeInventoryActionsApi g_pe_inventory_actions_api;
extern const PeEquipmentEffectsApi g_pe_equipment_effects_api;
#endif
