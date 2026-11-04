/* The opt-in path's reward decisions: fractions, backpressure, guarantees, streams, sequences. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "pe_econ_engine.h"

static int s_preview_refuse, s_previews;
static PeRegResult preview(const PePayoutContext *c, PePayoutResult *out) {
    uint64_t v;
    ++s_previews;
    if (s_preview_refuse) return PE_REG_CONFLICT;
    v = (uint64_t)c->nominal_10000 * (c->final_factor_10000 ? c->final_factor_10000 : 10000u) + c->remainder;
    out->junk = (uint32_t)(v / 100000000u);
    out->remainder = (uint32_t)(v % 100000000u);
    return PE_REG_OK;
}

static PeEconProgress fresh(void) {
    PeEconProgress g;
    memset(&g, 0, sizeof g);
    g.next_sequence = 1;
    pe_econ_seed(&g, 42);
    return g;
}
static PeEconBattleIn input(void) {
    PeEconBattleIn in;
    memset(&in, 0, sizeof in);
    in.wallet_room = 99999;
    in.generation = 1;
    in.hits = 9;
    in.has_list = 1;
    in.econ_guarantee = 8;
    in.permit_guarantee = 8;
    in.preview = preview;
    return in;
}
static PeEconConfig config(int builtin) {
    PeEconConfig c;
    assert(pe_econ_builtin(builtin, 100, &c) == PE_ECON_OK);
    return c;
}
enum { VANILLA = 1, LIGHT, STEADY, TREASURE };

static void test_seed(void) {
    PeEconProgress a = fresh(), b;
    memset(&b, 0, sizeof b);
    pe_econ_seed(&b, 0);
    assert(a.rng_payment && a.rng_item && a.rng_bundle && b.rng_payment && b.rng_item && b.rng_bundle);
    assert(a.rng_payment != a.rng_item && a.rng_item != a.rng_bundle);
}

static void test_income(void) {
    PeEconConfig c = config(STEADY);
    PeEconProgress g = fresh(), before;
    PeEconBattleIn in = input();
    PeEconBattleOut out;

    /* 100%: no draw, and the boss payment replaces the ordinary one. */
    before = g;
    pe_econ_battle(&c, &g, &in, &out);
    assert(out.junk == 3 && out.notify && out.result.junk == 3 && out.context.operation == 1);
    assert(g.rng_payment == before.rng_payment && g.next_sequence == 2);
    in.boss = 1;
    pe_econ_battle(&c, &g, &in, &out);
    assert(out.junk == 10 && out.context.boss == 1 && out.context.operation == 2);

    /* A4: base 1 with Scavenger pays 1, 2, 1, 2; the fraction carries. */
    c = config(LIGHT);
    g = fresh();
    in = input();
    in.scavenger = 1;
    {
        static const uint32_t expect[4] = {1, 2, 1, 2};
        for (int i = 0; i < 4; ++i) {
            pe_econ_battle(&c, &g, &in, &out);
            assert(out.junk == expect[i] && out.context.final_factor_10000 == 15000);
        }
        assert(g.remainder == 0);
    }

    /* Nothing whole yet: progress, but no sequence and no notice. */
    c = config(STEADY);
    c.income = 1;
    g = fresh();
    g.remainder = 0;
    in = input();
    c.flags |= PE_ECON_F_PERFORMANCE;
    c.performance_pct = 25;
    in.hits = 0;                       /* no-hit: base * 1.25 */
    pe_econ_battle(&c, &g, &in, &out);
    assert(out.junk == 1 && g.remainder == 25000000u);

    /* A refused preview changes nothing at all. */
    c = config(STEADY);
    c.payment_chance = 5000;
    g = fresh();
    before = g;
    in = input();
    s_preview_refuse = 1;
    for (int i = 0; i < 40; ++i) {
        pe_econ_battle(&c, &g, &in, &out);
        assert(!out.junk && !out.notify);
    }
    s_preview_refuse = 0;
    assert(!memcmp(&g, &before, sizeof g) || g.rng_payment != before.rng_payment);   /* misses may draw */
    assert(g.next_sequence == 1 && !g.remainder);

    /* The chance is honest: roughly half, with no bias from the range. */
    {
        int paid = 0;
        g = fresh();
        for (int i = 0; i < 10000; ++i) {
            pe_econ_battle(&c, &g, &in, &out);
            if (out.junk) ++paid;
        }
        assert(paid > 4700 && paid < 5300);
    }
}

