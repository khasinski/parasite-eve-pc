#ifndef PE_SERVICES_H
#define PE_SERVICES_H

/* The services mods offer each other (pe_service_publish / pe_service_get), in one neutral
 * place so no mod includes another mod's headers. A service is a versioned table of functions;
 * a mod that doesn't find one carries on without it.
 *
 *   "junk"      v1  pe_economy  the Junk counter
 *   "catalog"   v1  pe_catalog   armor effects unlocked in the Mod Catalog
 *   "keys"      v1  pe_qol      the key item pocket
 *   "stacks"    v1  pe_stacks   stack counts and stack-aware add/take for bag and crate
 *   "wardrobe"  v1  pe_wardrobe    costumes that can be worn now (for shops such as Wayne's)
 *   "bestiary"  v1  pe_collection  register creatures, map enemies to them, unlock entries
 *   "costumes"  v1  pe_wardrobe    register costumes (the game's outfits or a mod's own models)
 *   "costume_rules" v1 pe_economy  whether costumes must be unlocked before they are worn
 *   "wallet"    v1  pe_economy  battle rewards' fields in the Junk record (bridge findings D10)
 *   "rebuild"   v1  pe_catalog  dismantled gear to buy back (Wayne's Workshop)
 *   "phonecalls" v1 pe_phone    calls Aya can make after saving at a phone (call packs)
 *   "bp"        v1  pe_bp       BP upgrades: ranks, quotes, checked purchases, pauses (preview)
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

/* `wallet` v1: for the battle rewards only (bridge findings D10). The Junk wallet owns the
 * "pe_economy.loot" save record whole; the rewards' fields in it are copied in and out here, so
 * the wallet stays its only writer and keeps them as loaded while the rewards are off. None of
 * these spend, grant or save; the save host saves the record when the game saves. */
typedef struct PeRewardState {
    uint16_t hits[7];                   /* per enemy slot, this battle (the no-hit bonus) */
    uint16_t rare_dry, rare_carry;      /* pity progress; a rare that didn't fit the list */
    uint32_t rng;
    uint16_t charm_left;
    uint8_t econ_since, permit_since;   /* rares since an economy mod / a Mod Permit */
} PeRewardState;

struct PeEconState;
struct PeEconProgress;
typedef struct PeWalletApi {
    uint32_t struct_size;               /* sizeof(PeWalletApi) */
    void (*get_rewards)(PeRewardState *out);
    void (*put_rewards)(const PeRewardState *in);
    int (*preset)(void);                /* Difficulty: 0 Relaxed, 1 Normal, 2 Grind */
    /* Extra Junk for the battle being won, added before the items-found list is built: the wallet
     * shows it with vanilla Junk and steals as one "Junk xN" entry. */
    void (*battle_junk)(uint32_t n);
    /* The opt-in Economy path. policy: the applied state while it is active, else NULL, and then
     * nothing below may be used. room: Junk the wallet can still take whole this battle. */
    const struct PeEconState *(*policy)(void);
    void (*policy_commit)(const struct PeEconProgress *progress);
    uint32_t (*room)(void);
} PeWalletApi;

typedef struct PeCatalogApi {
    int (*armor_unlocked)(int effect);
    int (*unlock_armor)(int effect);   /* 1 if it was new */
} PeCatalogApi;

/* `rebuild` v1: the Mod Catalog's Rebuild list, newest first, for shop fronts (Wayne's Workshop).
 * rebuild() buys entry i back (Junk through the wallet); msg says what happened. */
struct ItemDataRecord;
typedef struct PeRebuildApi {
    int (*count)(void);
    uint8_t (*item)(int i);
    unsigned (*bought_slots)(int i);      /* slots added with Mod Permits before dismantling */
    uint32_t (*price)(int i);
    int (*rebuild)(int i, char *msg, size_t size);   /* 1 if rebuilt */
    /* Added to v1 (NULL from builds before): the record rebuild(i) would make (stats, bonuses,
     * slot count, empty slots), for a stats view; 0 if there is no entry i. Changes nothing. */
    int (*preview)(int i, struct ItemDataRecord *out);
} PeRebuildApi;

typedef struct PeKeysApi {
    int (*take)(uint16_t item);        /* pocket a key; 1 means handled */
} PeKeysApi;

