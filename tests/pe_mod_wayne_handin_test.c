/* Quick hand-in (#247): Wayne's Discard junk loop, driven the way his retail script runs it, through
 * the wallet's real hooks. Each step: his count +1, the remove command (with the script cursor on
 * the loop's "L = L - 1", words as on the disc), the game's remove, L -= 1. Quick hand-in must give
 * the same end (his count, the counter, the bag) in fewer steps, from the counter or the bag. */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "../mods/pe_economy/pe_loot.c"
#include "pe_econ_test_host.inc"
SaveRuntimeState g_SaveRuntimeState;
GameStateBlock g_GameStateBlock;
Pe1GameState g_GameState;
InventoryItemWorkspace g_InventoryItemWorkspace;
FieldActor *g_CurrentEntity;
ScriptCursorValue g_ScriptCursorValue;
s16 *g_InvItemPtr;
s32 g_InvSlotLimit;
int g_InvBonusSlotCount;

/* ---- the host and game around the wallet ---- */
static unsigned s_wayne;
unsigned pe_wayne_count(void) { return s_wayne; }
void pe_set_wayne_count(unsigned n) { s_wayne = n; }
int pe_game_loaded(void) { return 1; }
int pe_free_field(void) { return 0; }        /* his talk holds the field */
int pe_in_battle(void) { return 0; }
ItemDataRecord *pe_equipped(int armor) { (void)armor; return NULL; }
int pe_rec_has_effect(const ItemDataRecord *rec, int effect) { (void)rec; (void)effect; return 0; }
void *Text_LookupTable8(unsigned int i) { (void)i; return (void *)""; }
void pe_log(const char *tag, const char *fmt, ...) { (void)tag; (void)fmt; }
void pe_mod_log(const char *m, const char *fmt, ...) { (void)m; (void)fmt; }
void pe_toast(const char *text) { (void)text; }
int pe_hook_add(int id, PeHookFn fn, void *u, int p, const char *m) { (void)id; (void)fn; (void)u; (void)p; (void)m; return 0; }
int pe_service_publish(const char *n, int v, const void *a, const char *m) { (void)n; (void)v; (void)a; (void)m; return 0; }
int pe_save_register(const PeSaveRecord *r) { (void)r; return 0; }
int pe_menu_register_toggle(const char *s, const char *l, const char *d, int *f, PeMenuPerFrame t) { (void)s; (void)l; (void)d; (void)f; (void)t; return 0; }
int pe_menu_register_action(const char *s, const char *l, const char *d, PeMenuAction a) { (void)s; (void)l; (void)d; (void)a; return 0; }
int pe_menu_register_choice(const char *s, const char *l, const char *d, int *i, const char *const *n, unsigned c) { (void)s; (void)l; (void)d; (void)i; (void)n; (void)c; return 0; }
int pe_menu_register_int(const char *s, const char *l, const char *d, int *v, int lo, int hi, int st) { (void)s; (void)l; (void)d; (void)v; (void)lo; (void)hi; (void)st; return 0; }
int pe_menu_register_info(const char *s, const char *l, const char *d, PeMenuText t) { (void)s; (void)l; (void)d; (void)t; return 0; }
int pe_menu_register_live_int(const char *s, const char *l, const char *d, int lo, int hi, int st, PeMenuGet g, PeMenuSet t) { (void)s; (void)l; (void)d; (void)lo; (void)hi; (void)st; (void)g; (void)t; return 0; }
int pe_menu_register_live_choice(const char *s, const char *l, const char *d, const char *const *n, unsigned c, PeMenuGet g, PeMenuSet t) { (void)s; (void)l; (void)d; (void)n; (void)c; (void)g; (void)t; return 0; }
int pe_menu_register_tick(PeMenuPerFrame t) { (void)t; return 0; }

