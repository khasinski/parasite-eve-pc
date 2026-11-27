#ifndef PE_CATALOG_MOD_H
#define PE_CATALOG_MOD_H

/* The Mod Catalog mod's calls, shared by its files (mods/pe_catalog). Until November 2026 part of
 * the economy mod; its save record keeps its name, "pe_economy.catalog". Other mods use the
 * "catalog" and "rebuild" services (host/pe_services.h) instead. */

#include <stddef.h>
#include <stdint.h>
#include "pe_services.h"   /* PE_FX_*, PeJunkApi, PeCatalogApi, PeRebuildApi */

/* The Junk wallet, through the "junk" service (pe_catalog_wallet.c). With no wallet running the
 * Catalog acts as with Junk economy off: paid actions are refused, never free. */
int pe_catw_enabled(void);
uint32_t pe_catw_junk(void);
int pe_catw_spend_junk(uint32_t n);
void pe_catw_give_junk(uint32_t n);
int pe_catw_armor_has(int effect);
uint32_t pe_catw_price(uint32_t base);

/* The Mod Catalog (pe_catalog.c, pe_dismantle.c). which: 0 = the equipped gun, 1 = the equipped armor.
 */
enum {
    PE_CAT_OK, PE_CAT_NO_GAME, PE_CAT_OFF, PE_CAT_NO_ITEM, PE_CAT_NO_SLOTS, PE_CAT_LOCKED,
    PE_CAT_ALREADY, PE_CAT_BAG, PE_CAT_JUNK, PE_CAT_TOOL, PE_CAT_EMPTY,
};
struct ItemDataRecord *pe_cat_record(int which);  /* the equipped gun / armor record, or NULL */
int pe_cat_slots(int which);
int pe_cat_slot_effect(int which, int slot);
int pe_cat_board_count(int which);
int pe_cat_board_effect(int which, int i);
int pe_cat_unlocked(int which, int e);
uint32_t pe_cat_price(int which, int e);
int pe_cat_needs_tool(int which, int e);
int pe_cat_is_bag_mod(int which, int e);
int pe_cat_install(int which, int slot, int e, int dry_run);
int pe_cat_remove(int which, int slot, int dry_run);
int pe_cat_replaces(int which, int slot, int e, int *out, int max);
int pe_catalog_armor_unlocked(int effect);
int pe_catalog_unlock_armor(int effect);
uint32_t pe_catalog_dismantle_preview(const struct ItemDataRecord *rec);
uint32_t pe_catalog_dismantle_reward(const struct ItemDataRecord *rec, int *unlocks_out);
int pe_rebuild_count(void);
uint8_t pe_rebuild_item(int i);
uint32_t pe_rebuild_price_index(int i);
unsigned pe_rebuild_bought_slots(int i);
int pe_rebuild_index(int i, char *msg, size_t msg_size);
int pe_rebuild_preview(int i, struct ItemDataRecord *out);

void pe_catalog_register(const char *mod_id);            /* pe_catalog.c */
void pe_catalog_menu_register(const char *mod_id);       /* pe_catalog_menu.c: the Catalog in Aya's menu */
void pe_discard_dismantle_register(const char *mod_id);  /* pe_discard_dismantle.c: Discard on gear dismantles */
void pe_armor_effect_display_register(const char *mod_id);

#endif
