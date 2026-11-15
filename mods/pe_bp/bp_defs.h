#ifndef PE_BP_DEFS_H
#define PE_BP_DEFS_H
/* BP overhaul definitions: the tracks and lines, their prices, and the battle numbers. Pure: no
 * game globals, no I/O. The design doc (PE_BP_Overhaul_Revised_2026-11-12.md) owns every number. */
#include "bp_config.h"
#include <stddef.h>
#include <stdint.h>

enum {
    BP_STEP_PRICE = 100,          /* vanilla: every gear / Aya stat step */
    BP_MAX = 99999,               /* vanilla clamp of g_AyaBonusPoints */
    BP_GEAR_BONUS_MAX = 999,      /* vanilla: a bonus stat stops below 999 */
    BP_RESIST_TRACKS = 4,         /* Poison, Confusion, Darkness, Stiffness */
    BP_FORTITUDE = BP_RESIST_TRACKS, /* the fifth defensive track */
    BP_DEFENSE_TRACKS = 5,
    BP_MASTERY_LINES = 12,
    BP_MAX_RANK = 3,
    BP_MAX_GEAR = 50,
    BP_FULL_RESET_BASE = 200, BP_FULL_RESET_STEP = 100, BP_FULL_RESET_CAP = 500,
    BP_TRACK_RESET_BASE = 50, BP_RESET_FEE_PER_100 = 5
};

typedef enum { BP_FORT_BOTH, BP_FORT_SAVE_FOR_HP1 } BpFortitudeMode;

typedef struct BpTrackDef {
    const char *id, *name;
    uint16_t price[BP_MAX_RANK];
} BpTrackDef;
typedef struct BpLineDef {
    const char *id, *name;
    char tier;                    /* 'A', 'B', 'C' or 'S' (single rank) */
    int ranks;
    uint32_t powers;              /* parasite_spell_flags bits that unlock the line */
    int vanilla_drain;            /* fixed PE drain of the (first) power, 0 if not fixed */
    const char *rank_text[BP_MAX_RANK];  /* full totals at each rank */
    const char *for_whom, *pays_off;
} BpLineDef;

const BpTrackDef *bp_defense_def(int track);   /* 0..4 */
const BpLineDef *bp_line_def(int line);        /* 0..11 */
/* The feature flag (bp_config.h) a defense track belongs to: Fortitude or Resistance. */
unsigned bp_track_feature(int track);
const char *bp_gear_stat_name(int armor, int stat);
int bp_armor_chance(int track, int slow);      /* vanilla armor-mod chance, percent */
unsigned bp_track_price(int track, int rank);  /* price of rank `rank` (1-based) */
unsigned bp_line_price(int line, int rank);
unsigned bp_track_spent(int track, int rank);  /* total BP for ranks 1..rank */
unsigned bp_line_spent(int line, int rank);
/* Real one-time PE cost at a Mastery rank (vanilla drain, PE Light x2/3), or -1 when the
 * power has no fixed drain. Rebalance's own costs are not in this number. */
int bp_line_cost(int line, int rank, int pe_light, int max_pe);
/* Effective resist chance, percent: 1 - (1 - armor)(1 - 10 x rank). */
int bp_effective_resist(int armor_percent, int rank);

/* ---- battle effects (pure numbers; pe_bp_battle.c applies them) ---- */
enum { BP_FX_HALVE_HP = 8, BP_FX_HP_TO_1 = 9 };
/* Fortitude for one Halve HP / HP to 1 application on Aya. use: spend a charge; negate: cancel
 * the whole effect (buff strip too); else soften to result_hp. A charge is used only when the
 * protected HP beats the unprotected one. */
typedef struct BpFortitudePlan { int use, negate, result_hp, vanilla_hp; } BpFortitudePlan;
BpFortitudePlan bp_fortitude_plan(int effect, int hp, int barrier, int rank, int mode, int charges_used);
int bp_fortitude_charges(int rank);
int bp_line_for_power(int power);                 /* Mastery line of a PE power index, or -1 */
int bp_line_cut(int line, int rank);              /* cast-cost cut, percent */
int bp_heal_bonus(int rank);                      /* Heal line: extra healing, percent */
int bp_haste_bonus(int rank);                     /* Haste line: extra duration, percent */
int bp_neutral_chance(int rank);                  /* Slow/Confuse vs neutral enemies, percent */
/* Chance (percent) of a second roll after the vanilla 50% roll failed, so the total equals
 * bp_neutral_chance(rank). */
int bp_neutral_reroll(int rank);
int bp_energy_shot_bonus(int rank, int pe_above_half);   /* percent */
#endif
