#include <string.h>
#include "pe_content_api.h"
#include "pe_mod_host.h"
#include "pe_game.h"
#include "pe_api_native.h"
#include "pe1/inventory.h"

static int text(const char *s, size_t n) {
    return s && s[0] && memchr(s, 0, n);
}
static int header(const PeApiHeader *h, size_t n) {
    return h && h->version == 1 && h->size == n;
}
static PeRegResult put(const char *batch, int domain, const char *key, const void *d, size_t n,
                       uint32_t schema, const PeRegRef *refs, int nr) {
    PeRegEntry e = {(uint8_t)domain, key, d, (uint32_t)n, refs, (uint8_t)nr, schema};
    return pe_reg_submit(batch, &e);
}
PeRegResult pe_content_register(const char *batch, const PeContentDef *d) {
    if (!d) return PE_REG_BAD_ARGUMENT;
    if (!header(&d->api, sizeof *d)) return PE_REG_BAD_VERSION;
    if (!text(d->key, sizeof d->key) || !text(d->name, sizeof d->name) || !d->revision || !d->native_item ||
        d->kind < 1 || d->kind > 9 || d->capacity > 10 || d->count > d->capacity)
        return PE_REG_BAD_ARGUMENT;
    for (int i = 0; i < 10; ++i) {
        unsigned e = d->effects[i] & 31u;
        if (i >= d->count && d->effects[i]) return PE_REG_BAD_ARGUMENT;
        if (e > 20 || (d->kind == 9 && e == 16)) return PE_REG_UNSUPPORTED;
        if ((!e && d->effects[i]) || (e && (d->effects[i] >> 5) != pe_mod_effect_group(d->kind == 9, e)))
            return PE_REG_CONFLICT;
    }
    for (int i = 0; i < 3; ++i)
        if (d->bonus[i] < 0 || d->bonus[i] > 999) return PE_REG_BAD_ARGUMENT;
    return put(batch, PE_REG_CONTENT, d->key, d, sizeof *d, PE_SCHEMA_CONTENT, NULL, 0);
}
PeRegResult pe_effect_register(const char *batch, const PeEffectDef *d) {
    if (!d) return PE_REG_BAD_ARGUMENT;
    if (!header(&d->api, sizeof *d)) return PE_REG_BAD_VERSION;
    if (!text(d->key, sizeof d->key) || !text(d->name, sizeof d->name) || !d->revision || d->armor > 1 ||
        d->group > 7 || d->removable > 1)
        return PE_REG_BAD_ARGUMENT;
    if (!d->native_effect || d->native_effect > 20 || (d->armor && d->native_effect == 16))
        return PE_REG_UNSUPPORTED;
    if (d->group != pe_mod_effect_group(d->armor, d->native_effect)) return PE_REG_CONFLICT;
    return put(batch, PE_REG_EFFECT, d->key, d, sizeof *d, PE_SCHEMA_EFFECT, NULL, 0);
}
PeRegResult pe_catalog_entry_register(const char *batch, const PeCatalogEntryDef *d) {
    if (!d) return PE_REG_BAD_ARGUMENT;
    if (!header(&d->api, sizeof *d)) return PE_REG_BAD_VERSION;
    if (!text(d->key, sizeof d->key) || !text(d->name, sizeof d->name) ||
        !text(d->effect, sizeof d->effect) || d->ordinal >= 128 || d->junk_price > 99999)
        return PE_REG_BAD_ARGUMENT;
    PeRegRef ref = {PE_REG_EFFECT, d->effect, PE_SCHEMA_EFFECT};
    PeRegEntry e = {PE_REG_CATALOG,          d->key, d, sizeof *d, &ref, 1, PE_SCHEMA_CATALOG,
                    (uint32_t)d->ordinal + 1};
    return pe_reg_submit(batch, &e);
}
PeRegResult pe_shop_offer_register(const char *batch, const PeShopOfferDef *d) {
    if (!d) return PE_REG_BAD_ARGUMENT;
    if (!header(&d->api, sizeof *d)) return PE_REG_BAD_VERSION;
    if (!text(d->key, sizeof d->key) || !text(d->name, sizeof d->name) ||
        !text(d->target, sizeof d->target) || d->junk_price > 99999 || d->kind < PE_OFFER_CONTENT ||
        d->kind > PE_OFFER_CATALOG)
        return PE_REG_BAD_ARGUMENT;
    PeRegRef ref = {d->kind == PE_OFFER_CONTENT ? PE_REG_CONTENT : PE_REG_CATALOG, d->target,
                    d->kind == PE_OFFER_CONTENT ? PE_SCHEMA_CONTENT : PE_SCHEMA_CATALOG};
    return put(batch, PE_REG_SHOP, d->key, d, sizeof *d, PE_SCHEMA_SHOP, &ref, 1);
}
static const void *find(int domain, const char *key, size_t n, uint32_t schema) {
    const void *d = pe_reg_find_typed(domain, key, schema, (uint32_t)n);
    return d && header(d, n) ? d : NULL;
}
#define ACCESSORS(name, type, domain, schema)                                                                \
    const type *pe_##name##_find(const char *key) {                                                          \
        return find(domain, key, sizeof(type), schema);                                                      \
    }                                                                                                        \
    const type *pe_##name##_at(int index) {                                                                  \
        if (index < 0) return NULL;                                                                          \
        for (int i = 0; i < pe_reg_count(domain); ++i) {                                                     \
            const type *d = pe_##name##_find(pe_reg_key(domain, i));                                         \
            if (d && index-- == 0) return d;                                                                 \
        }                                                                                                    \
        return NULL;                                                                                         \
    }                                                                                                        \
    int pe_##name##_count(void) {                                                                            \
        int n = 0;                                                                                           \
        for (int i = 0; i < pe_reg_count(domain); ++i)                                                       \
            n += pe_##name##_find(pe_reg_key(domain, i)) != NULL;                                            \
        return n;                                                                                            \
    }
