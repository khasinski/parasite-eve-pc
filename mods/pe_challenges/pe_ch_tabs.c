/* Challenges: battle menu tabs. Each challenge says which tabs it takes away
 * (PeChallengeDef.battle_tabs); this file does it for all of them.
 *
 * The tabs come from g_MenuItemContextFlag bits 0-4 (Item, PE, Weapon, Armor, Escape). That word is
 * story state and is saved with the game, so the tabs are cleared only during a battle and the
 * game's own bits go back the first frame after. A bit the game (or another mod, such as Half-bar
 * ATB) changes mid-battle is followed, except the ones held clear. The game's menus always keep at
 * least one tab: if every tab would go, Weapon stays.
 */
#include "pe_challenges.h"
#include "pe_game.h"
#include "pe_mod_host.h"
#include "pe1/menu_state.h"

#define TAB_MANAGED ((int)(CH_TAB_ITEM | CH_TAB_PE | CH_TAB_WEAPON | CH_TAB_ARMOR | CH_TAB_ESCAPE))

/* After Half-bar ATB (LATE) on the battle and field hooks: its own tab save / restore runs first. */
#define CH_PRIORITY_AFTER_LATE (PE_PRIORITY_LATE + 10)

static struct {
    int held;      /* the game's tab bits are saved in base */
    int base;      /* the game's own value */
    int written;   /* what was last written */
} s_tabs;

static int pe_tabs_cleared(void) {
    unsigned clear = 0;
    for (unsigned i = 0; i < g_ch_def_count; ++i)
        if (pe_ch_on(g_ch_defs[i]->id)) clear |= g_ch_defs[i]->battle_tabs;
    return (int)clear & TAB_MANAGED;
}

static void pe_tabs_restore(void) {
    if (!s_tabs.held) return;
    /* Only the tab bits go back; the rest stay as the game has them now. */
    g_MenuItemContextFlag = (g_MenuItemContextFlag & ~TAB_MANAGED) | (s_tabs.base & TAB_MANAGED);
    s_tabs.held = 0;
}

static void pe_tabs_battle(void) {
    int clear = pe_tabs_cleared();
    const int now = g_MenuItemContextFlag;
    if (!s_tabs.held) {
        if (!clear) return;                 /* nothing to do: never touch the word */
        s_tabs.base = now;
        s_tabs.held = 1;
    } else if (now != s_tabs.written) {
        /* Someone else wrote it: follow their bits, keep the game's own for the managed ones. */
        s_tabs.base = (now & ~TAB_MANAGED) | (s_tabs.base & TAB_MANAGED);
    }
    if (!(s_tabs.base & TAB_MANAGED & ~clear)) clear &= ~(int)CH_TAB_WEAPON;
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

void pe_ch_tabs_register(const char *mod_id) {
    pe_hook_add(PE_HOOK_BATTLE_UPDATE, pe_tabs_on_battle, NULL, CH_PRIORITY_AFTER_LATE, mod_id);
    pe_hook_add(PE_HOOK_FRAME, pe_tabs_on_frame, NULL, CH_PRIORITY_AFTER_LATE, mod_id);
    pe_hook_add(PE_HOOK_SAVE_READ, pe_tabs_forget, NULL, PE_PRIORITY_NORMAL, mod_id);
    pe_hook_add(PE_HOOK_NEW_GAME, pe_tabs_forget, NULL, PE_PRIORITY_NORMAL, mod_id);
}
