/* The pe_economy/policy section through the real host, store and companion files. */
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#define RMDIR _rmdir
#else
#include <unistd.h>
#define RMDIR rmdir
#endif
#include "pe_econ_store.h"
#include "pe_economy_policy.h"
#include "pe_extension_store.h"
#include "pe_hooks.h"
#include "pe_mod_host.h"
#include "pe_mod_menu.h"
#include "pe_save.h"

int pe_mod_save_path(const char *path);
int pe_mod_load_admit(const char *path, const unsigned char *block);
void pe_mod_save_committed(const char *path);

#include "pe_registry.h"

static int s_early_failures;
#define CHECK_LATER(cond) \
    do { \
        if (!(cond)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #cond); ++s_early_failures; } \
    } while (0)
static int s_free_field = 1, s_toasts;
/* The payout API as a battle end really calls it: from inside a hook, with Scavenger last. */
static PeRegResult s_preview_result = PE_REG_CAPACITY;
static PePayoutResult s_preview_out;
static int on_rewards(void *ctx, void *user) {
    PePayoutContext c;
    (void)ctx; (void)user;
    memset(&c, 0, sizeof c);
    c.api.version = 1;
    c.api.size = sizeof c;
    c.generation = pe_reg_generation();
    c.operation = 1;
    c.nominal_10000 = 10000;
    c.remainder = 60000000u;
    c.final_factor_10000 = 15000;
    c.configurable = 1;
    s_preview_result = pe_economy_policy_preview(&c, &s_preview_out);
    return 0;
}
static int econ_init(void) {
    pe_econ_store_register("pe_economy");
    pe_hook_add(PE_HOOK_BATTLE_REWARDS, on_rewards, NULL, PE_PRIORITY_NORMAL, "pe_economy");
    return 0;
}
static const PeMod econ_mod = {"pe_economy", "Economy", "1", NULL, econ_init, NULL};

/* Two contributors and two observers from separate mods, neutral until a case sets them. */
static uint32_t s_add[2], s_factor[2] = {10000, 10000};
static PeRegResult s_modifier_result = PE_REG_OK;
static int s_modifier_calls[2], s_notices[2];
static uint64_t s_last_notice[2];
static PeRegResult s_nested_register = PE_REG_OK;
static uint32_t s_seen_nominal[2];
static PeRegResult contribute(int who, const PePayoutContext *c, PePayoutContribution *v) {
    ++s_modifier_calls[who];
    s_seen_nominal[who] = c->nominal_10000;
    v->add_junk = s_add[who];
    v->factor_10000 = s_factor[who];
    return who ? s_modifier_result : PE_REG_OK;
}
static PeRegResult modifier_a(const PePayoutContext *c, PePayoutContribution *v) { return contribute(0, c, v); }
static PeRegResult modifier_b(const PePayoutContext *c, PePayoutContribution *v) { return contribute(1, c, v); }
static PeEconomyPolicyDef s_late_def;
static void observer_a(const PePayoutContext *c, const PePayoutResult *v) {
    (void)v;
    ++s_notices[0];
    s_last_notice[0] = c->operation;
    s_nested_register = pe_economy_policy_register("late", &s_late_def);   /* must be refused */
}
static void observer_b(const PePayoutContext *c, const PePayoutResult *v) {
    (void)v;
    ++s_notices[1];
    s_last_notice[1] = c->operation;
}
static void provider(const char *batch, const char *key, int kind, PePayoutModifier m, PePayoutObserver o, PeEconomyPolicyDef *d) {
    memset(d, 0, sizeof *d);
    d->api.version = 1;
    d->api.size = sizeof *d;
    snprintf(d->key, sizeof d->key, "%s", key);
    snprintf(d->name, sizeof d->name, "%s", key);
    d->kind = (uint8_t)kind;
    d->modifier = m;
    d->observer = o;
    CHECK_LATER(pe_economy_policy_register(batch, d) == PE_REG_PENDING);
}
static PeEconomyPolicyDef s_defs[4];
static int provider_a_init(void) {
    CHECK_LATER(pe_reg_batch("policy", 1, NULL) == PE_REG_OK);
    provider("policy", "mod_a/bonus", PE_POLICY_MODIFIER, modifier_a, NULL, &s_defs[0]);
    provider("policy", "mod_a/watch", PE_POLICY_OBSERVER, NULL, observer_a, &s_defs[1]);
    return 0;
}
static int provider_b_init(void) {
    CHECK_LATER(pe_reg_batch("policy", 1, NULL) == PE_REG_OK);
    provider("policy", "mod_b/bonus", PE_POLICY_MODIFIER, modifier_b, NULL, &s_defs[2]);
    provider("policy", "mod_b/watch", PE_POLICY_OBSERVER, NULL, observer_b, &s_defs[3]);
    return 0;
}
static const PeMod provider_a = {"mod_a", "Provider A", "1", NULL, provider_a_init, NULL};
static const PeMod provider_b = {"mod_b", "Provider B", "1", NULL, provider_b_init, NULL};
/* Listed B first: results must not depend on start order. */
const PeMod *const g_pe_mods[] = {&econ_mod, &provider_b, &provider_a, NULL};

void pe_main_menu_init(void) {}
void pe_equip_sync_init(void) {}
void pe_present_install(void) {}
void pe_menu_begin(const char *page) { (void)page; }
void pe_menu_load_settings(void) {}
void pe_menu_alias_setting(const char *key, const char *legacy) { (void)key; (void)legacy; }
int pe_menu_register_toggle(const char *sub, const char *label, const char *description, int *flag, PeMenuPerFrame per_frame) {
    (void)sub; (void)label; (void)description; (void)per_frame;
    *flag = 1;
    return 0;
}
PeMenuRow *pe_menu_row(int index) { (void)index; return NULL; }
int pe_menu_row_count(void) { return 0; }
int pe_menu_top_page(const char *page, const char *description, int order) {
    (void)page; (void)description; (void)order;
    return 0;
}
void pe_panel_install(void) {}
void pe_toast(const char *text) { (void)text; ++s_toasts; }
int pe_free_field(void) { return s_free_field; }
void pe_pages_describe(void) {}

static int failures;
#define CHECK(cond) \
    do { \
        if (!(cond)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #cond); failures++; } \
    } while (0)

#define CARD "PEECONSTORETEST"
#define DIR_NAME ".pe-ext"

static void clean(void) {
    char file[128];
    for (unsigned g = 1; g < 64; ++g) {
        snprintf(file, sizeof file, DIR_NAME "/" CARD ".%08x.pex", g);
        remove(file);
    }
    RMDIR(DIR_NAME);
}

static void save(unsigned char *block) {
    memset(block, 0, 0x2000);
    CHECK(pe_mod_save_path(CARD) == 0);
    CHECK(pe_hook_dispatch(PE_HOOK_SAVE_WRITE, block) >= 0);
    pe_mod_save_committed(CARD);
}
static void load(unsigned char *block) {
    CHECK(pe_mod_load_admit(CARD, block) == 0);
    pe_hook_dispatch(PE_HOOK_SAVE_READ, block);
}
static int companion_count(void) {
    int n = 0;
    char file[128];
    for (unsigned g = 1; g < 64; ++g) {
        FILE *f;
        snprintf(file, sizeof file, DIR_NAME "/" CARD ".%08x.pex", g);
        if ((f = fopen(file, "rb"))) { fclose(f); ++n; }
    }
    return n;
}

static void provider_test_late(void) {
    memset(&s_late_def, 0, sizeof s_late_def);
    s_late_def.api.version = 1;
    s_late_def.api.size = sizeof s_late_def;
    snprintf(s_late_def.key, sizeof s_late_def.key, "mod_a/late");
    snprintf(s_late_def.name, sizeof s_late_def.name, "late");
    s_late_def.kind = PE_POLICY_OBSERVER;
    s_late_def.observer = observer_b;
}

int main(void) {
    static unsigned char plain[0x2000], light_save[0x2000], back_save[0x2000], bad_save[0x2000];
    PeHookInit init = {0, NULL};
    PeEconConfig light, steady, enhanced;
    PeEconState before;
    uint8_t held[PE_ECON_RECORD_SIZE];
    const PeEconState *s;

    clean();
    CHECK(pe_host_init(&init) == 0);
    s = pe_econ_store_state();
    CHECK(pe_econ_builtin(2, 75, &light) == PE_ECON_OK);
    CHECK(pe_econ_builtin(3, 75, &steady) == PE_ECON_OK);
    CHECK(pe_econ_builtin(0, 75, &enhanced) == PE_ECON_OK);
    pe_hook_dispatch(PE_HOOK_NEW_GAME, &(PeHookNewGame){PE_NEW_GAME_TITLE});

    /* 1 Junk * 1.5 + 0.6 carried = 2.1. */
    {
        PeHookRewards reward = {.exp = 0, .list = NULL};
        pe_hook_dispatch(PE_HOOK_BATTLE_REWARDS, &reward);
        CHECK(s_preview_result == PE_REG_OK && s_preview_out.junk == 2 && s_preview_out.remainder == 10000000u);
    }

    /* Never leaving Enhanced writes no companion. */
    save(plain);
    CHECK(companion_count() == 0);
    load(plain);
    CHECK(s->config.engine == PE_ECON_LEGACY && !s->initialized && pe_econ_store_status() == PE_ECON_STORE_READY);

    /* Enhanced to Enhanced doesn't create one either; a refused Apply changes nothing. */
    CHECK(pe_econ_store_apply(&enhanced) == PE_ECON_OK);
    save(plain);
    CHECK(companion_count() == 0);
    s_free_field = 0;
    CHECK(pe_econ_store_apply(&light) == PE_ECON_NOT_FREE_FIELD && !s->initialized);
    s_free_field = 1;

    /* Applied, saved, and read back whole. */
    CHECK(pe_econ_store_apply(&light) == PE_ECON_OK);
    save(light_save);
    CHECK(companion_count() == 1);
    before = *s;
    pe_hook_dispatch(PE_HOOK_NEW_GAME, &(PeHookNewGame){PE_NEW_GAME_TITLE});
    CHECK(!s->initialized && s->config.engine == PE_ECON_LEGACY);   /* New Game inherits nothing */
    load(light_save);
    CHECK(s->initialized && s->config.engine == PE_ECON_CONFIGURABLE && s->config.income == 1 && s->config.price_pct == 75);
    CHECK(!strcmp(s->config.key, "pe.light") && !memcmp(&s->progress, &before.progress, sizeof before.progress));
    CHECK(s->progress.rng_payment && s->progress.rng_item && s->progress.rng_bundle);

    /* What the reward engine commits is what a save holds. */
    {
        PeEconProgress g = s->progress;
        g.remainder = 1234;
        g.dry = 7;
        pe_econ_store_commit(&g);
        save(light_save);
        load(light_save);
        CHECK(s->progress.remainder == 1234 && s->progress.dry == 7);
    }
    CHECK(pe_econ_store_active() == &s->config);

    /* A save without the section never inherits the one loaded before it. */
    load(plain);
    CHECK(!s->initialized && s->config.engine == PE_ECON_LEGACY);

    /* Back on Enhanced the section stays, holding the settings for later. */
    load(light_save);
    CHECK(pe_econ_store_apply(&enhanced) == PE_ECON_OK);
    save(back_save);
    load(light_save);
    CHECK(s->config.engine == PE_ECON_CONFIGURABLE);
    load(back_save);
    CHECK(s->config.engine == PE_ECON_LEGACY && s->config.income == 1 && s->initialized);

    /* Damaged: Enhanced, a toast, Apply refused, and the bytes survive further saves untouched. */
    load(light_save);
    CHECK(pe_econ_store_apply(&steady) == PE_ECON_OK);
    pe_econ_store_test_bytes()[4] = 80;   /* a valid section checksum over a bad price */
    memcpy(held, pe_econ_store_test_bytes(), sizeof held);
    save(bad_save);
    s_toasts = 0;
    load(bad_save);
    CHECK(pe_econ_store_status() == PE_ECON_STORE_DAMAGED && s_toasts == 1);
    CHECK(s->config.engine == PE_ECON_LEGACY && !s->initialized);
    CHECK(pe_econ_store_apply(&light) == PE_ECON_BAD_STATE && !s->initialized);
    CHECK(!memcmp(pe_econ_store_test_bytes(), held, sizeof held));
    save(bad_save);
    load(bad_save);
    CHECK(pe_econ_store_status() == PE_ECON_STORE_DAMAGED && !memcmp(pe_econ_store_test_bytes(), held, sizeof held));

    /* A new game clears the condition, and a good save loads normally again. The store keeps only
     * recent generations for a card, so this needs a fresh save rather than an old block. */
    pe_hook_dispatch(PE_HOOK_NEW_GAME, &(PeHookNewGame){PE_NEW_GAME_TITLE});
    CHECK(pe_econ_store_status() == PE_ECON_STORE_READY);
    CHECK(pe_econ_store_apply(&light) == PE_ECON_OK);
    save(light_save);
    load(light_save);
    CHECK(pe_econ_store_status() == PE_ECON_STORE_READY && s->config.engine == PE_ECON_CONFIGURABLE);

    /* Several contributors: adds before the shared factor, Scavenger after, the fraction last. */
    {
        PePayoutContext c;
        PePayoutResult out;
        CHECK(s_early_failures == 0);
        CHECK(pe_economy_policy_count(PE_POLICY_MODIFIER) == 2 && pe_economy_policy_count(PE_POLICY_OBSERVER) == 2);
        memset(&c, 0, sizeof c);
        c.api.version = 1;
        c.api.size = sizeof c;
        c.generation = pe_reg_generation();
        c.operation = 5;
        c.nominal_10000 = 30000;      /* 3 Junk */
        c.remainder = 50000000u;      /* 0.5 */
        c.configurable = 1;
        s_add[0] = 1; s_add[1] = 2;
        s_factor[0] = 15000; s_factor[1] = 12000;
        /* (3 + 1 + 2) * (1 + .5 + .2) = 10.2, + 0.5 */
        CHECK(pe_economy_policy_preview(&c, &out) == PE_REG_OK && out.junk == 10 && out.remainder == 70000000u);
        CHECK(s_seen_nominal[0] == 30000 && s_seen_nominal[1] == 30000);   /* each sees the owner's value */
        /* * 1.5 after the contributors: 15.3, + 0.5 */
        c.final_factor_10000 = 15000;
        CHECK(pe_economy_policy_preview(&c, &out) == PE_REG_OK && out.junk == 15 && out.remainder == 80000000u);
        c.final_factor_10000 = 0;

        /* Same result with the contributions swapped between mods: order can't matter. */
        s_add[0] = 2; s_add[1] = 1;
        s_factor[0] = 12000; s_factor[1] = 15000;
        CHECK(pe_economy_policy_preview(&c, &out) == PE_REG_OK && out.junk == 10 && out.remainder == 70000000u);

        /* Factors that take everything pay nothing but keep the fraction; below zero is refused. */
        s_factor[0] = 0; s_factor[1] = 0;
        CHECK(pe_economy_policy_preview(&c, &out) == PE_REG_CONFLICT);
        s_factor[0] = 0; s_factor[1] = 10000;
        CHECK(pe_economy_policy_preview(&c, &out) == PE_REG_OK && out.junk == 0 && out.remainder == 50000000u);

        /* One provider failing or out of range fails the preview. */
        s_factor[0] = s_factor[1] = 10000;
        s_modifier_result = PE_REG_BAD_ARGUMENT;
        memset(&out, 0xAB, sizeof out);
        CHECK(pe_economy_policy_preview(&c, &out) == PE_REG_BAD_ARGUMENT && out.junk == 0xABABABABu);
        s_modifier_result = PE_REG_OK;
        s_factor[1] = 40001;
        CHECK(pe_economy_policy_preview(&c, &out) == PE_REG_BAD_ARGUMENT);
        s_factor[1] = 10000;
        s_add[1] = 65536;
        CHECK(pe_economy_policy_preview(&c, &out) == PE_REG_BAD_ARGUMENT);
        s_add[0] = s_add[1] = 0;

        /* Repeating a preview changes nothing: it has no memory. */
        CHECK(pe_economy_policy_preview(&c, &out) == PE_REG_OK);
        {
            PePayoutResult again;
            CHECK(pe_economy_policy_preview(&c, &again) == PE_REG_OK && !memcmp(&out, &again, sizeof out));
        }

        /* Observers each hear an operation once, can't register from inside, and old ids are refused. */
        memset(s_notices, 0, sizeof s_notices);
        s_nested_register = PE_REG_OK;
        provider_test_late();
        CHECK(pe_economy_policy_notify(&c, &out) == PE_REG_OK);
        CHECK(s_notices[0] == 1 && s_notices[1] == 1 && s_last_notice[0] == 5 && s_last_notice[1] == 5);
        CHECK(s_nested_register != PE_REG_OK && s_nested_register != PE_REG_PENDING);
        CHECK(pe_economy_policy_notify(&c, &out) == PE_REG_CONFLICT && s_notices[0] == 1);
        c.operation = 4;
        CHECK(pe_economy_policy_notify(&c, &out) == PE_REG_CONFLICT && s_notices[0] == 1);
        /* Loading rewinds: the restored save's lower ids are heard again. */
        load(light_save);
        CHECK(pe_economy_policy_notify(&c, &out) == PE_REG_OK && s_notices[0] == 2);
    }

    /* Apply repeated: nothing moves, and Enhanced and back keeps the progress, without reseeding. */
    {
        PeEconState first;
        PeEconProgress g;
        CHECK(pe_econ_store_apply(&light) == PE_ECON_OK);
        g = s->progress;
        g.dry = 9;
        g.remainder = 42;
        /* Streams no seed made: a reseed inside the same clock tick would otherwise look unchanged. */
        g.rng_payment = 0x11111111u;
        g.rng_item = 0x22222222u;
        g.rng_bundle = 0x33333333u;
        pe_econ_store_commit(&g);
        first = *s;
        CHECK(pe_econ_store_apply(&light) == PE_ECON_OK && !memcmp(&s->progress, &first.progress, sizeof first.progress));
        CHECK(pe_econ_store_apply(&steady) == PE_ECON_OK && !memcmp(&s->progress, &first.progress, sizeof first.progress));
        CHECK(pe_econ_store_apply(&enhanced) == PE_ECON_OK && !pe_econ_store_active());
        CHECK(!memcmp(&s->progress, &first.progress, sizeof first.progress));
        CHECK(pe_econ_store_apply(&light) == PE_ECON_OK && pe_econ_store_active());
        CHECK(!memcmp(&s->progress, &first.progress, sizeof first.progress));
        save(back_save);
        load(back_save);
        CHECK(!memcmp(&s->progress, &first.progress, sizeof first.progress) && s->config.income == light.income);
    }

    clean();
    if (failures) return 1;
    puts("Economy policy section: save, load, New Game, damage, providers and repeated Apply passed");
    return 0;
}
