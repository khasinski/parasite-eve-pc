/* Item randomizer: New Game shuffles what room chests, pickups and character gifts hand out, from a
 * six-digit seed picked under F1 > Randomizer (no difficulty to pick: the Hard rules). The game keeps the seed it started
 * with (a save record), so loading always gives the same layout; F1 changes only reach the next New
 * Game. Battle drops, Wayne's Junk trades and the Chrysler Building stay vanilla.
 *
 * Randomized play is the original game plus this mod only (and Always run): with "Randomize new
 * games" on when the game starts, the randomizer runs alone (pe_mod_run_alone) and no other mod
 * starts. Turning it on or off reaches the next start, like Manage mods. A randomized save loaded
 * with it off stops the other mods there and then (pe_mod_stand_alone), until the next start.
 *
 * How items move: when a room has loaded (PE_HOOK_SCENE_START, before its first frame; the room data
 * is in g_GameState.loaded_scene_assets in the disc's layout), the item operands of its spots are
 * rewritten in the loaded script, all or none per room. The game's own code then runs unchanged:
 * the full-bag swap, the item a chest keeps for the player, the opened bit, the "You got" popup.
 * Changing the id in Inv_AddItem instead would also catch steals, drops, unequips and the New Game
 * kit, and would turn the item a chest keeps for the player into the seed's item. A room whose
 * script isn't byte for byte what the table expects stays vanilla.
 *
 * Enemies (not in F1 for now, on top of the items, same seed): the seed's foe plan
 * (pe_rand_foes_generate) goes into the model and AI switches, which give each moved slot its foe's model,
 * animations and script at every load of its room; the foe's own script sets its stats. Bosses take bosses.
 * The game keeps whether it was started with enemies on (the "pe_randomizer.foes" record). */
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "pe_game.h"
#include "pe_item_table.h"
#include "pe_mod_host.h"
#include "pe_mod_menu.h"
#include "pe_save.h"
#include "pe1/asset_table.h"
#include "pe1/game_state.h"
#include "pe1/inventory.h"
#include "pe1/inventory_slots.h"
#include "pe1/scene_room.h"
#include "pe1/script_queue.h"
#include "pe1/script_state.h"
#include "pe_randomizer.h"
#include "pe_rand_locks.h"
#include "pe_rand_spots.h"
#include "pe_rand_foes.h"

#define MOD_ID "pe_randomizer"

/* What this game was started with: saved as the "pe_randomizer.run" record. */
typedef struct PeRandRun {
    uint8_t on, difficulty, logic, table;
    uint32_t seed;
} PeRandRun;
_Static_assert(sizeof(PeRandRun) == 8, "the saved record's layout");

static PeRandRun s_run, s_layout_for;
static uint16_t s_layout[PE_RAND_SPOTS];
static PeRandResult s_result;
static int s_layout_ready;
static int s_enabled, s_difficulty = PE_RAND_HARD, s_seed;   /* s_seed: six digits, 0..999999 */
static int s_alone;            /* "Randomize new games" was on at start: this session runs no other mod */
static unsigned char s_room_warned[SCENE_ROOM_COUNT + 1];
/* Named only for a save from a build that still had difficulties. */
static const char *const s_rules_names[] = {", Easy rules", ", Medium rules", ""};
/* Enemies of this game: saved as the "pe_randomizer.foes" record (none in an older save: off). */
typedef struct PeRandFoesRun {
    uint8_t on, version, reserved[2];
} PeRandFoesRun;
_Static_assert(sizeof(PeRandFoesRun) == 4, "the saved record's layout");
static PeRandFoesRun s_foes_run;
static int s_foes_enabled;

/* New games use the Hard rules with enemies off for now: F1 has no rows for these, and the player is told of no
 * difficulty. Saves started on Easy or Medium keep theirs. Only the run test sets them, to keep the rest checked
 * (difficulty -1: what a player gets). */
void pe_rand_test_options(int difficulty, int enemies) {
    if (difficulty >= 0) s_difficulty = difficulty;
    s_foes_enabled = enemies;
}
static uint16_t s_plan[PE_RAND_FOES];
static unsigned s_plan_moved;
static int s_plan_given;       /* the switches hold this game's plan (tests force their own swaps otherwise) */

static uint32_t pe_rand_seed(void) {
    return s_seed >= 0 && s_seed <= 999999 ? (uint32_t)s_seed : 0;
}

static int pe_rand_supported(void) {
    return s_run.logic == PE_RAND_LOGIC_VERSION && s_run.table == PE_RAND_TABLE_VERSION &&
           s_run.difficulty < PE_RAND_DIFFICULTIES;
}

static int pe_rand_foes_supported(void) {
    return s_run.on && pe_rand_supported() && s_foes_run.on && s_foes_run.version == PE_RAND_FOES_VERSION;
}

/* This game's plan into the switches (they swap at each room load), or nothing when its enemies are vanilla. */
static void pe_rand_foes_apply(void) {
    if (s_plan_given) { pe_rand_model_clear(); pe_rand_ai_clear(); pe_rand_battle_clear(); }
    s_plan_given = 0;
    s_plan_moved = 0;
    if (!pe_rand_foes_supported()) return;
    s_plan_moved = pe_rand_foes_generate(g_pe_rand_foes, g_pe_rand_foe_ok[0], PE_RAND_FOES, s_run.seed, s_plan);
    pe_rand_model_clear();
    pe_rand_ai_clear();
    pe_rand_battle_clear();
    for (unsigned i = 0; i < PE_RAND_FOES; ++i) {
        const PeRandFoe *a = &g_pe_rand_foes[i], *b = &g_pe_rand_foes[s_plan[i]];
        if (s_plan[i] == i) continue;
        pe_rand_model_force(a->room, a->entity, b->room, b->entity);
        pe_rand_ai_force(a->room, a->entity, b->room, b->entity);
        /* the new foe keeps the slot's own numbers (HP, defense, speed, exp...) and attack power */
        pe_rand_battle_force(a->room, a->entity, a->room, a->entity);
        pe_rand_battle_attack(a->room, a->entity, b->room, b->entity);
    }
    s_plan_given = 1;
    pe_log("rand_foes", "seed=%06u version=%u moved=%u of %u", (unsigned)s_run.seed, PE_RAND_FOES_VERSION, s_plan_moved,
           (unsigned)PE_RAND_FOES);
}

