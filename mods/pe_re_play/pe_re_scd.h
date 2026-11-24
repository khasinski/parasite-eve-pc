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
 * yields (pe_re_scd_run); each frame after runs them again from where they stopped (pe_re_scd_tick),
 * after restarting task 0 on the main script's procedure 1, the room's every-frame handler (GAME.C
 * calls Sce_scheduler each frame: Event_exec(0, 1), then every task, then Sce_work_clr).
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
 *   items    keep_item_ck (the pocket), sce_item_lost / sce_item_ck_lost (out of it, all or some),
 *            sce_item_get (into it) (SCE2.C Sce_Item_lost / Sce_Item_lost2 / Sce_Item_get)
 *   pad      sce_key_ck (held) / sce_trg_ck (pressed this frame) against the game's key bits
 *            (bio2-ds SCE2.C Sce_key_ck / Sce_trg_ck; PAD.C Pad_set's Key_type table)
 *   zones    aot_set, door_aot_se, item_aot_set and their 4-point forms: run = live, in the slot
 *            of its id (a later zone with the same id replaces it, and clears that id's
 *            aot_reset); aot_reset: a zone's kind, check bits and data replaced (kind 0: gone)
 *   text     message_on: the room's message, for the caller to show
 *   objects  work_set picks a task's object (the player, the partner, an enemy, a scene object,
 *            a door); member_set / member_set2 / member_copy / member_cmp / member_calc /
 *            member_calc2 / pos_set / dir_set read and write its fields (SCE1.C, def/SCENARIO.H's
 *            MEMBER_*). The player's position, facing and floor follow the session; pos_set or
 *            dir_set on her moves her (the caller places her). sce_em_set fills its enemy's.
 *   actors   plc_motion submits a clip for an imported enemy or person; plc_dest moves the selected
 *            enemy or player over successive ticks and publishes its Room_flg on arrival.
 *            plc_ret releases the command, plc_stop suspends it, and plc_flg / plc_rot / plc_cnt
 *            update its script flags, scratch words and starting sample.
 *   camera   cut_chg (that camera; camera zones locked out), cut_old (back to the one before,
 *            unlocked), cut_auto (lock on / off) (SCE1.C Cut_chg / Cut_old / Cut_auto)
 *   enemies  sce_em_set: run = placed (its offset kept; the caller reads the record)
 * Every other instruction is skipped (counted). Entering a room, and the end of every frame's run,
 * clears what the frame's zones and item use left (Sce_work_clr): F_atari (work 0), U_atari (1),
 * Use_id (2) and Get_id (3) become -1, and Use_flg (group 10) and Room_flg's second word (group 5,
 * bits 32-63) are cleared. So the zone check after a frame's run sets them for the next one. */
#include <stdint.h>

#include "pe_imp_logic.h"

#define PE_RE_FLAG_GROUPS 36                   /* RE2 scripts set groups 0-35 */
#define PE_RE_FG_STATUS 1
#define PE_RE_FG_STOP 2
#define PE_RE_FG_PRI 13
#define PE_RE_PRI_CAMERAS 16
#define PE_RE_STOP_PAD_LOCK 7
#define PE_RE_STOP_EVENTS 6
#define PE_RE_STAT_CINESCO 27
#define PE_RE_STAT_BG_JUST_DRAWN 11          /* (1 << 20): set by Cut_chg / Cut_old (SCE1.C) */
#define PE_RE_FG_ITEM 8
#define PE_RE_FG_ITEM2 34                      /* Item_flg2: taken items from stage 5 on (SCE_AT.C, Stage_no >= 4) */
#define PE_RE_WORK 256
#define PE_RE_LIVE_MAX 256
#define PE_RE_RESETS_MAX 64
#define PE_RE_TASKS 10
#define PE_RE_AOT_ON_MAX 8
#define PE_RE_ENEMIES_MAX 16
#define PE_RE_FG_ENEMY 6                       /* Enemy_flg: placements killed (Enemy_flg2, 7, from stage 4) */
#define PE_RE_FG_USE 10                        /* Use_flg: items usable here (bit = item) */
#define PE_RE_FG_KEY 31                        /* Key_flg: door locks opened (bit = lock id & 0x3F) */
#define PE_RE_LEVELS 4                         /* gosub levels a task keeps */
#define PE_RE_WORKS 64                         /* objects: 0 player, 1 partner, 2-17 enemies, 18-49 scene objects, 50-63 doors */
#define PE_RE_PATCHES 512                      /* work_copy: values written into the room's script (a hash) */
#define PE_RE_MEMBERS 44                       /* MEMBER_BE_FLAG0 .. MEMBER_DAMAGE_CNT */
enum { PE_RE_MEMBER_ID = 6, PE_RE_MEMBER_TYPE = 7, PE_RE_MEMBER_SCE_FLAGS = 22, PE_RE_MEMBER_SCE_FREE0 = 23, PE_RE_MEMBER_SCE_FREE1 = 24,
       PE_RE_MEMBER_SCE_FREE3 = 26,
       PE_RE_MEMBER_DEST_X = 20, PE_RE_MEMBER_DEST_Z = 21,
       PE_RE_MEMBER_AT_SCE_NO = 9, PE_RE_MEMBER_POS_X = 11, PE_RE_MEMBER_POS_Y = 12, PE_RE_MEMBER_POS_Z = 13, PE_RE_MEMBER_CDIR_Y = 15,
       PE_RE_MEMBER_NFLOOR = 17, PE_RE_MEMBER_ROUTINE0 = 2, PE_RE_MEMBER_ROUTINE1 = 3 };

