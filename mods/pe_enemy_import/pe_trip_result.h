#ifndef PE_TRIP_RESULT_H
#define PE_TRIP_RESULT_H
/* A battle trip from an imported room (pe_re_play's "re_play" service, re_play_api.h): the fight
 * stands in for a group of the room's enemies. Arm it before pausing the room.
 * After a win, report it once that room runs again. Each defeated slot is
 * beaten through defeated_slot(), its killed flag set, or for a boss with none its death seen by
 * the scripts (5-09's G, the lab lift's, 7-04's). Portable: only the service API. */
#include <stddef.h>
#include <stdint.h>

#include "re_play_api.h"

#define PE_TRIP_FOES_MAX 3          /* the current host can share one pack across these foes */

typedef struct {
    int armed;                  /* a room to report to */
    int count, defeated;         /* selected placements, and how many actually spawned and died */
    uint32_t stage, room;
    char game[16];
    uint8_t slot[PE_TRIP_FOES_MAX];
} PeTripResult;

/* Keep the room and up to PE_TRIP_FOES_MAX placements using the selected model.
 * Returns the number selected, or 0 when invalid. */
int pe_trip_arm(PeTripResult *t, const PeReRoomEvent *room, int placement);
/* On victory, record how many consecutive selected foes spawned and died. On
 * escape or loss pass 0. */
void pe_trip_won(PeTripResult *t, int defeated);
/* Each frame after the trip: report the selected defeats. Returns the number reported,
 * 0 while the room isn't back yet, -1 when there's nothing to
 * report or it can't be (another room, no defeated_slot). */
int pe_trip_report(PeTripResult *t, const PeRePlayApi *api);

/* The creature a trip from `room` fights: its first placement a game maps to a model
 * (pe_imp_enemy_kind_asset), people (kind 0x40 and up) left out. `asset_id` gets the model
 * ("re2:enemy/em31"). The placement's index, or -1 (no room, nothing mapped: the player's pick). */
int pe_trip_creature(const PeReRoomEvent *room, char *asset_id, size_t n);

#endif
