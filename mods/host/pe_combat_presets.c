#include <stddef.h>

#include "pe_combat_presets.h"
#include "pe_mod_host.h"
#include "pe_mod_menu.h"
#include "pe_pages.h"

enum { PE_COMBAT_PARTS = 16 };
static struct {
    const char *owner;
    PeCombatApply apply;
    PeCombatMatches matches;
} s_parts[PE_COMBAT_PARTS];
static unsigned s_part_count;
static int s_custom_selected;
static const char *const s_names[] = {"Original", "Enhanced", "Custom"};

int pe_combat_preset_add(const char *owner, PeCombatApply apply, PeCombatMatches matches)
{
    if (!owner || !apply || !matches || s_part_count == PE_COMBAT_PARTS)
        return -1;
    s_parts[s_part_count].owner = owner;
    s_parts[s_part_count].apply = apply;
    s_parts[s_part_count].matches = matches;
    s_part_count++;
    return 0;
}

static int pe_combat_get(void)
{
    int original = 1, enhanced = 1;
    unsigned i;

    if (s_custom_selected)
        return PE_COMBAT_CUSTOM;
    for (i = 0; i < s_part_count; i++) {
        if (!pe_mod_is_running(s_parts[i].owner))
            continue;
        original &= s_parts[i].matches(PE_COMBAT_ORIGINAL);
        enhanced &= s_parts[i].matches(PE_COMBAT_ENHANCED);
    }
    if (original)
        return PE_COMBAT_ORIGINAL;
    return enhanced ? PE_COMBAT_ENHANCED : PE_COMBAT_CUSTOM;
}

static int pe_combat_set(int preset)
{
    unsigned i;

    if (preset < PE_COMBAT_ORIGINAL || preset > PE_COMBAT_CUSTOM)
        return 0;
    s_custom_selected = preset == PE_COMBAT_CUSTOM;
    if (s_custom_selected)
        return 1;
    for (i = 0; i < s_part_count; i++)
        if (pe_mod_is_running(s_parts[i].owner))
            s_parts[i].apply(preset);
    return 1;
}

void pe_combat_presets_register(void)
{
    pe_menu_register_live_choice(PE_PAGE_COMBAT, "Combat preset",
        "Apply Original or Enhanced to enabled combat modules; edit settings for Custom.",
        s_names, 3, pe_combat_get, pe_combat_set);
}
