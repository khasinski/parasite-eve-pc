#ifndef PE_BP_INTERNAL_H
#define PE_BP_INTERNAL_H
/* Inside pe_bp only: how its parts reach each other. Other mods use the public "bp" service
 * (host/pe_services.h); the BP screens use "bp_screens" (pe_bp_screens.h).
 *
 *   pe_bp.c         mod entry: registers everything, publishes both services
 *   bp_store.c      save sections, the current record, load / New Game / EX Game carryover
 *   bp_game.c       the game side: snapshot and the writes a transaction commits (BpGameOps)
 *   bp_api.c        the "bp" service, pauses, events; every commit goes through bp_api_execute.
 *                   Pure over BpApiHost, so the unit tests run it without the game.
 *   pe_bp_battle.c  battle effects
 *   pe_bp_menu.c    F1 page and profile settings
 *   bp_defs/record/quote/txn.c   pure rules (bp_rules.h, bp_txn.h) */
#include "bp_txn.h"
#include "pe_services.h"

/* ---- bp_store.c ---- */
void bp_store_init(const char *mod_id);
const BpRecord *bp_store_record(void);
int bp_store_readable(void);                 /* a game is loaded and its BP sections were readable */
int bp_store_ready(void *user);              /* enable the sections for writing; 0 = refused */
void bp_store_set(const BpRecord *rec, void *user);

/* ---- bp_game.c ---- */
const BpGameOps *bp_game_ops(void);
int bp_game_snapshot(BpSnapshot *out);       /* 0: no game loaded, or in battle */
unsigned bp_game_price(unsigned base);       /* Junk fee after the Economy preset */
unsigned bp_game_bp(void);                   /* unspent BP */

/* ---- bp_api.c ---- */
typedef struct BpApiHost {
    const BpGameOps *ops;                    /* snapshot, pricing and the writes */
    const BpRecord *(*record)(void);         /* current ranks */
    int (*readable)(void);                   /* see bp_store_readable */
    unsigned (*flags)(void);                 /* F1's applied BP_FEATURE_* */
    int (*carry_mode)(void);                 /* BP_CARRY_* */
    unsigned (*bp)(void);                    /* unspent BP */
    void (*emit)(const char *event, const void *ctx);   /* NULL: no events (tests) */
    void (*log)(const char *line);           /* NULL: quiet */
} BpApiHost;
void bp_api_bind(const BpApiHost *host);
const PeBpApi *bp_api_public(void);
/* F1's applied flags minus every mod's pause: what quotes and battle effects use. */
unsigned bp_api_features(void);
/* Every purchase, refund and setting goes through here (the BP screens included): commits,
 * logs and announces "bp.changed". by: the asking mod's id, NULL for the player. */
BpResult bp_api_execute(const char *request, const char *by);
/* "bp.changed" for a change made outside a request (load, New Game, EX carry). */
void bp_api_notify(const char *why, unsigned bp_before, unsigned pool_before);
/* "bp.effect" from the battle effects. kind: PE_BP_DEFENSE / PE_BP_MASTERY. */
void bp_api_effect(int kind, int index, int rank, const char *what);
#ifdef PE_BP_UNIT_TESTS
void bp_api_reset_for_tests(void);           /* forget pauses */
#endif
#endif
