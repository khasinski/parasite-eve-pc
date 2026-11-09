/* Challenges: Glass cannon and Tough crowd.
 *
 *   Damage Aya takes   Glass cannon doubles her HP loss from hits and contact (PE_HOOK_HP_LOSS
 *                      sources 1 and 2; never more than she has left). Poison and the percentage
 *                      attacks keep their own rules.
 *   Damage Aya deals   Battle_StepEnemyMovement applies every hit on an enemy: the HP before
 *                      (PE_HOOK_ENEMY_HIT) and after (PE_HOOK_ENEMY_HIT_END) give the hit's damage,
 *                      which is scaled: x1.5 for Glass cannon, /1.5 or /2 for Tough crowd (as if
 *                      enemies had that much more HP). Enemy HP itself is never changed, so EXP, drops
 *                      and the scripted boss endings stay the game's own. At least 1 damage lands.
 */
#include "pe_challenges.h"
#include "pe_game.h"
#include "pe_mod_host.h"

/* After Rebalance's hit changes (NORMAL), before the Bestiary's kill check (LATE). */
#define CH_PRIORITY_BEFORE_LATE (PE_PRIORITY_LATE - 10)

static const BattleEntity *s_hit_entity;
static s32 s_hit_before;

static int pe_damage_taken(void *ctx, void *user) {
    PeHookHpLoss *q = ctx;
    int loss;
    (void)user;
    if (!pe_ch_on(CH_GLASS) || !q || !q->loss || *q->loss <= 0) return 0;
    if (q->source != 1 && q->source != 2) return 0;
    if (!pe_in_battle() || q->actor != (const void *)pe_aya_core()) return 0;
    loss = *q->loss * 2;
    if (q->current_hp > 0 && loss > q->current_hp) loss = q->current_hp;
    if (loss > *q->loss) *q->loss = loss;
    return 0;
}

/* The scale of the damage Aya deals, as num / den. */
static void pe_dealt_scale(int *num, int *den) {
    int n = 2, d = 2;
    if (pe_ch_on(CH_GLASS)) n = 3;                               /* x1.5 */
    if (pe_ch_on(CH_TOUGH)) d = g_ch_run.tough >= 2 ? 4 : 3;     /* /2 or /1.5 */
    *num = n;
    *den = d;
}

static int pe_damage_hit(void *ctx, void *user) {
    BattleEntity *entity = ctx;
    (void)user;
    s_hit_entity = NULL;
    if (!entity || !entity->core) return 0;
    if (!pe_ch_on(CH_GLASS) && !pe_ch_on(CH_TOUGH)) return 0;
    s_hit_entity = entity;
    s_hit_before = ((EnemyCombatant *)entity->core)->hpAlive;
    return 0;
}

static int pe_damage_hit_end(void *ctx, void *user) {
    BattleEntity *entity = ctx;
    EnemyCombatant *enemy;
    int num, den;
    long long dealt, scaled;
    (void)user;
    if (!entity || entity != s_hit_entity || !entity->core) { s_hit_entity = NULL; return 0; }
    s_hit_entity = NULL;
    enemy = (EnemyCombatant *)entity->core;
    dealt = (long long)s_hit_before - enemy->hpAlive;
    if (dealt <= 0) return 0;
    pe_dealt_scale(&num, &den);
    if (num == den) return 0;
    scaled = (dealt * num + den / 2) / den;
    if (scaled < 1) scaled = 1;
    enemy->hpAlive = (s32)((long long)s_hit_before - scaled);
    return 0;
}

static int pe_damage_forget(void *ctx, void *user) {
    (void)ctx; (void)user;
    s_hit_entity = NULL;
    return 0;
}

void pe_ch_damage_register(const char *mod_id) {
    pe_hook_add(PE_HOOK_HP_LOSS, pe_damage_taken, NULL, CH_PRIORITY_BEFORE_LATE, mod_id);
    pe_hook_add(PE_HOOK_ENEMY_HIT, pe_damage_hit, NULL, PE_PRIORITY_LATE, mod_id);
    pe_hook_add(PE_HOOK_ENEMY_HIT_END, pe_damage_hit_end, NULL, CH_PRIORITY_BEFORE_LATE, mod_id);
    pe_hook_add(PE_HOOK_SCENE_START, pe_damage_forget, NULL, PE_PRIORITY_NORMAL, mod_id);
    pe_hook_add(PE_HOOK_SAVE_READ, pe_damage_forget, NULL, PE_PRIORITY_NORMAL, mod_id);
}
