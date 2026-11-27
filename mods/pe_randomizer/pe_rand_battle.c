/* Battle randomizer, stats: an enemy actor of one room fights with another actor's numbers.
 *
 * An enemy is a room-script actor: op 111 gives it a battle slot (Entity_AllocSlot), then its script sets
 * its numbers with op 90, which lands in Entity_WriteFieldByCmd (PE_HOOK_ENEMY_FIELD). The stat block is
 * the codes in PE_RAND_STAT_CODES: HP, max HP, defense, ATB limit and speed, the status effect it inflicts
 * and its contact damage, exp and reward, drop and steal, and the status resistances (0x64-0x6d). The id
 * (0x29/0x2a: bestiary, Scan, the collection mod's keys), the model, the AI, the effect assets (0x32-0x37, PM
 * programs of its own room), the AI's own fields (0x40, 0x4a, 0x50, 0x51, 0x5e-0x60) and its size for reach
 * (0x6e: goes with the model) stay the actor's own. Attack power is not here: it lives in the actor's
 * animation event records (Script_SetObjAnimEntry12/5), set by its AI along with each attack.
 *
 * The setup runs in the frame op 111 does, and may set a code twice (M013's scripted Rat: a scaled HP,
 * then a flat 17). So the actor's own setup runs as written; its first field write marks the new slot,
 * and at the next frame (PE_HOOK_FRAME, or PE_HOOK_BATTLE_UPDATE, whichever comes first) the block is
 * written over it. Writes after that are the actor's AI at work (the Theater Actress sets her HP to
 * 1,000,000 mid-fight) and go through as written.
 * HP and defense are mostly (N0xa * k) >> 16, N0xa the room's difficulty scale (0x10000, or V0x50 when set,
 * the same in every room at a given point of the game): the block keeps k and the value is worked out from
 * the scale of the room the fight is in, so the swapped creature scales as it would at home. A source value
 * the scan couldn't read as a constant or k (a rolled drop or steal, a spawner's field) isn't in the
 * block, and the actor keeps its own for that code.
 *
 * Attack power (pe_rand_battle_attack): a slot whose script came from another foe scales each power that
 * script sets (PE_HOOK_ENEMY_POWER) by the slot's own attack power over that foe's (g_pe_rand_enemy_power,
 * both at this room's scale), so a Bat in a Rat's slot hits like the Rat. Left as written when either power
 * is unknown.
 */
#include "pe_mod_host.h"
#include "pe_game.h"
#include "pe_hooks.h"
#include "pe1/battle.h"
#include "pe1/battle_runtime.h"
#include "pe1/field_actor.h"
#include "pe1/script_node.h"
#include "pe_randomizer.h"
#include "pe_rand_enemies.h"

#define MOD_ID "pe_randomizer"
enum { PE_RAND_FORCED = 320, PE_RAND_SLOTS = 7 };

typedef struct PeRandBattleMap {
    uint16_t room;
    uint8_t entity;
    const PeRandEnemy *from;
    const PeRandEnemy *ai;   /* the foe whose script runs in the slot, or NULL: powers as written */
} PeRandBattleMap;
static PeRandBattleMap s_map[PE_RAND_FORCED];
static unsigned s_map_count;
/* Per battle slot: the allocation last seen (EnemyCombatant entityId), and its actor and block while the
 * block waits for the next frame. */
static uint8_t s_slot_instance[PE_RAND_SLOTS];
static FieldActor *s_pending_actor[PE_RAND_SLOTS];
static const PeRandEnemy *s_pending[PE_RAND_SLOTS];
static int s_writing;

static const PeRandEnemy *pe_rand_enemy(unsigned room, unsigned entity) {
    for (unsigned i = 0; i < PE_RAND_ENEMIES; ++i)
        if (g_pe_rand_enemies[i].room == room && g_pe_rand_enemies[i].entity == entity) return &g_pe_rand_enemies[i];
    return NULL;
}

int pe_rand_battle_force(unsigned room, unsigned entity, unsigned from_room, unsigned from_entity) {
    const PeRandEnemy *from = pe_rand_enemy(from_room, from_entity);
    if (!from || !pe_rand_enemy(room, entity) || s_map_count == PE_RAND_FORCED) return 0;
    s_map[s_map_count++] = (PeRandBattleMap){(uint16_t)room, (uint8_t)entity, from, NULL};
    return 1;
}

int pe_rand_battle_attack(unsigned room, unsigned entity, unsigned ai_room, unsigned ai_entity) {
    const PeRandEnemy *ai = pe_rand_enemy(ai_room, ai_entity);
    for (unsigned i = 0; i < s_map_count; ++i)
        if (s_map[i].room == room && s_map[i].entity == entity) { s_map[i].ai = ai; return ai != NULL; }
    return 0;
}

static int32_t pe_rand_scale(void) { return g_ScriptNodeActiveFlags[10] ? g_ScriptNodeActiveFlags[10] : 0x10000; }

/* An actor's attack power at this scale, 0 when unknown. */
static uint32_t pe_rand_power(const PeRandEnemy *e, int32_t scale) {
    const uint16_t *p = g_pe_rand_enemy_power[e - g_pe_rand_enemies];
    return p[1] ? ((uint32_t)scale * p[0]) >> 16 : p[0];
}

