/* The public "bp" service (mods/pe_bp/bp_api.c) against a fake game: the table, quotes that
 * change nothing, commits and their "bp.changed" event, pauses, upgrade-only grants, and old
 * callers' smaller out-structs. */
#include "pe_bp_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while (0)

/* ---- fake game ---- */
static BpSnapshot game;
static unsigned f1_flags = BP_FEATURE_ALL;
static int store_ok = 1;
static int f_read(BpSnapshot *out, void *u) { (void)u; *out = game; out->features = bp_api_features(); return game.loaded; }
static int f_store(void *u) { (void)u; return store_ok; }
static int f_spend(unsigned n, void *u) { (void)u; if (n > game.junk) return 0; game.junk -= n; return 1; }
static void f_bp(unsigned bp, void *u) { (void)u; game.bp = bp; }
static void f_gear(int r, int s, int v, void *u) { (void)u; (void)r; (void)s; (void)v; }
static void f_stat(int s, unsigned v, void *u) { (void)u; if (s == 5) game.active.value = v; else game.capacity.value = v; }
static void f_rec(const BpRecord *r, void *u) { (void)u; game.rec = *r; }
static const BpGameOps ops = {f_read, NULL, f_store, f_spend, f_bp, f_gear, f_stat, f_rec, NULL, NULL};
static const BpRecord *h_record(void) { return &game.rec; }
static int h_readable(void) { return game.loaded; }
static unsigned h_flags(void) { return f1_flags; }
static int h_carry(void) { return BP_CARRY_KEEP; }
static unsigned h_bp(void) { return game.bp; }

static int events;
static PeBpChanged last_changed;
static char last_why[48], last_by[32];
static PeBpEffect last_effect;
static void h_emit(const char *event, const void *ctx) {
    ++events;
    if (!strcmp(event, "bp.changed")) {
        last_changed = *(const PeBpChanged *)ctx;
        snprintf(last_why, sizeof last_why, "%s", last_changed.why ? last_changed.why : "");
        snprintf(last_by, sizeof last_by, "%s", last_changed.by ? last_changed.by : "");
    } else if (!strcmp(event, "bp.effect")) last_effect = *(const PeBpEffect *)ctx;
    else abort();
}
static const BpApiHost host = {&ops, h_record, h_readable, h_flags, h_carry, h_bp, h_emit, NULL};

static const PeBpApi *api;
static void fresh(void) {
    memset(&game, 0, sizeof game);
    game.loaded = 1; game.bp = 1000; game.economy = 1; game.junk = 300; game.level = 10;
    game.learned = 1u;   /* Heal */
    f1_flags = BP_FEATURE_ALL; store_ok = 1; events = 0;
    bp_api_reset_for_tests();
}
#define SAME(before) CHECK(!memcmp(&(before), &game, sizeof game))

static void table(void) {
    CHECK(api->struct_size == sizeof(PeBpApi));
    CHECK(!strcmp(api->key(PE_BP_DEFENSE, 0), "poison") && !strcmp(api->key(PE_BP_DEFENSE, 4), "fortitude"));
    CHECK(!strcmp(api->key(PE_BP_MASTERY, 0), "heal") && !strcmp(api->key(PE_BP_MASTERY, 11), "full_recover"));
    CHECK(!strcmp(api->name(PE_BP_MASTERY, 4), "Energy Shot"));
    CHECK(api->key(PE_BP_DEFENSE, 5) == NULL && api->key(2, 0) == NULL && api->name(PE_BP_MASTERY, -1) == NULL);
    CHECK(api->max_rank(PE_BP_DEFENSE, 0) == 3 && api->max_rank(PE_BP_MASTERY, 5) == 1 && api->max_rank(PE_BP_MASTERY, 12) == 0);
    CHECK(api->price(PE_BP_DEFENSE, 0, 1) == 75 && api->price(PE_BP_DEFENSE, 4, 3) == 200);
    CHECK(api->price(PE_BP_MASTERY, 0, 2) == 250 && api->price(PE_BP_MASTERY, 5, 2) == 0 && api->price(PE_BP_DEFENSE, 9, 1) == 0);
    CHECK(api->rank(PE_BP_DEFENSE, 9) == -1 && api->active_rank(PE_BP_MASTERY, 12) == -1 && api->carried(5, 0) == -1);
    CHECK(!strcmp(api->error_text(PE_BP_ERR_BP), "Not enough BP"));
}

