#ifndef PE_RE_BATTLE_RULES_H
#define PE_RE_BATTLE_RULES_H

#include "pe_re_session.h"

typedef struct {
    PeReEnemyPlacement lead;
    int effect;
    int needs_choice; /* The adapter may ask about the canister. */
} PeReBossFight;

typedef struct PeReBattleRules {
    const char *game;
    int max_foes;
    double floor_height;
    int (*combatant)(const PeReSession *, const PeReEnemyPlacement *);
    int (*pursues)(const PeReSession *, const PeReEnemyPlacement *);
    double (*encounter_radius)(const PeReSession *);
    int (*boss)(const PeReSession *, PeReBossFight *);
    int (*joins_boss)(const PeReSession *, const PeReBossFight *, const PeReEnemyPlacement *);
    void (*chase_step)(PeReSession *, PeReEnemyPlacement *, int, double x, double z, int floor);
} PeReBattleRules;

const PeReBattleRules *pe_re_battle_rules(const char *game);
extern const PeReBattleRules pe_re_battle_re2_rules;

#endif
