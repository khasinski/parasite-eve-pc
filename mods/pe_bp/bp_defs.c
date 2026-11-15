/* Definitions, prices and battle numbers (bp_defs.h). */
#include "bp_defs.h"

/* Section I and Fortitude: 3 ranks of +10% (75/100/125); Fortitude 100/150/200. */
static const BpTrackDef defense_defs[BP_DEFENSE_TRACKS] = {
    {"poison", "Poison", {75, 100, 125}},
    {"confusion", "Confusion", {75, 100, 125}},
    {"darkness", "Darkness", {75, 100, 125}},
    {"stiffness", "Stiffness", {75, 100, 125}},
    {"fortitude", "Fortitude", {100, 150, 200}},
};
/* Vanilla armor-mod chances (battle_ailment.h): Poison 70, Confusion 60, Darkness 60,
 * Stiffness lock 50 / slow 70. */
int bp_armor_chance(int track, int slow) {
    static const int chance[BP_RESIST_TRACKS] = {70, 60, 60, 50};
    if (track < 0 || track >= BP_RESIST_TRACKS) return 0;
    return track == 3 && slow ? 70 : chance[track];
}

#define POWER(n) (1u << (n))
/* Section F: tiers A 150/250/350, B 100/200/300, C 50/100/150, Scan one rank of 100. */
static const BpLineDef line_defs[BP_MASTERY_LINES] = {
    {"heal", "Heal", 'A', 3, POWER(0) | POWER(1) | POWER(2), 60,
     {"Healing +15%", "Healing +15%, cost -15%", "Healing +30%, cost -15%"},
     "staying in fights on PE instead of items", "long fights without a save phone nearby"},
    {"detox", "Detox", 'C', 3, POWER(3), 100,
     {"Cost -25%", "Cost -25%, one-use Poison immunity", "Cost -50%, one-use Poison immunity"},
     "not carrying Cure-P", "poison-heavy areas"},
    {"medic", "Medic", 'B', 3, POWER(4), 600,
     {"Cost -15%", "Cost -15%, heals 10% max HP", "Cost -30%, heals 10% max HP"},
     "one answer to every ailment", "enemies that stack several ailments"},
    {"barrier", "Barrier", 'B', 3, POWER(5), 0,
     {"Costs 1/4 of max PE", "Costs 1/5 of max PE", "Costs 1/6 of max PE"},
     "spending PE to save HP", "big single hits you can see coming"},
    {"energy_shot", "Energy Shot", 'B', 3, POWER(6), 0,
     {"Damage +10%", "Damage +20%", "Damage +20%, +10% more above half PE"},
     "fighting with PE instead of guns", "opening fights on a full PE bar"},
    {"scan", "Scan", 'S', 1, POWER(7), 50,
     {"Scan costs no PE", NULL, NULL},
     "knowing weaknesses before committing", "first meetings and multi-part bosses"},
    {"slow", "Slow", 'B', 3, POWER(8), 90,
     {"Neutral-enemy chance 60%", "Neutral-enemy chance 70%", "Neutral-enemy chance 80%"},
     "slowing a dangerous enemy down", "neutral enemies; weak ones are already sure"},
    {"haste", "Haste", 'A', 3, POWER(9), 200,
     {"Duration +25%", "Duration +50%", "Duration +50%, cost -25%"},
     "moving first and dodging", "fights decided by extra turns"},
    {"confuse", "Confuse", 'B', 3, POWER(10), 150,
     {"Neutral-enemy chance 60%", "Neutral-enemy chance 70%", "Neutral-enemy chance 80%"},
     "turning enemies against each other", "neutral enemies in groups"},
    {"gene_heal", "Gene Heal", 'B', 3, POWER(11), 400,
     {"Cost -15%", "Cost -15%, 2 HP per tick", "Cost -30%, 2 HP per tick"},
     "steady healing over time", "long fights with PE to spare"},
    {"preraise", "Preraise", 'B', 3, POWER(12), 1000,
     {"Cost -15%", "Cost -15%, revive with half PE", "Cost -30%, revive with half PE and 75% HP"},
     "a safety net", "boss fights and risky play"},
    {"full_recover", "Full Recover", 'B', 3, POWER(18), 1300,
     {"Cost -10%", "Cost -20%", "Cost -30%"},
     "a big emergency button", "when everything has gone wrong at once"},
};

/* Cast-cost cut (percent) at ranks 1..3; Scan's one rank is free. Barrier is in max-PE parts. */
static const uint8_t line_cut[BP_MASTERY_LINES][BP_MAX_RANK] = {
    {0, 15, 15}, {25, 25, 50}, {15, 15, 30}, {0, 0, 0}, {0, 0, 0}, {100, 100, 100},
    {0, 0, 0}, {0, 0, 25}, {0, 0, 0}, {15, 15, 30}, {15, 15, 30}, {10, 20, 30},
};
int bp_line_cost(int line, int rank, int pe_light, int max_pe) {
    const BpLineDef *d = bp_line_def(line);
    int cost;
    if (!d) return -1;
    if (line == 3) {   /* Barrier: 1/3 of max PE, then 1/4, 1/5, 1/6 */
        if (max_pe <= 0) return -1;
        cost = max_pe / (3 + (rank > 3 ? 3 : rank));
    } else {
        if (!d->vanilla_drain) return -1;
        cost = d->vanilla_drain * (100 - (rank ? line_cut[line][(rank > 3 ? 3 : rank) - 1] : 0)) / 100;
    }
    return pe_light ? cost * 2 / 3 : cost;
}
const BpTrackDef *bp_defense_def(int t) { return t >= 0 && t < BP_DEFENSE_TRACKS ? &defense_defs[t] : NULL; }
const BpLineDef *bp_line_def(int l) { return l >= 0 && l < BP_MASTERY_LINES ? &line_defs[l] : NULL; }

