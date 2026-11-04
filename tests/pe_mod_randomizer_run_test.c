/* Room scripts run headless, for the randomizer's story order. The step listing (mod-randomizer-rooms)
 * can't tell which characters a room places at a story value: that is decided when the room starts,
 * by its own scripts. Here each room is loaded through Scene_LoadRoom, its starting actors spawn from
 * the scene table (Script_DrawSyncAndFlush), and their scripts run frame by frame with the player
 * standing still, at the story variables a run line gives.
 *
 * The interpreter loop is Script_RunQueue's, copied so the test knows each step it runs. Logic steps
 * run their real handlers (jumps, tests, variables, child routines, event handlers and sends, kills,
 * spawns, bits, scene flags, entity fields, positions, zone tests). Anything else (models, animation,
 * camera, sound, menus, movement, waits) is skipped: those steps only touch what this test doesn't read.
 * A skipped step goes straight on, as most do in the game in the same frame; the second time a routine
 * reaches the same skipped step in one frame (a wait loop with nothing to wait for) it yields there
 * until the next frame. GiveItem succeeds and is noted; CountItems reads 0 (no keys held) unless the run says keys=all.
 * keys=D6,CA (hex items) holds only those keys (and puts them in free bag slots), keys=none none; either ends the
 * line in asked= (as gate lines do).
 * id=TEXT is echoed at the end of the output line, so a tool can pair runs with what it generated them for.
 * The item menu (op 169: a chest's "put an item in" on closing, a full bag's swap) is cancelled (-1), so
 * an open chest stays open and gives nothing more.
 * A runs line with bag=full fills every free bag slot with Medicine 1 (0x06) first and runs GiveItem for
 * real (Inv_AddItem: a bag item fails on a full bag, as in the game; on a gate line the mod gives a table
 * gift one slot past the bag instead, and has= looks there too); swap=N answers the first item menu as
 * the swap menu does when Aya leaves bag slot N's item in the chest: the given item takes the slot and the
 * menu answers the old one (the script keeps it in the chest), later menus are cancelled, and gives after
 * the swap (the chest's kept item) aren't checked against the layout. The bag is put back after the line. Checks: has=D6 (that item is in the bag
 * at the end), lack=D6 (it isn't). The output line ends in bag=full, swap=N, bagged=<+OFF:result of each
 * GiveItem>.
 * A door ends the run. A frame that runs more than SPIN_STEPS steps ends it too (noted as spin).
 *
 * Usage: test-mod-randomizer-run <out.txt> [runs.txt] (the disc: PE_TEST_DISC, or PE_TEST_DISC2 when
 * PE_RUN_DISC2 is set). Each runs.txt line: M114 0x88 0x0 [V0x41=0x20 ...]
 * [keys=all] (room, V0x4a, V0x1, more variables). Without runs.txt every room runs once at V0x4a = 0.
 * One output line per run:
 *   run M114 V0x4a=0x88 V0x1=0x0 end=<idle|door M115|spin|load> frames=N alive=<types> gave=<offsets>
 *       (end=noroom and nothing more for M5000/M8888: scenes of the main loop, not rooms)
 *       set=<V0x4a:0x88>0x190,...> steps=<e3:+1a2c,+1a40;e5:...>
 * Steps are the listing's offsets (from the task block, as in randomizer-rooms.txt) of every step run.
 *
 * Walkable floor: ops 128/129 mark collision triangles closed (bit 0x80: not walkable from a neighbour)
 * and open again. After the run the floor is flooded from Aya's triangle over open neighbours, and each
 * zone a PointInPoly step (op 119) tested is checked against it. Appended to the run line:
 *   start=<triangle|none> closed=<triangles with 0x80 at the end> cut=<+offset of each zone tested
 *   whose polygon touches no reached triangle> reach=<+offset@x,z: a reached point inside each other zone>
 * A door whose zone is cut can't be walked to in that state. A runs line may end in at=x,z[,angle]: from
 * frame AT_FRAME on, Aya stands there (facing angle), as if she had walked into that zone, for AT_FRAMES
 * frames. Up to AT_MAX at= in a row walk her through several zones in turn (AT_FRAMES each); the run
 * ends after the last. The output line then ends in at=x,z,angle;x,z,angle... too.
 *
 * A runs line with press (before its at=) also presses the examine / talk button while Aya stands at
 * each stop: one frame down every PRESS_EVERY frames from PRESS_AT on. The button is the pad's Circle,
 * which the boot pad table maps to field bits 0x100, 0x200 and 0x20000000 (the scripts' tests: op 17
 * modes 0-3, whose real handler runs in these runs). The output line then ends in press=1.
 * A runs line with menu=N answers every menu (op 67, Script_GetMenuChoice) with item N; without it the
 * step is skipped like the other menus, and the routine's variable keeps what it held (often item 0).
 * The output line then has menu=N after press=1.
 * A runs line with animwait makes each wait for an animation's end (op 48) take ANIM_FRAMES frames and each
 * camera move (op 75) and actor walk (op 68) its own frame count, as in the game: Wayne's talks in M052 hand a
 * flag back and forth around them, and with them skipped his second set lands before the first is cleared. The output line
 * then has animwait=1.
 *
 * Op 85 (Boot_MemCardStepInit, misnamed: the battle transition, CD reads and fade) is skipped, so the
 * battle never runs or ends; an encounter routine has often told the room's door controller to die
 * just before it. A run that reaches op 85 ends its line in battle=1: its alive= is not the room's.
 *
 * A runs line with win plays each battle to a won end instead, as the battle code drives
 * g_BattleModeState, which the encounter routine reads with op 148 and writes with ops 137/138/149/150:
 * op 85 starts it (state 0, the field loses control: D_8009D2E8 bit 1, which op 240 reads); once the
 * routine has set 6 or 5 (or BATTLE_FRAMES * 3 frames on without it), BATTLE_FRAMES frames later the fight
 * is over: 7 (Battle_PhaseInitEnemyTurn, the field has control again), or 12 when the routine's wait
 * loop tests op 148's value against 12 (a boss's scripted end); BATTLE_FRAMES later the post-battle
 * steps are done: 9 (Battle_StepLevelUp). The battle ops run for real only once a battle has started.
 * The line ends in won=N (battles won); battle=1 only when one is still going at the end.
 *
 * A stop may be at=@E,angle: Aya stands on actor type E (a chest, a character) and the two touch, as
 * Scene_UpdateEntityPositions links their contact tasks when the hit spheres meet (actors here have no
 * models, so the game's pass never does). Chests and talks run in that task.
 *
 * The randomizer's layout: seed=S,D (D 0-2: Easy, Medium, Hard) patches the loaded room as the mod does at
 * scene start (every site of the room or none), and the line ends in got=<item given at each gave=>.
 * Every table give that runs must hand out the layout's item. Checks a line may ask for: want=+OFF (the
 * give at that listing offset runs), never=+OFF (it doesn't), flag=V0x2c|0x80 (that bit is set at the
 * end), clear=V0x41|0x9 (none of those bits is set at the end), ran=+OFF (the room's step at that task block
 * offset ran: its battle start, say). A failed check fails the test. A runs file
 * with a #cover line must hand out every table spot: a spot
 * none of whose gives ran in a seeded line (say only in a room no way leads to) fails the test.
 *
 * Story gates: a line with gate runs with the real mod. The first such line starts the mod host from a
 * pe_mods.ini written for its seed= (Randomize new games on, so the randomizer runs alone), in <out>.mods/;
 * every gate line must share that seed. Each gate run is a new game (PE_HOOK_NEW_GAME), the mod patches
 * the room itself (PE_HOOK_SCENE_START), and CountItems asks the mod first (PE_HOOK_ITEM_COUNT), then
 * keys=all. The line ends in asked=<+OFF:item>answer for each CountItems step>. Checks: shut=+OFF (the
 * key test at that listing offset ran and always answered 0), open=+OFF (it ran and answered nonzero).
 * A shut= step must be a table lock (pe_rand_locks.h) whose key the layout moved.
 *
 * Moved doors (Hard): the line's variables are set before the mod's scene start, as in the game. A gate
 * line with from=M024 first loads that room and starts it for the mod (Aya leaving it, its door moved),
 * then loads the line's room. Checks: arrive=0x17a (V0x1 after the mod's scene start: the mod set it),
 * door=M378 (the run ends at a door to that room; door=none: at no door), floor (at the end Aya stands on a floor triangle,
 * start=N). The output's V0x1= is the value after the scene start.
 *
 * Enemy stats (battle randomizer): a line with stats runs the enemy ops for real: op 111 takes a battle slot
 * (Entity_AllocSlot's work, without its animation: no models here), ops 90 / 206 write the slot's fields
 * (Entity_WriteFieldByCmd, so PE_HOOK_ENEMY_FIELD) and op 89 reads them. The line starts from the same
 * random state every time, so two runs of a room roll the same. It ends in combat=<e5#3:v,v,...;...>: each
 * slot taken (actor type, allocation), with its stat block (PE_RAND_STAT_CODES order, pe_rand_enemies.h) at
 * the start of the frame after op 111 ran: the setup done, and on a gate line the mod's PE_HOOK_FRAME run. force=5<M005:2 (gate lines only: the real mod) gives actor 5
 * of the line's room the stat block of M005's actor 2 (pe_rand_battle_force); the line then has force= too.
 * ai=5<M012:5 (gate lines only) gives actor 5 the script and model of M012's actor 5 (pe_rand_ai_force,
 * pe_rand_model_force): after the load the mod's PE_HOOK_PLAYER_ROWS runs, as the game's load does before the
 * actors spawn. The line then ends in ai=5<M012:5, aisteps=<e5:N: steps actor 5 ran in the given script,
 * outside the room's own task block> and pm=<script slots the hook installed>. It fails when the script wasn't
 * given, the actor ran none of it, or a win line won no battle.
 * foes (gate lines; then every gate line of the file has it): the mod host starts with enemies on (players have
 * no such row: checked at every gate start, then set past it through pe_rand_test_options), and
 * the line keeps the swaps the mod's own New Game put in (this seed's foe plan) instead of forcing its ai=. Every
 * actor of the room the plan moves (pe_rand_foes_generate here, same seed) must have been given a script, and
 * no other; the line's ai= list (the moved actors that fight there) is checked as above.
 * tools/battlestats.py checks the lines. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>
#include "common.h"
#include "pe1/asset_table.h"
#include "pe1/battle.h"
#include "pe1/battle_runtime.h"
#include "pe1/boot.h"
#include "pe1/boot_disc_check.h"
#include "pe1/boot_services.h"
#include "pe1/cdrom.h"
#include "pe1/entity_allocation.h"
#include "pe1/field_actor.h"
#include "pe1/field_collision.h"
#include "pe1/field_sfx.h"
#include "pe1/game_state.h"
#include "pe1/inventory.h"
#include "pe1/inventory_slots.h"
#include "pe1/pad_state.h"
#include "pe1/player_entity.h"
#include "pe1/reloc_block.h"
#include "pe1/scene_room.h"
#include "pe1/scene_room_load.h"
#include "pe1/script_node.h"
#include "pe1/script_queue.h"
#include "pe1/script_state.h"
#include "pe_hooks.h"
#include "pe_randomizer.h"
#include "../mods/host/pe_mod_menu.h"
#include "pe_rand_foes.h"
#include "pe_rand_enemies.h"
#include "pe_rand_locks.h"
#include "pe_rand_spots.h"

/* Fixture I/O: the test-disc object renames the real image reads to these. */
int CdRom_ImageReadSectorsFromLba(u32 lba, void *buffer, u32 count);
int CdRom_ImagePollReady(void);
int CdRom_ReadSectorsFromLba(u32 lba, void *buffer, u32 count) { return CdRom_ImageReadSectorsFromLba(lba, buffer, count); }
int CdRom_PollReady(void) { return CdRom_ImagePollReady(); }
int Asset_Find08w(int id, int mode, s16 x, s16 y, s16 z) { (void)id; (void)mode; (void)x; (void)y; (void)z; return 0; }