static void test_full_wallet(void) {
    PeEconConfig c = config(STEADY);
    PeEconProgress g = fresh();
    PeEconBattleIn in = input();
    PeEconBattleOut out;
    int previews;

    in.wallet_room = 2;                /* can't take 3: all of it waits, none is lost */
    pe_econ_battle(&c, &g, &in, &out);
    assert(!out.junk && out.paused && out.notify);
    assert(g.pending[PE_ECON_SLOT_CURRENCY].kind == PE_ECON_PENDING_CURRENCY && g.pending[PE_ECON_SLOT_CURRENCY].amount == 3);

    /* Still full: paused, no new roll, no second notice. */
    previews = s_previews;
    pe_econ_battle(&c, &g, &in, &out);
    assert(!out.junk && out.paused && !out.notify && s_previews == previews);
    assert(g.pending[PE_ECON_SLOT_CURRENCY].amount == 3);

    /* Room: the held payment is delivered, and that victory earns nothing more. */
    in.wallet_room = 99999;
    pe_econ_battle(&c, &g, &in, &out);
    assert(out.junk == 3 && !out.paused && !out.notify && s_previews == previews);
    assert(!g.pending[PE_ECON_SLOT_CURRENCY].kind);
    pe_econ_battle(&c, &g, &in, &out);
    assert(out.junk == 3 && out.notify);
}

