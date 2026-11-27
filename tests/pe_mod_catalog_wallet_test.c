/* The Mod Catalog's view of the Junk wallet (mods/pe_catalog/pe_catalog_wallet.c), since it
 * became its own mod: everything goes through the "junk" service. With no wallet running, paid
 * actions are refused and nothing is free; with one, every call forwards unchanged. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "pe_catalog_mod.h"

static uint32_t s_junk = 100;
static int s_on = 1, s_wallet = 0;
static int on(void) { return s_on; }
static uint32_t junk(void) { return s_junk; }
static int spend(uint32_t n) { if (n > s_junk) return 0; s_junk -= n; return 1; }
static void give(uint32_t n) { s_junk += n; }
static int armor(int e) { return e == PE_FX_BARGAIN; }
static uint32_t price(uint32_t b) { return b * 85u / 100u; }
static const PeJunkApi s_api = {on, junk, spend, give, armor, price, NULL, NULL};
const void *pe_service_get(const char *name, int version) { (void)version; return s_wallet && !strcmp(name, "junk") ? &s_api : NULL; }

int main(void) {
    /* No wallet: Junk economy off, nothing to spend, nothing given, prices as listed. */
    assert(!pe_catw_enabled() && pe_catw_junk() == 0 && !pe_catw_spend_junk(1) && !pe_catw_armor_has(PE_FX_BARGAIN));
    pe_catw_give_junk(50);
    assert(s_junk == 100 && pe_catw_price(40) == 40);
    /* The wallet running: the same answers it gives everyone. */
    s_wallet = 1;
    assert(pe_catw_enabled() && pe_catw_junk() == 100 && pe_catw_price(40) == 34 && pe_catw_armor_has(PE_FX_BARGAIN));
    assert(pe_catw_spend_junk(30) && s_junk == 70 && !pe_catw_spend_junk(71) && s_junk == 70);
    pe_catw_give_junk(5);
    assert(s_junk == 75);
    s_on = 0;
    assert(!pe_catw_enabled());              /* Junk economy switched off: the Catalog sees it */
    puts("Catalog wallet: no wallet refuses and gives nothing; a wallet forwards enabled, Junk, spend, give, Bargain and prices");
    return 0;
}
