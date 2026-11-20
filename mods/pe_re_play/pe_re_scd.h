#ifndef PE_RE_SCD_H
#define PE_RE_SCD_H
/* The RE2 room script interpreter. Instruction set: IntelOrca's biohazard-utils / BioRand
 * (Bio2ConstantTable.cs g_instructionSignatures, OpcodeV2.cs; MIT). How the engine runs them: the
 * bio2-ds decompilation of RE2 Dual Shock (SCE0.C Sce_scheduler_main / Event_exec / Event_init,
 * SCE1.C's handlers), read for behaviour only, no code taken. PE-free: it reads the stored room file
 * (RoomLogic RDT0), the game's flags and work variables, and says which zone instructions ran.
 *
 * Tasks, as the game keeps them: up to 10 room tasks, each a script position with its own gosub
 * levels, open ifs and loops. Entering a room starts task 0 on the init script's (RDT section 16)
 * procedure 0 and task 1 on the main script's (17) procedure 0, then runs every task until it
 * yields (pe_re_scd_run); each frame after runs them again from where they stopped (pe_re_scd_tick).
 * gosub and evt_exec index the main script, as the game's do.
 *   control  if / else / endif (if pushes its block end; a false condition pops it and jumps there;
 *            outside any if the task waits, retrying it next frame), gosub / return, goto, evt_end
 *   loops    for / for2 / next, while / ewhile, do / edwhile (conditions joined by and / or),
 *            switch / case / default / eswitch, break
 *   waits    sleep / sleeping (frames), wsleep / wsleeping (while a message is up), evt_next
 *   tasks    evt_exec (the first free task 2-9), evt_kill, evt_chain
 *   flags    ck / set (36 groups of 256: script bits count from each word's top bit, so FG_STATUS
 *            bit 0 is STAT_PLAYER, off Leon, on Claire; bit 1 STAT_SCENARIO_B: bio2-ds def/FLAG.H)
 *   work     cmp / save / copy / calc / calc2 on the game's work variables (save-data halfwords,
 *            so they last across rooms)
 *   items    keep_item_ck (the pocket)
 *   zones    aot_set, door_aot_se, item_aot_set and their 4-point forms: run = live; aot_reset:
 *            a zone's kind and data replaced (kind 0: gone)
 *   text     message_on: the room's message, for the caller to show
 * Every other instruction is skipped (counted). Key and trigger checks are false for now. */
#include <stdint.h>

#include "pe_imp_logic.h"

#define PE_RE_FLAG_GROUPS 36                   /* RE2 scripts set groups 0-35 */
#define PE_RE_FG_STATUS 1
#define PE_RE_FG_ITEM 8
#define PE_RE_WORK 256
#define PE_RE_LIVE_MAX 256
#define PE_RE_RESETS_MAX 64
#define PE_RE_TASKS 10
#define PE_RE_LEVELS 4                         /* gosub levels a task keeps */

typedef struct {
    uint8_t bits[PE_RE_FLAG_GROUPS][32];
    int16_t work[PE_RE_WORK];                  /* the game's work variables (not in the save yet) */
} PeReFlags;

int pe_re_flag(const PeReFlags *f, unsigned group, unsigned bit);
void pe_re_flag_set(PeReFlags *f, unsigned group, unsigned bit, int on);

typedef struct { uint8_t id, sce, sat; uint8_t data[6]; } PeReReset;

typedef struct {
    uint8_t status;                            /* 0 idle */
    int8_t sub;                                /* gosub level */
    int8_t ifel[PE_RE_LEVELS], loop[PE_RE_LEVELS], loop_if[PE_RE_LEVELS][4];
    uint32_t pc, ret[PE_RE_LEVELS];
    uint32_t ifend[PE_RE_LEVELS][8];
    uint32_t lstack[PE_RE_LEVELS][4], lbreak[PE_RE_LEVELS][4];
    int16_t lcnt[PE_RE_LEVELS][4];
} PeReScdTask;

typedef struct {
    int ran;                                   /* 1: the scripts ran (else every record is live) */
    uint32_t live[PE_RE_LIVE_MAX];             /* RDT offsets of zone instructions that ran */
    int nlive;
    PeReReset reset[PE_RE_RESETS_MAX];         /* aot_reset, in order */
    int nreset;
    int executed, skipped, stopped;            /* instructions run, skipped (not handled), budgets run out */
    uint8_t first_skipped;                     /* an opcode skipped, for the log */
    int message;                               /* message_on's message, waiting to be shown; -1 none */
    int messages;                              /* message_on run so far */
    PeReScdTask task[PE_RE_TASKS];
    uint32_t sec[2], len[2];                   /* init and main scripts in the room file */
    int waiting;                               /* tasks running after the last tick */
} PeReScdRun;

/* What the scripts ask of the game. */
typedef struct {
    int (*has)(void *ctx, uint16_t item);      /* keep_item_ck: the pocket holds it */
    int (*talking)(void *ctx);                 /* wsleeping: a message is still up */
    void *ctx;
} PeReScdHost;

/* Enter room `l`: start its init and main tasks and run every task until it yields. RE2 only;
 * another game, or a room without its file, gives ran = 0. */
void pe_re_scd_run(const PeImpRoomLogic *l, PeReFlags *flags, const PeReScdHost *host, PeReScdRun *out);
/* A frame: every task again, from where it stopped. */
void pe_re_scd_tick(const PeImpRoomLogic *l, PeReFlags *flags, const PeReScdHost *host, PeReScdRun *run);
/* Did the zone instruction at `at` run? (1 when the scripts didn't run, or `at` is 0: a record
 * stored before RoomLogic kept its origin, ZAT0.) */
int pe_re_scd_live(const PeReScdRun *r, uint32_t at);

#endif
