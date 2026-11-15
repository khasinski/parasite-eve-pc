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
 *     pe_ch_runlog.c       battles won, play time, knockout detection
 *     pe_ch_history.c      the per-run file beside the preferences: run history, knockouts, dead runs
 *     pe_ch_milestones.c   a real save refreshes the run's history; a Clear Data save marks it cleared
 *     pe_ch_records.c      per-challenge records (clears, best level, fastest), from run_cleared
 *     pe_ch_summary.c      the Run, Warnings and Run summary info rows (F1)
 *     pe_ch_bundles.c      Bundles page: Purist, Nightmare, Clear all
 *     pe_ch_events.c       events other mods can listen to (changed, assisted, cleared, ended)
 *   screens (the game's own widgets and windows)
 *     pe_ch_screen.c       Challenges in Aya's System list: active and completed challenges
 *     pe_ch_notice.c       a field notice in the game's dialogue window, one option to close it
 *     pe_ch_card_marks.c   Hardcore saves marked in the game's save / load lists (window, title bar)
 *   challenges
 *     pe_ch_menu_rules.c   No items in battle, No escape, Locked loadout (battle tabs only)
 *     pe_ch_no_pe.c        No Parasite Energy
 *     pe_ch_damage.c       the shared dealt-damage modifiers; Glass cannon, Tough crowd
 *     pe_ch_club_only.c    Club only (Club hits 2x / 1.5x, guns do no damage)
 *     pe_ch_one_hit.c      One-hit KO
 *     pe_ch_half_health.c  Half health (HP capped at 1/2 or 1/4 of max)
 *     pe_ch_no_exp.c       No EXP
 *     pe_ch_blood_price.c  Blood price (PE powers also cost HP)
 *     pe_ch_fast_enemies.c Fast enemies (turn gauges fill 1.5x / 2x)
 *     pe_ch_hardcore.c     Hardcore (permadeath)
 *
 * A new challenge: an id below (append only), a PeChallengeDef in its own file (with its category),
 * and a line in g_ch_defs. Nothing else changes: F1 and the Records pages group by category.
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
    CH_BLOOD_PRICE = 9,
    CH_FAST_ENEMIES = 10,
    CH_CLUB_ONLY = 11,
    CH_HALF_HEALTH = 12,
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

/* PeChallengeDef.category: the F1 sub-page and the Records page a challenge is listed on. Names,
 * descriptions and order in pe_challenges.c (g_ch_categories). */
enum { CH_CAT_RESTRICTIONS, CH_CAT_ENEMIES, CH_CAT_AYA, CH_CAT_RUN_RULES, CH_CATEGORIES };
typedef struct PeChallengeCategory {
    const char *name;          /* the F1 sub-page and the Records page title */
    const char *description;   /* the sub-page's footer line */
} PeChallengeCategory;
extern const PeChallengeCategory g_ch_categories[CH_CATEGORIES];

/* PeChallengeDef.flags */
#define CH_DEF_NEW_GAME_ONLY 0x1u  /* can only be picked at the title, for a New Game */
#define CH_DEF_LOCKS_RUN     0x2u  /* its run can't lower any challenge */
#define CH_DEF_FEATURED      0x4u  /* its F1 row is on the Challenges top page, not its category page */

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
    int category;                  /* CH_CAT_* */
} PeChallengeDef;

extern const PeChallengeDef *const g_ch_defs[];
extern const unsigned g_ch_def_count;
const PeChallengeDef *pe_ch_def(int id);

#define CH_FLAG_CHEATS 0x1u   /* PeChallengeRun.flags: a cheat or assist was used while a challenge was on */

#define CH_PAGE         "Challenges"
#define CH_PAGE_BUNDLES "Presets"
#define CH_PAGE_RUN     "This run"
/* F1 sub-page order: the categories 10-40, then these. */
#define CH_ORDER_PRESETS 50
#define CH_ORDER_RUN     60
#define CH_ORDER_DEBUG   90

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

/* pe_ch_damage.c: a challenge that changes the damage Aya deals multiplies this hit's *num / *den
 * (nothing when it doesn't apply). Read at the hit's start: g_ActiveActor is the attacker. A num
 * of 0 means no damage at all. Registering also adds the shared damage hooks (once). */
struct BattleEntity;
typedef void (*PeChDealtModifier)(const struct BattleEntity *target, int *num, int *den);
void pe_ch_damage_modifier(PeChDealtModifier fn, const char *mod_id);