/* list: 0 Aya's bag, 1 Wayne's crate. */
typedef struct PeStacksApi {
    int (*count)(int list, int slot);                /* copies in slot (0 if empty, >=1 if occupied) */
    int (*total)(int list, uint16_t item);           /* total copies of item in list */
    int (*can_add)(int list, uint16_t item);         /* 1 if list can accept 1 more copy of item */
    int (*add)(int list, uint16_t item, int prefer); /* 1 if added (stacks or uses prefer/free slot) */
    int (*take)(int list, int slot);                 /* consumes 1 copy from slot; clears slot on last */
} PeStacksApi;

/* state: 0 for sale, 1 owned, 2 worn. Wayne's Workshop shows a Wardrobe row while published. */
struct PeModelSource;   /* below */
typedef struct PeWardrobeApi {
    int (*count)(void);
    const char *(*name)(int k);
    unsigned (*price)(int k);
    int (*state)(int k);
    int (*pick)(int k, char *msg, size_t size);   /* buy or wear; msg says what happened */
    /* Added to v1 (NULL from builds before: no previews): outfit k's model for a model stage; 0
     * when it has none to show while browsing (one only its provider's own loader can make). */
    int (*preview)(int k, struct PeModelSource *src);
} PeWardrobeApi;

/* `costume_rules` v1: a mod's rule that costumes must be owned (or free) before they are worn
 * (bridge findings D8 §12, D6 §15.5). The Wardrobe asks; it never calls the provider's code
 * otherwise, and the provider never edits the Wardrobe's profile. With no provider running, or
 * active() 0 (the player turned the rule off), every installed costume can be worn, without
 * becoming owned. active() is read only: it never grants, spends or saves. */
typedef struct PeCostumeRulesApi {
    int (*active)(void);
} PeCostumeRulesApi;

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

/* Optional core guard for legacy mod frontends. A host without custom identities permits its
 * existing behavior. Native engine paths use PE_HOOK_EQUIPMENT_GUARD through their normal hook
 * boundary, so standalone native units never link against the extension implementation. */
typedef struct PeEquipmentGuardApi {
    uint32_t struct_size;
    int (*record_editable)(const struct ItemDataRecord *record);
} PeEquipmentGuardApi;

/* Host-owned next-save transactions. Provider adapters own their profile format. Their admit
 * and owned callbacks are pure; promote is idempotent and writes atomically. Promote runs only
 * after the native card commits, or while replaying receipts from a validated loaded card.
 * Pending unlocks are visible now, but must never be copied into independently saved profile
 * progress (e.g. an earned Bestiary kill must merge only committed/earned bits).
 */
typedef struct PeProfileTransactionAdapter {
    uint32_t struct_size;
    const char *key;
    unsigned supported_bits;
    int (*admit)(const char *key,unsigned bits);
    unsigned (*owned)(const char *key);
    int (*promote)(const char *key,unsigned bits);
} PeProfileTransactionAdapter;
typedef struct PeProfileTransactionsApi {
    uint32_t struct_size;
    int (*add)(const PeProfileTransactionAdapter *adapter); /* init, owning namespace */
    unsigned (*pending)(const char *adapter,const char *key);
    int (*buy)(const char *adapter,const char *key,unsigned bits,uint32_t junk_price);
} PeProfileTransactionsApi;

/* ---- "phonecalls" v1 (pe_phone): calls Aya can make from a save phone ----
 * A call is a list of lines, one dialogue box each (longer lines run on into more boxes). Text is
 * ASCII in the game's glyphs (letters, digits, space and . , : ! ? ' - + * % /); "{name}" is the
 * name the player gave Aya. Aya's lines are drawn "{name}: text", the caller's "caller: text".
 * Packs register at their init (list "pe_phone" in requires); the game's own calls are a pack too
 * (pe_phone/calls/pe1.txt, read as text; players add .txt files in their pe_calls folder). Keys are
 * saved in the player's save ("pe1/pizza"): never rename one.
 * Events: "phone.call_start" and "phone.call_end", ctx the PePhoneCall. */
enum { PE_CALL_AYA = 0, PE_CALL_CALLER = 1 };
enum { PE_CALL_SERIOUS = 0, PE_CALL_FUNNY = 1 };

typedef struct PePhoneLine {
    int speaker;                       /* PE_CALL_AYA or PE_CALL_CALLER */
    const char *text;
} PePhoneLine;