static void test_bonus_item(void) {
    PeEconConfig c = config(TREASURE);
    PeEconProgress g = fresh();
    PeEconBattleIn in = input();
    PeEconBattleOut out;
    int won = 0;

    /* D1: guaranteed by the tenth victory without one, whatever the rolls do. */
    c.item_chance = 0;
    for (int i = 1; i <= 10; ++i) {
        pe_econ_battle(&c, &g, &in, &out);
        if (g.pending[PE_ECON_SLOT_ITEM].kind) { won = i; break; }
        assert(g.dry == i);
    }
    assert(won == 10 && g.dry == 0 && g.pending[PE_ECON_SLOT_ITEM].sequence == 1 && g.next_sequence == 2);

    /* Occupied: delivery only. No draw, no dry progress. */
    {
        const PeEconProgress before = g;
        pe_econ_battle(&c, &g, &in, &out);
        assert(!memcmp(&g, &before, sizeof g));
    }

    /* Lucky Find doubles dry progress, and nothing else. */
    g = fresh();
    in.lucky_find = 1;
    pe_econ_battle(&c, &g, &in, &out);
    assert(g.dry == 2 && !out.junk);
    in.lucky_find = 0;

    /* No list: nothing could be shown, so no roll and no progress. */
    g = fresh();
    in.has_list = 0;
    {
        const PeEconProgress before = g;
        pe_econ_battle(&c, &g, &in, &out);
        assert(!memcmp(&g, &before, sizeof g));
    }
    in.has_list = 1;

    /* The cascade: a sure outer roll, across many victories. */
    {
        int effect = 0, permit = 0, super_tool = 0, tool = 0, booster = 0;
        c.item_chance = 10000;
        in.econ_missing = 0xF;
        in.exp = 200;
        g = fresh();
        for (int i = 0; i < 4000; ++i) {
            const PeEconPending *e = &g.pending[PE_ECON_SLOT_ITEM];
            pe_econ_battle(&c, &g, &in, &out);
            assert(e->kind);
            /* Bundles are never in this pool. */
            assert(e->content != PE_ECON_ITEM_SUPER_JUNK && e->content != PE_ECON_ITEM_DUPER_JUNK);
            if (e->kind == PE_ECON_PENDING_EFFECT) { assert(e->content < PE_ECON_MOD_COUNT); ++effect; }
            else if (e->kind == PE_ECON_PENDING_PERMIT) { assert(e->content == PE_ECON_ITEM_MOD_PERMIT); ++permit; }
            else if (e->content == 0x36) ++super_tool;
            else if (e->content == 0x16) ++tool;
            else { assert(e->content >= 0x1D + 3 && e->content <= 0x31 + 3); ++booster; }   /* top EXP tier */
            memset(&g.pending[PE_ECON_SLOT_ITEM], 0, sizeof g.pending[PE_ECON_SLOT_ITEM]);
        }
        assert(effect && permit && super_tool && tool && booster);
    }

    /* Caps: what's held takes a step out of the pool. All skipped: always a booster. */
    {
        c.item_chance = 10000;
        in = input();
        in.held_super_tool = 1;
        in.held_permits = 2;
        in.tool_kit = 1;
        g = fresh();
        for (int i = 0; i < 200; ++i) {
            const PeEconPending *e = &g.pending[PE_ECON_SLOT_ITEM];
            pe_econ_battle(&c, &g, &in, &out);
            assert(e->kind == PE_ECON_PENDING_ITEM && e->content >= 0x1D && e->content <= 0x31);
            memset(&g.pending[PE_ECON_SLOT_ITEM], 0, sizeof g.pending[PE_ECON_SLOT_ITEM]);
        }
    }

    /* The economy mod and permit guarantees use the new path's own counters. */
    {
        in = input();
        in.econ_missing = 0x4;
        in.econ_guarantee = 1;
        g = fresh();
        pe_econ_battle(&c, &g, &in, &out);
        assert(g.pending[PE_ECON_SLOT_ITEM].kind == PE_ECON_PENDING_EFFECT && g.pending[PE_ECON_SLOT_ITEM].content == 2);
    }
}

static void test_bundles_and_streams(void) {
    PeEconConfig c = config(VANILLA);
    PeEconProgress g = fresh();
    PeEconBattleIn in = input();
    PeEconBattleOut out;
    int super = 0, duper = 0;

    /* Everything off: not one draw from any stream. */
    {
        const PeEconProgress before = g;
        for (int i = 0; i < 20; ++i) pe_econ_battle(&c, &g, &in, &out);
        assert(!memcmp(&g, &before, sizeof g) && !out.junk);
    }
    c.flags = PE_ECON_F_SUPER_DROP | PE_ECON_F_DUPER_DROP;
    c.super_chance = 2000;
    c.duper_chance = 1000;
    for (int i = 0; i < 10000; ++i) {
        const PeEconPending *e = &g.pending[PE_ECON_SLOT_BUNDLE];
        pe_econ_battle(&c, &g, &in, &out);
        if (e->kind) {
            assert(e->kind == PE_ECON_PENDING_BUNDLE);
            if (e->content == PE_ECON_ITEM_SUPER_JUNK) ++super; else { assert(e->content == PE_ECON_ITEM_DUPER_JUNK); ++duper; }
            memset(&g.pending[PE_ECON_SLOT_BUNDLE], 0, sizeof g.pending[PE_ECON_SLOT_BUNDLE]);
        }
    }
    assert(super > 1800 && super < 2200 && duper > 850 && duper < 1150);

    /* Separate streams: bundles on or off, the bonus item sequence doesn't move. */
    {
        PeEconProgress a = fresh(), b = fresh();
        PeEconConfig only_item = config(TREASURE), both = only_item;
        both.flags |= PE_ECON_F_SUPER_DROP;
        both.super_chance = 5000;
        for (int i = 0; i < 200; ++i) {
            pe_econ_battle(&only_item, &a, &in, &out);
            pe_econ_battle(&both, &b, &in, &out);
            assert(a.rng_item == b.rng_item && a.dry == b.dry);
            memset(&a.pending[PE_ECON_SLOT_ITEM], 0, sizeof a.pending[PE_ECON_SLOT_ITEM]);
            memset(&b.pending[PE_ECON_SLOT_ITEM], 0, sizeof b.pending[PE_ECON_SLOT_ITEM]);
            memset(&b.pending[PE_ECON_SLOT_BUNDLE], 0, sizeof b.pending[PE_ECON_SLOT_BUNDLE]);
        }
    }

    /* Each family has its own slot: a blocked one doesn't stop another. */
    {
        PeEconConfig all = config(TREASURE);
        all.flags |= PE_ECON_F_INCOME | PE_ECON_F_SUPER_DROP;
        all.income = 3;
        all.super_chance = 10000;
        all.item_chance = 10000;
        g = fresh();
        g.next_sequence = 5;
        g.pending[PE_ECON_SLOT_ITEM] = (PeEconPending){PE_ECON_PENDING_ITEM, 0, 0, 0x1D, 0, 1, 4};
        pe_econ_battle(&all, &g, &in, &out);
        assert(out.junk == 3 && g.pending[PE_ECON_SLOT_BUNDLE].kind == PE_ECON_PENDING_BUNDLE);
        assert(g.pending[PE_ECON_SLOT_ITEM].sequence == 4);
    }
}