typedef enum {
    PE_RE_ZOMBIE_IDLE, PE_RE_ZOMBIE_PURSUIT, PE_RE_ZOMBIE_CRAWL, PE_RE_ZOMBIE_PRONE,
    PE_RE_ZOMBIE_CORPSE, PE_RE_ZOMBIE_FEEDING, PE_RE_ZOMBIE_SCRIPT, PE_RE_ZOMBIE_UNSUPPORTED
} PeReZombieMode;

PeReZombieMode pe_re_zombie_mode(unsigned type, int routine);
#define PE_RE_SLOTS 256                        /* zone ids */
#define PE_RE_WORK_F_ATARI 0                   /* the zone hit in front of the player (SCE_AT.C) */
#define PE_RE_WORK_U_ATARI 1                   /* the zone hit under the player */
#define PE_RE_WORK_USE_ID 2                    /* the item last used */
#define PE_RE_WORK_GET_ID 3                    /* the item last picked up */
#define PE_RE_WORK_STAGE 0x18                  /* Stage_no, Room_no, Cut_no (MEM_CARD.H's SaveData order) */
#define PE_RE_WORK_ROOM 0x19
#define PE_RE_WORK_CUT 0x1A
#define PE_RE_WORK_RANDOM 0x1D                 /* Sce_random: rolled each frame, at room entry and by sce_rnd */
#define PE_RE_WORK_ROOM_OLD 0x1B               /* the room before: room + (stage + 1) * 256 */

typedef struct {
    uint8_t bits[PE_RE_FLAG_GROUPS][32];
    int16_t work[PE_RE_WORK];                  /* the game's work variables (not in the save yet) */
} PeReFlags;

int pe_re_flag(const PeReFlags *f, unsigned group, unsigned bit);
void pe_re_flag_set(PeReFlags *f, unsigned group, unsigned bit, int on);

typedef struct { uint8_t id, sce, sat; uint8_t data[6]; } PeReReset;

enum { PE_RE_KENDO = 0x48 };

typedef struct PeReSceneActor {
    uint8_t slot, kind, move;
    uint16_t frame;
    int16_t x, y, z, dir;
    int16_t part_y[3]; /* EM3F: parts 1, 2 and 4/6/8/10 */
    uint8_t absolute_parts;
    uint8_t field_pose;              /* field clip laid over the actor's simulated position */
    uint8_t direct, room_clip, reverse, start_frame, destination_pose;
    uint8_t loop;                    /* a direct clip that loops (Sce_flg 0x04) rather than holding its last frame */
    char bank[PE_IMP_ID_MAX];
} PeReSceneActor;

enum { PE_RE_EFFECT_MAX = 64 };
typedef struct PeReEffect {
    int x, y, z;
    uint16_t age, duration;
    uint8_t id, type, handle, scale, attachment, target, loop, tagged;
} PeReEffect;

