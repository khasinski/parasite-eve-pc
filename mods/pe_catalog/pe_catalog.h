#ifndef PE_CATALOG_H
#define PE_CATALOG_H

/* The Mod Catalog's private interface, shared by its three files:
 *   pe_catalog.c        effects, prices and rules; install / remove; unlocks; the PECT save block
 *   pe_dismantle.c      Dismantle, Rebuild and Tool salvage
 *   pe_catalog_pages.c  the F1 pages (Mod Catalog, Dismantle, Rebuild, Debug)
 * The rest of the mod uses the calls in pe_catalog_mod.h (pe_cat_*, pe_rebuild_*). */

#include <stdint.h>

#include "pe_game.h"

enum { PE_GUN = 0, PE_ARMOR = 1 };

#define PE_CAT_MAX_MODS  10
#define PE_REBUILD_MAX   30
#define PE_PAYOUT_CAP    150u   /* no dismantle pays more */
#define PE_REBUILD_CAP   150u   /* nor does a rebuild cost more (except its floor, see pe_dismantle.c) */
#define PE_GUN_EFFECTS   20
#define PE_ARMOR_EFFECTS 20     /* highest armor effect; 16 is unused */

/* ---- state: unlocks and the Rebuild list (one save block, the recomp's PECT v1) ---- */
typedef struct PeRebuildEntry {
    uint8_t id;          /* base item id */
    uint8_t slots;       /* mod slot count */
    int16_t bonus[3];
    uint16_t payout;     /* Junk dismantling paid (the rebuild price's base) */
} PeRebuildEntry;

typedef struct PeCatalogState {
    uint32_t bits[2];                         /* unlocked effects, bit e, per PE_GUN / PE_ARMOR */
    uint8_t rebuild_count;
    uint8_t pad[3];
    PeRebuildEntry rebuild[PE_REBUILD_MAX];   /* newest first */
} PeCatalogState;

PeCatalogState *pe_catalog_state(void);
int pe_catalog_is_unlocked(int which, int effect);

/* ---- effects ---- */
const char *pe_cat_effect_name(int which, int effect);    /* "RATE OF FIRE 5" */
const char *pe_cat_effect_short(int which, int effect);   /* "ROF 5" */
int pe_cat_effect_tier(int which, int effect);            /* 0 common, 1 mid, 2 premium (+ Tool) */
int pe_cat_tier_price(int tier);                          /* after the preset and Bargain */
int pe_cat_rebuild_markup(void);                          /* percent of the payout */

/* ---- gear ---- */
const char *pe_cat_item_name(int id);                     /* ASCII name (pe_item_table.h) */
int pe_cat_bag_limit(void);
ItemDataRecord *pe_cat_bag_record(int slot);              /* equipment record in a bag slot, or NULL */
int pe_cat_record_type(const ItemDataRecord *rec);        /* PE_GUN, PE_ARMOR or -1 */
int pe_cat_mod_count(const ItemDataRecord *rec);
int pe_cat_bonus_sum(const ItemDataRecord *rec);
int pe_cat_carried(int id);                               /* in the bag or Wayne's storage */
int pe_cat_take_tool(int dry_run);                        /* one Tool (free with a kit) */

/* ---- messages: a toast, also kept for the pages' Result rows ---- */
void pe_cat_say(const char *text);
void pe_cat_note(const char *text);                       /* the Result rows only, no toast */
const char *pe_cat_last_said(void);
int pe_cat_ready(void);                                   /* a game with the economy on, else says why */

/* ---- feature registration (called from pe_economy.c through pe_catalog_register) ---- */
void pe_dismantle_register(const char *mod_id);
void pe_catalog_pages_register(void);

/* ---- pe_dismantle.c, for the pages ---- */
unsigned pe_dismantle_list_count(void);
const char *pe_dismantle_list_name(unsigned i);
int *pe_dismantle_choice(void);
uint32_t pe_dismantle_selected_payout(int *unlocks_out);  /* 0 when nothing is selected */
void pe_dismantle_run(void);                              /* press twice */
unsigned pe_rebuild_list_count(void);
const char *pe_rebuild_list_name(unsigned i);
int *pe_rebuild_choice(void);
void pe_rebuild_run(void);
void pe_dismantle_debug_unlock_all(void);
void pe_dismantle_debug_clear(void);
void pe_dismantle_debug_test_gun(void);
void pe_dismantle_debug_test_armor(void);

#endif
