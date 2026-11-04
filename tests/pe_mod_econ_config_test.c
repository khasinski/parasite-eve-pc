/* Economy settings: validation, presets and their numbers, matching, preview and Apply. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "pe_econ_config.h"

static int s_store_calls, s_store_answer = 1;
static int store(void) { ++s_store_calls; return s_store_answer; }

static PeEconConfig preset(int i) {
    PeEconConfig c;
    assert(pe_econ_builtin(i, 100, &c) == PE_ECON_OK);
    return c;
}
enum { ENHANCED, VANILLA, LIGHT, STEADY, TREASURE };

static void test_presets(void) {
    PeEconConfig c;
    assert(pe_econ_builtin_count() == 5);
    assert(!strcmp(pe_econ_builtin_name(LIGHT), "Light economy") && !pe_econ_builtin_name(5));
    assert(preset(ENHANCED).engine == PE_ECON_LEGACY);
    for (int i = VANILLA; i <= TREASURE; ++i) {
        c = preset(i);
        assert(c.engine == PE_ECON_CONFIGURABLE && c.route == PE_ECON_ROUTE_COUNTER);
        assert(c.super_value == 10 && c.duper_value == 20);
        assert(!(c.flags & (PE_ECON_F_SUPER_DROP | PE_ECON_F_DUPER_DROP | PE_ECON_F_PERFORMANCE | PE_ECON_F_EMPTY_STEAL)));
        assert(pe_econ_match_builtin(&c) == i);
    }
    c = preset(VANILLA);
    assert(c.flags == 0);
    c = preset(LIGHT);
    assert(c.income == 1 && c.boss_income == 3 && (c.flags & PE_ECON_F_SUPER_REDEEM) && (c.flags & PE_ECON_F_DUPER_REDEEM));
    c = preset(STEADY);
    assert(c.income == 3 && c.boss_income == 10 && c.flags == PE_ECON_F_INCOME);
    c = preset(TREASURE);
    assert(c.item_chance == 1000 && c.item_guarantee == 10 && c.flags == PE_ECON_F_BONUS_ITEM);
    assert(pe_econ_builtin(LIGHT, 90, &c) == PE_ECON_BAD_PRICE);
    assert(pe_econ_builtin(9, 100, &c) == PE_ECON_BAD_PRESET);
}

static void test_validation(void) {
    PeEconConfig c = preset(STEADY);
    /* Items routing refuses every kind of added income and redemption, and changes nothing itself. */
    c.route = PE_ECON_ROUTE_ITEMS;
    assert(pe_econ_validate(&c) == PE_ECON_ROUTE_NEEDS_COUNTER && c.flags == PE_ECON_F_INCOME);
    c = preset(LIGHT);
    c.flags = PE_ECON_F_SUPER_REDEEM;
    c.route = PE_ECON_ROUTE_ITEMS;
    assert(pe_econ_validate(&c) == PE_ECON_ROUTE_NEEDS_COUNTER);
    c.flags = PE_ECON_F_EMPTY_STEAL; c.steal_junk = 5;
    assert(pe_econ_validate(&c) == PE_ECON_ROUTE_NEEDS_COUNTER);
    assert(strstr(pe_econ_reason(PE_ECON_ROUTE_NEEDS_COUNTER), "empty-steal"));
    c.flags = PE_ECON_F_BONUS_ITEM; c.item_chance = 1000;   /* non-currency rewards may stay */
    assert(pe_econ_validate(&c) == PE_ECON_OK);

    /* Switched on but can never award anything. */
    c = preset(STEADY); c.payment_chance = 0; assert(pe_econ_validate(&c) == PE_ECON_NEVER_PAYS);
    c = preset(STEADY); c.income = c.boss_income = 0; assert(pe_econ_validate(&c) == PE_ECON_NEVER_PAYS);
    c = preset(STEADY); c.income = 0; assert(pe_econ_validate(&c) == PE_ECON_OK);   /* bosses still pay */
    c = preset(TREASURE); c.item_guarantee = 0; assert(pe_econ_validate(&c) == PE_ECON_OK);
    c.item_chance = 0; assert(pe_econ_validate(&c) == PE_ECON_NEVER_PAYS);
    c = preset(VANILLA); c.flags = PE_ECON_F_SUPER_DROP; assert(pe_econ_validate(&c) == PE_ECON_NEVER_PAYS);
    c = preset(VANILLA); c.flags = PE_ECON_F_DUPER_DROP; assert(pe_econ_validate(&c) == PE_ECON_NEVER_PAYS);
    c = preset(STEADY); c.flags |= PE_ECON_F_PERFORMANCE; assert(pe_econ_validate(&c) == PE_ECON_NEVER_PAYS);

    /* Algorithm and key shape. */
    c = preset(VANILLA); c.algorithm = 2; assert(pe_econ_validate(&c) == PE_ECON_BAD_ALGORITHM);
    c = preset(VANILLA); c.reserved = 1; assert(pe_econ_validate(&c) == PE_ECON_BAD_ALGORITHM);
    {
        static const char *const bad_keys[] = {"steady", ".steady", "pe.", "PE.steady", "pe.st eady", "pe.\xff"};
        for (size_t i = 0; i < sizeof bad_keys / sizeof bad_keys[0]; ++i) {
            c = preset(VANILLA);
            memset(c.key, 0, sizeof c.key);
            strcpy(c.key, bad_keys[i]);
            assert(pe_econ_validate(&c) == PE_ECON_BAD_KEY);
        }
    }
    c = preset(VANILLA); memset(c.key, 0, sizeof c.key); assert(pe_econ_validate(&c) == PE_ECON_OK);   /* Custom */

    c = preset(VANILLA); c.route = 2; assert(pe_econ_validate(&c) == PE_ECON_BAD_ROUTE);
    c = preset(VANILLA); c.engine = 2; assert(pe_econ_validate(&c) == PE_ECON_BAD_ENGINE);
    c = preset(VANILLA); c.flags = 0x100; assert(pe_econ_validate(&c) == PE_ECON_BAD_FLAGS);
    c = preset(VANILLA); c.price_pct = 80; assert(pe_econ_validate(&c) == PE_ECON_BAD_PRICE);
    c = preset(VANILLA); c.payment_chance = 10001; assert(pe_econ_validate(&c) == PE_ECON_BAD_CHANCE);
    c = preset(VANILLA); c.item_chance = 10001; assert(pe_econ_validate(&c) == PE_ECON_BAD_CHANCE);
    c = preset(VANILLA); c.super_chance = 6000; c.duper_chance = 4001; assert(pe_econ_validate(&c) == PE_ECON_BUNDLE_CHANCES);
    c.duper_chance = 4000; assert(pe_econ_validate(&c) == PE_ECON_OK);
    c = preset(LIGHT); c.super_value = 0; assert(pe_econ_validate(&c) == PE_ECON_REDEEM_ZERO);
    c = preset(VANILLA); c.super_value = 0; assert(pe_econ_validate(&c) == PE_ECON_OK);   /* off: no redemption */
    c = preset(VANILLA); c.flags = PE_ECON_F_EMPTY_STEAL; assert(pe_econ_validate(&c) == PE_ECON_STEAL_ZERO);
    c = preset(VANILLA); c.flags = PE_ECON_F_PERFORMANCE; assert(pe_econ_validate(&c) == PE_ECON_PERFORMANCE_NO_INCOME);
    c = preset(VANILLA); memset(c.key, 'x', sizeof c.key); assert(pe_econ_validate(&c) == PE_ECON_BAD_KEY);
    c = preset(VANILLA); c.key[40] = 'x'; assert(pe_econ_validate(&c) == PE_ECON_BAD_KEY);   /* not zero padded */
    assert(pe_econ_validate(NULL) != PE_ECON_OK);
    for (int r = PE_ECON_OK; r <= PE_ECON_STORE_REFUSED; ++r)
        assert(strcmp(pe_econ_reason((PeEconResult)r), "Unknown result."));
}

