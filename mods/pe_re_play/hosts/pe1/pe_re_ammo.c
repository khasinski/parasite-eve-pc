/* Resident Evil 2's ammo types for Parasite Eve's guns (service "ammo_types", host/ammo_types_api.h).
 * RE2 gives each weapon its own ammunition; while RE2 is played, each kind of PE gun Aya carries takes
 * the RE2 ammo closest to it. PE has no Magnum: its rifles stand in for RE2's Magnum. Rocket
 * launchers keep PE's Rockets pool, Maeda's gun his own bullets. Registered once the service is up. */
#include "ammo_types_api.h"
#include "pe_mod_api.h"
#include "pe_re_stage.h"
#include <string.h>

#include "pe_game.h"
#include "pe1/inventory.h"
#include "pe1/inventory_queries.h"
#include "pe1/inventory_slots.h"

static const PeAmmoType k_re2_types[] = {
    {"re2.handgun", "Handgun Bullets", PE_AMMO_POOL_BULLETS, PE_AMMO_KIND_HANDGUN},
    {"re2.shotgun", "Shotgun Shells", PE_AMMO_POOL_BULLETS, PE_AMMO_KIND_SHOTGUN},
    {"re2.smg", "Sub-machine Gun Ammo", PE_AMMO_POOL_BULLETS, PE_AMMO_KIND_MACHINE_GUN},
    {"re2.magnum", "Magnum Rounds", PE_AMMO_POOL_BULLETS, PE_AMMO_KIND_RIFLE},
    {"re2.grenade", "Grenade Rounds", PE_AMMO_POOL_BULLETS, PE_AMMO_KIND_GRENADE},
    {"re2.rocket", "Rockets", PE_AMMO_POOL_ROCKETS, PE_AMMO_KIND_ROCKET},
    {"re2.bolts", "Bowgun Bolts", PE_AMMO_POOL_BULLETS, 0},
    {"re2.spark", "Sparkshot Charges", PE_AMMO_POOL_BULLETS, 0},
    /* Appended (never reorder: the saved split follows this order). Not routed until their
     * weapons are: the Flamethrower and the launcher's round selection. */
    {"re2.fuel", "Fuel", PE_AMMO_POOL_BULLETS, 0},
    {"re2.flame", "Flame Rounds", PE_AMMO_POOL_BULLETS, 0},
    {"re2.acid", "Acid Rounds", PE_AMMO_POOL_BULLETS, 0},
};
static const PeAmmoScheme k_re2 = {"re2", "Resident Evil 2", sizeof k_re2_types / sizeof *k_re2_types, k_re2_types};

int pe_re_ammo_pickup(const char *game, uint16_t item, unsigned count) {
    static const char *const types[] = {
        [0x14] = "re2.handgun", [0x15] = "re2.shotgun", [0x16] = "re2.magnum",
        [0x18] = "re2.grenade", [0x19] = "re2.grenade", [0x1A] = "re2.grenade",
        [0x1B] = "re2.smg", [0x1C] = "re2.spark", [0x1D] = "re2.bolts"
    };
    if (!game || strcmp(game, k_re2.id) || item >= sizeof types / sizeof *types || !types[item]) return 0;
    const PeAmmoTypesApi *api = pe_service_get(PE_AMMO_TYPES_SERVICE, PE_AMMO_TYPES_SERVICE_VERSION);
    const int rounds = api && api->rounds ? api->rounds(types[item]) : -1;
    if (rounds >= 0) {
        if (rounds >= 999 || count > (unsigned)(999 - rounds)) return -1;
        return api->add(types[item], (int)count) == (int)count ? 1 : -1;
    }
    /* Per-type ammo is off (F1 > Ammo, or the mod is off): every gun shares PE's Bullets, as in
     * Parasite Eve. All of RE2's gun ammo here is Bullets-class (handguns to grenade launchers). */
    if (!pe_save_loaded()) return 0;
    ItemDataRecord *bullets = &g_KeyItemRecords[0];
    if (bullets->ammo >= 999 || count > (unsigned)(999 - bullets->ammo)) return -1;
    bullets->ammo = (u16)(bullets->ammo + count);
    {   /* its bag stack, if there's room for one */
        const int n = Inv_GetAyaSlotLimit();
        int at = -1, free_slot = -1;
        for (int i = 0; i < n && i < 50; ++i) {
            if (g_AyaInventoryItems[i] == 0x200) at = i;
            if (!g_AyaInventoryItems[i] && free_slot < 0) free_slot = i;
        }
        if (at < 0 && free_slot >= 0) g_AyaInventoryItems[free_slot] = 0x200;
    }
    return 1;
}

void pe_re_ammo_register(void) {
    static int registered;
    if (registered) return;
    const PeAmmoTypesApi *api = pe_service_get(PE_AMMO_TYPES_SERVICE, PE_AMMO_TYPES_SERVICE_VERSION);
    if (api && api->struct_size >= offsetof(PeAmmoTypesApi, register_weapon_scheme) + sizeof api->register_weapon_scheme &&
        api->register_weapon_scheme && !api->register_weapon_scheme(&k_re2, pe_re_weapon_ammo_type)) registered = 1;
}
