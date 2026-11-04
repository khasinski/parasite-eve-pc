#ifndef PE_ECONOMY_POLICY_H
#define PE_ECONOMY_POLICY_H
#include "pe_content_api.h"
/* Opt-in configurable payouts only. Legacy/Enhanced callers keep their existing pipeline and
 * must not invoke contributors. This API neither draws RNG nor spends/credits the wallet.
 * Contributions are computed into locals; no mutation or notification occurs during preview.
 */
typedef struct {
    PeApiHeader api;
    uint32_t generation;
    uint64_t operation;
    uint32_t nominal_10000; /* owner's nominal payment in ten-thousandths of one Junk */
    uint32_t remainder;     /* previous remainder, 0..99,999,999 */
    uint8_t configurable, boss;
} PePayoutContext;
typedef struct {
    uint32_t add_junk, factor_10000;
} PePayoutContribution;
typedef struct {
    uint32_t junk, remainder;
} PePayoutResult;
typedef PeRegResult (*PePayoutModifier)(const PePayoutContext *, PePayoutContribution *);
typedef void (*PePayoutObserver)(const PePayoutContext *, const PePayoutResult *);
typedef struct {
    PeApiHeader api;
    /* Complete self-contained preset, not a partial overlay. No callback and no auto-apply. */
    uint16_t battle_junk, boss_junk, super_value, duper_value;
    uint16_t bonus_chance_10000, bonus_guarantee;
    uint16_t payment_chance_10000, super_chance_10000, duper_chance_10000;
    uint16_t performance_pct, empty_steal_junk;
    uint8_t vanilla_drops, rare_drops, extra_junk, super_drops, duper_drops;
    uint8_t redeem_super, redeem_duper, performance_bonus, empty_steal, native_to_counter;
    uint8_t algorithm; /* PE_ECON_ALGORITHM_1 */
} PeEconomyPreset;
enum { PE_ECON_ALGORITHM_1 = 1 };
enum { PE_POLICY_MODIFIER = 1, PE_POLICY_OBSERVER = 2, PE_POLICY_PRESET = 3 };
typedef struct {
    PeApiHeader api;
    char key[48], name[64];
    uint8_t kind;
    PePayoutModifier modifier;
    PePayoutObserver observer;
    PeEconomyPreset preset;
} PeEconomyPolicyDef;
PeRegResult pe_economy_policy_register(const char *batch, const PeEconomyPolicyDef *def);
const PeEconomyPolicyDef *pe_economy_policy_find(const char *key);
const PeEconomyPolicyDef *pe_economy_policy_at(int kind, int index);
int pe_economy_policy_count(int kind);
PeRegResult pe_economy_policy_preview(const PePayoutContext *ctx, PePayoutResult *out);
/* Core owner calls after a committed award. Monotonic operation ids prevent re-emission without an unbounded
 * history; old/out-of-order operation ids are refused, never re-emitted. Reset on load/new game, not on a
 * provider callback. Observers cannot initiate another typed operation from the callback.
 */
PeRegResult pe_economy_policy_notify(const PePayoutContext *ctx, const PePayoutResult *result);
void pe_economy_policy_reset(void);
typedef struct {
    uint32_t struct_size;
    PeRegResult (*add)(const char *, const PeEconomyPolicyDef *);
    const PeEconomyPolicyDef *(*find)(const char *);
    int (*count)(int);
    const PeEconomyPolicyDef *(*at)(int, int);
    PeRegResult (*preview)(const PePayoutContext *, PePayoutResult *);
} PeEconomyPolicyApi;
extern const PeEconomyPolicyApi g_pe_economy_policy_api;
#endif
