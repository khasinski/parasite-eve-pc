#ifndef PE_TRIP_RESULT_H
#define PE_TRIP_RESULT_H
/* A battle trip from an imported room (pe_re_play's "re_play" service, re_play_api.h): the fight
 * stands in for one of the room's enemies. Arm it before pausing the room.
 * After a win, report it once that room runs again. Its selected slot is
 * beaten through defeated_slot(), its killed flag set, or for a boss with none its death seen by
 * the scripts (5-09's G, the lab lift's, 7-04's). Portable: only the service API. */
#include <stddef.h>
#include <stdint.h>

#include "re_play_api.h"

typedef struct {
    int armed;                  /* a room to report to */
    int won;                    /* the fight was won: report on the next running frame of that room */
    uint32_t stage, room;
    char game[16];
    uint8_t slot;
} PeTripResult;

/* Keep the room and the selected placement. 1 armed, 0 invalid. */
int pe_trip_arm(PeTripResult *t, const PeReRoomEvent *room, int placement);
/* The fight's end: won (1) or not (escape, loss: nothing is reported). */
void pe_trip_won(PeTripResult *t, int won);
/* Each frame after the trip: report the selected defeat. Returns 1 when reported,
 * 0 while the room isn't back yet, -1 when there's nothing to
 * report or it can't be (another room, no defeated_slot). */
int pe_trip_report(PeTripResult *t, const PeRePlayApi *api);

/* The creature a trip from `room` fights: its first placement a game maps to a model
 * (pe_imp_enemy_kind_asset), people (kind 0x40 and up) left out. `asset_id` gets the model
 * ("re2:enemy/em31"). The placement's index, or -1 (no room, nothing mapped: the player's pick). */
int pe_trip_creature(const PeReRoomEvent *room, char *asset_id, size_t n);

#endif
