/* Challenge: Fast enemies. Enemies' turn gauges fill 1.5x or 2x as fast.
 *
 * Each battle frame the game adds an enemy's chargeStep to its gauge (atbCharge) up to 9000, a
 * little less while it is Slowed, nothing while the battle is paused (turn.c). This rule never
 * touches chargeStep (Slow, Rebalance and the enemy's data own it): at each Battle_Update it looks
 * at what every enemy's gauge actually gained since the last frame and adds a share of that gain,
 * +50% or +100%. So pauses, Slow, menus and scripted fights all keep the game's own behaviour: no
 * gain, nothing added. A gauge that went down (the turn was taken, a new enemy in the slot) only
 * restarts the tracking. Fractions are carried per slot, so 1.5x is exact over time.
 */
#include <string.h>

#include "pe_challenges.h"
#include "pe_game.h"
#include "pe_mod_host.h"

#define CH_SLOTS      7
#define CH_GAUGE_FULL 0x2328     /* 9000, the game's full gauge */

static struct {
    const EnemyCombatant *core;   /* the enemy last seen in the slot */
    int last;                     /* its gauge after our share, -1 = not tracked */
    int carry;                    /* halves carried at 1.5x */
} s_slots[CH_SLOTS];

static void pe_fast_forget(void) {
    memset(s_slots, 0, sizeof s_slots);
    for (int i = 0; i < CH_SLOTS; ++i) s_slots[i].last = -1;
}

static int pe_fast_battle(void *ctx, void *user) {
    const int level = pe_ch_level(CH_FAST_ENEMIES);
    (void)ctx; (void)user;
    if (!level || !pe_in_battle()) return 0;
    for (int i = 0; i < CH_SLOTS; ++i) {
        BattleEnemySlot *slot = &g_BattleEnemySlots[i];
        EnemyCombatant *enemy = &slot->combatant;
        const int now = enemy->atbCharge;
        int gain, extra, next;
        if (!slot->active || enemy->hpAlive <= 0) { s_slots[i].last = -1; continue; }
        if (s_slots[i].core != enemy || s_slots[i].last < 0 || now < s_slots[i].last) {
            s_slots[i].core = enemy;                  /* new enemy, or its turn just reset the gauge */
            s_slots[i].last = now;
            s_slots[i].carry = 0;
            continue;
        }
        gain = now - s_slots[i].last;
        if (gain <= 0 || now >= CH_GAUGE_FULL) { s_slots[i].last = now; continue; }
        if (level >= 2) extra = gain;                 /* 2x: the same again */
        else {                                        /* 1.5x: half again, the odd half carried */
            s_slots[i].carry += gain;
            extra = s_slots[i].carry / 2;
            s_slots[i].carry %= 2;
        }
        next = now + extra;
        if (next > CH_GAUGE_FULL) next = CH_GAUGE_FULL;
        enemy->atbCharge = (u16)next;
        s_slots[i].last = next;
    }
    return 0;
}

static int pe_fast_reset(void *ctx, void *user) {
    (void)ctx; (void)user;
    pe_fast_forget();
    return 0;
}

static int pe_fast_frame(void *ctx, void *user) {
    (void)ctx; (void)user;
    if (!pe_in_battle()) pe_fast_forget();
    return 0;
}

static void pe_fast_register(const char *mod_id) {
    pe_fast_forget();
    pe_hook_add(PE_HOOK_BATTLE_UPDATE, pe_fast_battle, NULL, PE_PRIORITY_NORMAL, mod_id);
    pe_hook_add(PE_HOOK_FRAME, pe_fast_frame, NULL, PE_PRIORITY_NORMAL, mod_id);
    pe_hook_add(PE_HOOK_SAVE_READ, pe_fast_reset, NULL, PE_PRIORITY_NORMAL, mod_id);
    pe_hook_add(PE_HOOK_NEW_GAME, pe_fast_reset, NULL, PE_PRIORITY_NORMAL, mod_id);
}

static const char *const kFastLevels[] = {"Off", "1.5x", "2x"};

const PeChallengeDef g_ch_fast_enemies = {
    CH_FAST_ENEMIES, "Fast enemies", "Fast enemies",
    "Enemies' turn gauges fill 1.5x or 2x as fast. Slow still slows them.",
    kFastLevels, 3, 0, 0, pe_fast_register
};