static int pe_is_key_item(unsigned id) { return (id >= 0x38 && id <= 0x3E) || (id >= 0xC8 && id <= 0xDC); }

/* Can sit in a pickup or gift: bag items and ammo, whose add fails only on a full bag, the same way
 * the spot's own item does. Not equipment (it also needs a free equipment record) or keys. */
static int pe_gift_safe(unsigned id) {
    ItemDataRecord *item;
    if (!id || id > 0xFF || pe_is_key_item(id) || !(item = Item_LookupBaseData(id - 1))) return 0;
    return item->kind == 10 || (item->kind >= 12 && item->kind <= 18);
}

static const char *pe_item_name(unsigned id) {
    for (unsigned i = 0; i < PE_ITEM_COUNT; ++i)
        if (pe_item_ids[i] == id) return pe_item_names[i];
    return "?";
}

static int pe_rand_ready(void) {
    if (!s_run.on || !pe_rand_supported()) return 0;
    if (!s_layout_ready || memcmp(&s_layout_for, &s_run, sizeof s_run)) {
        pe_rand_generate(g_pe_rand_spots, PE_RAND_SPOTS, s_run.seed, s_run.difficulty, pe_gift_safe, s_layout, &s_result);
        s_layout_for = s_run;
        s_layout_ready = 1;
        memset(s_room_warned, 0, sizeof s_room_warned);
        pe_log("rand_layout", "seed=%06u difficulty=%u tries=%u keys_moved=%u fallback=%d keys_locked=%d",
               (unsigned)s_run.seed, (unsigned)s_run.difficulty, s_result.attempts, s_result.keys_moved,
               s_result.fallback, s_result.keys_locked);
    }
    return 1;
}

static uint32_t pe_get32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static void pe_put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

static int pe_rand_scene(void *ctx, void *user) {
    uint8_t *room = (uint8_t *)g_GameState.loaded_scene_assets;
    int map = pe_room_number();
    unsigned first = 0, end, patched = 0;
    size_t bytes;
    (void)ctx;
    (void)user;
    if (!room || map < 1 || map > SCENE_ROOM_COUNT || !pe_rand_ready()) return 0;
    while (first < PE_RAND_SITES && g_pe_rand_sites[first].map < map) ++first;
    for (end = first; end < PE_RAND_SITES && g_pe_rand_sites[end].map == map; ++end) {}
    if (first == end) return 0;
    bytes = (size_t)g_RoomSectorTable[map - 1].roomSectors * 2048;
    /* all or none: a half-patched room could give one version of a spot's item and not the other */
    for (unsigned i = first; i < end; ++i) {
        const PeRandSite *s = &g_pe_rand_sites[i];
        int same = s->at <= bytes && bytes - s->at >= 16;
        for (unsigned w = 0; same && w < 4; ++w) {
            uint32_t have = pe_get32(room + s->at + 4 * w);
            same = have == s->expect[w] || (w == 2u + s->operand && have == s_layout[s->spot]);
        }
        if (!same) {
            if (!s_room_warned[map]) {
                s_room_warned[map] = 1;
                pe_mod_log(MOD_ID, "M%03d: script differs from the table at +%06X; its items stay vanilla",
                           map, (unsigned)s->at);
            }
            return 0;
        }
    }
    for (unsigned i = first; i < end; ++i) {
        const PeRandSite *s = &g_pe_rand_sites[i];
        pe_put32(room + s->at + 8 + 4 * s->operand, s_layout[s->spot]);
        patched += s_layout[s->spot] != g_pe_rand_spots[s->spot].vanilla;
    }
    pe_log("rand_room", "map=%d sites=%u changed=%u", map, end - first, patched);
    return 0;
}

/* Ways back on Hard, from Day 2 on: Carnegie Hall and its sewer, and the hospital's power cut, are
 * places the story leaves only one way, so their chests would be gone for a key that comes later.
 * The mod can't add a door, so an existing one leads somewhere else: a door is op 49 with its target
 * room's scene token as the one immediate operand, rewritten here when the room starts (same check as
 * the item sites). Aya's spot in a room comes from V0x1, which the old room set for the door's old
 * target; the new room gets a value one of its own ways in sets, when the room Aya left had a moved door. */
typedef struct PeRandDoor {
    uint16_t from;
    uint32_t at;           /* the door step, from the room's task block (the step listing's offset) */
    uint16_t old, to;      /* the room it led to, the room it leads to now */
    uint32_t came;         /* V0x1 in the new room */
    const char *what;      /* for the spoiler */
} PeRandDoor;
static const PeRandDoor s_doors[] = {
    {24, 0x1F5C, 0, 378, 0x17A, "Carnegie Hall front (later days): the right-hand map door leads to the lobby"},
    {378, 0x770, 1, 24, 0x18, "Carnegie Hall lobby: the street door leads back to the front (later days)"},
    {27, 0xB80, 26, 12, 0xC, "Carnegie Hall sewer: the door back to M026 leads to backstage"},
    {320, 0xC10, 329, 127, 0x7E, "Hospital power cut: the shortcut door to M329 leads to the first-floor hall M127"},
};
#define PE_RAND_DOOR_COUNT (sizeof s_doors / sizeof *s_doors)

/* Story flags Hard sets when a room starts, from a story value (V0x4a) on: a door that only a one-time
 * event opens, which the key walks went through (tools/hardwalk.py; no run sets the flag once it's missed). */
typedef struct PeRandFlag {
    uint16_t map;
    uint8_t var;           /* V0x.. */
    uint32_t bits;
    uint16_t from;         /* V0x4a */
} PeRandFlag;