static void status_and_sizes(void) {
    PeBpStatus s;
    struct { PeBpStatus head; uint32_t tail; } old;
    fresh();
    game.rec.carried_pool = 40; game.rec.full_resets = 2;
    memset(&s, 0, sizeof s); s.struct_size = sizeof s;
    CHECK(api->status(&s) == 1 && s.loaded && s.bp == 1000 && s.upgrade_pool == 40 && s.full_resets == 2);
    CHECK(s.features == BP_FEATURE_ALL && s.paused == 0 && s.carry_mode == PE_BP_CARRY_KEEP);
    /* A caller built against a shorter struct gets only that much written. */
    memset(&old, 0xAB, sizeof old);
    old.head.struct_size = (uint32_t)offsetof(PeBpStatus, bp);
    CHECK(api->status(&old.head) == 1 && old.head.struct_size == offsetof(PeBpStatus, bp));
    CHECK(old.head.features == BP_FEATURE_ALL && old.head.bp == 0xABABABABu && old.tail == 0xABABABABu);
    game.loaded = 0;
    memset(&s, 0, sizeof s); s.struct_size = sizeof s;
    CHECK(api->status(&s) == 0 && !s.loaded && s.bp == 0);
    CHECK(api->rank(PE_BP_DEFENSE, 0) == 0);   /* nothing loaded: no ranks */
}

static void quotes_and_commits(void) {
    PeBpQuote q;
    BpSnapshot before;
    fresh();
    before = game;
    memset(&q, 0, sizeof q); q.struct_size = sizeof q;
    CHECK(api->quote("buy.def.0", &q) == PE_BP_OK && q.bp_cost == 75 && q.bp_after == 925 && q.rank_after == 1);
    SAME(before); CHECK(events == 0);   /* a quote changes nothing and says nothing */
    CHECK(api->quote("buy.line.1", &q) == PE_BP_ERR_LOCKED && q.error == PE_BP_ERR_LOCKED);   /* Detox not learned */
    CHECK(api->quote("nonsense", &q) == PE_BP_ERR_UNKNOWN);
    CHECK(api->execute("buy.def.0", "test_mod", &q) == PE_BP_OK && game.bp == 925 && api->rank(PE_BP_DEFENSE, 0) == 1);
    CHECK(events == 1 && !strcmp(last_why, "buy.def.0") && !strcmp(last_by, "test_mod"));
    CHECK(last_changed.bp_before == 1000 && last_changed.bp_after == 925);
    /* A refusal changes nothing and announces nothing. */
    game.bp = 10; before = game;
    CHECK(api->execute("buy.def.0", "test_mod", NULL) == PE_BP_ERR_BP);
    SAME(before); CHECK(events == 1);
    /* The screens' path (no mod id) announces too. */
    game.bp = 1000;
    CHECK(bp_api_execute("buy.line.0", NULL).error == BP_OK && events == 2 && !strcmp(last_by, "") && !strcmp(last_why, "buy.line.0"));
    /* Not loaded. */
    game.loaded = 0;
    CHECK(api->quote("buy.def.0", &q) == PE_BP_ERR_NOT_LOADED && api->execute("buy.def.0", NULL, &q) == PE_BP_ERR_NOT_LOADED);
}

