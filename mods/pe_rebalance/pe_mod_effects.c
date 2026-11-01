/*
 * Mod effects rebalance, ported from the psxrecomp build (src/mods/pe_mod_effects.c; bridge
 * findings pe-mod-effects-overhaul-2026-10-14.md, pe-rebalance-economy-basis.md,
 * pe-gun-ladder-2026-10-20.md). F1 > PE Enhancements > Mod Effects.
 *
 * Rules: Vanilla (this file does nothing) / Rebalanced (default) / Custom (any row changed).
 *
 * Aya's gun hits: at the entry of Battle_StepEnemyMovement (PE_HOOK_ENEMY_HIT), which computes
 *   damage = ((stat1E / 5 + action power bonus) x hit% - enemy defense / shots) x quality x element
 * and subtracts it, we change the inputs for this one hit, exactly as the recomp build did:
 *   - element: scale BOTH the attack term (via the power bonus) and the defense by the multiplier
 *     we want and clear the enemy's Heat / Frost bits, so the game adds no factor of its own;
 *   - Rate of Fire 7 / 10 per-shot %, installed-RoF whole-volley % (R-E), flat gun bonuses
 *     (R-B / R-C / R-F): the attack term alone;
 *   - Acid / Tranquilizer pre-rolled from our chances (bosses unchanged);
 *   - Critical: some normal hits (reaction 2) become criticals (reaction 3);
 *   - R-A: an Attack All turn in a crowd takes at most "Attack All crowd cap %" of a target's
 *     max HP (the hit's power bonus is lowered just enough; a minimum-damage hit that still
 *     crosses is topped back up at the next hook);
 * and put back what we changed at the next hit or the next Battle_Update (PE_HOOK_BATTLE_UPDATE).
 *
 * Per battle (frame tick): poison strength, Quickdraw top-up, damage taken with Attack Down /
 * Slow Active Time armor, fast volleys for installed RoF; "modfx" / "hitq" / "reach" play-log rows.
 *
 * Not ported here: the weapon / armor cache sync (it exists for Catalog / Tool / editor edits,
 * which come with the economy port), the HP-level repair and the old Counter-boost restore (both
 * only for saves and save states made by early recomp builds).
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "pe_game.h"
#include "pe_mod_effects.h"
#include "pe_mod_host.h"
#include "pe_mod_menu.h"
#include "pe_pages.h"
#include "pe1/weapon_list_output.h"

enum { RULES_VANILLA, RULES_REBALANCED, RULES_CUSTOM };
static const char *const s_rules_names[] = { "Vanilla", "Rebalanced", "Custom" };
static const char *const s_onoff[] = { "Off", "On" };

typedef struct { const char *label, *desc; int vanilla, rebalanced, min, max, step; int *value; int choice; } PeRow;
static int s_rules = RULES_REBALANCED;
static int s_rules_shown = -1;
static int s_elem_neutral, s_elem_weak, s_elem_resist, s_elem_best;
static int s_rof7, s_rof10, s_status_resist, s_status_average, s_poison, s_crit, s_quickdraw;
static int s_guard_attack_down, s_guard_slow_at;
static int s_g3a3_bonus, s_m203_4_bonus, s_aa_cap, s_rof_volley, s_fast_volley, s_prog_bonus;
static PeRow s_rows[] = {
    { "Heat/Frost neutral %", "Damage vs enemies without a match.", 100, 115, 50, 200, 5, &s_elem_neutral, 0 },
    { "Heat/Frost weak %", "Damage vs enemies weak to it.", 150, 150, 50, 300, 5, &s_elem_weak, 0 },
    { "Heat/Frost resist %", "Damage vs enemies that resist it.", 20, 80, 10, 150, 5, &s_elem_resist, 0 },
    { "Heat+Frost best of two", "Both on: the better one counts.", 0, 1, 0, 1, 1, &s_elem_best, 1 },
    { "RoF 7 per-shot %", "Each of 7 shots (vanilla 18).", 18, 20, 10, 40, 1, &s_rof7, 0 },
    { "RoF 10 per-shot %", "Each of 10 shots (vanilla 13).", 13, 15, 8, 30, 1, &s_rof10, 0 },
    { "Installed RoF volley %", "RoF 5/7/10 mods: whole volley.", 0, 200, 0, 300, 10, &s_rof_volley, 0 },
    { "G3A3 attack bonus", "Added to the G3A3's attack.", 0, 45, 0, 100, 5, &s_g3a3_bonus, 0 },
    { "M203 4 attack bonus", "Added to the M203 4's attack.", 0, 15, 0, 100, 5, &s_m203_4_bonus, 0 },
    { "Progression gun bonuses", "Weaker guns: +5 to +25 attack.", 0, 1, 0, 1, 1, &s_prog_bonus, 1 },
    { "Attack All crowd cap %", "Max HP lost a turn (100 = off).", 100, 75, 25, 100, 5, &s_aa_cap, 0 },
    { "RoF mods: fast volleys", "Installed RoF 5/7/10 fire fast.", 0, 1, 0, 1, 1, &s_fast_volley, 1 },
    { "Acid/Tranq vs resist %", "Chance vs resistant (vanilla 0).", 0, 20, 0, 100, 5, &s_status_resist, 0 },
    { "Acid/Tranq vs normal %", "Chance vs others (vanilla 50).", 50, 60, 0, 100, 5, &s_status_average, 0 },
    { "Poison strength %", "Acid poison damage per tick.", 100, 125, 50, 300, 5, &s_poison, 0 },
    { "Critical bonus points", "Crit chance the effect adds (20).", 20, 40, 20, 80, 5, &s_crit, 0 },
    { "Quickdraw bonus points", "Added to the speed-stat chance.", 0, 15, 0, 90, 5, &s_quickdraw, 0 },
    { "Attack Down dmg taken %", "Armor: damage Aya takes.", 100, 75, 50, 100, 5, &s_guard_attack_down, 0 },
    { "Slow AT dmg taken %", "Armor: damage Aya takes.", 100, 85, 50, 100, 5, &s_guard_slow_at, 0 },
};
#define PE_ROWS ((int)(sizeof s_rows / sizeof s_rows[0]))
#define PE_ENEMY_SLOTS 7

/* ---- per-hit changes, restored after ---- */
static struct {
    int active;
    EnemyCombatant *enemy;
    BattleAction *action;
    uint32_t s2_mask, s2_orig;
    uint16_t def_orig, power_orig;
    int def_set, power_set;
    int32_t hp_before;   /* self-check: the hit's predicted damage vs what happened */
    int pred;
} s_hit;

