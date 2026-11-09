#ifndef PE_SHOP_API_H
#define PE_SHOP_API_H
#include "pe_inventory_api.h"
enum { PE_DISCOVERED = 1, PE_UNLOCKED = 2 };
typedef struct {
    uint32_t generation;
    uint64_t token;
    char offer[48];
    uint32_t price;
    uint16_t stock_left;
    uint8_t unlimited;
} PeShopQuote;
/* An undiscovered entry has no public descriptor through this user-facing accessor; callers
 * render ???? with no details. pe_catalog_entry_find is for provider/core definition lookup. */
const PeCatalogEntryDef *pe_catalog_entry_visible(const char *key);
PeRegResult pe_catalog_entry_state(const char *key, unsigned *state);
PeRegResult pe_catalog_entry_discover(const char *key, int unlock);
PeRegResult pe_economy_shop_quote(const char *offer, PeShopQuote *out);
PeRegResult pe_economy_shop_buy(const PeShopQuote *quote, uint32_t *instance);
void pe_shop_api_install(void);
void pe_shop_api_loaded(void);
typedef struct {
    uint32_t struct_size;
    PeRegResult (*add)(const char *, const PeCatalogEntryDef *);
    const PeCatalogEntryDef *(*find)(const char *);
    int (*count)(void);
    const PeCatalogEntryDef *(*at)(int);
    const PeCatalogEntryDef *(*visible)(const char *);
    PeRegResult (*state)(const char *, unsigned *);
    PeRegResult (*discover)(const char *, int);
} PeCatalogEntriesApi;
typedef struct {
    uint32_t struct_size;
    PeRegResult (*add)(const char *, const PeShopOfferDef *);
    const PeShopOfferDef *(*find)(const char *);
    int (*count)(void);
    const PeShopOfferDef *(*at)(int);
    PeRegResult (*quote)(const char *, PeShopQuote *);
    PeRegResult (*buy)(const PeShopQuote *, uint32_t *);
} PeEconomyShopApi;
extern const PeCatalogEntriesApi g_pe_catalog_entries_api;
extern const PeEconomyShopApi g_pe_economy_shop_api;
#endif
