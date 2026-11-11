#include <stddef.h>

#include "common.h"
#include "pe1/battle.h"
#include "pe1/battle_runtime.h"
#include "pe_mod_host.h"
#include "pe_mod_menu.h"

#define MOD_ID "pe_melee"

static struct {
    const char *label;
    const char *description;
    int enabled;
} s_features[BATTLE_MELEE_FEATURE_COUNT] = {
    [BATTLE_MELEE_FOLLOW_UP] = {
        "Flank follow-up", "Press Cross within 0.8 seconds of an eligible club side hit to backhand.", 1
    },
    [BATTLE_MELEE_FAST_WINDUP] = {
        "Faster windup", "Speed up club windups throughout the game.", 1
    },
    [BATTLE_MELEE_BODY_REACH] = {
        "Body-based club reach", "Measure all club swings to the enemy's body as well as its aim point.", 1
    },
    [BATTLE_MELEE_REACH_CANCEL] = {
        "Cancel out-of-reach follow-ups", "Skip the backhand when the target is predicted to be out of reach.", 1
    },
    [BATTLE_MELEE_SPARK] = {
        "Backhand hit spark", "Draw a blue-white spark on a successful follow-up hit.", 0
    },
};

int Battle_MeleeFeatureEnabled(BattleMeleeFeature feature)
{
    return s_features[feature].enabled && pe_mod_is_running(MOD_ID);
}

static int melee_init(void)
{
    int i;
    int failed = 0;

    pe_menu_begin("Melee Combat");
    for (i = 0; i < BATTLE_MELEE_FEATURE_COUNT; i++) {
        if (pe_menu_register_toggle(NULL, s_features[i].label, s_features[i].description,
                                    &s_features[i].enabled, NULL) != 0)
            failed = 1;
    }
    pe_menu_begin(NULL);
    return failed;
}

const PeMod pe_mod_pe_melee = {MOD_ID, "Melee Combat", "0.1", NULL, melee_init, NULL};