/* Hit quality by distance (any rules): the reaction the game picked (core flags >> 15: 1 graze,
 * 2 hit, 3 critical, 4 splash) per distance band, distance as % of the gun's range. */
static const int s_band_edge[5] = { 50, 80, 100, 120, 150 };
static unsigned s_hq[6][5];
static unsigned s_hq_range, s_hq_gun;
static unsigned s_reach[4], s_reach_frames, s_reach_range;

static struct {
    unsigned hits, elem_hits, elem_neutral, elem_weak, elem_resist, rof_boost;
    unsigned rof_volley, gun_bonus, aa_capped, aa_topup, fast_volley, pred_ok, pred_bad;
    unsigned status_on, status_off, crit_up, quickdraw, poison_scaled;
    unsigned hp_refunded;
} s_bt;
static int s_in_battle, s_battle_frames;
static uint32_t s_rng = 0x51ED270Bu;
static int16_t s_poison_written[PE_ENEMY_SLOTS], s_poison_original[PE_ENEMY_SLOTS];
static void pe_restore_poison(void) {
    for (int i = 0; i < PE_ENEMY_SLOTS; ++i) {
        if (s_poison_written[i] && g_BattleEnemySlots[i].combatant.periodicDamage == s_poison_written[i])
            g_BattleEnemySlots[i].combatant.periodicDamage = s_poison_original[i];
        s_poison_written[i] = s_poison_original[i] = 0;
    }
}
static int s_prev_hp = -1, s_refund_frac;

static uint32_t pe_rand(void) { s_rng ^= s_rng << 13; s_rng ^= s_rng >> 17; s_rng ^= s_rng << 5; return s_rng; }
static int pe_roll(int pct) { return (int)(pe_rand() % 100u) < pct; }
static int pe_on(void) { return s_rules != RULES_VANILLA; }

static unsigned pe_gun_id(void) {
    const ItemDataRecord *rec = pe_equipped(0);
    return rec ? rec->itemId : 0u;
}

static void pe_note_hit_quality(BattleEntity *entity, EnemyCombatant *enemy, BattleAction *action) {
    BattleEntity *player = pe_player();
    unsigned pct, reaction;
    int band = 0;
    if (!player || action->range <= 0) return;
    pct = (unsigned)Battle_CalcDistToPlayer(entity, player) * 100u / (unsigned)action->range;
    while (band < 5 && pct >= (unsigned)s_band_edge[band]) ++band;
    reaction = (enemy->coreFlags >> 15) & 7u;
    if (reaction > 4u) reaction = 0;
    ++s_hq[band][reaction];
    s_hq_range = (unsigned)action->range;
    s_hq_gun = pe_gun_id();
}

static void pe_check_prediction(void);

static void pe_restore_hit(void) {
    if (!s_hit.active) return;
    pe_check_prediction();
    if (s_hit.enemy) {
        if (s_hit.s2_mask)
            s_hit.enemy->statusFlags2 = (s_hit.enemy->statusFlags2 & ~s_hit.s2_mask) | (s_hit.s2_orig & s_hit.s2_mask);
        if (s_hit.def_set) s_hit.enemy->defense = s_hit.def_orig;
    }
    if (s_hit.power_set && s_hit.action) s_hit.action->powerBonus = s_hit.power_orig;
    memset(&s_hit, 0, sizeof s_hit);
}

/* Element multiplier in % for 2-bit resist value v (0 neutral, 1 resists, 2 weak). */
static int pe_elem_pct(unsigned v) { return v == 1u ? s_elem_resist : v == 2u ? s_elem_weak : s_elem_neutral; }

/* Pre-roll a status round: the enemy's 2 bits at `shift` become 2 (always) or 1 (never). */
static void pe_status(uint32_t *s2, uint32_t *mask, unsigned shift) {
    const unsigned v = (*s2 >> shift) & 3u;
    int on;
    if (v == 2u) return;
    on = pe_roll(v == 0u ? s_status_average : s_status_resist);
    *s2 = (*s2 & ~(3u << shift)) | ((on ? 2u : 1u) << shift);
    *mask |= 3u << shift;
    if (on) ++s_bt.status_on; else ++s_bt.status_off;
}

/* ---- R-A / R-B / R-C / R-E / R-F ---- */
#define GUN_G3A3   0x7Du
#define GUN_M203_4 0x86u
/* Shared by battle damage and the Weapon power (DPS) display. These flat attack
 * bonuses follow the Progression gun bonuses toggle and the rules preset. */