/* A scripted clip (plc_motion). For the player, the partner and people (0x40 up) the mode picks the
 * bank as Spl_Scenario does (SPL_SCE.C): 0 the room's clips, 1 the actor's own, 2 and 3 its two sub
 * banks; 10-13 the same with authored root motion (not applied yet). Sce_flg: 0x04 loops it (else it
 * holds its last frame), 0x08 plays it at double speed, 0x10 at half (EM_SUB.H SCE_FLG_*). */
enum { PE_RE_CLIP_ROOM, PE_RE_CLIP_OWN, PE_RE_CLIP_SUB0, PE_RE_CLIP_SUB1, PE_RE_CLIP_LEGACY = 0xFF };
typedef struct {
    uint8_t active, move, reverse, next;
    uint16_t elapsed;                          /* clip frames played (60 Hz steps, by its speed) */
    uint8_t reset_parts, direct, start_frame;
    uint8_t mode, bank, loop, speed, half;     /* plc_motion's mode, PE_RE_CLIP_*, loop, 2 double 0 half 1 normal */
    uint16_t ticks;                            /* frames since it started, whatever its speed */
} PeReSceneMotion;

typedef struct {
    uint8_t moving, owned, mode;
    uint16_t arrival;                          /* the Room_flg bit to set on arrival, plus one (0 none) */
    int16_t x, z;
} PeReActorDestination;

/* Advance in RE2 room units, returning true on arrival. Facing is RE2 yaw. */
int pe_re_actor_destination_step(const PeReActorDestination *dest, double *x, double *z, int *facing);

typedef struct {
    uint8_t status;                            /* 0 idle */
    int16_t proc;                              /* the main script's procedure it started on (-1 init) */
    int8_t work;                               /* work_set's object, -1 none */
    int16_t spd[12];                           /* speed_set: speed x/y/z, turn x/y/z, then their accelerations */
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
    uint32_t slot[PE_RE_SLOTS];                /* each zone id's instruction (RDT offset), 0 none */
    PeReReset reset[PE_RE_RESETS_MAX];         /* aot_reset, in order */
    int nreset;
    int executed, skipped, stopped;            /* instructions run, skipped (not handled), budgets run out */
    uint8_t first_skipped;                     /* an opcode skipped, for the log */
    uint16_t skipped_by_op[256];               /* how often each skipped opcode was met (saturates) */
    int message;                               /* message_on's message, waiting to be shown; -1 none */
    unsigned movie_task;                       /* interrupted task plus one */
    unsigned movie_request;                    /* Movie_on ID plus one, 0 when idle */
    int messages;                              /* message_on run so far */
    PeReScdTask task[PE_RE_TASKS];
    uint32_t sec[2], len[2];                   /* init and main scripts in the room file */
    int waiting;                               /* tasks running after the last tick */
    uint8_t aot_on[PE_RE_AOT_ON_MAX];          /* aot_on: zones the scripts set off, for the caller */
    uint32_t enemy[PE_RE_ENEMIES_MAX];         /* sce_em_set instructions that ran (RDT offsets) */
    uint8_t sound_cues[16], sound_count;
    uint16_t voice_track;
    uint8_t voice_mode, voice_pending, voice_busy, voice_managed, voice_volume, voice_volume_set;
    uint32_t bgm_revision[3];
    uint8_t bgm_command[3];
    int camera, camera_old;                    /* the camera now (the caller keeps it current), the last */
    int camera_lock;                           /* the scripts hold the camera: zones don't switch it */
    int bg_drawn, bg_camera;                   /* STAT_BG_JUST_DRAWN's ticks left, the camera it last saw */
    uint8_t cut_swapped;                       /* cut_replace has run: cut_to / cut_from below hold */
    uint8_t cut_to[256], cut_from[256];        /* cut_replace's camera swaps in the switch zones, both ways */
    uint16_t arrived[PE_RE_WORKS];             /* plc_dest's arrival flag (Room_flg bit) plus one per walker, set
                                                * every frame from the next one until plc_dest / plc_ret again; 0 none.
                                                * Bit 0 counts: SCE2.C Plc_dest keeps Set_no as given (2-02, 4-09). */
    PeReActorDestination player_dest;
    PeReActorDestination enemy_dest[PE_RE_ENEMIES_MAX];
    uint8_t actor_stopped[PE_RE_WORKS];         /* plc_stop suspends the active scripted action */
    int16_t works[PE_RE_WORKS][PE_RE_MEMBERS]; /* the objects' fields (the player's kept current by the caller) */
    int player_set;                            /* pos_set / dir_set moved the player: works[0] */
    struct { uint32_t at; int16_t value; uint8_t wide; } patch[PE_RE_PATCHES];  /* work_copy's script patches,
                                                * by address (0 empty) */
    int npatch;
    uint16_t sca_id[256];                      /* sca_id_set: box slot -> its Id's top bits | 1 (0: as built) */
    int sca_changed;                           /* set when one changes: the caller tells the walker, clears it */
    uint8_t em_busy[16];                       /* enemy stand-ins: frames left on a scripted order (see the .c) */
    uint8_t em_ready[16];                      /* emergence acknowledged, even after the script clears its reply */
    uint8_t alligator_chosen;                  /* the player chose how to enter the battle in 4-0A */
    uint8_t em_dead[16];                       /* beaten (pe_re_session_beaten): 1, then 2 once the stand-in saw it */
    uint8_t em_alerted[16];                    /* room-local awareness survives movement and camera cuts */
    uint16_t em_idle_ticks[16];                /* standing zombie's time since script release */
    PeReSceneMotion scene[16];
    PeReSceneMotion pl_motion[2];              /* the player's and the partner's scripted clips (works 0, 1) */
    int nenemy;
    int naot_on;
    PeReEffect effects[PE_RE_EFFECT_MAX];
    int effect_count;
} PeReScdRun;

