/* Challenge: Club only. Only the Club hurts enemies, and it hits harder to make up for one hit a
 * turn at close range: x2 (Club 2x) or x1.5 (Club 1.5x, the harder level).
 *
 *   The rule    a dealt-damage modifier (pe_ch_damage.c): a weapon hit (not a Parasite Energy
 *               power: g_ActiveActor->stateFlags & 0x180000 clear) with a Club equipped (items
 *               0x3F-0x43, Club 1-5) is scaled up; with a gun equipped it deals no damage.
 *               Parasite Energy is untouched (No PE is its own challenge).
 *   Why so      the game has no "refuse this equip" hook (the equipment guard covers removing and
 *               Tools only), so guns aren't blocked: they just do nothing. The Weapon tab stays,
 *               so a gun brought into a fight can be swapped for the Club there.
 *   Telling     with a gun equipped, a short message once per room in the field and once per
 *               battle on the first wasted shot. Aya starts every New Game with Club 1.
 */
#include "pe_challenges.h"
#include "pe_game.h"
#include "pe_mod_host.h"
#include "pe1/scene_dispatch.h"

#define CH_CLUB_FIRST     0x3F      /* Club 1 */
#define CH_CLUB_LAST      0x43      /* Club 5 */
#define CH_ACTOR_SPECIAL  0x180000u /* g_ActiveActor->stateFlags: a PE power, not a weapon */

static int s_told_battle, s_told_room = -1;

static int pe_club_equipped(void) {
    const ItemDataRecord *gun = pe_equipped(0);
    return gun && gun->itemId >= CH_CLUB_FIRST && gun->itemId <= CH_CLUB_LAST;
}

static void pe_club_dealt(const BattleEntity *target, int *num, int *den) {
    (void)target;
    if (!pe_ch_on(CH_CLUB_ONLY) || !g_ActiveActor || (g_ActiveActor->stateFlags & CH_ACTOR_SPECIAL)) return;
    if (!pe_club_equipped()) {
        *num = 0;                                             /* a gun: no damage */
        if (!s_told_battle) { pe_toast("Club only: guns do no damage. Switch to your Club"); s_told_battle = 1; }
        return;
    }
    if (pe_ch_level(CH_CLUB_ONLY) >= 2) { *num *= 3; *den *= 2; }   /* Club 1.5x */
    else *num *= 2;                                                 /* Club 2x */
}

/* In the field: a reminder once per room while a gun is equipped. */
static int pe_club_frame(void *ctx, void *user) {
    const int room = (int)g_SceneDispatchToken;
    (void)ctx; (void)user;
    if (!pe_in_battle()) s_told_battle = 0;
    if (!pe_ch_on(CH_CLUB_ONLY) || !pe_free_field() || pe_club_equipped() || room == s_told_room) return 0;
    s_told_room = room;
    pe_toast("Club only: equip a Club, guns do no damage");
    return 0;
}

static int pe_club_reset(void *ctx, void *user) {
    (void)ctx; (void)user;
    s_told_battle = 0;
    s_told_room = -1;
    return 0;
}

static void pe_club_register(const char *mod_id) {
    pe_ch_damage_modifier(pe_club_dealt, mod_id);
    pe_hook_add(PE_HOOK_FRAME, pe_club_frame, NULL, PE_PRIORITY_NORMAL, mod_id);
    pe_hook_add(PE_HOOK_SAVE_READ, pe_club_reset, NULL, PE_PRIORITY_NORMAL, mod_id);
    pe_hook_add(PE_HOOK_NEW_GAME, pe_club_reset, NULL, PE_PRIORITY_NORMAL, mod_id);
}

/* Level 2 is the harder one, so going from 1.5x to 2x counts as lowering the challenge. */
static const char *const kClubLevels[] = {"Off", "Club 2x", "Club 1.5x"};

const PeChallengeDef g_ch_club_only = {
    CH_CLUB_ONLY, "Club only", "Club only",
    "Only the Club hurts enemies, and it hits 2x (or 1.5x) harder. Guns do no damage; PE works.",
    kClubLevels, 3, 0, 0, pe_club_register
};