void Entity_RelocateSceneData(void);
void Script_GarbageCollectNodes(void);
void Entity_CollectGarbage(void);
FieldActor *Scene_LoadMap(u8 *scene, FieldActor *after, int allocateFull);

enum { AT_FRAME = 60, AT_FRAMES = 240, AT_MAX = 8, FRAMES = 600, ANIM_FRAMES = 30, SPIN_STEPS = 200000, VARS = 512, TAKE = 0x458 };
enum { PRESS_AT = 20, PRESS_EVERY = 30, BATTLE_FRAMES = 30 };
extern int g_BattleModeState;
static const u32 press_bits = 0x20000300;   /* Circle: field bits 8, 9 and 29 */
static unsigned failures;
#define FAIL(...) do { if (++failures <= 40) { fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)

/* Opcodes whose real handler runs: logic over script variables, nodes and entity fields. */
static const u16 real_ops[] = {
    0, 1, 2, 4, 5, 9, 10, 11, 12, 18, 19, 20, 24, 25, 26, 28, 29, 30, 31, 32, 35, 36, 38, 40, 41, 42, 43,
    45, 50, 55, 57, 63, 64, 65, 66, 79, 80, 81, 82, 83, 94, 119, 120, 121, 128, 129, 136, 139, 170, 171, 199,
    222, 240,   /* 240: 1 unless a battle is set up (D_8009D2E8 & 1): zone routines wait on it */
};
static u8 is_real[8192];

/* The run in progress. */
static const u8 *block;          /* task block start: listing offsets count from here */
static size_t block_bytes;
static u8 *ran;                  /* one per word of the block: step started there */
static u8 *anim_left;            /* one per word of the block: frames an animwait op 48 has waited */
static u32 gave[64], got[64]; static unsigned gave_count;   /* GiveItems run: offset and item */
static u32 asked[64], asked_item[64], answer[64]; static unsigned asked_count;   /* CountItems run */
static int keys_all, press, animwait, battle, spin, trace, gate;   /* trace: PE_RUN_TRACE set, each step to stderr */
static int menu = -1;            /* menu=N: op 67's answer, or -1 (skipped) */
static int bag_full, swap = -1;  /* bag=full: GiveItem runs for real; swap=N: op 169's answer, or -1 */
static int added_ok[64];         /* bag=full: what each GiveItem returned (0 = in the bag) */
static unsigned swapped_at = 64; /* gives before the swap: the chest's own item; after it, what Aya left */
static int held_only; static unsigned held_count; static u32 held_items[8];   /* keys=none / keys=D6,.. */

static u32 key_count(u32 item) {
    if (!held_only) return (u32)keys_all;
    for (unsigned i = 0; i < held_count; ++i) if (held_items[i] == item) return 1;
    return 0;
}
static unsigned long frame_steps;
/* win: battles in progress (fighting: frames since op 85, -1 if none), won so far, the value the
 * routine's last op 148 wait tested against, the frame the routine set 6 (-1: not yet). */
static int win, fighting, won, awaited, set6_at, *state_read;
static unsigned cur_frame;
/* stats: enemy ops run; each slot taken, its stat block at the end of that frame. */
static int stats;
typedef struct Combat { u8 type, instance, slot, done; s32 v[PE_RAND_STATS]; } Combat;
static Combat combats[32]; static unsigned combat_count;
/* ai=: steps each actor type ran outside the room's task block (in a script the mod gave it). */
static unsigned ai_steps[16];
static u32 random_start[33];   /* the random generator's words when the test started */

/* Entity_AllocSlot (battle/enemy_slots.c) without Entity_SetActionMode: no model banks here. */
extern const EnemyCombatant g_EnemyCombatantInitial;
static void alloc_slot(FieldActor *a) {
    BattleEntity *e = (BattleEntity *)a;
    for (u8 i = 0; i < 7; ++i) {
        if (g_BattleEnemySlots[i].active) continue;
        g_BattleEnemySlots[i].active = 1;
        g_BattleEnemySlots[i].combatant = g_EnemyCombatantInitial;
        EnemyCombatant *c = &g_BattleEnemySlots[i].combatant;
        e->core = c;
        c->lowAndEntityId.bytes.entityId = ++g_EntityInstanceCounter;
        c->slotBit = 1 << i;
        if (!(e->entityFlags & 0x2000)) { c->effect = &c->records[0]; g_BattleLiveEnemyCount++; }
        if (combat_count < 32) combats[combat_count++] = (Combat){ a->type_id, c->lowAndEntityId.bytes.entityId, i, 0, {0} };
        return;
    }
}

/* A field code's value as Entity_WriteFieldByCmd stored it. */
static s32 stat_of(const FieldActorState *s, u8 code) {
    switch (code) {
    case 0x28: return s->amount_mirror;
    case 0x2c: return s->control10.command_value;
    case 0x2d: return s->action_value14;
    case 0x3c: return s->script_value88;
    case 0x3d: return s->script_value8c;
    case 0x3e: return s->script_value8e;
    case 0x3f: return s->effect_chance;
    case 0x41: return s->effect_param92;
    case 0x42: return s->effect_param93;
    case 0x43: return s->effect_level;
    case 0x44: return s->effect_duration;
    case 0x45: return s->script_value96;
    case 0x5a: return s->script_value98;
    case 0x5b: return s->script_value9a;
    case 0x5c: return s->script_value9e;
    case 0x5d: return s->script_value9f;
    case 0x64: return s->status_flags2.bits.field64;
    case 0x65: return s->status_flags2.bits.field65;
    case 0x66: return s->status_flags2.bits.field66;
    case 0x67: return s->status_flags2.bits.field67;
    case 0x68: return s->status_flags2.bits.field68;
    case 0x69: return s->status_flags2.bits.field69;
    case 0x6a: return s->status_flags2.bits.field6a;
    case 0x6b: return s->status_flags2.bits.field6b;
    case 0x6c: return s->status_flags2.bits.field6c;
    case 0x6d: return s->status_flags2.bits.field6d;
    }
    return -1;
}

/* stats: a frame starts (the mod's PE_HOOK_FRAME on gate lines: a forced block goes in), then each slot
 * taken last frame has its setup done: its block is read. */
struct Run;
static void frame_start(const struct Run *r);

/* Op 206's target: the actor op 139 would find (type, sub id, alive). */
static FieldActor *actor_by_type(int type, int sub) {
    FieldActor *a = g_FieldActorListHead;
    while (a && (a->type_id != type || a->sub_id != sub || (a->flags & 0x10))) a = a->next;
    return a;
}
/* Zones PointInPoly tested: listing offset and the polygon's whole-unit corners (x, z). */
typedef struct Zone { u32 at; s16 x[4], z[4]; } Zone;
static Zone zones[256]; static unsigned zone_count;

static u32 word_at(const u8 *p, size_t offset) { u32 v; memcpy(&v, p + offset, 4); return v; }