static void pauses(void) {
    PeBpStatus s;
    PeBpQuote q;
    BpSnapshot before;
    char long_id[40];
    fresh();
    game.rec.defense[0] = 2; game.rec.defense[BP_FORTITUDE] = 1; game.rec.mastery[0] = 1;
    CHECK(api->active_rank(PE_BP_DEFENSE, 0) == 2);
    CHECK(api->pause("pe_challenges", PE_BP_FEATURE_RESISTANCE) == 0);
    CHECK(api->rank(PE_BP_DEFENSE, 0) == 2 && api->active_rank(PE_BP_DEFENSE, 0) == 0);   /* ranks stay, effect off */
    CHECK(api->active_rank(PE_BP_DEFENSE, BP_FORTITUDE) == 1 && api->active_rank(PE_BP_MASTERY, 0) == 1);
    CHECK(!(bp_api_features() & BP_FEATURE_RESISTANCE) && (bp_api_features() & BP_FEATURE_FORTITUDE));
    memset(&s, 0, sizeof s); s.struct_size = sizeof s;
    api->status(&s);
    CHECK(s.paused == PE_BP_FEATURE_RESISTANCE && s.features == (BP_FEATURE_ALL & ~BP_FEATURE_RESISTANCE));
    before = game;
    memset(&q, 0, sizeof q); q.struct_size = sizeof q;
    CHECK(api->execute("buy.def.0", NULL, &q) == PE_BP_ERR_INACTIVE); SAME(before);
    CHECK(api->quote("buy.def.4", &q) == PE_BP_OK);   /* Fortitude is its own feature */
    /* A second mod's pause; the first lifts its own: each mod's pause is its own. */
    CHECK(api->pause("cutscene_mod", PE_BP_FEATURE_MASTERY) == 0);
    CHECK(api->pause("pe_challenges", 0) == 0);
    CHECK(api->active_rank(PE_BP_DEFENSE, 0) == 2 && api->active_rank(PE_BP_MASTERY, 0) == 0);
    CHECK(api->pause("cutscene_mod", 0) == 0 && bp_api_features() == BP_FEATURE_ALL);
    /* The F1 rules still apply under no pause. */
    f1_flags = BP_FEATURE_ALL & ~BP_FEATURE_MASTERY;
    CHECK(api->active_rank(PE_BP_MASTERY, 0) == 0);
    f1_flags = BP_FEATURE_ALL;
    /* Refusals: no id, an id too long, a full table. Lifting an unknown pause is fine. */
    memset(long_id, 'x', sizeof long_id - 1); long_id[sizeof long_id - 1] = 0;
    CHECK(api->pause(NULL, 1) == -1 && api->pause("", 1) == -1 && api->pause(long_id, 1) == -1);
    for (int i = 0; i < 8; ++i) { char id[8]; snprintf(id, sizeof id, "m%d", i); CHECK(api->pause(id, PE_BP_FEATURE_RESPEC) == 0); }
    CHECK(api->pause("m8", PE_BP_FEATURE_RESPEC) == -1);
    CHECK(api->pause("m0", PE_BP_FEATURE_MASTERY) == 0);   /* an existing pause can still change */
    CHECK(api->pause("never_paused", 0) == 0);
}

static void grants(void) {
    PeBpQuote q;
    BpSnapshot before;
    fresh();
    CHECK(api->grant_pool(200, "pe_rewards") == PE_BP_OK && game.rec.carried_pool == 200 && game.bp == 1000);
    CHECK(events == 1 && !strcmp(last_why, "grant") && !strcmp(last_by, "pe_rewards"));
    CHECK(last_changed.pool_before == 0 && last_changed.pool_after == 200);
    /* Upgrade-only: Poison 1 (75) comes from the pool; a gun step (100 BP) never does. */
    memset(&q, 0, sizeof q); q.struct_size = sizeof q;
    CHECK(api->execute("buy.def.0", "test", &q) == PE_BP_OK && q.pool_cost == 75 && q.bp_cost == 0 && game.bp == 1000);
    CHECK(game.rec.carried_pool == 125);
    before = game;
    CHECK(api->grant_pool(BP_MAX, NULL) == PE_BP_ERR_OVERFLOW); SAME(before);
    CHECK(api->grant_pool(0, NULL) == PE_BP_ERR_UNCHANGED); SAME(before);
    store_ok = 0;
    CHECK(api->grant_pool(5, NULL) == PE_BP_ERR_STORE); SAME(before);
    store_ok = 1; game.loaded = 0; before = game;
    CHECK(api->grant_pool(5, NULL) == PE_BP_ERR_NOT_LOADED); SAME(before);
}

static void carried_and_effects(void) {
    fresh();
    game.rec.defense[1] = 3; game.rec.carried_defense[1] = 2;
    game.rec.mastery[0] = 1; game.rec.carried_mastery[0] = 3;   /* never more than the rank */
    CHECK(api->carried(PE_BP_DEFENSE, 1) == 2 && api->carried(PE_BP_MASTERY, 0) == 1 && api->carried(PE_BP_DEFENSE, 0) == 0);
    bp_api_effect(PE_BP_DEFENSE, 2, 3, "resisted");
    CHECK(events == 1 && last_effect.kind == PE_BP_DEFENSE && last_effect.index == 2 && last_effect.rank == 3 &&
          !strcmp(last_effect.what, "resisted"));
    bp_api_notify("load", 1000, 0);
    CHECK(events == 2 && !strcmp(last_why, "load") && !strcmp(last_by, ""));
}

int main(void) {
    bp_api_bind(&host);
    api = bp_api_public();
    table(); status_and_sizes(); quotes_and_commits(); pauses(); grants(); carried_and_effects();
    puts("PASS: public bp service (table, quotes, commits, events, pauses, grants, struct sizes)");
    return 0;
}