static void test_matching(void) {
    PeEconConfig c = preset(STEADY), d;
    c.price_pct = 125;                         /* price and key don't make it Custom */
    memset(c.key, 0, sizeof c.key);
    assert(pe_econ_match_builtin(&c) == STEADY);
    c.super_value = 33;                        /* a value behind a switched-off control doesn't either */
    assert(pe_econ_match_builtin(&c) == STEADY);
    c.income = 4;
    assert(pe_econ_match_builtin(&c) == -1);
    c = preset(STEADY);
    c.flags |= PE_ECON_F_SUPER_REDEEM;
    assert(pe_econ_match_builtin(&c) == -1);
    c = preset(LIGHT);
    c.super_value = 11;                        /* switched on, it counts */
    assert(pe_econ_match_builtin(&c) == -1);
    d = preset(VANILLA);
    assert(!pe_econ_same_settings(&d, &(PeEconConfig){.engine = PE_ECON_LEGACY}));
}

static void test_provider_preset(void) {
    PeEconomyPreset p;
    PeEconConfig c;
    memset(&p, 0, sizeof p);
    p.algorithm = PE_ECON_ALGORITHM_1;
    p.native_to_counter = 1; p.extra_junk = 1; p.battle_junk = 3; p.boss_junk = 10;
    p.payment_chance_10000 = 10000; p.super_value = 10; p.duper_value = 20;
    assert(pe_econ_from_preset("example.steady", &p, 75, &c) == PE_ECON_OK);
    assert(pe_econ_match_builtin(&c) == STEADY && c.price_pct == 75 && !strcmp(c.key, "example.steady"));
    p.native_to_counter = 0;
    assert(pe_econ_from_preset("example.steady", &p, 75, &c) == PE_ECON_ROUTE_NEEDS_COUNTER);
    p.native_to_counter = 1;
    assert(pe_econ_from_preset("pe.steady", &p, 75, &c) == PE_ECON_BAD_KEY);   /* reserved for built-ins */
    p.algorithm = 0;
    assert(pe_econ_from_preset("example.steady", &p, 75, &c) == PE_ECON_BAD_ALGORITHM);
    p.algorithm = PE_ECON_ALGORITHM_1;
    p.payment_chance_10000 = 0;                /* left unset: refused, not silently unpaid */
    assert(pe_econ_from_preset("example.steady", &p, 75, &c) == PE_ECON_NEVER_PAYS);
    p.payment_chance_10000 = 10000;
    assert(pe_econ_from_preset(NULL, &p, 75, &c) == PE_ECON_BAD_PRESET);
}

