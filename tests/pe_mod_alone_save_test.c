/* A save that must be played alone (PeSaveRecord.wants_alone): loading it stops every other mod
 * before their records go back, whatever the session started with; only pe_hook_add_beside handlers
 * go on. A mod whose init failed is never active. */
#include <stdio.h>
#include <string.h>
#include "pe_hooks.h"
#include "pe_mod_host.h"
#include "pe_mod_menu.h"
#include "pe_save.h"

static int frames_other, frames_beside, frames_broken, frames_keeper, shutdowns;
static const int api = 1;
static struct { int on; } s_run;
static struct { int value; } s_other;
static uint8_t s_block[PE_SAVE_TAIL_OFFSET + PE_SAVE_TAIL_SIZE];

static int count(void *ctx, void *user) { (void)ctx; ++*(int *)user; return 0; }
static int wants_alone(const void *data) { return ((const struct { int on; } *)data)->on; }

static const PeSaveRecord s_run_record = {"pe_keeper.run", 1, &s_run, sizeof s_run, NULL, NULL, NULL, NULL, wants_alone};
static const PeSaveRecord s_other_record = {"pe_other.data", 1, &s_other, sizeof s_other, NULL, NULL, NULL, NULL, NULL};

static int init_other(void) {
    pe_service_publish("pe_test_other", 1, &api, "pe_other");
    pe_hook_add(PE_HOOK_FRAME, count, &frames_other, PE_PRIORITY_NORMAL, "pe_other");
    pe_hook_add_beside(PE_HOOK_FRAME, count, &frames_beside, PE_PRIORITY_NORMAL, "pe_other");
    return pe_save_register(&s_other_record) < 0;
}
static void shutdown_other(void) { shutdowns++; }
static int init_broken(void) {
    pe_hook_add(PE_HOOK_FRAME, count, &frames_broken, PE_PRIORITY_NORMAL, "pe_broken");
    return 1;
}
static int init_keeper(void) {
    pe_hook_add(PE_HOOK_FRAME, count, &frames_keeper, PE_PRIORITY_NORMAL, "pe_keeper");
    return pe_save_register(&s_run_record) < 0;
}

static const PeMod mod_other = {"pe_other", "Other", "1", NULL, init_other, shutdown_other};
static const PeMod mod_broken = {"pe_broken", "Broken", "1", NULL, init_broken, NULL};
static const PeMod mod_keeper = {"pe_keeper", "Keeper", "1", NULL, init_keeper, NULL};
const PeMod *const g_pe_mods[] = {&mod_other, &mod_broken, &mod_keeper, NULL};

/* The host's menu and panel, not under test: every toggle on. */
void pe_main_menu_init(void) {}
void pe_equip_sync_init(void) {}
void pe_present_install(void) {}
void pe_menu_begin(const char *page) { (void)page; }
void pe_menu_load_settings(void) {}
void pe_menu_alias_setting(const char *key, const char *legacy) { (void)key; (void)legacy; }
int pe_menu_register_toggle(const char *sub, const char *label, const char *description, int *flag,
                            PeMenuPerFrame per_frame) {
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
void pe_toast(const char *text) { (void)text; }
int pe_free_field(void) { return 1; }
void pe_pages_describe(void) {}

static int failures;
#define CHECK(cond) \
    do { \
        if (!(cond)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #cond); failures++; } \
    } while (0)

int main(void) {
    PeHookInit init = {0, NULL};
    CHECK(pe_host_init(&init) == 0);

    /* Every mod runs, but the one whose init failed. */
    CHECK(pe_mod_active("pe_other") && pe_mod_active("pe_keeper"));
    CHECK(!pe_mod_active("pe_broken"));
    CHECK(pe_mod_active("host") && pe_mod_active(NULL));
    pe_hook_dispatch(PE_HOOK_FRAME, NULL);
    CHECK(frames_other == 1 && frames_beside == 1 && frames_keeper == 1 && frames_broken == 0);

    /* A save that doesn't ask changes nothing. */
    pe_hook_dispatch(PE_HOOK_SAVE_WRITE, s_block);
    pe_hook_dispatch(PE_HOOK_SAVE_READ, s_block);
    CHECK(pe_mod_active("pe_other") && shutdowns == 0);

    /* A save that asks: the other mod stops before its record loads. */
    s_run.on = 1;
    s_other.value = 7;
    pe_hook_dispatch(PE_HOOK_SAVE_WRITE, s_block);
    s_run.on = 0;
    s_other.value = 0;
    pe_hook_dispatch(PE_HOOK_SAVE_READ, s_block);
    CHECK(s_run.on == 1);
    CHECK(pe_mod_active("pe_keeper") && !pe_mod_active("pe_other"));
    CHECK(s_other.value == 0);                       /* an off mod's record doesn't go back */
    CHECK(shutdowns == 1);
    CHECK(pe_service_get("pe_test_other", 1) == NULL);

    pe_hook_dispatch(PE_HOOK_FRAME, NULL);
    CHECK(frames_keeper == 2);
    CHECK(frames_other == 1);                        /* stopped */
    CHECK(frames_beside == 2);                       /* goes on */
    CHECK(frames_broken == 0);

    CHECK(pe_mod_stand_alone("pe_keeper") == 0);     /* once */

    /* The record it kept goes back into later saves: nothing is lost. */
    pe_hook_dispatch(PE_HOOK_SAVE_WRITE, s_block);
    pe_hook_dispatch(PE_HOOK_SAVE_READ, s_block);
    CHECK(s_run.on == 1);

    if (failures) return 1;
    puts("pe_mod_alone_save: ok");
    return 0;
}
