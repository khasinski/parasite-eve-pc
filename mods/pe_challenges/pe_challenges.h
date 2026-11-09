#ifndef PE_CHALLENGES_H
#define PE_CHALLENGES_H

/* Challenges: the private interface between the mod's files (no other mod includes this).
 *
 * Every challenge is a PeChallengeDef that lives in the file of its rule: its rows, what it takes
 * away from the battle menu, how it behaves in a run and its hooks. The rest of the mod reads the
 * definitions and knows nothing about any one challenge.
 *
 *   pe_challenges.c        the mod; the list of challenges (g_ch_defs), in F1 row order
 *   core
 *     pe_ch_run.c          the F1 rows, the run's save record, rows <-> run, New Game only / locks
 *     pe_ch_tabs.c         removes the battle tabs the challenges that are on ask for
 *     pe_ch_assist.c       cheats, assists and cheat_* log rows mark the run assisted; other mods' rows
 *     pe_ch_runlog.c       battles won, play time, the per-run file (knockouts, dead runs)
 *     pe_ch_summary.c      the Run, Warnings and Run summary info rows
 *     pe_ch_bundles.c      Bundles page: Purist, Nightmare, Clear all
 *   challenges
 *     pe_ch_menu_rules.c   No items in battle, No escape, Locked loadout (battle tabs only)
 *     pe_ch_no_pe.c        No Parasite Energy
 *     pe_ch_damage.c       Glass cannon, Tough crowd
 *     pe_ch_one_hit.c      One-hit KO
 *     pe_ch_no_exp.c       No EXP
 *     pe_ch_hardcore.c     Hardcore (permadeath)
 *
 * A new challenge: an id below (append only), a PeChallengeDef in its own file, and a line in
 * g_ch_defs. Nothing else changes.
 */

#include <stdint.h>

/* Ids are bits in the save record: append only, never renumber or reuse. */
enum {
    CH_NO_PE = 0,
    CH_NO_ITEMS = 1,
    CH_NO_ESCAPE = 2,
    CH_GLASS = 3,
    CH_TOUGH = 4,
    CH_HARDCORE = 5,
    CH_LOCKED_LOADOUT = 6,
    CH_ONE_HIT = 7,
    CH_NO_EXP = 8,
    CH_COUNT
};
#define CH_BIT(c) (1u << (c))
#define CH_ALL    (CH_BIT(CH_COUNT) - 1u)
#define CH_MAX_LEVELS 4   /* a level row: Off + up to 3 levels */

/* Battle menu tabs (g_MenuItemContextFlag bits 0-4), for PeChallengeDef.battle_tabs. */
#define CH_TAB_ITEM   0x01u
#define CH_TAB_PE     0x02u
#define CH_TAB_WEAPON 0x04u
#define CH_TAB_ARMOR  0x08u
#define CH_TAB_ESCAPE 0x10u

/* PeChallengeDef.flags */
#define CH_DEF_NEW_GAME_ONLY 0x1u  /* can only be picked at the title, for a New Game */
#define CH_DEF_LOCKS_RUN     0x2u  /* its run can't lower any challenge */

typedef struct PeChallengeDef {
    int id;                        /* CH_* */
    const char *label;             /* the F1 row */
    const char *short_name;        /* lists in the summary and toasts */
    const char *description;
    const char *const *levels;     /* NULL: on / off; else a choice, [0] = "Off" */
    unsigned level_count;          /* <= CH_MAX_LEVELS */
    unsigned battle_tabs;          /* CH_TAB_* removed while it is on (pe_ch_tabs.c) */
    unsigned flags;                /* CH_DEF_* */
    void (*register_rules)(const char *mod_id);   /* its hooks, or NULL */
} PeChallengeDef;

extern const PeChallengeDef *const g_ch_defs[];
extern const unsigned g_ch_def_count;
const PeChallengeDef *pe_ch_def(int id);

#define CH_FLAG_CHEATS 0x1u   /* PeChallengeRun.flags: a cheat or assist was used while a challenge was on */

#define CH_PAGE         "Challenges"
#define CH_PAGE_BUNDLES "Bundles"
#define CH_PAGE_RUN     "Run summary"

/* The run, saved with the game (record "pe_challenges.run", version 2). */
typedef struct PeChallengeRun {
    uint32_t active;    /* challenges on now */
    uint32_t ever;      /* every challenge that was on at some point this run */
    uint32_t dropped;   /* turned off or lowered mid-run: the run is assisted */
    uint32_t flags;     /* CH_FLAG_* */
    uint32_t run_id;    /* random, nonzero once a challenge was on; keys the per-run file */
    uint32_t battles;   /* battles won with a challenge on */
    uint32_t seconds;   /* play time with a challenge on (F1 and pauses excluded) */
    uint8_t level[16];  /* by id: 0 off, 1 on (or the level of a level row) */
    uint8_t pad[4];
} PeChallengeRun;
_Static_assert(sizeof(PeChallengeRun) == 48, "PeChallengeRun layout changed: raise its record version");
_Static_assert(CH_COUNT <= 16, "PeChallengeRun.level holds 16 challenges");

extern PeChallengeRun g_ch_run;

static inline int pe_ch_on(int c) { return (int)((g_ch_run.active >> c) & 1u); }
static inline int pe_ch_level(int c) { return pe_ch_on(c) ? g_ch_run.level[c] : 0; }
static inline int pe_ch_assisted(void) { return g_ch_run.dropped || (g_ch_run.flags & CH_FLAG_CHEATS); }

/* pe_ch_run.c */
int pe_ch_run_register(const char *mod_id);       /* the record, the rows, load / new game / sync */
unsigned pe_ch_rows_mask(void);                    /* what the rows ask for (the next pick at the title) */
void pe_ch_rows_turn_on(unsigned mask, int level); /* bundles: only ever turns on or raises */
void pe_ch_rows_clear(void);
int pe_ch_underway(void);                          /* past the grace period: lowering counts */
int pe_ch_run_locked(void);                        /* a challenge that locks the run is on */
void pe_ch_names(unsigned mask, char *out, unsigned size);   /* "No PE, Glass cannon" or "None" */

/* pe_ch_tabs.c */
void pe_ch_tabs_register(const char *mod_id);

/* pe_ch_assist.c */
void pe_ch_assist_register(const char *mod_id);
void pe_ch_assist_reset(void);                     /* a load or a new game: tell again next time */
const char *pe_ch_cheat_row_on(void);              /* a switched-on cheat / assist row's label, or NULL */
int pe_ch_row_on(const char *mod_id, const char *label);   /* another mod's row is on (saved or live) */

/* pe_ch_runlog.c */
void pe_ch_runlog_register(const char *mod_id);
unsigned pe_ch_knockouts(void);
int pe_ch_run_is_dead(uint32_t run_id);           /* a Hardcore run that ended in a game over */
void pe_ch_run_mark_dead(uint32_t run_id);        /* written to the file at once */

/* pe_ch_summary.c, pe_ch_bundles.c */
int pe_ch_summary_register(void);
int pe_ch_bundles_register(void);

/* The challenges (each defined in its own file). */
extern const PeChallengeDef g_ch_no_pe, g_ch_no_items, g_ch_no_escape, g_ch_locked_loadout,
    g_ch_glass, g_ch_tough, g_ch_one_hit, g_ch_no_exp, g_ch_hardcore;

#endif
