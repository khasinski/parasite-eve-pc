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
const PeMod *const g_pe_mods[] = {&econ_mod, NULL};

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

    clean();
    if (failures) return 1;
    puts("Economy policy section: save, load, New Game and damage passed");
    return 0;
}
