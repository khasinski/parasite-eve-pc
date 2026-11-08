#include <string.h>
#include "pe_shop_api.h"
#include "pe_inventory_internal.h"
#include "pe_extension_store.h"
#include "pe_mod_host.h"
#include "pe_services.h"
#include "pe_game.h"
#include "pe_api_native.h"
#include "pe1/inventory_state.h"
/* v1 bounded persistent tables: 20 discovered entries and 10 finite stock counters.
 * Failure at either limit refuses the whole transaction; unknown keys are retained unchanged. */
static unsigned char s_discovery[1024], s_stock[512];
static PeExtensionSection s_discovery_section = {"pe_host/catalog", 1, 1, sizeof s_discovery, s_discovery};
static PeExtensionSection s_stock_section = {"pe_host/shop", 1, 1, sizeof s_stock, s_stock};
static struct {
    PeShopQuote quote;
    uint32_t wallet, inventory;
    int state;
} s_quotes[32];
static uint64_t s_token;
static int s_busy;
static uint32_t hash(const void *data, unsigned n) {
    const unsigned char *p = data;
    uint32_t h = 2166136261u;
    while (n--)
        h = (h ^ *p++) * 16777619u;
    return h;
}
static const PeJunkApi *wallet(void) {
    const PeJunkApi *j = pe_service_get("junk", 1);
    if (!j || !j->enabled || !j->junk || !j->spend || !pe_reg_readonly_begin()) return NULL;
    int on = j->enabled();
    pe_reg_readonly_end();
    return on ? j : NULL;
}
static uint32_t money(const PeJunkApi *j) {
    uint32_t v = 0;
    if (pe_reg_readonly_begin()) {
        v = j->junk();
        pe_reg_readonly_end();
    }
    return v;
}
static int debit(const PeJunkApi *j, uint32_t n) {
    if (!pe_reg_readonly_begin()) return 0;
    int ok = j->spend(n);
    pe_reg_readonly_end();
    return ok;
}
static int row(unsigned char *b, int count, int stride, const char *key) {
    for (int i = 0; i < count; ++i)
        if (b[i * stride] && !strcmp((char *)b + i * stride, key)) return i;
    return -1;
}
static int empty(unsigned char *b, int count, int stride) {
    for (int i = 0; i < count; ++i)
        if (!b[i * stride]) return i;
    return -1;
}
static int valid(unsigned char *b, int count, int stride) {
    for (int i = 0; i < count; ++i)
        if (b[i * stride]) {
            if (!memchr(b + i * stride, 0, 48)) return 0;
            for (int j = 0; j < i; ++j)
                if (b[j * stride] && !strcmp((char *)b + i * stride, (char *)b + j * stride)) return 0;
        }
    return 1;
}
static PeRegResult gate(void) {
    PeRegResult r = pe_reg_access();
    if (r != PE_REG_OK) return r;
    if (s_busy || !pe_api_native_loaded() || pe_api_native_in_battle()) return PE_REG_WRONG_PHASE;
    return PE_REG_OK;
}
void pe_shop_api_install(void) {
    (void)pe_extension_register(&s_discovery_section);
    (void)pe_extension_register(&s_stock_section);
}
void pe_shop_api_loaded(void) {
    memset(s_quotes, 0, sizeof s_quotes);
    s_busy = 0;
}
PeRegResult pe_catalog_entry_state(const char *key, unsigned *out) {
    PeRegResult r = pe_reg_access();
    if (r != PE_REG_OK) return r;
    if (!out || !key) return PE_REG_BAD_ARGUMENT;
    if (!pe_catalog_entry_find(key)) return PE_REG_UNAVAILABLE;
    if (!pe_extension_available(&s_discovery_section) || !valid(s_discovery, 20, 50))
        return PE_REG_UNSAVEABLE;
    int at = row(s_discovery, 20, 50, key);
    unsigned v = at < 0 ? 0 : s_discovery[at * 50 + 48];
    if (v > 3) return PE_REG_UNSAVEABLE;
    *out = v;
    return PE_REG_OK;
}
const PeCatalogEntryDef *pe_catalog_entry_visible(const char *key) {
    unsigned s;
    return pe_catalog_entry_state(key, &s) == PE_REG_OK && (s & PE_DISCOVERED) ? pe_catalog_entry_find(key)
                                                                               : NULL;
}
static PeRegResult prepare_discovery(const char *key, int *at) {
    unsigned state;
    PeRegResult r = pe_catalog_entry_state(key, &state);
    if (r != PE_REG_OK) return r;
    *at = row(s_discovery, 20, 50, key);
    if (*at < 0) *at = empty(s_discovery, 20, 50);
    if (*at < 0) return PE_REG_CAPACITY;
    return pe_extension_enable(&s_discovery_section);
}
static void discover(const char *key, int at, int unlock) {
    unsigned char *p = s_discovery + at * 50;
    strcpy((char *)p, key);
    p[48] |= PE_DISCOVERED | (unlock ? PE_UNLOCKED : 0);
}
PeRegResult pe_catalog_entry_discover(const char *key, int unlock) {
    PeRegResult r = gate();
    if (r != PE_REG_OK) return r;
    if (unlock != 0 && unlock != 1) return PE_REG_BAD_ARGUMENT;
    int at;
    if ((r = prepare_discovery(key, &at)) != PE_REG_OK) return r;
    discover(key, at, unlock);
    return PE_REG_OK;
}
static PeRegResult stock(const PeShopOfferDef *d, unsigned *left, int *at) {
    *at = -1;
    *left = 0;
    if (!d->stock) return PE_REG_OK;
    if (!pe_extension_available(&s_stock_section) || !valid(s_stock, 10, 50)) return PE_REG_UNSAVEABLE;
    *at = row(s_stock, 10, 50, d->key);
    unsigned sold = *at < 0 ? 0 : s_stock[*at * 50 + 48] | (unsigned)s_stock[*at * 50 + 49] << 8;
    if (sold > d->stock) return PE_REG_CONFLICT;
    *left = d->stock - sold;
    return PE_REG_OK;
}
static PeRegResult quote_inner(const char *key, PeShopQuote *out, uint32_t *balance) {
    const PeShopOfferDef *d = pe_shop_offer_find(key);
    if (!d) return PE_REG_UNAVAILABLE;
    const PeJunkApi *j = wallet();
    if (!j) return PE_REG_UNAVAILABLE;
    unsigned left;
    int at;
    PeRegResult r = stock(d, &left, &at);
    if (r != PE_REG_OK) return r;
    if (d->stock && !left) return PE_REG_CAPACITY;
    if (d->kind == PE_OFFER_CATALOG) {
        unsigned state;
        if ((r = pe_catalog_entry_state(d->target, &state)) != PE_REG_OK) return r;
        if (state & PE_UNLOCKED) return PE_REG_CONFLICT;
    }
    PeShopQuote q = {0};
    q.generation = pe_reg_generation();
    strcpy(q.offer, key);
    q.price = d->junk_price;
    q.stock_left = left;
    q.unlimited = !d->stock;
    *balance = money(j);
    *out = q;
    return PE_REG_OK;
}
PeRegResult pe_economy_shop_quote(const char *key, PeShopQuote *out) {
    PeRegResult r = gate();
    if (r != PE_REG_OK) return r;
    if (!key || !out) return PE_REG_BAD_ARGUMENT;
    if (s_token == UINT64_MAX) return PE_REG_CAPACITY;
    s_busy = 1;
    PeShopQuote q;
    uint32_t balance;
    r = quote_inner(key, &q, &balance);
    if (r == PE_REG_OK) {
        q.token = ++s_token;
        int at = q.token % 32;
        s_quotes[at].quote = q;
        s_quotes[at].wallet = balance;
        s_quotes[at].inventory = hash(&PE_API_INV, sizeof PE_API_INV);
        s_quotes[at].state = 1;
        *out = q;
    }
    s_busy = 0;
    return r;
}
PeRegResult pe_economy_shop_buy(const PeShopQuote *q, uint32_t *out) {
    PeRegResult r = gate();
    if (r != PE_REG_OK) return r;
    if (!q) return PE_REG_BAD_ARGUMENT;
    if (q->generation != pe_reg_generation()) return PE_REG_STALE_GENERATION;
    int at = q->token % 32;
    if (!q->token || s_quotes[at].state != 1 ||
        (q->generation != s_quotes[at].quote.generation || q->token != s_quotes[at].quote.token ||
         memcmp(q->offer, s_quotes[at].quote.offer, 48) || q->price != s_quotes[at].quote.price ||
         q->stock_left != s_quotes[at].quote.stock_left || q->unlimited != s_quotes[at].quote.unlimited))
        return PE_REG_CONFLICT;
    s_busy = 1;
    PeShopQuote current;
    uint32_t balance;
    r = quote_inner(q->offer, &current, &balance);
    if (r != PE_REG_OK) goto done;
    if (balance != s_quotes[at].wallet || hash(&PE_API_INV, sizeof PE_API_INV) != s_quotes[at].inventory ||
        current.price != q->price || current.stock_left != q->stock_left) {
        r = PE_REG_CONFLICT;
        goto done;
    }
    if (balance < q->price) {
        r = PE_REG_CONFLICT;
        goto done;
    }
    const PeShopOfferDef *d = pe_shop_offer_find(q->offer);
    PeInventoryGrantPlan plan;
    int discovery = -1;
    if (d->kind == PE_OFFER_CONTENT) {
        if (!out) {
            r = PE_REG_BAD_ARGUMENT;
            goto done;
        }
        r = pe_inventory_prepare_grant(d->target, &plan);
    } else r = prepare_discovery(d->target, &discovery);
    if (r != PE_REG_OK) goto done;
    int stock_at = -1;
    if (d->stock) {
        stock_at = row(s_stock, 10, 50, d->key);
        if (stock_at < 0) stock_at = empty(s_stock, 10, 50);
        if (stock_at < 0) {
            r = PE_REG_CAPACITY;
            goto done;
        }
        if ((r = pe_extension_enable(&s_stock_section)) != PE_REG_OK) goto done;
    }
    const PeJunkApi *j = wallet();
    if (!j || money(j) != balance) {
        r = PE_REG_CONFLICT;
        goto done;
    }
    if (!debit(j, q->price)) {
        r = PE_REG_CONFLICT;
        goto done;
    }
    /* No callbacks, allocation or I/O after debit. Production wallet's spend is synchronous;
     * arbitrary extension wallets must honor that existing contract. Quote consumed first. */
    s_quotes[at].state = 2;
    if (d->kind == PE_OFFER_CONTENT) *out = pe_inventory_commit_grant(&plan);
    else discover(d->target, discovery, 1);
    if (stock_at >= 0) {
        unsigned char *p = s_stock + stock_at * 50;
        strcpy((char *)p, d->key);
        unsigned sold = (p[48] | (unsigned)p[49] << 8) + 1;
        p[48] = sold;
        p[49] = sold >> 8;
    }
    r = PE_REG_OK;
done:
    s_busy = 0;
    return r;
}

const PeCatalogEntriesApi g_pe_catalog_entries_api = {sizeof(PeCatalogEntriesApi), pe_catalog_entry_register,
                                                      pe_catalog_entry_find,       pe_catalog_entry_count,
                                                      pe_catalog_entry_at,         pe_catalog_entry_visible,
                                                      pe_catalog_entry_state,      pe_catalog_entry_discover};
const PeEconomyShopApi g_pe_economy_shop_api = {
    sizeof(PeEconomyShopApi), pe_shop_offer_register, pe_shop_offer_find, pe_shop_offer_count,
    pe_shop_offer_at,         pe_economy_shop_quote,  pe_economy_shop_buy};
