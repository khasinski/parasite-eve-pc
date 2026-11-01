#ifndef PE_PAGES_H
#define PE_PAGES_H

/* Where the PE Enhancements rows live in the Mods panel (F1). Every feature registers under one
 * of these, so the panel reads the same however the mods are combined:
 *
 *   PE Enhancements
 *     Movement            always run, speeds                        (pe_qol)
 *     Combat              rules, mod effects, Parasite Energy, DPS  (pe_rebalance)
 *     Economy             Junk, difficulty, Catalog, Dismantle,      (pe_economy)
 *                         Rebuild
 *     Inventory           key item pocket, bag sorting              (pe_qol)
 *     Collection          Bestiary, Wardrobe                        (pe_collection)
 *     Debug               tuning and test rows of every mod
 *
 * The sub-page strings are passed as `sub` to pe_menu_register_* while PE_MENU_PAGE is the
 * current page; pe_pages_describe() gives each page its description and order. */

/* The PE Enhancements suite's version, one for all of its mods: they are released together. */
#define PE_ENHANCEMENTS_VERSION "0.10.0"

#define PE_PAGE_MOVEMENT       "Movement"
#define PE_PAGE_COMBAT         "Combat"
#define PE_PAGE_MOD_EFFECTS    "Combat/Mod effects"
#define PE_PAGE_PE_BALANCE     "Combat/Parasite Energy"
#define PE_PAGE_QUICK_POWERS    "Combat/Parasite Energy/Quick powers"
#define PE_PAGE_COMBINE         "Combat/Combine items"
#define PE_PAGE_SMOKE           "Combat/Smoke Grenades"
#define PE_PAGE_ECONOMY        "Economy"
#define PE_PAGE_REQUISITION    "Economy/Requisition"
#define PE_PAGE_CATALOG        "Economy/Mod Catalog"
#define PE_PAGE_DISMANTLE      "Economy/Dismantle"
#define PE_PAGE_REBUILD        "Economy/Rebuild"
#define PE_PAGE_INVENTORY       "Inventory"
#define PE_PAGE_KEYS            "Inventory/Key items"
#define PE_PAGE_COLLECTION      "Collection"
#define PE_PAGE_BESTIARY        "Collection/Bestiary"
#define PE_PAGE_WARDROBE        "Collection/Wardrobe"
#define PE_PAGE_DEBUG          "Debug"
#define PE_PAGE_DEBUG_COLLECTION "Debug/Collection"
#define PE_PAGE_DEBUG_LOOT     "Debug/Loot tuning"
#define PE_PAGE_DEBUG_CATALOG  "Debug/Mod Catalog"

/* Descriptions and order of the pages above (host/pe_pages.c; called by the host at start). */
void pe_pages_describe(void);

#endif