static const uint8_t s_prog_guns[][2] = {
    { 0x52u, 15 }, /* M1911A3 */
    { 0x5Fu, 5 },  /* PPK */
    { 0x69u, 10 }, /* M870 2 (MA10-2) */
    { 0x79u, 25 }, /* M16A1 */
    { 0x7Au, 20 }, /* M16A2 */
    { 0x7Bu, 15 }, /* SG550 */
    { 0x83u, 20 }, /* M203 */
    { 0x84u, 15 }, /* M203 2 */
    { 0x85u, 20 }, /* M203 3 */
    { 0x87u, 15 }, /* M203 5 */
    { 0x8Au, 20 }, /* M79 2 */
    { 0x8Bu, 18 }, /* M79 3 */
    { 0x8Cu, 15 }, /* M79 4 */
    { 0x8Du, 10 }, /* M79 5 */
};

static int pe_gun_bonus(unsigned gun) {
    unsigned i;
    if (gun == GUN_G3A3) return s_g3a3_bonus;
    if (gun == GUN_M203_4) return s_m203_4_bonus;
    if (s_prog_bonus)
        for (i = 0; i < sizeof s_prog_guns / sizeof s_prog_guns[0]; ++i)
            if (s_prog_guns[i][0] == gun) return s_prog_guns[i][1];
    return 0;
}
static const uint8_t s_shot_pct[11] = { 0, 100, 60, 41, 0, 25, 0, 18, 0, 0, 13 };

/* The damage Battle_StepEnemyMovement deals for a gun hit (see its source); minimum 1, 2 on a
 * critical. The Acid / Tranq / Disable rolls change no damage. */
static int pe_hit_damage(int s, int pb, unsigned shots, unsigned def, uint32_t cf, uint32_t s2, uint32_t turn) {
    int dmg;
    if (shots < 1u || shots > 10u || !s_shot_pct[shots]) shots = 1u;
    dmg = (s + pb) * s_shot_pct[shots] / 100 - (int)(def / shots);
    if (dmg > 0) {
        const uint32_t r = cf & 0x38000u;
        const unsigned cls = (cf >> 18) & 3u;
        if (r == 0x8000u) dmg = dmg * 3 / 10;
        else if (r == 0x18000u) dmg = dmg * 3 / 2;
        else if (r == 0x20000u) dmg /= 10;
        if (cls != 2u && cls != 3u && !(cf & 0x400u)) {
            int mode = 0;
            if (turn & 0x100u) { const unsigned v = (s2 >> 14) & 3u; if (v == 1u) mode = 1; else if (v == 2u) mode = 2; }
            if (mode != 2 && (turn & 0x200u)) { const unsigned v = (s2 >> 16) & 3u; if (v == 1u) mode = 1; else if (v == 2u) mode = 2; }
            if (mode == 1) dmg /= 5; else if (mode == 2) dmg = dmg * 3 / 2;
        }
    }
    if (dmg <= 0) dmg = (cf & 0x38000u) == 0x18000u ? 2 : 1;
    return dmg;
}

/* R-A applies in ordinary fights: two or more enemies alive and none of them a boss (0x40000). */
static int pe_crowd_fight(void) {
    int alive = 0, i;
    for (i = 0; i < PE_ENEMY_SLOTS; ++i) {
        const EnemyCombatant *core = &g_BattleEnemySlots[i].combatant;
        if (!g_BattleEnemySlots[i].active || core->hpAlive <= 0) continue;
        if (core->statusFlags2 & 0x40000u) return 0;
        ++alive;
    }
    return alive >= 2;
}

/* R-A per-turn reference: each target's HP at its first Attack All hit of the turn; 40 battle
 * frames with no Attack All hit start a new turn. */
static struct {
    EnemyCombatant *enemy[PE_ENEMY_SLOTS];
    int32_t ref[PE_ENEMY_SLOTS];
    int n, idle;
    EnemyCombatant *fix_enemy;
    int32_t fix_floor;
} s_aa;

static int32_t pe_aa_ref(EnemyCombatant *enemy, int32_t hp) {
    int i;
    if (s_aa.idle > 40) s_aa.n = 0;
    s_aa.idle = 0;
    for (i = 0; i < s_aa.n; ++i) if (s_aa.enemy[i] == enemy) return s_aa.ref[i];
    if (s_aa.n < PE_ENEMY_SLOTS) { s_aa.enemy[s_aa.n] = enemy; s_aa.ref[s_aa.n] = hp; ++s_aa.n; }
    return hp;
}

static void pe_aa_fix(void) {
    if (!s_aa.fix_enemy) return;
    if (s_aa.fix_enemy->hpAlive < s_aa.fix_floor) s_aa.fix_enemy->hpAlive = s_aa.fix_floor;
    s_aa.fix_enemy = NULL;
}

/* R-E: the equipped gun's RoF for this many shots is an installed mod (effects 3 / 4 / 5 = RoF
 * 5 / 7 / 10): the gun has it and its base item doesn't (the P90's own RoF 10 stays built in). */
static int pe_installed_rof(unsigned shots) {
    const int effect = shots == 5u ? 3 : shots == 7u ? 4 : shots == 10u ? 5 : 0;
    const ItemDataRecord *rec = pe_equipped(0);
    if (!effect || !rec || !pe_rec_has_effect(rec, effect)) return 0;
    return !pe_rec_has_effect(pe_base_item(rec->itemId), effect);
}

/* Self-check: every gun hit's damage is predicted at the hook and compared with the HP the
 * target lost by the next hook (dmg_pred in the modfx row; "dmgchk" rows for the first misses). */
