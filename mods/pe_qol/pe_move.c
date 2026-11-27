/*
 * Movement: Always run, run / walk / combat speed, battle-start speed. Ported from the psxrecomp
 * build (src/mods/pe_mod_menu.c, "movement"). F1 > PE Enhancements.
 *
 * SPEED: scale only the local multiplier in the original player-input walk/run
 * handlers. Never write BattleEntity.moveFactor: scripted walks and encounter
 * entry share that field and may deliberately use the same factors as our options.
 * The combat setting applies only while Aya can move (battle phase 0).
 *
 * ALWAYS RUN: the run bit of the field input is input bit 0 of the button mapping table
 * (g_FieldRenderFlagTable[0], Circle by default). In free field movement with Always run on it
 * uses a local d-pad mask, so moving means running, and no mask while the run button is held.
 * The original button mapping is never modified. Only the run button walks: the game drops the
 * run bit while the action button is held (Cross over Circle), so after the poll the bit is put
 * back for a d-pad direction with Cross held (issue 205). Examining works while running.
 *
 * ANALOG: the game runs when the stick is past the "Analog run threshold" option (a radius,
 * the same on diagonals) and ignores the run button. With Always run on, before the game reads
 * the pad (PE_HOOK_PAD_READ), the stick is reshaped, both axes scaled together so the steering
 * angle is unchanged: to run, out until the larger axis reaches its edge; to walk (run button
 * held), in to just inside the threshold. With Always run off the stick is left alone.
 *
 * BATTLE START: while Aya walks to her spot (phase 7), scale the local script
 * speed before its original destination clamp, and the local animation increment.
 * No mod-owned velocity or animation step persists in the actor. Off by default.
 */
#include <stdint.h>

#include "pe_game.h"
#include "pe_mod_host.h"
#include "pe_mod_menu.h"
#include "pe_pages.h"
#include "pe1/boot.h"
#include "pe1/field_actor.h"
#include "pe1/pad_state.h"
#include "pe1/game_state.h"

#define PE_RUN_MAP_DPAD     0x00F0u   /* up/right/down/left, internal pad bits */
#define PE_STICK_CENTER     0x80
#define PE_FIELD_RUN        0x1u          /* field bit0: run */
#define PE_FIELD_DIRS       0x78u         /* field bits3..6: directions */
#define PE_FIELD_ACTION     0x20000300u   /* field bits the game lets win over run */
#define PE_MOVE_FACTOR_STOCK 0x10000
#define PE_ANIM_STEP_STOCK  0x10000
#define PE_ENTRY_ACTION_MODE 5u

static const char *const s_speed_names[] = { "Normal", "1.25x", "1.5x", "1.75x", "2x", "2.25x", "2.5x", "3x" };
static const int32_t s_speed_factor[] = { 0x10000, 0x14000, 0x18000, 0x1C000, 0x20000, 0x24000, 0x28000, 0x30000 };
#define PE_SPEED_CHOICES 8
static const char *const s_entry_names[] = { "Normal", "1.5x", "2x", "3x", "4x" };
static const int32_t s_entry_factor[] = { 0x10000, 0x18000, 0x20000, 0x30000, 0x40000 };
#define PE_ENTRY_CHOICES 5

static int s_always_run = 1;
static int s_run_choice = 3, s_walk_choice = 3;   /* 1.75x field movement by default */
static int s_combat_choice = 0, s_entry_choice = 0;
static int s_repeat_interact;
static unsigned s_interact_ticks;

static int32_t pe_speed(int choice) { return s_speed_factor[choice >= 0 && choice < PE_SPEED_CHOICES ? choice : 0]; }

static int pe_on_player_move(void *ctx, void *user) {
    PeHookPlayerMove *step = ctx;
    (void)user;
    if (!pe_game_loaded() || step->actor != pe_player() || *step->factor != PE_MOVE_FACTOR_STOCK) return 0;
    if (step->combat) {
        if (pe_in_battle() && g_BattleModeState == 0) *step->factor = pe_speed(s_combat_choice);
    } else if (pe_free_field()) {
        *step->factor = pe_speed(step->running ? s_run_choice : s_walk_choice);
    }
    return 0;
}

