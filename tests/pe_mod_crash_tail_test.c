/* Crash report room snapshot, save tail: the host's records fill the snapshot
 * block as a real save writes them, without calling any mod code; nothing is
 * taken while mod code is running, and later saves still reach the mods. */
#include <stdio.h>
#include <string.h>
#include "pe_hooks.h"
#include "pe_mod_host.h"
#include "pe_mod_menu.h"
#include "pe_save.h"

static int writes, inside_mod, cancel_ex, new_games, last_kind, reset_before_callback = 1;
static unsigned char card[0x2000], live[0x2000];
static unsigned char state[4] = {'T', 'E', 'S', 'T'};
static const PeSaveRecord record = {"pe_test.state", 1, state, sizeof state, NULL, NULL, NULL};

static int on_save_write(void *ctx, void *user) {
    (void)ctx; (void)user;
    writes++;
    return 0;
}

static int on_frame(void *ctx, void *user) {
    (void)ctx; (void)user;
    if (inside_mod) inside_mod = pe_mod_snapshot_save_tail(card);
    return 0;
}

static int on_command(void *ctx, void *user) {
    (void)user;
    return cancel_ex && ((PeHookInvCommand *)ctx)->command == 1117;
}
static int on_new_game(void *ctx, void *user) {
    const unsigned char zero[sizeof state] = {0};
    (void)user;
    if (memcmp(state, zero, sizeof state)) reset_before_callback = 0;
    last_kind = ((PeHookNewGame *)ctx)->kind;
    ++new_games;
    return 0;
}
static int test_init(void) {
    pe_hook_add(PE_HOOK_INV_COMMAND, on_command, NULL, PE_PRIORITY_NORMAL, "pe_test");
    pe_hook_add(PE_HOOK_NEW_GAME, on_new_game, NULL, PE_PRIORITY_NORMAL, "pe_test");
    pe_hook_add(PE_HOOK_SAVE_WRITE, on_save_write, NULL, PE_PRIORITY_NORMAL, "pe_test");
    pe_hook_add(PE_HOOK_FRAME, on_frame, NULL, PE_PRIORITY_NORMAL, "pe_test");
    return pe_save_register(&record);
}

static const PeMod test_mod = {"pe_test", "Test", "1", NULL, test_init, NULL};
const PeMod *const g_pe_mods[] = {&test_mod, NULL};

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

/* The record's bytes in a block's tail, or NULL. */
static const unsigned char *find_state(const unsigned char *block) {
    const unsigned char *t = block + PE_SAVE_TAIL_OFFSET;
    unsigned total = t[6] | t[7] << 8, at;
    if (memcmp(t, "PEMC", 4)) return NULL;
    for (at = PE_SAVE_HEADER_SIZE; at + PE_SAVE_RECORD_HEADER_SIZE <= total;) {
        unsigned size = t[at + 6] | t[at + 7] << 8;
        if (size == sizeof state && !memcmp(t + at + PE_SAVE_RECORD_HEADER_SIZE, state, sizeof state))
            return t + at + PE_SAVE_RECORD_HEADER_SIZE;
        at += PE_SAVE_RECORD_HEADER_SIZE + size;
    }
    return NULL;
}

int main(void) {
    PeHookInit init = {0, NULL};
    char report[2048];
    CHECK(pe_host_init(&init) == 0);

    /* A normal save still reaches the mods, and holds the record. */
    pe_hook_dispatch(PE_HOOK_SAVE_WRITE, live);
    CHECK(writes == 1 && find_state(live));

    /* Asked while mod code is running: nothing is taken. */
    inside_mod = 1;
    pe_hook_dispatch(PE_HOOK_FRAME, NULL);
    CHECK(inside_mod == -1);
    CHECK(writes == 1 && card[PE_SAVE_TAIL_OFFSET] == 0);

    /* The snapshot: the record, and no mod code. */
    CHECK(pe_mod_snapshot_save_tail(card) == 0);
    CHECK(writes == 1 && find_state(card));

    /* Every room takes one: later saves and snapshots still work. */
    memset(live, 0, sizeof(live));
    pe_hook_dispatch(PE_HOOK_SAVE_WRITE, live);
    CHECK(writes == 2 && find_state(live));
    memset(card, 0, sizeof(card));
    CHECK(pe_mod_snapshot_save_tail(card) == 0);
    CHECK(writes == 2 && find_state(card));

    /* The crash report lists the snapshot's records. */
    pe_mod_crash_describe(report, sizeof report, 1);
    CHECK(strstr(report, "pe_test.state") != NULL);

    /* A handled inventory command never reaches the original EX Game code. */
    memcpy(state, "TEST", sizeof state);
    PeHookInvCommand ex = {1117, 0, 0};
    cancel_ex = 1;
    CHECK(pe_hook_dispatch(PE_HOOK_INV_COMMAND, &ex) == 1);
    CHECK(!memcmp(state, "TEST", sizeof state) && new_games == 0);
    cancel_ex = 0;
    const int kinds[] = {PE_NEW_GAME_TITLE, PE_NEW_GAME_TUTORIAL, PE_NEW_GAME_EX};
    for (unsigned i = 0; i < sizeof kinds / sizeof kinds[0]; ++i) {
        memcpy(state, "TEST", sizeof state);
        if (kinds[i] == PE_NEW_GAME_EX) pe_hook_dispatch(PE_HOOK_INV_COMMAND, &ex);
        else pe_hook_dispatch(PE_HOOK_NEW_GAME, &(PeHookNewGame){kinds[i]});
        CHECK(new_games == (int)i + 1 && last_kind == kinds[i] && reset_before_callback);
    }
    if (failures) return 1;
    printf("room snapshot save tail: ok\n");
    return 0;
}
