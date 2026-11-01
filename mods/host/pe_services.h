#ifndef PE_SERVICES_H
#define PE_SERVICES_H

/* The services mods offer each other (pe_service_publish / pe_service_get), in one neutral
 * place so no mod includes another mod's headers. A service is a versioned table of functions;
 * a mod that doesn't find one carries on without it.
 *
 *   "junk"      v1  pe_economy  the Junk counter
 *   "catalog"   v1  pe_economy  armor effects unlocked in the Mod Catalog
 *   "keys"      v1  pe_qol      the key item pocket
 *   "wardrobe"  v1  pe_collection  costumes that can be worn now (for shops such as Wayne's)
 *   "bestiary"  v1  pe_collection  register creatures, map enemies to them, unlock entries
 *   "costumes"  v1  pe_collection  register costumes (the game's outfits or a mod's own models)
 *
 * Providers (an import pack, a randomizer) register at their init, so they list "pe_collection"
 * in their PeMod requires. Keys are stable names saved in the player's profile ("pe1/rat",
 * "re2/claire"): a key must never change once released; unlocks of packs that aren't loaded stay
 * in the profile untouched.
 */

#include <stddef.h>
#include <stdint.h>

/* Economy armor effects, in the game's unused armor effect numbers 17-20. */
#define PE_FX_BARGAIN    17 /* prices -15% */
#define PE_FX_SCAVENGER  18 /* battle Junk +50% */
#define PE_FX_LUCKY_FIND 19 /* pity builds twice as fast */
#define PE_FX_SALVAGER   20 /* dismantle Junk +25% */

typedef struct PeJunkApi {
    int (*enabled)(void);              /* "Junk economy" switch */
    uint32_t (*junk)(void);
    int (*spend)(uint32_t n);          /* 0 (and nothing spent) if short */
    void (*give)(uint32_t n);
    int (*armor_has)(int effect);      /* on the equipped armor */
    uint32_t (*price)(uint32_t base);  /* after preset and Bargain; callers cap after */
    int (*wayne_waiting)(void);        /* he holds 300+ but can't build before Day 4 */
    int (*handin_blocked)(void);       /* a hand-in needs one free bag slot */
} PeJunkApi;

typedef struct PeCatalogApi {
    int (*armor_unlocked)(int effect);
    int (*unlock_armor)(int effect);   /* 1 if it was new */
} PeCatalogApi;

typedef struct PeKeysApi {
    int (*take)(uint16_t item);        /* pocket a key; 1 means handled */
} PeKeysApi;

/* state: 0 for sale, 1 owned, 2 worn. Wayne's Workshop shows a Wardrobe row while published. */
typedef struct PeWardrobeApi {
    int (*count)(void);
    const char *(*name)(int k);
    unsigned (*price)(int k);
    int (*state)(int k);
    int (*pick)(int k, char *msg, size_t size);   /* buy or wear; msg says what happened */
} PeWardrobeApi;

/* ---- Bestiary ----
 * A creature's entry has three tiers: basics (met: first kill or any purchase), weaknesses (Scan,
 * 5 kills, defeating a boss, or Junk) and loot (15 kills, or Junk; bosses: Junk only). The info is
 * the provider's and may be replaced at any time (a randomizer's drops). Elements are the game's:
 * bits 1 heat, 2 frost, 4 cyanide, 8 acid, 16 tranquilizer. */
enum { PE_BESTIARY_MET = 1u, PE_BESTIARY_WEAK = 2u, PE_BESTIARY_LOOT = 4u };

typedef struct PeCreatureInfo {
    const char *key;          /* stable, unique: "pe1/rat"; saved in the profile */
    const char *name;
    const char *section;      /* the F1 Area filter: "Day 1", "Chrysler", "Resident Evil 2" */
    uint8_t boss, ex;         /* ex: the EX Game's version */
    uint16_t hp, defense;
    uint8_t weak, strong;     /* element bits */
    const char *drop, *steal; /* shown as given: "Medicine 1 90%, nothing 10%" */
    uint16_t price_weak, price_loot;   /* Junk */
} PeCreatureInfo;

/* Where a creature's (or later a costume's) 3D model comes from, for the Bestiary's model viewer. The
 * viewer reads it at run time; packs only say where. Kinds:
 *   PE_MODEL_ROOM      an appearance on the Parasite Eve disc: the room (map) and bank slot (actor)
 *                      it stands in; read from the player's own disc (its colours are that room's)
 *   PE_MODEL_SKIN      a skin folder in the game's player formats, pe_skins/<folder>/
 *   PE_MODEL_CALLBACK  the providing mod decodes it itself (another game's formats): load(user) */
