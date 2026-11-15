#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "pe_game.h"
#include "pe_mod_host.h"
#include "pe_mod_menu.h"
#include "pe_atb_items.h"
#include "pe1/menu_state.h"
#include "pe1/battle_status.h"

SaveRuntimeState g_SaveRuntimeState;
Combatant *g_ActiveActor;
BattleInitSlot g_BattleActionQueue[45];
u8 g_BattleActionQueueCount, g_BattleGaugeCapLatch;
int g_BattleModeState, g_MenuItemContextFlag;
s8 g_BattleItemMenuState;
unsigned int g_FieldPadPressed;
RenderGouraudQuad g_BattleAtbGauges[2];
static Combatant aya;
static BattleEntity player;
static int battle = 1;
static PeHookFn hooks[PE_HOOK_COUNT];
static PeMenuPerFrame balance_tick;
static int *rules, *quick_heal1;
int g_pe_panacea;
int pe_game_loaded(void) { return 1; }
int pe_in_battle(void) { return battle; }
Combatant *pe_aya_core(void) { return &aya; }
BattleEntity *pe_player(void) { return &player; }
void pe_log(const char *tag, const char *fmt, ...) { (void)tag; (void)fmt; }
void pe_toast(const char *text) { (void)text; }
int pe_service_publish(const char *n,int v,const void *api,const char *mod) {
    (void)n;(void)v;(void)api;(void)mod;return 0;
}
int pe_hook_add(int id, PeHookFn fn, void *user, int priority, const char *mod) {
    (void)user; (void)priority; (void)mod; hooks[id] = fn; return 0;
}
int pe_menu_register_toggle(const char *p, const char *l, const char *d, int *v, PeMenuPerFrame t) {
    (void)p; (void)l; (void)d; (void)v; (void)t; return 0;
}
int pe_menu_register_int(const char *p, const char *l, const char *d, int *v, int min, int max, int step) {
    (void)p; (void)l; (void)d; (void)v; (void)min; (void)max; (void)step; return 0;
}
int pe_menu_register_choice(const char *p, const char *l, const char *d, int *v,
                            const char *const *names, unsigned count) {
    (void)p; (void)d; (void)names; (void)count;
    if (!strcmp(l, "Rules")) rules = v;
    if (!strcmp(l, "Heal 1")) quick_heal1 = v;
    return 0;
}
int pe_menu_register_tick(PeMenuPerFrame t) { balance_tick = t; return 0; }
void pe_pe_balance_register(const char *);
static void update(void) { hooks[PE_HOOK_BATTLE_UPDATE](NULL, NULL); }
static void reset(void) {
    hooks[PE_HOOK_SAVE_READ](NULL, NULL);
    memset(&aya, 0, sizeof aya);
    memset(&g_SaveRuntimeState, 0, sizeof g_SaveRuntimeState);
    memset(g_BattleAtbGauges, 0, sizeof g_BattleAtbGauges);
    memset(g_BattleActionQueue, 0, sizeof g_BattleActionQueue);
    g_BattleModeState = 0; g_FieldPadPressed = 0; g_BattleActionQueueCount = 0;
    g_MenuItemContextFlag = 0x11F; g_BattleItemMenuState = 1;
    aya.scaledOffense = 20; aya.stat22 = 50; battle = 1;
    g_pe_half_enabled = 1;
    g_pe_half_allow[0] = g_pe_half_allow[1] = g_pe_half_allow[2] = 1;
    g_pe_half_allow[3] = g_pe_half_allow[4] = 0;
    g_pe_quick_mask = 0x59B;
}
static void borrow(unsigned gauge) {
    aya.hpAlive = gauge; g_FieldPadPressed = 0x80; update();
    assert(aya.hpAlive == 9000 && g_MenuItemContextFlag == 0x107);
    g_FieldPadPressed = 0; g_BattleModeState = 1; update();
}
static void command(int id) {
    g_BattleActionQueueCount = 1; g_BattleActionQueue[0].command = id; update();
}
static void finish(void) {
    g_BattleModeState = 0; g_BattleActionQueueCount = 0; aya.hpAlive = 0; update();
    assert(g_MenuItemContextFlag == 0x11F);
}
int main(void) {
    pe_atb_items_register("pe_rebalance");
    /* Disabled means no changes to native gauge, input, tabs or HUD, at every phase. */
    const unsigned gauges[] = {0, 4499, 4500, 8999, 9000};
    for (unsigned i = 0; i < sizeof gauges / sizeof gauges[0]; ++i)
        for (int phase = 0; phase < 4; ++phase) {
            reset(); g_pe_half_enabled = 0; g_BattleModeState = phase;
            aya.hpAlive = gauges[i]; aya.scaledOffense = 0;
            g_FieldPadPressed = 0x280;
            memset(g_BattleAtbGauges, 0x35, sizeof g_BattleAtbGauges);
            Combatant before = aya;
            RenderGouraudQuad hud[2]; memcpy(hud, g_BattleAtbGauges, sizeof hud);
            update();
            assert(!memcmp(&before, &aya, sizeof aya));
            assert(!memcmp(hud, g_BattleAtbGauges, sizeof hud));
            assert(g_FieldPadPressed == 0x280 && g_MenuItemContextFlag == 0x11F);
        }
    reset(); aya.hpAlive = 4499; g_FieldPadPressed = 0x80; update(); assert(aya.hpAlive == 4499);
    reset(); borrow(6000); assert(g_BattleAtbGauges[0].r0 == 0x60);
    command(3); finish(); assert(aya.hpAlive == 1500);
    reset(); borrow(4500); command(3); finish(); assert(aya.hpAlive == 0);
    reset(); borrow(6500); g_BattleItemMenuState = 0; g_FieldPadPressed = 0x200; update();
    assert((g_FieldPadPressed & 0x2000) && !(g_FieldPadPressed & 0x200));
    aya.scaledOffense = 0; finish(); assert(aya.hpAlive == 6500 && aya.scaledOffense == 20);
    reset(); aya.hpAlive = 6000; g_FieldPadPressed = 0x80; update(); g_FieldPadPressed = 0; update();
    assert(aya.hpAlive == 6000 && g_MenuItemContextFlag == 0x11F); /* rejected menu */
    reset(); borrow(6000);
    for (int power = 0; power < 20; ++power) {
        int enabled = 1; PeHookPowerAvailable query = {power, &enabled};
        hooks[PE_HOOK_POWER_AVAILABLE](&query, NULL);
        assert(enabled == (int)((g_pe_quick_mask >> power) & 1u));
        enabled = 0; hooks[PE_HOOK_POWER_AVAILABLE](&query, NULL); assert(!enabled);
    }
    reset(); borrow(6000); command(0x183 + 7); finish(); assert(aya.hpAlive == 1500); /* Quick Scan */
    reset(); borrow(6000); command(0x183 + 2); finish(); assert(aya.hpAlive == 0); /* full Heal 3 */
    aya.hpAlive = 2000; update(); assert(aya.hpAlive == 0);
    aya.hpAlive = 1500; update(); assert(aya.hpAlive == 500); /* remaining debt 1000 */
    reset(); borrow(6000); command(0x183 + 6); command(1); finish();
    aya.hpAlive = 9000; update(); assert(aya.hpAlive == 6000); /* debt applies even at cap */
    reset(); g_BattleModeState = 1; aya.hpAlive = 9000; update(); command(3); finish();
    assert(aya.hpAlive == 4500); /* item from full bar */
    reset(); g_BattleModeState = 1; aya.hpAlive = 9000; update(); command(1); finish();
    assert(aya.hpAlive == 0); /* normal attacks still full cost */
    reset(); borrow(6000); g_AyaSaveState.equipped_weapon_slot = 2; update(); finish(); assert(aya.hpAlive == 1500);
    reset(); borrow(6000); hooks[PE_HOOK_SCENE_START](NULL, NULL); assert(g_MenuItemContextFlag == 0x11F);
    reset(); borrow(6000); g_MenuItemContextFlag = 0x123;
    hooks[PE_HOOK_SAVE_READ](NULL, NULL); assert(g_MenuItemContextFlag == 0x123); /* do not write old tabs over save */
    reset(); g_pe_half_enabled = 0; aya.scaledOffense = 0; aya.hpAlive = 6000;
    g_FieldPadPressed = 0x80; update(); assert(aya.hpAlive == 6000 && aya.scaledOffense == 0);
    reset(); g_MenuItemContextFlag = 0x118; aya.hpAlive = 6000; g_FieldPadPressed = 0x80;
    update(); assert(aya.hpAlive == 6000 && g_MenuItemContextFlag == 0x118); /* no available tab */
    pe_pe_balance_register("pe_rebalance"); assert(g_pe_half_enabled && g_pe_quick_mask == 0x59B);
    *rules = 0; balance_tick(); assert(!g_pe_half_enabled && !g_pe_quick_mask && !g_pe_panacea);
    *rules = 1; balance_tick(); assert(g_pe_half_enabled && g_pe_quick_mask == 0x59B && g_pe_panacea);
    *quick_heal1 = 0; balance_tick(); assert(*rules == 2 && g_pe_quick_mask == 0x59A);
    puts("Passed native half-bar tests: thresholds, item/weapon costs, cancel/rejected-menu refunds, Quick/full PE, attack debt, rate recovery, tab cleanup, Vanilla and preset settings.");
    return 0;
}