static int s_chk_logged;
static void pe_check_prediction(void) {
    int32_t lost;
    if (!s_hit.pred || !s_hit.enemy) return;
    lost = s_hit.hp_before - s_hit.enemy->hpAlive;
    if (lost == s_hit.pred) ++s_bt.pred_ok;
    else {
        ++s_bt.pred_bad;
        if (s_chk_logged < 8) {
            ++s_chk_logged;
            pe_log("dmgchk", "predicted=%d lost=%d hp_before=%d flags=0x%08X",
                   s_hit.pred, (int)lost, (int)s_hit.hp_before, (unsigned)s_hit.enemy->coreFlags);
        }
    }
    s_hit.pred = 0;
}

static int pe_on_enemy_hit(void *ctx, void *user) {
    BattleEntity *entity = (BattleEntity *)ctx;
    EnemyCombatant *enemy;
    Combatant *actor = g_ActiveActor;
    BattleAction *action;
    uint32_t turn, s2, mask = 0;
    unsigned shots, d, nd;
    int boss, atk_pm = 1000, both_pm = 1000, bonus, s, pb, want;
    (void)user;

    pe_restore_hit();
    pe_aa_fix();
    if (!pe_game_loaded() || !entity || !(enemy = (EnemyCombatant *)entity->core)) return 0;
    if (!actor || actor != pe_aya_core() || (void *)enemy == (void *)actor) return 0;
    if (actor->stateFlags & 0x180000) return 0;          /* PE skills etc.: guns only */
    if (!(action = actor->action)) return 0;
    pe_note_hit_quality(entity, enemy, action);
    if (!pe_on()) return 0;
    turn = action->turnWord;
    shots = turn & 0xFu;
    s2 = enemy->statusFlags2;
    boss = (s2 & 0x40000u) != 0;
    ++s_bt.hits;

    if (s_rof_volley > 0 && pe_installed_rof(shots)) {   /* R-E: whole volley % */
        atk_pm = s_rof_volley * 1000 / (int)(shots * s_shot_pct[shots]);
        ++s_bt.rof_volley;
    } else {
        if (shots == 7u && s_rof7 != 18) { atk_pm = s_rof7 * 1000 / 18; ++s_bt.rof_boost; }
        if (shots == 10u && s_rof10 != 13) { atk_pm = s_rof10 * 1000 / 13; ++s_bt.rof_boost; }
    }
    bonus = pe_gun_bonus(pe_gun_id());                    /* R-B / R-C / R-F */
    if (bonus) ++s_bt.gun_bonus;
    if (turn & 0x300u) {
        const unsigned h = (s2 >> 14) & 3u, f = (s2 >> 16) & 3u;
        unsigned which;
        int pct;
        if (s_elem_best || !(turn & 0x100u) || !(turn & 0x200u)) {
            pct = 0;
            if (turn & 0x100u) pct = pe_elem_pct(h);
            if ((turn & 0x200u) && pe_elem_pct(f) > pct) pct = pe_elem_pct(f);
        } else {                                          /* vanilla order: Heat, then Frost unless weak */
            unsigned st = h == 1u ? 1u : h == 2u ? 2u : 0u;
            if (st != 2u && (f == 1u || f == 2u)) st = f;
            pct = pe_elem_pct(st);
        }
        which = (turn & 0x100u) && pe_elem_pct(h) == pct ? h : f;
        if (which == 1u) ++s_bt.elem_resist; else if (which == 2u) ++s_bt.elem_weak; else ++s_bt.elem_neutral;
        ++s_bt.elem_hits;
        both_pm = pct * 10;
        s2 &= ~(0xFu << 14);                              /* the game adds no element factor itself */
        mask |= 0xFu << 14;
    }
    if (!boss) {
        if (turn & 0x800u) pe_status(&s2, &mask, 2);      /* Acid */
        if (turn & 0x1000u) pe_status(&s2, &mask, 4);     /* Tranquilizer */
    }
    if ((turn & 0x20000u) && s_crit > 20) {               /* Critical: normal hits -> criticals */
        if (((enemy->coreFlags >> 15) & 7u) == 2u && pe_roll((s_crit - 20) * 100 / 75)) {
            enemy->coreFlags = (enemy->coreFlags & ~0x38000u) | 0x18000u;
            ++s_bt.crit_up;
        }
    }

    s_hit.active = 1;
    s_hit.enemy = enemy;
    s_hit.action = action;
    if (mask) {
        s_hit.s2_mask = mask;
        s_hit.s2_orig = enemy->statusFlags2;
        enemy->statusFlags2 = (s_hit.s2_orig & ~mask) | (s2 & mask);
    }
    s = (u16)actor->stat1E / 5;
    pb = (s16)action->powerBonus;
    d = enemy->defense;
    want = pb;
    nd = d;
    if (atk_pm != 1000 || both_pm != 1000 || bonus) {
        want = (int)(((int64_t)(s + pb + bonus) * atk_pm / 1000) * both_pm / 1000) - s;
        if (both_pm != 1000) {
            nd = (unsigned)((uint64_t)d * (unsigned)both_pm / 1000u);
            if (nd > 0xFFFFu) nd = 0xFFFFu;
        }
    }
    /* R-A: an Attack All turn takes at most s_aa_cap % of a target's max HP (crowds only). */
    if (s_aa_cap < 100 && (turn & 0xC0u) == 0xC0u && pe_crowd_fight()) {
        const int32_t hp = enemy->hpAlive;
        int32_t max = enemy->hpAliveCap, floor;
        if (max <= 0) max = hp;
        floor = pe_aa_ref(enemy, hp) - (int32_t)((int64_t)max * s_aa_cap / 100);
        if (floor > 0 && hp > 0) {
            const uint32_t cf = enemy->coreFlags;
            const int allowed = hp - floor;
            if (pe_hit_damage(s, want, shots, nd, cf, s2, turn) > allowed) {
                int lo = -s, hi = want;
                if (pe_hit_damage(s, lo, shots, nd, cf, s2, turn) > allowed) {
                    want = lo;
                    s_aa.fix_enemy = enemy;
                    s_aa.fix_floor = floor;
                    ++s_bt.aa_topup;
                } else {
                    while (lo < hi) {                     /* the largest bonus that stays above the line */
                        const int mid = lo + (hi - lo + 1) / 2;
                        if (pe_hit_damage(s, mid, shots, nd, cf, s2, turn) <= allowed) lo = mid; else hi = mid - 1;
                    }
                    want = lo;
                }
                ++s_bt.aa_capped;
            }
        }
    }
    if (want > 0x7FFF) want = 0x7FFF;
    if (want < -0x8000) want = -0x8000;
    if (want != pb) {
        s_hit.power_orig = (uint16_t)pb;
        s_hit.power_set = 1;
        action->powerBonus = (u16)(s16)want;
    }
    if (nd != d) {
        s_hit.def_orig = (uint16_t)d;
        s_hit.def_set = 1;
        enemy->defense = (u16)nd;
    }
    s_hit.hp_before = enemy->hpAlive;
    s_hit.pred = pe_hit_damage(s, want, shots, nd, enemy->coreFlags, s2, turn);
    if (s_bt.hits == 1)
        pe_mod_log("pe_rebalance", "first gun hit: shots=%u power %d->%d defense %u->%u bonus=%d predicted=%d hp=%d",
                   shots, pb, want, d, nd, bonus, s_hit.pred, (int)enemy->hpAlive);
    return 0;
}