/* The raw pad as the game's internal button bits (Circle/Cross swapped) and the left stick. */
typedef struct { uint32_t bits; int analog, x, y, stick; } PePad;

static PePad pe_read_pad(void) {
    PePad p = { 0, 0, PE_STICK_CENTER, PE_STICK_CENTER, 0 };
    const unsigned header = g_PadPackets[0].header, id = (header >> 8) & 0xFFu;
    uint32_t inv;
    if ((header & 0xFFu) != 0 || id == 0 || id == 0xFFu) return p;
    inv = ~(uint32_t)g_PadPackets[0].buttons & 0xFFFFu;
    p.bits = inv & 0x9FFFu;
    if (inv & 0x2000u) p.bits |= 0x4000u;
    if (inv & 0x4000u) p.bits |= 0x2000u;
    if ((header & 0xF000u) == 0x7000u) {
        p.analog = 1;
        p.x = g_PadPackets[0].leftX;
        p.y = g_PadPackets[0].leftY;
        p.stick = p.x < 0x5A || p.x >= 0xA1 || p.y < 0x5A || p.y >= 0xA1;
    }
    return p;
}

/* The game's analog field run test (Field_HandleStateTransition): the stick's distance from the
 * centre against the "Analog run threshold" option, a percentage of full tilt. */
static int pe_stick_radius_runs(int dx, int dy) {
    const unsigned threshold = g_FieldAnalogRunThreshold;
    if (dx < -127) dx = -127;
    if (dy < -127) dy = -127;
    return (unsigned)(dx * dx + dy * dy) * 10000u >= 127u * 127u * threshold * threshold;
}

/* The game reads a direction only outside the stick dead zone (0x5A..0xA0 on both axes). */
static int pe_stick_moves(int x, int y) { return x < 0x5A || x >= 0xA1 || y < 0x5A || y >= 0xA1; }

static int pe_on_pad_read(void *ctx, void *user) {
    PePad p;
    int dx, dy, run, big, reach, x, y;
    (void)ctx;
    (void)user;
    if (!s_always_run || !pe_game_loaded() || !pe_free_field()) return 0;
    p = pe_read_pad();
    if (!p.analog || !p.stick) return 0;
    dx = p.x - PE_STICK_CENTER;
    dy = p.y - PE_STICK_CENTER;
    run = !(p.bits & (uint32_t)g_FieldRenderFlagTable[0]);
    if (run == pe_stick_radius_runs(dx, dy)) return 0;      /* already what we want */
    if (run) {
        /* Out until the larger axis reaches its edge: at least full tilt for any threshold. */
        big = dx < 0 ? -dx : dx;
        if ((dy < 0 ? -dy : dy) > big) big = dy < 0 ? -dy : dy;
        if (big <= 0) return 0;
        x = PE_STICK_CENTER + (dx * 127 + (dx < 0 ? -big / 2 : big / 2)) / big;
        y = PE_STICK_CENTER + (dy * 127 + (dy < 0 ? -big / 2 : big / 2)) / big;
    } else {
        /* In to just inside the run threshold, diagonals included. A threshold too low to walk
         * outside the dead zone leaves the stick alone: Aya runs rather than stops. */
        const int length = (int)pe_isqrt((unsigned)(dx * dx + dy * dy)) + 1;
        for (reach = 127 * (int)g_FieldAnalogRunThreshold / 100;; --reach) {
            if (reach <= 0) return 0;
            x = PE_STICK_CENTER + dx * reach / length;
            y = PE_STICK_CENTER + dy * reach / length;
            if (!pe_stick_moves(x, y)) return 0;
            if (!pe_stick_radius_runs(x - PE_STICK_CENTER, y - PE_STICK_CENTER)) break;
        }
    }
    g_PadPackets[0].leftX = (u8)(x < 0 ? 0 : x > 0xFF ? 0xFF : x);
    g_PadPackets[0].leftY = (u8)(y < 0 ? 0 : y > 0xFF ? 0xFF : y);
    return 0;
}