const char *bp_gear_stat_name(int armor, int stat) {
    static const char *const gun[3] = {"Attack", "Range", "Bullets"};
    static const char *const vest[3] = {"Defense", "P. Energy", "Critical"};
    return stat >= 0 && stat < 3 ? (armor ? vest : gun)[stat] : "";
}

unsigned bp_track_price(int t, int rank) {
    const BpTrackDef *d = bp_defense_def(t);
    return d && rank >= 1 && rank <= BP_MAX_RANK ? d->price[rank - 1] : 0;
}
unsigned bp_line_price(int l, int rank) {
    const BpLineDef *d = bp_line_def(l);
    if (!d || rank < 1 || rank > d->ranks) return 0;
    switch (d->tier) {
    case 'A': return 50u + 100u * (unsigned)rank;   /* 150 / 250 / 350 */
    case 'B': return 100u * (unsigned)rank;         /* 100 / 200 / 300 */
    case 'C': return 50u * (unsigned)rank;          /*  50 / 100 / 150 */
    default:  return 100u;                          /* Scan: one rank */
    }
}
unsigned bp_track_spent(int t, int rank) { unsigned n = 0; for (int i = 1; i <= rank; ++i) n += bp_track_price(t, i); return n; }
unsigned bp_line_spent(int l, int rank) { unsigned n = 0; for (int i = 1; i <= rank; ++i) n += bp_line_price(l, i); return n; }
int bp_effective_resist(int armor, int rank) {
    int bp = 10 * rank;
    return 100 - (100 - armor) * (100 - bp) / 100;
}
unsigned bp_track_feature(int t) { return t == BP_FORTITUDE ? BP_FEATURE_FORTITUDE : BP_FEATURE_RESISTANCE; }

/* ---- battle effects ---- */
int bp_fortitude_charges(int rank) { return rank <= 0 ? 0 : rank == 1 ? 1 : 2; }
BpFortitudePlan bp_fortitude_plan(int effect, int hp, int barrier, int rank, int mode, int used) {
    BpFortitudePlan p = {0, 0, hp, hp};
    if (effect != BP_FX_HALVE_HP && effect != BP_FX_HP_TO_1) return p;
    /* What the game does without protection (enemy_actions.c). */
    if (effect == BP_FX_HALVE_HP) p.vanilla_hp = barrier || hp < 2 ? hp : hp >> 1;
    else p.vanilla_hp = hp >= 2 ? 1 : hp == 1 ? -1 : hp;
    p.result_hp = p.vanilla_hp;
    if (used >= bp_fortitude_charges(rank) || hp <= 0) return p;
    if (effect == BP_FX_HALVE_HP && mode == BP_FORT_SAVE_FOR_HP1) return p;
    if (rank >= 3 && used == 0) {
        p.negate = 1; p.result_hp = hp;
    } else if (effect == BP_FX_HALVE_HP) {
        p.result_hp = barrier || hp < 2 ? hp : hp - hp / 4;
    } else {
        p.result_hp = hp / 4 > 1 ? hp / 4 : 1;
    }
    p.use = p.result_hp > p.vanilla_hp;
    if (!p.use) { p.negate = 0; p.result_hp = p.vanilla_hp; }
    return p;
}
int bp_line_for_power(int power) {
    static const signed char map[20] = {0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, -1, -1, -1, -1, -1, 11, -1};
    return power >= 0 && power < 20 ? map[power] : -1;
}
int bp_line_cut(int line, int rank) {
    if (line < 0 || line >= BP_MASTERY_LINES || rank <= 0) return 0;
    return line_cut[line][(rank > 3 ? 3 : rank) - 1];
}
int bp_heal_bonus(int rank) { return rank <= 0 ? 0 : rank < 3 ? 15 : 30; }
int bp_haste_bonus(int rank) { return rank <= 0 ? 0 : rank == 1 ? 25 : 50; }
int bp_neutral_chance(int rank) { return rank <= 0 ? 50 : 50 + 10 * (rank > 3 ? 3 : rank); }
/* 50 + 50% x reroll = chance, so reroll = 2 x (chance - 50): 20 / 40 / 60. */
int bp_neutral_reroll(int rank) { return (bp_neutral_chance(rank) - 50) * 2; }
int bp_energy_shot_bonus(int rank, int above_half) {
    if (rank <= 0) return 0;
    return rank == 1 ? 10 : rank == 2 ? 20 : 20 + (above_half ? 10 : 0);
}
