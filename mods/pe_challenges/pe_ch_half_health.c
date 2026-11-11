/* Challenge: Half health. Aya's HP can't go above half (or a quarter) of her max.
 *
 * A cap, not a new max: every field and battle frame her current HP is held at or below the cap,
 * in both places it lives (her battle combatant, curHP / maxHP, and the saved state, current_hp /
 * max_hp, as the Cheats mod's god mode keeps them). Her real max HP, saved with the game and raised
 * by level-ups, is never touched, so turning the challenge off simply stops the cap. Heals,
 * Preraise, Revive and auto-medicine work as usual up to the cap; the cap follows her max as she
 * levels. The game's own HP display still shows her true max (480/960).
 *
 * Runs after other mods' per-frame HP changes (god mode, refills: PE_PRIORITY_LATE), so the cap
 * has the last word; a cheat on with a challenge marks the run assisted anyway.
 */
#include "pe_challenges.h"
#include "pe_game.h"
#include "pe_mod_host.h"

#define CH_PRIORITY_AFTER_LATE (PE_PRIORITY_LATE + 20)

/* The cap for a max: 1/2 or 1/4 of it, rounded up, at least 1. */
static int pe_cap(int max) {
    const int div = pe_ch_level(CH_HALF_HEALTH) >= 2 ? 4 : 2;
    int cap = (max + div - 1) / div;
    return cap < 1 ? 1 : cap;
}

static void pe_half_health_apply(void) {
    Combatant *aya;
    if (!pe_ch_on(CH_HALF_HEALTH) || !pe_game_loaded()) return;
    if ((aya = pe_aya_core()) != NULL && aya->maxHP) {
        const int cap = pe_cap(aya->maxHP);
        if ((s16)aya->curHP > cap) aya->curHP = (u16)cap;
    }
    if (g_AyaSaveState.max_hp) {
        const int cap = pe_cap(g_AyaSaveState.max_hp);
        if (g_AyaSaveState.current_hp > cap) g_AyaSaveState.current_hp = (u16)cap;
    }
}

static int pe_half_health_tick(void *ctx, void *user) {
    (void)ctx; (void)user;
    pe_half_health_apply();
    return 0;
}

static void pe_half_health_register(const char *mod_id) {
    pe_hook_add(PE_HOOK_FRAME, pe_half_health_tick, NULL, CH_PRIORITY_AFTER_LATE, mod_id);
    pe_hook_add(PE_HOOK_BATTLE_UPDATE, pe_half_health_tick, NULL, CH_PRIORITY_AFTER_LATE, mod_id);
}

/* Level 2 is the harder one. */
static const char *const kHalfLevels[] = {"Off", "1/2 HP", "1/4 HP"};

const PeChallengeDef g_ch_half_health = {
    CH_HALF_HEALTH, "Half health", "Half health",
    "Aya's HP can't go above half, or a quarter, of her max. Heals stop there, her max still grows.",
    kHalfLevels, 3, 0, 0, pe_half_health_register, CH_CAT_AYA
};