/* With the d-pad, the game drops the run bit while the action button is held (originally so
 * that Cross wins over a held Circle). Always run maps run to the d-pad itself, so that rule
 * would make Cross a walk button. After the poll, restore the run bit for that case only:
 * a d-pad direction, the run button not held and the bit dropped by the action rule. */
static int pe_on_run_frame(void *ctx, void *user) {
    PePad p;
    (void)ctx;
    (void)user;
    if (!s_always_run || !pe_game_loaded() || !pe_free_field()) return 0;
    if ((g_FieldPadBits & PE_FIELD_RUN) || !(g_FieldPadBits & PE_FIELD_DIRS) || !(g_FieldPadBits & PE_FIELD_ACTION))
        return 0;
    p = pe_read_pad();
    if (p.analog || !(p.bits & PE_RUN_MAP_DPAD) || (p.bits & (uint32_t)g_FieldRenderFlagTable[0])) return 0;
    g_FieldPadBits |= PE_FIELD_RUN;
    if (g_FieldPadLastFrame & PE_FIELD_RUN) g_FieldPadReleased &= ~PE_FIELD_RUN;
    else g_FieldPadPressed |= PE_FIELD_RUN;
    return 0;
}

/* Read the game's current mapping on each poll. No remembered mask can leak
 * across load, remapping, a cutscene or disabling the mod. */
static int pe_on_pad_map(void *ctx, void *user) {
    PeHookPadMap *pad = ctx;
    (void)user;
    if (pe_game_loaded() && pe_free_field() && s_always_run)
        *pad->run_mask = pad->buttons & (unsigned)*pad->run_mask ? 0 : PE_RUN_MAP_DPAD;
    return 0;
}

static int pe_on_actor_step(void *ctx, void *user) {
    PeHookActorStep *step = ctx;
    BattleEntity *player = pe_player();
    (void)user;
    if (!pe_game_loaded() || !player || step->actor != player ||
        !pe_in_battle() || g_BattleModeState != 7 || player->actionMode != PE_ENTRY_ACTION_MODE)
        return 0;
    /* Do not amplify a turn toward the entry destination. Use this frame's
     * original heading, not cached state from a previous actor or encounter. */
    unsigned turn = ((unsigned)player->facingAngle - (unsigned)((const FieldActor *)player)->saved_rot_y) & 0xFFF;
    if (turn > 46 && turn < 0x1000 - 46) return 0;
    int factor = s_entry_factor[s_entry_choice >= 0 && s_entry_choice < PE_ENTRY_CHOICES ? s_entry_choice : 0];
    if (step->anim) {
        if (*step->anim == PE_ANIM_STEP_STOCK && (player->motionX || player->motionZ))
            *step->anim = factor;
    } else {
        int64_t value = (int64_t)*step->speed * factor / 0x10000;
        *step->speed = value > INT32_MAX ? INT32_MAX : value < INT32_MIN ? INT32_MIN : (int32_t)value;
    }
    return 0;
}

/* Original contact scripts test the cooked action edge (field bit8).
 * Repeating only that edge preserves held buttons, movement and remapping. */
static int pe_on_interact_frame(void *ctx, void *user) {
    (void)ctx; (void)user;
    const unsigned action = 0x100u;
    if (!s_repeat_interact || !pe_game_loaded() || !pe_free_field() ||
        pe_in_battle() || (g_FieldMoveLock & 1) || !(g_FieldPadBits & action)) {
        s_interact_ticks = 0;
        return 0;
    }
    if (g_FieldPadPressed & action) s_interact_ticks = 0;
    else if (++s_interact_ticks == 8) {
        g_FieldPadPressed |= action;
        s_interact_ticks = 0;
    }
    return 0;
}

