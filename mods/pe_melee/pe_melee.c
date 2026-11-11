#include <stddef.h>
#include <stdio.h>

#include "common.h"
#include "pe1/battle.h"
#include "pe1/battle_runtime.h"
#include "pe_mod_host.h"
#include "pe_mod_menu.h"
#include "pe_pages.h"
#include "pe_combat_presets.h"

#define MOD_ID "pe_melee"

static struct {
    const char *label;
    const char *description;
    int enabled;
} s_features[BATTLE_MELEE_FEATURE_COUNT] = {
    [BATTLE_MELEE_FOLLOW_UP] = {
        "Flank follow-up", "Press Cross within 0.8 seconds of an eligible side or enabled rear hit.", 1
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
    [BATTLE_MELEE_REAR_BONUS] = {
        "Rear hit bonus", "Rear club hits deal 25% extra damage and can offer a flank follow-up.", 1
    },
};

int Battle_MeleeFeatureEnabled(BattleMeleeFeature feature)
{
    return s_features[feature].enabled && pe_mod_is_running(MOD_ID);
}

static void melee_preset_apply(int preset)
{
    int i;
    for (i = 0; i < BATTLE_MELEE_FEATURE_COUNT; i++)
        s_features[i].enabled = preset == PE_COMBAT_ENHANCED && i != BATTLE_MELEE_SPARK;
}

static int melee_preset_matches(int preset)
{
    int i;
    for (i = 0; i < BATTLE_MELEE_FEATURE_COUNT; i++)
        if (s_features[i].enabled != (preset == PE_COMBAT_ENHANCED && i != BATTLE_MELEE_SPARK))
            return 0;
    return 1;
}

static int melee_init(void)
{
    int i;
    int failed;
    char key[192], legacy[192];

    pe_combat_preset_add(MOD_ID, melee_preset_apply, melee_preset_matches);
    pe_menu_begin(PE_MENU_PAGE);
    failed = pe_menu_page(PE_PAGE_MELEE, "Club positioning, follow-ups, reach and swing speed.", 15) != 0;
    for (i = 0; i < BATTLE_MELEE_FEATURE_COUNT; i++) {
        snprintf(key, sizeof(key), "%s/%s/%s", PE_MENU_PAGE, PE_PAGE_MELEE, s_features[i].label);
        snprintf(legacy, sizeof(legacy), "Melee Combat/%s", s_features[i].label);
        pe_menu_alias_setting(key, legacy);
        if (pe_menu_register_toggle(PE_PAGE_MELEE, s_features[i].label, s_features[i].description,
                                    &s_features[i].enabled, NULL) != 0)
            failed = 1;
    }
    pe_menu_begin(NULL);
    return failed;
}

const PeMod pe_mod_pe_melee = {MOD_ID, "Melee Combat", "0.1", NULL, melee_init, NULL};