static void test_state_validate(void) {
    PeEconState s;
    pe_econ_state_reset(&s);
    assert(pe_econ_state_validate(&s) == PE_ECON_OK);
    s.config.income = 3;                       /* never left Enhanced, yet has settings */
    assert(pe_econ_state_validate(&s) == PE_ECON_BAD_STATE);
    pe_econ_state_reset(&s);
    s.config.engine = PE_ECON_CONFIGURABLE;
    assert(pe_econ_state_validate(&s) == PE_ECON_BAD_STATE);
    pe_econ_state_reset(&s);
    s.initialized = 2;
    assert(pe_econ_state_validate(&s) == PE_ECON_BAD_STATE);

    /* Settings kept while on Enhanced are checked too. */
    s.initialized = 1;
    s.config = preset(STEADY);
    s.progress.next_sequence = 1;
    assert(pe_econ_state_validate(&s) == PE_ECON_OK);
    s.config.engine = PE_ECON_LEGACY;
    assert(pe_econ_state_validate(&s) == PE_ECON_OK);
    s.config.price_pct = 80;
    assert(pe_econ_validate(&s.config) == PE_ECON_OK && pe_econ_state_validate(&s) == PE_ECON_BAD_PRICE);
    assert(pe_econ_state_validate(NULL) == PE_ECON_BAD_STATE);
}

