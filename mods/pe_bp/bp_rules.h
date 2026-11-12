#ifndef PE_BP_RULES_H
#define PE_BP_RULES_H
/* BP overhaul rules: prices, quotes and the transaction plan. Pure: no game globals, no I/O.
 * The design doc (PE_BP_Overhaul_Revised_2026-11-12.md) owns every number here. A quote is
 * computed from a snapshot; the executor re-reads a fresh snapshot and re-quotes before it
 * writes anything, so a stale screen can never commit a stale price. */
#include "bp_config.h"
#include <stddef.h>
#include <stdint.h>

enum {
    BP_STEP_PRICE = 100,          /* vanilla: every gear / Aya stat step */
    BP_MAX = 99999,               /* vanilla clamp of g_AyaBonusPoints */
    BP_GEAR_BONUS_MAX = 999,      /* vanilla: a bonus stat stops below 999 */
    BP_RESIST_TRACKS = 4,         /* Poison, Confusion, Darkness, Stiffness */
    BP_FORTITUDE = BP_RESIST_TRACKS, /* the fifth defensive track */
    BP_DEFENSE_TRACKS = 5,
    BP_MASTERY_LINES = 12,
    BP_MAX_RANK = 3,
    BP_MAX_GEAR = 50,
    BP_FULL_RESET_BASE = 200, BP_FULL_RESET_STEP = 100, BP_FULL_RESET_CAP = 500,
    BP_TRACK_RESET_BASE = 50, BP_RESET_FEE_PER_100 = 5
};

typedef enum { BP_FORT_BOTH, BP_FORT_SAVE_FOR_HP1 } BpFortitudeMode;

/* Persistent state, owned by the BP mod (saved as two companion sections, see bp_store.h). */
typedef struct BpRecord {
    uint8_t defense[BP_DEFENSE_TRACKS];   /* ranks 0..3 */
    uint8_t fortitude_mode;               /* BpFortitudeMode */
    uint8_t mastery[BP_MASTERY_LINES];    /* ranks 0..3 (Scan 0..1) */
    /* Run data (not carried into the EX Game). */
    uint8_t full_resets;                  /* full resets this playthrough */
    uint16_t active_steps;                /* Active Time steps bought on this screen */
    uint16_t active_points;               /* raw stat value those steps added */
    uint32_t carried_pool;                /* EX Game: refunds of carried ranks, upgrade-only */
} BpRecord;

typedef struct BpGear {
    int record;             /* equipment record index (bag value - 0x100) */
    int armor;              /* 0 gun, 1 armor */
    int equipped;
    unsigned item_id;
    const char *name;
    int base[3], bonus[3];
} BpGear;

/* One Aya stat (5 Active Time, 6 Item Capacity) with display values the adapter computed with
 * the game's own tables. */
typedef struct BpAyaStat {
    unsigned value, next_value;   /* raw table value now and after one 100 BP step */
    int level, sub;               /* game level (0-based) and sub-level 0..48 */
    int next_level, next_sub;
    int distance, next_distance;  /* raw points to the next level, now and after the step */
    int shown, next_shown;        /* what the player reads: Active Time level or bag slots */
    int maxed;                    /* no further step exists */
} BpAyaStat;

typedef struct BpSnapshot {
    unsigned features;            /* applied profile feature mask; ranks stay save-owned */
    int loaded;                   /* a game is running and the screen may act */
    unsigned bp;
    int economy;                  /* Junk economy on (Wayne's reset available) */
    unsigned junk;
    int level;                    /* Aya's level, 0-based */
    unsigned learned;             /* parasite_spell_flags */
    int armor_resist[BP_RESIST_TRACKS]; /* equipped armor carries the matching resist mod */
    int pe_light;                 /* equipped armor attribute 0x200 (PE cost x 2/3) */
    int max_pe;                   /* Aya's max PE (Barrier's cost basis), 0 if unknown */
    BpAyaStat active, capacity;
    unsigned active_base;         /* start + 10 x level: Active Time with no BP */
    unsigned capacity_base;
    int capacity_reset_ok;        /* the stat-6 BP share replays exactly (10-point steps) */
    unsigned capacity_steps;      /* exact stat-6 steps bought (any screen) */
    int capacity_after_reset;     /* bag slots after a full reset */
    int bag_items_beyond;         /* occupied bag slots a full reset would remove */
    BpGear gear[BP_MAX_GEAR];
    int gear_count;
    BpRecord rec;
} BpSnapshot;

/* ---- definitions ---- */
typedef struct BpTrackDef {
    const char *id, *name;
    uint16_t price[BP_MAX_RANK];
} BpTrackDef;
typedef struct BpLineDef {
    const char *id, *name;
    char tier;                    /* 'A', 'B', 'C' or 'S' (single rank) */
    int ranks;
    uint32_t powers;              /* parasite_spell_flags bits that unlock the line */
    int vanilla_drain;            /* fixed PE drain of the (first) power, 0 if not fixed */
    const char *rank_text[BP_MAX_RANK];  /* full totals at each rank */
    const char *for_whom, *pays_off;
} BpLineDef;