typedef struct PePhoneCall {
    const char *key;                   /* stable id under the pack's prefix: "pe1/nypd_desk" */
    const char *caller;                /* name shown for the other side: "NYPD Desk" */
    int tone;                          /* PE_CALL_SERIOUS or PE_CALL_FUNNY */
    const PePhoneLine *lines;
    int line_count;
    int weight;                        /* chance in the random pool; 0 counts as 1 */
    int once;                          /* at most once per playthrough */
    int day_from, day_to;              /* story days it may play (the save screen's Day); 0 = any */
    unsigned story_from, story_to;     /* finer: script variable 74 range; both 0 = any */
    const short *rooms;                /* phone rooms (map numbers) it may play at; NULL = any */
    int room_count;
    int priority;                      /* > 0: a set call that beats the random pool when it fits */
    const char *after;                 /* key of a call that must have been heard first, or NULL */
    const char *needs;                 /* conditions that must all hold, space-separated
                                          ("warner.supplies"); NULL = none. See add_condition. */
} PePhoneCall;

typedef struct PePhoneContext {
    int room;                          /* map number of the phone, or -1 */
    unsigned story;                    /* script variable 74 */
    int day;                           /* 1..6 */
    int tone_mask;                     /* bit 1 << PE_CALL_SERIOUS / PE_CALL_FUNNY allowed */
} PePhoneContext;

typedef struct PePhoneCallsApi {
    int (*add)(const PePhoneCall *call);            /* at init; the call must stay valid. -1: refused (logged) */
    const PePhoneCall *(*find)(const char *key);
    const PePhoneCall *(*pick)(const PePhoneContext *context);   /* NULL: nothing fits */
    int (*heard)(const char *key);                  /* heard this playthrough (once calls and the last few) */
    void (*reset)(void);                            /* forget this save's heard calls */
    /* A yes/no a call's `needs` can name, so a call can depend on another mod's feature without
     * either mod knowing the other: the mod that owns the feature offers it ("warner.supplies":
     * pe_shops, Warner helps out with the Junk economy on). Name: letters, digits, . _ -, up to 31.
     * Asked each time a call is picked; a name nobody offers never holds, so the call stays quiet
     * when that mod is off or missing. Any time after the service is up (a mod that may start
     * before pe_phone offers it from its first frame). 0 ok, -1 refused (logged). */
    int (*add_condition)(const char *name, int (*holds)(void));
} PePhoneCallsApi;

/* ---- "bp" v1 (pe_bp, BP Overhaul preview): what the game's Bonus Points buy ----
 * Reads the upgrades a save holds and buys, refunds or grants them through the same checked
 * transactions as the BP screens: everything that can refuse runs before any write, so a refusal
 * changes nothing. Earning BP stays the game's own; nothing here creates normal BP.
 *
 * Kinds and indexes (append only; keys are stable):
 *   PE_BP_DEFENSE  0 poison, 1 confusion, 2 darkness, 3 stiffness (Resistance), 4 fortitude
 *   PE_BP_MASTERY  0 heal, 1 detox, 2 medic, 3 barrier, 4 energy_shot, 5 scan, 6 slow, 7 haste,
 *                  8 confuse, 9 gene_heal, 10 preraise, 11 full_recover
 * Requests (quote / execute):
 *   "buy.def.<i>"  "buy.line.<i>"   next rank (a Mastery line needs its power learned)
 *   "reset.def.<i>" "reset.line.<i>" "reset.all"   refund at Wayne's prices: Junk, Junk economy on
 *   "set.fort.<0|1>"  Fortitude: 0 guards both effects, 1 saves charges for HP to 1
 *   "buy.gear.<record>.<stat>" "buy.aya.<5|6>"   the game's own 100 BP steps
 * Quotes and purchases need a loaded game outside battle (PE_BP_ERR_NOT_LOADED otherwise).
 *
 * Pauses: a mod that must switch effects off for a while (a challenge, a cutscene) calls
 * pause(its id, PE_BP_FEATURE_* mask); pause(id, 0) lifts it. While any pause holds, those
 * features act as if the player turned them off in F1: ranks stay, battle effects stop, and
 * purchases there are refused (PE_BP_ERR_INACTIVE). Pauses are not saved.
 *
 * Out-structs: set struct_size to sizeof your struct first; fields past it are not written, so a
 * mod built against this v1 keeps working when fields are appended.
 *
 * Events (pe_event_id):
 *   "bp.changed" (const PeBpChanged *)  after this mod changed BP, ranks or the pool, and after a
 *                                       save load or a New Game
 *   "bp.effect"  (const PeBpEffect *)   a rank just changed something in battle
 */
