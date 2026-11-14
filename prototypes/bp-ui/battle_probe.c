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
#include "pe1/battle_start.h"
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
/* Cast `spell` at a Mastery rank from HP hp0 and full PE: what the game charged and healed. The
 * rank-0 cast is the baseline: vanilla, or whatever another mod (Rebalance) sets, which Mastery
 * must scale rather than stack on. */
typedef struct { int spent, healed; } Cast;
static Cast cast_at(Combatant *a, const BpRecord *base, int line, int rank, unsigned spell, int hp0) {
    Cast c;
    int before;
    ranks(base, -1, 0, line, rank);
    a->curHP = (u16)hp0; a->exp_or_acc = a->maxAtk; before = a->exp_or_acc;
    cast(a, spell);
    c.spent = before - a->exp_or_acc; c.healed = (s16)a->curHP - hp0;
    return c;
}
/* The charge a cut of `cut`% gives, from the baseline charge (PE Light undone first). */
static int cut_charge(Combatant *a, int spent0, int cut) {
    const int light = (a->attributes->effectFlags & 0x200) != 0;
    const int raw = light ? spent0 * 3 / 2 : spent0;
    return drain_of(a, (int)((long long)raw * (100 - cut) / 100));
}
static int near(int a, int b) { return a - b <= 2 && b - a <= 2; }   /* 16.16 rounding */
static void mastery(Combatant *a, const BpRecord *base) {
    Saved s;
    int before, spent, want;
    Cast c0, c;
    save_state(&s, a);

    /* Heal 1: rank 2 = 15% more healing than rank 0, and 15% cheaper. */
    c0 = cast_at(a, base, 0, 0, 0, 10);
    report("heal_rank0", c0.healed >= 30 && c0.spent > 0, "healed=%d pe=%d (baseline; vanilla 30 HP for 60 PE)", c0.healed, c0.spent >> 16);
    c = cast_at(a, base, 0, 2, 0, 10);
    want = cut_charge(a, c0.spent, 15);
    report("heal_rank2", c.healed == c0.healed + c0.healed * 15 / 100 && near(c.spent, want),
           "healed=%d expected=%d pe=%d.%04X expected=%d.%04X", c.healed, c0.healed + c0.healed * 15 / 100,
           c.spent >> 16, c.spent & 0xFFFF, want >> 16, want & 0xFFFF);
    restore_state(&s, a);

    /* Haste rank 2: timer 450 -> 675, cost unchanged. */
    c0 = cast_at(a, base, 7, 0, 9, (s16)a->curHP);
    c = cast_at(a, base, 7, 2, 9, (s16)a->curHP);
    report("haste_rank2", g_ActorEffectFlag100Timer == 675 && c.spent == c0.spent,
           "timer=%u expected=675 pe=%d expected=%d", g_ActorEffectFlag100Timer, c.spent >> 16, c0.spent >> 16);
    restore_state(&s, a);

    /* Barrier rank 3: costs 1/6 of max PE instead of 1/3. */
    ranks(base, -1, 0, 3, 3);
    a->exp_or_acc = a->maxAtk; before = a->exp_or_acc;
    cast(a, 5); spent = before - a->exp_or_acc;
    want = a->maxAtk / 3 - (a->maxAtk / 3 - a->maxAtk / 6);
    report("barrier_rank3", spent == want && (a->stateFlags & AYA_BARRIER), "pe=%d expected=%d (max %d)", spent >> 16, want >> 16, a->maxAtk >> 16);
    restore_state(&s, a);

    /* Detox rank 2: cures, leaves one-use Poison immunity, 25% cheaper than rank 0. */
    c0 = cast_at(a, base, 1, 0, 3, (s16)a->curHP);
    restore_state(&s, a);
    ranks(base, -1, 0, 1, 2);
    a->stateFlags = (a->stateFlags & ~AYA_POISON_MASK) | AYA_POISON;
    a->exp_or_acc = a->maxAtk; before = a->exp_or_acc;
    cast(a, 3); spent = before - a->exp_or_acc;
    want = cut_charge(a, c0.spent, 25);
    report("detox_rank2", (a->stateFlags & AYA_POISON_MASK) == AYA_POISON_IMMUNE && near(spent, want),
           "poison_state=%d expected=%d pe=%d expected=%d (rank 0: %d)", (int)(a->stateFlags & AYA_POISON_MASK), AYA_POISON_IMMUNE,
           spent >> 16, want >> 16, c0.spent >> 16);
    restore_state(&s, a);

    /* Medic rank 2: also heals 10% of max HP, 15% cheaper than rank 0. */
    c0 = cast_at(a, base, 2, 0, 4, 10);
    c = cast_at(a, base, 2, 2, 4, 10);
    want = cut_charge(a, c0.spent, 15);
    report("medic_rank2", c.healed == c0.healed + (s16)a->maxHP / 10 && near(c.spent, want), "healed=%d expected=%d pe=%d expected=%d",
           c.healed, c0.healed + (s16)a->maxHP / 10, c.spent >> 16, want >> 16);
    restore_state(&s, a);

    /* Scan: free at rank 1, never a PE gain (Rebalance's own Scan cost included). */
    c0 = cast_at(a, base, 5, 0, 7, (s16)a->curHP);
    c = cast_at(a, base, 5, 1, 7, (s16)a->curHP);
    report("scan_rank1", c.spent == 0 && c0.spent > 0, "pe=%d expected=0 (rank 0: %d)", c.spent >> 16, c0.spent >> 16);
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

/* ---- Slow / Confuse against a real enemy of this battle, forced neutral ---- */
/* Enemies are the battle's actors other than Aya that carry a combatant (encounter.c walks
 * g_FieldActorListHead the same way); prefer one whose combatant is an active enemy slot. */
static BattleEntity *find_enemy(void) {
    BattleEntity *any = NULL, *aya = pe_player();
    for (BattleEntity *e = (BattleEntity *)g_FieldActorListHead; e; e = e->next) {
        if (e == aya || !e->core) continue;
        for (int s = 0; s < 7; ++s)
            if (g_BattleEnemySlots[s].active && e->core == (void *)&g_BattleEnemySlots[s].combatant) return e;
        if (!any) any = e;
    }
    return any;
}
static int neutral_trials(Combatant *a, BattleEntity *ent, unsigned spell, int n) {
    EnemyCombatant *en = ent->core;
    const BattleEntity ent0 = *ent;
    const EnemyCombatant en0 = *en;
    const SavePromptBlock scan0 = g_BattleScanPanel;
    const int shift = spell == 8 ? 8 : 10;
    int landed = 0;
    for (int i = 0; i < n; ++i) {
        *ent = ent0; *en = en0;
        en->statusFlags2 &= ~(3u << shift);          /* neutral: the 50% case */
        en->coreFlags &= ~(spell == 8 ? 1u : 0xEu);
        ent->entityFlags &= ~0x1000u;
        a->exp_or_acc = a->maxAtk;
        g_ActiveActor = a;
        Battle_ApplySpellEffect(spell, ent);
        landed += spell == 8 ? (int)(en->coreFlags & 1) : (ent->entityFlags & 0x1000) != 0;
    }
    *ent = ent0; *en = en0; g_BattleScanPanel = scan0;
    return landed;
}
static void control(Combatant *a, const BpRecord *base) {
    enum { N = 300 };
    BattleEntity *ent = find_enemy();
    Saved s;
    if (!ent) { report("slow_confuse", 0, "no enemy entity found in this battle"); return; }
    save_state(&s, a);
    for (int c = 0; c < 2; ++c) {
        const unsigned spell = c ? 10 : 8;
        const int line = c ? 8 : 6;
        const char *name = c ? "confuse" : "slow";
        char label[32];
        int landed;
        ranks(base, -1, 0, line, 0);
        landed = neutral_trials(a, ent, spell, N);          /* vanilla 50%: 150 +-4 sd */
        snprintf(label, sizeof label, "%s_rank0", name);
        report(label, landed >= 115 && landed <= 185, "landed=%d/%d expected~150 (50%%)", landed, N);
        ranks(base, -1, 0, line, 3);
        landed = neutral_trials(a, ent, spell, N);          /* rank 3 80%: 240 +-4 sd */
        snprintf(label, sizeof label, "%s_rank3", name);
        report(label, landed >= 212 && landed <= 268, "landed=%d/%d expected~240 (80%%)", landed, N);
    }
    restore_state(&s, a);
}

/* ---- multi-frame cases: Gene Heal over 60 frames, Preraise through the real revive ---- */
enum { PH_INSTANT, PH_GENE, PH_PRERAISE, PH_DONE };
static struct {
    int phase, step, frames, rank, healed, last_hp, pe_before, revived, gene0;
    Saved saved;
    BpRecord base;
} P;

static void gene_start(Combatant *a, int rank) {
    save_state(&P.saved, a);
    ranks(&P.base, -1, 0, 9, rank);
    a->stateFlags |= AYA_GENE_HEAL;
    a->curHP = a->hpMirror = (u16)((s16)a->maxHP / 2);
    a->exp_or_acc = a->maxAtk;
    P.rank = rank; P.frames = 0; P.healed = 0; P.last_hp = (s16)a->curHP;
}
static int gene_step(Combatant *a) {   /* 1 when the 60-frame window is over */
    int hp = (s16)a->curHP;
    if (hp > P.last_hp) P.healed += hp - P.last_hp;   /* enemy damage frames are ignored */
    P.last_hp = hp;
    if (++P.frames < 60) return 0;
    restore_state(&P.saved, a);
    return 1;
}
static void preraise_start(Combatant *a, int rank) {
    save_state(&P.saved, a);
    ranks(&P.base, -1, 0, 10, rank);
    /* Preraise is on Aya for a few frames first (as after a cast); she falls at frame 5. */
    a->stateFlags = (a->stateFlags | AYA_PRERAISE) & ~AYA_BARRIER;
    a->exp_or_acc = a->maxAtk;
    P.rank = rank; P.frames = 0; P.revived = 0;
}
static int preraise_step(Combatant *a) {   /* 1 when this case is reported */
    int hp = (s16)a->curHP, max = (s16)a->maxHP;
    char label[32];
    {   /* trace for diagnosis: every change of HP / Preraise flag / PE */
        static int last_hp = -9999, last_flag = -1, last_pe = -1;
        int flag = (a->stateFlags & AYA_PRERAISE) != 0;
        if (hp != last_hp || flag != last_flag || a->exp_or_acc != last_pe)
            emit("BP_BATTLE_TRACE rank=%d frame=%d hp=%d preraise=%d pe=%d.%04X\n", P.rank, P.frames, hp, flag,
                 a->exp_or_acc >> 16, a->exp_or_acc & 0xFFFF);
        last_hp = hp; last_flag = flag; last_pe = a->exp_or_acc;
    }
    if (++P.frames < 5) return 0;
    if (P.frames == 5) {
        a->exp_or_acc = a->maxAtk;
        P.pe_before = a->exp_or_acc;
        a->curHP = a->hpMirror = 0;
        return 0;
    }
    if (!P.revived && hp > 0) { P.revived = P.frames; return 0; }
    if (!P.revived && P.frames < 1500) return 0;
    if (P.revived && P.frames < P.revived + 3) return 0;   /* pe_bp's rider runs the frame after */
    snprintf(label, sizeof label, "preraise_rank%d", P.rank);
    if (!P.revived) report(label, 0, "no revive within 1500 frames (hp=%d)", hp);
    else if (P.rank == 0)
        report(label, hp == max >> 1 && a->exp_or_acc <= (P.pe_before >> 2) + 0x10000,
               "hp=%d expected=%d pe=%d expected~%d (a quarter)", hp, max >> 1, a->exp_or_acc >> 16, (P.pe_before >> 2) >> 16);
    else
        report(label, hp == max * 3 / 4 && a->exp_or_acc >= (P.pe_before >> 1) - 0x10000,
               "hp=%d expected=%d pe=%d expected~%d (half)", hp, max * 3 / 4, a->exp_or_acc >> 16, (P.pe_before >> 1) >> 16);
    restore_state(&P.saved, a);
    return 1;
}

static int on_battle(void *ctx, void *user) {
    Combatant *a = pe_aya_core();
    (void)ctx; (void)user;
    if (s_done || !a || !a->attributes || ++s_frames < 120) return 0;
    switch (P.phase) {
    case PH_INSTANT:
        P.base = *pe_bp_test_record();
        emit("BP_BATTLE_START hp=%d/%d pe=%d/%d pe_light=%d\n", (s16)a->curHP, (s16)a->maxHP, a->exp_or_acc >> 16,
             a->maxAtk >> 16, !!(a->attributes->effectFlags & 0x200));
        resistance(a, &P.base);
        fortitude(a, &P.base);
        mastery(a, &P.base);
        control(a, &P.base);
        P.phase = PH_GENE; P.step = 0;
        gene_start(a, 0);
        return 0;
    case PH_GENE:
        if (!gene_step(a)) return 0;
        if (P.step == 0) { P.step = 1; P.gene0 = P.healed; gene_start(a, 2); return 0; }
        {
            const int vanilla = P.gene0, mastered = P.healed;
            report("gene_heal_rank0", vanilla >= 25 && vanilla <= 35, "healed=%d in 60 frames expected~30", vanilla);
            report("gene_heal_rank2", mastered >= 50 && mastered <= 70 && mastered >= vanilla * 17 / 10,
                   "healed=%d in 60 frames expected~60 (twice rank 0)", mastered);
        }
        {   /* Fortitude's charges belong to the whole battle: still spent 120+ frames later. */
            EnemyCombatant ent;
            EnemyActionEffect fx;
            Saved s;
            int hp0 = (s16)a->maxHP < 100 ? (s16)a->maxHP : 100;
            save_state(&s, a);
            memset(&ent, 0, sizeof ent); memset(&fx, 0, sizeof fx);
            fx.effectType = ENEMY_EFFECT_REDUCE_HP_TO_1; ent.effect = &fx;
            ranks(&P.base, BP_FORTITUDE, 3, -1, 0);
            a->stateFlags &= ~(AYA_BARRIER | AYA_PRERAISE);
            a->curHP = (s16)hp0; g_ActiveActor = a;
            Battle_ApplyEnemyAttack(&ent);
            report("fortitude_charges_persist", (s16)a->curHP == 1, "hp=%d expected=1 (charges spent earlier this battle)", (s16)a->curHP);
            restore_state(&s, a);
        }
        P.phase = PH_PRERAISE; P.step = 0;
        preraise_start(a, 0);
        return 0;
    case PH_PRERAISE:
        if (!preraise_step(a)) return 0;
        if (P.step == 0) { P.step = 1; preraise_start(a, 3); return 0; }
        P.phase = PH_DONE;
        /* fallthrough */
    default:
        s_done = 1;
        pe_bp_test_set_record(&P.base);
        emit("BP_BATTLE_DONE pass=%d fail=%d\n", s_pass, s_fail);
        return 0;
    }
}

/* ---- EX Game carryover probe (PE_BP_TEST_EX=1): in free field, buy Poison 1 and Heal 1 through
 * the real transaction service, then start an EX Game with the game's own command
 * (INV_CMD_START_NEW_GAME; the host announces PE_HOOK_NEW_GAME EX at its entry) and report what the
 * new run starts with. The carryover mode is the F1 setting in pe_mods.ini. ---- */
#include "pe_bp_screens.h"
#include "pe_services.h"
#include "pe1/menu_inventory.h"
static int s_ex_step, s_ex_wait;
static unsigned s_ex_bp_before;
static int on_ex_frame(void *ctx, void *user) {
    const PeBpScreensApi *api = pe_service_get("bp_screens", 1);
    BpSnapshot s;
    (void)ctx; (void)user;
    if (s_ex_step >= 3 || !api) return 0;
    if (s_ex_step == 0 && !pe_free_field()) return 0;   /* the read-back runs whatever screen follows */
    if (++s_ex_wait < (s_ex_step ? 5 : 60)) return 0;
    if (s_ex_step == 0) {
        BpResult a = api->execute("buy.def.0"), b = api->execute("buy.line.0");
        if (!api->snapshot(&s)) return 0;
        emit("BP_EX_BEFORE level=%d bp=%u poison=%u heal=%u buy_poison=%d buy_heal=%d\n", s.level + 1, s.bp,
             s.rec.defense[0], s.rec.mastery[0], a.error, b.error);
        s_ex_bp_before = s.bp;
        s_ex_step = 1;
        s_ex_wait = 0;
        Inv_DispatchCommand(INV_CMD_START_NEW_GAME, 0, 0, NULL);
        return 0;
    }
    if (s_ex_step == 1) {
        s_ex_step = 2;
        if (!api->snapshot(&s)) { emit("BP_EX_AFTER unreadable\n"); return 0; }
        emit("BP_EX_AFTER level=%d bp=%u bp_before=%u new_game_count=%u poison=%u heal=%u carried_poison=%u carried_heal=%u pool=%u full_resets=%u\n",
             s.level + 1, s.bp, s_ex_bp_before, (unsigned)g_AyaSaveState.new_game_count, s.rec.defense[0], s.rec.mastery[0],
             s.rec.carried_defense[0], s.rec.carried_mastery[0], s.rec.carried_pool, s.rec.full_resets);
        s_ex_step = 3;
    }
    return 0;
}

/* ---- public "bp" service probe (PE_BP_TEST_API=1): in free field, a client mod's view. Uses only
 * host/pe_services.h and the host's events, as another mod would: quote, execute, the
 * "bp.changed" event, an upgrade-only grant spent on a Mastery rank, a pause and its lifting, and
 * a screen purchase announced with no mod id. ---- */
static int s_api_step, s_api_wait, s_api_events;
static char s_api_why[48], s_api_by[32];
static PeBpChanged s_api_last;
static int on_bp_changed(void *ctx, void *user) {
    const PeBpChanged *e = ctx;
    (void)user;
    ++s_api_events;
    s_api_last = *e;
    snprintf(s_api_why, sizeof s_api_why, "%s", e->why ? e->why : "");
    snprintf(s_api_by, sizeof s_api_by, "%s", e->by ? e->by : "");
    return 0;
}
static int on_api_frame(void *ctx, void *user) {
    const PeBpApi *bp = pe_service_get("bp", 1);
    const PeBpScreensApi *screens = pe_service_get("bp_screens", 1);
    PeBpStatus st, st2;
    PeBpQuote q;
    int ev;
    (void)ctx; (void)user;
    if (s_api_step || !pe_free_field() || ++s_api_wait < 60) return 0;
    s_api_step = 1;
    if (!bp || bp->struct_size < sizeof *bp || !screens) { report("api_published", 0, "bp=%p screens=%p", (void *)bp, (void *)screens); emit("BP_API_DONE pass=%d fail=%d\n", s_pass, s_fail); return 0; }
    memset(&st, 0, sizeof st); st.struct_size = sizeof st;
    report("api_status", bp->status(&st) == 1 && st.loaded && st.bp == g_AyaSaveState.bonus_points && st.features == 15,
           "loaded=%d bp=%u game=%u features=%u pool=%u", st.loaded, (unsigned)st.bp, (unsigned)g_AyaSaveState.bonus_points,
           (unsigned)st.features, (unsigned)st.upgrade_pool);
    report("api_table", !strcmp(bp->key(PE_BP_DEFENSE, 0), "poison") && bp->price(PE_BP_DEFENSE, 0, 1) == 75 &&
           bp->max_rank(PE_BP_MASTERY, 5) == 1 && bp->rank(PE_BP_DEFENSE, 0) == 0, "poison price=%u", (unsigned)bp->price(PE_BP_DEFENSE, 0, 1));

    memset(&q, 0, sizeof q); q.struct_size = sizeof q;
    ev = s_api_events;
    report("api_quote_changes_nothing", bp->quote("buy.def.0", &q) == PE_BP_OK && q.bp_cost == 75 &&
           g_AyaSaveState.bonus_points == st.bp && bp->rank(PE_BP_DEFENSE, 0) == 0 && s_api_events == ev,
           "err=%d cost=%u bp=%u events=%d", (int)q.error, (unsigned)q.bp_cost, (unsigned)g_AyaSaveState.bonus_points, s_api_events - ev);

    report("api_execute", bp->execute("buy.def.0", "api_probe", &q) == PE_BP_OK && bp->rank(PE_BP_DEFENSE, 0) == 1 &&
           g_AyaSaveState.bonus_points == st.bp - 75, "rank=%d bp=%u", bp->rank(PE_BP_DEFENSE, 0), (unsigned)g_AyaSaveState.bonus_points);
    report("api_changed_event", s_api_events == ev + 1 && !strcmp(s_api_why, "buy.def.0") && !strcmp(s_api_by, "api_probe") &&
           s_api_last.bp_before == st.bp && s_api_last.bp_after == st.bp - 75, "events=%d why=%s by=%s", s_api_events - ev, s_api_why, s_api_by);

    ev = s_api_events;
    report("api_grant_pool", bp->grant_pool(100, "api_probe") == PE_BP_OK && s_api_events == ev + 1 && !strcmp(s_api_why, "grant") &&
           s_api_last.pool_after == st.upgrade_pool + 100, "pool=%u", (unsigned)s_api_last.pool_after);
    /* Heal 1 costs 150: 100 from the pool, 50 BP. */
    memset(&q, 0, sizeof q); q.struct_size = sizeof q;
    {
        unsigned bp_before = g_AyaSaveState.bonus_points;
        int err = bp->execute("buy.line.0", "api_probe", &q);
        report("api_pool_spent", err == PE_BP_OK && q.pool_cost == 100 && q.bp_cost == 50 && g_AyaSaveState.bonus_points == bp_before - 50 &&
               bp->rank(PE_BP_MASTERY, 0) == 1, "err=%d pool_cost=%u bp_cost=%u", err, (unsigned)q.pool_cost, (unsigned)q.bp_cost);
    }

    report("api_pause", bp->pause("api_probe", PE_BP_FEATURE_RESISTANCE) == 0 && bp->rank(PE_BP_DEFENSE, 0) == 1 &&
           bp->active_rank(PE_BP_DEFENSE, 0) == 0 && bp->quote("buy.def.0", &q) == PE_BP_ERR_INACTIVE &&
           bp->active_rank(PE_BP_MASTERY, 0) == 1, "active=%d quote=%d", bp->active_rank(PE_BP_DEFENSE, 0), (int)q.error);
    memset(&st2, 0, sizeof st2); st2.struct_size = sizeof st2;
    bp->status(&st2);
    report("api_pause_status", st2.paused == PE_BP_FEATURE_RESISTANCE && !(st2.features & PE_BP_FEATURE_RESISTANCE), "paused=%u features=%u",
           (unsigned)st2.paused, (unsigned)st2.features);
    {   /* the screens see the pause too */
        BpSnapshot s;
        int seen = screens->snapshot(&s) && !(s.features & BP_FEATURE_RESISTANCE);
        bp->pause("api_probe", 0);
        report("api_unpause", seen && bp->active_rank(PE_BP_DEFENSE, 0) == 1 && bp->quote("buy.def.0", &q) == PE_BP_OK,
               "screens_saw_pause=%d active=%d", seen, bp->active_rank(PE_BP_DEFENSE, 0));
    }

    ev = s_api_events;
    {
        BpResult r = screens->execute("buy.def.1");
        report("api_screens_announce", r.error == BP_OK && s_api_events == ev + 1 && !strcmp(s_api_why, "buy.def.1") && !s_api_by[0],
               "err=%d events=%d by=%s", r.error, s_api_events - ev, s_api_by);
    }
    emit("BP_API_DONE pass=%d fail=%d\n", s_pass, s_fail);
    return 0;
}

void pe_bp_battle_probe_install(void) {
    const char *on = getenv("PE_BP_TEST_BATTLE"), *ex = getenv("PE_BP_TEST_EX"), *api = getenv("PE_BP_TEST_API");
    if (on && on[0] == '1') pe_hook_add(PE_HOOK_BATTLE_UPDATE, on_battle, NULL, PE_PRIORITY_LATE, "host");
    if (ex && ex[0] == '1') pe_hook_add(PE_HOOK_FRAME, on_ex_frame, NULL, PE_PRIORITY_LATE, "host");
    if (api && api[0] == '1') {
        pe_hook_add(pe_event_id("bp.changed"), on_bp_changed, NULL, PE_PRIORITY_NORMAL, "host");
        pe_hook_add(PE_HOOK_FRAME, on_api_frame, NULL, PE_PRIORITY_LATE, "host");
    }
}
