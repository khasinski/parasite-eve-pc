/* Resident Evil 3 items: the item table (names, inventory icons) as an ItemTable record (pe_imp_items.c),
 * built when CD_DATA/ETC/ITEMI.PIX (the icons) is scanned. Read only.
 * Names: IntelOrca's biohazard-utils, Bio3ConstantTable.cs g_itemNames, Copyright (c) 2022-2023 Ted
 * John, MIT License. Item number = index. Which icon each item uses isn't known yet. */
#include "pe_imp_items.h"
#include "pe_imp_pictures.h"
#include "pe_src_re3.h"

static const char *const k_names[] = {
    "", "Combat Knife", "Handgun Sigpro", "Handgun Beretta",
    "Shotgun Benelli", "Magnum SW", "Grenade Launcher (Burst)", "Grenade Launcher (Flame)",
    "Grenade Launcher (Acid)", "Grenade Launcher (Freeze)", "Rocket Launcher", "Gatling Gun",
    "Mine Thrower", "Handgun Eagle", "Rifle M4A1 (Manual)", "Rifle M4A1 (Auto)",
    "Shotgun M37", "Handgun Sigpro (Enhanced)", "Handgun Beretta (Enhanced)", "Shotgun Benelli (Enhanced)",
    "Mine Thrower (Enhanced)", "Handgun Ammo", "Magnum Ammo", "Shotgun Ammo",
    "Grenade Rounds", "Flame Rounds", "Acid Rounds", "Freeze Rounds",
    "Mine Thrower Ammo", "Rifle Ammo", "Handgun Enhanced Ammo", "Shotgun Enhanced Ammo",
    "First Aid Spray", "Herb (G)", "Herb (B)", "Herb (R)",
    "Herb (GG)", "Herb (GB)", "Herb (GR)", "Herb (GGG)",
    "Herb (GGB)", "Herb (GRB)", "First Aid Spray Box", "Crank",
    "Medal (Red)", "Medal (Blue)", "Medal (Gold)", "STARS Card (Jill)",
    "Oil Can", "Battery", "Fire Hook", "Power Cable",
    "Fuse", "Broken Fire Hose", "Oil Additive", "Card case (Brad)",
    "STARS Card (Brad)", "Machine Oil", "Mixed Oil", "Unknown Steel Chain",
    "Wrench", "Iron Pipe", "Unknown Cylinder", "Fire Hose",
    "Tape Recorder", "Lighter (Oil)", "Lighter (No Oil)", "Lighter",
    "Gem (Green)", "Gem (Blue)", "Ball (Amber)", "Ball (Obsidian)",
    "Ball (Crystal)", "Remote Control (No Batteries)", "Remote Control (Batteries)", "AA Batteries",
    "Gear (Gold)", "Gear (Silver)", "Gear (Chronos)", "Bronze Book",
    "Bronze Compass", "Vaccine Medium", "Vaccine Base", "",
    "", "Vaccine", "", "",
    "Medium Base", "EAGLE Parts (A)", "EAGLE Parts (B)", "M37 Parts (A)",
    "M37 Parts (B)", "", "Chronos Chain", "Rusted Crank",
    "Card Key", "Gunpowder (A)", "Gunpowder (B)", "Gunpowder (C)",
    "Gunpowder (AA)", "Gunpowder (BB)", "Gunpowder (AC)", "Gunpowder (BC)",
    "Gunpowder (CC)", "Gunpowder (AAA)", "Gunpowder (AAB)", "Gunpowder (BBA)",
    "Gunpowder (BBB)", "Gunpowder (CCC)", "Infinite Bullets", "Water Sample",
    "System Disk", "Dummy Key", "Lockpick", "Warehouse Key",
    "Sickroom Key", "Emblem Key", "Keyring With 4 Unknown Keys", "Clock Tower Key (Bezel)",
    "Clock Tower Key (Winder)", "Chronos Key", "", "Park Key (Front)",
    "Park Key (Graveyard)", "Park Key (Rear)", "Facility Key (No barcode)", "Facility Key (Barcode)",
    "Boutique Key", "Ink Ribbon", "Reloading Tool", "Game Instructions (A)",
    "Game Instructions (B)", "Game Instructions (A2)",
};

static int read_items(const uint8_t *data, uint32_t size, const char *path, PeImpSink *sink, char *why, size_t n) {
    (void)path;
    return pe_imp_emit_item_table(sink, PE_IMP_RE3, k_names, sizeof k_names / sizeof k_names[0], 0, data, size, why, n);
}

const PeImpReader g_re3_items_ps1 = {
    .api = {PE_IMP_API_VERSION, sizeof(PeImpReader)},
    .game = "re3", .platform = "ps1", .format = "items",
    .version = 1, .produces = PE_IMP_ITEMTABLE, .match = "CD_DATA/ETC/ITEMI.PIX",
    .tags = "items", .read = read_items
};

/* The item pictures in colour: CD_DATA/ETC/ITEMG.PIX, a TIM every 0x2800 bytes (134 of them), 112 x 72, 8 bits, each with
 * its own palette, as the inventory shows them. Cataloged as Pictures "items/pictures"; picture i is
 * item i (checked: 1 the knife, 2 the handgun, 7 the shotgun). */
static int read_pictures(const uint8_t *data, uint32_t size, const char *path, PeImpSink *sink, char *why, size_t n) {
    (void)path;
    return pe_imp_emit_tims(sink, "items/pictures", "Item pictures", data, size, 0x2800, why, n);
}

const PeImpReader g_re3_item_pictures_ps1 = {
    .api = {PE_IMP_API_VERSION, sizeof(PeImpReader)},
    .game = "re3", .platform = "ps1", .format = "item-pictures",
    .version = 1, .produces = PE_IMP_PICTURES, .match = "CD_DATA/ETC/ITEMG.PIX",
    .tags = "items", .read = read_pictures
};