const BpTrackDef *bp_defense_def(int track);   /* 0..4 */
const BpLineDef *bp_line_def(int line);        /* 0..11 */
const char *bp_gear_stat_name(int armor, int stat);
int bp_armor_chance(int track, int slow);      /* vanilla armor-mod chance, percent */
unsigned bp_track_price(int track, int rank);  /* price of rank `rank` (1-based) */
unsigned bp_line_price(int line, int rank);
unsigned bp_track_spent(int track, int rank);  /* total BP for ranks 1..rank */
unsigned bp_line_spent(int line, int rank);
int bp_line_learned(const BpSnapshot *s, int line);
/* Real one-time PE cost at a Mastery rank (vanilla drain, PE Light x2/3), or -1 when the
 * power has no fixed drain. Rebalance's own costs are not in this build. */
int bp_line_cost(int line, int rank, int pe_light, int max_pe);
/* Effective resist chance, percent: 1 - (1 - armor)(1 - 10 x rank). */
int bp_effective_resist(int armor_percent, int rank);

/* ---- quotes ---- */
typedef enum {
    BP_OK, BP_ERR_NOT_LOADED, BP_ERR_UNKNOWN, BP_ERR_MAXED, BP_ERR_LOCKED, BP_ERR_BP,
    BP_ERR_JUNK, BP_ERR_OVERFLOW, BP_ERR_EMPTY, BP_ERR_NO_ECONOMY, BP_ERR_BAG,
    BP_ERR_UNCHANGED, BP_ERR_STORE, BP_ERR_INACTIVE
} BpError;
const char *bp_error_text(BpError e);

typedef enum {
    BP_REQ_NONE, BP_REQ_GEAR, BP_REQ_AYA, BP_REQ_DEFENSE, BP_REQ_MASTERY,
    BP_REQ_RESET_DEFENSE, BP_REQ_RESET_MASTERY, BP_REQ_RESET_ALL, BP_REQ_FORT_MODE
} BpRequestKind;
typedef struct BpRequest { BpRequestKind kind; int a, b; } BpRequest;

/* Request IDs are the UI's confirmation targets: "buy.gear.<record>.<stat>", "buy.aya.<5|6>",
 * "buy.def.<track>", "buy.line.<line>", "reset.def.<track>", "reset.line.<line>", "reset.all",
 * "set.fort.<0|1>". */
int bp_request_parse(const char *id, BpRequest *out);
void bp_request_format(const BpRequest *r, char *out, size_t size);

typedef struct BpQuote {
    BpError error;
    unsigned bp_cost;          /* BP taken (purchases) */
    unsigned pool_cost;        /* carried-pool BP taken (EX Game) */
    unsigned bp_refund;        /* BP returned (resets) */
    unsigned junk_fee_base;    /* fee before the Economy preset */
    unsigned junk_fee;         /* fee after the preset (filled by the caller's pricing) */
    unsigned bp_after, junk_after;
    /* Full reset breakdown. */
    unsigned refund_aya, refund_defense, refund_mastery;
    unsigned active_steps, capacity_steps;
    unsigned next_full_fee_base;
    int rank_before, rank_after;
} BpQuote;

typedef unsigned (*BpPriceFn)(unsigned base);   /* Economy preset pricing; NULL = as is */
BpQuote bp_quote(const BpSnapshot *s, const BpRequest *r, BpPriceFn price);
unsigned bp_reset_fee_base(int full, unsigned refund, unsigned full_resets_done);

/* Field-by-field (padding ignored). */
int bp_record_equal(const BpRecord *a, const BpRecord *b);
/* Save encoding: "pe_bp/ranks" (carried into the EX Game later) and "pe_bp/run" (not carried).
 * decode refuses out-of-range ranks (0 = ok). */
enum { BP_RANKS_BYTES = 20, BP_RUN_BYTES = 12 };
void bp_record_encode(const BpRecord *r, uint8_t ranks[BP_RANKS_BYTES], uint8_t run[BP_RUN_BYTES]);
int bp_record_decode(const uint8_t ranks[BP_RANKS_BYTES], const uint8_t run[BP_RUN_BYTES], BpRecord *out);

/* The writes a successful quote implies, applied to a copy of the snapshot (the executor
 * performs exactly these on the game). Returns BP_OK or why not. */
BpError bp_apply(BpSnapshot *s, const BpRequest *r, const BpQuote *q);

/* ---- battle effects (pure numbers; pe_bp_battle.c applies them) ---- */
enum { BP_FX_HALVE_HP = 8, BP_FX_HP_TO_1 = 9 };
/* Fortitude for one Halve HP / HP to 1 application on Aya. use: spend a charge; negate: cancel
 * the whole effect (buff strip too); else soften to result_hp. A charge is used only when the
 * protected HP beats the unprotected one. */
typedef struct BpFortitudePlan { int use, negate, result_hp, vanilla_hp; } BpFortitudePlan;
BpFortitudePlan bp_fortitude_plan(int effect, int hp, int barrier, int rank, int mode, int charges_used);
int bp_fortitude_charges(int rank);
int bp_line_for_power(int power);                 /* Mastery line of a PE power index, or -1 */
int bp_line_cut(int line, int rank);              /* cast-cost cut, percent */
int bp_heal_bonus(int rank);                      /* Heal line: extra healing, percent */
int bp_haste_bonus(int rank);                     /* Haste line: extra duration, percent */
int bp_neutral_chance(int rank);                  /* Slow/Confuse vs neutral enemies, percent */
/* Chance (percent) of a second roll after the vanilla 50% roll failed, so the total equals
 * bp_neutral_chance(rank). */
int bp_neutral_reroll(int rank);
int bp_energy_shot_bonus(int rank, int pe_above_half);   /* percent */
#endif