/* Script_RunQueue (script/flow_opcodes.c), with the step policy above. */
static void run_queue(void) {
    int *args[16];
    do {
        ScriptNode *node = g_ScriptNodePool;
        FieldActor *entity = g_CurrentEntity;
        if (node->flags & 0x50) continue;
        if ((entity->flags & 0x1000) && !(node->flags & 0x80)) continue;
        if ((g_GameStateFlags & 0x100) && entity != g_PlayerEntity && !(node->flags & 0x80)) continue;
        if (node->active == 0) continue;
        node->active--;
        if (node->active != 0) continue;
        g_ScriptCursor = node->current.script;
        size_t skipped[64]; unsigned skipped_count = 0;
        int result;
        do {
            u32 *step = g_ScriptCursor, header = step[0], tail = step[1], modes = header >> 17;
            u32 op = header & 0x1FFF, count = (header >> 13) & 15, *values = step + 2;
            g_ScriptCursor = values + count;
            size_t at = (size_t)((const u8 *)step - block);
            if (at < block_bytes) ran[at / 4] = 1;
            else if (entity->type_id < 16) ++ai_steps[entity->type_id];
            if (++frame_steps > SPIN_STEPS) { spin = 1; return; }
            if (op == 85) battle = 1;
            if (trace) { fprintf(stderr, "e%u +%#zx op %u\n", entity->type_id, at, op); fflush(stderr); }
            for (u32 i = 0; i < count; i++) {
                switch (modes & 7) {
                case 0: args[i] = (int *)&values[i]; break;
                case 3: args[i] = (int *)&g_ScriptOperandTable[values[i]]; break;
                case 1: args[i] = (int *)&g_CurrentEntity->pad_0AC[values[i]]; break;
                case 2: args[i] = (int *)&g_ScriptVariableBank[values[i]]; break;
                case 4: args[i] = &g_ScriptNodeActiveFlags[values[i]]; break;
                }
                modes >>= 3;
                if (i == 4) modes = tail;
            }
            if (op == 119 && at < block_bytes) {
                unsigned z = 0;
                while (z < zone_count && zones[z].at != at) ++z;
                if (z == zone_count && zone_count < 256) zones[zone_count++].at = (u32)at;
                if (z < 256)
                    for (int i = 0; i < 4; ++i) { zones[z].x[i] = (s16)(*args[i * 2] >> 16); zones[z].z[i] = (s16)(*args[i * 2 + 1] >> 16); }
            }
            int battle_op = win && (op == 137 || op == 138 || op == 148 || op == 149 || op == 150) && (fighting >= 0 || won);
            if (op == 148) state_read = args[0];
            if (op == 9 && count == 4 && *args[0] == 14 && args[2] == state_read) awaited = *args[3];   /* wait: state != n */
            if (op == 139 && *args[0] != 0) {   /* enemy field: actors here have no battle state, as if absent */
                FieldActor *a = g_FieldActorListHead;
                while (a && (a->type_id != *args[0] || a->sub_id != *args[1] || (a->flags & 0x10))) a = a->next;
                result = a && a->state ? g_ScriptQueueOpcodeHandlers[op](args) : 1;
            } else if (stats && op == 111) {                                      /* a battle slot */
                if (!entity->state) alloc_slot(entity);
                result = 1;
            } else if (stats && (op == 89 || op == 90) && entity->type_id != 0) {   /* the slot's fields */
                result = entity->state ? g_ScriptQueueOpcodeHandlers[op](args) : 1;
            } else if (stats && (op == 112 || op == 183) && entity->type_id != 0) {   /* attack power (PE_HOOK_ENEMY_POWER) */
                result = entity->state ? g_ScriptQueueOpcodeHandlers[op](args) : 1;
            } else if (stats && op == 206 && *args[0] != 0) {
                FieldActor *a = actor_by_type(*args[0], *args[1]);
                result = a && a->state ? g_ScriptQueueOpcodeHandlers[op](args) : 1;
            } else if (op == 9 && count == 4 && (*args[0] == 16 || *args[0] == 21 || *args[0] == 22) && *args[3] == 0) {
                /* x / 0, x % 0: fields skipped ops fill in the game (battle stats, op 89; op 14) read 0 here */
                *args[1] = 0; result = 1;
            } else if (is_real[op] || battle_op) {
                result = g_ScriptQueueOpcodeHandlers[op](args);
                if ((op == 137 || op == 138) && set6_at < 0) set6_at = (int)cur_frame;
            } else if (op == 85 && win) {                                         /* battle: starts */
                if (fighting < 0) { fighting = 0; set6_at = -1; awaited = -1; g_BattleModeState = 0; D_8009D2E8 |= 1; }
                result = 1;
            }
            else if (op == 49) result = g_ScriptQueueOpcodeHandlers[op](args);   /* door: ends the run */
            else if (op == 17 && press && (u32)*args[0] <= 3) result = g_ScriptQueueOpcodeHandlers[op](args);   /* pad */
            else if (op == 67 && menu >= 0) { *args[0] = menu; result = 1; }   /* menu: answered */
            else if ((op == 48 || ((op == 75 || op == 68) && count == 3)) && animwait && at < block_bytes) {   /* animation end, camera, walk */
                int frames = op == 48 ? ANIM_FRAMES : *args[2] < 255 ? *args[2] : 255;
                if (anim_left[at / 4]++ < frames) { g_ScriptCursor = step; result = 0; g_ScriptNodePool->active = 1; }
                else { anim_left[at / 4] = 0; result = 1; }
            }
            else if (op == 167) {                                                 /* GiveItem: given */
                if (bag_full) result = g_ScriptQueueOpcodeHandlers[op](args); else { *args[1] = 0; result = 1; }
                if (gave_count < 64) { added_ok[gave_count] = *args[1]; gave[gave_count] = (u32)at; got[gave_count++] = (u32)*args[0]; }
            } else if (op == 168) {                                               /* CountItems */
                if (!gate || !PE_HOOK(PE_HOOK_ITEM_COUNT, args)) *args[1] = key_count((u32)*args[0]);
                if (asked_count < 64) { asked[asked_count] = (u32)at; asked_item[asked_count] = (u32)*args[0]; answer[asked_count++] = (u32)*args[1]; }
                result = 1;
            }
            else if (op == 169 && count == 2) {                                   /* item menu: cancelled, or swap=N */
                *args[1] = -1; result = 1;
                if (swap >= 0 && swap < g_InvSlotLimit && g_InvItemPtr) {   /* slot N's item out, the new one in, once */
                    *args[1] = g_InvItemPtr[swap]; g_InvItemPtr[swap] = (s16)*args[0];
                    swap = -1; swapped_at = gave_count;
                }
            }
            else if (op == 8) {                                                   /* spawn at a point */
                char scene[2] = { (char)*args[0], (char)*args[1] };
                if (trace) { fprintf(stderr, "spawn type %d sub %d\n", scene[0], scene[1]); fflush(stderr); }
                FieldActor *a = Scene_LoadMap((u8 *)scene, g_CurrentEntity, 1);
                if (a) { a->pos_x = *args[2]; a->pos_y = *args[3]; a->pos_z = *args[4]; }
                result = 1;
            } else if (op == 17 && *args[0] == 2500) {                            /* spawn command */
                char scene[2] = { (char)*args[1], (char)*args[2] };
                FieldActor *a = Scene_LoadMap((u8 *)scene, g_CurrentEntity, 0);
                if (a) { a->pos_x = *args[3]; a->pos_y = *args[4]; a->pos_z = *args[5]; }
                result = 1;
            } else {                                                              /* skipped */
                unsigned k = 0;
                while (k < skipped_count && skipped[k] != at) ++k;
                if (k < skipped_count || skipped_count == 64) { result = 0; g_ScriptNodePool->active = 1; }
                else { skipped[skipped_count++] = at; result = 1; }
            }
            if (result < 0) { result = 0; g_ScriptNodePool->active = 1; }
        } while (result && !(g_GameStateFlags & 0x2000));
        g_ScriptNodePool->current.script = g_ScriptCursor;
        if (g_GameStateFlags & 0x2000) return;
    } while ((g_ScriptNodePool = g_ScriptNodePool->next) != 0);
}

/* Entity_DispatchCallbacks: the actor's three node lists. */
static void dispatch(FieldActor *a) {
    g_CurrentEntity = a;
    for (int i = 0; i < 3 && !spin && !(g_GameStateFlags & 0x2000); ++i) {
        g_ScriptNodePool = a->script_node_lists[i];
        if (g_ScriptNodePool) run_queue();
    }
}

/* The collision floor: triangle i's header, links and corner c. */
static u8 *tri_at(unsigned i) {
    return g_CollisionPlaneTable ? (u8 *)&g_CollisionDb->triangles.xyz[i] : (u8 *)&g_CollisionDb->triangles.xz[i];
}
static const CollisionTriangleLinks *tri_links(unsigned i) {
    return g_CollisionPlaneTable ? &g_CollisionDb->triangles.xyz[i].layout.links : &g_CollisionDb->triangles.xz[i].layout.links;
}
static void tri_corner(unsigned i, int c, s16 *x, s16 *z) {
    CollisionVertexTable v;
    if (g_CollisionPlaneTable) { COLLISION_VERTEX(v, tri_links(i)->vertices[c], CollisionVertexXYZ); *x = (s16)v.xyz->x; *z = (s16)v.xyz->z; }
    else { COLLISION_VERTEX(v, tri_links(i)->vertices[c], CollisionVertexXZ); *x = (s16)v.xz->x; *z = (s16)v.xz->z; }
}

