#ifndef PE_CONTENT_API_H
#define PE_CONTENT_API_H
/* Version-one, copied, bounded definitions. No engine pointers or save-file paths in descriptors.
 * Register in a declared pe_reg_batch during init. PENDING is not readiness; query after seal.
 * Keys belong to the submitting mod. References may belong to other mods and are type checked.
 * Version/size are exact for v1: unknown layouts are refused, never read speculatively.
 */
#include "pe_registry.h"
#define PE_CONTENT_API_VERSION 1
#define PE_CONTENT_NAME_MAX 63
enum {
    PE_SCHEMA_CONTENT = 0x50430101u,
    PE_SCHEMA_EFFECT = 0x50450101u,
    PE_SCHEMA_CATALOG = 0x50410101u,
    PE_SCHEMA_SHOP = 0x50530101u,
    PE_SCHEMA_POLICY = 0x50500101u
};
typedef struct {
    uint32_t version, size;
} PeApiHeader;
/* Native-compatible archetype. native_item remains the fallback item id. No new combat code,
 * animations, consumable identity or arbitrary native effect numbers are implied by registration.
 * kinds 1..8 weapons, 9 armor. All 10 physical native slots remain bounded.
 */
typedef struct {
    PeApiHeader api;
    char key[48], name[64];
    uint16_t revision;
    uint8_t kind, native_item, base[3], capacity, count, effects[10];
    int16_t bonus[3];
} PeContentDef;
/* An alias for an implemented native effect, with its original native exclusivity group.
 * Armor 16 and >20, weapon >20 and zero are unsupported. Armor 17..20 need pe_economy.
 * The factory/installer rechecks the runtime provider, not only the declaration.
 */
typedef struct {
    PeApiHeader api;
    char key[48], name[64];
    uint16_t revision;
    uint8_t armor, native_effect, group, removable;
} PeEffectDef;
typedef struct {
    PeApiHeader api;
    char key[48], name[64], effect[48];
    uint16_t ordinal; /* stable provider ordinal, never reused after release */
    uint32_t junk_price;
} PeCatalogEntryDef;
enum { PE_OFFER_CONTENT = 1, PE_OFFER_CATALOG = 2 };
typedef struct {
    PeApiHeader api;
    char key[48], name[64], target[48];
    uint32_t junk_price;
    uint16_t stock; /* 0 unlimited; per-save stock is owned by the shop API */
    uint8_t kind;   /* referenced content or Catalog entry */
} PeShopOfferDef;

PeRegResult pe_content_register(const char *batch, const PeContentDef *def);
PeRegResult pe_effect_register(const char *batch, const PeEffectDef *def);
PeRegResult pe_catalog_entry_register(const char *batch, const PeCatalogEntryDef *def);
PeRegResult pe_shop_offer_register(const char *batch, const PeShopOfferDef *def);
const PeContentDef *pe_content_find(const char *key);
const PeEffectDef *pe_effect_find(const char *key);
const PeCatalogEntryDef *pe_catalog_entry_find(const char *key);
const PeShopOfferDef *pe_shop_offer_find(const char *key);
/* Enumerates only typed definitions, skips an inactive provider, canonical registry order. */
int pe_content_count(void);
int pe_effect_count(void);
int pe_catalog_entry_count(void);
int pe_shop_offer_count(void);
const PeContentDef *pe_content_at(int index);
const PeEffectDef *pe_effect_at(int index);
const PeCatalogEntryDef *pe_catalog_entry_at(int index);
const PeShopOfferDef *pe_shop_offer_at(int index);
/* Factory only: produces a complete native-compatible fallback record in caller memory.
 * It does not insert it into inventory. Inventory admission/identity belongs to inventory_actions.
 */
struct ItemDataRecord;
PeRegResult pe_content_preview(const char *key, struct ItemDataRecord *out);
typedef struct {
    uint32_t struct_size;
    PeRegResult (*add)(const char *, const PeContentDef *);
    const PeContentDef *(*find)(const char *);
    int (*count)(void);
    const PeContentDef *(*at)(int);
    PeRegResult (*preview)(const char *, struct ItemDataRecord *);
} PeContentRegistryApi;
extern const PeContentRegistryApi g_pe_content_registry_api;
#endif