enum { PE_MODEL_NONE, PE_MODEL_ROOM, PE_MODEL_SKIN, PE_MODEL_CALLBACK };
#define PE_BESTIARY_MAX_FORMS 8          /* forms per creature: form 0 the main body, then parts / phases */
struct PeModel;
typedef struct PeModelSource {
    uint8_t kind;
    uint8_t actor;                       /* PE_MODEL_ROOM */
    uint16_t map;                        /* PE_MODEL_ROOM: room; PE_MODEL_SKIN without folder: Aya bank */
    const char *folder;                  /* PE_MODEL_SKIN: pe_skins folder, or NULL for Aya bank */
    struct PeModel *(*load)(void *user); /* PE_MODEL_CALLBACK */
    void *user;
} PeModelSource;

/* Which creature is this enemy? (room number, the enemy's identity, its battle record); a creature
 * handle, or -1 to leave it to the next resolver and finally the mapped table. */
struct EnemyCombatant;
typedef int (*PeBestiaryResolver)(int room, unsigned identity, const struct EnemyCombatant *enemy);

typedef struct PeBestiaryApi {
    int (*add)(const PeCreatureInfo *info);              /* handle, or -1 (key taken, full); info must outlive the game */
    int (*find)(const char *key);                        /* handle or -1 */
    const PeCreatureInfo *(*info)(int handle);
    int (*replace)(int handle, const PeCreatureInfo *info);  /* same key; 0 if refused */
    int (*map)(int room, unsigned identity, int handle); /* enemy `identity` in `room` is this creature */
    int (*add_resolver)(PeBestiaryResolver fn);          /* asked first, newest first */
    unsigned (*state)(int handle);                       /* PE_BESTIARY_* bits */
    unsigned (*kills)(int handle);
    void (*unlock)(int handle, unsigned bits);           /* also announces it */
    /* Added to v1 (packs built before keep working): a creature's model forms for the viewer. */
    int (*set_model)(int handle, int form, const PeModelSource *src);   /* copied; 0 if refused */
    int (*model)(int handle, int form, PeModelSource *out);              /* 1 if that form exists */
} PeBestiaryApi;

/* ---- Costumes ----
 * A costume is one of:
 *   - one of the game's player bundles (bank 10 evening dress, 12 Aya, 13 T-shirt: g_SceneAreaType),
 *     optionally with a room pack (`bundle`: pe_skins/<bundle>/rooms.bin only), the room animations
 *     of Aya's other skeletons retargeted to this outfit's, swapped in when a room installs them;
 *   - a skin folder in the game's own player formats (bank -1, `bundle`): pe_skins/<bundle>/ beside
 *     the preferences, holding texture.bin, model.bin, day1-8.bin and optionally rooms.bin and
 *     meta.bin (the bridge's build_skin.py makes them); the Wardrobe loads it and swaps it in
 *     instantly, and hides the costume while its files are missing;
 *   - a model its provider puts in itself (bank -1, `load`): load(1) at the start of Aya's model
 *     load (PE_HOOK_PLAYER_BANK, entity_texture_phase 0) while it is worn, load(0) once when it
 *     comes off; the provider adds whatever other hooks its loader needs. */
typedef struct PeCostume {
    const char *key;          /* stable, unique: "pe1/tshirt"; saved in the profile */
    const char *name;         /* "Aya: T-shirt", "Claire (RE2)" */
    const char *group;        /* the F1 Group filter: "Aya's outfits", "Resident Evil" */
    uint16_t price;           /* Junk, paid once for every game */
    int16_t bank;             /* >= 0: the game's player bundle; -1: `bundle` or load() */
    int (*available)(void);   /* NULL: always; 0 hides it from every list (e.g. its files are missing) */
    void (*load)(int wearing);
    const char *bundle;       /* bank -1 without load(): the skin folder under pe_skins/;
                               * bank >= 0: an optional room pack folder (NULL: none) */
} PeCostume;

typedef struct PeCostumesApi {
    int (*add)(const PeCostume *costume);   /* handle, or -1 (key taken, full); must outlive the game */
    const PeCostume *(*worn)(void);         /* NULL: Aya's own clothes */
    int (*reload)(void);                    /* the game's same-room reload, so a change shows now; 0 if not now */
} PeCostumesApi;

/* Events (pe_event_id): "bestiary.unlock" (PeBestiaryEvent *), "wardrobe.worn" (const PeCostume *,
 * NULL for Aya's own). */
typedef struct PeBestiaryEvent {
    int handle;
    unsigned gained;          /* PE_BESTIARY_* bits new with this unlock */
    const char *why;          /* "kill", "scan", "junk", or a mod's own */
} PeBestiaryEvent;

#endif