/* Stacked items, as pe_stacks keeps them: extra copies per bag slot. */
static int s_stacks_on, s_extra[PE_BAG_SLOTS];
static int st_count(int l, int slot) { return l == 0 && g_AyaSaveState.inventory_items[slot] ? 1 + s_extra[slot] : 0; }
static int st_total(int l, uint16_t item) {
    int i, n = 0;
    for (i = 0; l == 0 && i < PE_BAG_SLOTS; ++i) if (g_AyaSaveState.inventory_items[i] == (s16)item) n += 1 + s_extra[i];
    return n;
}
static int st_take(int l, int slot) {
    if (l != 0 || !g_AyaSaveState.inventory_items[slot]) return 0;
    if (s_extra[slot]) --s_extra[slot]; else g_AyaSaveState.inventory_items[slot] = 0;
    return 1;
}
static const PeStacksApi s_stacks = { st_count, st_total, NULL, NULL, st_take };
const void *pe_service_get(const char *name, int version) {
    (void)version;
    if (!strcmp(name, "stacks")) return s_stacks_on ? (const void *)&s_stacks : NULL;
    return !strcmp(name, "wallet") ? (const void *)&s_wallet_api : !strcmp(name, "junk") ? (const void *)&s_junk_api : NULL;
}

/* ---- Wayne's loop ---- */
#define LOCAL 0x19                               /* his crafting version's L (0xd, 0xb, 0x7 elsewhere) */
static FieldActor s_actor;
static u32 s_after_remove[] = { 0x00B08009u, 0, 1, 0, LOCAL, 1, 0x0032400Au, 0, LOCAL, 0, 0x00002000u, 0 };
static u32 s_elsewhere[] = { 0x00B08009u, 0, 0, 0, LOCAL, 1, 0x0032400Au, 0, LOCAL, 0, 0x00002000u, 0 };
static size_t s_task_bytes = sizeof s_after_remove;
size_t Script_TaskBytesAvailable(const void *address) {
    if (address == s_after_remove || address == s_elsewhere) return s_task_bytes;
    return 0;
}

/* The game's own remove (Inv_DispatchCommand 1112): the first Junk slot of the active list. */
static void game_remove(void) {
    int i;
    for (i = 0; i < g_InvSlotLimit; ++i) {
        if (g_InvItemPtr[i] != PE_ITEM_JUNK) continue;
        if (g_InvItemPtr == g_AyaSaveState.inventory_items && s_stacks_on && s_extra[i]) --s_extra[i];
        else g_InvItemPtr[i] = 0;
        return;
    }
}
static int count_junk(void) {
    int i, n = 0;
    for (i = 0; i < g_InvSlotLimit; ++i)
        if (g_InvItemPtr[i] == PE_ITEM_JUNK)
            n += 1 + (g_InvItemPtr == g_AyaSaveState.inventory_items && s_stacks_on ? s_extra[i] : 0);
    return n;
}
/* One Discard junk; returns the steps his loop took. */
static int hand_in(u32 *cursor) {
    int item = PE_ITEM_JUNK, result = 0, *args[2] = { &item, &result }, steps = 0;
    s_frames += 200;                             /* a fresh talk */
    pe_on_item_count(args, NULL);
    result = count_junk();                       /* the hook only redirects the list it reads */
    s_actor.pad_0AC[LOCAL] = (unsigned)result;
    while ((int)s_actor.pad_0AC[LOCAL] > 0) {
        PeHookInvCommand c = { PE_INV_CMD_REMOVE_ITEM, PE_ITEM_JUNK, 0 };
        ++s_wayne;
        g_CurrentEntity = &s_actor;
        g_ScriptCursor = cursor;
        pe_on_inv_command(&c, NULL);
        game_remove();
        --s_actor.pad_0AC[LOCAL];
        ++steps;
        assert(steps < 5000);
    }
    return steps;
}
static void reset(int quick, int enabled, int mode, uint32_t junk, unsigned wayne) {
    memset(&g_AyaSaveState.inventory_items, 0, sizeof g_AyaSaveState.inventory_items);
    memset(s_extra, 0, sizeof s_extra);
    g_AyaSaveState.inventory_slot_count = 30;
    g_InvItemPtr = g_AyaSaveState.inventory_items;
    g_InvSlotLimit = 30;
    g_AyaSaveState.inventory_items[0] = 0x2A;    /* something else in the bag */
    s_quick_handin = quick; s_enabled = enabled; s_handin_mode = mode;
    s_st.junk = junk; s_wayne = wayne;
    s_handin_active = 0; s_handin_left = 0; s_list_redirected = 0; s_stacks_on = 0;
}
static int bag_junk(void) { int s = s_stacks_on; s_stacks_on = 1; int n = st_total(0, PE_ITEM_JUNK); s_stacks_on = s; return n; }

