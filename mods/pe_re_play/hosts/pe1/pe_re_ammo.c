/* Resident Evil 2's ammo types for Parasite Eve's guns (service "ammo_types", host/ammo_types_api.h).
 * RE2 gives each weapon its own ammunition; while RE2 is played, each kind of PE gun Aya carries takes
 * the RE2 ammo closest to it. PE has no Magnum: its rifles stand in for RE2's Magnum. Rocket
 * launchers keep PE's Rockets pool, Maeda's gun his own bullets. Registered once the service is up. */
#include "ammo_types_api.h"
#include "pe_mod_api.h"
#include "pe_re_stage.h"

static const PeAmmoType k_re2_types[] = {
    {"re2.handgun", "Handgun Bullets", PE_AMMO_POOL_BULLETS, PE_AMMO_KIND_HANDGUN},
    {"re2.shotgun", "Shotgun Shells", PE_AMMO_POOL_BULLETS, PE_AMMO_KIND_SHOTGUN},
    {"re2.smg", "Sub-machine Gun Ammo", PE_AMMO_POOL_BULLETS, PE_AMMO_KIND_MACHINE_GUN},
    {"re2.magnum", "Magnum Rounds", PE_AMMO_POOL_BULLETS, PE_AMMO_KIND_RIFLE},
    {"re2.grenade", "Grenade Rounds", PE_AMMO_POOL_BULLETS, PE_AMMO_KIND_GRENADE},
    {"re2.rocket", "Rockets", PE_AMMO_POOL_ROCKETS, PE_AMMO_KIND_ROCKET},
};
static const PeAmmoScheme k_re2 = {"re2", "Resident Evil 2", sizeof k_re2_types / sizeof *k_re2_types, k_re2_types};

void pe_re_ammo_register(void) {
    static int registered;
    if (registered) return;
    const PeAmmoTypesApi *api = pe_service_get(PE_AMMO_TYPES_SERVICE, PE_AMMO_TYPES_SERVICE_VERSION);
    if (api && api->register_scheme && !api->register_scheme(&k_re2)) registered = 1;
}
