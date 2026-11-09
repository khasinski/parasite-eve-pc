/* Challenges: damage. The shared scale for the damage Aya deals, and the two challenges that only
 * change damage numbers, Glass cannon and Tough crowd.
 *
 *   Damage Aya deals   Battle_StepEnemyMovement applies every hit on an enemy: the HP before
 *                      (PE_HOOK_ENEMY_HIT) and after (PE_HOOK_ENEMY_HIT_END) give the hit's damage.
 *                      Every challenge that changes it registers a modifier
 *                      (pe_ch_damage_modifier): each multiplies the hit's num / den, read at the
 *                      hit's start while the attacker (g_ActiveActor) and its action are current.
 *                      The product scales the damage; at least 1 lands, unless a modifier made it 0
 *                      (no damage at all). Enemy HP itself is never changed, so EXP, drops and the
 *                      scripted boss endings stay the game's own. Ordered after Rebalance's hit
 *                      changes (NORMAL) and before the Bestiary's kill check (LATE).
 *   Damage Aya takes   Glass cannon doubles her HP loss from hits and contact (PE_HOOK_HP_LOSS
 *                      sources 1 and 2; never more than she has left). Poison and the percentage
 *                      attacks keep their own rules.
 *
 *   Glass cannon  x1.5 dealt.   Tough crowd  x2/3 or x1/2 dealt (as if enemies had 1.5x / 2x HP).
 *   Club only (pe_ch_club_only.c) is a modifier too.
 */
#include <stddef.h>

#include "pe_challenges.h"
#include "pe_game.h"
#include "pe_mod_host.h"

#define CH_PRIORITY_BEFORE_LATE (PE_PRIORITY_LATE - 10)
#define CH_MODIFIERS 8

static PeChDealtModifier s_modifiers[CH_MODIFIERS];
static int s_modifier_count;
static const BattleEntity *s_hit_entity;
static s32 s_hit_before;
static int s_hit_num, s_hit_den;

/* ---- the shared hooks --------------------------------------------------------------------- */

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

static int pe_damage_hit(void *ctx, void *user) {
    BattleEntity *entity = ctx;
    int num = 1, den = 1;
    (void)user;
    s_hit_entity = NULL;
    if (!entity || !entity->core) return 0;
    for (int i = 0; i < s_modifier_count; ++i) s_modifiers[i](entity, &num, &den);
    if (num == den || den <= 0) return 0;
    s_hit_entity = entity;
    s_hit_before = ((EnemyCombatant *)entity->core)->hpAlive;
    s_hit_num = num;
    s_hit_den = den;
    return 0;
}

static int pe_damage_hit_end(void *ctx, void *user) {
    BattleEntity *entity = ctx;
    EnemyCombatant *enemy;
    long long dealt, scaled;
    (void)user;
    if (!entity || entity != s_hit_entity || !entity->core) { s_hit_entity = NULL; return 0; }
    s_hit_entity = NULL;
    enemy = (EnemyCombatant *)entity->core;
    dealt = (long long)s_hit_before - enemy->hpAlive;
    if (dealt <= 0) return 0;
    scaled = s_hit_num ? (dealt * s_hit_num + s_hit_den / 2) / s_hit_den : 0;
    if (scaled < 1 && s_hit_num) scaled = 1;
    enemy->hpAlive = (s32)((long long)s_hit_before - scaled);
    return 0;
}

static int pe_damage_forget(void *ctx, void *user) {
    (void)ctx; (void)user;
    s_hit_entity = NULL;
    return 0;
}

static void pe_damage_hooks(const char *mod_id) {
    static int done;
    if (done) return;
    done = 1;
    pe_hook_add(PE_HOOK_HP_LOSS, pe_damage_taken, NULL, CH_PRIORITY_BEFORE_LATE, mod_id);
    pe_hook_add(PE_HOOK_ENEMY_HIT, pe_damage_hit, NULL, PE_PRIORITY_LATE, mod_id);
    pe_hook_add(PE_HOOK_ENEMY_HIT_END, pe_damage_hit_end, NULL, CH_PRIORITY_BEFORE_LATE, mod_id);
    pe_hook_add(PE_HOOK_SCENE_START, pe_damage_forget, NULL, PE_PRIORITY_NORMAL, mod_id);
    pe_hook_add(PE_HOOK_SAVE_READ, pe_damage_forget, NULL, PE_PRIORITY_NORMAL, mod_id);
}

void pe_ch_damage_modifier(PeChDealtModifier fn, const char *mod_id) {
    pe_damage_hooks(mod_id);
    for (int i = 0; i < s_modifier_count; ++i) if (s_modifiers[i] == fn) return;
    if (fn && s_modifier_count < CH_MODIFIERS) s_modifiers[s_modifier_count++] = fn;
}

/* ---- Glass cannon, Tough crowd ------------------------------------------------------------ */

static void pe_glass_dealt(const BattleEntity *target, int *num, int *den) {
    (void)target;
    if (!pe_ch_on(CH_GLASS)) return;
    *num *= 3; *den *= 2;                                         /* x1.5 */
}

static void pe_tough_dealt(const BattleEntity *target, int *num, int *den) {
    (void)target;
    if (!pe_ch_on(CH_TOUGH)) return;
    if (pe_ch_level(CH_TOUGH) >= 2) *den *= 2;                    /* /2 */
    else { *num *= 2; *den *= 3; }                                /* /1.5 */
}

static void pe_glass_register(const char *mod_id) { pe_ch_damage_modifier(pe_glass_dealt, mod_id); }
static void pe_tough_register(const char *mod_id) { pe_ch_damage_modifier(pe_tough_dealt, mod_id); }

static const char *const kToughLevels[] = {"Off", "1.5x HP", "2x HP"};

const PeChallengeDef g_ch_glass = {
    CH_GLASS, "Glass cannon", "Glass cannon",
    "Aya takes double damage from hits and contact, and deals 1.5x.",
    NULL, 0, 0, 0, pe_glass_register, CH_CAT_AYA
};

const PeChallengeDef g_ch_tough = {
    CH_TOUGH, "Tough crowd", "Tough crowd",
    "Enemies take less damage, as if they had 1.5x or 2x HP. EXP and drops unchanged.",
    kToughLevels, 3, 0, 0, pe_tough_register, CH_CAT_ENEMIES
};