static const PeRandFlag s_flags[] = {
    {94, 0x19, 0x20, 0x190},   /* Soho M094 from Day 3: the door to M095 (chests 49-52), opened by M094's event */
};
#define PE_RAND_FLAG_COUNT (sizeof s_flags / sizeof *s_flags)
#define PE_RAND_DAY2 0x88u             /* V0x4a: Day 1 is over */
static int s_door_moved = -1;          /* the moved door of the room that started last, or -1 */

static uint32_t pe_room_token(unsigned map) {
    char name[8];
    snprintf(name, sizeof name, "m%04ui", map);
    return (uint32_t)Text_ParseBase32Id((signed char *)name);
}

/* Doors that test a story flag a key's own pickup sets, not the key: that flag is also the pickup's
 * done-guard, so it stays the pickup's, and the doors test a V0x19 bit no room uses instead (spottable.py's
 * rule), set at the room's start when Aya holds the key. Never the spot's flag: that is set by whatever item the
 * spot now gives, so it would open the door without the key.
 * - The Theater keys on Hard: M012's doors to M014/M016/M018/M022 (V0x18 0x20, the Dressing Room Key's)
 *   and M319 (0x80000, the Rehearsal Room Key's). No Theater key can be in M012 itself.
 * - The Blue Cardkey: M339's door to M329 tests V0x41 0x2, set by its vanilla spot in M328 (spot 180)
 *   whatever item that now gives, so a moved card met "need the Blue Cardkey" at the door. The door
 *   takes the card and sets V0x42 0x20 (its open-after flag, tested before these), and no spot is in M339.
 * - The St. Francis Key: M321's door to M327 tests V0x41 0x1, set by its vanilla spot in M325 (spot 179, also
 *   that gift's done-guard). Two tests pick the door's locked or open handler at the room's start, five pick
 *   whether a way out of M321 sets V0x41 0x400000 (read in M320 / M321 / M323): all seven follow the key.
 *   The door's unlock scene takes the key and sets V0x41 0x8, so the bit is also set once that has run.
 *   M329 sets V0x41 0x1 and 0x8 on entry, and M329 is reached by the Blue Cardkey door without passing
 *   this one (M323 -> M339): the mod drops those two sets, so neither the door nor spot 179's done-guard
 *   is set there. No spot is in M321. */
typedef struct PeRandFlagLock {
    uint32_t at;           /* the op 9 bit test (T<dest> = V<old_var> & mask), from the room's task block */
    uint8_t dest, old_var;
    uint32_t old_mask, new_mask;
} PeRandFlagLock;

static const PeRandFlagLock s_theater_locks[] = {
    {0xB94, 0, 0x18, 0x20, 0x10000000}, {0xE44, 0, 0x18, 0x20, 0x10000000},   /* M014, M014 */
    {0x10EC, 0, 0x18, 0x20, 0x10000000}, {0x1684, 0, 0x18, 0x20, 0x10000000},   /* M016, M018 */
    {0x192C, 0, 0x18, 0x20, 0x10000000}, {0x1D7C, 0, 0x18, 0x80000, 0x20000000},   /* M022, M319 */
};
static const PeRandFlagLock s_blue_locks[] = {
    {0x1344, 1, 0x41, 0x2, 0x40000000}, {0x1398, 0, 0x41, 0x2, 0x40000000},   /* M329, both tests of the door */
};
static const PeRandFlagLock s_francis_locks[] = {
    {0x118C, 0, 0x41, 0x1, 0x2000000}, {0x11F4, 0, 0x41, 0x1, 0x2000000},   /* the door's two handlers */
    {0x450, 0, 0x41, 0x1, 0x2000000}, {0x608, 0, 0x41, 0x1, 0x2000000}, {0x1DF0, 0, 0x41, 0x1, 0x2000000},
    {0x1EAC, 0, 0x41, 0x1, 0x2000000}, {0x1F68, 0, 0x41, 0x1, 0x2000000},   /* ways out: V0x41 0x400000 */
};
#define PE_RAND_THEATER_MAP 12
#define PE_RAND_DRESSING_KEY 0xC8
#define PE_RAND_REHEARSAL_KEY 0xC9
#define PE_RAND_BLUE_MAP 339
#define PE_RAND_BLUE_KEY 0xD2
#define PE_RAND_FRANCIS_MAP 321
#define PE_RAND_FRANCIS_KEY 0xD1
#define PE_RAND_FRANCIS_DONE 0x8u   /* V0x41: the door's unlock scene ran (it took the key) */
#define PE_RAND_M329 329
static const uint32_t s_m329_sets[][2] = { {0xA6C, 0x1}, {0xA94, 0x8} };   /* T0 = V0x41 | mask, then V0x41 = T0 */

static int pe_rand_holds(unsigned item) {
    for (unsigned i = 0; i < sizeof g_AyaInventoryItems / sizeof *g_AyaInventoryItems; ++i)
        if ((unsigned)g_AyaInventoryItems[i] == item) return 1;
    return 0;
}

/* Points the loaded room's tests at their V0x19 bits: all or none, each the six words the scan saw (or
 * already rewritten). */
