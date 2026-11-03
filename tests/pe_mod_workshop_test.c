/* Workshop model fixture: the production rules (pe_economy/pe_workshop_model.c) over a stand-in
 * Requisition board, without the game's menus. Covers the Workshop Overhaul decisions: gray rows
 * stay selectable and say why, a bought one-of-a-kind good reads "owned", and every purchase asks. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "pe_mod_host.h"
#include "pe_requisition.h"
#include "pe_services.h"
#include "pe_workshop_model.h"
#include "pe1/inventory.h"

/* The board: medicine (affordable), ammo (full), an economy effect bought already, one too dear. */
static const struct { uint16_t id; const char *name; uint32_t price; const char *blocked; } s_board[] = {
    {0x14, "Medicine 1", 15, NULL},
    {0x0B, "Bullets", 10, "Ammo is full"},
    {0xF001, "Hunter's eye", 120, "Already unlocked"},
    {0x36, "Mod Permit", 400, "Not enough Junk"},
};
#define BOARD ((int)(sizeof s_board / sizeof s_board[0]))
static int s_wardrobe_on = 1;

int pe_shop_count(void) { return BOARD; }
uint16_t pe_shop_item(int row) { return row >= 0 && row < BOARD ? s_board[row].id : 0; }
static int at(uint16_t id) { int i; for (i = 0; i < BOARD; ++i) if (s_board[i].id == id) return i; return -1; }
const char *pe_shop_name(uint16_t id) { return at(id) >= 0 ? s_board[at(id)].name : "None"; }
uint32_t pe_shop_price(uint16_t id) { return at(id) >= 0 ? s_board[at(id)].price : 0; }
const char *pe_shop_blocked(uint16_t id) { return at(id) >= 0 ? s_board[at(id)].blocked : "No such goods"; }
/* The wallet and a stand-in Rebuild list: a gun with 2 bought slots (40 Junk), armor too dear. */
static int s_economy_on = 1;
static uint32_t s_junk = 100;
int pe_shopw_enabled(void) { return s_economy_on; }
uint32_t pe_shopw_junk(void) { return s_junk; }
static int rb_count(void) { return 2; }
static uint8_t rb_item(int i) { return i == 0 ? 0x14 : 0x0B; }
static unsigned rb_bought(int i) { return i == 0 ? 2u : 0u; }
static uint32_t rb_price(int i) { return i == 0 ? 40u : 150u; }
static int rb_rebuilt = -1;
static int rb_rebuild(int i, char *msg, size_t size) { rb_rebuilt = i; snprintf(msg, size, "Rebuilt"); return 1; }
static int rb_preview(int i, ItemDataRecord *out) {
    memset(out, 0, sizeof *out);
    out->iconId = 1;
    out->kind = i == 0 ? 1 : 9;
    out->baseStats[0] = 30;
    out->bonusStats[0] = 5;
    out->tailCount = i == 0 ? 4 : 1;
    return 1;
}
static PeRebuildApi s_rebuild = { rb_count, rb_item, rb_bought, rb_price, rb_rebuild, rb_preview };
static int s_rebuild_on = 1;

const void *pe_service_get(const char *name, int v) {
    (void)v;
    if (!strcmp(name, "rebuild")) return s_rebuild_on ? &s_rebuild : NULL;
    return !strcmp(name, "wardrobe") && s_wardrobe_on ? (const void *)1 : NULL;
}

