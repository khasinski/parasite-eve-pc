#ifndef AMMO_TYPES_API_H
#define AMMO_TYPES_API_H

/* "ammo_types" v1 (pe_ammo_types): separate ammo per weapon type.
 *
 * Parasite Eve gives every ordinary gun the same Bullets. A game played through Parasite Eve (an
 * imported Resident Evil, or a mod's own rules) can register a scheme: a list of ammo types, each
 * used by some PE weapon kinds and merging into one of PE's three native pools when the scheme is
 * off. While a scheme is in force, each type keeps its own count, held in one of the game's nine
 * ammo records while needed (src/main/item via PE_HOOK_AMMO_POOL: the gun in hand and the bag's guns
 * first), so the HUD, Reload and pickups follow the gun in hand.
 *
 * The scheme in force follows what is being played: request() sets it, and an imported game being
 * played through RE play asks for the scheme with its game id ("re2"). Switching only happens in
 * the free field; leaving a scheme merges its pools back into the native ones and remembers the
 * split, entering it splits them again. F1 > Ammo turns the whole thing on and off. */

#include <stddef.h>
#include <stdint.h>

#define PE_AMMO_TYPES_SERVICE "ammo_types"
#define PE_AMMO_TYPES_SERVICE_VERSION 1

enum { PE_AMMO_TYPES_MAX = 16 };

/* PE weapon kinds (the item table's kind byte), as bits for PeAmmoType.weapon_kinds. */
enum {
    PE_AMMO_KIND_HANDGUN = 1u << 1, PE_AMMO_KIND_SHOTGUN = 1u << 2, PE_AMMO_KIND_MACHINE_GUN = 1u << 3,
    PE_AMMO_KIND_RIFLE = 1u << 4, PE_AMMO_KIND_GRENADE = 1u << 5, PE_AMMO_KIND_ROCKET = 1u << 6,
    PE_AMMO_KIND_MAEDA = 1u << 7
};
/* PE's native pools. */
enum { PE_AMMO_POOL_BULLETS, PE_AMMO_POOL_ROCKETS, PE_AMMO_POOL_MAEDA };

typedef struct PeAmmoType {
    const char *id;            /* stable: "re2.shotgun" (saved as its hash; never rename) */
    const char *label;         /* "Shotgun Shells" */
    uint8_t native_pool;       /* PE_AMMO_POOL_*: where it merges when the scheme is off */
    uint8_t weapon_kinds;      /* PE_AMMO_KIND_* bits of the guns that fire it */
} PeAmmoType;

typedef struct PeAmmoScheme {
    const char *id;            /* "re2": a game id, or a mod's own */
    const char *name;          /* "Resident Evil 2" */
    int count;                 /* types, at most PE_AMMO_TYPES_MAX; more than the six spare records is fine */
    const PeAmmoType *types;   /* must stay valid while the game runs */
} PeAmmoScheme;

struct ItemDataRecord;
/* Return the stable type ID for a specific weapon instance, or NULL to use kind routing. */
typedef const char *(*PeAmmoWeaponType)(const struct ItemDataRecord *weapon);

typedef struct PeAmmoTypesApi {
    size_t struct_size;
    int (*register_scheme)(const PeAmmoScheme *scheme);   /* 0 registered; -1 refused (logged) */
    /* the scheme wanted besides an imported game's own: its id, or NULL for none */
    void (*request)(const char *scheme_id);
    const char *(*active)(void);                         /* the scheme in force now, or NULL */
    int (*rounds)(const char *type_id);                  /* in its pool; -1 when not in force */
    int (*add)(const char *type_id, int rounds);         /* rounds added (capped at 999); -1 not in force */
    void (*fill)(int rounds);                            /* add to every type in force (or PE's pools) */
    int (*register_weapon_scheme)(const PeAmmoScheme *scheme, PeAmmoWeaponType weapon_type);
} PeAmmoTypesApi;

#endif
