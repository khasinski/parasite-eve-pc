#ifndef PE_RE_ENCOUNTER_ROOMS_H
#define PE_RE_ENCOUNTER_ROOMS_H

#include "re_play_api.h"

enum { PE_RE_MESH_OFF, PE_RE_MESH_ALL, PE_RE_MESH_ENCOUNTERS };

/* Rooms audited for automatic encounters. The RDT still chooses the living roster. */
static inline int pe_re2_proximity_room(unsigned stage, unsigned room) {
    if (!stage) return room == 0;
    return stage == 1 && (room == 0x01 || room == 0x02 || room == 0x09 || room == 0x0a);
}

static inline int pe_re2_proximity_zombie(const PeReEnemyPlacement *enemy, unsigned stage) {
    return enemy->kind >= 0x10 && enemy->kind < 0x20 && enemy->slot < 16 &&
           enemy->killed_flag != 0xff && (!stage || !(enemy->behaviour & 0xc000));
}

#endif