ACCESSORS(content, PeContentDef, PE_REG_CONTENT, PE_SCHEMA_CONTENT)
ACCESSORS(effect, PeEffectDef, PE_REG_EFFECT, PE_SCHEMA_EFFECT)
ACCESSORS(catalog_entry, PeCatalogEntryDef, PE_REG_CATALOG, PE_SCHEMA_CATALOG)
ACCESSORS(shop_offer, PeShopOfferDef, PE_REG_SHOP, PE_SCHEMA_SHOP)

PeRegResult pe_content_preview(const char *key, ItemDataRecord *out) {
    PeRegResult r = pe_reg_access();
    if (r != PE_REG_OK) return r;
    if (!out) return PE_REG_BAD_ARGUMENT;
    const PeContentDef *d = pe_content_find(key);
    if (!d) return PE_REG_UNAVAILABLE;
    if (d->kind < 1 || d->kind > 9 || d->capacity > 10 || d->count > d->capacity) return PE_REG_BAD_ARGUMENT;
    if (!pe_api_native_loaded() || !pe_api_native()->base_item) return PE_REG_UNAVAILABLE;
    const ItemDataRecord *base = pe_api_native()->base_item(d->native_item);
    if (!base || !base->iconId || base->kind != d->kind) return PE_REG_UNSUPPORTED;
    if (d->kind == 9)
        for (int i = 0; i < d->count; ++i)
            if ((d->effects[i] & 31u) >= 17 && !pe_mod_is_running("pe_economy")) return PE_REG_UNAVAILABLE;
    ItemDataRecord copy = *base;
    copy.reserved02[0] = d->native_item;
    copy.reserved02[1] = 0;
    copy.tailCapacity = d->capacity;
    copy.tailCount = d->count;
    memcpy(copy.baseStats, d->base, sizeof d->base);
    memcpy(copy.bonusStats, d->bonus, sizeof d->bonus);
    memset(copy.tailData, 0, sizeof copy.tailData);
    memcpy(copy.tailData, d->effects, sizeof d->effects);
    *out = copy;
    return PE_REG_OK;
}

const PeContentRegistryApi g_pe_content_registry_api = {sizeof(PeContentRegistryApi),
                                                        pe_content_register,
                                                        pe_content_find,
                                                        pe_content_count,
                                                        pe_content_at,
                                                        pe_content_preview};
