/* Where a battle in an imported room is fought. See pe_battle_space.h. */
#include "pe_battle_space.h"

void pe_battle_space_to_host(const PeBattleSpace *s, double room_x, double room_z, double *host_x, double *host_z) {
    *host_x = s->anchor_x + (room_x - s->room_x) / s->k;
    *host_z = s->anchor_z + (room_z - s->room_z) / s->k;
}

void pe_battle_space_to_room(const PeBattleSpace *s, double host_x, double host_z, double *room_x, double *room_z) {
    *room_x = s->room_x + (host_x - s->anchor_x) * s->k;
    *room_z = s->room_z + (host_z - s->anchor_z) * s->k;
}

static int overlaps(const PeBattleBox *a, const PeBattleBox *b, double gap) {
    return a->x0 < b->x1 + gap && b->x0 < a->x1 + gap && a->z0 < b->z1 + gap && b->z0 < a->z1 + gap;
}

static int inside(const PeBattleBox *a) {
    const double lim = PE_BATTLE_SPACE_LIMIT - PE_BATTLE_SPACE_MARGIN;
    return a->x0 >= -lim && a->x1 <= lim && a->z0 >= -lim && a->z1 <= lim;
}

int pe_battle_space_choose(const PeBattleBox *host, const PeBattleBox *room, double room_x, double room_z,
                           double k, PeBattleSpace *out) {
    if (!host || !room || !out || !(k > 0)) return -1;
    /* the room's box relative to the player's spot in it, in host units */
    const double rx0 = (room->x0 - room_x) / k, rx1 = (room->x1 - room_x) / k;
    const double rz0 = (room->z0 - room_z) / k, rz1 = (room->z1 - room_z) / k;
    const double lim = PE_BATTLE_SPACE_LIMIT - PE_BATTLE_SPACE_MARGIN, gap = PE_BATTLE_SPACE_CLEARANCE;
    const double hx = (host->x0 + host->x1) / 2, hz = (host->z0 + host->z1) / 2;
    /* candidate anchors: the battle's box just past each side of the host room, centred on it the
     * other way; then pushed to the far edge of the space on that side, which leaves the most room */
    const double cand[8][2] = {
        {host->x1 + gap - rx0, hz - (rz0 + rz1) / 2}, {host->x0 - gap - rx1, hz - (rz0 + rz1) / 2},
        {hx - (rx0 + rx1) / 2, host->z1 + gap - rz0}, {hx - (rx0 + rx1) / 2, host->z0 - gap - rz1},
        {lim - rx1, 0}, {-lim - rx0, 0}, {0, lim - rz1}, {0, -lim - rz0},
    };
    for (int i = 0; i < 8; i++) {
        const PeBattleBox b = {cand[i][0] + rx0, cand[i][1] + rz0, cand[i][0] + rx1, cand[i][1] + rz1};
        if (!inside(&b) || overlaps(&b, host, gap)) continue;
        out->anchor_x = cand[i][0], out->anchor_z = cand[i][1];
        out->room_x = room_x, out->room_z = room_z, out->k = k;
        return 0;
    }
    return -1;
}
