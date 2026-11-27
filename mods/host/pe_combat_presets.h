#ifndef PE_COMBAT_PRESETS_H
#define PE_COMBAT_PRESETS_H

enum {
    PE_COMBAT_ORIGINAL,
    PE_COMBAT_ENHANCED,
    PE_COMBAT_CUSTOM
};

typedef void (*PeCombatApply)(int preset);
typedef int (*PeCombatMatches)(int preset);

/* Features own their settings and contribute only their apply/match callbacks.
 * Standalone feature fixtures can compile without the aggregate preset host. */
#ifdef PE_COMBAT_PRESETS
int pe_combat_preset_add(const char *owner, PeCombatApply apply, PeCombatMatches matches);
#else
static inline int pe_combat_preset_add(const char *owner, PeCombatApply apply, PeCombatMatches matches)
{
    (void)owner;
    (void)apply;
    (void)matches;
    return 0;
}
#endif

void pe_combat_presets_register(void);

#endif