static int pe_on_enemy_hit_end(void *ctx, void *user) {
    (void)ctx; (void)user;
    pe_restore_hit();
    pe_aa_fix();
    return 0;
}

static int pe_on_battle_update(void *ctx, void *user) {
    (void)ctx;
    (void)user;
    pe_restore_hit();
    pe_aa_fix();
    if (s_aa.idle < 1000) ++s_aa.idle;
    return 0;
}

/* ---- per battle ---- */
static void pe_battle_start(void) {
    memset(&s_bt, 0, sizeof s_bt);
    memset(&s_aa, 0, sizeof s_aa);
    memset(s_hq, 0, sizeof s_hq);
    memset(s_reach, 0, sizeof s_reach);
    memset(s_poison_written, 0, sizeof s_poison_written);
    s_chk_logged = 0;
    s_reach_frames = 0;
    s_battle_frames = 0;
    s_prev_hp = -1;
    s_refund_frac = 0;
}

/* The actor's core is one of the battle's enemy slots (other actors' cores are other records). */
static const EnemyCombatant *pe_enemy_core(const BattleEntity *e) {
    int i;
    for (i = 0; i < PE_ENEMY_SLOTS; ++i)
        if (e->core == &g_BattleEnemySlots[i].combatant) return g_BattleEnemySlots[i].active ? e->core : NULL;
    return NULL;
}

/* Reach: while Aya's bar is full, how far the nearest live enemy is against the gun's range. */
static void pe_note_reach(void) {
    BattleEntity *player = pe_player(), *e;
    Combatant *aya = pe_aya_core();
    unsigned best = 0xFFFFFFFFu;
    int guard;
    ++s_reach_frames;
    if (!player || !aya || aya->atb < 9000u || !aya->action || aya->action->range <= 0) return;
    e = (BattleEntity *)g_FieldActorListHead;
    for (guard = 0; e && guard < 64; ++guard, e = e->next) {
        const EnemyCombatant *core = pe_enemy_core(e);
        unsigned pct;
        if (e == player || !core) continue;
        if ((e->entityFlags & 0x2040u) == 0x40u || (e->entityFlags & 0x4000u) || core->hpAlive <= 0) continue;
        pct = (unsigned)Battle_CalcDistToPlayer(e, player) * 100u / (unsigned)aya->action->range;
        if (pct < best) best = pct;
    }
    if (best == 0xFFFFFFFFu) return;
    s_reach_range = (unsigned)aya->action->range;
    ++s_reach[best <= 100u ? 0 : best <= 150u ? 1 : best <= 180u ? 2 : 3];
}

