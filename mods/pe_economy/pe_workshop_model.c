/* The Workshop's rules without drawing. See pe_workshop_model.h. */
#include <stdio.h>
#include <string.h>

#include "pe_mod_host.h"
#include "pe_requisition.h"
#include "pe_services.h"
#include "pe_workshop_model.h"

#define PE_ECON_FIRST 0xF000u
#define PE_OWNED_WHY  "Already unlocked"     /* pe_shop_blocked's word for an economy effect bought */

static const char *const s_name[PE_WS_SERVICES] = {"Requisition", "Rebuild", "Wardrobe", "Mod Permit Card"};
static const char *const s_help[PE_WS_SERVICES] = {
    "Medicine, ammo, Tools and Mod Catalog effects for Junk",
    "Buy back dismantled guns and armor",
    "Outfits to buy or wear",
    "Add a mod slot to a gun or armor",
};

int pe_ws_services(int *out) {
    int n = 0;
    out[n++] = PE_WS_REQUISITION;
    out[n++] = PE_WS_REBUILD;
    if (pe_service_get("wardrobe", 1)) out[n++] = PE_WS_WARDROBE;   /* while a mod offers costumes */
    out[n++] = PE_WS_PERMIT;
    return n;
}

const char *pe_ws_service_name(int s) { return s >= 0 && s < PE_WS_SERVICES ? s_name[s] : ""; }
const char *pe_ws_service_help(int s) { return s >= 0 && s < PE_WS_SERVICES ? s_help[s] : ""; }

int pe_ws_goods(int row, PeWsGoods *g) {
    memset(g, 0, sizeof *g);
    if (row < 0 || row >= pe_shop_count() || !(g->id = pe_shop_item(row))) return 0;
    g->name = g->id >= PE_ECON_FIRST ? pe_shop_name(g->id) : NULL;
    g->price = pe_shop_price(g->id);
    g->blocked = pe_shop_blocked(g->id);
    g->owned = g->blocked && strcmp(g->blocked, PE_OWNED_WHY) == 0;
    return 1;
}

int pe_ws_cross(int row, char *msg, size_t size) {
    PeWsGoods g;
    if (!pe_ws_goods(row, &g)) { snprintf(msg, size, "Nothing to buy there"); return PE_WS_REFUSE; }
    if (g.blocked) { snprintf(msg, size, "%s", g.owned ? "Already owned" : g.blocked); return PE_WS_REFUSE; }
    snprintf(msg, size, "Buy %s for %u Junk?", g.name ? g.name : pe_shop_name(g.id), (unsigned)g.price);
    return PE_WS_ASK;
}
