#ifndef PE_BP_QUOTE_H
#define PE_BP_QUOTE_H
/* Snapshots, requests, quotes and the transaction plan. Pure: no game globals, no I/O. A quote
 * is computed from a snapshot; the executor (bp_txn.c) re-reads a fresh snapshot and re-quotes
 * before it writes anything, so a stale screen can never commit a stale price. */
#include "bp_record.h"

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
    unsigned features;            /* features in effect: applied profile flags minus mod pauses */
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

int bp_line_learned(const BpSnapshot *s, int line);

/* Error numbers are public: the "bp" service (host/pe_services.h, PE_BP_ERR_*) uses the same
 * values. Append only. */
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

/* Request IDs are the UI's confirmation targets and the public API's requests:
 * "buy.gear.<record>.<stat>", "buy.aya.<5|6>", "buy.def.<track>", "buy.line.<line>",
 * "reset.def.<track>", "reset.line.<line>", "reset.all", "set.fort.<0|1>". */
int bp_request_parse(const char *id, BpRequest *out);
void bp_request_format(const BpRequest *r, char *out, size_t size);

typedef struct BpQuote {
    BpError error;
    unsigned bp_cost;          /* BP taken (purchases) */
    unsigned pool_cost;        /* carried-pool BP taken (EX Game) */
    unsigned bp_refund;        /* BP returned (resets), pool share included */
    unsigned pool_refund;      /* the part of bp_refund that goes to the upgrade-only pool */
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

/* The writes a successful quote implies, applied to a copy of the snapshot (the executor
 * performs exactly these on the game). Returns BP_OK or why not. */
BpError bp_apply(BpSnapshot *s, const BpRequest *r, const BpQuote *q);
#endif
