/* Economy settings for the opt-in configurable path: one canonical configuration, its validation,
 * preset matching, a pure preview and an explicit Apply. Plain data: no game state, no RNG, no
 * saving and no rewards. Enhanced never reads this. Bridge findings D1 section 4, D2 sections 3-5. */
#ifndef PE_ECON_CONFIG_H
#define PE_ECON_CONFIG_H
#include <stdint.h>
#include "pe_economy_policy.h"

enum { PE_ECON_LEGACY = 0, PE_ECON_CONFIGURABLE = 1 };
enum { PE_ECON_ROUTE_COUNTER = 0, PE_ECON_ROUTE_ITEMS = 1 };
enum {
    PE_ECON_F_INCOME = 1u << 0,
    PE_ECON_F_BONUS_ITEM = 1u << 1,
    PE_ECON_F_SUPER_DROP = 1u << 2,
    PE_ECON_F_DUPER_DROP = 1u << 3,
    PE_ECON_F_SUPER_REDEEM = 1u << 4,
    PE_ECON_F_DUPER_REDEEM = 1u << 5,
    PE_ECON_F_PERFORMANCE = 1u << 6,
    PE_ECON_F_EMPTY_STEAL = 1u << 7,
    PE_ECON_F_ALL = 0xFFu,
};

typedef struct {
    uint8_t engine, route;
    uint8_t algorithm, reserved;              /* PE_ECON_ALGORITHM_1; reserved stays 0 */
    uint16_t flags;
    uint16_t price_pct;                       /* base price, captured from Difficulty: 75/100/125 */
    uint16_t income, boss_income, payment_chance;
    uint16_t item_chance, item_guarantee;     /* guarantee 0: none */
    uint16_t super_chance, duper_chance;      /* together at most 10,000 */
    uint16_t super_value, duper_value;        /* kept while redemption is off */
    uint16_t performance_pct, steal_junk;
    char key[48];                             /* zero padded; empty: Custom, else namespace.name in a-z 0-9 _ - . / */
} PeEconConfig;

typedef enum {
    PE_ECON_OK = 0,
    PE_ECON_BAD_ENGINE,
    PE_ECON_BAD_ROUTE,
    PE_ECON_BAD_FLAGS,
    PE_ECON_BAD_PRICE,
    PE_ECON_BAD_CHANCE,
    PE_ECON_BUNDLE_CHANCES,
    PE_ECON_ROUTE_NEEDS_COUNTER,   /* added income or redemption with native Junk kept as items */
    PE_ECON_REDEEM_ZERO,
    PE_ECON_STEAL_ZERO,
    PE_ECON_PERFORMANCE_NO_INCOME,
    PE_ECON_NEVER_PAYS,            /* switched on, with amounts or chances that can't award anything */
    PE_ECON_BAD_ALGORITHM,
    PE_ECON_BAD_KEY,
    PE_ECON_BAD_PRESET,
    PE_ECON_BAD_STATE,
    PE_ECON_NOT_FREE_FIELD,
    PE_ECON_STORE_REFUSED,
} PeEconResult;

/* One sentence for the player or the log, for any result. */
const char *pe_econ_reason(PeEconResult result);
PeEconResult pe_econ_validate(const PeEconConfig *config);

/* The built-in presets, in menu order. Enhanced is index 0 and selects Legacy. */
int pe_econ_builtin_count(void);
const char *pe_econ_builtin_name(int index);
/* `price_pct` is the base price to keep: presets never change it. */
PeEconResult pe_econ_builtin(int index, uint16_t price_pct, PeEconConfig *out);
PeEconResult pe_econ_from_preset(const char *key, const PeEconomyPreset *preset, uint16_t price_pct, PeEconConfig *out);

/* Same behaviour? Compares what is in effect: a value behind a switched-off control doesn't count,
 * nor do the key or the price. */
int pe_econ_same_settings(const PeEconConfig *a, const PeEconConfig *b);
/* The built-in preset these settings match, or -1 for Custom. */
int pe_econ_match_builtin(const PeEconConfig *config);

typedef enum {
    PE_ECON_NO_CHANGE,      /* identical: Apply does nothing */
    PE_ECON_ENTER_NEW,      /* Enhanced to the new path */
    PE_ECON_WITHIN_NEW,     /* progress kept, nothing paid */
    PE_ECON_RETURN_ENHANCED,
} PeEconTransition;

/* What Apply can't see for itself. */
typedef struct {
    uint32_t held_super, held_duper;   /* copies in the bag and storage */
    uint16_t enhanced_price_pct;       /* the price the retained Difficulty implies */
    uint16_t legacy_carried;           /* Enhanced's carried rare, or 0 */
} PeEconContext;

typedef struct {
    PeEconResult result;
    PeEconTransition transition;
    uint32_t redeem_super, redeem_duper;  /* held bundles later sweeps will take; shown before Apply */
    uint32_t redeem_junk;
    uint16_t price_from, price_to;        /* differ: disclose a price change */
    uint8_t freezes_legacy_carried;
    uint8_t first_opt_in;
} PeEconPreview;

/* One reward waiting to be delivered: the final amount and identity, never a callback. */
enum { PE_ECON_PENDING_ITEM = 1, PE_ECON_PENDING_EFFECT, PE_ECON_PENDING_PERMIT, PE_ECON_PENDING_BUNDLE, PE_ECON_PENDING_CURRENCY };
enum { PE_ECON_SLOT_ITEM, PE_ECON_SLOT_BUNDLE, PE_ECON_SLOT_CURRENCY, PE_ECON_SLOTS };
typedef struct {
    uint8_t kind, origin;      /* kind 0: empty, every other field 0 */
    uint16_t flags, content, reserved;
    uint32_t amount, sequence;
} PeEconPending;

/* The new path's progress. It waits untouched while Enhanced is active. */
typedef struct {
    uint32_t rng_payment, rng_item, rng_bundle;
    uint16_t dry;
    uint8_t effect_progress, permit_progress;
    uint32_t remainder;        /* 0..99,999,999 */
    uint32_t next_sequence;    /* from 1; exhaustion stops new awards, it never wraps */
    PeEconPending pending[PE_ECON_SLOTS];
} PeEconProgress;

/* The applied settings and progress: the pe_economy/policy companion section. */
typedef struct {
    PeEconConfig config;       /* kept when back on Enhanced, whose engine is Legacy */
    PeEconProgress progress;
    uint8_t initialized;       /* new-path progress has been set up, once */
} PeEconState;

/* D3 section 3's fixed little-endian layout. All zero means no policy: a state that never left
 * Enhanced encodes to that, and it decodes to the reset state. */
enum { PE_ECON_RECORD_SIZE = 160 };
void pe_econ_encode(const PeEconState *state, uint8_t out[PE_ECON_RECORD_SIZE]);
/* Refused: `state` stays as it was. */
PeEconResult pe_econ_decode(const uint8_t in[PE_ECON_RECORD_SIZE], PeEconState *state);

void pe_econ_state_reset(PeEconState *state);
/* For loading: also checks the settings kept while on Enhanced, which pe_econ_validate doesn't. */
PeEconResult pe_econ_state_validate(const PeEconState *state);
/* Pure: changes nothing and calls nothing. */
void pe_econ_preview(const PeEconState *state, const PeEconConfig *target, const PeEconContext *context, PeEconPreview *out);
/* `enable_store` makes the settings saveable and runs only when entering the new path, before any
 * change. Refused, or outside free field: `state` stays as it was. */
PeEconResult pe_econ_apply(PeEconState *state, const PeEconConfig *target, int free_field, int (*enable_store)(void));
#endif
