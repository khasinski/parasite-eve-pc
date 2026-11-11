/* The aggregate row coordinates independent feature settings through callbacks. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "pe_combat_presets.h"
#include "pe_mod_menu.h"
#include "pe_pages.h"

static PeMenuGet preset_get;
static PeMenuSet preset_set;
static int weapons = 1, powers = 1, melee = 1;
static int rebalance_on = 1, melee_on = 1;
static int weapon_applies, power_applies, melee_applies;

int pe_mod_is_running(const char *owner)
{
    if (!strcmp(owner, "rebalance")) return rebalance_on;
    if (!strcmp(owner, "melee")) return melee_on;
    return 0;
}

int pe_menu_register_live_choice(const char *page, const char *label, const char *description,
                                const char *const *names, unsigned count, PeMenuGet get, PeMenuSet set)
{
    assert(!strcmp(page, PE_PAGE_COMBAT));
    assert(!strcmp(label, "Combat preset") && description[0]);
    assert(count == 3 && !strcmp(names[0], "Original") && !strcmp(names[1], "Enhanced"));
    preset_get = get;
    preset_set = set;
    return 0;
}

static void apply_weapons(int preset) { weapons = preset; weapon_applies++; }
static void apply_powers(int preset) { powers = preset; power_applies++; }
static void apply_melee(int preset) { melee = preset; melee_applies++; }
static int matches_weapons(int preset) { return weapons == preset; }
static int matches_powers(int preset) { return powers == preset; }
static int matches_melee(int preset) { return melee == preset; }

int main(void)
{
    assert(pe_combat_preset_add(NULL, apply_weapons, matches_weapons) == -1);
    assert(!pe_combat_preset_add("rebalance", apply_weapons, matches_weapons));
    assert(!pe_combat_preset_add("rebalance", apply_powers, matches_powers));
    assert(!pe_combat_preset_add("melee", apply_melee, matches_melee));
    pe_combat_presets_register();

    assert(preset_get() == PE_COMBAT_ENHANCED);
    assert(preset_set(PE_COMBAT_ORIGINAL));
    assert(weapons == 0 && powers == 0 && melee == 0);
    assert(preset_get() == PE_COMBAT_ORIGINAL);
    assert(weapon_applies == 1 && power_applies == 1 && melee_applies == 1);

    assert(preset_set(PE_COMBAT_ENHANCED));
    assert(weapons == 1 && powers == 1 && melee == 1);
    powers = 7;
    assert(preset_get() == PE_COMBAT_CUSTOM);
    assert(preset_set(PE_COMBAT_CUSTOM));
    assert(weapons == 1 && powers == 7 && melee == 1);

    rebalance_on = 0;
    assert(preset_set(PE_COMBAT_ORIGINAL));
    assert(weapons == 1 && powers == 7 && melee == 0);
    assert(weapon_applies == 2 && power_applies == 2 && melee_applies == 3);
    assert(preset_get() == PE_COMBAT_ORIGINAL);
    rebalance_on = 1;
    assert(preset_get() == PE_COMBAT_CUSTOM);

    assert(preset_set(PE_COMBAT_ENHANCED));
    assert(preset_get() == PE_COMBAT_ENHANCED);
    assert(!preset_set(-1) && !preset_set(3));
    assert(preset_get() == PE_COMBAT_ENHANCED);
    rebalance_on = melee_on = 0;
    assert(preset_get() == PE_COMBAT_ORIGINAL);

    puts("PASS combined combat presets, multiple features per module, Custom and disabled modules");
    return 0;
}
