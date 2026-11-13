#ifndef PE_BP_TXN_H
#define PE_BP_TXN_H
/* Transaction executor: re-reads a fresh snapshot, re-quotes, then commits in one step.
 * Order: everything that can refuse runs first (snapshot, quote, store enable, Junk spend);
 * the remaining writes cannot fail. A refusal at any point leaves BP, Junk, ranks, stats and
 * gear exactly as they were. The game adapter (bp_live.c) and the tests supply BpGameOps. */
#include "bp_rules.h"

typedef struct BpGameOps {
    int (*read)(BpSnapshot *out, void *user);          /* 0 = can't read now */
    BpPriceFn price;                                   /* Economy preset; may be NULL */
    int (*store_ready)(void *user);                    /* enable the save sections; 0 = refused */
    int (*junk_spend)(unsigned n, void *user);         /* 0 = short, nothing spent */
    /* Infallible writes after the checks. */
    void (*set_bp)(unsigned bp, void *user);
    void (*set_gear_bonus)(int record, int stat, int value, void *user);
    void (*set_aya_stat)(int stat, unsigned value, void *user);   /* entries 5-6 only */
    void (*set_record)(const BpRecord *rec, void *user);
    void (*after)(int stats_changed, int gear_record, void *user);  /* refresh derived game state */
    void *user;
} BpGameOps;

typedef struct BpResult {
    BpError error;
    BpQuote quote;
    BpRequest request;
} BpResult;

BpResult bp_execute(const BpGameOps *ops, const char *request_id);
/* The same read and quote as bp_execute, without committing anything. */
BpResult bp_preview(const BpGameOps *ops, const char *request_id);
/* Human-readable outcome for the UI notice. */
void bp_result_text(const BpResult *r, const BpSnapshot *after, char *out, size_t size);
#endif
