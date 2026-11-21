/* A won battle trip from an imported room, reported back to it. See pe_trip_result.h. */
#include <stddef.h>
#include <string.h>

#include "pe_imp_enemy.h"
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
    if (t->armed) t->defeated = defeated > 0 && defeated <= t->count ? defeated : 0;
}

int pe_trip_report(PeTripResult *t, const PeRePlayApi *api) {
    PeReRoomEvent room = {sizeof room};
    if (!t->armed || !t->defeated || !has_slot_api(api)) { t->armed = 0; return -1; }
    if (api->room(&room)) return 0;                        /* not running yet: the trip back, a door */
    t->armed = 0;
    if (!room.game || strcmp(room.game, t->game) || room.stage != t->stage || room.room != t->room) return -1;
    int reported = 0;
    for (int i = 0; i < t->defeated; i++) reported += !api->defeated_slot(t->slot[i]);
    return reported ? reported : -1;
}
