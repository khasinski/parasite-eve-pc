/* Challenges: No Parasite Energy, No items in battle, No escape.
 *
 * The battle menu's tabs come from g_MenuItemContextFlag bits 0-4 (Item, PE, Weapon, Armor,
 * Escape). That word is story state and is saved with the game, so the rule clears its bits only
 * during a battle and puts the game's own bits back the first frame after. A bit the game (or
 * another mod, such as Half-bar ATB) changes mid-battle is followed, except the ones the rule holds
 * clear. No PE also dims every power, in battle and in the field (PE_HOOK_POWER_AVAILABLE: the game
 * refuses it with its error sound).
 */
#include "pe_challenges.h"
#include "pe_game.h"
#include "pe_mod_host.h"
#include "pe1/menu_state.h"

#define TAB_ITEM    0x01
#define TAB_PE      0x02
#define TAB_ESCAPE  0x10
#define TAB_MANAGED (TAB_ITEM | TAB_PE | TAB_ESCAPE)

/* After Half-bar ATB (LATE) on the battle and field hooks: its own tab save / restore runs first. */
#define CH_PRIORITY_AFTER_LATE (PE_PRIORITY_LATE + 10)

static struct {
    int held;      /* the game's tab bits are saved in base */
    int base;      /* the game's own value */
    int written;   /* what this rule last wrote */
} s_tabs;

static int pe_tabs_cleared(void) {
    int clear = 0;
    if (pe_ch_on(CH_NO_ITEMS))  clear |= TAB_ITEM;
    if (pe_ch_on(CH_NO_PE))     clear |= TAB_PE;
    if (pe_ch_on(CH_NO_ESCAPE)) clear |= TAB_ESCAPE;
    return clear;
}

static void pe_tabs_restore(void) {
    if (!s_tabs.held) return;
    /* Only the bits this rule manages go back; the rest stay as the game has them now. */
    g_MenuItemContextFlag = (g_MenuItemContextFlag & ~TAB_MANAGED) | (s_tabs.base & TAB_MANAGED);
    s_tabs.held = 0;
}

static void pe_tabs_battle(void) {
    const int clear = pe_tabs_cleared();
    const int now = g_MenuItemContextFlag;
    if (!s_tabs.held) {
        if (!clear) return;                 /* nothing to do: never touch the word */
        s_tabs.base = now;
        s_tabs.held = 1;
    } else if (now != s_tabs.written) {
        /* Someone else wrote it: follow their bits, keep the game's own for the ones held. */
        s_tabs.base = (now & ~TAB_MANAGED) | (s_tabs.base & TAB_MANAGED);
    }
    s_tabs.written = s_tabs.base & ~clear;
    if (g_MenuItemContextFlag != s_tabs.written) g_MenuItemContextFlag = s_tabs.written;
}

static int pe_tabs_on_battle(void *ctx, void *user) {
    (void)ctx; (void)user;
    if (pe_in_battle()) pe_tabs_battle();
    return 0;
}

static int pe_tabs_on_frame(void *ctx, void *user) {
    (void)ctx; (void)user;
    if (!pe_in_battle()) pe_tabs_restore();
    return 0;
}

/* A load or a new game brings the game's own word: drop what was held, never restore over it. */
static int pe_tabs_forget(void *ctx, void *user) {
    (void)ctx; (void)user;
    s_tabs.held = 0;
    return 0;
}

static int pe_tabs_power(void *ctx, void *user) {
    PeHookPowerAvailable *p = ctx;
    (void)user;
    if (pe_ch_on(CH_NO_PE) && pe_save_loaded() && p && p->enabled) *p->enabled = 0;
    return 0;
}

void pe_ch_tabs_register(const char *mod_id) {
    pe_hook_add(PE_HOOK_BATTLE_UPDATE, pe_tabs_on_battle, NULL, CH_PRIORITY_AFTER_LATE, mod_id);
    pe_hook_add(PE_HOOK_FRAME, pe_tabs_on_frame, NULL, CH_PRIORITY_AFTER_LATE, mod_id);
    pe_hook_add(PE_HOOK_SAVE_READ, pe_tabs_forget, NULL, PE_PRIORITY_NORMAL, mod_id);
    pe_hook_add(PE_HOOK_NEW_GAME, pe_tabs_forget, NULL, PE_PRIORITY_NORMAL, mod_id);
    pe_hook_add(PE_HOOK_POWER_AVAILABLE, pe_tabs_power, NULL, PE_PRIORITY_LATE, mod_id);
}
