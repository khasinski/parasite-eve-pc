#include <limits.h>
#include <math.h>
#include <string.h>

#include "pe_re_battle.h"

const PeReBattleRules *pe_re_battle_rules(const char *game) {
    return game && !strcmp(game, pe_re_battle_re2_rules.game) ? &pe_re_battle_re2_rules : NULL;
}

static double distance_squared(double x, double z, double tx, double tz) {
    const double dx = x - tx, dz = z - tz;
    return dx * dx + dz * dz;
}

static int same_room(const PeReBattle *battle, const PeReSession *s) {
    return s->game && battle->rules && !strcmp(s->game->game, battle->rules->game) &&
           battle->stage == s->stage && battle->room == s->room;
}

static int16_t room_coord(double value) {
    return (int16_t)lround(fmin(INT16_MAX, fmax(INT16_MIN, value)));
}

static void join(PeReBattle *battle, const PeReEnemyPlacement *enemy, int visible) {
    battle->foe[enemy->slot] = (PeReBattleFoe){
        .slot = enemy->slot, .state = PE_RE_BATTLE_FOE_JOINED,
        .x = enemy->x, .z = enemy->z, .visible = visible,
    };
    ++battle->joined;
}

static void join_initial(PeReBattle *battle, const PeReEnemyPlacement *enemy, int visible) {
    join(battle, enemy, visible);
    battle->initial_slots[battle->initial_count++] = enemy->slot;
}

void pe_re_battle_init(PeReBattle *battle) {
    *battle = (PeReBattle){.settings = {3.0, 4000.0, 1}};
}

static void queue_visible(PeReBattle *battle, const PeReEnemyPlacement *const *by_slot) {
    for (;;) {
        int best = -1;
        double nearest = 0;
        for (int slot = 0; slot < PE_RE_ENEMIES_MAX; ++slot) {
            const PeReEnemyPlacement *enemy = by_slot[slot];
            if (!enemy || !battle->foe[slot].visible || battle->foe[slot].state != PE_RE_BATTLE_FOE_NONE) continue;
            const double distance = distance_squared(enemy->x, enemy->z, battle->aya_x, battle->aya_z);
            if (best < 0 || distance < nearest) best = slot, nearest = distance;
        }
        if (best < 0) return;
        const PeReEnemyPlacement *enemy = by_slot[best];
        battle->foe[best] = (PeReBattleFoe){
            .slot = (uint8_t)best, .state = PE_RE_BATTLE_FOE_QUEUED,
            .x = enemy->x, .z = enemy->z, .visible = 1,
        };
        battle->waiting[battle->waiting_count++] = (uint8_t)best;
    }
}

int pe_re_battle_begin(PeReBattle *battle, PeReSession *s, const PeReBattleRules *rules,
                       const PeReEnemyPlacement *lead, int (*in_view)(const PeReEnemyPlacement *)) {
    if (!rules || !s->game || strcmp(s->game->game, rules->game) ||
        !lead || lead->slot >= PE_RE_ENEMIES_MAX || lead->floor != s->floor ||
        !pe_re_battle_encounter_allowed_for(battle, s, lead->slot) || !rules->combatant(s, lead) ||
        rules->max_foes < 1 || rules->max_foes > PE_RE_ENEMIES_MAX)
        return -1;

    const int retain_grace = same_room(battle, s);
    if (!retain_grace) {
        memset(battle->grace, 0, sizeof battle->grace);
        battle->grace_frames = 0;
    }
    battle->phase = PE_RE_BATTLE_STARTING;
    battle->rules = rules;
    battle->stage = s->stage, battle->room = s->room, battle->floor = s->floor;
    battle->aya_x = s->x, battle->aya_z = s->z, battle->camera = s->camera;
    battle->joined = battle->initial_count = battle->waiting_count = 0;
    memset(battle->foe, 0, sizeof battle->foe);
    join_initial(battle, lead, in_view && in_view(lead) == 1);
    if (!in_view) return 0;

    PeReEnemyPlacement enemies[PE_RE_ENEMIES_MAX];
    const int count = pe_re_session_enemies(s, enemies, PE_RE_ENEMIES_MAX);
    const PeReEnemyPlacement *visible[PE_RE_ENEMIES_MAX] = {0};
    for (int i = 0; i < count; ++i) {
        const PeReEnemyPlacement *enemy = &enemies[i];
        if (enemy->slot >= PE_RE_ENEMIES_MAX || enemy->slot == lead->slot || visible[enemy->slot] ||
            enemy->floor != battle->floor || !rules->combatant(s, enemy) || in_view(enemy) != 1)
            continue;
        visible[enemy->slot] = enemy;
        battle->foe[enemy->slot].visible = 1;
    }
    while (battle->joined < rules->max_foes) {
        const PeReEnemyPlacement *next = NULL;
        double nearest = 0;
        for (int i = 0; i < count; ++i) {
            const PeReEnemyPlacement *enemy = &enemies[i];
            if (enemy->slot >= PE_RE_ENEMIES_MAX || visible[enemy->slot] != enemy ||
                battle->foe[enemy->slot].state != PE_RE_BATTLE_FOE_NONE) continue;
            const double distance = distance_squared(enemy->x, enemy->z, lead->x, lead->z);
            if (!next || distance < nearest) next = enemy, nearest = distance;
        }
        if (!next) break;
        join_initial(battle, next, 1);
    }
    queue_visible(battle, visible);
    return 0;
}

