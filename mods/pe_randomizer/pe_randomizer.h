#ifndef PE_RANDOMIZER_H
#define PE_RANDOMIZER_H

/* Item randomizer: the spot table (pe_rand_spots.h, generated from the disc by
 * tools/spottable.py) and the seed generator (pe_rand_gen.c). The generator has no game
 * code in it, so the tests run it on its own. */

#include <stdint.h>

/* Raise PE_RAND_LOGIC_VERSION whenever the same seed would give a different layout (the generator,
 * its rules or PE_RAND_TABLE_VERSION). A save keeps the versions it was made with; a build that
 * doesn't know them leaves that game's items vanilla rather than moving them under the player. */
#define PE_RAND_LOGIC_VERSION 12
#define PE_RAND_TABLE_VERSION 14

enum { PE_RAND_EASY, PE_RAND_MEDIUM, PE_RAND_HARD, PE_RAND_DIFFICULTIES };

/* 1 (shipped): Medium and Hard move keys where the key logic allows (each its own rule). 0: key items stay in their
 * places on every difficulty. Changing it changes layouts: raise PE_RAND_LOGIC_VERSION with it. The
 * tests also build with it at 0 to keep the keys-stay path covered. */
#ifndef PE_RAND_MOVE_KEYS
#define PE_RAND_MOVE_KEYS 1
#endif

/* How a spot hands its item out. Only chests take a key from elsewhere or any item: on a full
 * bag Aya can swap and the chest keeps the item. Pickups and gifts take items that can't be lost
 * there (gift-safe ones), or their own vanilla item. */
enum { PE_RAND_CHEST, PE_RAND_PICKUP, PE_RAND_GIFT };
enum { PE_RAND_KEY = 1 };   /* spot flag: its vanilla item is a key item that unlocks something */

/* Key logic, from real play (tools/keylogic.py) or, until there is some, tools/keys-by-hand.txt
 * (a key only into a chest of its own room). Bit k is the k-th key spot of the table
 * (table order) and means both: key k is held, and key k's own spot has been reached (picking a key up
 * can also move the story on, and that spot still does it when it gives something else).
 *   needs   bits that reach the spot: an over-estimate (a run reached it with them).
 *           Unknown: no key goes there; a key spot with unknown needs counts as needing every key of
 *           the days before it, and keeps its own key (and every key of its day stays put).
 *   before  keys whose lock was first used after a run had reached this spot: only those may be put
 *           here, so a key never sits in a spot the player could already have walked past for good.
 *   late    keys of a later story day this chest may take on Medium: the story-windows walk opened
 *           it at every story value where the key's lock needed the key (tools/windowruns.py,
 *           spottable.py).
 *   hard    keys of its own or a later story day this chest may take on Hard (instead of before and
 *           late): at every story value where the key's lock asks, the player can walk to it through
 *           doors and the map (Hard's moved doors included) and it still gives (tools/hardreach.py). */
#define PE_RAND_NEEDS_UNKNOWN 0xFFFFFFFFu
#define PE_RAND_MAX_KEYS 32

typedef struct PeRandSpot {
    uint16_t vanilla;
    uint8_t kind, day, area, flags;   /* day: story day 1-6, 0 unknown; area: save label id */
    uint32_t needs, before, late, hard;
} PeRandSpot;

/* One script word to rewrite: room map, offset of the step in the room data, which operand holds
 * the item, and the step's 16 bytes as scanned (header, mode word, operands 0 and 1). */
typedef struct PeRandSite {
    uint16_t map, spot;
    uint32_t at;
    uint8_t operand;
    uint32_t expect[4];
} PeRandSite;

/* A GiveItem step of a spot: the mod logs which spot gives (rand_give rows), for keylogic.py. */
typedef struct PeRandGive {
    uint16_t map, spot;
    uint32_t at;
} PeRandGive;

/* A story gate (pe_rand_locks.h, generated): a key's item-count step (map, offset in the room data)
 * and the story bit its vanilla spot sets (V var | bit). */
typedef struct PeRandLock {
    uint16_t map;
    uint32_t at;
    uint16_t item, spot;
    uint8_t var;
    uint32_t bit;
} PeRandLock;

typedef struct PeRandResult {
    unsigned attempts;    /* tries used, 1 or more */
    unsigned keys_moved;
    int fallback;         /* 1: keys stayed vanilla after the tries ran out; 2: everything vanilla */
    int keys_locked;      /* keys can't move: PE_RAND_MOVE_KEYS is 0, or no story day has logic data
                           * for all its key spots */
} PeRandResult;

/* Fills out[0..count) with the item for each spot. Same inputs, same layout on every machine.
 * gift_safe(item): the item can go to a pickup or gift spot. Always returns a usable layout:
 * when no try passes the checks, keys stay vanilla, and then everything does. */
void pe_rand_generate(const PeRandSpot *spots, unsigned count, uint32_t seed, int difficulty,
                      int (*gift_safe)(unsigned item), uint16_t *out, PeRandResult *result);

/* The checks every layout passes (also used by the tests). 0 = fine, else a reason. */
const char *pe_rand_check(const PeRandSpot *spots, unsigned count, int difficulty,
                          int (*gift_safe)(unsigned item), const uint16_t *layout);

