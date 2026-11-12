/* BP overhaul battle effects: Resistance, Fortitude and PE Mastery (numbers in bp_rules.c).
 *
 *   Resistance  PE_HOOK_ENEMY_EFFECT stage 1: after the armor's roll failed, a second roll at
 *               10% per rank ("Resisted Poison"). Own RNG: the game's rand() stream is untouched.
 *   Fortitude   stage 0 for Halve HP / HP to 1: negate (cancel) or soften (PE_HOOK_HP_LOSS
 *               source 4 then gets the softened loss). Charges refill each battle.
 *   Mastery     PE_HOOK_SPELL (pre-state) + PE_HOOK_SPELL_DONE (heal bonus, cost cut, riders);
 *               Gene Heal and Preraise riders on PE_HOOK_BATTLE_UPDATE; Energy Shot raises the
 *               hit's power bonus at PE_HOOK_ENEMY_HIT and restores it at _END (the Rebalance
 *               pattern; Rebalance skips PE attacks, so they never touch the same hit). */
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "pe_bp_battle.h"
#include "bp_rules.h"
#include "pe_game.h"
#include "pe_mod_host.h"
#include "pe1/battle.h"
#include "pe1/battle_ailment.h"
#include "pe1/battle_runtime.h"
#include "pe1/entity_animation.h"

#define MOD_ID "pe_bp"
enum { PE_ENERGY_SHOT_FLAG = 0x100000, PE_LIBERATE_FLAG = 0x80000, ENEMY_CONFUSED = 0x1000 };

static const BpRecord *(*s_record)(void);
static uint32_t s_rng;
static int s_in_battle, s_charges_used;
static struct { int active, target_hp; } s_soften;
static struct { int valid, slowed, confused; } s_pre;
static struct { BattleAction *action; u16 orig; int set; } s_shot;
static struct { int hp, flags, pe; } s_last;
extern int g_ActiveDrawSlot;
static int s_preraise_armed;

static unsigned roll(void) {   /* xorshift32, percent 0..99 */
    s_rng ^= s_rng << 13; s_rng ^= s_rng >> 17; s_rng ^= s_rng << 5;
    return s_rng % 100u;
}
static Combatant *aya(void) { return pe_aya_core(); }
static int rank_def(int t) { return s_record()->defense[t]; }
static int rank_line(int l) { return l < 0 ? 0 : s_record()->mastery[l]; }

static void restore_shot(void) {
    if (s_shot.set && s_shot.action) s_shot.action->powerBonus = s_shot.orig;
    s_shot.set = 0; s_shot.action = NULL;
}

/* ---- battle bookkeeping ---- */
/* PE_HOOK_FRAME also runs during battles (PE fights in the field), so a battle ends only when
 * the game is out of battle; otherwise Fortitude's charges and Preraise's memory would reset
 * every frame. */
static int on_field_frame(void *ctx, void *user) {
    (void)ctx; (void)user;
    if (!pe_in_battle()) { s_in_battle = 0; s_preraise_armed = 0; restore_shot(); }
    return 0;
}
static int on_battle_update(void *ctx, void *user) {
    Combatant *a = aya();
    (void)ctx; (void)user;
    restore_shot();
    if (!s_in_battle) {
        s_in_battle = 1; s_charges_used = 0; s_soften.active = 0;
        s_last.hp = a ? (s16)a->curHP : 0; s_last.flags = a ? a->stateFlags : 0; s_last.pe = a ? a->exp_or_acc : 0;
    }
    if (!a) return 0;
    /* Gene Heal ranks 2-3: a second HP per tick. Vanilla heals 1 HP (and its mirror) on frames with
     * draw slot 1 (combat.c); this heals on the other frames, so the rate doubles. */
    if ((a->stateFlags & AYA_GENE_HEAL) && rank_line(9) >= 2 && g_ActiveDrawSlot == 0 &&
        (s16)a->curHP > 0 && (s16)a->curHP < (s16)a->maxHP) {
        a->curHP += 1;
        a->hpMirror += 1;
    }
    /* Preraise ranks 2-3: the revive keeps half the PE (vanilla a quarter), rank 3 also 75% HP.
     * The revive is recognised by what it leaves after a Preraise that was on while Aya was alive
     * or down: the flag gone, HP at exactly half and PE cut to a quarter of the last frame's. */
    if (s_preraise_armed && !(a->stateFlags & AYA_PRERAISE) && rank_line(10) >= 2 &&
        (s16)a->curHP > 0 && (s16)a->curHP == ((s16)a->maxHP >> 1) &&
        a->exp_or_acc <= (s_last.pe >> 2) + 0x10000) {
        if (a->exp_or_acc < (s_last.pe >> 1)) a->exp_or_acc = s_last.pe >> 1;
        if (rank_line(10) >= 3) a->curHP = a->hpMirror = (u16)((s16)a->maxHP * 3 / 4);
        pe_toast("Preraise mastery");
        pe_mod_log(MOD_ID, "preraise rider: hp=%d pe=%d", (s16)a->curHP, a->exp_or_acc >> 16);
    }
    if ((s16)a->curHP > 0) s_preraise_armed = (a->stateFlags & AYA_PRERAISE) != 0;   /* kept while she is down */
    else if (a->stateFlags & AYA_PRERAISE) s_preraise_armed = 1;
    s_last.hp = (s16)a->curHP; s_last.flags = a->stateFlags; s_last.pe = a->exp_or_acc;
    return 0;
}

