/* Resident Evil items: the item table (names, inventory icons) as an ItemTable record (pe_imp_items.c),
 * built when <dir>/DATA/ITEM_ALL.PIX (the icons) is scanned. Read only.
 * Names: IntelOrca's biohazard-utils, Bio1ConstantTable.cs g_itemNames, Copyright (c) 2022-2023 Ted
 * John, MIT License. Item number = index. Icon i is item i. */
#include "pe_imp_items.h"
#include "pe_src_re1.h"

static const char *const k_names[] = {
    "", "Combat Knife", "Beretta", "Shotgun",
    "DumDum Colt", "Colt Python", "FlameThrower", "Bazooka Acid",
    "Bazooka Explosive", "Bazooka Flame", "Rocket Launcher", "Clip",
    "Shells", "DumDum Rounds", "Magnum Rounds", "FlameThrower Fuel",
    "Explosive Rounds", "Acid Rounds", "Flame Rounds", "Empty Bottle",
    "Water", "Umb No. 2", "Umb No. 4", "Umb No. 7",
    "Umb No. 13", "Yellow 6", "NP-003", "V-Jolt",
    "Broken Shotgun", "Square Crank", "Hex Crank", "Wood Emblem",
    "Gold Emblem", "Blue Jewel", "Red Jewel", "Music Notes",
    "Wolf Medal", "Eagle Medal", "Chemical", "Battery",
    "MO Disk", "Wind Crest", "Flare", "Slides",
    "Moon Crest", "Star Crest", "Sun Crest", "Ink Ribbon",
    "Lighter", "Lock Pick", "Nameless (Can of Oil)", "Sword Key",
    "Armor Key", "Sheild Key", "Helmet Key", "Lab Key (1)",
    "Special Key", "Dorm Key (002)", "Dorm Key (003)", "C. Room Key",
    "Lab Key (2)", "Small Key", "Red Book", "Doom Book (2)",
    "Doom Book (1)", "F-Aid Spray", "Serum", "Red Herb",
    "Green Herb", "Blue Herb", "Mixed (Red+Green)", "Mixed (2 Green)",
    "Mixed (Blue + Green)", "Mixed (All)", "Mixed (Silver Color)", "Mixed (Bright Blue-Green)",
};

static int read_items(const uint8_t *data, uint32_t size, const char *path, PeImpSink *sink, char *why, size_t n) {
    (void)path;
    return pe_imp_emit_item_table(sink, PE_IMP_RE1, k_names, sizeof k_names / sizeof k_names[0], 1, data, size, why, n);
}

const PeImpReader g_re1_items_ps1 = {
    .api = {PE_IMP_API_VERSION, sizeof(PeImpReader)},
    .game = "re1", .platform = "ps1", .format = "items",
    .version = 1, .produces = PE_IMP_ITEMTABLE, .match = "*/DATA/ITEM_ALL.PIX",
    .tags = "items", .read = read_items
};