static const PeRandBattleMap *pe_rand_map_of(int room, unsigned entity) {
    for (unsigned i = 0; i < s_map_count; ++i)
        if (s_map[i].room == room && s_map[i].entity == entity) return &s_map[i];
    return NULL;
}

void pe_rand_battle_clear(void) {
    s_map_count = 0;
    for (int i = 0; i < PE_RAND_SLOTS; ++i) s_pending[i] = NULL;
}

static int pe_rand_slot_of(const FieldActor *actor) {
    for (int slot = 0; slot < PE_RAND_SLOTS; ++slot)
        if ((const void *)&g_BattleEnemySlots[slot].combatant == (const void *)actor->state) return slot;
    return -1;
}

/* A new slot of a mapped actor: its block goes in at the next frame. */
static int pe_rand_battle_field(void *ctx, void *user) {
    PeHookEnemyField *f = ctx;
    const PeRandEnemy *from = NULL;
    int room, slot;
    uint8_t instance;
    (void)user;
    if (!s_map_count || s_writing || !f->actor || !f->actor->state || (slot = pe_rand_slot_of(f->actor)) < 0) return 0;
    instance = g_BattleEnemySlots[slot].combatant.lowAndEntityId.bytes.entityId;
    if (s_slot_instance[slot] == instance) return 0;
    s_slot_instance[slot] = instance;
    room = pe_room_number();
    const PeRandBattleMap *map = pe_rand_map_of(room, f->actor->type_id);
    if (map) from = map->from;
    s_pending[slot] = from;
    s_pending_actor[slot] = f->actor;
    return 0;
}

static int pe_rand_battle_frame(void *ctx, void *user) {
    int32_t scale = pe_rand_scale();
    (void)ctx;
    (void)user;
    for (int slot = 0; slot < PE_RAND_SLOTS; ++slot) {
        const PeRandEnemy *from = s_pending[slot];
        FieldActor *actor = s_pending_actor[slot];
        if (!from) continue;
        s_pending[slot] = NULL;
        /* the slot still holds that allocation, for that actor */
        if (!g_BattleEnemySlots[slot].active || actor->state != (void *)&g_BattleEnemySlots[slot].combatant ||
            g_BattleEnemySlots[slot].combatant.lowAndEntityId.bytes.entityId != s_slot_instance[slot]) continue;
        s_writing = 1;
        for (int i = 0; i < PE_RAND_STATS; ++i) {
            if (!(from->have & (1u << i))) continue;
            int32_t v = from->scaled & (1u << i) ? (int32_t)((uint32_t)scale * (uint32_t)from->value[i]) >> 16 : from->value[i];
            Entity_WriteFieldByCmd(actor, PE_RAND_STAT_CODES[i], v);
        }
        s_writing = 0;
        const PeRandBattleMap *map = pe_rand_map_of(pe_room_number(), actor->type_id);
        if (map && map->ai)
            pe_log("rand_battle", "map=%d e%u stats of M%03u e%u, power %u for M%03u e%u's %u", pe_room_number(),
                   actor->type_id, from->room, from->entity, (unsigned)pe_rand_power(from, scale), map->ai->room,
                   map->ai->entity, (unsigned)pe_rand_power(map->ai, scale));
        else
            pe_log("rand_battle", "map=%d e%u stats of M%03u e%u", pe_room_number(), actor->type_id, from->room, from->entity);
    }
    return 0;
}

/* A moved foe's attack: the power its script sets, times the slot's own power over the foe's. */
static int pe_rand_battle_power(void *ctx, void *user) {
    PeHookEnemyPower *p = ctx;
    const PeRandBattleMap *map;
    int32_t scale = pe_rand_scale();
    uint32_t own, theirs, v;
    (void)user;
    if (!s_map_count || !p->actor || p->power <= 0 || !(map = pe_rand_map_of(pe_room_number(), p->actor->type_id)) ||
        !map->ai || !(own = pe_rand_power(map->from, scale)) || !(theirs = pe_rand_power(map->ai, scale)))
        return 0;
    v = ((uint32_t)p->power * own + theirs / 2) / theirs;
    p->power = v < 1 ? 1 : v > 0xFFFF ? 0xFFFF : (int)v;
    return 0;
}

static int pe_rand_battle_scene(void *ctx, void *user) {
    (void)ctx;
    (void)user;
    for (int i = 0; i < PE_RAND_SLOTS; ++i) { s_slot_instance[i] = 0; s_pending[i] = NULL; }
    return 0;
}

void pe_rand_battle_register(void) {
    pe_hook_add(PE_HOOK_SCENE_START, pe_rand_battle_scene, NULL, PE_PRIORITY_NORMAL, MOD_ID);
    pe_hook_add(PE_HOOK_ENEMY_FIELD, pe_rand_battle_field, NULL, PE_PRIORITY_NORMAL, MOD_ID);
    pe_hook_add(PE_HOOK_ENEMY_POWER, pe_rand_battle_power, NULL, PE_PRIORITY_NORMAL, MOD_ID);
    pe_hook_add(PE_HOOK_FRAME, pe_rand_battle_frame, NULL, PE_PRIORITY_NORMAL, MOD_ID);
    pe_hook_add(PE_HOOK_BATTLE_UPDATE, pe_rand_battle_frame, NULL, PE_PRIORITY_NORMAL, MOD_ID);
}