static PeEconState full_state(void) {
    PeEconState s;
    memset(&s, 0, sizeof s);
    s.initialized = 1;
    s.config = preset(LIGHT);
    s.config.flags |= PE_ECON_F_BONUS_ITEM | PE_ECON_F_EMPTY_STEAL | PE_ECON_F_PERFORMANCE;
    s.config.price_pct = 125;
    s.config.item_chance = 1234; s.config.item_guarantee = 7;
    s.config.super_chance = 300; s.config.duper_chance = 200;
    s.config.performance_pct = 15; s.config.steal_junk = 9;
    s.progress.rng_payment = 0x11223344u; s.progress.rng_item = 0x55667788u; s.progress.rng_bundle = 0x99AABBCCu;
    s.progress.dry = 0xBEEF; s.progress.effect_progress = 3; s.progress.permit_progress = 4;
    s.progress.remainder = 99999999u; s.progress.next_sequence = 10;
    s.progress.pending[PE_ECON_SLOT_ITEM] = (PeEconPending){PE_ECON_PENDING_PERMIT, 2, 0, 0x37, 0, 1, 7};
    s.progress.pending[PE_ECON_SLOT_BUNDLE] = (PeEconPending){PE_ECON_PENDING_BUNDLE, 1, 0, 0x40, 0, 1, 8};
    s.progress.pending[PE_ECON_SLOT_CURRENCY] = (PeEconPending){PE_ECON_PENDING_CURRENCY, 3, 0, 0, 0, 99999, 9};
    return s;
}

static void test_record(void) {
    PeEconState s = full_state(), back, fresh;
    uint8_t b[PE_ECON_RECORD_SIZE], again[PE_ECON_RECORD_SIZE], bad[PE_ECON_RECORD_SIZE];

    /* The whole state survives, at D3's offsets, little-endian. */
    pe_econ_encode(&s, b);
    memset(&back, 0, sizeof back);
    assert(pe_econ_decode(b, &back) == PE_ECON_OK);
    assert(!memcmp(&back.config, &s.config, sizeof s.config) && !memcmp(&back.progress, &s.progress, sizeof s.progress));
    assert(back.initialized == 1);
    pe_econ_encode(&back, again);
    assert(!memcmp(b, again, sizeof b));
    assert(b[0] == 1 && b[4] == 125 && b[6] == 1 && b[8] == 3 && b[10] == 0x10 && b[11] == 0x27);
    assert(b[28] == strlen("pe.light") && b[29] == 1 && !strcmp((const char *)b + 32, "pe.light"));
    assert(b[80] == 0x44 && b[83] == 0x11 && b[92] == 0xEF && b[94] == 3 && b[95] == 4 && b[100] == 10);
    assert(b[104] == PE_ECON_PENDING_PERMIT && b[120] == PE_ECON_PENDING_BUNDLE && b[136] == PE_ECON_PENDING_CURRENCY);
    assert(b[152] == 1);

    /* Never left Enhanced: all zero, and zero reads back as that. */
    pe_econ_state_reset(&fresh);
    pe_econ_encode(&fresh, again);
    for (int i = 0; i < PE_ECON_RECORD_SIZE; ++i) assert(!again[i]);
    s = full_state();
    assert(pe_econ_decode(again, &s) == PE_ECON_OK && !s.initialized && s.config.engine == PE_ECON_LEGACY);

    /* Enhanced with kept settings stays decodable, and they come back. */
    s = full_state();
    s.config.engine = PE_ECON_LEGACY;
    pe_econ_encode(&s, again);
    assert(pe_econ_decode(again, &back) == PE_ECON_OK && back.config.engine == PE_ECON_LEGACY && back.config.income == 1);

    /* Refused, each one leaving the state as it was. */
    {
        static const struct { int at; uint8_t value; } breaks[] = {
            {0, 2},      /* engine */
            {1, 2},      /* route */
            {3, 1},      /* unknown flag */
            {4, 80},     /* price */
            {11, 0xFF},  /* payment chance */
            {28, 3},     /* key length */
            {29, 2},     /* algorithm */
            {30, 1}, {31, 1},   /* reserved */
            {32, 'A'},   /* key characters */
            {99, 0xFF},  /* remainder */
            {100, 0}, {100, 7},   /* next sequence: gone; a pending entry at or past it */
            {104, 99},   /* kind in the wrong slot */
            {106, 1}, {110, 1},   /* entry flags, entry reserved */
            {120, PE_ECON_PENDING_CURRENCY},
            {152, 0}, {152, 3},   /* progress flags */
            {156, 1}, {159, 1},   /* reserved */
        };
        s = full_state();
        pe_econ_encode(&s, b);
        for (size_t i = 0; i < sizeof breaks / sizeof breaks[0]; ++i) {
            memcpy(bad, b, sizeof b);
            bad[breaks[i].at] = breaks[i].value;
            memset(&back, 0x5A, sizeof back);
            assert(pe_econ_decode(bad, &back) != PE_ECON_OK);
            assert(back.initialized == 0x5A);
        }
    }
}