/* ---- Resistance and Fortitude ---- */
static int track_of(int effect) {
    switch (effect) {
    case ENEMY_EFFECT_POISON: return 0;
    case ENEMY_EFFECT_CONFUSION: return 1;
    case ENEMY_EFFECT_DARKNESS: return 2;
    case ENEMY_EFFECT_STIFFNESS_LOCK: case ENEMY_EFFECT_STIFFNESS_SLOW: return 3;
    default: return -1;
    }
}
static int on_enemy_effect(void *ctx, void *user) {
    PeHookEnemyEffect *e = ctx;
    Combatant *a = aya();
    (void)user;
    if (!a || e->target != a) return 0;
    if (e->stage == 1) {
        int t = track_of(e->effect), rank = t >= 0 ? rank_def(t) : 0;
        if (rank && roll() < (unsigned)(10 * rank)) {
            char msg[32];
            *e->cancel = 1;
            snprintf(msg, sizeof msg, "Resisted %s", bp_defense_def(t)->name);
            pe_toast(msg);
            pe_mod_log(MOD_ID, "resist: %s rank %d", bp_defense_def(t)->name, rank);
        }
        return 0;
    }
    s_soften.active = 0;
    if (e->effect == BP_FX_HALVE_HP || e->effect == BP_FX_HP_TO_1) {
        int hp = (s16)a->curHP;
        BpFortitudePlan p = bp_fortitude_plan(e->effect, hp, (a->stateFlags & AYA_BARRIER) != 0,
                                              rank_def(BP_FORTITUDE), s_record()->fortitude_mode, s_charges_used);
        if (!p.use) return 0;
        ++s_charges_used;
        if (p.negate) *e->cancel = 1;
        else { s_soften.active = 1; s_soften.target_hp = p.result_hp; }
        pe_toast("Fortitude!");
        pe_mod_log(MOD_ID, "fortitude: effect=%d hp=%d -> %d (vanilla %d)%s charges_used=%d", e->effect, hp,
                   p.result_hp, p.vanilla_hp, p.negate ? " negated" : "", s_charges_used);
    }
    return 0;
}
static int on_hp_loss(void *ctx, void *user) {
    PeHookHpLoss *q = ctx;
    (void)user;
    if (!s_soften.active || q->source != 4 || q->actor != aya()) return 0;
    s_soften.active = 0;
    *q->loss = q->current_hp > s_soften.target_hp ? q->current_hp - s_soften.target_hp : 0;
    return 0;
}