int pe_re_scene_kind(unsigned kind);
int pe_re_scd_scene_actors(const PeReScdRun *run, PeReSceneActor *out, int max);
int pe_re_scd_effects(const PeReScdRun *run, PeReEffect out[PE_RE_EFFECT_MAX]);

/* What the scripts ask of the game. */
typedef struct {
    int (*has)(void *ctx, uint16_t item);      /* keep_item_ck: the pocket holds it */
    int (*talking)(void *ctx);                 /* wsleeping: a message is still up */
    uint32_t key, key_trg;                     /* the game's key bits held / pressed this frame
                                                  (pe_re_scd_keys) */
    void *ctx;
    void (*lose)(void *ctx, uint16_t item, uint16_t count);   /* count 0: all of it */
    void (*give)(void *ctx, uint16_t item, uint16_t count);
} PeReScdHost;

/* Enter room `l`: start its init and main tasks and run every task until it yields. RE2 only;
 * another game, or a room without its file, gives ran = 0. */
void pe_re_scd_run(const PeImpRoomLogic *l, PeReFlags *flags, const PeReScdHost *host, PeReScdRun *out);
/* A frame: every task again, from where it stopped. */
void pe_re_scd_tick(const PeImpRoomLogic *l, PeReFlags *flags, const PeReScdHost *host, PeReScdRun *run);
/* Did the zone instruction at `at` run? (1 when the scripts didn't run, or `at` is 0: a record
 * stored before RoomLogic kept its origin, ZAT0.) */
int pe_re_scd_live(const PeReScdRun *r, uint32_t at);
/* Diagnostics: every skipped instruction met so far, by opcode, across every run. */
extern uint32_t pe_re_scd_skips[256];

/* Start procedure `proc` of the main script on task `level` (10 and up: the first free task 2-9),
 * as an event zone does (Event_exec). It runs from the next tick. */
void pe_re_scd_exec(const PeImpRoomLogic *l, PeReScdRun *run, unsigned level, unsigned proc);
/* The game's key bits for a pad in libetc order (up 0x1000, cross 0x40, ...): bit n is set when the
 * pad holds Key_type[0][n]'s button (bit 6/7/12 Cross, 8 R1, 9 Square, 13 Triangle, 14 Circle). */
uint32_t pe_re_scd_keys(uint16_t pad);
/* Is a task running main-script procedure `proc`? */
int pe_re_scd_running(const PeReScdRun *run, unsigned proc);

#endif