int main(void) {
    int services[PE_WS_SERVICES], n;
    PeWsGoods g;
    char msg[96];

    /* The Workshop's services: Wardrobe only while a mod offers costumes. */
    n = pe_ws_services(services);
    assert(n == 4 && services[0] == PE_WS_REQUISITION && services[2] == PE_WS_WARDROBE && services[3] == PE_WS_PERMIT);
    assert(!strcmp(pe_ws_service_name(PE_WS_PERMIT), "Mod Permit Card") && *pe_ws_service_help(PE_WS_REBUILD));
    s_wardrobe_on = 0;
    n = pe_ws_services(services);
    assert(n == 3 && services[2] == PE_WS_PERMIT);

    /* Rows: an item uses the game's name (name NULL); an economy effect its own; gray ones say why. */
    assert(pe_ws_goods(0, &g) && g.id == 0x14 && !g.name && g.price == 15 && !g.blocked && !g.owned);
    assert(pe_ws_goods(1, &g) && g.blocked && !strcmp(g.blocked, "Ammo is full") && !g.owned);
    assert(pe_ws_goods(2, &g) && g.name && !strcmp(g.name, "Hunter's eye") && g.owned && g.blocked);
    assert(pe_ws_goods(3, &g) && !g.owned && !strcmp(g.blocked, "Not enough Junk"));
    assert(!pe_ws_goods(4, &g) && !pe_ws_goods(-1, &g));

    /* Cross: every purchase asks; a gray row refuses with its reason; an owned one says so. */
    assert(pe_ws_cross(0, msg, sizeof msg) == PE_WS_ASK && !strcmp(msg, "Buy Medicine 1 for 15 Junk?"));
    assert(pe_ws_cross(1, msg, sizeof msg) == PE_WS_REFUSE && !strcmp(msg, "Ammo is full"));
    assert(pe_ws_cross(2, msg, sizeof msg) == PE_WS_REFUSE && !strcmp(msg, "Already owned"));
    assert(pe_ws_cross(3, msg, sizeof msg) == PE_WS_REFUSE && !strcmp(msg, "Not enough Junk"));
    assert(pe_ws_cross(9, msg, sizeof msg) == PE_WS_REFUSE);

    /* Rebuild: rows from the Catalog's list with the item it makes; gray rows say why; every
     * rebuild asks; no Catalog, no rows; a Catalog without preview gives no stats, not garbage. */
    {
        PeWsRebuild b;
        ItemDataRecord rec;
        assert(pe_ws_rebuild_count() == 2);
        assert(pe_ws_rebuild(0, &b, &rec) && b.id == 0x14 && b.price == 40 && b.bought == 2 && !b.blocked);
        assert(rec.kind == 1 && rec.baseStats[0] == 30 && rec.bonusStats[0] == 5 && rec.tailCount == 4);
        assert(pe_ws_rebuild(1, &b, NULL) && b.blocked && !strcmp(b.blocked, "Not enough Junk"));
        assert(!pe_ws_rebuild(2, &b, NULL) && !pe_ws_rebuild(-1, &b, NULL));
        assert(pe_ws_rebuild_cross(0, msg, sizeof msg) == PE_WS_ASK && !strcmp(msg, "Rebuild Medicine 1 for 40 Junk?"));
        assert(pe_ws_rebuild_cross(1, msg, sizeof msg) == PE_WS_REFUSE && !strcmp(msg, "Not enough Junk"));
        assert(pe_ws_rebuild_do(0, msg, sizeof msg) && rb_rebuilt == 0);
        s_economy_on = 0;
        assert(pe_ws_rebuild(0, &b, NULL) && b.blocked && !strcmp(b.blocked, "Junk economy is off"));
        s_economy_on = 1;
        s_rebuild.preview = NULL;
        assert(pe_ws_rebuild(0, &b, &rec) && rec.iconId == 0 && rec.tailCount == 0);
        s_rebuild_on = 0;
        assert(pe_ws_rebuild_count() == 0 && !pe_ws_rebuild(0, &b, NULL));
        assert(!pe_ws_rebuild_do(0, msg, sizeof msg) && !strcmp(msg, "Mod Catalog is off"));
    }

    puts("Workshop checks passed: services, Wardrobe only when offered, item and effect rows, gray reasons, owned mark, ask before every purchase, Rebuild rows, stats and refusals.");
    return 0;
}