static void pe_rand_flag_locks(uint8_t *room, int map, const PeRandFlagLock *locks, unsigned count, const char *what) {
    size_t bytes = (size_t)g_RoomSectorTable[map - 1].roomSectors * 2048, dir, table, base;
    dir = pe_get32(room + 4);
    if (dir > bytes || bytes - dir < sizeof(SceneRoomDirectory)) return;
    table = ((const SceneRoomDirectory *)(room + dir))->taskBlock & 0x3FFFFF;
    if (table > bytes - 8) return;
    base = pe_get32(room + table + 4) & 0xFFFFFF;
    for (int pass = 0; pass < 2; ++pass)
        for (unsigned i = 0; i < count; ++i) {
            const PeRandFlagLock *l = &locks[i];
            size_t at = base + l->at;
            uint32_t var, mask;
            if (at > bytes - 24) return;
            var = pe_get32(room + at + 16);
            mask = pe_get32(room + at + 20);
            if (pass == 0) {
                if (pe_get32(room + at) != 0x01308009u || pe_get32(room + at + 4) || pe_get32(room + at + 8) != 3 ||
                    pe_get32(room + at + 12) != l->dest ||
                    !((var == l->old_var && mask == l->old_mask) || (var == 0x19 && mask == l->new_mask))) {
                    if (!s_room_warned[map]) {
                        s_room_warned[map] = 1;
                        pe_mod_log(MOD_ID, "M%03d: key test +%06X differs from the table; %s", map, (unsigned)l->at, what);
                    }
                    return;
                }
            } else {
                pe_put32(room + at + 16, 0x19);
                pe_put32(room + at + 20, l->new_mask);
            }
        }
}

static int pe_rand_theater(void *ctx, void *user) {
    uint8_t *room = (uint8_t *)g_GameState.loaded_scene_assets;
    (void)ctx;
    (void)user;
    if (!room || pe_room_number() != PE_RAND_THEATER_MAP || !s_run.on || !pe_rand_supported() ||
        s_run.difficulty != PE_RAND_HARD) return 0;
    /* from Day 2 both: Day 1 can't end without them (M018's 0x54 and M023's 0x5e are behind these doors) */
    if (pe_rand_holds(PE_RAND_DRESSING_KEY) || g_ScriptVariableBank[SCRIPT_VAR_AREA] >= PE_RAND_DAY2)
        g_ScriptVariableBank[0x19] |= 0x10000000u;
    if (pe_rand_holds(PE_RAND_REHEARSAL_KEY) || g_ScriptVariableBank[SCRIPT_VAR_AREA] >= PE_RAND_DAY2)
        g_ScriptVariableBank[0x19] |= 0x20000000u;
    pe_rand_flag_locks(room, PE_RAND_THEATER_MAP, s_theater_locks, sizeof s_theater_locks / sizeof *s_theater_locks,
                       "the Theater doors stay vanilla");
    pe_log("rand_theater", "V0x19=%#x", (unsigned)g_ScriptVariableBank[0x19]);
    return 0;
}

/* Every difficulty: the card moves on Medium too. Set or cleared each time, so the bit follows the card. */
static int pe_rand_blue_door(void *ctx, void *user) {
    uint8_t *room = (uint8_t *)g_GameState.loaded_scene_assets;
    (void)ctx;
    (void)user;
    if (!room || pe_room_number() != PE_RAND_BLUE_MAP || !s_run.on || !pe_rand_supported()) return 0;
    if (pe_rand_holds(PE_RAND_BLUE_KEY))
        g_ScriptVariableBank[0x19] |= 0x40000000u;
    else
        g_ScriptVariableBank[0x19] &= ~0x40000000u;
    pe_rand_flag_locks(room, PE_RAND_BLUE_MAP, s_blue_locks, sizeof s_blue_locks / sizeof *s_blue_locks,
                       "the Blue Cardkey door stays vanilla");
    pe_log("rand_blue", "V0x19=%#x", (unsigned)g_ScriptVariableBank[0x19]);
    return 0;
}

/* M329's two sets of the St. Francis flags become V0x41 | 0: all or none, the six words checked. */
static void pe_rand_m329(uint8_t *room) {
    size_t bytes = (size_t)g_RoomSectorTable[PE_RAND_M329 - 1].roomSectors * 2048, dir, table, base;
    dir = pe_get32(room + 4);
    if (dir > bytes || bytes - dir < sizeof(SceneRoomDirectory)) return;
    table = ((const SceneRoomDirectory *)(room + dir))->taskBlock & 0x3FFFFF;
    if (table > bytes - 8) return;
    base = pe_get32(room + table + 4) & 0xFFFFFF;
    for (int pass = 0; pass < 2; ++pass)
        for (unsigned i = 0; i < sizeof s_m329_sets / sizeof *s_m329_sets; ++i) {
            size_t at = base + s_m329_sets[i][0];
            uint32_t mask;
            if (at > bytes - 24) return;
            mask = pe_get32(room + at + 20);
            if (pass == 0) {
                if (pe_get32(room + at) != 0x01308009u || pe_get32(room + at + 4) || pe_get32(room + at + 8) != 2 ||
                    pe_get32(room + at + 12) || pe_get32(room + at + 16) != 0x41 || (mask != s_m329_sets[i][1] && mask)) {
                    if (!s_room_warned[PE_RAND_M329]) {
                        s_room_warned[PE_RAND_M329] = 1;
                        pe_mod_log(MOD_ID, "M329: flag set +%06X differs from the table; it still opens the St. Francis door",
                                   (unsigned)s_m329_sets[i][0]);
                    }
                    return;
                }
            } else
                pe_put32(room + at + 20, 0);
        }
}

/* Same for the St. Francis Key (every difficulty). */
static int pe_rand_francis_door(void *ctx, void *user) {
    uint8_t *room = (uint8_t *)g_GameState.loaded_scene_assets;
    (void)ctx;
    (void)user;
    if (!room || !s_run.on || !pe_rand_supported()) return 0;
    if (pe_room_number() == PE_RAND_M329) {
        pe_rand_m329(room);
        return 0;
    }
    if (pe_room_number() != PE_RAND_FRANCIS_MAP) return 0;
    if (pe_rand_holds(PE_RAND_FRANCIS_KEY) || (g_ScriptVariableBank[0x41] & PE_RAND_FRANCIS_DONE))
        g_ScriptVariableBank[0x19] |= 0x2000000u;
    else
        g_ScriptVariableBank[0x19] &= ~0x2000000u;
    pe_rand_flag_locks(room, PE_RAND_FRANCIS_MAP, s_francis_locks, sizeof s_francis_locks / sizeof *s_francis_locks,
                       "the St. Francis door stays vanilla");
    pe_log("rand_francis", "V0x19=%#x", (unsigned)g_ScriptVariableBank[0x19]);
    return 0;
}