/* Point (x, z) in the quad, by crossing count; points on an edge count as in. */
static int in_quad(const Zone *q, int x, int z) {
    int in = 0;
    for (int i = 0, j = 3; i < 4; j = i++) {
        int xi = q->x[i], zi = q->z[i], xj = q->x[j], zj = q->z[j];
        long long cross = (long long)(xj - xi) * (z - zi) - (long long)(zj - zi) * (x - xi);
        if (cross == 0 && x >= (xi < xj ? xi : xj) && x <= (xi > xj ? xi : xj) && z >= (zi < zj ? zi : zj) && z <= (zi > zj ? zi : zj)) return 1;
        if ((zi > z) != (zj > z) && x < xi + (long long)(xj - xi) * (z - zi) / (zj - zi)) in = !in;
    }
    return in;
}
static long long side(int ax, int az, int bx, int bz, int px, int pz) { return (long long)(bx - ax) * (pz - az) - (long long)(bz - az) * (px - ax); }
static int segments_cross(int ax, int az, int bx, int bz, int cx, int cz, int dx, int dz) {
    long long d1 = side(cx, cz, dx, dz, ax, az), d2 = side(cx, cz, dx, dz, bx, bz);
    long long d3 = side(ax, az, bx, bz, cx, cz), d4 = side(ax, az, bx, bz, dx, dz);
    return ((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) && ((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0));
}
/* Triangle i and the zone share any area (a corner of one inside the other, or crossing edges). */
static int tri_meets_zone(unsigned i, const Zone *q) {
    s16 x[3], z[3];
    for (int c = 0; c < 3; ++c) { tri_corner(i, c, &x[c], &z[c]); if (in_quad(q, x[c], z[c])) return 1; }
    u8 *t = tri_at(i);
    for (int k = 0; k < 4; ++k) if (Geo_PointInTri(t, q->x[k], q->z[k])) return 1;
    for (int c = 0; c < 3; ++c)
        for (int k = 0, l = 3; k < 4; l = k++)
            if (segments_cross(x[c], z[c], x[(c + 1) % 3], z[(c + 1) % 3], q->x[l], q->z[l], q->x[k], q->z[k])) return 1;
    return 0;
}

/* A point inside both triangle i and the zone, or 0. */
static int stand_point(unsigned i, const Zone *q, int *px, int *pz) {
    s16 x[3], z[3];
    for (int c = 0; c < 3; ++c) tri_corner(i, c, &x[c], &z[c]);
    int cx = (q->x[0] + q->x[1] + q->x[2] + q->x[3]) / 4, cz = (q->z[0] + q->z[1] + q->z[2] + q->z[3]) / 4;
    int tx = (x[0] + x[1] + x[2]) / 3, tz = (z[0] + z[1] + z[2]) / 3;
    int cand[16][2], n = 0;
    cand[n][0] = cx; cand[n++][1] = cz;
    cand[n][0] = tx; cand[n++][1] = tz;
    for (int k = 0; k < 4; ++k) { cand[n][0] = (q->x[k] + 3 * cx) / 4; cand[n++][1] = (q->z[k] + 3 * cz) / 4; }
    for (int c = 0; c < 3; ++c) { cand[n][0] = (x[c] + 3 * tx) / 4; cand[n++][1] = (z[c] + 3 * tz) / 4; }
    for (int k = 0; k < 4; ++k) { cand[n][0] = (q->x[k] + tx) / 2; cand[n++][1] = (q->z[k] + tz) / 2; }
    for (int k = 0; k < n; ++k)
        if (in_quad(q, cand[k][0], cand[k][1]) && Geo_PointInTri(tri_at(i), (s16)cand[k][0], (s16)cand[k][1])) {
            *px = cand[k][0]; *pz = cand[k][1]; return 1;
        }
    return 0;
}

/* Flood the floor from Aya's triangle over neighbours without 0x80; prints start=, closed=, cut= and reach=.
 * 1: Aya stands on a floor triangle. */
static int print_floor(FILE *out) {
    unsigned n = g_CollisionDb ? g_CollisionDb->triangleCount : 0, start = n;
    FieldActor *aya = g_PlayerEntity;
    if (aya && n)
        for (unsigned i = 0; i < n && start == n; ++i)
            if (Geo_PointInTri(tri_at(i), (s16)(aya->pos_x >> 16), (s16)(aya->pos_z >> 16))) start = i;
    int on_floor = start < n;
    if (aya) fprintf(out, "\taya=%d,%d", aya->pos_x >> 16, aya->pos_z >> 16); else fputs("\taya=none", out);
    if (aya && n && start == n) {   /* off the floor (no spawn for this V0x1): the triangle nearest her, start=~N */
        long long best = -1;
        for (unsigned i = 0; i < n; ++i) {
            s16 x, z; long long sx = 0, sz = 0;
            for (int c = 0; c < 3; ++c) { tri_corner(i, c, &x, &z); sx += x; sz += z; }
            long long dx = sx / 3 - (aya->pos_x >> 16), dz = sz / 3 - (aya->pos_z >> 16), d = dx * dx + dz * dz;
            if (!(*tri_at(i) & 0x80) && (best < 0 || d < best)) { best = d; start = i; }
        }
        if (start < n) fprintf(out, "\tstart=~%u", start);
    } else if (start < n) fprintf(out, "\tstart=%u", start);
    if (start == n) fputs("\tstart=none", out);
    fputs("\tclosed=", out);
    int first = 1;
    for (unsigned i = 0; i < n; ++i) if (*tri_at(i) & 0x80) { fprintf(out, "%s%u", first ? "" : ",", i); first = 0; }
    fputs("\tcut=", out);
    if (start == n) return 0;
    u8 *reached = calloc(n, 1); unsigned *queue = malloc(n * sizeof(*queue)), head = 0, tail = 0;
    reached[start] = 1; queue[tail++] = start;
    while (head < tail) {
        const CollisionTriangleLinks *l = tri_links(queue[head++]);
        for (int s = 0; s < 3; ++s) {
            unsigned m = l->neighbours[s];
            if (m < n && !reached[m] && !(*tri_at(m) & 0x80)) { reached[m] = 1; queue[tail++] = m; }
        }
    }
    static int px[256], pz[256]; static u8 has[256];
    first = 1;
    for (unsigned z = 0; z < zone_count; ++z) {
        unsigned i = 0;
        has[z] = 0;
        while (i < n && !(reached[i] && tri_meets_zone(i, &zones[z]))) ++i;
        if (i == n) { fprintf(out, "%s+%X", first ? "" : ",", zones[z].at); first = 0; continue; }
        for (; i < n && !has[z]; ++i) if (reached[i] && stand_point(i, &zones[z], &px[z], &pz[z])) has[z] = 1;
    }
    fputs("\treach=", out);
    first = 1;
    for (unsigned z = 0; z < zone_count; ++z)
        if (has[z]) { fprintf(out, "%s+%X@%d,%d", first ? "" : ",", zones[z].at, px[z], pz[z]); first = 0; }
    free(reached); free(queue);
    return on_floor;
}

/* Room number a scene token names, or -1 (M0000, M5000 and M8888 are scenes of the main loop). */
static int token_room(u32 token) {
    char name[8] = {0};
    Text_EncodeBase32(name, token);
    if (name[0] != 'M' || name[5] != 'I') return -1;
    for (int i = 1; i < 5; ++i) if (name[i] < '0' || name[i] > '9') return -1;
    return atoi(name + 1);
}

typedef struct Run Run;
struct Run {
    unsigned room; u32 area, left; unsigned nvars; u16 var[32]; u32 val[32]; int keys, press, animwait, win;
    int has_menu, menu;                                       /* menu=N */
    int at, at_x[AT_MAX], at_z[AT_MAX], at_angle[AT_MAX];   /* Aya's stops from AT_FRAME on, at of them */
    int at_actor[AT_MAX];                                     /* -1, or the actor type she stands on */
    int seeded, difficulty; u32 seed;                         /* seed=: the randomizer's layout */
    int foes;                                                 /* foes: the mod's own enemy plan */
    unsigned nwant, nnever, nflag, nclear, nran; u32 want[8], never[8], ran_at[4], flag_bit[8], clear_bit[8]; u16 flag_var[8], clear_var[8];
    int gate; unsigned nshut, nopen; u32 shut[8], open[8];    /* gate: the real mod answers key tests */
    int held; unsigned nheld; u32 held_item[8];               /* keys=none / keys=D6,..: only those held */
    unsigned from, door; int has_door, has_arrive, floor; u32 arrive;   /* from=, door=, arrive=, floor */
    int full, swap; unsigned nhas, nlack; u32 has[4], lack[4];   /* bag=full, swap=N, has=, lack= */
    int stats; unsigned nforce; u16 force_to[4], force_room[4], force_from[4];   /* stats, force=5<M005:2 */
    unsigned nai; u16 ai_to[4], ai_room[4], ai_from[4];       /* ai=5<M012:5 */
    char id[48];                                              /* id=: echoed on the output line */
};

/* Gate lines: the mod host, started once for the first gate line's seed, its files in gate_dir. */
void pe_rand_test_options(int difficulty, int enemies);
static char gate_dir[1100];
static int gate_started, gate_difficulty, gate_foes; static u32 gate_seed;
static int start_gate(const Run *r) {
    char path[1200];
    FILE *f;
    if (gate_started) return gate_seed == r->seed && gate_difficulty == r->difficulty && gate_foes == r->foes;
    if (!*gate_dir || !SDL_CreateDirectory(gate_dir)) return 0;
    snprintf(path, sizeof(path), "%s/pe_mods.ini", gate_dir);
    if (!(f = fopen(path, "w"))) return 0;
    fprintf(f, "Manage mods/All mods=1\nManage mods/Randomizer=1\nRandomizer/Randomize new games=1\nRandomizer/Seed=%u\n", r->seed);
    fclose(f);
    snprintf(path, sizeof(path), "%s/prefs", gate_dir);   /* only its folder is used */
    PE_HOOK(PE_HOOK_INIT, (&(PeHookInit){0, path}));
    for (int i = 0; i < pe_menu_row_count(); ++i) {   /* not the Economy page's own Difficulty */
        const PeMenuRow *row = pe_menu_row(i);
        if (!strcmp(row->page, "Randomizer") && (!strcmp(row->label, "Randomize enemies") || !strcmp(row->label, "Difficulty")))
            FAIL("F1 still has a %s row", row->label);
    }
    pe_rand_test_options(r->difficulty, r->foes);
    gate_started = 1; gate_seed = r->seed; gate_difficulty = r->difficulty; gate_foes = r->foes;
    return 1;
}

/* The layout for a seed, as the mod makes it (pe_gift_safe's rule: bag items and ammo, no keys). */
static uint16_t layout[PE_RAND_SPOTS];
static int layout_ready, layout_difficulty; static u32 layout_seed;
static int gift_safe(unsigned id) {
    ItemDataRecord *item;
    if (!id || id > 0xFF || (id >= 0x38 && id <= 0x3E) || (id >= 0xC8 && id <= 0xDC) || !(item = Item_LookupBaseData(id - 1))) return 0;
    return item->kind == 10 || (item->kind >= 12 && item->kind <= 18);
}

/* pe_randomizer.c's scene-start patch: every site of the room holds its expected bytes, then each gets
 * its spot's item; else the room stays vanilla (a failure here: the table and the disc disagree). */
static void patch_room(unsigned room, u8 *assets) {
    size_t bytes = (size_t)g_RoomSectorTable[room - 1].roomSectors * 2048;
    for (unsigned i = 0; i < PE_RAND_SITES; ++i) {
        const PeRandSite *s = &g_pe_rand_sites[i];
        if (s->map != room) continue;
        for (unsigned w = 0; w < 4; ++w)
            if (s->at + 16 > bytes || word_at(assets, s->at + 4 * w) != s->expect[w]) { FAIL("M%03u site +%06X differs", room, s->at); return; }
    }
    for (unsigned i = 0; i < PE_RAND_SITES; ++i) {
        const PeRandSite *s = &g_pe_rand_sites[i];
        u32 item = layout[s->spot];
        if (s->map == room) memcpy(assets + s->at + 8 + 4 * s->operand, &item, 4);
    }
}

/* Scene_LinkContactTask (entity/allocation.c): start SELF's contact task for OTHER unless one runs. */
static void link_contact(FieldActor *self, FieldActor *other) {
    if (!self->script_cursor_1a0) return;
    for (ScriptNode *n = self->script_node_lists[1]; n; n = n->next)
        if ((n->flags & 1) && !(n->flags & 0x10) && n->trigger_value == other->field_sfx_id) return;
    ScriptNode *node = Script_AllocNode(self->script_cursor_1a0, 0);
    if (!node) return;
    node->flags |= 1; node->trigger_value = other->field_sfx_id;
    node->target18.coordinate = other->type_id; node->target1c = other->sub_id;
    if (self->script_node_lists[1]) { node->next = self->script_node_lists[1]; self->script_node_lists[1]->prev = node; }
    self->script_node_lists[1] = node;
}

static int hex_offset(const char *s, u32 *out) { return *s == '+' && sscanf(s + 1, "%x", out) == 1; }

static int parse_run(char *line, Run *r) {
    memset(r, 0, sizeof(*r));
    char *tok = strtok(line, " \t\r\n");
    if (!tok || (tok[0] != 'M' && tok[0] != 'm')) return 0;
    r->room = (unsigned)atoi(tok + 1);
    if (!(tok = strtok(NULL, " \t\r\n"))) return 0;
    r->area = (u32)strtoul(tok, NULL, 0);
    if (!(tok = strtok(NULL, " \t\r\n"))) return 0;
    r->left = (u32)strtoul(tok, NULL, 0);
    while ((tok = strtok(NULL, " \t\r\n"))) {
        if (!strcmp(tok, "keys=all")) { r->keys = 1; continue; }
        if (!strcmp(tok, "keys=none")) { r->held = 1; continue; }
        if (!strncmp(tok, "keys=", 5)) {
            r->held = 1;
            for (char *p = tok + 5; *p; ) {
                char *end; u32 item = (u32)strtoul(p, &end, 16);
                if (end == p || r->nheld == 8) return 0;
                r->held_item[r->nheld++] = item;
                p = *end == ',' ? end + 1 : end;
                if (*end && *end != ',') return 0;
            }
            continue;
        }
        if (!strncmp(tok, "id=", 3)) { snprintf(r->id, sizeof(r->id), "%s", tok + 3); continue; }
        if (!strcmp(tok, "press")) { r->press = 1; continue; }
        if (!strcmp(tok, "animwait")) { r->animwait = 1; continue; }
        if (!strcmp(tok, "bag=full")) { r->full = 1; continue; }
        if (!strncmp(tok, "swap=", 5)) { r->swap = atoi(tok + 5) + 1; continue; }
        if (!strncmp(tok, "has=", 4)) { if (r->nhas == 4) return 0; r->has[r->nhas++] = (u32)strtoul(tok + 4, NULL, 16); continue; }
        if (!strncmp(tok, "lack=", 5)) { if (r->nlack == 4) return 0; r->lack[r->nlack++] = (u32)strtoul(tok + 5, NULL, 16); continue; }
        if (!strncmp(tok, "menu=", 5)) { r->has_menu = 1; r->menu = atoi(tok + 5); continue; }
        if (!strcmp(tok, "win")) { r->win = 1; continue; }
        if (!strcmp(tok, "stats")) { r->stats = 1; continue; }
        if (!strncmp(tok, "force=", 6)) {
            unsigned to, room, from;
            if (r->nforce == 4 || sscanf(tok + 6, "%u<M%u:%u", &to, &room, &from) != 3) return 0;
            r->force_to[r->nforce] = (u16)to; r->force_room[r->nforce] = (u16)room; r->force_from[r->nforce++] = (u16)from;
            r->stats = 1;
            continue;
        }
        if (!strncmp(tok, "ai=", 3)) {
            unsigned to, room, from;
            if (r->nai == 4 || sscanf(tok + 3, "%u<M%u:%u", &to, &room, &from) != 3 || to >= 16) return 0;
            r->ai_to[r->nai] = (u16)to; r->ai_room[r->nai] = (u16)room; r->ai_from[r->nai++] = (u16)from;
            continue;
        }
        if (!strcmp(tok, "gate")) { r->gate = 1; continue; }
        if (!strcmp(tok, "foes")) { r->foes = 1; continue; }
        if (!strcmp(tok, "floor")) { r->floor = 1; continue; }
        if (!strncmp(tok, "from=M", 6)) { r->from = (unsigned)atoi(tok + 6); if (!r->from || r->from > SCENE_ROOM_COUNT) return 0; continue; }
        if (!strncmp(tok, "door=M", 6)) { r->door = (unsigned)atoi(tok + 6); r->has_door = 1; continue; }
        if (!strcmp(tok, "door=none")) { r->has_door = 2; continue; }
        if (!strncmp(tok, "arrive=", 7)) { r->arrive = (u32)strtoul(tok + 7, NULL, 0); r->has_arrive = 1; continue; }
        if (!strncmp(tok, "shut=", 5)) { if (r->nshut == 8 || !hex_offset(tok + 5, &r->shut[r->nshut++])) return 0; continue; }
        if (!strncmp(tok, "open=", 5)) { if (r->nopen == 8 || !hex_offset(tok + 5, &r->open[r->nopen++])) return 0; continue; }
        if (!strncmp(tok, "at=@", 4)) {
            if (r->at == AT_MAX || sscanf(tok + 4, "%d,%d", &r->at_actor[r->at], &r->at_angle[r->at]) < 1) return 0;
            ++r->at;
            continue;
        }
        if (!strncmp(tok, "at=", 3)) {
            if (r->at == AT_MAX || sscanf(tok + 3, "%d,%d,%d", &r->at_x[r->at], &r->at_z[r->at], &r->at_angle[r->at]) < 2) return 0;
            r->at_actor[r->at++] = -1;
            continue;
        }
        if (!strncmp(tok, "seed=", 5)) {
            if (sscanf(tok + 5, "%u,%d", &r->seed, &r->difficulty) != 2 || r->difficulty < 0 || r->difficulty >= PE_RAND_DIFFICULTIES) return 0;
            r->seeded = 1;
            continue;
        }
        if (!strncmp(tok, "want=", 5)) { if (r->nwant == 8 || !hex_offset(tok + 5, &r->want[r->nwant++])) return 0; continue; }
        if (!strncmp(tok, "ran=", 4)) { if (r->nran == 4 || !hex_offset(tok + 4, &r->ran_at[r->nran++])) return 0; continue; }
        if (!strncmp(tok, "never=", 6)) { if (r->nnever == 8 || !hex_offset(tok + 6, &r->never[r->nnever++])) return 0; continue; }
        if (!strncmp(tok, "flag=V", 6)) {
            char *bar = strchr(tok, '|');
            if (r->nflag == 8 || !bar) return 0;
            r->flag_var[r->nflag] = (u16)strtoul(tok + 6, NULL, 0); r->flag_bit[r->nflag++] = (u32)strtoul(bar + 1, NULL, 0);
            continue;
        }
        if (!strncmp(tok, "clear=V", 7)) {
            char *bar = strchr(tok, '|');
            if (r->nclear == 8 || !bar) return 0;
            r->clear_var[r->nclear] = (u16)strtoul(tok + 7, NULL, 0); r->clear_bit[r->nclear++] = (u32)strtoul(bar + 1, NULL, 0);
            continue;
        }
        char *eq = strchr(tok, '=');
        if (tok[0] != 'V' || !eq || r->nvars == 32) return 0;
        r->var[r->nvars] = (u16)strtoul(tok + 1, NULL, 0); r->val[r->nvars++] = (u32)strtoul(eq + 1, NULL, 0);
    }
    return r->room >= 1 && r->room <= SCENE_ROOM_COUNT ? 1 : 2;   /* 2: M5000, M8888 (main-loop scenes) */
}

static unsigned runs_done, runs_door, runs_spin, gives_checked, checks_done;
static unsigned char spot_given[PE_RAND_SPOTS];

static void set_vars(const Run *r) {
    g_ScriptVariableBank[SCRIPT_VAR_AREA] = r->area; g_ScriptVariableBank[SCRIPT_VAR_ROOM] = r->left;
    for (unsigned i = 0; i < r->nvars; ++i) if (r->var[i] < VARS) g_ScriptVariableBank[r->var[i]] = r->val[i];
    /* keys=D6,..: those keys go in free bag slots too (the mod reads the bag at scene start), taken out again
     * before the next line; the rest of the bag stays */
    static int added[8], nadded;
    enum { BAG = sizeof(g_AyaInventoryItems) / sizeof(*g_AyaInventoryItems) };
    while (nadded) g_AyaInventoryItems[added[--nadded]] = 0;
    for (unsigned i = 0, slot = 0; r->held && i < r->nheld && nadded < 8; ++i) {
        while (slot < BAG && g_AyaInventoryItems[slot]) ++slot;
        if (slot < BAG) { g_AyaInventoryItems[slot] = (s16)r->held_item[i]; added[nadded++] = (int)slot++; }
    }
}

static void frame_start(const Run *r) {
    if (r->gate) PE_HOOK(PE_HOOK_FRAME, NULL);
    for (unsigned c = 0; c < combat_count; ++c) {
        Combat *k = &combats[c];
        if (k->done) continue;
        k->done = 1;
        for (int i = 0; i < PE_RAND_STATS; ++i)
            k->v[i] = stat_of((const FieldActorState *)&g_BattleEnemySlots[k->slot].combatant, PE_RAND_STAT_CODES[i]);
    }
}

static void do_run(const Run *r, FILE *out) {
    char name[16]; snprintf(name, sizeof(name), "m%04ui", r->room);
    u32 token = (u32)Text_ParseBase32Id((signed char *)name);
    if (!g_RoomSectorTable[r->room - 1].roomSectors) { FAIL("M%03u is no room on the disc", r->room); return; }
    if (r->gate && (!r->seeded || !start_gate(r))) { FAIL("M%03u: a gate line needs the gate lines' one seed= (mod host in %s)", r->room, gate_dir); return; }
    if (r->nforce && !r->gate) { FAIL("M%03u: force= needs gate (the real mod)", r->room); return; }
    if (r->nai && !r->gate) { FAIL("M%03u: ai= needs gate (the real mod)", r->room); return; }
    if (r->foes && !r->gate) { FAIL("M%03u: foes needs gate (the real mod)", r->room); return; }
    if (r->gate) PE_HOOK(PE_HOOK_NEW_GAME, (&(PeHookNewGame){PE_NEW_GAME_TITLE}));
    if (!r->foes) {   /* foes: the plan the mod's New Game gave the switches stays */
        pe_rand_battle_clear();
        pe_rand_ai_clear();
        pe_rand_model_clear();
    }
    for (unsigned i = 0; i < r->nai && !r->foes; ++i)
        if (!pe_rand_ai_force(r->room, r->ai_to[i], r->ai_room[i], r->ai_from[i]) ||
            !pe_rand_model_force(r->room, r->ai_to[i], r->ai_room[i], r->ai_from[i])) {
            FAIL("M%03u: ai=%u<M%03u:%u: not taken", r->room, r->ai_to[i], r->ai_room[i], r->ai_from[i]);
            return;
        }
    for (unsigned i = 0; i < r->nforce; ++i)
        if (!pe_rand_battle_force(r->room, r->force_to[i], r->force_room[i], r->force_from[i])) {
            FAIL("M%03u: force=%u<M%03u:%u: not both in pe_rand_enemies.h", r->room, r->force_to[i], r->force_room[i], r->force_from[i]);
            return;
        }
    Entity_ResetAllPools();   /* zeroes the variables, node flags and actors, as a new game does */
    if (r->from) {   /* the room Aya leaves: the mod sees it start and moves its door */
        char from_name[16]; snprintf(from_name, sizeof(from_name), "m%04ui", r->from);
        u32 from_token = (u32)Text_ParseBase32Id((signed char *)from_name);
        if (!r->gate) { FAIL("M%03u: from= needs gate (the real mod)", r->room); return; }
        if (Scene_LoadRoom(from_token)) { FAIL("M%03u: from=M%03u doesn't load", r->room, r->from); return; }
        set_vars(r);
        g_SceneDispatchToken = from_token;
        PE_HOOK(PE_HOOK_SCENE_START, NULL);
    }
    int status = Scene_LoadRoom(token);
    if (status) {
        if (out) fprintf(out, "run\tM%03u\tV0x4a=%#x\tV0x1=%#x\tend=load %d%s%s\n", r->room, r->area, r->left, status, r->id[0] ? "\tid=" : "", r->id);
        return;
    }
    const u8 *assets = (const u8 *)g_GameState.loaded_scene_assets;
    const SceneRoomDirectory *directory = (const void *)(assets + word_at(assets, 4));
    u32 table = directory->taskBlock & 0x3FFFFF;
    block_bytes = word_at(assets, table) & 0xFFFFFF;
    size_t block_in_room = word_at(assets, table + 4) & 0xFFFFFF;
    block = assets + block_in_room;
    free(ran); ran = calloc(block_bytes / 4 + 1, 1);
    free(anim_left); anim_left = calloc(block_bytes / 4 + 1, 1);
    set_vars(r);   /* before the mod's scene start: it reads V0x4a and may set V0x1 */
    if (r->seeded) {
        if (!layout_ready || layout_seed != r->seed || layout_difficulty != r->difficulty) {
            PeRandResult result;
            pe_rand_generate(g_pe_rand_spots, PE_RAND_SPOTS, r->seed, r->difficulty, gift_safe, layout, &result);
            layout_ready = 1; layout_seed = r->seed; layout_difficulty = r->difficulty;
        }
        if (r->gate) {   /* the mod patches the room itself, under the room's own token (pe_room_number) */
            g_SceneDispatchToken = token;
            PE_HOOK(PE_HOOK_SCENE_START, NULL);
            for (unsigned i = 0; i < PE_RAND_SITES; ++i) {
                const PeRandSite *s = &g_pe_rand_sites[i];
                if (s->map == r->room && word_at(assets, s->at + 8 + 4 * s->operand) != layout[s->spot])
                    FAIL("M%03u site +%06X: the mod didn't patch in seed %06u's item", r->room, s->at, r->seed);
            }
        } else patch_room(r->room, (u8 *)g_GameState.loaded_scene_assets);
    }
    /* ai=: the banks are filled; the mod gives the scripts (and models) before the actors spawn. The slots
     * the room doesn't install are emptied first (earlier lines' rooms filled some; not the global ones, 0-7
     * and 85-114), so pm= is what the mod adds. */
    char pm[256] = ""; size_t pm_len = 0;
    memset(ai_steps, 0, sizeof(ai_steps));
    if (r->nai || r->foes) {
        static const void *slot_was[85 + 104];
        RelocBlock *tasks = g_GameState.bank_reset_944[0];
        const u8 *record = assets + (directory->scripts & 0x3FFFFF);
        u8 own_slot[85 + 104] = {0};
        for (unsigned i = 0; i < directory->scripts >> 22; ++i) if (record[12 * i + 7] < 85 + 104) own_slot[record[12 * i + 7]] = 1;
        for (unsigned i = 0; i < 85 + 104; ++i) {
            if (!own_slot[i] && i >= 8 && (i < 85 || i > 114)) SceneRoom_SetScriptSlot(i, NULL);
            slot_was[i] = SceneRoom_ScriptSlot(i);
        }
        PE_HOOK(PE_HOOK_PLAYER_ROWS, NULL);
        for (unsigned i = 0; i < 85 + 104; ++i)
            if (SceneRoom_ScriptSlot(i) != slot_was[i] && pm_len < sizeof(pm) - 8)
                pm_len += (size_t)snprintf(pm + pm_len, sizeof(pm) - pm_len, "%s%u", pm_len ? "," : "", i);
        for (unsigned i = 0; i < r->nai; ++i) {
            const u8 *own = r->ai_to[i] < tasks->count ? tasks->offsets[r->ai_to[i]] : NULL;
            if (!own || (own >= block && own < block + block_bytes))
                FAIL("M%03u: ai=%u<M%03u:%u: the mod gave no script", r->room, r->ai_to[i], r->ai_room[i], r->ai_from[i]);
        }
        if (r->foes) {   /* exactly the plan's swaps in this room */
            static uint16_t plan[PE_RAND_FOES];
            int moved[16] = {0};
            pe_rand_foes_generate(g_pe_rand_foes, g_pe_rand_foe_ok[0], PE_RAND_FOES, r->seed, plan);
            for (unsigned i = 0; i < PE_RAND_FOES; ++i)
                if (g_pe_rand_foes[i].room == r->room && plan[i] != i) moved[g_pe_rand_foes[i].entity] = 1;
            for (unsigned e = 0; e < tasks->count && e < 16; ++e) {
                const int given = tasks->offsets[e] && !(tasks->offsets[e] >= block && tasks->offsets[e] < block + block_bytes);
                if (given != moved[e]) FAIL("M%03u: foes: actor %u %s", r->room, e, moved[e] ? "moved by the plan got no script" : "got a script the plan doesn't give");
                checks_done += moved[e];
            }
        }
    }

    static u32 before[VARS];
    u32 left = g_ScriptVariableBank[SCRIPT_VAR_ROOM];
    if (r->has_arrive && left != r->arrive) FAIL("M%03u V0x4a=%#x: V0x1 is %#x after the scene start, not %#x", r->room, r->area, left, r->arrive);
    checks_done += r->has_arrive;
    memcpy(before, g_ScriptVariableBank, sizeof(before));
    keys_all = r->keys; held_only = r->held; held_count = r->nheld; memcpy(held_items, r->held_item, sizeof(held_items));
    press = r->press; animwait = r->animwait; menu = r->has_menu ? r->menu : -1; battle = 0; spin = 0; gave_count = 0; zone_count = 0;
    bag_full = r->full; swap = r->swap - 1; swapped_at = 64;
    s16 bag_was[64]; int bag_slots = g_InvItemPtr && g_InvSlotLimit > 0 ? (g_InvSlotLimit < 64 ? g_InvSlotLimit : 64) : 0;
    if (bag_full) {
        if (!bag_slots) { FAIL("M%03u: bag=full but the bag isn't set up", r->room); return; }
        memcpy(bag_was, g_InvItemPtr, (size_t)bag_slots * sizeof(*bag_was));
        for (int i = 0; i < bag_slots; ++i) if (!g_InvItemPtr[i]) g_InvItemPtr[i] = 0x06;
    }
    gate = r->gate; asked_count = 0;
    win = r->win; fighting = -1; won = 0; awaited = -1; set6_at = -1; state_read = 0;
    stats = r->stats; combat_count = 0;
    if (stats) {
        memcpy(g_DrawPrimGeneratorWords, random_start, sizeof(random_start));
        memset(g_BattleEnemySlots, 0, sizeof(g_BattleEnemySlots));
        g_EntityInstanceCounter = 0; g_BattleLiveEnemyCount = 0;
    }
    g_BattleModeState = 0; D_8009D2E8 &= ~1;
    g_FieldPadBits = g_FieldPadPressed = g_FieldPadReleased = 0;
    memset(g_FieldPadHeldFrameCounts, 0, 32 * sizeof(*g_FieldPadHeldFrameCounts));
    g_GameStateFlags &= ~0x2100u; g_SceneDispatchToken = token; g_FieldSfxQueueCount = 0;
    g_EntitySerialCounter = 1;
    /* No models: actors spawn on Scene_LoadMap's modelless path (no render or animation blocks). */
    memset(g_GameState.bank_slots, 0, sizeof(g_GameState.bank_slots));
    Entity_InitFreePool(); Script_InitNodeFreeList(); Entity_RelocateSceneData();
    Script_DrawSyncAndFlush();

    unsigned frame;
    for (frame = 0; frame < (r->at ? AT_FRAME + r->at * AT_FRAMES : FRAMES) && !spin && !(g_GameStateFlags & 0x2000); ++frame) {
        frame_steps = 0; cur_frame = frame;
        if (stats) frame_start(r);
        if (fighting >= 0) {   /* the battle code's part of a won battle */
            int since = ++fighting;
            if (((g_BattleModeState == 6 || g_BattleModeState == 5) && (int)frame >= set6_at + BATTLE_FRAMES) || (g_BattleModeState == 0 && since >= BATTLE_FRAMES * 3)) {
                g_BattleModeState = awaited == 12 ? 12 : 7; D_8009D2E8 &= ~1; fighting = BATTLE_FRAMES * 3;
            } else if ((g_BattleModeState == 7 || g_BattleModeState == 12) && since >= BATTLE_FRAMES * 4) {
                if (g_BattleModeState == 7) g_BattleModeState = 9;
                fighting = -1; ++won;
            }
        }
        if (r->at && frame >= AT_FRAME && g_PlayerEntity) {
            FieldActor *aya = g_PlayerEntity, *on = 0;
            int stop = (int)(frame - AT_FRAME) / AT_FRAMES;
            if (r->at_actor[stop] >= 0)
                for (on = g_FieldActorListHead; on && (on == aya || on->type_id != r->at_actor[stop] || (on->flags & 0x10)); on = on->next) {}
            if (on) { aya->pos_x = aya->base_x = on->pos_x; aya->pos_z = aya->base_z = on->pos_z; }
            else if (r->at_actor[stop] < 0) { aya->pos_x = aya->base_x = r->at_x[stop] << 16; aya->pos_z = aya->base_z = r->at_z[stop] << 16; }
            aya->rot_y = (short)r->at_angle[stop];
            if (on) { link_contact(aya, on); link_contact(on, aya); }
        }
        if (press) {   /* the button, as Field_HandleStateTransition leaves it for this frame */
            unsigned k = frame >= AT_FRAME ? (frame - AT_FRAME) % AT_FRAMES : 0;
            u32 down = r->at && frame >= AT_FRAME && k >= PRESS_AT && (k - PRESS_AT) % PRESS_EVERY == 0 ? press_bits : 0;
            g_FieldPadPressed = down & ~g_FieldPadBits; g_FieldPadReleased = g_FieldPadBits & ~down; g_FieldPadBits = down;
            for (int b = 0; b < 32; ++b) if (press_bits >> b & 1) g_FieldPadHeldFrameCounts[b] = down ? g_FieldPadHeldFrameCounts[b] + 1 : 0;
        }
        Scene_UpdateEntityList();
        for (FieldActor *a = g_FieldActorListHead; a && !spin && !(g_GameStateFlags & 0x2000); a = a->next)
            if (!(a->flags & 0x10)) dispatch(a);
        /* End of Entity_FrameUpdate: freed nodes and dead actors back to their pools, new actors settled. */
        Script_GarbageCollectNodes();
        Entity_CollectGarbage();
        for (FieldActor *a = g_FieldActorListHead; a; a = a->next) a->flags &= ~0x10000000;
    }
    if (stats) frame_start(r);
    ++runs_done;
    for (unsigned i = 0; r->seeded && i < gave_count && i < swapped_at; ++i)
        for (unsigned g = 0; g < PE_RAND_GIVES; ++g)
            if (g_pe_rand_gives[g].map == r->room && g_pe_rand_gives[g].at == block_in_room + gave[i]) {
                unsigned spot = g_pe_rand_gives[g].spot;
                ++gives_checked; spot_given[spot] = 1;
                if (got[i] != layout[spot])
                    FAIL("M%03u +%06X (spot %u): gave %#x, seed %06u's layout has %#x", r->room, g_pe_rand_gives[g].at, spot,
                         got[i], r->seed, layout[spot]);
            }
    for (unsigned k = 0; k < r->nwant + r->nnever; ++k) {
        int want = k < r->nwant; u32 at = want ? r->want[k] : r->never[k - r->nwant];
        unsigned i = 0;
        while (i < gave_count && gave[i] != at) ++i;
        if ((i < gave_count) != want) FAIL("M%03u V0x4a=%#x: the give at +%X %s", r->room, r->area, at, want ? "didn't run" : "ran");
        ++checks_done;
    }
    for (unsigned k = 0; k < r->nran; ++k, ++checks_done)
        if (r->ran_at[k] / 4 > block_bytes / 4 || !ran[r->ran_at[k] / 4])
            FAIL("M%03u V0x4a=%#x: the room's step at +%X didn't run", r->room, r->area, r->ran_at[k]);
    for (unsigned k = 0; k < r->nflag; ++k, ++checks_done)
        if (!(g_ScriptVariableBank[r->flag_var[k]] & r->flag_bit[k]))
            FAIL("M%03u V0x4a=%#x: V%#x|%#x not set", r->room, r->area, r->flag_var[k], r->flag_bit[k]);
    for (unsigned k = 0; k < r->nclear; ++k, ++checks_done)
        if (g_ScriptVariableBank[r->clear_var[k]] & r->clear_bit[k])
            FAIL("M%03u V0x4a=%#x: V%#x has %#x set", r->room, r->area, r->clear_var[k],
                 (unsigned)(g_ScriptVariableBank[r->clear_var[k]] & r->clear_bit[k]));
    for (unsigned k = 0; k < r->nshut + r->nopen; ++k, ++checks_done) {
        int shut = k < r->nshut; u32 at = shut ? r->shut[k] : r->open[k - r->nshut];
        unsigned i, ran_at = 0, opened = 0, l = 0;
        for (i = 0; i < asked_count; ++i)
            if (asked[i] == at) { ++ran_at; opened += answer[i] != 0; }
        if (!ran_at) { FAIL("M%03u V0x4a=%#x: the key test at +%X didn't run", r->room, r->area, at); continue; }
        if (shut ? opened != 0 : opened == 0)
            FAIL("M%03u V0x4a=%#x: the key test at +%X answered %s", r->room, r->area, at, shut ? "held (gate open)" : "not held (gate shut)");
        while (l < PE_RAND_LOCKS && !(g_pe_rand_locks[l].map == r->room && g_pe_rand_locks[l].at == block_in_room + at)) ++l;
        if (shut && (l == PE_RAND_LOCKS || layout[g_pe_rand_locks[l].spot] == g_pe_rand_spots[g_pe_rand_locks[l].spot].vanilla))
            FAIL("M%03u +%X: shut= needs a table lock whose key seed %06u moved", r->room, at, r->seed);
    }
    if (r->has_door == 1 && (!(g_GameStateFlags & 0x2000) || token_room(g_SceneDispatchToken) != (int)r->door))
        FAIL("M%03u V0x4a=%#x V0x1=%#x: the run didn't end at a door to M%03u", r->room, r->area, left, r->door);
    if (r->has_door == 2 && (g_GameStateFlags & 0x2000))
        FAIL("M%03u V0x4a=%#x V0x1=%#x: the run ended at a door to M%03u", r->room, r->area, left, token_room(g_SceneDispatchToken));
    checks_done += r->has_door != 0;
    for (unsigned k = 0; k < r->nhas + r->nlack; ++k, ++checks_done) {
        int has = k < r->nhas, in_bag = 0; u32 item = has ? r->has[k] : r->lack[k - r->nhas];
        for (int i = 0; i < g_InvSlotLimit && i < 64; ++i) in_bag |= (u32)g_InvItemPtr[i] == item;   /* overflow slots too */
        if (in_bag != has) FAIL("M%03u V0x4a=%#x: item %#x %s the bag at the end", r->room, r->area, item, has ? "isn't in" : "is in");
    }
    if (bag_full) {   /* and no overflow slot left (a gift on a full bag, pe_randomizer.c) */
        for (int i = bag_slots; i < g_InvSlotLimit && i < 50; ++i) g_InvItemPtr[i] = 0;
        g_InvOverflowSlotCount = 0; g_InvSlotLimit = Inv_GetAyaSlotLimit();
        memcpy(g_InvItemPtr, bag_was, (size_t)bag_slots * sizeof(*bag_was));
    }
    if (!out) return;
    fprintf(out, "run\tM%03u\tV0x4a=%#x\tV0x1=%#x\tend=", r->room, r->area, left);
    if (g_GameStateFlags & 0x2000) {
        int to = token_room(g_SceneDispatchToken); ++runs_door;
        if (to >= 0) fprintf(out, "door M%03d", to); else fprintf(out, "door %08x", (u32)g_SceneDispatchToken);
    } else if (spin) { fputs("spin", out); ++runs_spin; }
    else fputs("idle", out);
    fprintf(out, "\tframes=%u\talive=", frame);
    int first = 1;
    for (FieldActor *a = g_FieldActorListHead; a; a = a->next)
        if (!(a->flags & 0x10)) { fprintf(out, "%s%u", first ? "" : ",", a->type_id); first = 0; }
    fputs("\tgave=", out);
    for (unsigned i = 0; i < gave_count; ++i) fprintf(out, "%s+%06X", i ? "," : "", gave[i]);
    fputs("\tset=", out);
    first = 1;
    for (unsigned v = 0; v < VARS; ++v)
        if (g_ScriptVariableBank[v] != before[v]) { fprintf(out, "%sV%#x:%#x>%#x", first ? "" : ",", v, before[v], g_ScriptVariableBank[v]); first = 0; }
    fputs("\tsteps=", out);
    first = 1;
    for (size_t w = 0; w <= block_bytes / 4; ++w)
        if (ran[w]) { fprintf(out, "%s%#zx", first ? "" : ",", w * 4); first = 0; }
    if (!print_floor(out) && r->floor) FAIL("M%03u V0x4a=%#x V0x1=%#x: Aya doesn't stand on the floor", r->room, r->area, left);
    checks_done += r->floor;
    for (int i = 0; i < r->at; ++i)
        if (r->at_actor[i] >= 0) fprintf(out, "%s@%d,%d", i ? ";" : "\tat=", r->at_actor[i], r->at_angle[i]);
        else fprintf(out, "%s%d,%d,%d", i ? ";" : "\tat=", r->at_x[i], r->at_z[i], r->at_angle[i]);
    if (r->nai) {
        fputs("\tai=", out);
        for (unsigned i = 0; i < r->nai; ++i) fprintf(out, "%s%u<M%03u:%u", i ? "," : "", r->ai_to[i], r->ai_room[i], r->ai_from[i]);
        fputs("\taisteps=", out);
        for (unsigned i = 0; i < r->nai; ++i) {
            fprintf(out, "%se%u:%u", i ? "," : "", r->ai_to[i], ai_steps[r->ai_to[i]]);
            if (!ai_steps[r->ai_to[i]]) FAIL("M%03u: ai=%u<M%03u:%u: the actor ran none of the given script", r->room, r->ai_to[i], r->ai_room[i], r->ai_from[i]);
        }
        fprintf(out, "\tpm=%s", pm);
        if (r->win && !won) FAIL("M%03u: ai= line won no battle", r->room);
        checks_done += r->nai;
    }
    if (press) fputs("\tpress=1", out);
    if (animwait) fputs("\tanimwait=1", out);
    if (r->has_menu) fprintf(out, "\tmenu=%d", r->menu);
    if (r->full) {
        fputs("\tbag=full", out);
        if (r->swap) fprintf(out, "\tswap=%d", r->swap - 1);
        fputs("\tbagged=", out);
        for (unsigned i = 0; i < gave_count; ++i) fprintf(out, "%s+%06X:%d", i ? "," : "", gave[i], added_ok[i]);
    }
    if (r->seeded) {
        fprintf(out, "\tseed=%u,%d\tgot=", r->seed, r->difficulty);
        for (unsigned i = 0; i < gave_count; ++i) fprintf(out, "%s%#x", i ? "," : "", got[i]);
    }
    if (r->gate || r->held) {
        fputs("\tasked=", out);
        for (unsigned i = 0; i < asked_count; ++i) fprintf(out, "%s+%06X:%#x>%u", i ? "," : "", asked[i], asked_item[i], answer[i]);
    }
    if (won) fprintf(out, "\twon=%d", won);
    if (r->nforce) {
        fputs("\tforce=", out);
        for (unsigned i = 0; i < r->nforce; ++i) fprintf(out, "%s%u<M%03u:%u", i ? "," : "", r->force_to[i], r->force_room[i], r->force_from[i]);
    }
    if (r->stats) {
        fputs("\tcombat=", out);
        for (unsigned c = 0; c < combat_count; ++c) {
            fprintf(out, "%se%u#%u:", c ? ";" : "", combats[c].type, combats[c].instance);
            for (int i = 0; i < PE_RAND_STATS; ++i) fprintf(out, "%s%d", i ? "," : "", combats[c].v[i]);
        }
    }
    if (battle && (!win || fighting >= 0)) fputs("\tbattle=1", out);
    if (r->id[0]) fprintf(out, "\tid=%s", r->id);
    fputc('\n', out);
}

int main(int argc, char **argv) {
    /* PE_RUN_DISC2: the rooms from Disc 2's image (PE_TEST_DISC2), as after the disc change */
    const char *path = getenv(getenv("PE_RUN_DISC2") ? "PE_TEST_DISC2" : "PE_TEST_DISC");
    if (!path || !*path) return getenv("PE_REQUIRE_GAME_ASSETS") ? 1 : 77;
    PeDisc disc = {0};
    /* Nothing is drawn, but ResetGraph opens psyz: keep it windowless whichever way the test runs. */
    if (!SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "PSYZ_HEADLESS", "1", true) ||
        !SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "SDL_AUDIODRIVER", "dummy", true)) return 1;
    if (!SDL_Init(SDL_INIT_EVENTS) || !pe_disc_open(&disc, path)) return 1;
    ResetGraph(0); CdRom_BindImage(&disc, NULL);
    if (Asset_OpenPeImage() != 0) return 1;
    Boot_InitGameState(); D_8009CDC8 = 'm';
    /* The resident data and a new game's bag: spawning Aya builds her weapon and armour lists. */
    if (Scene_LoadSceneData() < 0) { fputs("Scene_LoadSceneData failed\n", stderr); return 1; }
    Inv_InitNewGameInventory();
    for (size_t i = 0; i < sizeof(real_ops) / sizeof(*real_ops); ++i) is_real[real_ops[i]] = 1;
    memcpy(random_start, g_DrawPrimGeneratorWords, sizeof(random_start));
    trace = getenv("PE_RUN_TRACE") != NULL;
    /* A third argument: start at that runs line (1-based) and append, after a run that crashed. */
    unsigned from = argc > 3 ? (unsigned)atoi(argv[3]) : 1;
    FILE *out = argc > 1 ? fopen(argv[1], from > 1 ? "a" : "w") : NULL;
    if (argc > 1 && !out) return 1;
    if (argc > 1) snprintf(gate_dir, sizeof(gate_dir), "%s.mods", argv[1]);
    if (argc > 2) {
        FILE *in = fopen(argv[2], "r");
        if (!in) return 1;
        char line[1024]; unsigned n = 0, cover = 0;
        while (fgets(line, sizeof(line), in)) {
            ++n; Run r;
            if (!strncmp(line, "#cover", 6)) cover = 1;
            if (n < from || line[0] == '#' || line[0] == '\n') continue;
            int read = parse_run(line, &r);
            if (!read) { FAIL("runs line %u can't be read", n); continue; }
            if (read == 2) {   /* no room to run, but one output line per runs line all the same */
                if (out) { fprintf(out, "run\tM%03u\tV0x4a=%#x\tV0x1=%#x\tend=noroom\n", r.room, r.area, r.left); fflush(out); }
                continue;
            }
            if (trace) fprintf(stderr, "line %u\n", n);
            do_run(&r, out);
            if (out) fflush(out);   /* one line per run on disk: a crash names the next line */
        }
        fclose(in);
        /* A restart (from > 1) didn't see the earlier lines' gives. */
        for (unsigned s = 0; cover && from == 1 && s < PE_RAND_SPOTS; ++s)
            if (!spot_given[s])
                for (unsigned g = 0; g < PE_RAND_GIVES; ++g)
                    if (g_pe_rand_gives[g].spot == s) { FAIL("spot %u (M%03u +%06X): no seeded line handed it out", s, g_pe_rand_gives[g].map, g_pe_rand_gives[g].at); break; }
    } else {
        for (unsigned room = 1; room <= SCENE_ROOM_COUNT; ++room) {
            if (!g_RoomSectorTable[room - 1].roomSectors) continue;
            Run r = { .room = room };
            do_run(&r, out);
        }
    }
    if (out) fclose(out);
    free(ran);
    CdRom_BindImage(NULL, NULL); pe_disc_close(&disc);
    if (failures) { fprintf(stderr, "%u failures\n", failures); return 1; }
    printf("Randomizer runs: %u room runs, %u ended at a door, %u spun; %u table gives handed out the layout's item, %u checks held\n",
           runs_done, runs_door, runs_spin, gives_checked, checks_done);
    return 0;
}