void pe_move_register(const char *mod_id) {
    pe_menu_register_toggle(PE_PAGE_MOVEMENT, "Always run", "Hold the run button to walk (d-pad and stick).", &s_always_run, NULL);
    pe_menu_register_toggle(PE_PAGE_MOVEMENT, "Repeat interact", "Keep trying interactions while holding the action button.", &s_repeat_interact, NULL);
    pe_menu_register_choice(PE_PAGE_MOVEMENT, "Run speed", "How fast Aya runs in the field.", &s_run_choice, s_speed_names, PE_SPEED_CHOICES);
    pe_menu_register_choice(PE_PAGE_MOVEMENT, "Walk speed", "How fast Aya walks in the field.", &s_walk_choice, s_speed_names, PE_SPEED_CHOICES);
    pe_menu_register_choice(PE_PAGE_MOVEMENT, "Combat speed", "How fast Aya moves during battle.", &s_combat_choice, s_speed_names, PE_SPEED_CHOICES);
    pe_menu_register_choice(PE_PAGE_MOVEMENT, "Battle start speed", "Aya's walk to her spot as a fight opens.", &s_entry_choice,
                            s_entry_names, PE_ENTRY_CHOICES);
    pe_mod_log(mod_id, "movement: always_run=%d run=%s walk=%s combat=%s battle_start=%s",
        s_always_run, s_speed_names[s_run_choice], s_speed_names[s_walk_choice],
        s_speed_names[s_combat_choice], s_entry_names[s_entry_choice]);

    pe_hook_add(PE_HOOK_FRAME, pe_on_run_frame, NULL, PE_PRIORITY_NORMAL, mod_id);
    pe_hook_add(PE_HOOK_FRAME, pe_on_interact_frame, NULL, PE_PRIORITY_NORMAL, mod_id);
    pe_hook_add(PE_HOOK_PAD_READ, pe_on_pad_read, NULL, PE_PRIORITY_NORMAL, mod_id);
    pe_hook_add(PE_HOOK_PAD_MAP, pe_on_pad_map, NULL, PE_PRIORITY_NORMAL, mod_id);
    pe_hook_add(PE_HOOK_ACTOR_STEP, pe_on_actor_step, NULL, PE_PRIORITY_NORMAL, mod_id);
    pe_hook_add(PE_HOOK_PLAYER_MOVE, pe_on_player_move, NULL, PE_PRIORITY_NORMAL, mod_id);
}

#if PE_NATIVE_STATE_CPU_COMPONENTS
#include "pe_checkpoint_wire.h"
#include "pe_checkpoint_qol.h"
/* Preserve the 20-byte owner layout. Legacy first words 0/1 mean repeat off;
 * added bits own the repeat setting and phase. Older readers reject them. */
int pe_move_checkpoint_walk(const unsigned char *in,unsigned char *out,int apply) {
    if ((!in && !out) || (apply && (!in || !pe_move_checkpoint_walk(in,0,0)))) return 0;
    if ((unsigned)s_always_run>1 || (unsigned)s_repeat_interact>1 || s_interact_ticks>7) return 0;
    PeCheckpointWire w={in,out,0,apply,1};
    uint32_t flags=(unsigned)s_always_run | ((unsigned)s_repeat_interact<<1);
    uint32_t packed=pe_checkpoint_u32(&w,flags | (s_interact_ticks<<2),0,31,0);
    if ((packed & 3u)!=flags) w.ok=0; /* settings must match, as before */
    if (apply && w.ok) s_interact_ticks=packed>>2;
    PE_CK_U(w,s_run_choice,0,7,1);PE_CK_U(w,s_walk_choice,0,7,1);
    PE_CK_U(w,s_combat_choice,0,7,1);PE_CK_U(w,s_entry_choice,0,4,1);
    return w.ok && w.at==20u;
}

#endif