enum { PE_BP_DEFENSE = 0, PE_BP_MASTERY = 1 };
enum { PE_BP_DEFENSE_COUNT = 5, PE_BP_MASTERY_COUNT = 12 };
enum {
    PE_BP_FEATURE_RESISTANCE = 1, PE_BP_FEATURE_FORTITUDE = 2, PE_BP_FEATURE_MASTERY = 4,
    PE_BP_FEATURE_RESPEC = 8
};
enum {   /* append only */
    PE_BP_OK, PE_BP_ERR_NOT_LOADED, PE_BP_ERR_UNKNOWN, PE_BP_ERR_MAXED, PE_BP_ERR_LOCKED,
    PE_BP_ERR_BP, PE_BP_ERR_JUNK, PE_BP_ERR_OVERFLOW, PE_BP_ERR_EMPTY, PE_BP_ERR_NO_ECONOMY,
    PE_BP_ERR_BAG, PE_BP_ERR_UNCHANGED, PE_BP_ERR_STORE, PE_BP_ERR_INACTIVE
};
enum { PE_BP_CARRY_OFF, PE_BP_CARRY_KEEP, PE_BP_CARRY_CONVERT };   /* EX Game carryover (F1) */

typedef struct PeBpStatus {
    uint32_t struct_size;
    uint8_t loaded;          /* a game is loaded and its BP data was readable */
    uint8_t carry_mode;      /* PE_BP_CARRY_* */
    uint16_t full_resets;    /* Wayne's full resets this playthrough */
    uint32_t features;       /* PE_BP_FEATURE_* in effect (F1 rules minus pauses) */
    uint32_t paused;         /* PE_BP_FEATURE_* some mod paused */
    uint32_t bp;             /* unspent BP (the game's own counter) */
    uint32_t upgrade_pool;   /* upgrade-only BP: Resistance, Fortitude, Mastery only */
} PeBpStatus;

typedef struct PeBpQuote {
    uint32_t struct_size;
    int32_t error;                     /* PE_BP_* */
    uint32_t bp_cost, pool_cost;       /* taken from BP / from the upgrade-only pool */
    uint32_t bp_refund, pool_refund;   /* returned in all / the part that goes to the pool */
    uint32_t junk_fee;                 /* after the Economy preset */
    uint32_t bp_after, junk_after;
    int32_t rank_before, rank_after;
} PeBpQuote;

typedef struct PeBpChanged {
    const char *why;                   /* a request id, "grant", "load", "new_game", "ex_carry" */
    const char *by;                    /* mod id that asked, NULL for the player / the game */
    uint32_t bp_before, bp_after;
    uint32_t pool_before, pool_after;
} PeBpChanged;

typedef struct PeBpEffect {
    int32_t kind, index, rank;         /* PE_BP_DEFENSE / PE_BP_MASTERY, index, rank that acted */
    const char *what;                  /* "resisted", "fortitude", "preraise", "second_roll" */
} PeBpEffect;

typedef struct PeBpApi {
    uint32_t struct_size;                                   /* sizeof(PeBpApi) */
    int (*status)(PeBpStatus *out);                         /* 1, or 0 with loaded 0 */
    int (*rank)(int kind, int index);                       /* bought rank; -1 bad kind/index */
    int (*active_rank)(int kind, int index);                /* rank in effect: 0 while off or paused */
    int (*carried)(int kind, int index);                    /* ranks carried from the last run */
    int (*max_rank)(int kind, int index);                   /* 0 for a bad kind/index */
    const char *(*key)(int kind, int index);                /* stable: "poison", "heal"; NULL if bad */
    const char *(*name)(int kind, int index);               /* shown name: "Poison", "Heal" */
    uint32_t (*price)(int kind, int index, int rank);       /* BP for that rank (1-based); 0 if none */
    int (*quote)(const char *request, PeBpQuote *out);      /* PE_BP_*; changes nothing */
    /* Re-reads the game, re-quotes and commits, or changes nothing. by: the calling mod's id. */
    int (*execute)(const char *request, const char *by, PeBpQuote *out);
    const char *(*error_text)(int error);                   /* "Not enough BP" */
    /* Adds upgrade-only BP (a reward that only buys Resistance, Fortitude and Mastery). Saved with
     * the game. PE_BP_ERR_OVERFLOW past 99,999 (nothing added). */
    int (*grant_pool)(uint32_t bp, const char *by);
    int (*pause)(const char *mod_id, uint32_t features);    /* 0 ok, -1 refused (table full, no id) */
} PeBpApi;

#endif
