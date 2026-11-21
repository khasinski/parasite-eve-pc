/* A won battle trip from an imported room, reported back to it. See pe_trip_result.h. */
#include <stddef.h>
#include <string.h>

#include "pe_trip_result.h"

static int has_slot_api(const PeRePlayApi *api) {
    return api && api->struct_size >= offsetof(PeRePlayApi, defeated_slot) + sizeof api->defeated_slot &&
           api->room && api->defeated_slot;
}

int pe_trip_arm(PeTripResult *t, const PeReRoomEvent *room, uint8_t slot) {
    memset(t, 0, sizeof *t);
    if (!room || !room->game || strlen(room->game) >= sizeof t->game) return 0;
    for (int i = 0; i < room->count; i++) {
        if (room->enemies[i].slot != slot) continue;
        strcpy(t->game, room->game);
        t->armed = 1, t->stage = room->stage, t->room = room->room;
        t->slot = slot;
        return 1;
    }
    return 0;
}

void pe_trip_won(PeTripResult *t, int won) {
    if (t->armed) t->won = won != 0;
}

int pe_trip_report(PeTripResult *t, const PeRePlayApi *api) {
    PeReRoomEvent room = {sizeof room};
    if (!t->armed || !t->won || !has_slot_api(api)) { t->armed = 0; return -1; }
    if (api->room(&room)) return 0;                        /* not running yet: the trip back, a door */
    t->armed = 0;
    if (!room.game || strcmp(room.game, t->game) || room.stage != t->stage || room.room != t->room) return -1;
    return api->defeated_slot(t->slot) ? -1 : 1;
}
