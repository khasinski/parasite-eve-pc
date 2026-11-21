#ifndef PE_TRIP_RESULT_H
#define PE_TRIP_RESULT_H
/* A battle trip from an imported room (pe_re_play's "re_play" service, re_play_api.h): the fight
 * uses one of the room's placements. Keep the room and the chosen enemy's slot. After a
 * win, report it once the room runs again (the trip back is over): the slot still placed there is
 * beaten through defeated_slot(), its killed flag set, or for a boss with none its death seen by
 * the scripts (5-09's G, the lab lift's, 7-04's). Portable: only the service API. */
#include <stdint.h>

#include "re_play_api.h"

typedef struct {
    int armed;                  /* a room to report to */
    int won;                    /* the fight was won: report on the next running frame of that room */
    char game[16];
    uint32_t stage, room;
    uint8_t slot;
} PeTripResult;

/* Keep the imported room and the slot actually chosen for battle. 1 armed, 0 not. */
int pe_trip_arm(PeTripResult *t, const PeReRoomEvent *room, uint8_t slot);
/* The fight's end: won (1) or not (escape, loss: nothing is reported). */
void pe_trip_won(PeTripResult *t, int won);
/* Each frame after the trip: once the armed room runs again, beat its chosen slot. 1 if
 * beaten (and the result is spent), 0 while the room isn't back yet, -1 when there's nothing to
 * report or it can't be (another room, no defeated_slot). */
int pe_trip_report(PeTripResult *t, const PeRePlayApi *api);

#endif