static int pe_rand_doors(void *ctx, void *user) {
    uint8_t *room = (uint8_t *)g_GameState.loaded_scene_assets;
    int map = pe_room_number(), moved = s_door_moved;
    (void)ctx;
    (void)user;
    s_door_moved = -1;
    if (!room || map < 1 || map > SCENE_ROOM_COUNT || !s_run.on || !pe_rand_supported() ||
        s_run.difficulty != PE_RAND_HARD || g_ScriptVariableBank[SCRIPT_VAR_AREA] < PE_RAND_DAY2) return 0;
    for (unsigned i = 0; i < PE_RAND_FLAG_COUNT; ++i) {
        const PeRandFlag *f = &s_flags[i];
        if (f->map != map || g_ScriptVariableBank[SCRIPT_VAR_AREA] < f->from ||
            (g_ScriptVariableBank[f->var] & f->bits) == f->bits) continue;
        g_ScriptVariableBank[f->var] |= f->bits;
        pe_log("rand_flag", "map=%d V0x%x|=%#x", map, (unsigned)f->var, (unsigned)f->bits);
    }
    if (moved >= 0 && s_doors[moved].to == map) {
        g_ScriptVariableBank[SCRIPT_VAR_ROOM] = s_doors[moved].came;
        pe_log("rand_door", "map=%d from=%d came=%#x", map, s_doors[moved].from, (unsigned)s_doors[moved].came);
    }
    for (unsigned i = 0; i < PE_RAND_DOOR_COUNT; ++i) {
        const PeRandDoor *d = &s_doors[i];
        size_t bytes = (size_t)g_RoomSectorTable[map - 1].roomSectors * 2048, dir, table, at;
        uint32_t head, token;
        if (d->from != map) continue;
        dir = pe_get32(room + 4);
        if (dir > bytes || bytes - dir < sizeof(SceneRoomDirectory)) return 0;
        table = ((const SceneRoomDirectory *)(room + dir))->taskBlock & 0x3FFFFF;
        if (table > bytes - 8) return 0;
        at = (pe_get32(room + table + 4) & 0xFFFFFF) + d->at;
        if (at > bytes - 12) return 0;
        head = pe_get32(room + at);
        token = pe_get32(room + at + 8);
        /* op 49, one operand, immediate */
        if (head != (49u | 1u << 13) || (token != pe_room_token(d->old) && token != pe_room_token(d->to))) {
            if (!s_room_warned[map]) {
                s_room_warned[map] = 1;
                pe_mod_log(MOD_ID, "M%03d: door +%06X differs from the table; it stays vanilla", map, (unsigned)d->at);
            }
            continue;
        }
        pe_put32(room + at + 8, pe_room_token(d->to));
        s_door_moved = (int)i;
    }
    return 0;
}

/* The spot's own item: vanilla, this run's, or what its sites in the loaded room hold now (both
 * items of a roll). */
static int pe_rand_spot_item(const uint8_t *room, size_t bytes, int map, unsigned spot, unsigned item) {
    if (item == g_pe_rand_spots[spot].vanilla) return 1;
    if (s_layout_ready && s_run.on && pe_rand_supported() && item == s_layout[spot]) return 1;
    for (unsigned i = 0; i < PE_RAND_SITES; ++i) {
        const PeRandSite *s = &g_pe_rand_sites[i];
        if (s->map == map && s->spot == spot && s->at <= bytes && bytes - s->at >= 16 &&
            pe_get32(room + s->at + 8 + 4 * s->operand) == item) return 1;
    }
    return 0;
}

/* A gift on a full bag (randomizer on): one slot past the bag (g_InvOverflowSlotCount, 11/10) so the
 * give can't fail. Many gifts set their done flag either way, and a gift that was ammo never had a
 * retry: the item was lost for good (Day 3 NYPD, M116). The host moves it into the bag's first empty
 * slot later and shrinks the bag back (pe_equip_sync.c), so no slot is gained for good. */
static void pe_rand_gift_room(unsigned spot) {
    if (!s_run.on || !pe_rand_supported() || g_pe_rand_spots[spot].kind != PE_RAND_GIFT ||
        g_InvItemPtr != g_AyaSaveState.inventory_items || g_InvSlotLimit >= 50) return;
    for (int i = 0; i < g_InvSlotLimit; ++i)
        if (!g_InvItemPtr[i]) return;
    ++g_InvOverflowSlotCount;
    g_InvSlotLimit = Inv_GetAyaSlotLimit();
    pe_log("rand_gift_full", "spot=%u slots=%d", spot, (int)g_InvSlotLimit);
}

/* A table spot giving its item: a rand_give row (play log), on or off, for tools/keylogic.py,
 * which works out from a run which keys each spot needs. During a script step the cursor is just past
 * it, so a GiveItem (two operands, 16 bytes) starts 16 bytes back. Only rows for the table's own give
 * steps are written; steals, drops and menus aren't script gives. A battle that starts right after a
 * give leaves the cursor there, so its rewards look like that step: only the spot's own item counts. */
static int pe_rand_item_add(void *ctx, void *user) {
    const uint8_t *room = (const uint8_t *)g_GameState.loaded_scene_assets;
    const int *item = ctx;
    int map = pe_room_number();
    uintptr_t base = (uintptr_t)room, cursor = (uintptr_t)g_ScriptCursor;
    size_t bytes, at;
    (void)user;
    if (!room || !item || map < 1 || map > SCENE_ROOM_COUNT || pe_in_battle() || cursor < base + 16) return 0;
    bytes = (size_t)g_RoomSectorTable[map - 1].roomSectors * 2048;
    if (cursor - base > bytes) return 0;
    at = cursor - base - 16;
    if ((pe_get32(room + at) & 0x1FFFu) != 167u) return 0;
    for (unsigned i = 0; i < PE_RAND_GIVES && g_pe_rand_gives[i].map <= map; ++i)
        if (g_pe_rand_gives[i].map == map && g_pe_rand_gives[i].at == at) {
            if (pe_rand_spot_item(room, bytes, map, g_pe_rand_gives[i].spot, (unsigned)*item)) {
                pe_log("rand_give", "map=%d at=%06X spot=%u item=0x%02X", map, (unsigned)at,
                       (unsigned)g_pe_rand_gives[i].spot, (unsigned)*item);
                pe_rand_gift_room(g_pe_rand_gives[i].spot);
            }
            break;
        }
    return 0;
}

