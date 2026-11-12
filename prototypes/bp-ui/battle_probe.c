/* Test-only battle probe (preview build, PE_BP_TEST_BATTLE=1). Once a real battle has run for a
 * while, it calls the game's own Battle_ApplyEnemyAttack / Battle_ApplySpellEffect on Aya with
 * chosen ranks, so the real hook lines, the HP-loss path and pe_bp's handlers run in a live
 * battle. Each case snapshots Aya, the haste timer, the scan panel and the ranks, and restores
 * them afterwards. Prints BP_BATTLE_CASE / BP_BATTLE_DONE lines for the harness. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bp_rules.h"
#include "pe_game.h"
#include "pe_mod_host.h"
#include "pe1/battle.h"
#include "pe1/battle_ailment.h"
#include "pe1/battle_runtime.h"
#include "pe1/save_slot_metadata.h"

void pe_bp_test_set_record(const BpRecord *rec);
const BpRecord *pe_bp_test_record(void);

static int s_frames, s_done, s_pass, s_fail;

#include <stdarg.h>
/* Results go to stdout and to bp_battle_results.txt in the working directory (harnesses that
 * only read the control JSON from stdout read the file). */
static void emit(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void emit(const char *fmt, ...) {
    char line[320];
    FILE *f;
    va_list ap;
    va_start(ap, fmt); vsnprintf(line, sizeof line, fmt, ap); va_end(ap);
    fputs(line, stdout); fflush(stdout);
    if ((f = fopen("bp_battle_results.txt", "a")) != NULL) { fputs(line, f); fclose(f); }
}
static void report(const char *name, int ok, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static void report(const char *name, int ok, const char *fmt, ...) {
    char detail[200];
    va_list ap;
    va_start(ap, fmt); vsnprintf(detail, sizeof detail, fmt, ap); va_end(ap);
    emit("BP_BATTLE_CASE name=%s pass=%d %s\n", name, ok, detail);
    ok ? ++s_pass : ++s_fail;
}

typedef struct Saved {
    Combatant aya, *active;
    u32 armor_flags;
    u16 haste_timer;
    SavePromptBlock scan;
    BpRecord rec;
} Saved;
static void save_state(Saved *s, Combatant *a) {
    s->aya = *a; s->active = g_ActiveActor; s->armor_flags = a->attributes->effectFlags;
    s->haste_timer = g_ActorEffectFlag100Timer; s->scan = g_BattleScanPanel; s->rec = *pe_bp_test_record();
}
static void restore_state(const Saved *s, Combatant *a) {
    *a = s->aya; g_ActiveActor = s->active; a->attributes->effectFlags = s->armor_flags;
    g_ActorEffectFlag100Timer = s->haste_timer; g_BattleScanPanel = s->scan; pe_bp_test_set_record(&s->rec);
}
static void ranks(const BpRecord *base, int def, int def_rank, int line, int line_rank) {
    BpRecord r = *base;
    memset(r.defense, 0, sizeof r.defense); memset(r.mastery, 0, sizeof r.mastery);
    if (def >= 0) r.defense[def] = (uint8_t)def_rank;
    if (line >= 0) r.mastery[line] = (uint8_t)line_rank;
    r.fortitude_mode = BP_FORT_BOTH;
    pe_bp_test_set_record(&r);
}

/* ---- Resistance: N poison attacks, count how many the second roll stopped ---- */
static int poison_trials(Combatant *a, int n) {
    EnemyCombatant ent;
    EnemyActionEffect fx;
    int landed = 0;
    memset(&ent, 0, sizeof ent); memset(&fx, 0, sizeof fx);
    fx.effectType = ENEMY_EFFECT_POISON; ent.effect = &fx; ent.effectLevel = 1; ent.effectDuration = 60;
    for (int i = 0; i < n; ++i) {
        a->stateFlags &= ~AYA_POISON_MASK;
        g_ActiveActor = a;
        Battle_ApplyEnemyAttack(&ent);
        landed += (a->stateFlags & AYA_POISON_MASK) == AYA_POISON;
    }
    return landed;
}
static void resistance(Combatant *a, const BpRecord *base) {
    enum { N = 300 };
    Saved s;
    int landed;
    save_state(&s, a);
    a->attributes->effectFlags &= ~EFFECT_RESIST_POISON;
    ranks(base, 0, 0, -1, 0);
    landed = poison_trials(a, N);
    report("resist_rank0_no_armor", landed == N, "landed=%d/%d expected=all", landed, N);
    ranks(base, 0, 3, -1, 0);
    landed = poison_trials(a, N);   /* 30% resisted: 90 of 300, +-4 sd */
    report("resist_rank3_no_armor", N - landed >= 53 && N - landed <= 127, "resisted=%d/%d expected~90 (30%%)", N - landed, N);
    a->attributes->effectFlags |= EFFECT_RESIST_POISON;
    landed = poison_trials(a, N);   /* 1-(0.3)(0.7) = 79%: 237 of 300 */
    report("resist_rank3_with_armor", N - landed >= 209 && N - landed <= 265, "resisted=%d/%d expected~237 (79%%)", N - landed, N);
    restore_state(&s, a);
}

/* ---- Fortitude rank 3: negate (buffs kept), then soften to 1/4, then vanilla ---- */
static void fortitude(Combatant *a, const BpRecord *base) {
    EnemyCombatant ent;
    EnemyActionEffect fx;
    Saved s;
    int hp0;
    save_state(&s, a);
    memset(&ent, 0, sizeof ent); memset(&fx, 0, sizeof fx);
    fx.effectType = ENEMY_EFFECT_REDUCE_HP_TO_1; ent.effect = &fx;
    ranks(base, BP_FORTITUDE, 3, -1, 0);
    hp0 = (s16)a->maxHP < 100 ? (s16)a->maxHP : 100;
    a->stateFlags = (a->stateFlags & ~(AYA_BARRIER | AYA_PRERAISE)) | AYA_HASTE;
    a->curHP = (s16)hp0; g_ActiveActor = a;
    Battle_ApplyEnemyAttack(&ent);
    report("fortitude_negate", (s16)a->curHP == hp0 && (a->stateFlags & AYA_HASTE), "hp=%d expected=%d haste=%d",
           (s16)a->curHP, hp0, !!(a->stateFlags & AYA_HASTE));
    a->curHP = (s16)hp0; g_ActiveActor = a;
    Battle_ApplyEnemyAttack(&ent);
    report("fortitude_soften", (s16)a->curHP == (hp0 / 4 > 1 ? hp0 / 4 : 1) && !(a->stateFlags & AYA_HASTE),
           "hp=%d expected=%d haste=%d", (s16)a->curHP, hp0 / 4 > 1 ? hp0 / 4 : 1, !!(a->stateFlags & AYA_HASTE));
    a->curHP = (s16)hp0; g_ActiveActor = a;
    Battle_ApplyEnemyAttack(&ent);
    report("fortitude_spent", (s16)a->curHP == 1, "hp=%d expected=1 (charges used up)", (s16)a->curHP);
    restore_state(&s, a);
}

/* ---- PE Mastery through the real spell code ---- */
static int drain_of(Combatant *a, int raw) {   /* what the game subtracts for a 16.16 cost */
    return (a->attributes->effectFlags & 0x200) ? raw * 2 / 3 : raw;
}
static void cast(Combatant *a, unsigned spell) { g_ActiveActor = a; Battle_ApplySpellEffect(spell, NULL); }
static void mastery(Combatant *a, const BpRecord *base) {
    Saved s;
    int before, spent, want, hp;
    save_state(&s, a);

    /* Heal 1: rank 0 vanilla, rank 2 = +15% healing and 15% cheaper. */
    ranks(base, -1, 0, 0, 0);
    a->curHP = 10; a->exp_or_acc = a->maxAtk; before = a->exp_or_acc;
    cast(a, 0); spent = before - a->exp_or_acc; hp = (s16)a->curHP;
    report("heal_rank0", hp == 40 && spent == drain_of(a, 0x3C0000), "hp=%d expected=40 pe=%d expected=%d", hp, spent >> 16, drain_of(a, 0x3C0000) >> 16);
    ranks(base, -1, 0, 0, 2);
    a->curHP = 10; a->exp_or_acc = a->maxAtk; before = a->exp_or_acc;
    cast(a, 0); spent = before - a->exp_or_acc; hp = (s16)a->curHP;
    want = drain_of(a, (int)((long long)0x3C0000 * 85 / 100));
    report("heal_rank2", hp == 44 && spent == want, "hp=%d expected=44 pe=%d.%04X expected=%d.%04X", hp, spent >> 16, spent & 0xFFFF, want >> 16, want & 0xFFFF);
    restore_state(&s, a);

    /* Haste rank 2: timer 450 -> 675, cost unchanged. */
    ranks(base, -1, 0, 7, 2);
    a->exp_or_acc = a->maxAtk; before = a->exp_or_acc;
    cast(a, 9); spent = before - a->exp_or_acc;
    report("haste_rank2", g_ActorEffectFlag100Timer == 675 && spent == drain_of(a, 0xC80000),
           "timer=%u expected=675 pe=%d expected=%d", g_ActorEffectFlag100Timer, spent >> 16, drain_of(a, 0xC80000) >> 16);
    restore_state(&s, a);

    /* Barrier rank 3: costs 1/6 of max PE instead of 1/3. */
    ranks(base, -1, 0, 3, 3);
    a->exp_or_acc = a->maxAtk; before = a->exp_or_acc;
    cast(a, 5); spent = before - a->exp_or_acc;
    want = a->maxAtk / 3 - (a->maxAtk / 3 - a->maxAtk / 6);
    report("barrier_rank3", spent == want && (a->stateFlags & AYA_BARRIER), "pe=%d expected=%d (max %d)", spent >> 16, want >> 16, a->maxAtk >> 16);
    restore_state(&s, a);

    /* Detox rank 2: cures, leaves one-use Poison immunity, 25% cheaper. */
    ranks(base, -1, 0, 1, 2);
    a->stateFlags = (a->stateFlags & ~AYA_POISON_MASK) | AYA_POISON;
    a->exp_or_acc = a->maxAtk; before = a->exp_or_acc;
    cast(a, 3); spent = before - a->exp_or_acc;
    want = drain_of(a, (int)((long long)0x640000 * 75 / 100));
    report("detox_rank2", (a->stateFlags & AYA_POISON_MASK) == AYA_POISON_IMMUNE && spent == want,
           "poison_state=%d expected=%d pe=%d expected=%d", (int)(a->stateFlags & AYA_POISON_MASK), AYA_POISON_IMMUNE, spent >> 16, want >> 16);
    restore_state(&s, a);

    /* Medic rank 2: heals 10% of max HP, 15% cheaper. */
    ranks(base, -1, 0, 2, 2);
    a->curHP = 10; a->exp_or_acc = a->maxAtk; before = a->exp_or_acc;
    cast(a, 4); spent = before - a->exp_or_acc; hp = (s16)a->curHP;
    want = drain_of(a, (int)((long long)0x2580000 * 85 / 100));
    report("medic_rank2", hp == 10 + (s16)a->maxHP / 10 && spent == want, "hp=%d expected=%d pe=%d expected=%d",
           hp, 10 + (s16)a->maxHP / 10, spent >> 16, want >> 16);
    restore_state(&s, a);

    /* Scan: free. */
    ranks(base, -1, 0, 5, 1);
    a->exp_or_acc = a->maxAtk; before = a->exp_or_acc;
    cast(a, 7); spent = before - a->exp_or_acc;
    report("scan_rank1", spent == 0, "pe=%d expected=0", spent >> 16);
    restore_state(&s, a);

    /* Energy Shot rank 3 at full PE: the hit's power bonus +30% of the attack term, restored. */
    if (a->action) {
        int pb0 = (s16)a->action->powerBonus, base_term = (u16)a->stat1E / 5 + pb0, raised;
        ranks(base, -1, 0, 4, 3);
        a->exp_or_acc = a->maxAtk;
        a->stateFlags = (a->stateFlags & ~0x80000) | 0x100000;
        g_ActiveActor = a;
        pe_hook_dispatch(PE_HOOK_ENEMY_HIT, NULL);
        raised = (s16)a->action->powerBonus;
        pe_hook_dispatch(PE_HOOK_ENEMY_HIT_END, NULL);
        report("energy_shot_rank3", raised == pb0 + base_term * 30 / 100 && (s16)a->action->powerBonus == pb0,
               "bonus=%d expected=%d restored=%d", raised, pb0 + base_term * 30 / 100, (s16)a->action->powerBonus);
    } else report("energy_shot_rank3", 0, "no action record on Aya");
    restore_state(&s, a);
}

static int on_battle(void *ctx, void *user) {
    Combatant *a = pe_aya_core();
    BpRecord base;
    (void)ctx; (void)user;
    if (s_done || !a || !a->attributes || ++s_frames < 120) return 0;
    s_done = 1;
    base = *pe_bp_test_record();
    emit("BP_BATTLE_START hp=%d/%d pe=%d/%d pe_light=%d\n", (s16)a->curHP, (s16)a->maxHP, a->exp_or_acc >> 16,
         a->maxAtk >> 16, !!(a->attributes->effectFlags & 0x200));
    resistance(a, &base);
    fortitude(a, &base);
    mastery(a, &base);
    pe_bp_test_set_record(&base);
    emit("BP_BATTLE_DONE pass=%d fail=%d\n", s_pass, s_fail);
    return 0;
}

void pe_bp_battle_probe_install(void) {
    const char *on = getenv("PE_BP_TEST_BATTLE");
    if (on && on[0] == '1') pe_hook_add(PE_HOOK_BATTLE_UPDATE, on_battle, NULL, PE_PRIORITY_LATE, "host");
}