static void test_preview_and_apply(void) {
    PeEconState s, before;
    PeEconPreview v;
    const PeEconContext ctx = {.held_super = 2, .held_duper = 1, .enhanced_price_pct = 100, .legacy_carried = 0x37};
    PeEconConfig light = preset(LIGHT), steady = preset(STEADY), enhanced = preset(ENHANCED), bad = preset(STEADY);

    pe_econ_state_reset(&s);
    before = s;
    pe_econ_preview(&s, &light, &ctx, &v);
    assert(v.result == PE_ECON_OK && v.transition == PE_ECON_ENTER_NEW && v.first_opt_in && v.freezes_legacy_carried);
    assert(v.redeem_super == 2 && v.redeem_duper == 1 && v.redeem_junk == 2 * 10 + 20);
    assert(!memcmp(&s, &before, sizeof s) && s_store_calls == 0);          /* preview is pure */

    /* Refusals leave everything as it was. */
    assert(pe_econ_apply(&s, &light, 0, store) == PE_ECON_NOT_FREE_FIELD && s_store_calls == 0);
    s_store_answer = 0;
    assert(pe_econ_apply(&s, &light, 1, store) == PE_ECON_STORE_REFUSED && s_store_calls == 1);
    assert(!memcmp(&s, &before, sizeof s));
    s_store_answer = 1;
    bad.route = PE_ECON_ROUTE_ITEMS;
    assert(pe_econ_apply(&s, &bad, 1, store) == PE_ECON_ROUTE_NEEDS_COUNTER && s_store_calls == 1);
    pe_econ_preview(&s, &bad, &ctx, &v);
    assert(v.result == PE_ECON_ROUTE_NEEDS_COUNTER);

    /* Enhanced to Enhanced: nothing, not even the store. */
    assert(pe_econ_apply(&s, &enhanced, 0, store) == PE_ECON_OK && s_store_calls == 1 && !s.initialized);

    assert(pe_econ_apply(&s, &light, 1, store) == PE_ECON_OK && s.initialized && s.config.engine == PE_ECON_CONFIGURABLE);
    assert(s_store_calls == 2);

    /* The same again is a no-op, in or out of free field. */
    before = s;
    pe_econ_preview(&s, &light, &ctx, &v);
    assert(v.transition == PE_ECON_NO_CHANGE && !v.redeem_junk);
    assert(pe_econ_apply(&s, &light, 0, store) == PE_ECON_OK && !memcmp(&s, &before, sizeof s) && s_store_calls == 2);

    /* Another preset: same engine, no store call. Redemption off discloses nothing. */
    pe_econ_preview(&s, &steady, &ctx, &v);
    assert(v.transition == PE_ECON_WITHIN_NEW && !v.first_opt_in && !v.redeem_junk && !v.freezes_legacy_carried);
    assert(pe_econ_apply(&s, &steady, 1, store) == PE_ECON_OK && s_store_calls == 2 && s.config.income == 3);

    /* Back to Enhanced: price from the retained Difficulty, disclosed; settings kept for later. */
    s.config.price_pct = 125;
    pe_econ_preview(&s, &enhanced, &ctx, &v);
    assert(v.transition == PE_ECON_RETURN_ENHANCED && v.price_from == 125 && v.price_to == 100);
    assert(pe_econ_apply(&s, &enhanced, 0, store) == PE_ECON_NOT_FREE_FIELD && s.config.engine == PE_ECON_CONFIGURABLE);
    assert(pe_econ_apply(&s, &enhanced, 1, store) == PE_ECON_OK);
    assert(s.config.engine == PE_ECON_LEGACY && s.config.income == 3 && s.initialized);

    /* Returning again isn't a first opt-in. */
    pe_econ_preview(&s, &steady, &ctx, &v);
    assert(v.transition == PE_ECON_ENTER_NEW && !v.first_opt_in);
}

int main(void) {
    test_presets();
    test_validation();
    test_matching();
    test_provider_preset();
    test_state_validate();
    test_record();
    test_preview_and_apply();
    puts("Economy settings: presets, validation, matching, preview and Apply passed");
    return 0;
}
