/* Challenge: Blood price. Every Parasite Energy power also costs Aya HP: its PE cost (x1 or x2),
 * in battle and from the field menu. It never takes her last HP point.
 *
 *   The cost   the game's own Inv_GetSlotHighlightState(power, 0): the table cost after every
 *              mod's PE_HOOK_POWER_COST (Rebalance, assists) and the armor discount, so the HP
 *              cost follows whatever the PE cost is. Powers whose cost isn't a table cost (the game
 *              answers 0 for them) cost no HP.
 *   Once a cast  PE_HOOK_SPELL (Battle_ApplySpellEffect) runs once per target in battle, so a power
 *              that hits several enemies arrives as a burst of calls in the same moment. In battle
 *              only the first call of a burst pays (calls within CH_CAST_WINDOW_MS of the last paid
 *              one are the same cast; Aya can't cast twice that fast, the gauge has to refill). The
 *              field menu calls it once per cast, so every call pays there.
 *   When       at the hook, before the power's effect: a heal heals after paying.
 */
#include <SDL3/SDL.h>

#include "pe_challenges.h"
#include "pe_game.h"
#include "pe_mod_host.h"
#include "pe1/inventory.h"

#define CH_CAST_WINDOW_MS 1500u
#define CH_POWERS 20

static uint64_t s_last_paid_ms;

static int pe_blood_price_spell(void *ctx, void *user) {
    const PeHookSpell *spell = ctx;
    Combatant *aya;
    int cost, hp, pay;
    (void)user;
    if (!pe_ch_on(CH_BLOOD_PRICE) || !spell || spell->spell >= CH_POWERS || !pe_game_loaded()) return 0;
    if (pe_in_battle()) {
        const uint64_t now = SDL_GetTicks();
        if (s_last_paid_ms && now - s_last_paid_ms < CH_CAST_WINDOW_MS) return 0;
        s_last_paid_ms = now ? now : 1;
    }
    if (!(aya = pe_aya_core())) return 0;
    cost = Inv_GetSlotHighlightState((int)spell->spell, 0);
    if (cost <= 0) return 0;
    hp = (s16)aya->curHP;
    pay = cost * pe_ch_level(CH_BLOOD_PRICE);
    if (pay > hp - 1) pay = hp - 1;
    if (pay <= 0) return 0;
    aya->curHP = (u16)(hp - pay);
    pe_log("challenge_blood_price", "power=%s cost=%d hp=%d->%d", pe_power_tag((int)spell->spell), cost, hp,
           hp - pay);
    return 0;
}

static int pe_blood_price_forget(void *ctx, void *user) {
    (void)ctx; (void)user;
    s_last_paid_ms = 0;
    return 0;
}

static void pe_blood_price_register(const char *mod_id) {
    pe_hook_add(PE_HOOK_SPELL, pe_blood_price_spell, NULL, PE_PRIORITY_LATE, mod_id);
    pe_hook_add(PE_HOOK_SAVE_READ, pe_blood_price_forget, NULL, PE_PRIORITY_NORMAL, mod_id);
    pe_hook_add(PE_HOOK_NEW_GAME, pe_blood_price_forget, NULL, PE_PRIORITY_NORMAL, mod_id);
}

static const char *const kBloodLevels[] = {"Off", "1 HP per PE", "2 HP per PE"};

const PeChallengeDef g_ch_blood_price = {
    CH_BLOOD_PRICE, "Blood price", "Blood price",
    "Parasite Energy powers also cost Aya HP: their PE cost, once or twice. Never her last HP.",
    kBloodLevels, 3, 0, 0, pe_blood_price_register
};
