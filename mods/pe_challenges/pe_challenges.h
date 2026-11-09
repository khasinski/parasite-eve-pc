#ifndef PE_CHALLENGES_H
#define PE_CHALLENGES_H

/* Challenges: the private interface between the mod's files (no other mod includes this).
 *
 *   pe_challenges.c    the mod: starts every feature below
 *   pe_ch_run.c        the challenge table, the F1 rows, the run's save record, rows <-> run
 *   pe_ch_bundles.c    Bundles page: Purist, Nightmare, Clear all
 *   pe_ch_tabs.c       rules: No Parasite Energy, No items in battle, No escape
 *   pe_ch_damage.c     rules: Glass cannon, Tough crowd
 *   pe_ch_hardcore.c   rule: Hardcore (permadeath): a game over ends the run; its saves send you to the title
 *   pe_ch_assist.c     cheats and playtest assists mark the run assisted
 *   pe_ch_runlog.c     battles won, play time, and the per-run file beside the preferences
 *                      (knockouts, dead Hardcore runs)
 *   pe_ch_summary.c    the Run, Warnings and Run summary info rows
 *
 * A new challenge: an enum entry here, a line in the table (pe_ch_run.c), and its rule in a file
 * of its own with a pe_ch_<rule>_register() called from pe_challenges.c. The record's masks have
 * room for 32. */

#include <stdint.h>

enum {
    CH_NO_PE,       /* no Parasite Energy: the battle tab is gone and every power is dimmed */
    CH_NO_ITEMS,    /* no Item tab in battle */
    CH_NO_ESCAPE,   /* no Escape tab in battle */
    CH_GLASS,       /* Aya takes x2 from hits and contact, deals x1.5 */
    CH_TOUGH,       /* enemies take less damage: as if they had x1.5 / x2 HP (a level row) */
    CH_HARDCORE,    /* permadeath; New Game only, and it locks every challenge of its run */
    CH_COUNT
};
#define CH_BIT(c) (1u << (c))
#define CH_ALL    (CH_BIT(CH_COUNT) - 1u)
#define CH_HARDCORE_BIT CH_BIT(CH_HARDCORE)

#define CH_FLAG_CHEATS 0x1u   /* a cheat or playtest assist was used while a challenge was on */

#define CH_PAGE         "Challenges"
#define CH_PAGE_BUNDLES "Bundles"
#define CH_PAGE_RUN     "Run summary"

/* The run, saved with the game (record "pe_challenges.run"). */
typedef struct PeChallengeRun {
    uint32_t active;    /* challenges on now */
    uint32_t ever;      /* every challenge that was on at some point this run */
    uint32_t dropped;   /* turned off (or lowered) mid-run: the run is assisted */
    uint32_t flags;     /* CH_FLAG_* */
    uint32_t run_id;    /* random, nonzero once a challenge was on; keys the knockout count */
    uint32_t battles;   /* battles won with a challenge on */
    uint32_t seconds;   /* play time with a challenge on (F1 and pauses excluded) */
    uint8_t tough;      /* Tough crowd level now: 0 off, 1 x1.5, 2 x2 */
    uint8_t tough_max;  /* highest level this run */
    uint8_t pad[2];
} PeChallengeRun;
_Static_assert(sizeof(PeChallengeRun) == 32, "PeChallengeRun layout changed: raise its record version");

extern PeChallengeRun g_ch_run;

static inline int pe_ch_on(int c) { return (int)((g_ch_run.active >> c) & 1u); }
static inline int pe_ch_assisted(void) { return g_ch_run.dropped || (g_ch_run.flags & CH_FLAG_CHEATS); }

/* pe_ch_run.c */
int pe_ch_run_register(const char *mod_id);       /* the record, the rows, load / new game / sync */
unsigned pe_ch_rows_mask(void);                    /* what the rows ask for (the next pick at the title) */
void pe_ch_rows_turn_on(unsigned mask, int tough); /* bundles: only ever turns challenges on */
void pe_ch_rows_clear(void);
int pe_ch_underway(void);                          /* past the grace period: lowering counts */
int pe_ch_hardcore_locked(void);                   /* a Hardcore run: nothing can be lowered */
void pe_ch_names(unsigned mask, char *out, unsigned size);   /* "No PE, Glass cannon" or "None" */

/* pe_ch_bundles.c */
int pe_ch_bundles_register(void);

/* rules */
void pe_ch_tabs_register(const char *mod_id);
void pe_ch_damage_register(const char *mod_id);
void pe_ch_hardcore_register(const char *mod_id);

/* pe_ch_assist.c */
void pe_ch_assist_register(const char *mod_id);
void pe_ch_assist_reset(void);                     /* a load or a new game: tell again next time */
const char *pe_ch_cheat_row_on(void);              /* a switched-on cheat / assist row's label, or NULL */

/* pe_ch_runlog.c */
void pe_ch_runlog_register(const char *mod_id);
unsigned pe_ch_knockouts(void);
int pe_ch_run_is_dead(uint32_t run_id);           /* a Hardcore run that ended in a game over */
void pe_ch_run_mark_dead(uint32_t run_id);        /* written to the file at once */

/* pe_ch_summary.c */
int pe_ch_summary_register(void);

#endif
