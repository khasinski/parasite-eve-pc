#ifndef PE_BP_RECORD_H
#define PE_BP_RECORD_H
/* The BP record: ranks and run data a save holds, its three save sections and the EX Game
 * carryover. Pure: no game globals, no I/O. */
#include "bp_defs.h"

/* Persistent state, owned by the BP mod (saved as three companion sections, see bp_store.c). */
typedef struct BpRecord {
    uint8_t defense[BP_DEFENSE_TRACKS];   /* ranks 0..3 */
    uint8_t fortitude_mode;               /* BpFortitudeMode */
    uint8_t mastery[BP_MASTERY_LINES];    /* ranks 0..3 (Scan 0..1) */
    /* Run data (not carried into the EX Game). */
    uint8_t full_resets;                  /* full resets this playthrough */
    uint16_t active_steps;                /* Active Time steps bought on this screen */
    uint16_t active_points;               /* raw stat value those steps added */
    uint32_t carried_pool;                /* EX Game: refunds of carried ranks, upgrade-only */
    /* EX Game carryover: how many of each track's / line's ranks came from the last run (the
     * lowest ranks). Their refund goes to carried_pool, never to normal BP (BP-Q19). */
    uint8_t carried_defense[BP_DEFENSE_TRACKS];
    uint8_t carried_mastery[BP_MASTERY_LINES];
} BpRecord;

/* Field-by-field (padding ignored). */
int bp_record_equal(const BpRecord *a, const BpRecord *b);
/* Save encoding: "pe_bp/ranks" (carried into the EX Game) and "pe_bp/run" (not carried).
 * decode refuses out-of-range ranks (0 = ok). */
enum { BP_RANKS_BYTES = 20, BP_RUN_BYTES = 12 };
void bp_record_encode(const BpRecord *r, uint8_t ranks[BP_RANKS_BYTES], uint8_t run[BP_RUN_BYTES]);
int bp_record_decode(const uint8_t ranks[BP_RANKS_BYTES], const uint8_t run[BP_RUN_BYTES], BpRecord *out);
/* "pe_bp/carry": the carried rank counts. A save without it carries nothing (all zero).
 * decode refuses a carried count above the rank it belongs to (0 = ok). */
enum { BP_CARRY_BYTES = 18 };
void bp_carry_encode(const BpRecord *r, uint8_t carry[BP_CARRY_BYTES]);
int bp_carry_decode(const uint8_t carry[BP_CARRY_BYTES], BpRecord *inout);

/* Ranks 1..carried of a track / line came from the last run: BP value of that share. */
unsigned bp_carried_track_value(const BpRecord *r, int track);
unsigned bp_carried_line_value(const BpRecord *r, int line);
/* BP value of the ranks a record holds (all of them, or only the carried ones). */
unsigned bp_record_rank_value(const BpRecord *r, int carried_only);

/* EX Game carryover (F1 > PE Enhancements > Bonus Points). Unspent BP always carries: the game
 * itself keeps it. Off: ranks reset. Keep: ranks carry (tagged carried). Convert: ranks reset and
 * their BP value goes to the upgrade-only pool. Run data (full-reset count, Active Time ledger)
 * always restarts. Returns the record the new run starts with. */
enum { BP_CARRY_OFF, BP_CARRY_KEEP, BP_CARRY_CONVERT, BP_CARRY_MODES };
const char *bp_carry_name(int mode);
BpRecord bp_ex_carry(const BpRecord *last_run, int mode);
#endif
