#ifndef PE_RE_ENCOUNTER_ROOMS_H
#define PE_RE_ENCOUNTER_ROOMS_H

#include "re_play_api.h"

enum { PE_RE_MESH_OFF, PE_RE_MESH_ALL, PE_RE_MESH_ENCOUNTERS };

static inline int pe_re2_opening_street(unsigned stage, unsigned room) {
    return !stage && !room;
}

/* Ordinary combat placements with a persistent defeat flag. Bosses and
 * dependent parts use their own scene rules and are never picked by distance. */
static inline int pe_re2_proximity_enemy(const PeReEnemyPlacement *enemy, int street) {
    if (enemy->slot >= 16 || enemy->killed_flag == 0xff || (!street && (enemy->behaviour & 0xc000))) return 0;
    const unsigned kind = enemy->kind;
    if (street) return kind >= 0x10 && kind < 0x20;
    return (kind >= 0x10 && kind <= 0x22) || kind == 0x24 || kind == 0x25 ||
           kind == 0x2e || (kind >= 0x39 && kind <= 0x3b);
}

#endif
