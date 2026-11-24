#ifndef PE_RE_ENCOUNTER_ROOMS_H
#define PE_RE_ENCOUNTER_ROOMS_H

#include "re_play_api.h"

enum { PE_RE_MESH_OFF, PE_RE_MESH_ALL, PE_RE_MESH_ENCOUNTERS };

/* Room-script behaviours decide which placements can use these ground routines. */
static inline int pe_re2_ivy(unsigned kind) {
    return kind == 0x2e || kind == 0x39;
}

static inline int pe_re2_ground_pursuit_kind(unsigned kind) {
    return (kind >= 0x10 && kind <= 0x20) || kind == 0x22 || kind == 0x24 || pe_re2_ivy(kind);
}

static inline int pe_re2_opening_street(unsigned stage, unsigned room) {
    return !stage && !room;
}

/* EM2A's scripted and parked forms share the combat model, but only released
 * placements with their own defeat flag are eligible for a battle. */
static inline int pe_re2_mr_x_fightable(const PeReEnemyPlacement *enemy) {
    return enemy->kind == 0x2a && enemy->slot < 16 && enemy->killed_flag != 0xff &&
           !(enemy->behaviour & 0xc000);
}

/* EM27.C: types 0 and 3 can attack. The story entrance uses type 1. */
static inline int pe_re2_g_embryo_combat_type(const PeReEnemyPlacement *enemy) {
    const unsigned type = enemy->behaviour & 0x3f;
    return !(enemy->behaviour & 0xc000) && (type == 0 || type == 3);
}

/* The first adult G placement has an unflagged offspring at the same spot. */
static inline int pe_re2_g_attached_embryo(unsigned stage, unsigned room, const PeReEnemyPlacement *enemy) {
    return stage == 2 && (room == 3 || room == 9) && enemy->slot == 1 && enemy->kind == 0x27 &&
           enemy->behaviour == 0 && enemy->killed_flag == 0xff;
}

static inline int pe_re2_scene_only_enemy(const PeReEnemyPlacement *enemy) {
    switch (enemy->kind) {
    case 0x22:
    case 0x28: return (enemy->behaviour & 0xc000) != 0;
    case 0x27: return !pe_re2_g_embryo_combat_type(enemy);
    case 0x2a: return !pe_re2_mr_x_fightable(enemy);
    case 0x2d: /* wall arms */
    case 0x29: /* traversal swarms */
    case 0x30: /* G cutscene appearances */
    case 0x37: /* final G's parked tentacles */
        return enemy->killed_flag == 0xff;
    default: return 0;
    }
}

/* Ordinary combat placements with a persistent defeat flag. Scripted bosses and
 * dependent parts use their own scene rules. */
static inline int pe_re2_proximity_enemy(const PeReEnemyPlacement *enemy, int street) {
    if (enemy->slot >= 16 || enemy->killed_flag == 0xff || (!street && (enemy->behaviour & 0xc000))) return 0;
    const unsigned kind = enemy->kind;
    if (street) return kind >= 0x10 && kind < 0x20;
    return (kind >= 0x10 && kind <= 0x22) || kind == 0x24 || kind == 0x25 ||
           (kind == 0x27 && pe_re2_g_embryo_combat_type(enemy)) || kind == 0x28 ||
           pe_re2_mr_x_fightable(enemy) || kind == 0x2e || (kind >= 0x39 && kind <= 0x3b);
}

#endif