/* The game asking whether Aya holds a movable key (a lock, or the script near one): a rand_lock row,
 * once per key and room per launch and run, for keylogic.py's before= (where a key may go). */
static unsigned char s_lock_logged[PE_RAND_MAX_KEYS][SCENE_ROOM_COUNT + 1];

static void pe_rand_lock_seen(unsigned item) {
    int map = pe_room_number();
    unsigned k = 0;
    if (map < 1 || map > SCENE_ROOM_COUNT) return;
    for (unsigned i = 0; i < PE_RAND_SPOTS && k < PE_RAND_MAX_KEYS; ++i) {
        if (!(g_pe_rand_spots[i].flags & PE_RAND_KEY)) continue;
        if (g_pe_rand_spots[i].vanilla == item) {
            if (!s_lock_logged[k][map]) pe_log("rand_lock", "item=0x%02X map=%d", item, map);
            s_lock_logged[k][map] = 1;
            return;
        }
        ++k;
    }
}

/* Story gates (pe_rand_locks.h): a moved key opens its lock only once the story has reached the lock's
 * vanilla point, the bit its vanilla spot sets; until then the lock's own key test hears "not held".
 * An early key is then harmless. Only the table's steps are gated (a script asking about a key for
 * another reason gets the true answer), only for keys that moved, only in a randomized run. The
 * script step just run is `size` bytes back from the cursor (8 + 4 per operand). A rand_gate row
 * whenever a lock's state changes. */
static unsigned char s_gate_state[PE_RAND_LOCKS];   /* 0 not seen, 1 shut, 2 open */

static int pe_rand_gate_shut(unsigned item, unsigned opcode, size_t size) {
    const uint8_t *room = (const uint8_t *)g_GameState.loaded_scene_assets;
    const PeRandLock *lock;
    int map = pe_room_number(), shut;
    uintptr_t base = (uintptr_t)room, cursor = (uintptr_t)g_ScriptCursor;
    size_t bytes, at;
    unsigned l = 0;
    if (!room || map < 1 || map > SCENE_ROOM_COUNT || cursor < base + size || !pe_rand_ready()) return 0;
    bytes = (size_t)g_RoomSectorTable[map - 1].roomSectors * 2048;
    if (cursor - base > bytes) return 0;
    at = cursor - base - size;
    if ((pe_get32(room + at) & 0x1FFFu) != opcode) return 0;
    while (l < PE_RAND_LOCKS && !(g_pe_rand_locks[l].map == map && g_pe_rand_locks[l].at == at)) ++l;
    if (l == PE_RAND_LOCKS) return 0;
    lock = &g_pe_rand_locks[l];
    if (lock->item != item || s_layout[lock->spot] == g_pe_rand_spots[lock->spot].vanilla) return 0;
    shut = !(g_ScriptVariableBank[lock->var] & lock->bit);
    if (s_gate_state[l] != 2 - shut) {
        s_gate_state[l] = (unsigned char)(2 - shut);
        pe_log("rand_gate", "map=%d at=%06X item=0x%02X %s", map, (unsigned)at, item, shut ? "shut" : "open");
    }
    return shut;
}

static int pe_rand_item_count(void *ctx, void *user) {
    int **args = ctx;
    (void)user;
    if (!args || !args[0]) return 0;
    pe_rand_lock_seen((unsigned)*args[0]);
    if (!pe_rand_gate_shut((unsigned)*args[0], 168, 16)) return 0;
    *args[1] = 0;
    return 1;
}

/* No room script sends 1102 (the rooms ctest walks them all), so the table has no such step and this
 * gate never shuts today; it's here so a 1102 lock can't slip past. 1112 (remove) is never gated. */
static int pe_rand_inv_command(void *ctx, void *user) {
    PeHookInvCommand *c = ctx;
    (void)user;
    if (!c || (c->command != 1102 && c->command != 1112)) return 0;
    pe_rand_lock_seen((unsigned)c->value);
    if (c->command != 1102 || !pe_rand_gate_shut((unsigned)c->value, 233, 28)) return 0;
    c->result = 0;
    return 1;
}

/* Every room a spot is in (sites are sorted by map): one chest can show in several. The spot number
 * tells two chests of one room apart and is the tools' chest number (hard-reach.txt and the rest). */
static void pe_rand_spot_rooms(FILE *f, unsigned n) {
    unsigned last = 0;
    for (unsigned i = 0; i < PE_RAND_SITES; ++i) {
        if (g_pe_rand_sites[i].spot != n || g_pe_rand_sites[i].map == last) continue;
        fprintf(f, "%sM%03u", last ? "/" : "", (unsigned)g_pe_rand_sites[i].map);
        last = g_pe_rand_sites[i].map;
    }
    fprintf(f, " day %u (spot %u)", (unsigned)g_pe_rand_spots[n].day, n);
}

/* randomizer-<seed>.txt beside the preferences: where every key item is, Hard's ways back, then every
 * spot that changed. */
