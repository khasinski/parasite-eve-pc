#include "pe_mod_menu.h"
#include "pe_pages.h"

void pe_pages_describe(void) {
    pe_menu_top_page(PE_MENU_PAGE, "Version " PE_ENHANCEMENTS_VERSION ". Quality of life, rebalance, economy and collection, by topic.", 10);
    pe_menu_begin(PE_MENU_PAGE);
    pe_menu_page(PE_PAGE_MOVEMENT, "Always run and how fast Aya moves.", 10);
    pe_menu_page(PE_PAGE_COMBAT, "Damage rules, gun mods and Parasite Energy.", 20);
    pe_menu_page(PE_PAGE_MOD_EFFECTS, "How gun and armor mods behave in battle.", 10);
    pe_menu_page(PE_PAGE_PE_BALANCE, "Heals and Parasite Energy costs.", 20);
    pe_menu_page(PE_PAGE_QUICK_POWERS, "Choose which PE powers cost half a turn bar.", 10);
    pe_menu_page(PE_PAGE_COMBINE, "Combine carried medicines before a battle.", 30);
    pe_menu_page(PE_PAGE_SMOKE, "Buy an escape item for ordinary battles.", 40);
    pe_menu_page(PE_PAGE_BP, "Bonus Points upgrades, refund policy and playstyle guidance.", 25);
    pe_menu_page(PE_PAGE_DEBUG_BP, "BP screen previews and unfinished rules-preset previews.", 40);
    pe_menu_page(PE_PAGE_ECONOMY, "Junk, difficulty, and Wayne's Workshop services.", 30);
    pe_menu_page(PE_PAGE_REQUISITION, "Buy supplies and economy effects with Junk.", 5);
    pe_menu_page(PE_PAGE_CATALOG, "Install unlocked effects on your equipped gun or armor.", 10);
    pe_menu_page(PE_PAGE_DISMANTLE, "Break down spare gear for Junk and new effects.", 20);
    pe_menu_page(PE_PAGE_REBUILD, "Buy dismantled gear back.", 30);
    pe_menu_page(PE_PAGE_ECON_STORAGE, "Custom playstyle: where normal Junk goes.", 40);
    pe_menu_page(PE_PAGE_ECON_INCOME, "Custom playstyle: extra Junk for won battles.", 50);
    pe_menu_page(PE_PAGE_ECON_BONUS, "Custom playstyle: bonus rare items and bundles.", 60);
    pe_menu_page(PE_PAGE_ECON_REDEEM, "Custom playstyle: turning bundles into Junk.", 70);
    pe_menu_page(PE_PAGE_ECON_LEGACY, "Enhanced's Difficulty. The other playstyles ignore it.", 80);
    pe_menu_page(PE_PAGE_INVENTORY, "Key-item pocket, remembered bag sorting and inventory counters.", 40);
    pe_menu_page(PE_PAGE_KEYS, "Browse pocketed keys and story items.", 10);
    pe_menu_page(PE_PAGE_COLLECTION, "Bestiary and Wardrobe, kept across every game.", 50);
    pe_menu_page(PE_PAGE_BESTIARY, "Creatures you've met: weaknesses and loot.", 10);
    pe_menu_page(PE_PAGE_WARDROBE, "Aya's outfits, bought once with Junk.", 20);
    pe_menu_page(PE_PAGE_DEBUG, "Tuning and test tools. Changes the balance.", 90);
    pe_menu_page(PE_PAGE_DEBUG_LOOT, "Junk amounts, rare drop chances, debug Junk.", 10);
    pe_menu_page(PE_PAGE_DEBUG_CATALOG, "Prices, unlock-all and test gear.", 20);
    pe_menu_page(PE_PAGE_DEBUG_COLLECTION, "Reset Bestiary and Wardrobe unlocks.", 30);
    pe_menu_begin(0);
}
