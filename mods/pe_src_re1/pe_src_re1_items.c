/* Resident Evil items: the item table (names, inventory icons) as an ItemTable record (pe_imp_items.c),
 * built when <dir>/DATA/ITEM_ALL.PIX (the icons) is scanned. Read only.
 * Names: IntelOrca's biohazard-utils, Bio1ConstantTable.cs g_itemNames, Copyright (c) 2022-2023 Ted
 * John, MIT License. Item number = index. Icon k is item k + 1 (item 0 is "nothing"). */
#include <stdio.h>
#include <string.h>

#include "pe_imp_items.h"
#include "pe_imp_pictures.h"
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
    .version = 2, .produces = PE_IMP_ITEMTABLE, .match = "*/DATA/ITEM_ALL.PIX",
    .tags = "items", .read = read_items
};

/* The icons in colour: ITEM_ALL.PIX's 76 icons (40 x 30, 8 bits) with the status screen's palette,
 * row 2 of DATA/STATUS.TIM's (found by eye: rows 0 and 1 give wrong colours). Cataloged as
 * Pictures "items/icons"; picture k is item k + 1 (the first is the combat knife, item 1). */
static int find_status(PeImpDisc *d, const char *path) {
    char sibling[PE_IMP_PATH_MAX];
    const char *slash = strrchr(path, '/');
    if (!slash || snprintf(sibling, sizeof sibling, "%.*s/STATUS.TIM", (int)(slash - path), path) >= (int)sizeof sibling)
        return -1;
    return pe_imp_disc_find(d, sibling);
}

static int read_icons(const uint8_t *icons, uint32_t size, const uint8_t *status, uint32_t status_size, const char *path,
                      PeImpSink *sink, char *why, size_t n) {
    (void)path;
    if (!status || status_size < 20 + 3 * 512 || status[0] != 0x10 || status[4] != 9 || status[16] != 0 || status[17] != 1 ||
        status[18] < 3)
        return 0;                                                       /* not the palette we know */
    return pe_imp_emit_pix8(sink, "items/icons", "Item icons", icons, size, status + 20 + 2 * 512, 40, 30, why, n);
}

const PeImpReader g_re1_item_icons_ps1 = {
    .api = {PE_IMP_API_VERSION, sizeof(PeImpReader)},
    .game = "re1", .platform = "ps1", .format = "item-icons",
    .version = 1, .produces = PE_IMP_PICTURES, .match = "*/DATA/ITEM_ALL.PIX",
    .tags = "items", .read_pair = read_icons, .find_companion = find_status
};
