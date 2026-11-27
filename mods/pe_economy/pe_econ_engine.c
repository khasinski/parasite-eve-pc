#include "pe_econ_engine.h"

#include <string.h>

#define NO_MORE_SEQUENCES 0xFFFFFFFFu

static uint32_t next_draw(uint32_t *state) {
    uint32_t x = *state ? *state : 1u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

/* Unbiased: draws above the last whole multiple of n are thrown back. */
static uint32_t draw_below(uint32_t *state, uint32_t n) {
    const uint32_t limit = 0xFFFFFFFFu - 0xFFFFFFFFu % n;
    uint32_t x;
    do x = next_draw(state); while (x >= limit);
    return x % n;
}

void pe_econ_seed(PeEconProgress *g, uint32_t seed) {
    uint32_t *const streams[3] = {&g->rng_payment, &g->rng_item, &g->rng_bundle};
    for (int i = 0; i < 3; ++i) {
        uint32_t z = seed += 0x9E3779B9u;
        z = (z ^ (z >> 16)) * 0x85EBCA6Bu;
        z = (z ^ (z >> 13)) * 0xC2B2AE35u;
        z ^= z >> 16;
        *streams[i] = z ? z : 0x6D2B79F5u;
    }
}

static void set_pending(PeEconProgress *g, int slot, uint8_t kind, uint16_t content, uint32_t amount) {
    PeEconPending *e = &g->pending[slot];
    memset(e, 0, sizeof *e);
    e->kind = kind;
    e->content = content;
    e->amount = amount;
    e->sequence = g->next_sequence++;
}

static void currency(const PeEconConfig *c, PeEconProgress *g, const PeEconBattleIn *in, PeEconBattleOut *out) {
    PeEconPending *held = &g->pending[PE_ECON_SLOT_CURRENCY];
    const uint32_t base = in->boss ? c->boss_income : c->income;
    uint32_t rng = g->rng_payment;
    uint64_t nominal;
    if (held->kind) {
        if (held->amount <= in->wallet_room) {
            out->junk = held->amount;
            memset(held, 0, sizeof *held);
        } else {
            out->paused = 1;
        }
        return;
    }
    if (!(c->flags & PE_ECON_F_INCOME) || !base || g->next_sequence == NO_MORE_SEQUENCES || !in->preview) return;
    if (c->payment_chance < 10000u && draw_below(&rng, 10000u) >= c->payment_chance) {
        g->rng_payment = rng;
        return;
    }
    /* Base, the performance bonus, then contributors, then Scavenger, then the fraction (A5). */
    nominal = (uint64_t)base * 10000u;
    if ((c->flags & PE_ECON_F_PERFORMANCE) && in->hits < 4u)
        nominal += (uint64_t)base * c->performance_pct * (4u - in->hits) * 25u;
    if (nominal > 0xFFFFFFFFu) nominal = 0xFFFFFFFFu;
    memset(&out->context, 0, sizeof out->context);
    out->context.api.version = 1;
    out->context.api.size = sizeof out->context;
    out->context.generation = in->generation;
    out->context.operation = g->next_sequence;
    out->context.nominal_10000 = (uint32_t)nominal;
    out->context.remainder = g->remainder;
    out->context.final_factor_10000 = in->scavenger ? 15000u : 0u;
    out->context.configurable = 1;
    out->context.boss = in->boss;
    if (in->preview(&out->context, &out->result) != PE_REG_OK) {
        out->preview_failed = 1;   /* nothing drawn or earned: the next victory tries afresh */
        return;
    }
    g->rng_payment = rng;
    g->remainder = out->result.remainder;
    if (!out->result.junk) return;
    if (out->result.junk <= in->wallet_room) {
        out->junk = out->result.junk;
        ++g->next_sequence;
    } else {
        set_pending(g, PE_ECON_SLOT_CURRENCY, PE_ECON_PENDING_CURRENCY, 0, out->result.junk);
        out->paused = 1;
    }
    out->notify = 1;
}

/* Enhanced's cascade without bundles: their own family here. */
enum { STEP_ECON, STEP_SUPER_TOOL, STEP_PERMIT, STEP_TOOL, STEP_BOOSTER, STEPS };
static const uint8_t s_tool_chance[5] = {85, 65, 40, 20, 10};
static const uint8_t s_econ_weight[PE_ECON_MOD_COUNT] = {1, 2, 4, 3};

static int step_skipped(int step, const PeEconBattleIn *in, uint32_t *pct) {
    switch (step) {
    case STEP_ECON: *pct = 15; return !in->econ_missing;
    case STEP_SUPER_TOOL: *pct = 6; return in->held_super_tool > 0;
    case STEP_PERMIT: *pct = 15; return in->held_permits >= 2u;
    case STEP_TOOL:
        if (in->tool_kit || in->held_tools >= 5u) return 1;
        *pct = s_tool_chance[in->held_tools];
        return 0;
    default: *pct = 100; return 0;
    }
}

static uint8_t bump(uint8_t v) { return v < 250u ? (uint8_t)(v + 1u) : v; }

static void select_item(PeEconProgress *g, const PeEconBattleIn *in) {
    int hit = STEP_BOOSTER;
    for (int step = 0; step < STEPS; ++step) {
        uint32_t pct = 0, guarantee = 0, since = 0;
        if (step_skipped(step, in, &pct)) continue;
        if (step == STEP_ECON) { guarantee = in->econ_guarantee; since = g->effect_progress; }
        if (step == STEP_PERMIT) { guarantee = in->permit_guarantee; since = g->permit_progress; }
        if (step == STEP_BOOSTER || (guarantee && since + 1u >= guarantee) || draw_below(&g->rng_item, 100u) < pct) {
            hit = step;
            break;
        }
    }
    if (in->econ_missing) g->effect_progress = hit == STEP_ECON ? 0 : bump(g->effect_progress);
    if (in->held_permits < 2u) g->permit_progress = hit == STEP_PERMIT ? 0 : bump(g->permit_progress);
    switch (hit) {
    case STEP_ECON: {
        uint32_t total = 0, r;
        int pick = 0;
        for (int i = 0; i < PE_ECON_MOD_COUNT; ++i)
            if (in->econ_missing & (1u << i)) total += s_econ_weight[i];
        r = draw_below(&g->rng_item, total);
        for (int i = 0; i < PE_ECON_MOD_COUNT; ++i) {
            if (!(in->econ_missing & (1u << i))) continue;
            pick = i;
            if (r < s_econ_weight[i]) break;
            r -= s_econ_weight[i];
        }
        set_pending(g, PE_ECON_SLOT_ITEM, PE_ECON_PENDING_EFFECT, (uint16_t)pick, 1);
        break;
    }
    case STEP_SUPER_TOOL: set_pending(g, PE_ECON_SLOT_ITEM, PE_ECON_PENDING_ITEM, 0x36, 1); break;
    case STEP_PERMIT: set_pending(g, PE_ECON_SLOT_ITEM, PE_ECON_PENDING_PERMIT, PE_ECON_ITEM_MOD_PERMIT, 1); break;
    case STEP_TOOL: set_pending(g, PE_ECON_SLOT_ITEM, PE_ECON_PENDING_ITEM, 0x16, 1); break;
    default: {
        static const uint16_t base[6] = {0x1D, 0x21, 0x25, 0x29, 0x2D, 0x31};
        const uint32_t tier = in->exp < 20u ? 0u : in->exp < 60u ? 1u : in->exp < 150u ? 2u : 3u;
        set_pending(g, PE_ECON_SLOT_ITEM, PE_ECON_PENDING_ITEM, (uint16_t)(base[draw_below(&g->rng_item, 6u)] + tier), 1);
        break;
    }
    }
}

static void bonus_item(const PeEconConfig *c, PeEconProgress *g, const PeEconBattleIn *in) {
    uint32_t step;
    if (g->pending[PE_ECON_SLOT_ITEM].kind || !(c->flags & PE_ECON_F_BONUS_ITEM) || !in->has_list ||
        g->next_sequence == NO_MORE_SEQUENCES)
        return;
    if ((c->item_guarantee && g->dry + 1u >= c->item_guarantee) ||
        (c->item_chance && draw_below(&g->rng_item, 10000u) < c->item_chance)) {
        g->dry = 0;   /* only here, where the award becomes pending */
        select_item(g, in);
        return;
    }
    step = (uint32_t)g->dry + (in->lucky_find ? 2u : 1u);   /* Lucky Find: dry progress only (A6) */
    g->dry = step > 0xFFFFu ? 0xFFFFu : (uint16_t)step;
}

static void bundle(const PeEconConfig *c, PeEconProgress *g, const PeEconBattleIn *in) {
    const uint32_t super = (c->flags & PE_ECON_F_SUPER_DROP) ? c->super_chance : 0u;
    const uint32_t duper = (c->flags & PE_ECON_F_DUPER_DROP) ? c->duper_chance : 0u;
    uint32_t r;
    if (g->pending[PE_ECON_SLOT_BUNDLE].kind || !(super + duper) || !in->has_list || g->next_sequence == NO_MORE_SEQUENCES)
        return;
    r = draw_below(&g->rng_bundle, 10000u);
    if (r < super) set_pending(g, PE_ECON_SLOT_BUNDLE, PE_ECON_PENDING_BUNDLE, PE_ECON_ITEM_SUPER_JUNK, 1);
    else if (r < super + duper) set_pending(g, PE_ECON_SLOT_BUNDLE, PE_ECON_PENDING_BUNDLE, PE_ECON_ITEM_DUPER_JUNK, 1);
}

void pe_econ_battle(const PeEconConfig *c, PeEconProgress *g, const PeEconBattleIn *in, PeEconBattleOut *out) {
    memset(out, 0, sizeof *out);
    currency(c, g, in, out);
    bonus_item(c, g, in);
    bundle(c, g, in);
}