static void save_foe_position(PeReSession *s, const PeReBattleFoe *foe) {
    int16_t *work = s->run.works[2 + foe->slot];
    if (!work[0] || s->run.em_dead[foe->slot]) return;
    work[PE_RE_MEMBER_POS_X] = room_coord(foe->x);
    work[PE_RE_MEMBER_POS_Z] = room_coord(foe->z);
}

static void update_grace(PeReBattle *battle, const PeReEnemyPlacement *enemies, int count,
                         double x, double z) {
    battle->grace_frames = 0;
    for (unsigned slot = 0; slot < PE_RE_ENEMIES_MAX; ++slot) {
        PeReBattleGrace *grace = &battle->grace[slot];
        if (!grace->frames) continue;
        int i = 0;
        while (i < count && enemies[i].slot != slot) ++i;
        if (i == count || distance_squared(x, z, enemies[i].x, enemies[i].z) >= grace->distance * grace->distance)
            grace->frames = 0;
        else
            --grace->frames;
        if (grace->frames > battle->grace_frames) battle->grace_frames = grace->frames;
    }
}

static void chase_unjoined(PeReBattle *battle, PeReSession *s, PeReEnemyPlacement *enemies, int count) {
    PeReEnemyPlacement moving[PE_RE_ENEMIES_MAX];
    int index[PE_RE_ENEMIES_MAX], n = 0;
    for (int i = 0; i < count; ++i) {
        const PeReEnemyPlacement *enemy = &enemies[i];
        if (enemy->slot >= PE_RE_ENEMIES_MAX || enemy->floor != battle->floor) continue;
        const PeReBattleFoeState state = battle->foe[enemy->slot].state;
        if ((state != PE_RE_BATTLE_FOE_NONE && state != PE_RE_BATTLE_FOE_QUEUED) ||
            !battle->rules->pursues(s, enemy)) continue;
        moving[n] = *enemy;
        index[n++] = i;
    }
    if (!n) return;
    battle->rules->chase_step(s, moving, n, battle->aya_x, battle->aya_z, battle->floor);
    for (int i = 0; i < n; ++i) enemies[index[i]] = moving[i];
}