/* Battle randomizer, stats (pe_rand_battle.c): an enemy actor takes another actor's stat block (pe_rand_enemies.h,
 * generated by tools/enemyscan.py --blocks) and keeps its own model, AI, effects and creature id. */
#define PE_RAND_STATS 26
typedef struct PeRandEnemy {
    uint16_t room;
    uint8_t entity, creature;     /* entity: the actor's script entry (FieldActor type_id); creature: pe_collection's */
    uint32_t have, scaled;        /* bit i: PE_RAND_STAT_CODES[i] is set; its value is k of (N0xa * k) >> 16 */
    int32_t value[PE_RAND_STATS];
} PeRandEnemy;

/* Actor `entity` of room `room` takes the stat block of actor `from_entity` of room `from_room`, from its next
 * battle slot on, over what its script's setup wrote. 0 when either isn't in the table. Up to 320;
 * pe_rand_battle_clear drops them all. Randomize enemies maps each moved slot to its own block: the new foe
 * fights with the numbers of the one it replaced. */
int pe_rand_battle_force(unsigned room, unsigned entity, unsigned from_room, unsigned from_entity);
/* That forced actor runs the script of actor `ai_entity` of room `ai_room`: each attack power the script sets is
 * scaled by the stat block's attack power over the script's own (pe_rand_battle.c). 0 when not forced or unknown. */
int pe_rand_battle_attack(unsigned room, unsigned entity, unsigned ai_room, unsigned ai_entity);
void pe_rand_battle_clear(void);
void pe_rand_battle_register(void);   /* pe_rand_init: the PE_HOOK_ENEMY_FIELD / _POWER handlers */

/* Battle randomizer, models (pe_rand_model.c), test only too: actor `entity` of room `room` shows up with the
 * model, textures and animation rows of actor `from_entity` of room `from_room` (read from the disc at each
 * load of `room`). Both must be placed in the enemy VRAM blocks, and the new one must fit (its own blocks or
 * the room's free ones). Up to 512 (a seed's plan); pe_rand_model_clear drops them all. pe_rand_model_apply does the swaps
 * for the room just loaded (PE_HOOK_PLAYER_ROWS calls it), once a load; it returns how many it did. */
int pe_rand_model_force(unsigned room, unsigned entity, unsigned from_room, unsigned from_entity);
void pe_rand_model_clear(void);
int pe_rand_model_apply(int room);
void pe_rand_model_register(void);

/* Battle randomizer, AI (pe_rand_ai.c), test only too: actor `entity` of room `room` runs the script of actor
 * `from_entity` of room `from_room` (a copy read from the disc at each load of `room`, its own and its spawner's
 * numbers changed to this room's), with the script slots that room installs and this one lacks. Up to 512;
 * pe_rand_ai_clear drops them all. pe_rand_ai_apply does it for the room just loaded (PE_HOOK_PLAYER_ROWS
 * calls it), once a load; it returns how many it did. */
int pe_rand_ai_force(unsigned room, unsigned entity, unsigned from_room, unsigned from_entity);
void pe_rand_ai_clear(void);
int pe_rand_ai_apply(int room);
void pe_rand_ai_register(void);

/* Battle randomizer, the seed (pe_rand_gen.c): which foe takes each foe's slot (pe_rand_foes.h, generated by
 * tools/enemyai.py --table, in room order). A foe is an enemy actor whose script and model can move: placed in
 * the enemy VRAM blocks, and no story write of its own. Its VRAM place: first block, block count, whether another
 * bank slot shares its blocks, and its room's first free block and palette row (from 450), as
 * pe_rand_model_apply works them out. */
typedef struct PeRandFoe {
    uint16_t room;
    uint8_t entity, boss;
    int8_t block;
    uint8_t blocks, shared, end, rows_end;
    const char *name, *place;
} PeRandFoe;

/* out[i]: the foe whose script and model foe i's slot takes (i: it stays). Each slot takes a random foe its
 * ok row allows (ok: count rows of words = (count + 31) / 32 words; bosses only take bosses, and with
 * PE_RAND_MOVE_BOSSES 0 boss slots stay) and whose model
 * fits in the room's VRAM after the swaps before it in the room (pe_rand_model_apply's rule, in table order);
 * else it stays. Same seed, same plan on every machine. Returns how many slots moved. */
unsigned pe_rand_foes_generate(const PeRandFoe *foes, const uint32_t *ok, unsigned count, uint32_t seed, uint16_t *out);
/* Raise PE_RAND_FOES_VERSION whenever the same seed would give a different plan (the generator or pe_rand_foes.h).
 * A save keeps the version it was made with; a build that doesn't know it leaves that game's enemies vanilla. */
#define PE_RAND_FOES_VERSION 3
/* 0: boss fights stay vanilla, normal fights only (the user's test runs). */
#define PE_RAND_MOVE_BOSSES 0

/* The checks every plan passes (also used by the tests). 0 = fine, else a reason. */
const char *pe_rand_foes_check(const PeRandFoe *foes, const uint32_t *ok, unsigned count, const uint16_t *plan);

#endif