int main(void) {
    int off, on, i;
    unsigned w_off; uint32_t j_off;

    /* Counter, "Next gun": 450 Junk, Wayne at 0 -> he takes 300. */
    reset(0, 1, 0, 450, 0); off = hand_in(s_after_remove); w_off = s_wayne; j_off = s_st.junk;
    reset(1, 1, 0, 450, 0); on = hand_in(s_after_remove);
    assert(w_off == 300 && j_off == 150 && off == 300);
    assert(s_wayne == w_off && s_st.junk == j_off && on == 3 && bag_junk() == 0);
    printf("counter 300 (Next gun): %d steps -> %d, Wayne %u, counter %u\n", off, on, s_wayne, (unsigned)s_st.junk);

    /* Counter, "Next gun" from 250: 50 in one step. */
    reset(1, 1, 0, 450, 250); on = hand_in(s_after_remove);
    assert(on == 1 && s_wayne == 300 && s_st.junk == 400);

    /* Counter, "All Junk": capped at 999. */
    reset(0, 1, 1, 1500, 0); off = hand_in(s_after_remove); w_off = s_wayne; j_off = s_st.junk;
    reset(1, 1, 1, 1500, 0); on = hand_in(s_after_remove);
    assert(w_off == 999 && j_off == 501 && s_wayne == w_off && s_st.junk == j_off && on == 10);
    printf("counter 999 (All Junk): %d steps -> %d\n", off, on);

    /* Every size from 1 to 400, both modes: same end as one at a time. */
    for (i = 1; i <= 400; ++i) {
        int m;
        for (m = 0; m < 2; ++m) {
            reset(0, 1, m, (uint32_t)i, 37); off = hand_in(s_after_remove); w_off = s_wayne; j_off = s_st.junk;
            reset(1, 1, m, (uint32_t)i, 37); on = hand_in(s_after_remove);
            assert(s_wayne == w_off && s_st.junk == j_off && bag_junk() == 0);
            assert(on == (off + 99) / 100);
        }
    }

    /* Bag, Junk economy off, stacked: 3 slots of 99, 99, 52 (250 Junk). */
    for (i = 0; i < 2; ++i) {
        reset(i, 0, 0, 0, 10); s_stacks_on = 1;
        g_AyaSaveState.inventory_items[3] = g_AyaSaveState.inventory_items[5] = g_AyaSaveState.inventory_items[9] = PE_ITEM_JUNK;
        s_extra[3] = 98; s_extra[5] = 98; s_extra[9] = 51;
        if (!i) off = hand_in(s_after_remove); else on = hand_in(s_after_remove);
        assert(s_wayne == 260 && bag_junk() == 0 && g_AyaSaveState.inventory_items[0] == 0x2A);
    }
    assert(off == 250 && on == 3);
    printf("bag 250 stacked (economy off): %d steps -> %d\n", off, on);

    /* Bag without Stacked items: 20 plain slots. */
    reset(1, 0, 0, 0, 0);
    for (i = 1; i <= 20; ++i) g_AyaSaveState.inventory_items[i] = PE_ITEM_JUNK;
    on = hand_in(s_after_remove);
    assert(on == 1 && s_wayne == 20 && bag_junk() == 0 && g_AyaSaveState.inventory_items[0] == 0x2A);

    /* A remove from anything but his loop (cursor elsewhere) is never bundled. */
    reset(1, 1, 0, 450, 0); on = hand_in(s_elsewhere);
    assert(on == 300 && s_wayne == 300 && s_st.junk == 150);
    reset(1, 0, 0, 0, 0);
    for (i = 1; i <= 20; ++i) g_AyaSaveState.inventory_items[i] = PE_ITEM_JUNK;
    on = hand_in(s_elsewhere);
    assert(on == 20 && s_wayne == 20);

    /* Even a matching loop prefix is not safe to inspect when the task ends
     * before the complete guard. Keep the original pace for every truncation. */
    for (i = 0; i < 10; ++i) {
        s_task_bytes = (size_t)i * sizeof(u32);
        reset(1, 1, 0, 450, 0); on = hand_in(s_after_remove);
        assert(on == 300 && s_wayne == 300 && s_st.junk == 150);
    }
    s_task_bytes = sizeof s_after_remove;

    puts("Wayne hand-in: Quick hand-in matches one-at-a-time from the counter and the bag");
    return 0;
}
