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

#define PE_TRIP_FOES_MAX 3

typedef struct {
    int armed;                  /* a room to report to */
    int count, defeated;         /* selected placements, and how many actually spawned and died */
    uint32_t stage, room;
    char game[16];
    uint8_t slot[PE_TRIP_FOES_MAX];
} PeTripResult;

/* Keep up to PE_TRIP_FOES_MAX mapped placements whose packs the caller accepts, in room order.
 * Pass NULL for prepare to accept all mapped placements. */
int pe_trip_arm(PeTripResult *t, const PeReRoomEvent *room,
                int (*prepare)(const char *asset, int already_selected, const void *ctx), const void *ctx);
/* On victory, record how many consecutive selected foes spawned and died. On
 * escape or loss pass 0. */
void pe_trip_won(PeTripResult *t, int defeated);
/* Each frame after the trip: report the selected defeats. Returns the number reported,
 * 0 while the room isn't back yet, -1 when there's nothing to
 * report or it can't be (another room, no defeated_slot). */
int pe_trip_report(PeTripResult *t, const PeRePlayApi *api);

#endif
