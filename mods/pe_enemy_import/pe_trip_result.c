/* A battle trip from an imported room, with defeats reported back to it. See pe_trip_result.h. */
#include <stddef.h>
#include <string.h>

#include "pe_imp_enemy.h"
#include "re_encounter_rooms.h"
#include "pe_trip_result.h"

static int has_slot_api(const PeRePlayApi *api) {
    return api && api->struct_size >= offsetof(PeRePlayApi, defeated_slot) + sizeof api->defeated_slot &&
           api->room && api->defeated_slot;
}

int pe_trip_arm(PeTripResult *t, const PeReRoomEvent *room,
                int (*prepare)(const char *asset, int already_selected, const char *entrance,
                               const PeReEnemyPlacement *placement, void *ctx), void *ctx) {
    memset(t, 0, sizeof *t);
    if (!room || !room->game || strlen(room->game) >= sizeof t->game) return 0;
    strcpy(t->game, room->game);
    t->stage = room->stage, t->room = room->room;
    uint8_t kinds[PE_TRIP_FOES_MAX];
    for (int i = 0; i < room->count && t->count < PE_TRIP_FOES_MAX; i++) {
        const PeReEnemyPlacement *e = &room->enemies[i];
        char asset[96];
        if (e->kind >= 0x40 || pe_imp_enemy_kind_asset(room->game, e->kind, asset, sizeof asset)) continue;
        if (pe_imp_enemy_body(asset) == PE_IMP_ENEMY_SCENE) continue;
        int duplicate = 0, already_selected = 0;
        for (int j = 0; j < t->count; j++) {
            duplicate |= t->slot[j] == e->slot;
            already_selected |= kinds[j] == e->kind;
        }
        if (duplicate) continue;
        const PeImpEnemyStart *start = pe_imp_enemy_starts(asset);
        while (start && start->name && (e->behaviour & start->mask) != start->value) start++;
        if (prepare && !prepare(asset, already_selected, start ? start->name : NULL, e, ctx)) continue;
        kinds[t->count] = e->kind;
        t->slot[t->count++] = e->slot;
    }
    t->armed = t->count > 0;
    return t->count;
}
void pe_trip_won(PeTripResult *t, int defeated) {
    pe_trip_defeated(t, defeated > 0 && defeated <= t->count ? (1u << defeated) - 1 : 0);
}

void pe_trip_defeated(PeTripResult *t, unsigned defeated) {
    if (t->armed) t->defeated = defeated & ((1u << t->count) - 1);
}

static const PeReEnemyPlacement *nearest_enemy(const PeReRoomEvent *room, double x, double z, int floor,
                                                double limit, unsigned selected,
                                                int (*reachable)(double, double, double, double)) {
    const PeReEnemyPlacement *nearest = NULL;
    for (int i = 0; i < room->count; i++) {
        const PeReEnemyPlacement *e = &room->enemies[i];
        if (!pe_re2_proximity_enemy(e, pe_re2_opening_street(room->stage, room->room)) || e->floor != floor ||
            (selected & (1u << e->slot))) continue;
        const double dx = e->x - x, dz = e->z - z, distance = dx * dx + dz * dz;
        if (distance > limit || (nearest && distance == limit)) continue;
        if (reachable && !reachable(x, z, e->x, e->z)) continue;
        nearest = e;
        limit = distance;
    }
    return nearest;
}

int pe_trip_approach(const PeReRoomEvent *room, double x, double z, int floor,
                     int (*reachable)(double, double, double, double), PeReEnemyPlacement *out) {
    if (strcmp(room->game, "re2")) return 0;
    const int street = pe_re2_opening_street(room->stage, room->room);
    if (street) reachable = NULL;
    else if (!reachable) return 0;
    const double radius = street ? 1600 : 1400;
    const double group_radius = street ? 6500 : 3900;
    const PeReEnemyPlacement *lead = nearest_enemy(room, x, z, floor, radius * radius, 0, reachable);
    if (!lead) return 0;
    out[0] = *lead;
    int count = 1;
    unsigned selected = 1u << lead->slot;
    while (count < PE_TRIP_FOES_MAX) {
        const PeReEnemyPlacement *neighbor = nearest_enemy(room, lead->x, lead->z, floor,
                                                            group_radius * group_radius, selected, reachable);
        if (!neighbor) break;
        out[count++] = *neighbor;
        selected |= 1u << neighbor->slot;
    }
    return count;
}

int pe_trip_report(PeTripResult *t, const PeRePlayApi *api) {
    PeReRoomEvent room = {sizeof room};
    if (!t->armed || !t->defeated || !has_slot_api(api)) { t->armed = 0; return -1; }
    if (api->room(&room)) return 0;                        /* not running yet: the trip back, a door */
    t->armed = 0;
    if (!room.game || strcmp(room.game, t->game) || room.stage != t->stage || room.room != t->room) return -1;
    int reported = 0;
    for (int i = 0; i < t->count; i++) {
        if (!(t->defeated & (1u << i))) continue;
        if (t->canister_detonated && api->struct_size >= sizeof *api && api->defeated_by_canister) {
            if (!api->defeated_by_canister(t->slot[i])) { reported++; continue; }
        }
        reported += !api->defeated_slot(t->slot[i]);
    }
    return reported ? reported : -1;
}