static void test_exhausted_sequences(void) {
    PeEconConfig c = config(TREASURE);
    PeEconProgress g = fresh(), before;
    PeEconBattleIn in = input();
    PeEconBattleOut out;
    c.flags |= PE_ECON_F_INCOME | PE_ECON_F_SUPER_DROP;
    c.income = 3;
    c.super_chance = 10000;
    c.item_chance = 10000;
    g.next_sequence = 0xFFFFFFFFu;
    before = g;
    pe_econ_battle(&c, &g, &in, &out);
    assert(!memcmp(&g, &before, sizeof g) && !out.junk && !out.notify);   /* stops; never wraps */
}

/* Whatever comes out must pass the loader's checks. */
static void test_results_save(void) {
    PeEconState s, back;
    PeEconBattleIn in = input();
    PeEconBattleOut out;
    uint8_t bytes[PE_ECON_RECORD_SIZE];
    memset(&s, 0, sizeof s);
    s.initialized = 1;
    s.config = config(TREASURE);
    s.config.flags |= PE_ECON_F_INCOME | PE_ECON_F_SUPER_DROP | PE_ECON_F_DUPER_DROP;
    s.config.income = 1;
    s.config.super_chance = 3000;
    s.config.duper_chance = 3000;
    s.config.payment_chance = 7000;
    s.progress = fresh();
    in.scavenger = 1;
    in.econ_missing = 0xF;
    for (int i = 0; i < 3000; ++i) {
        in.wallet_room = i % 7 == 0 ? 0 : 99999;
        pe_econ_battle(&s.config, &s.progress, &in, &out);
        pe_econ_encode(&s, bytes);
        assert(pe_econ_decode(bytes, &back) == PE_ECON_OK);
        assert(!memcmp(&back.progress, &s.progress, sizeof back.progress));
        if (i % 5 == 0) {
            memset(&s.progress.pending[PE_ECON_SLOT_ITEM], 0, sizeof s.progress.pending[0]);
            memset(&s.progress.pending[PE_ECON_SLOT_BUNDLE], 0, sizeof s.progress.pending[0]);
        }
    }
}

int main(void) {
    test_seed();
    test_income();
    test_full_wallet();
    test_bonus_item();
    test_bundles_and_streams();
    test_exhausted_sequences();
    test_results_save();
    puts("Economy engine: income, fractions, full wallet, guarantees, cascade, bundles, streams and sequences passed");
    return 0;
}