static void pe_rand_spoiler(void) {
    char name[64], path[1100], temp[1110];
    FILE *f;
    snprintf(name, sizeof name, "randomizer-%06u.txt", (unsigned)s_run.seed);
    if (pe_mod_path(path, sizeof path, name) != 0) return;
    snprintf(temp, sizeof temp, "%s.tmp", path);
    if (!(f = fopen(temp, "w"))) { pe_toast("Randomizer: couldn't write the spoiler file"); return; }
    fprintf(f, "Parasite Eve randomizer, seed %06u%s%s (logic %u, table %u", (unsigned)s_run.seed,
            s_rules_names[s_run.difficulty], s_foes_run.on ? ", enemies" : "", (unsigned)s_run.logic, (unsigned)s_run.table);
    if (s_foes_run.on) fprintf(f, ", enemies %u", (unsigned)s_foes_run.version);
    fprintf(f, ")\n");
    if (s_result.keys_locked) fprintf(f, "Key items stay in their places.\n");
    else if (s_result.fallback) fprintf(f, "No layout passed its checks: %s.\n", s_result.fallback == 1 ? "key items stay in their places" : "nothing moved");
    fprintf(f, "\nKey items (now in, was in):\n");
    for (unsigned n = 0; n < PE_RAND_SPOTS; ++n) {
        unsigned from = n;
        if (!pe_is_key_item(s_layout[n])) continue;
        for (unsigned i = 0; i < PE_RAND_SPOTS; ++i)
            if (g_pe_rand_spots[i].vanilla == s_layout[n]) { from = i; break; }
        fprintf(f, "  %s: ", pe_item_name(s_layout[n]));
        pe_rand_spot_rooms(f, n);
        if (from == n) fprintf(f, " (not moved)\n");
        else { fprintf(f, ", was "); pe_rand_spot_rooms(f, from); fprintf(f, "\n"); }
    }
    if (s_run.difficulty == PE_RAND_HARD) {
        fprintf(f, "\nWays back (from Day 2):\n");
        for (unsigned i = 0; i < PE_RAND_DOOR_COUNT; ++i) fprintf(f, "  M%03u: %s\n", (unsigned)s_doors[i].from, s_doors[i].what);
    }
    fprintf(f, "\nEvery changed spot:\n");
    for (unsigned n = 0; n < PE_RAND_SPOTS; ++n) {
        if (s_layout[n] == g_pe_rand_spots[n].vanilla) continue;
        fprintf(f, "  ");
        pe_rand_spot_rooms(f, n);
        fprintf(f, ": %s (was %s)\n", pe_item_name(s_layout[n]), pe_item_name(g_pe_rand_spots[n].vanilla));
    }
    if (s_foes_run.on) {
        fprintf(f, "\nEnemies (%u of %u slots moved; bosses take bosses):\n", s_plan_moved, (unsigned)PE_RAND_FOES);
        for (unsigned i = 0; i < PE_RAND_FOES; ++i) {
            const PeRandFoe *a = &g_pe_rand_foes[i], *b = &g_pe_rand_foes[s_plan[i]];
            if (s_plan[i] == i) continue;
            fprintf(f, "  %s M%03u e%u: %s, from %s M%03u (was %s)\n", a->place, (unsigned)a->room, (unsigned)a->entity,
                    b->name, b->place, (unsigned)b->room, a->name);
        }
    }
    if (fclose(f) != 0) { remove(temp); pe_toast("Randomizer: couldn't write the spoiler file"); return; }
    remove(path);
    if (rename(temp, path) != 0) { remove(temp); pe_toast("Randomizer: couldn't write the spoiler file"); }
}

/* Every new game (the title's New Game or Tutorial, or the EX Game), after the host has reset the
 * record: the only place a run's settings are taken. Only a session where the randomizer runs alone
 * randomizes, and never the Tutorial. */
static int pe_rand_new_game(void *ctx, void *user) {
    const PeHookNewGame *game = ctx;
    char line[96];
    (void)user;
    memset(s_lock_logged, 0, sizeof s_lock_logged);   /* a new run: its locks are logged again */
    memset(s_gate_state, 0, sizeof s_gate_state);
    s_door_moved = -1;
    if (s_enabled && !s_alone) pe_toast("Randomizer: restart the game first; this New Game isn't randomized");
    s_run.on = s_alone && s_enabled && game && game->kind != PE_NEW_GAME_TUTORIAL;
    s_run.difficulty = (uint8_t)(s_difficulty >= 0 && s_difficulty < PE_RAND_DIFFICULTIES ? s_difficulty : PE_RAND_HARD);
    s_run.logic = PE_RAND_LOGIC_VERSION;
    s_run.table = PE_RAND_TABLE_VERSION;
    s_run.seed = pe_rand_seed();
    s_foes_run.on = (uint8_t)(s_run.on && s_foes_enabled);
    s_foes_run.version = PE_RAND_FOES_VERSION;
    pe_rand_foes_apply();
    if (!s_run.on || !pe_rand_ready()) {
        pe_log("rand_newgame", "on=0 keys_moved=0");
        return 0;
    }
    pe_log("rand_newgame", "on=1 seed=%06u difficulty=%u keys_moved=%u", (unsigned)s_run.seed,
           (unsigned)s_run.difficulty, s_result.keys_moved);
    pe_rand_spoiler();
    snprintf(line, sizeof line, "Randomizer: seed %06u%s%s", (unsigned)s_run.seed, s_rules_names[s_run.difficulty],
             s_foes_run.on ? ", enemies" : "");
    pe_toast(line);
    return 0;
}

static int pe_rand_save_read(void *ctx, void *user) {
    (void)ctx;
    (void)user;
    s_layout_ready = 0;
    s_door_moved = -1;   /* the loaded room is the save's, not one past a moved door */
    if (s_run.on && !pe_rand_supported()) {
        pe_toast("Randomizer: this save's layout is from another version; items stay vanilla");
        pe_mod_log(MOD_ID, "save from logic %u / table %u, this build %u / %u: items stay vanilla",
                   (unsigned)s_run.logic, (unsigned)s_run.table, PE_RAND_LOGIC_VERSION, PE_RAND_TABLE_VERSION);
    } else if (s_run.on && !s_alone) {
        /* the host has stopped the other mods already (pe_rand_wants_alone) */
        s_alone = 1;
        pe_toast("This save is randomized: other mods are off until the game restarts");
        pe_mod_log(MOD_ID, "randomized save (seed %06u) loaded: other mods stopped", (unsigned)s_run.seed);
    }
    if (s_run.on && pe_rand_supported() && s_foes_run.on && s_foes_run.version != PE_RAND_FOES_VERSION) {
        pe_toast("Randomizer: this save's enemies are from another version; enemies stay vanilla");
        pe_mod_log(MOD_ID, "save's enemies from version %u, this build %u: enemies stay vanilla",
                   (unsigned)s_foes_run.version, PE_RAND_FOES_VERSION);
    }
    pe_rand_foes_apply();
    return 0;
}