/* ---- PE Mastery ---- */
static int neutral_pair(BattleEntity *ent, int shift) {
    return ent && ent->core ? (int)((((EnemyCombatant *)ent->core)->statusFlags2 >> shift) & 3) : -1;
}
static int on_spell(void *ctx, void *user) {
    PeHookSpell *s = ctx;
    (void)user;
    s_pre.valid = s->target && s->target->core;
    if (s_pre.valid) {
        s_pre.slowed = (((EnemyCombatant *)s->target->core)->coreFlags & 1) != 0;
        s_pre.confused = (s->target->entityFlags & ENEMY_CONFUSED) != 0;
    }
    return 0;
}
static int on_spell_done(void *ctx, void *user) {
    PeHookSpellDone *s = ctx;
    Combatant *a = s->actor;
    int line = bp_line_for_power((int)s->spell), rank = rank_line(line), cut;
    (void)user;
    if (!a || !rank) return 0;
    cut = bp_line_cut(line, rank);
    switch (line) {
    case 0: {   /* Heal 1-3: more healing, whatever the heal was (Rebalance included) */
        int healed = (s16)a->curHP - s->hp_before;
        if (healed > 0) a->curHP += healed * bp_heal_bonus(rank) / 100;
        break;
    }
    case 1:     /* Detox ranks 2-3: one-use Poison immunity once cured */
        if (rank >= 2 && (a->stateFlags & AYA_POISON_MASK) == 0) a->stateFlags |= AYA_POISON_IMMUNE;
        break;
    case 2:     /* Medic ranks 2-3: heal 10% of max HP */
        if (rank >= 2) a->curHP += (s16)a->maxHP / 10;
        break;
    case 3:     /* Barrier: 1/3 of max PE was taken; give back down to 1/4, 1/5, 1/6 */
        a->exp_or_acc += a->maxAtk / 3 - a->maxAtk / (3 + rank);
        break;
    case 6: case 8: {   /* Slow / Confuse: a second roll against neutral enemies */
        int slow = line == 6, pair = neutral_pair(s->target, slow ? 8 : 10);
        if (pair != 0 || !s_pre.valid) break;
        if (slow ? (s_pre.slowed || (((EnemyCombatant *)s->target->core)->coreFlags & 1))
                 : (s_pre.confused || (s->target->entityFlags & ENEMY_CONFUSED))) break;
        if (roll() >= (unsigned)bp_neutral_reroll(rank)) break;
        if (slow) ((EnemyCombatant *)s->target->core)->coreFlags |= 1;
        else {   /* as the game confuses an enemy (attack.c) */
            EnemyCombatant *en = s->target->core;
            en->coreFlags = (en->coreFlags & ~0xEu) | ((((roll() % 3) + 3) & 7) * 2);
            Entity_SetActionMode(s->target, 4);
            s->target->entityFlags |= ENEMY_CONFUSED;
        }
        pe_mod_log(MOD_ID, "%s mastery second roll landed (rank %d)", slow ? "slow" : "confuse", rank);
        break;
    }
    case 7:     /* Haste: longer timer (it counts down) */
        g_ActorEffectFlag100Timer = (u16)(g_ActorEffectFlag100Timer * (100 + bp_haste_bonus(rank)) / 100);
        break;
    default: break;
    }
    if (cut && s->drain) *s->drain = (int)((long long)*s->drain * (100 - cut) / 100);
    return 0;
}
static int on_enemy_hit(void *ctx, void *user) {
    Combatant *a = g_ActiveActor;
    int rank = rank_line(4), pct, base;
    (void)ctx; (void)user;
    restore_shot();
    if (!rank || !a || a != aya() || !a->action) return 0;
    if (!(a->stateFlags & PE_ENERGY_SHOT_FLAG) || (a->stateFlags & PE_LIBERATE_FLAG)) return 0;
    pct = bp_energy_shot_bonus(rank, a->exp_or_acc > a->maxAtk / 2);
    base = (u16)a->stat1E / 5 + (s16)a->action->powerBonus;
    s_shot.action = a->action; s_shot.orig = a->action->powerBonus; s_shot.set = 1;
    a->action->powerBonus = (u16)((s16)a->action->powerBonus + base * pct / 100);
    return 0;
}
static int on_enemy_hit_end(void *ctx, void *user) { (void)ctx; (void)user; restore_shot(); return 0; }

void pe_bp_battle_init(const char *mod_id, const BpRecord *(*record)(void)) {
    s_record = record;
    s_rng = (uint32_t)time(NULL) ^ 0x9E3779B9u;
    if (!s_rng) s_rng = 1;
    pe_hook_add(PE_HOOK_FRAME, on_field_frame, NULL, PE_PRIORITY_NORMAL, mod_id);
    pe_hook_add(PE_HOOK_BATTLE_UPDATE, on_battle_update, NULL, PE_PRIORITY_NORMAL, mod_id);
    pe_hook_add(PE_HOOK_ENEMY_EFFECT, on_enemy_effect, NULL, PE_PRIORITY_NORMAL, mod_id);
    pe_hook_add(PE_HOOK_HP_LOSS, on_hp_loss, NULL, PE_PRIORITY_NORMAL, mod_id);
    pe_hook_add(PE_HOOK_SPELL, on_spell, NULL, PE_PRIORITY_NORMAL, mod_id);
    pe_hook_add(PE_HOOK_SPELL_DONE, on_spell_done, NULL, PE_PRIORITY_NORMAL, mod_id);
    pe_hook_add(PE_HOOK_ENEMY_HIT, on_enemy_hit, NULL, PE_PRIORITY_NORMAL, mod_id);
    pe_hook_add(PE_HOOK_ENEMY_HIT_END, on_enemy_hit_end, NULL, PE_PRIORITY_NORMAL, mod_id);
}