static void pe_battle_end(void) {
    const unsigned ready = s_reach[0] + s_reach[1] + s_reach[2] + s_reach[3];
    unsigned total = 0;
    int b, r;
    pe_restore_hit();
    if (ready)
        pe_log("reach", "gun=0x%02X range=%u frames=%u ready=%u in=%u graze=%u far=%u out=%u",
               pe_gun_id(), s_reach_range, s_reach_frames, ready, s_reach[0], s_reach[1], s_reach[2], s_reach[3]);
    for (b = 0; b < 6; ++b) for (r = 0; r < 5; ++r) total += s_hq[b][r];
    if (total) {
        static const char *const names[6] = { "lt50", "50_80", "80_100", "100_120", "120_150", "gt150" };
        char buf[320];
        int n = snprintf(buf, sizeof buf, "gun=0x%02X range=%u", s_hq_gun, s_hq_range);
        for (b = 0; b < 6 && n < (int)sizeof buf; ++b) {
            const unsigned *q = s_hq[b];
            if (q[0] + q[1] + q[2] + q[3] + q[4])
                n += snprintf(buf + n, sizeof buf - (size_t)n, " %s=%u/%u/%u/%u", names[b], q[2], q[1], q[3], q[4]);
        }
        pe_log("hitq", "%s", buf);
    }
    if (!pe_on()) return;
    pe_log("modfx", "rules=%s hits=%u elem=%u(n%u/w%u/r%u) rof_boost=%u status_on=%u status_off=%u crit_up=%u quickdraw=%u poison_scaled=%u hp_refunded=%u"
           " rof_volley=%u gun_bonus=%u aa_capped=%u aa_topup=%u fast_volley=%u dmg_pred=%u/%u",
           s_rules_names[s_rules], s_bt.hits, s_bt.elem_hits, s_bt.elem_neutral, s_bt.elem_weak, s_bt.elem_resist,
           s_bt.rof_boost, s_bt.status_on, s_bt.status_off, s_bt.crit_up, s_bt.quickdraw, s_bt.poison_scaled,
           s_bt.hp_refunded, s_bt.rof_volley, s_bt.gun_bonus, s_bt.aa_capped, s_bt.aa_topup, s_bt.fast_volley,
           s_bt.pred_ok, s_bt.pred_ok + s_bt.pred_bad);
}

static void pe_battle_tick(void) {
    Combatant *aya = pe_aya_core();
    int guard = 100, hp, i;
    if (!aya) return;
    ++s_battle_frames;
    /* Quickdraw: top up the game's stat22 % chance, once, early in the battle. */
    if (s_battle_frames == 2 && s_quickdraw > 0 && pe_rec_has_effect(pe_equipped(0), 15)) {
        const int st = aya->stat22;
        int want = st + s_quickdraw;
        if (want > 90) want = 90;
        if (aya->atb < 9000u && st < 100 && want > st && pe_roll((want - st) * 100 / (100 - st))) {
            aya->atb = 9000;
            ++s_bt.quickdraw;
        }
    }
    /* Poison strength: each enemy's periodicDamage once (never compounded). */
    if (s_poison == 100) pe_restore_poison();
    else {
        for (i = 0; i < PE_ENEMY_SLOTS; ++i) {
            EnemyCombatant *core = &g_BattleEnemySlots[i].combatant;
            int p, np;
            if (!g_BattleEnemySlots[i].active) { s_poison_written[i] = s_poison_original[i] = 0; continue; }
            p = core->periodicDamage;
            if (p <= 0) { s_poison_written[i] = s_poison_original[i] = 0; continue; }
            if (!s_poison_written[i] || p != s_poison_written[i]) s_poison_original[i] = (s16)p;
            np = s_poison_original[i] * s_poison / 100;
            if (np < 1) np = 1;
            if (np > 0x7FFF) np = 0x7FFF;
            if (p == np) continue;
            core->periodicDamage = (s16)np;
            s_poison_written[i] = (int16_t)np;
            ++s_bt.poison_scaled;
        }
    }
    /* Damage taken with Attack Down / Slow Active Time armor: HP refunded as hits land. */
    if (pe_rec_has_effect(pe_equipped(1), 15)) guard = guard * s_guard_attack_down / 100;
    if (pe_rec_has_effect(pe_equipped(1), 13)) guard = guard * s_guard_slow_at / 100;
    hp = (s16)aya->curHP;
    if (guard < 100 && s_prev_hp > 0 && hp > 0 && hp < s_prev_hp) {
        int back;
        s_refund_frac += (s_prev_hp - hp) * (100 - guard);
        back = s_refund_frac / 100;
        s_refund_frac %= 100;
        if (back > 0) {
            int nh = hp + back;
            if (nh > (s16)g_AyaSaveState.max_hp) nh = (s16)g_AyaSaveState.max_hp;
            aya->curHP = (u16)nh;
            s_bt.hp_refunded += (unsigned)(nh - hp);
            s_prev_hp = nh;
            return;
        }
    }
    s_prev_hp = hp;
}

/* ---- R-E fast volleys for installed RoF 5 / 7 / 10 ----
 * Each extra bullet replays Aya's follow-up clip (action modes 7 / 9 / 11, Battle_StepCharacterAction);
 * the bullet's hit lands when g_BattleHitPending clears, and the next bullet starts only when the
 * clip reaches its last frame. Between bullets the action's hold (animMode[2], copied to
 * g_BattleActionAnimModes[2] at the turn start) replays the aim clip. While on: once a follow-up
 * clip's hit has landed (and its damage: no enemy still carries the hit mark 0x6000), the rest of
 * the clip is skipped, and the hold reads 0. Off: the hold is the game's table value again. */
static const uint16_t s_follow_x100[9] = { 100, 250, 750, 100, 375, 625, 250, 250, 100 };

static int pe_fast_volley_gun(unsigned *kind) {
    const ItemDataRecord *rec = pe_equipped(0);
    if (!rec) return 0;
    *kind = rec->kind;
    return *kind < 9u && (pe_installed_rof(5u) || pe_installed_rof(7u) || pe_installed_rof(10u));
}

/* Only undo values this feature actually replaced. Vanilla and disabled
 * settings must never copy factory animation rows over authored actions. */
static struct {
    BattleAction *action;
    u8 hold, global_hold;
    int global_owned;
} s_fast_hold;

