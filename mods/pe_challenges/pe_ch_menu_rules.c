/* Challenges that only take battle menu tabs away (pe_ch_tabs.c does the work). */
#include <stddef.h>

#include "pe_challenges.h"

const PeChallengeDef g_ch_no_items = {
    CH_NO_ITEMS, "No items in battle", "No items",
    "No Item tab in battle. Items still work in the field.",
    NULL, 0, CH_TAB_ITEM, 0, NULL
};

const PeChallengeDef g_ch_no_escape = {
    CH_NO_ESCAPE, "No escape", "No escape",
    "No Escape tab in battle: every fight must be won.",
    NULL, 0, CH_TAB_ESCAPE, 0, NULL
};

const PeChallengeDef g_ch_locked_loadout = {
    CH_LOCKED_LOADOUT, "Locked loadout", "Locked loadout",
    "No gun or armor changes in battle. Change gear in the field.",
    NULL, 0, CH_TAB_WEAPON | CH_TAB_ARMOR, 0, NULL
};
