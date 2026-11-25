#include <math.h>
#include <string.h>

#include "pe_re_battle.h"

const PeReBattleRules *pe_re_battle_rules(const char *game) {
    return game && !strcmp(game, pe_re_battle_re2_rules.game) ? &pe_re_battle_re2_rules : NULL;
}

static void join(PeReBattle *battle, const PeReEnemyPlacement *enemy) {
    battle->foe[enemy->slot] = (PeReBattleFoe){enemy->slot, PE_RE_BATTLE_FOE_JOINED, enemy->x, enemy->z};
    battle->initial_slots[battle->initial_count++] = enemy->slot;
    ++battle->joined;
}

int pe_re_battle_begin(PeReBattle *battle, PeReSession *s, const PeReBattleRules *rules,
                       const PeReEnemyPlacement *lead, int (*in_view)(const PeReEnemyPlacement *)) {
    if (!pe_re_battle_encounter_allowed(battle) || !rules || !s->game ||
        strcmp(s->game->game, rules->game) || !lead || lead->slot >= PE_RE_ENEMIES_MAX ||
        lead->floor != s->floor || !rules->combatant(s, lead) ||
        rules->max_foes < 1 || rules->max_foes > PE_RE_ENEMIES_MAX)
        return -1;

    *battle = (PeReBattle){
        .phase = PE_RE_BATTLE_STARTING, .rules = rules,
        .stage = s->stage, .room = s->room, .floor = s->floor,
        .aya_x = s->x, .aya_z = s->z, .camera = s->camera,
    };
    join(battle, lead);
    if (!in_view) return 0;

    PeReEnemyPlacement enemies[PE_RE_ENEMIES_MAX];
    const int count = pe_re_session_enemies(s, enemies, PE_RE_ENEMIES_MAX);
    int visible[PE_RE_ENEMIES_MAX] = {0};
    for (int i = 0; i < count; ++i) {
        const PeReEnemyPlacement *enemy = &enemies[i];
        visible[i] = enemy->slot < PE_RE_ENEMIES_MAX && enemy->slot != lead->slot &&
                     enemy->floor == battle->floor && rules->combatant(s, enemy) && in_view(enemy) == 1;
    }
    while (battle->joined < rules->max_foes) {
        const PeReEnemyPlacement *next = NULL;
        double nearest = 0;
        for (int i = 0; i < count; ++i) {
            const PeReEnemyPlacement *enemy = &enemies[i];
            if (!visible[i] || battle->foe[enemy->slot].state != PE_RE_BATTLE_FOE_NONE) continue;
            const double dx = enemy->x - lead->x, dz = enemy->z - lead->z;
            const double distance = dx * dx + dz * dz;
            if (!next || distance < nearest) next = enemy, nearest = distance;
        }
        if (!next) break;
        join(battle, next);
    }
    return 0;
}

int pe_re_battle_step(PeReBattle *battle, PeReSession *s, double x, double z, int time_runs,
                      int (*in_view)(const PeReEnemyPlacement *), PeReBattleEvent *ev, int max) {
    (void)s;
    (void)time_runs;
    (void)in_view;
    (void)ev;
    (void)max;
    if (battle->phase == PE_RE_BATTLE_STARTING) battle->phase = PE_RE_BATTLE_RUNNING;
    if (battle->phase == PE_RE_BATTLE_RUNNING) battle->aya_x = x, battle->aya_z = z;
    return 0;
}

void pe_re_battle_foe_moved(PeReBattle *battle, uint8_t slot, double x, double z) {
    if (slot >= PE_RE_ENEMIES_MAX || battle->foe[slot].state != PE_RE_BATTLE_FOE_JOINED) return;
    battle->foe[slot].x = x, battle->foe[slot].z = z;
}

void pe_re_battle_foe_died(PeReBattle *battle, uint8_t slot) {
    if (slot >= PE_RE_ENEMIES_MAX || battle->foe[slot].state != PE_RE_BATTLE_FOE_JOINED) return;
    battle->foe[slot].state = PE_RE_BATTLE_FOE_DEAD;
    if (!--battle->joined) battle->phase = PE_RE_BATTLE_ENDING;
}

void pe_re_battle_end(PeReBattle *battle, PeReSession *s, int escaped) {
    (void)s;
    (void)escaped;
    *battle = (PeReBattle){0};
}

int pe_re_battle_encounter_allowed(const PeReBattle *battle) {
    return battle->phase == PE_RE_BATTLE_IDLE;
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
