/* The Workshop's rules without drawing. See pe_workshop_model.h. */
#include <stdio.h>
#include <string.h>

#include "pe_item_table.h"
#include "pe_mod_host.h"
#include "pe_requisition.h"
#include "pe_services.h"
#include "pe_shops.h"
#include "pe_workshop_model.h"
#include "pe1/inventory.h"

#define PE_ECON_FIRST 0xF000u
#define PE_OWNED_WHY  "Already unlocked"     /* pe_shop_blocked's word for an economy effect bought */

static const char *const s_name[PE_WS_SERVICES] = {"Requisition", "Rebuild", "Wardrobe", "Mod Permit Card"};
static const char *const s_help[PE_WS_SERVICES] = {
    "Medicine, ammo, Tools and Mod Catalog effects for Junk",
    "Buy back dismantled guns and armor",
    "Outfits to buy or wear",
    "Add a mod slot to a gun or armor",
};

static const PeWsService *extra_services[PE_WS_EXTRA_SERVICES];
static unsigned extra_count;
int pe_ws_register_service(const PeWsService *s) {
    if(!s||!s->id||!s->id[0]||!s->name||!s->help||!s->open||extra_count==PE_WS_EXTRA_SERVICES)return 0;
    for(unsigned i=0;i<extra_count;++i)if(!strcmp(s->id,extra_services[i]->id))return 0;
    extra_services[extra_count++]=s;return 1;
}
static const PeWsService *extra_service(int service) {
    int at=service-PE_WS_SERVICES;
    return at>=0&&(unsigned)at<extra_count?extra_services[at]:NULL;
}
int pe_ws_open_service(int service, struct MenuWidgetNode *from) {
    const PeWsService *s=extra_service(service);
    return s&&(!s->available||s->available())?s->open(from):0;
}

int pe_ws_services(int *out) {
    int n = 0;
    out[n++] = PE_WS_REQUISITION;
    out[n++] = PE_WS_REBUILD;
    if (pe_service_get("wardrobe", 1)) out[n++] = PE_WS_WARDROBE;   /* while a mod offers costumes */
    out[n++] = PE_WS_PERMIT;
    for(unsigned i=0;i<extra_count;++i)
        if(!extra_services[i]->available||extra_services[i]->available())out[n++]=PE_WS_SERVICES+(int)i;
    return n;
}

const char *pe_ws_service_name(int s) { const PeWsService *extra=extra_service(s); return s >= 0 && s < PE_WS_SERVICES ? s_name[s] : extra ? extra->name : ""; }
const char *pe_ws_service_help(int s) { const PeWsService *extra=extra_service(s); return s >= 0 && s < PE_WS_SERVICES ? s_help[s] : extra ? extra->help : ""; }

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

const char *pe_ws_item_name(unsigned id) {
    unsigned i;
    for (i = 0; i < PE_ITEM_COUNT; ++i) if (pe_item_ids[i] == id) return pe_item_names[i];
    return "it";
}

/* ---- Rebuild ---- */
static const PeRebuildApi *pe_ws_rebuild_api(void) { return (const PeRebuildApi *)pe_service_get("rebuild", 1); }

int pe_ws_rebuild_count(void) { const PeRebuildApi *r = pe_ws_rebuild_api(); return r ? r->count() : 0; }

int pe_ws_rebuild(int row, PeWsRebuild *out, ItemDataRecord *rec) {
    const PeRebuildApi *r = pe_ws_rebuild_api();
    memset(out, 0, sizeof *out);
    if (!r || row < 0 || row >= r->count() || !(out->id = r->item(row))) return 0;
    out->price = r->price(row);
    out->bought = r->bought_slots(row);
    if (!pe_shopw_enabled()) out->blocked = "Junk economy is off";
    else if (out->price > pe_shopw_junk()) out->blocked = "Not enough Junk";
    if (rec && (!r->preview || !r->preview(row, rec))) memset(rec, 0, sizeof *rec);   /* no stats from older Catalogs */
    return 1;
}

int pe_ws_rebuild_cross(int row, char *msg, size_t size) {
    PeWsRebuild b;
    if (!pe_ws_rebuild(row, &b, NULL)) { snprintf(msg, size, "Nothing to rebuild"); return PE_WS_REFUSE; }
    if (b.blocked) { snprintf(msg, size, "%s", b.blocked); return PE_WS_REFUSE; }
    snprintf(msg, size, "Rebuild %s for %u Junk?", pe_ws_item_name(b.id), (unsigned)b.price);
    return PE_WS_ASK;
}

int pe_ws_rebuild_do(int row, char *msg, size_t size) {
    const PeRebuildApi *r = pe_ws_rebuild_api();
    if (r) return r->rebuild(row, msg, size);
    snprintf(msg, size, "Mod Catalog is off");
    return 0;
}