static void pe_fast_volley_tick(int battle) {
    unsigned kind = 0, mode;
    const int on = battle && pe_on() && s_fast_volley && pe_fast_volley_gun(&kind);
    Combatant *aya = pe_aya_core();
    BattleEntity *player = pe_player();
    int i;
    BattleAction *a = aya ? aya->action : NULL;
    const unsigned id = a ? a->actionCode.bytes.actionIdByte : 9u;
    if (s_fast_hold.action && (!on || a != s_fast_hold.action || id != kind)) {
        if (a == s_fast_hold.action) {
            if (a->animMode[2] == 0) a->animMode[2] = s_fast_hold.hold;
            if (s_fast_hold.global_owned && g_ActiveActor == aya && g_BattleActionAnimModes[2] == 0)
                g_BattleActionAnimModes[2] = s_fast_hold.global_hold;
        }
        memset(&s_fast_hold, 0, sizeof s_fast_hold);
    }
    if (on && a && id < 9u && id == kind) {
        if (a != s_fast_hold.action || a->animMode[2] != 0) {
            s_fast_hold.action = a;
            s_fast_hold.hold = a->animMode[2];
        }
        a->animMode[2] = 0;
        if (g_ActiveActor == aya) {
            if (!s_fast_hold.global_owned || g_BattleActionAnimModes[2] != 0)
                s_fast_hold.global_hold = g_BattleActionAnimModes[2];
            s_fast_hold.global_owned = 1;
            g_BattleActionAnimModes[2] = 0;
        }
    }
    if (!on || !player || s_follow_x100[kind] <= 100u) return;
    mode = player->actionMode;
    if (mode != 7u && mode != 9u && mode != 11u) return;
    if (g_BattleHitPending) return;                       /* this bullet's hit hasn't landed yet */
    for (i = 0; i < PE_ENEMY_SLOTS; ++i)                  /* ...nor its damage */
        if (g_BattleEnemySlots[i].active && (g_BattleEnemySlots[i].combatant.coreFlags & 0x6000u)) return;
    if ((unsigned)(player->animFrame >> 16) >= player->animLastFrame) return;
    player->animFrame = (s32)player->animLastFrame << 16; /* the next advance makes animPrev = last */
    ++s_bt.fast_volley;
}

/* Quitting mid-battle: write the battle's rows anyway. */
void pe_mod_effects_shutdown(void) {
    pe_fast_volley_tick(0);
    pe_restore_poison();
    if (!s_in_battle) return;
    pe_mod_log("pe_rebalance", "game closed in battle: %u gun hits, prediction %u/%u", s_bt.hits, s_bt.pred_ok,
               s_bt.pred_ok + s_bt.pred_bad);
    pe_battle_end();
    s_in_battle = 0;
}

/* For pe_weapon_power.c's DPS line. */
int pe_fx_attack_bonus(unsigned gun) { return pe_on() ? pe_gun_bonus(gun) : 0; }
int pe_fx_rof_volley_pct(void) { return pe_on() ? s_rof_volley : 0; }

/* ---- rules ---- */
static void pe_apply_rules(int rules) {
    int i;
    if (rules == RULES_CUSTOM) return;
    for (i = 0; i < PE_ROWS; ++i) *s_rows[i].value = rules == RULES_VANILLA ? s_rows[i].vanilla : s_rows[i].rebalanced;
}

static int pe_matches(int rules) {
    int i;
    for (i = 0; i < PE_ROWS; ++i)
        if (*s_rows[i].value != (rules == RULES_VANILLA ? s_rows[i].vanilla : s_rows[i].rebalanced)) return 0;
    return 1;
}

static void pe_mod_effects_tick(void) {
    int battle;
    if (s_rules != s_rules_shown) {                       /* the Rules row was changed */
        pe_apply_rules(s_rules);
        s_rules_shown = s_rules;
    } else if (s_rules != RULES_CUSTOM && !pe_matches(s_rules)) {
        s_rules = s_rules_shown = RULES_CUSTOM;           /* a value row was changed */
    } else if (s_rules == RULES_CUSTOM && pe_matches(RULES_REBALANCED)) {
        s_rules = s_rules_shown = RULES_REBALANCED;
    } else if (s_rules == RULES_CUSTOM && pe_matches(RULES_VANILLA)) {
        s_rules = s_rules_shown = RULES_VANILLA;
    }
    if (!pe_game_loaded()) {
        s_in_battle = 0;
        memset(&s_fast_hold, 0, sizeof s_fast_hold);
        memset(s_poison_written, 0, sizeof s_poison_written);
        return;
    }
    battle = pe_in_battle();
    if (battle && !s_in_battle) {
        pe_mod_log("pe_rebalance", "battle start (gun 0x%02X, rules %s)", pe_gun_id(), s_rules_names[s_rules]);
        pe_battle_start();
    }
    if (!battle && s_in_battle) {
        pe_mod_log("pe_rebalance", "battle end: %u gun hits, prediction %u/%u", s_bt.hits, s_bt.pred_ok,
                   s_bt.pred_ok + s_bt.pred_bad);
        pe_battle_end();
    }
    s_in_battle = battle;
    if (battle) pe_note_reach();
    if (battle && pe_on()) pe_battle_tick();
    else pe_restore_poison();
    pe_fast_volley_tick(battle);                          /* also restores the game's values when off */
}

