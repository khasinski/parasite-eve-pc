/* Resident Evil 2 items: the item table (names, inventory icons) as an ItemTable record (pe_imp_items.c),
 * built when COMMON/DATA/ITEMALL.PIX (the icons) is scanned. Read only.
 * Names: IntelOrca's biohazard-utils, Bio2ConstantTable.cs g_itemNames, Copyright (c) 2022-2023 Ted
 * John, MIT License. Item number = index. Which icon each item uses isn't known yet. */
#include "pe_imp_items.h"
#include "pe_imp_pictures.h"
#include "pe_src_re2.h"

static const char *const k_names[] = {
    "", "Knife", "Handgun Leon", "Handgun Claire",
    "Custom Handgun", "Magnum", "Custom Magnum", "Shotgun",
    "Custom Shotgun", "Grenade Launcher Explosive", "Grenade Launcher Flame", "Grenade Launcher Acid",
    "Bowgun", "Colt SAA", "Sparkshot", "SMG",
    "Flamethrower", "Rocket Launcher", "Gatling Gun", "Beretta",
    "Handgun Ammo", "Shotgun Ammo", "Magnum Ammo", "Fuel Tank",
    "Explosive Rounds", "Flame Rounds", "Acid Rounds", "SMG Ammo",
    "Sparkshot Ammo", "Bowgun Ammo", "Ink Ribbon", "Small Key",
    "Handgun Parts", "Magnum Parts", "Shotgun Parts", "F Aid Spray",
    "Antivirus Bomb", "Chemical A Cw32", "Herb G", "Herb R",
    "Herb B", "Herb GG", "Herb GR", "Herb GB",
    "Herb GGG", "Herb GGB", "Herb GRB", "Lighter",
    "Lockpick", "Photo Sherry", "Valve Handle", "Red Jewel",
    "Red Card", "Blue Card", "Serpent Stone", "Jaguar Stone",
    "Jaguar Stone L", "Jaguar Stone R", "Eagle Stone", "Bishop Plug",
    "Rook Plug", "Knight Plug", "King Plug", "Weapon Box Key",
    "Detonator", "C4 Explosive", "C4 Detonator", "Crank",
    "Film A", "Film B", "Film C", "Unicorn Medal",
    "Eagle Medal", "Wolf Medal", "Cog", "Manhole Opener",
    "Main Fuse", "Fuse Case", "Vaccine", "Vaccine Cart",
    "Film D", "Vaccine Base", "G Virus", "Special Key",
    "Joint Plug Blue", "Joint Plug Red", "Cord", "Photo Ada",
    "Cabin Key", "Spade Key", "Diamond Key", "Heart Key",
    "Club Key", "Down Key", "Up Key", "Power Room Key",
    "MO Disk", "Umbrella Key Card", "Master Key", "Platform Key",
    "", "", "", "",
    "Chris Diary", "Federal Police Report", "Memoto Leon", "Police Memorandum",
    "Operation Report1", "Mail To The Chief1", "Mail To The Chief2", "Secretary Diary A",
    "Secretary Diary B", "Operation Report2", "User Registration", "Developed Film A",
    "Developed Film B", "Developed Film C", "Patrol Report", "Watchman Diary",
    "Chief Diary", "Sewer Manager Diary", "Sewer Manager Fax", "Developed Film D",
    "Vaccine Synthesis", "Lab Security Manual", "P Epsilon Report", "Hint Files1",
    "Hint Files2",
};

static int read_items(const uint8_t *data, uint32_t size, const char *path, PeImpSink *sink, char *why, size_t n) {
    (void)path;
    return pe_imp_emit_item_table(sink, PE_IMP_RE2, k_names, sizeof k_names / sizeof k_names[0], 0, data, size, why, n);
}

const PeImpReader g_re2_items_ps1 = {
    .api = {PE_IMP_API_VERSION, sizeof(PeImpReader)},
    .game = "re2", .platform = "ps1", .format = "items",
    .version = 1, .produces = PE_IMP_ITEMTABLE, .match = "COMMON/DATA/ITEMALL.PIX",
    .tags = "items", .read = read_items
};

/* The item pictures in colour: COMMON/DATA/ITPS.ITP, a TIM every 0x3000 bytes (100 of them), 112 x 72, 8 bits, each with
 * its own palette, as the inventory shows them. Cataloged as Pictures "items/pictures"; picture i is
 * item i (checked: 1 the knife, 2 the handgun, 7 the shotgun). */
static int read_pictures(const uint8_t *data, uint32_t size, const char *path, PeImpSink *sink, char *why, size_t n) {
    (void)path;
    return pe_imp_emit_tims(sink, "items/pictures", "Item pictures", data, size, 0x3000, why, n);
}

const PeImpReader g_re2_item_pictures_ps1 = {
    .api = {PE_IMP_API_VERSION, sizeof(PeImpReader)},
    .game = "re2", .platform = "ps1", .format = "item-pictures",
    .version = 1, .produces = PE_IMP_PICTURES, .match = "COMMON/DATA/ITPS.ITP",
    .tags = "items", .read = read_pictures
};