/* pe_ch_assist.c */
void pe_ch_assist_register(const char *mod_id);
void pe_ch_assist_reset(void);                     /* a load or a new game: tell again next time */
const char *pe_ch_cheat_row_on(void);              /* a switched-on cheat / assist row's label, or NULL */
int pe_ch_row_on(const char *mod_id, const char *label);   /* another mod's row is on (saved or live) */

/* pe_ch_events.c: announcements to other mods (payloads documented there and in the README). */
#define CH_EVENT_CHANGED   "challenges.changed"    /* const uint32_t *: active mask */
#define CH_EVENT_ASSISTED  "challenges.assisted"   /* const char *: what marked the run */
#define CH_EVENT_RUN_ENDED "challenges.run_ended"  /* const uint32_t *: the Hardcore run id */
#define CH_EVENT_CLEARED   "challenges.run_cleared" /* const uint32_t *: the run id */
void pe_ch_events_register(void);
void pe_ch_emit_changed(void);
void pe_ch_emit_assisted(const char *what);
void pe_ch_emit_run_ended(uint32_t run_id);
void pe_ch_emit_cleared(uint32_t run_id);

/* pe_ch_runlog.c */
void pe_ch_runlog_register(const char *mod_id);

/* pe_ch_history.c: one entry per run that had a challenge, newest last in the file. */
enum { CH_RUN_PLAYING = 0, CH_RUN_CLEARED = 1, CH_RUN_FALLEN = 2 };
typedef struct PeChRunEntry {
    uint32_t id, knockouts, state;     /* CH_RUN_* */
    uint32_t active, ever, dropped, flags, battles, seconds;
    uint32_t levels;                   /* 2 bits per challenge id */
    int64_t at;                        /* last update, Unix seconds (0: unknown, an older file) */
} PeChRunEntry;
int pe_ch_history_count(void);
const PeChRunEntry *pe_ch_history_newest(int index);   /* 0 = newest */
const PeChRunEntry *pe_ch_history_find(uint32_t run_id);
void pe_ch_history_note(int state);    /* this run's numbers now; a state never goes back to playing */
void pe_ch_history_knockout(void);     /* +1 for this run, written at once */
unsigned pe_ch_knockouts(void);
int pe_ch_run_is_dead(uint32_t run_id);           /* a Hardcore run that ended in a game over */
static inline int pe_ch_entry_level(const PeChRunEntry *e, int id) { return (int)((e->levels >> (2 * id)) & 3u); }

/* pe_ch_records.c: per challenge, kept across games; fed by "challenges.run_cleared". */
typedef struct PeChRecord {
    uint32_t clean, assisted;   /* clears, by whether the run was assisted */
    uint32_t best_level;        /* hardest level cleared (1 for an on / off challenge) */
    uint32_t fastest;           /* shortest time with challenges, seconds */
    uint32_t hardcore;          /* ever cleared in a Hardcore run */
    int64_t first;              /* first clear, Unix seconds */
} PeChRecord;
void pe_ch_records_register(const char *mod_id);
const PeChRecord *pe_ch_record(int id);
int pe_ch_records_cleared(void);   /* challenges with at least one clear */

/* pe_ch_milestones.c */
void pe_ch_milestones_register(const char *mod_id);

/* pe_ch_notice.c: a message in the game's dialogue window over the field, then `closed`. One at a
 * time; returns 0 if one is already up. The box opens on the first frame the field allows it. */
int pe_ch_notice_show(const char *line1, const char *line2, const char *option, void (*closed)(void));
int pe_ch_notice_busy(void);
void pe_ch_notice_register(const char *mod_id);

/* pe_ch_summary.c, pe_ch_bundles.c, pe_ch_screen.c */
int pe_ch_summary_register(void);
int pe_ch_bundles_register(void);
void pe_ch_screen_register(const char *mod_id);
void pe_ch_card_marks_register(const char *mod_id);
const char *pe_ch_status_text(void);   /* "Clean", "Assisted", "Clean, Hardcore" ... (shared by F1 and the screen) */

/* The challenges (each defined in its own file). */
extern const PeChallengeDef g_ch_no_pe, g_ch_no_items, g_ch_no_escape, g_ch_locked_loadout,
    g_ch_glass, g_ch_tough, g_ch_one_hit, g_ch_no_exp, g_ch_blood_price, g_ch_fast_enemies, g_ch_club_only,
    g_ch_half_health, g_ch_hardcore;

#endif