void pe_mod_effects_register(const char *mod_id) {
    int i;
    /* Saved values restore as the rows register; the rules then follow from what they match. */
    pe_apply_rules(RULES_REBALANCED);
    pe_menu_register_choice(PE_PAGE_MOD_EFFECTS, "Rules", "Vanilla, Rebalanced, or Custom.", &s_rules, s_rules_names, 3);
    for (i = 0; i < PE_ROWS; ++i) {
        if (s_rows[i].choice)
            pe_menu_register_choice(PE_PAGE_MOD_EFFECTS, s_rows[i].label, s_rows[i].desc, s_rows[i].value, s_onoff, 2);
        else
            pe_menu_register_int(PE_PAGE_MOD_EFFECTS, s_rows[i].label, s_rows[i].desc, s_rows[i].value,
                                 s_rows[i].min, s_rows[i].max, s_rows[i].step);
    }
    /* Restore the selected preset after all saved individual rows have loaded. */
    pe_apply_rules(s_rules);
    s_rules = pe_matches(RULES_REBALANCED) ? RULES_REBALANCED : pe_matches(RULES_VANILLA) ? RULES_VANILLA : RULES_CUSTOM;
    s_rules_shown = s_rules;
    pe_menu_register_tick(pe_mod_effects_tick);
    pe_hook_add(PE_HOOK_ENEMY_HIT, pe_on_enemy_hit, NULL, PE_PRIORITY_NORMAL, mod_id);
    pe_hook_add(PE_HOOK_ENEMY_HIT_END, pe_on_enemy_hit_end, NULL, PE_PRIORITY_NORMAL, mod_id);
    pe_hook_add(PE_HOOK_BATTLE_UPDATE, pe_on_battle_update, NULL, PE_PRIORITY_NORMAL, mod_id);
}

#if PE_NATIVE_STATE_CPU_COMPONENTS
#include "pe_checkpoint_rebalance.h"
const char*pe_rb_effects_blocker(void){if(s_hit.active)return "Finish the current Rebalance hit override first";if(s_fast_hold.action||s_fast_hold.global_owned)return "Finish the current Rebalance fast-volley action first";return "Rebalance settings or typed battle bookkeeping are invalid";}
int pe_rb_effects_walk(const unsigned char*in,unsigned char*out,int apply){if((!in&&!out)||(s_hit.active||s_fast_hold.action||s_fast_hold.global_owned))return 0;if(apply&&(!in||!pe_rb_effects_walk(in,0,0)))return 0;PeCheckpointWire w={in,out,0,apply,1};
PE_CK_S(w,s_rules,0,2,1);PE_CK_S(w,s_rules_shown,-1,2,0);for(int i=0;i<PE_ROWS;i++){int value=*s_rows[i].value;PE_CK_S(w,value,s_rows[i].min,s_rows[i].max,1);}PE_CK_U(w,s_rng,0,UINT32_MAX,0);PE_CK_S(w,s_in_battle,0,1,0);PE_CK_S(w,s_battle_frames,0,INT_MAX,0);PE_CK_S(w,s_prev_hp,INT16_MIN,INT16_MAX,0);PE_CK_S(w,s_refund_frac,0,99,0);for(int i=0;i<PE_ENEMY_SLOTS;i++){PE_CK_S(w,s_poison_written[i],INT16_MIN,INT16_MAX,0);PE_CK_S(w,s_poison_original[i],INT16_MIN,INT16_MAX,0);}PE_CK_U(w,s_bt.hits,0,UINT32_MAX,0);PE_CK_U(w,s_bt.elem_hits,0,UINT32_MAX,0);PE_CK_U(w,s_bt.elem_neutral,0,UINT32_MAX,0);PE_CK_U(w,s_bt.elem_weak,0,UINT32_MAX,0);PE_CK_U(w,s_bt.elem_resist,0,UINT32_MAX,0);PE_CK_U(w,s_bt.rof_boost,0,UINT32_MAX,0);PE_CK_U(w,s_bt.rof_volley,0,UINT32_MAX,0);PE_CK_U(w,s_bt.gun_bonus,0,UINT32_MAX,0);PE_CK_U(w,s_bt.aa_capped,0,UINT32_MAX,0);PE_CK_U(w,s_bt.aa_topup,0,UINT32_MAX,0);PE_CK_U(w,s_bt.fast_volley,0,UINT32_MAX,0);PE_CK_U(w,s_bt.pred_ok,0,UINT32_MAX,0);PE_CK_U(w,s_bt.pred_bad,0,UINT32_MAX,0);PE_CK_U(w,s_bt.status_on,0,UINT32_MAX,0);PE_CK_U(w,s_bt.status_off,0,UINT32_MAX,0);PE_CK_U(w,s_bt.crit_up,0,UINT32_MAX,0);PE_CK_U(w,s_bt.quickdraw,0,UINT32_MAX,0);PE_CK_U(w,s_bt.poison_scaled,0,UINT32_MAX,0);PE_CK_U(w,s_bt.hp_refunded,0,UINT32_MAX,0);for(int i=0;i<6;i++)for(int j=0;j<5;j++)PE_CK_U(w,s_hq[i][j],0,UINT32_MAX,0);PE_CK_U(w,s_hq_range,0,UINT32_MAX,0);PE_CK_U(w,s_hq_gun,0,UINT32_MAX,0);for(int i=0;i<4;i++)PE_CK_U(w,s_reach[i],0,UINT32_MAX,0);PE_CK_U(w,s_reach_frames,0,UINT32_MAX,0);PE_CK_U(w,s_reach_range,0,UINT32_MAX,0);if(apply&&w.ok){memset(&s_hit,0,sizeof s_hit);memset(&s_fast_hold,0,sizeof s_fast_hold);}
return pe_rb_finish(&w);}
#endif
