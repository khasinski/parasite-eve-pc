/* Challenge: One-hit KO. Any hit, contact or HP-reduction attack on Aya in battle takes all her HP
 * (PE_HOOK_HP_LOSS sources 1, 2 and 4). Poison (3) keeps its own rule. Preraise and Revive still
 * work: that is the game's own rule, not a hit. */
#include "pe_challenges.h"
#include "pe_game.h"
#include "pe_mod_host.h"

/* After Glass cannon (LATE - 10): this one has the last word on the loss. */
#define CH_PRIORITY_ONE_HIT (PE_PRIORITY_LATE - 5)

static int pe_one_hit_loss(void *ctx, void *user) {
    PeHookHpLoss *q = ctx;
    (void)user;
    if (!pe_ch_on(CH_ONE_HIT) || !q || !q->loss || *q->loss <= 0 || q->current_hp <= 0) return 0;
    if (q->source != 1 && q->source != 2 && q->source != 4) return 0;
    if (!pe_in_battle() || q->actor != (const void *)pe_aya_core()) return 0;
    if (*q->loss < q->current_hp) *q->loss = q->current_hp;
    return 0;
}

static void pe_one_hit_register(const char *mod_id) {
    pe_hook_add(PE_HOOK_HP_LOSS, pe_one_hit_loss, NULL, CH_PRIORITY_ONE_HIT, mod_id);
}

const PeChallengeDef g_ch_one_hit = {
    CH_ONE_HIT, "One-hit KO", "One-hit KO",
    "Any hit in battle takes all of Aya's HP. Preraise and Revive still work.",
    NULL, 0, 0, 0, pe_one_hit_register
};