int pe_re_battle_step(PeReBattle *battle, PeReSession *s, double x, double z, int time_runs,
                      int (*in_view)(const PeReEnemyPlacement *), PeReBattleEvent *ev, int max) {
    if (!same_room(battle, s)) {
        if (battle->phase == PE_RE_BATTLE_IDLE || battle->phase == PE_RE_BATTLE_GRACE) {
            memset(battle->grace, 0, sizeof battle->grace);
            battle->grace_frames = 0;
            battle->phase = PE_RE_BATTLE_IDLE;
        }
        return 0;
    }
    if (battle->phase == PE_RE_BATTLE_IDLE || battle->phase == PE_RE_BATTLE_ENDING) return 0;

    battle->aya_x = x, battle->aya_z = z;
    PeReEnemyPlacement enemies[PE_RE_ENEMIES_MAX];
    const int count = pe_re_session_enemies(s, enemies, PE_RE_ENEMIES_MAX);
    update_grace(battle, enemies, count, x, z);
    if (battle->phase == PE_RE_BATTLE_GRACE) {
        if (!battle->grace_frames) battle->phase = PE_RE_BATTLE_IDLE;
        return 0;
    }
    battle->phase = PE_RE_BATTLE_RUNNING;
    if (!ev) max = 0;
    if (time_runs && battle->rules->pursues && battle->rules->chase_step)
        chase_unjoined(battle, s, enemies, count);

    const PeReEnemyPlacement *by_slot[PE_RE_ENEMIES_MAX] = {0};
    for (int slot = 0; slot < PE_RE_ENEMIES_MAX; ++slot) battle->foe[slot].visible = 0;
    for (int i = 0; i < count; ++i) {
        PeReEnemyPlacement *enemy = &enemies[i];
        if (enemy->slot >= PE_RE_ENEMIES_MAX || enemy->floor != battle->floor || by_slot[enemy->slot]) continue;
        PeReBattleFoe *foe = &battle->foe[enemy->slot];
        if (foe->state == PE_RE_BATTLE_FOE_DEAD) continue;
        if (foe->state == PE_RE_BATTLE_FOE_JOINED) {
            enemy->x = room_coord(foe->x);
            enemy->z = room_coord(foe->z);
        } else if (!battle->rules->combatant(s, enemy)) {
            continue;
        }
        foe->visible = in_view && in_view(enemy) == 1;
        by_slot[enemy->slot] = enemy;
    }
    queue_visible(battle, by_slot);

    int emitted = 0, pending = 0;
    for (int i = 0; i < battle->waiting_count; ++i) {
        const uint8_t slot = battle->waiting[i];
        const PeReEnemyPlacement *enemy = by_slot[slot];
        if (!enemy) {
            battle->foe[slot] = (PeReBattleFoe){0};
        } else if (battle->foe[slot].visible && emitted < max && battle->joined < battle->rules->max_foes) {
            join(battle, enemy, 1);
            ev[emitted++] = (PeReBattleEvent){.kind = PE_RE_BATTLE_EV_JOIN, .slot = slot};
        } else {
            battle->waiting[pending++] = slot;
        }
    }
    battle->waiting_count = pending;
    if (!battle->joined && emitted < max) {
        battle->phase = PE_RE_BATTLE_ENDING;
        ev[emitted++] = (PeReBattleEvent){.kind = PE_RE_BATTLE_EV_END};
    }
    return emitted;
}

void pe_re_battle_foe_moved(PeReBattle *battle, uint8_t slot, double x, double z) {
    if (slot >= PE_RE_ENEMIES_MAX || battle->foe[slot].state != PE_RE_BATTLE_FOE_JOINED) return;
    if (isfinite(x) && isfinite(z)) battle->foe[slot].x = x, battle->foe[slot].z = z;
}

void pe_re_battle_foe_died(PeReBattle *battle, uint8_t slot) {
    if (slot >= PE_RE_ENEMIES_MAX || battle->foe[slot].state != PE_RE_BATTLE_FOE_JOINED) return;
    battle->foe[slot].state = PE_RE_BATTLE_FOE_DEAD;
    battle->grace[slot] = (PeReBattleGrace){0};
    --battle->joined;
}

void pe_re_battle_end(PeReBattle *battle, PeReSession *s, int escaped) {
    if (pe_re_battle_encounter_allowed(battle)) return;
    if (escaped && same_room(battle, s)) {
        for (unsigned slot = 0; slot < PE_RE_ENEMIES_MAX; ++slot) {
            const PeReBattleFoe *foe = &battle->foe[slot];
            if (foe->state != PE_RE_BATTLE_FOE_JOINED) continue;
            save_foe_position(s, foe);
            PeReBattleGrace *grace = &battle->grace[slot];
            const double seconds = battle->settings.seconds, distance = battle->settings.distance;
            const int shifts = battle->settings.halve_on_repeat ? (int)grace->escapes : 0;
            if (grace->escapes < 1024) ++grace->escapes;
            const double frames = ldexp(seconds, -shifts) * 60.0;
            grace->distance = isfinite(distance) && distance > 0 ? ldexp(distance, -shifts) : 0;
            grace->frames = isfinite(seconds) && seconds > 0 && grace->distance > 0 ?
                            (int)fmin(INT_MAX, ceil(frames)) : 0;
        }
        s->x = battle->aya_x, s->z = battle->aya_z;
    }
    battle->grace_frames = 0;
    for (int i = 0; i < PE_RE_ENEMIES_MAX; ++i)
        if (battle->grace[i].frames > battle->grace_frames) battle->grace_frames = battle->grace[i].frames;
    battle->phase = battle->grace_frames ? PE_RE_BATTLE_GRACE : PE_RE_BATTLE_IDLE;
    battle->joined = battle->initial_count = battle->waiting_count = 0;
    memset(battle->foe, 0, sizeof battle->foe);
}

