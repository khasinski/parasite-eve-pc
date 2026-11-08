/* The opt-in path's battle reward decisions: pure, with no game state. Enhanced never runs this.
 *
 * Draw order, fixed by algorithm 1. Three separate xorshift32 streams, so one family never shifts
 * another's numbers; a disabled, full or certain roll draws nothing.
 *   Currency (rng_payment): one draw, only when the payment chance is below 100%.
 *   Bonus item (rng_item): the outer chance unless guaranteed; then down the cascade, one draw per
 *     step that isn't skipped or certain; then the economy mod's weight, or the booster's type.
 *   Bundle (rng_bundle): one draw, Super then Duper over the same 0..9,999 range.
 * A family that starts the victory occupied gets delivery only: no roll, no progress. */
#ifndef PE_ECON_ENGINE_H
#define PE_ECON_ENGINE_H
#include "pe_econ_config.h"

typedef PeRegResult (*PeEconPreviewFn)(const PePayoutContext *context, PePayoutResult *out);

enum { PE_ECON_MOD_COUNT = 4 };
enum { PE_ECON_ITEM_SUPER_JUNK = 0x0B, PE_ECON_ITEM_DUPER_JUNK = 0x0C, PE_ECON_ITEM_MOD_PERMIT = 0x37 };

typedef struct {
    uint32_t exp;                 /* picks the booster tier */
    uint32_t wallet_room;         /* what the wallet can still take this battle */
    uint32_t generation;          /* the registry's, for the payout API */
    uint32_t hits;                /* on Aya, this battle */
    uint32_t held_super_tool, held_permits, held_tools;   /* copies, not slots */
    uint8_t boss, has_list;       /* no list: nothing can be shown, so no item or bundle rolls */
    uint8_t scavenger, lucky_find;
    uint8_t tool_kit;             /* either kit held: Tools are free */
    uint8_t econ_missing;         /* bit per economy mod still locked; 0 with no Mod Catalog */
    uint8_t econ_guarantee, permit_guarantee;
    PeEconPreviewFn preview;
} PeEconBattleIn;

typedef struct {
    uint32_t junk;                /* pay this now */
    uint8_t notify;               /* a new payout: tell observers `result`, after committing */
    uint8_t paused;               /* the wallet can't take a whole payment: it waits */
    uint8_t preview_failed;       /* nothing changed in the currency family */
    PePayoutContext context;
    PePayoutResult result;
} PeEconBattleOut;

/* Updates `progress` in place. The caller commits it, then shows the pending entries. */
void pe_econ_battle(const PeEconConfig *config, PeEconProgress *progress, const PeEconBattleIn *in, PeEconBattleOut *out);

/* Three nonzero stream states from a one-time seed, without touching any other RNG. */
void pe_econ_seed(PeEconProgress *progress, uint32_t seed);
#endif
