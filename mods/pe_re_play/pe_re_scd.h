#ifndef PE_RE_SCD_H
#define PE_RE_SCD_H
/* The RE2 room script interpreter, first cut: what a room's own scripts set up when the player
 * walks in. Instruction set: IntelOrca's biohazard-utils / BioRand (Bio2ConstantTable.cs
 * g_instructionSignatures, OpcodeV2.cs; MIT). PE-free: it reads the stored room file (RoomLogic
 * RDT0) and the game's flags, and says which zone instructions ran.
 *
 * Runs on entering a room: the init script (RDT section 16) procedure 0, then the main script
 * (section 17) procedure 0 up to its first wait or loop, following gosub / evt_exec / evt_chain,
 * within an instruction budget. Handled:
 *   control  if / else / endif as the game does them (bio2-ds decompilation, SCE0.C / SCE1.C, read
 *            for behaviour only): if pushes its block end (record end + offset); a false condition
 *            pops it and jumps there; else, reached on the true path, pops and jumps by its offset
 *            from its own start; endif (2 bytes) pops. A false condition outside any if makes the
 *            game wait until it's true: that thread stops for now. evt_end, return, gosub
 *   flags    ck / set (36 groups of 256), work variables: cmp / save / copy / calc
 *   items    keep_item_ck (the pocket)
 *   zones    aot_set, door_aot_se, item_aot_set and their 4-point forms: run = live;
 *            aot_reset: a zone's kind and data replaced (kind 0: gone)
 * Waits and loops (sleep, for, while, do, switch, goto) end that thread for now; every other
 * instruction is skipped and counted. Engine flags start at 0 except FG_STATUS bit 0, the player:
 * script bits count from each word's top bit, so it is the game's STAT_PLAYER (off Leon, on Claire);
 * bit 1 is STAT_SCENARIO_B (bio2-ds def/FLAG.H). */
#include <stdint.h>

#include "pe_imp_logic.h"

#define PE_RE_FLAG_GROUPS 36                   /* RE2 scripts set groups 0-35 */
#define PE_RE_FG_STATUS 1
#define PE_RE_FG_ITEM 8
#define PE_RE_LIVE_MAX 256
#define PE_RE_RESETS_MAX 64

typedef struct { uint8_t bits[PE_RE_FLAG_GROUPS][32]; } PeReFlags;

int pe_re_flag(const PeReFlags *f, unsigned group, unsigned bit);
void pe_re_flag_set(PeReFlags *f, unsigned group, unsigned bit, int on);

typedef struct { uint8_t id, sce, sat; uint8_t data[6]; } PeReReset;

typedef struct {
    int ran;                                   /* 1: the scripts ran (else every record is live) */
    uint32_t live[PE_RE_LIVE_MAX];             /* RDT offsets of zone instructions that ran */
    int nlive;
    PeReReset reset[PE_RE_RESETS_MAX];         /* aot_reset, in order */
    int nreset;
    int executed, skipped, stopped;            /* instructions run, skipped (not handled), threads ended early */
    uint8_t first_skipped;                     /* an opcode skipped, for the log */
} PeReScdRun;

/* Does the pocket hold `item`? (keep_item_ck) */
typedef int (*PeReHasItem)(void *ctx, uint16_t item);

/* Run room `l`'s scripts against `flags` (they may set some). RE2 only; another game, or a room
 * without its file, gives ran = 0. */
void pe_re_scd_run(const PeImpRoomLogic *l, PeReFlags *flags, PeReHasItem has, void *ctx, PeReScdRun *out);
/* Did the zone instruction at `at` run? (1 when the scripts didn't run, or `at` is 0: a record
 * stored before RoomLogic kept its origin, ZAT0.) */
int pe_re_scd_live(const PeReScdRun *r, uint32_t at);

#endif