int pe_re_battle_encounter_allowed(const PeReBattle *battle) {
    return battle->phase == PE_RE_BATTLE_IDLE || battle->phase == PE_RE_BATTLE_GRACE;
}

int pe_re_battle_encounter_allowed_for(const PeReBattle *battle, const PeReSession *s, uint8_t slot) {
    return slot < PE_RE_ENEMIES_MAX && pe_re_battle_encounter_allowed(battle) &&
           (!same_room(battle, s) || !battle->grace[slot].frames);
}

int pe_re_battle_target_order(const PeReBattle *battle, uint8_t *slots, int max) {
    if (!slots) return 0;
    int count = 0;
    unsigned selected = 0;
    while (count < max) {
        const PeReBattleFoe *best = NULL;
        double nearest = 0;
        for (unsigned slot = 0; slot < PE_RE_ENEMIES_MAX; ++slot) {
            const PeReBattleFoe *foe = &battle->foe[slot];
            if ((selected & (1u << slot)) || foe->state != PE_RE_BATTLE_FOE_JOINED) continue;
            const double distance = distance_squared(foe->x, foe->z, battle->aya_x, battle->aya_z);
            if (!best || foe->visible > best->visible || (foe->visible == best->visible && distance < nearest))
                best = foe, nearest = distance;
        }
        if (!best) break;
        slots[count++] = best->slot;
        selected |= 1u << best->slot;
    }
    return count;
}

static int valid_bounds(PeReBattleBounds bounds) {
    return isfinite(bounds.min.x) && isfinite(bounds.min.z) &&
           isfinite(bounds.max.x) && isfinite(bounds.max.z) &&
           bounds.min.x <= bounds.max.x && bounds.min.z <= bounds.max.z;
}

int pe_re_battle_anchor(PeReBattleBounds room, PeReBattleBounds host, PeReBattleBounds zones,
                        double scale, double clearance, PeReBattlePoint *out) {
    if (!valid_bounds(room) || !valid_bounds(host) || !valid_bounds(zones) ||
        !isfinite(scale) || scale <= 0 || !isfinite(clearance) || clearance < 0 || !out)
        return 0;

    const PeReBattleBounds available = {
        {host.min.x - room.min.x / scale, host.min.z - room.min.z / scale},
        {host.max.x - room.max.x / scale, host.max.z - room.max.z / scale},
    };
    const double left = zones.min.x - clearance - room.max.x / scale;
    const double right = zones.max.x + clearance - room.min.x / scale;
    const double above = zones.min.z - clearance - room.max.z / scale;
    const double below = zones.max.z + clearance - room.min.z / scale;
    const PeReBattleBounds sides[] = {
        {available.min, {fmin(available.max.x, left), available.max.z}},
        {{fmax(available.min.x, right), available.min.z}, available.max},
        {available.min, {available.max.x, fmin(available.max.z, above)}},
        {{available.min.x, fmax(available.min.z, below)}, available.max},
    };
    for (unsigned i = 0; i < sizeof sides / sizeof *sides; ++i) {
        if (!valid_bounds(sides[i])) continue;
        out->x = fmax(sides[i].min.x, fmin(0, sides[i].max.x));
        out->z = fmax(sides[i].min.z, fmin(0, sides[i].max.z));
        return 1;
    }
    return 0;
}

PeReBattlePoint pe_re_battle_to_host(PeReBattlePoint room, PeReBattlePoint origin,
                                    PeReBattlePoint anchor, double scale) {
    return (PeReBattlePoint){anchor.x + (room.x - origin.x) / scale, anchor.z + (room.z - origin.z) / scale};
}

PeReBattlePoint pe_re_battle_to_room(PeReBattlePoint host, PeReBattlePoint origin,
                                    PeReBattlePoint anchor, double scale) {
    return (PeReBattlePoint){origin.x + (host.x - anchor.x) * scale, origin.z + (host.z - anchor.z) * scale};
}
