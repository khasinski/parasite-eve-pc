#ifndef PE_RE_ZONE_H
#define PE_RE_ZONE_H
/* RE2's zone check: which of a room's zones the player is in, as the game decides it each frame
 * (bio2-ds SCE_AT.C Sce_at / Sce_at_check and def/SCENARIO.H, read for behaviour only, no code
 * taken). PE-free, so it runs in tests.
 *
 * A zone counts when its instruction ran (pe_re_scd.h; every record when the scripts didn't run),
 * with any aot_reset of its id applied (kind, check bits, data). Its check bits (sat) say:
 *   0x01 the player sets it off (0x02 enemies, 0x04 objects: not checked here)
 *   0x10 only when the player examines (presses Cross); without it, every frame
 *   0x20 tested at a point 620 units in front of the player; a hit sets F_atari (work 0)
 *   0x40 tested where the player stands; a hit sets U_atari (work 1)
 *   0x80 four corners rather than a box (the stored record keeps corners either way)
 * Its floor must be the player's unless it has bit 0x80. Zones placed on an object or enemy
 * (super bit 0x80) are skipped: those aren't tracked. The game walks the zones by id: an examine
 * stops at the first hit whose kind isn't 0; the every-frame check handles every hit. A hit sets
 * F_atari or U_atari to the zone's id even when its kind is 0 (that handler, Nothing, puts the two
 * back as they were). */
#include <stdint.h>

#include "pe_imp_logic.h"
#include "pe_re_rules.h"
#include "pe_re_scd.h"

enum {                                  /* zone kinds (SCENARIO.H SCE_*) */
    PE_RE_ZONE_NOTHING = 0, PE_RE_ZONE_DOOR = 1, PE_RE_ZONE_ITEM = 2, PE_RE_ZONE_NORMAL = 3,
    PE_RE_ZONE_MESSAGE = 4, PE_RE_ZONE_EVENT = 5, PE_RE_ZONE_FLAG = 6, PE_RE_ZONE_WATER = 7,
    PE_RE_ZONE_MOVE = 8, PE_RE_ZONE_SAVE = 9, PE_RE_ZONE_ITEMBOX = 10, PE_RE_ZONE_DAMAGE = 11,
    PE_RE_ZONE_STATUS = 12, PE_RE_ZONE_DRAWER = 13, PE_RE_ZONE_WINDOWS = 14
};
#define PE_RE_SAT_PLAYER 0x01u
#define PE_RE_SAT_MANUAL 0x10u
#define PE_RE_SAT_FRONT 0x20u
#define PE_RE_SAT_UNDER 0x40u
#define PE_RE_FRONT_REACH 620.0         /* 0x26C */

typedef enum { PE_RE_FROM_TRIGGER, PE_RE_FROM_DOOR, PE_RE_FROM_PICKUP } PeReZoneFrom;

typedef struct {
    uint8_t id, sce, sat;               /* as the scripts left it */
    uint8_t data[6];                    /* a trigger's data (aot_reset's for a reset zone) */
    PeReZoneFrom from;                  /* which record */
    uint32_t index;                     /* its index in that section */
} PeReZoneHit;

/* The point the front test uses, for a player facing `facing` (0..4095, as the stage turns Aya:
 * 0 faces -z, 1024 faces -x). */
void pe_re_front(double x, double z, int facing, double *fx, double *fz);

/* Check the zones for the player at (x, z) on `floor`, facing `facing`: `manual` 1 for an examine
 * (zones with bit 0x10; stops at the first hit with a kind), 0 for the every-frame check. Sets
 * work[0] / work[1] (F_atari / U_atari) as the game does when `work` isn't NULL. Pickups taken
 * already (pe_re_pickup_gone) don't count. Fills up to `max` hits in id order; how many. */
int pe_re_zone_check(const PeImpRoomLogic *room, const PeReScdRun *run, const PeReProgress *p, double x, double z,
                     int facing, int floor, int manual, int16_t *work, PeReZoneHit *hits, int max);

/* The zone the scripts hold under `id` (aot_on sets it off): 0 and the zone, or -1. */
int pe_re_zone_by_id(const PeImpRoomLogic *room, const PeReScdRun *run, const PeReProgress *p, uint8_t id,
                     PeReZoneHit *out);

#endif