/* A layout from an unknown version is kept as it is (saved back unchanged), only not used. */
static int pe_rand_check_record(void *data) {
    const PeRandRun *run = data;
    return run->on > 1;
}
/* The mod is off this session and the save is randomized: rooms load vanilla, so a spot the run
 * already emptied may hold an item that is now somewhere else. The record is kept as it is. */
static void pe_rand_release(void *data) {
    const PeRandRun *run = data;
    if (!run->on) return;
    pe_mod_log(MOD_ID, "randomized save (seed %06u) loaded with the mod off: items stay vanilla", (unsigned)run->seed);
    pe_toast("This save is randomized: turn the Randomizer mod back on to keep its items");
}
/* A layout this build plays is played with no other mod, whatever the toggle said at start. */
static int pe_rand_wants_alone(const void *data) {
    (void)data;
    return s_run.on && pe_rand_supported();
}
static const PeSaveRecord s_record = {"pe_randomizer.run", 1, &s_run, sizeof s_run, pe_rand_check_record, NULL, NULL,
                                      pe_rand_release, pe_rand_wants_alone};
/* Enemies from an unknown version are kept as they are, only not used; with the mod off they are vanilla anyway. */
static int pe_rand_check_foes(void *data) {
    const PeRandFoesRun *run = data;
    return run->on > 1;
}
static const PeSaveRecord s_foes_record = {"pe_randomizer.foes", 1, &s_foes_run, sizeof s_foes_run, pe_rand_check_foes,
                                           NULL, NULL, NULL};

static void pe_rand_random_seed(void) {
    uint32_t x = (uint32_t)time(NULL) ^ (uint32_t)clock() * 2654435761u;
    x ^= x >> 15; x *= 2246822519u; x ^= x >> 13;
    s_seed = (int)(x % 1000000u);
}

static void pe_rand_run_text(char *out, unsigned size) {
    if (!pe_save_loaded() || !s_run.on) { snprintf(out, size, "Not randomized"); return; }
    if (!pe_rand_supported()) { snprintf(out, size, "Other version: vanilla"); return; }
    pe_rand_ready();
    snprintf(out, size, "%06u%s, %s%s", (unsigned)s_run.seed, s_rules_names[s_run.difficulty],
             s_result.keys_locked || s_result.fallback ? "keys vanilla" : "keys moved",
             pe_rand_foes_supported() ? ", enemies" : s_foes_run.on ? ", enemies vanilla" : "");
}

static int pe_rand_init(void) {
    pe_menu_begin("Randomizer");
    pe_menu_top_page("Randomizer", "Shuffle items from a seed. Set before New Game.", 50);
    pe_menu_register_toggle(0, "Randomize new games", "New Game shuffles items with this seed. Applies the next time the game "
                            "starts; while on, no other mod runs (only Always run). A game keeps the seed it began with.", &s_enabled, NULL);
    /* the saved value is in s_enabled now: a randomized session is this mod and the original game only */
    s_alone = s_enabled && pe_mod_run_alone() == 0;
    pe_menu_register_digits(0, "Seed", "Six digits: the same seed gives the same items. Enter types a seed, "
                            "C copies it to share.", &s_seed, 6);
    pe_menu_register_action(0, "Random seed", "Pick a new seed.", pe_rand_random_seed);
    pe_menu_register_info(0, "This game", "The seed this game was started with. F1 changes only reach the next New Game.",
                          pe_rand_run_text);
    pe_menu_begin(0);
    pe_hook_add(PE_HOOK_SCENE_START, pe_rand_scene, NULL, PE_PRIORITY_NORMAL, MOD_ID);
    pe_hook_add(PE_HOOK_SCENE_START, pe_rand_doors, NULL, PE_PRIORITY_NORMAL, MOD_ID);
    pe_hook_add(PE_HOOK_SCENE_START, pe_rand_theater, NULL, PE_PRIORITY_NORMAL, MOD_ID);
    pe_hook_add(PE_HOOK_SCENE_START, pe_rand_blue_door, NULL, PE_PRIORITY_NORMAL, MOD_ID);
    pe_hook_add(PE_HOOK_SCENE_START, pe_rand_francis_door, NULL, PE_PRIORITY_NORMAL, MOD_ID);
    pe_hook_add(PE_HOOK_NEW_GAME, pe_rand_new_game, NULL, PE_PRIORITY_NORMAL, MOD_ID);
    pe_hook_add(PE_HOOK_SAVE_READ, pe_rand_save_read, NULL, PE_PRIORITY_NORMAL, MOD_ID);
    /* these read only: rows for keylogic.py */
    pe_hook_add(PE_HOOK_ITEM_ADD, pe_rand_item_add, NULL, PE_PRIORITY_EARLY, MOD_ID);
    pe_hook_add(PE_HOOK_ITEM_COUNT, pe_rand_item_count, NULL, PE_PRIORITY_EARLY, MOD_ID);
    pe_hook_add(PE_HOOK_INV_COMMAND, pe_rand_inv_command, NULL, PE_PRIORITY_EARLY, MOD_ID);
    pe_rand_battle_register();
    pe_rand_model_register();
    pe_rand_ai_register();
    return pe_save_register(&s_record) < 0 || pe_save_register(&s_foes_record) < 0;
}

const PeMod pe_mod_pe_randomizer = {MOD_ID, "Randomizer", "0.1", NULL, pe_rand_init, NULL};
