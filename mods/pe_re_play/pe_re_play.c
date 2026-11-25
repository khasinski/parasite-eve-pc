/* Play imported games: the room runtime (design doc "RE rooms in Parasite Eve", phase 4, the
 * playable slice). Only games imported through F1 > Import are offered; with none, the mod does
 * nothing. It reads only the import store, never a disc.
 *
 *   pe_re_session.c  the session: room, camera, position; camera switch zones and doors (no PE code)
 *   pe_re_stage.c    the room on the field screen: picture, masks, camera, walking with collision
 *   pe_re_box.c      messages in the game's dialogue band
 *   pe_re_door.c     going through a door: fade, the door's sounds, the next room
 *   pe_re_save.c     the progress, kept with the Parasite Eve save
 *   pe_re_play.c     the mod: F1 > Play imported games, and each frame's step
 *
 * The room is played inside whatever PE room Aya stands in (a shell room): she walks the imported
 * room's floor, cameras switch where its zones say, and Cross in a door's zone takes her through
 * to the next room and camera (pe_re_rules.c: locked doors need their key, or say why; Cross picks
 * up items into the session's pocket and reads message zones, shown in the game's dialogue band (pe_re_box.c); RE keys
 * can't go into PE's key pocket, which holds PE items only). The room's own scripts run on entry
 * (pe_re_scd.c, RE2): only the doors, items and messages they set up are there, and they set the
 * game's flags. Not yet: waits and loops in scripts (events), door animations, enemies
 * (reported to the encounter host later), saving. RE2 only for now: RE1 and RE3 rooms need their
 * cameras and collision drawn. The PE room underneath is only a shell: it never hears Cross, and when
 * Parasite Eve loads a room of its own (an event, a warp) the imported room comes back over it once
 * the field is free. */
#include <stdio.h>
#include <stddef.h>
#include <math.h>
#include <string.h>

#include "pe_mod_api.h"
#include "pe_re_play_runtime.h"
#include "pe_re_door.h"
#include "pe_re_music.h"
#include "pe_re_save.h"
#include "pe_re_battle.h"
#include "pe_re_session.h"
#include "re_host.h"
#include "re_encounter_rooms.h"

#define MOD_ID "pe_re_play"

static char s_store[1100];
static const PeReGame *s_offer[PE_RE_GAMES];
static int s_offers, s_game_pick, s_player, s_listed, s_masks = 1, s_size = 100, s_jump_stage = 1, s_jump_room;
static PeReSession s_session;
/* The progress, in the save's companion (pe_re_save.h). Enabled before the first change; kept
 * encoded while playing, and on Stop (so Continue picks it up). Loading a save that was in a game
 * resumes there once the field is free. */
static uint8_t s_saved[PE_RE_SAVE_SIZE];
/* Room checkpoints, for testers: the progress as it was when she came into the room, kept beside the
 * import store (one file, overwritten each room), for the title and pause menus' "last room". */
static void checkpoint_path(char *out, size_t n) { snprintf(out, n, "%s/../re_play_checkpoint.bin", s_store[0] ? s_store : "."); }
static const ReHostProgress s_progress = {"pe_re_play/progress", PE_RE_SAVE_VERSION, 1,
                                          sizeof s_saved, s_saved, pe_re_save_upgrade};
static int s_registered, s_enabled, s_resume;
static int s_playing, s_cross_held, s_square_held, s_reshown, s_start_pending;
static int s_start_held, s_select_held;          /* Start: the status screen; Select: the pause menu */
static const unsigned *s_opening_movies;
static int s_climbing;
/* The buttons that closed a box (or were down while it was up): they reach the field only once let go,
 * so the Cross that picks something in a box doesn't also examine the spot she's on (a typewriter or
 * a socket would open its box again). */
static uint16_t s_box_buttons;
static int s_box_was_up;
enum { TITLE_CLOSED, TITLE_WAIT_FIELD, TITLE_CHOOSING, TITLE_STARTING };
static int s_title, s_title_choice, s_title_wait;
static char s_status[200] = "Not playing.";

static void list_games(void) {
    if (s_listed) return;
    s_listed = 1, s_offers = 0;
    for (int i = 0; i < PE_RE_GAMES; i++)
        if (pe_re_imported(s_store, pe_re_game(i))) s_offer[s_offers++] = pe_re_game(i);
}

static unsigned game_count(void) { list_games(); return s_offers ? (unsigned)s_offers : 1; }
static const char *game_name(unsigned i) {
    static char name[80];
    if (i >= (unsigned)s_offers) return "Nothing imported: scan a disc in F1 > Import";
    snprintf(name, sizeof name, "%s%s", s_offer[i]->title, s_offer[i]->playable ? "" : " (not playable yet)");
    return name;
}

/* In the game's dialogue band; a toast if no box can be had (the font not loaded yet). */
static void say(const char *text) {
    if (!re_host()->say(text)) pe_toast(text);
}

/* ---- enemies: reported, not fought here ("re_play", re_play_api.h) ----
 * Each room's placements go out as "re_play.room" when she comes in and "re_play.enemies" when
 * the placement list changes; an encounter mod decides what to do with it. */
static PeReEnemyPlacement s_enemies[PE_RE_ENEMIES_MAX];
static int s_nenemies, s_room_reported;
static int s_enemy_debug_slot = -1;
static int s_battle_paused;
static int s_movie_playing, s_movie_cross;
enum { ENDING_NONE, ENDING_MOVIE, ENDING_RESULTS };
static int s_ending;
static int s_placement_markers;
static int s_placement_meshes;
static int s_placement_idle = 1;
static int s_room_event = -1, s_enemies_event = -1;

static int api_room(PeReRoomEvent *out) {
    if (!out || !s_playing || !s_room_reported || s_battle_paused || !re_host()->room_active() || pe_re_door_busy()) return -1;
    memset(out, 0, sizeof *out);
    out->struct_size = sizeof *out;
    out->game = s_session.game->game, out->stage = s_session.stage, out->room = s_session.room;
    out->count = s_nenemies, out->enemies = s_enemies;
    return 0;
}
static int api_defeated(uint8_t killed_flag) {
    if (!s_playing || !s_room_reported || s_battle_paused || !re_host()->room_active() || pe_re_door_busy()) return -1;
    for (int i = 0; i < s_nenemies; i++) {
        if (s_enemies[i].killed_flag == killed_flag)
            return pe_re_session_defeated(&s_session, killed_flag);
    }
    return -1;
}
static int api_defeated_slot(uint8_t slot) {
    if (!s_playing || !s_room_reported || s_battle_paused || !re_host()->room_active() || pe_re_door_busy()) return -1;
    for (int i = 0; i < s_nenemies; i++)
        if (s_enemies[i].slot == slot) return pe_re_session_beaten(&s_session, slot, s_enemies[i].killed_flag);
    return -1;
}
static int pause_for_battle(void);
static void resume_after_battle(void);
static int encounter_position(double *x, double *z, int *floor);
static int encounter_reachable(double x, double z, double target_x, double target_z);
static void advance_enemies(double x, double z, int floor);

static int scene_locked(void) { return s_ending || s_movie_playing || s_session.run.movie_request || pe_re_session_controls_locked(&s_session); }

/* Aya's pose while a scene has her: a scripted walk's mode, else a scripted clip from her sub bank 0,
 * which holds the walk (move 0), run (1) and idle (2) as Spl_sce04-06 play them (SPL_SCE.C); other
 * clips (the room's, her own) can't be shown on Aya's model, so she stands. */
static int scene_move_mode(void) {
    const PeReScdRun *r = &s_session.run;
    if (r->actor_stopped[0]) return 0;
    if (r->player_dest.moving) return r->player_dest.mode == PE_RE_DEST_TURN ? 0 : r->player_dest.mode;
    const PeReSceneMotion *m = &r->pl_motion[0];
    if (m->active && m->bank == PE_RE_CLIP_SUB0) return m->move == 0 ? 4 : m->move == 1 ? 5 : 0;
    return 0;
}
static void sync_scene_controls(void) {
    const ReHost *host = re_host();
    if (host->struct_size >= offsetof(ReHost, scene_controls) + sizeof host->scene_controls && host->scene_controls)
        host->scene_controls(scene_locked(), scene_move_mode());
}
static int encounter_boss(void) { return s_playing ? pe_re_session_ready_boss(&s_session) : -1; }
static int prepare_boss(uint8_t slot);
static int api_canister_defeat(uint8_t slot) {
    if (!s_playing || !s_room_reported || s_battle_paused || !re_host()->room_active() || pe_re_door_busy()) return -1;
    for (int i = 0; i < s_nenemies; i++) {
        if (s_enemies[i].slot == slot)
            return pe_re_session_canister_win(&s_session, slot, s_enemies[i].killed_flag);
    }
    return -1;
}
static const char *api_enemy_archive(void) {
    if (!s_playing || !s_session.game || strcmp(s_session.game->game, "re2")) return NULL;
    /* bio2-ds EM_SET.C, Emd_load_file: Leon (including HUNK) and Claire use
     * different enemy archives. Sce_em_set's Model_type controls animation handling. */
    return s_session.player == 1 ? "cdemd1" : "cdemd0";
}
static int api_enemy_room_move(unsigned slot, char *asset, size_t size) {
    return s_playing ? pe_re_session_enemy_room_move(&s_session, slot, asset, size) : -1;
}
static int api_battle_result(const PeReBattleResult *result)
{
    if (!result || !result->game || !result->defeated_slots) return -1;
    PeReRoomEvent room;
    if (api_room(&room)) return 0;
    if (strcmp(result->game, room.game) || result->stage != room.stage || result->room != room.room) return -1;
    int applied = 0;
    for (unsigned slot = 0; slot < PE_RE_ENEMIES_MAX; ++slot) {
        const unsigned bit = 1u << slot;
        if (!(result->defeated_slots & bit)) continue;
        if ((result->canister_slots & bit) && !api_canister_defeat((uint8_t)slot)) {
            ++applied;
            continue;
        }
        applied += !api_defeated_slot((uint8_t)slot);
    }
    return applied ? applied : -1;
}

static double api_units_per_pe_unit(void) {
    const ReHost *host = re_host();
    return s_playing && host->player_scale ? host->player_scale() : 0;
}

static int api_in_view(const PeReEnemyPlacement *e) {
    const ReHost *host = re_host();
    if (!s_playing || host->struct_size < offsetof(ReHost, in_view) + sizeof host->in_view || !host->in_view) return -1;
    return host->in_view(e->x, e->y, e->z);
}

static int api_room_extent(double *x0, double *z0, double *x1, double *z1) {
    const ReHost *host = re_host();
    const PeRe2RoomCollision *c = s_playing && host->room_collision ? host->room_collision() : NULL;
    if (!c || !c->count) return -1;
    *x0 = *z0 = 1e9, *x1 = *z1 = -1e9;
    for (unsigned i = 0; i < c->count; i++) {
        const PeRe2Collider *e = &c->entries[i];
        const double a = e->x0 < e->x1 ? e->x0 : e->x1, b = e->x0 < e->x1 ? e->x1 : e->x0;
        const double p = e->z0 < e->z1 ? e->z0 : e->z1, q = e->z0 < e->z1 ? e->z1 : e->z0;
        if (a < *x0) *x0 = a;
        if (b > *x1) *x1 = b;
        if (p < *z0) *z0 = p;
        if (q > *z1) *z1 = q;
    }
    return 0;
}

static int api_vram_reserved(int *out, int max) {
    const ReHost *host = re_host();
    if (!s_playing || host->struct_size < offsetof(ReHost, vram_reserved) + sizeof host->vram_reserved ||
        !host->vram_reserved) return 0;
    return host->vram_reserved(out, max);
}

/* The battle director (docs/RE_PLAY_BATTLES.md, layer 2): one battle at a time, with the room's
 * grace periods after it. The PE adapter drives it through the API below; RE play steps its grace
 * on field frames. */
static PeReBattle s_battle;
static int s_battle_ready;

static PeReBattle *battle(void) {
    if (!s_battle_ready) pe_re_battle_init(&s_battle), s_battle_ready = 1;
    return &s_battle;
}

static int battle_in_view(const PeReEnemyPlacement *e) {
    const ReHost *host = re_host();
    if (host->struct_size < offsetof(ReHost, in_view) + sizeof host->in_view || !host->in_view) return -1;
    return host->in_view(e->x, e->y, e->z);
}

static int api_battle_begin(uint8_t lead_slot, uint8_t *slots, int max) {
    PeReEnemyPlacement enemies[PE_RE_ENEMIES_MAX];
    if (!s_playing || !s_session.game) return -1;
    const PeReBattleRules *rules = pe_re_battle_rules(s_session.game->game);
    const int n = pe_re_session_enemies(&s_session, enemies, PE_RE_ENEMIES_MAX);
    for (int i = 0; i < n; i++)
        if (enemies[i].slot == lead_slot) {
            PeReBattle *b = battle();
            if (pe_re_battle_begin(b, &s_session, rules, &enemies[i], battle_in_view)) return -1;
            int count = 0;
            for (int k = 0; k < b->initial_count && count < max; k++) slots[count++] = b->initial_slots[k];
            return count;
        }
    return -1;
}

static int api_battle_step(double aya_x, double aya_z, int time_runs, PeReEnemyPlacement *joins, int max) {
    PeReBattleEvent ev[PE_RE_ENEMIES_MAX];
    PeReEnemyPlacement enemies[PE_RE_ENEMIES_MAX];
    if (!s_playing) return 0;
    const int n = pe_re_battle_step(battle(), &s_session, aya_x, aya_z, time_runs, battle_in_view, ev,
                                    max < PE_RE_ENEMIES_MAX ? max : PE_RE_ENEMIES_MAX);
    const int placed = n ? pe_re_session_enemies(&s_session, enemies, PE_RE_ENEMIES_MAX) : 0;
    int count = 0;
    for (int i = 0; i < n; i++) {
        if (ev[i].kind != PE_RE_BATTLE_EV_JOIN || count >= max) continue;
        for (int k = 0; k < placed; k++)
            if (enemies[k].slot == ev[i].slot) { joins[count++] = enemies[k]; break; }
    }
    return count;
}

static void api_battle_foe_moved(uint8_t slot, double x, double z) { pe_re_battle_foe_moved(battle(), slot, x, z); }
static void api_battle_foe_died(uint8_t slot) { pe_re_battle_foe_died(battle(), slot); }
static void api_battle_end(int escaped) { if (s_playing) pe_re_battle_end(battle(), &s_session, escaped); }
static int api_battle_target_order(uint8_t *slots, int max) { return pe_re_battle_target_order(battle(), slots, max); }
static int api_battle_encounter_allowed_for(uint8_t slot) {
    return s_playing ? pe_re_battle_encounter_allowed_for(battle(), &s_session, slot) : 1;
}
static void api_battle_settings(double seconds, double distance, int halve) {
    PeReBattle *b = battle();
    b->settings.seconds = seconds, b->settings.distance = distance, b->settings.halve_on_repeat = halve;
}
static int api_battle_point(int to_room, double x, double z, double *out_x, double *out_z) {
    const ReHost *host = re_host();
    if (host->struct_size < offsetof(ReHost, battle_point) + sizeof host->battle_point || !host->battle_point) return -1;
    return host->battle_point(to_room, x, z, out_x, out_z);
}

static void api_battle_mark(int id, int kind, double x, double z) {
    const ReHost *host = re_host();
    if (host->struct_size >= offsetof(ReHost, battle_mark) + sizeof host->battle_mark && host->battle_mark)
        host->battle_mark(id, kind, x, z);
}

static double api_enemy_walk_speed(uint8_t slot) {
    if (!s_playing) return 0;
    for (int i = 0; i < s_nenemies; i++)
        if (s_enemies[i].slot == slot) return pe_re_session_walk_speed(&s_session, &s_enemies[i]);
    return 0;
}

static unsigned s_games_started;
static unsigned api_games_started(void) { return s_games_started; }
static const char *api_playing_game(void) { return s_playing && s_session.game ? s_session.game->game : NULL; }

static const PeRePlayApi s_api = {
    sizeof s_api, api_room, api_defeated, pause_for_battle, resume_after_battle, api_defeated_slot, api_enemy_archive,
    api_enemy_room_move, encounter_position, encounter_reachable, encounter_boss, prepare_boss, api_canister_defeat,
    NULL, /* field movement is driven by the room, independently of encounter selection */
    api_battle_result, api_units_per_pe_unit, api_in_view, api_room_extent, api_vram_reserved,
    api_battle_begin, api_battle_step, api_battle_foe_moved, api_battle_foe_died, api_battle_end,
    api_battle_target_order, api_battle_encounter_allowed_for, api_battle_settings, api_battle_point,
    api_battle_mark, api_enemy_walk_speed, api_games_started, api_playing_game
};

static void report_enemies(void) {
    PeReEnemyPlacement placements[PE_RE_ENEMIES_MAX];
    const int fresh = !s_room_reported;
    const int n = pe_re_session_enemies(&s_session, placements, PE_RE_ENEMIES_MAX);
    const ReHost *host = re_host();
    if (host->struct_size >= offsetof(ReHost, set_mask_groups) + sizeof host->set_mask_groups &&
        host->set_mask_groups && (unsigned)s_session.camera < PE_RE_PRI_CAMERAS)
        host->set_mask_groups(s_session.progress.flags.bits[PE_RE_FG_PRI + s_session.camera]);
    if (host->struct_size >= offsetof(ReHost, set_scene_actors) + sizeof host->set_scene_actors &&
        host->set_scene_actors) {
        PeReSceneActor actors[16];
        const int count = pe_re_scd_scene_actors(&s_session.run, actors, 16);
        for (int i = 0; i < count; i++)
            if (actors[i].direct) pe_re_session_enemy_room_move(&s_session, actors[i].slot, actors[i].bank, sizeof actors[i].bank);
        host->set_scene_actors(actors, count);
    }
    if (host->struct_size >= offsetof(ReHost, set_effects) + sizeof host->set_effects && host->set_effects) {
        PeReEffect effects[PE_RE_EFFECT_MAX];
        const int count = pe_re_scd_effects(&s_session.run, effects);
        host->set_effects(s_session.logic, effects, count);
    }
    if (!fresh && n == s_nenemies && pe_re_enemy_lists_equal(s_enemies, placements, n)) return;
    memcpy(s_enemies, placements, (size_t)n * sizeof *placements);
    s_nenemies = n, s_room_reported = 1;
    host->set_placements(s_enemies, n);
    PeReRoomEvent e;
    if (api_room(&e)) return;
    e.fresh = fresh;
    pe_event_emit(fresh ? s_room_event : s_enemies_event, &e);
    if (n) pe_mod_log(MOD_ID, "%s %x-%02X: %d enemies placed (first: kind 0x%02X at %d,%d)", fresh ? "room" : "updated",
                      (unsigned)s_session.stage + 1, (unsigned)s_session.room, n, s_enemies[0].kind, s_enemies[0].x,
                      s_enemies[0].z);
}

static void text_enemies(char *out, unsigned size) {
    PeReRoomEvent room;
    if (api_room(&room)) { snprintf(out, size, "-"); return; }
    if (!s_nenemies) { snprintf(out, size, "None placed here."); return; }
    int at = snprintf(out, size, "%d placed (reported for fights):", s_nenemies);
    for (int i = 0; i < s_nenemies && at > 0 && (unsigned)at < size; i++)
        at += snprintf(out + at, size - (unsigned)at, " 0x%02X", s_enemies[i].kind);
}

static void text_enemy_movement(char *out, unsigned size) {
    PeReRoomEvent room;
    if (api_room(&room) || !room.count) { snprintf(out, size, "-"); return; }
    static const char *const states[] = {
        "waiting", "scripted", "family unsupported", "inactive", "different floor",
        "out of range", "within attack range", "blocked", "moving",
        "variant unsupported", "prone or rising", "corpse", "feeding"
    };
    int at = snprintf(out, size, "%d placed.", s_nenemies);
    for (int i = 0; i < s_nenemies && at > 0 && (unsigned)at < size; i++) {
        const PeReEnemyPlacement *enemy = &s_enemies[i];
        const unsigned slot = enemy->slot;
        if (s_enemy_debug_slot >= 0 && slot != (unsigned)s_enemy_debug_slot) continue;
        const unsigned state = slot < 16 ? s_session.enemy_state[slot] : PE_RE_ENEMY_UNCHECKED;
        const int routine = slot < 16 ? s_session.run.works[2 + slot][PE_RE_MEMBER_ROUTINE0] : -1;
        const int command = slot < 16 ? s_session.run.works[2 + slot][PE_RE_MEMBER_ROUTINE1] : -1;
        at += snprintf(out + at, size - (unsigned)at, " Slot %u EM%02X type %04X routine %d/%d at %d,%d: %s.",
                       slot, enemy->kind, enemy->behaviour, routine, command, enemy->x, enemy->z, states[state]);
        if (s_enemy_debug_slot >= 0 && slot < 16 && s_session.run.scene[slot].active &&
            at > 0 && (unsigned)at < size)
            at += snprintf(out + at, size - (unsigned)at, " Script clip %u, frame %u.",
                           s_session.run.scene[slot].move, s_session.run.scene[slot].elapsed);
        if (s_enemy_debug_slot >= 0 && slot < PE_RE_ENEMIES_MAX && at > 0 && (unsigned)at < size) {
            const PeReActorDestination *dest = &s_session.run.enemy_dest[slot];
            if (dest->moving) at += snprintf(out + at, size - (unsigned)at, " Script destination %d,%d.",
                                             dest->x, dest->z);
            if (s_session.run.actor_stopped[2 + slot] && at > 0 && (unsigned)at < size)
                snprintf(out + at, size - (unsigned)at, " Script paused.");
        }
    }
}

/* ---- the pocket and the item box ---- */
enum { PICK_NONE, PICK_USE, PICK_BOX, PICK_SAVE, PICK_COMPUTER, PICK_ALLIGATOR,
       PICK_STATUS, PICK_STATUS_ACT, PICK_COMBINE, PICK_PAUSE };
/* A typewriter save: the ribbon is used when she says yes; PE's own save screen opens on the next
 * free frame (as its save phones' script opcode opens it); backing out without a save gives the
 * ribbon back once the room is up again. */
enum { TYPE_NONE, TYPE_OPEN, TYPE_SCREEN };
static int s_typing, s_typed;
static int s_pick, s_pick_at;

static int prepare_boss(uint8_t slot) {
    const PeReBattleRules *rules = pe_re_battle_rules(s_session.game ? s_session.game->game : NULL);
    PeReBossFight fight;
    if (!rules || !rules->boss || !rules->boss(&s_session, &fight) || fight.lead.slot != slot)
        return PE_RE_BOSS_STANDARD;
    if (!fight.needs_choice) return fight.effect;
    static const char *const choices[] = {"Fight directly", "Release the gas canister"};
    if (!re_host()->choose("The Alligator is coming.", choices, 2, 0)) return PE_RE_BOSS_STANDARD;
    s_pick = PICK_ALLIGATOR;
    pe_mod_log("pe_re_play", "Alligator: approach choice opened");
    return PE_RE_BOSS_WAIT;
}

static int encounter_position(double *x, double *z, int *floor) {
    const ReHost *host = re_host();
    if (!s_playing || s_battle_paused || !host->room_active() || !host->field_free() ||
        pe_re_door_busy() || host->dialogue_busy() || host->native_menu_active() ||
        s_typing != TYPE_NONE || s_climbing || scene_locked() || s_pick != PICK_NONE) return -1;
    int facing;
    host->player_pose(x, z, &facing);
    *floor = host->player_floor();
    return 0;
}

static int encounter_reachable(double x, double z, double target_x, double target_z) {
    const ReHost *host = re_host();
    const double dx = target_x - x, dz = target_z - z;
    const int steps = (int)(fmax(fabs(dx), fabs(dz)) / 220) + 2;
    for (int i = 1; i < steps; i++)
        if (host->position_blocked(x + dx * i / steps, z + dz * i / steps)) return 0;
    return 1;
}

static void advance_enemies(double x, double z, int floor) {
    const ReHost *host = re_host();
    if (!s_room_reported || host->struct_size < offsetof(ReHost, room_collision) + sizeof host->room_collision ||
        !host->room_collision) return;
    /* Update the live snapshot for the renderer and encounter selector without treating
     * locomotion as an authored placement event. */
    if (pe_re_session_chase(&s_session, host->room_collision(), s_enemies, s_nenemies, x, z, floor))
        host->set_placements(s_enemies, s_nenemies);
}

static int pause_for_battle(void) {
    if (!s_playing) return 0;
    if (s_battle_paused || s_movie_playing || s_session.run.movie_request || !re_host()->room_active() || !re_host()->field_free() ||
        pe_re_door_busy() || re_host()->dialogue_busy() || s_typing != TYPE_NONE) return -1;
    re_host()->player_pose(&s_session.x, &s_session.z, &s_session.facing);
    s_session.floor = re_host()->player_floor();
    s_battle_paused = 1;
    pe_re_music_suspend();
    /* the battle plays in front of this room: with a backdrop the room stays up until the battle has
     * it (a battle started in place never leaves it); a PE room's load hides it anyway */
    if (!re_host()->arm_backdrop || !re_host()->release_backdrop || !re_host()->arm_backdrop()) re_host()->hide_room();
    snprintf(s_status, sizeof s_status, "Paused for battle.");
    return 1;
}

static void resume_after_battle(void) {
    if (!s_battle_paused) return;
    s_battle_paused = 0;
    if (re_host()->release_backdrop) re_host()->release_backdrop();
    snprintf(s_status, sizeof s_status, "Room %x-%02X, camera %d.", (unsigned)s_session.stage + 1,
             (unsigned)s_session.room, s_session.camera);
    s_cross_held = s_square_held = 1;
    s_session.key = s_session.key_trg = 0;
}
static int on_committed(void *ctx, void *user) {   /* a card save was written: a typewriter's counts */
    (void)ctx, (void)user;
    if (s_typing == TYPE_SCREEN) s_typed = 1;
    return 0;
}
static uint16_t s_use_here;                       /* testing: the item the last place to use one takes */
static uint16_t s_pick_item[RE_HOST_DIALOGUE_CHOICES];
static int s_pick_slot[RE_HOST_DIALOGUE_CHOICES];        /* pocket slot, or -1 - box slot */

static void held_label(const PeReHeld *h, const char *verb, char *out, size_t n) {
    char name[48];
    pe_re_item_name(s_session.items, h->item, name, sizeof name);
    if (h->count > 1) snprintf(out, n, "%s%s x%u", verb, name, (unsigned)h->count);
    else snprintf(out, n, "%s%s", verb, name);
}

static void open_pocket(void) {
    static char text[RE_HOST_DIALOGUE_CHOICES][41];
    const char *labels[RE_HOST_DIALOGUE_CHOICES];
    const PeReProgress *p = &s_session.progress;
    int n = 0;
    for (int i = 0; i < p->held && n < RE_HOST_DIALOGUE_CHOICES; i++, n++) {
        held_label(&p->pocket[i], "", text[n], sizeof text[n]);
        labels[n] = text[n], s_pick_item[n] = p->pocket[i].item, s_pick_slot[n] = i;
    }
    if (!n) { say("You aren't carrying anything."); return; }
    if (re_host()->choose("Use which item?", labels, n, 0)) s_pick = PICK_USE;
}

/* ---- RE2's status screen (Start), in boxes: the pocket, then what to do with an item ----
 * As RE2's own (bio2-ds GAME.C opens it on KEY_STATUS): Use, Combine (Mix) and Check. Equip is left
 * out: the fights are Parasite Eve's. */
static int s_status_slot;
static const char *who_plays(void) {
    if (s_session.player_id == 12) return "HUNK";
    return s_session.player ? "Claire" : "Leon";
}
static void open_status(int at) {
    static char text[RE_HOST_DIALOGUE_CHOICES][41], title[64];
    const char *labels[RE_HOST_DIALOGUE_CHOICES];
    const PeReProgress *p = &s_session.progress;
    int n = 0;
    for (int i = 0; i < p->held && n < RE_HOST_DIALOGUE_CHOICES; i++, n++) {
        held_label(&p->pocket[i], "", text[n], sizeof text[n]);
        labels[n] = text[n], s_pick_item[n] = p->pocket[i].item, s_pick_slot[n] = i;
    }
    snprintf(title, sizeof title, "Status: %s  %d/%d", who_plays(), p->held, PE_RE_POCKET_SLOTS);
    if (!n) { say("Status: nothing carried."); return; }
    if (re_host()->choose(title, labels, n, at >= 0 && at < n ? at : 0)) s_pick = PICK_STATUS;
}
static void open_status_actions(void) {
    static const char *const actions[] = {"Use", "Combine", "Check", "Back"};
    static char title[64];
    const PeReProgress *p = &s_session.progress;
    if (s_status_slot >= p->held) { open_status(0); return; }
    held_label(&p->pocket[s_status_slot], "", title, sizeof title);
    if (re_host()->choose(title, actions, 4, 0)) s_pick = PICK_STATUS_ACT;
}
static void open_combine(void) {
    static char text[RE_HOST_DIALOGUE_CHOICES][41];
    const char *labels[RE_HOST_DIALOGUE_CHOICES];
    const PeReProgress *p = &s_session.progress;
    int n = 0;
    for (int i = 0; i < p->held && n < RE_HOST_DIALOGUE_CHOICES; i++) {
        if (i == s_status_slot) continue;
        held_label(&p->pocket[i], "With ", text[n], sizeof text[n]);
        labels[n] = text[n], s_pick_slot[n] = i, n++;
    }
    if (!n) { say("There's nothing to combine it with."); return; }
    if (re_host()->choose("Combine", labels, n, 0)) s_pick = PICK_COMBINE;
}
static void check_item(void) {
    char label[64], name[48], text[200];
    const PeReProgress *p = &s_session.progress;
    held_label(&p->pocket[s_status_slot], "", label, sizeof label);
    int n = snprintf(text, sizeof text, "%s.", label);
    for (int i = 0; i < p->held && n < (int)sizeof text - 48; i++) {   /* what it goes with, as a hint */
        if (i == s_status_slot || !pe_re_combined(&s_session, p->pocket[s_status_slot].item, p->pocket[i].item)) continue;
        pe_re_item_name(s_session.items, p->pocket[i].item, name, sizeof name);
        n += snprintf(text + n, sizeof text - (size_t)n, " It goes with the %s.", name);
    }
    say(text);
}

/* ---- the pause menu (Select), as RE2's Select opens its own menu (GAME.C, SYS_CONFIG) ---- */
static void open_title(void);
static void stop(void);
static void update_crash_note(int force);
enum { PAUSE_RESUME, PAUSE_UNSTICK, PAUSE_LAST_ROOM, PAUSE_TITLE, PAUSE_QUIT, PAUSE_REPORT };
static void open_pause(void) {
    static const char *const options[] = {"Resume", "Unstick scene", "Back to last room", "RE2 title menu",
                                          "Back to Parasite Eve", "Write a bug report"};
    if (re_host()->choose("Paused", options, 6, PAUSE_RESUME)) s_pick = PICK_PAUSE;
}
static void bug_report(void);
/* The report waits until the boxes are gone, so its screenshot shows the game, not the menu. */
static int s_report_pending;
static void request_bug_report(void) { if (s_playing) s_report_pending = 4; else bug_report(); }
/* Unstick scene: end a scene that never lets go, and say where it was for the bug report. */
static void unstick_scene(void) {
    char report[600];
    const int ended = pe_re_session_unstick(&s_session, report, sizeof report);
    pe_mod_log("pe_re_play", "unstick: %s", report);
    snprintf(s_status, sizeof s_status, ended ? "Scene ended (%d tasks); see pe_mods.log for where it stuck." :
             "No scene was running; controls released.", ended);
    pe_toast(s_status);
}
static void resume(void);
static int load_checkpoint(void);

static void open_item_box(int at) {
    static char text[RE_HOST_DIALOGUE_CHOICES][41];
    const char *labels[RE_HOST_DIALOGUE_CHOICES];
    const PeReProgress *p = &s_session.progress;
    int n = 0;
    for (int i = 0; i < p->held && n < RE_HOST_DIALOGUE_CHOICES; i++, n++) {
        held_label(&p->pocket[i], "Put in: ", text[n], sizeof text[n]);
        labels[n] = text[n], s_pick_slot[n] = i;
    }
    for (int i = 0; i < p->boxed && n < RE_HOST_DIALOGUE_CHOICES; i++, n++) {
        held_label(&p->box[i], "Take out: ", text[n], sizeof text[n]);
        labels[n] = text[n], s_pick_slot[n] = -1 - i;
    }
    if (!n) { say("The item box is empty, and so are your pockets."); s_pick = PICK_NONE; return; }
    s_pick = re_host()->choose("Item box", labels, n, at < n ? at : n - 1) ? PICK_BOX : PICK_NONE;
}

/* a zone the session hands over: the pocket, an item box, a typewriter */
static void zone_handed(void) {
    switch (s_session.zone.sce) {
    case PE_RE_ZONE_STATUS: open_pocket(); break;
    case PE_RE_ZONE_WINDOWS: {                       /* a computer: says, or asks to use a card */
        static const char *const yes_no[2] = {"Yes", "No"};
        if (pe_re_session_computer(&s_session, 0) != PE_RE_COMPUTER_ASK) say(s_session.said);
        else if (re_host()->choose("Use the Blue Card?", yes_no, 2, 0)) s_pick = PICK_COMPUTER;
        break;
    }
    case PE_RE_ZONE_ITEMBOX: open_item_box(0); break;
    case PE_RE_ZONE_SAVE: {
        static const char *const yes_no[2] = {"Yes", "No"};
        if (!s_registered) say("Saving imported progress is unavailable in this host.");
        else if (!pe_re_has(&s_session.progress, PE_RE2_INK_RIBBON))
            say("There's a typewriter. With an Ink Ribbon you could record your progress.");
        else if (re_host()->choose("Use an Ink Ribbon to save?", yes_no, 2, 0))
            s_pick = PICK_SAVE;
        break;
    }
    default: break;
    }
    pe_mod_log("pe_re_play", "zone %u kind %u", (unsigned)s_session.zone.id, (unsigned)s_session.zone.sce);
}

/* a choice made in the band */
static int keep_progress(void);

static void picked(void) {
    const int c = re_host()->dialogue_choice();
    if (c == -1 || s_pick == PICK_NONE) return;
    const int mode = s_pick;
    s_pick = PICK_NONE;
    if (mode == PICK_ALLIGATOR) {
        s_session.run.alligator_chosen = 1;
        if (c == 1) pe_re_session_release_canister(&s_session);
        return;
    }
    if (mode == PICK_STATUS) {
        if (c >= 0) s_status_slot = s_pick_slot[c], open_status_actions();
        return;
    }
    if (mode == PICK_STATUS_ACT) {
        if (c == -2 || c == 3) { open_status(s_status_slot); return; }   /* Back: the pocket again */
        if (c == 0) {
            const uint16_t item = s_session.progress.pocket[s_status_slot].item;
            char name[48];
            pe_re_item_name(s_session.items, item, name, sizeof name);
            if (pe_re_session_use(&s_session, item) == PE_RE_SAID) say(s_session.said);
            else snprintf(s_status, sizeof s_status, "Used the %s.", name);
        } else if (c == 1) {
            open_combine();
        } else if (c == 2) {
            check_item();
        }
        return;
    }
    if (mode == PICK_COMBINE) {
        if (c < 0) { open_status_actions(); return; }
        const PeReProgress *p = &s_session.progress;
        const uint16_t a = p->pocket[s_status_slot].item, b = p->pocket[s_pick_slot[c]].item;
        const uint16_t made = pe_re_session_combine(&s_session, a, b);
        char name[48], text[96];
        if (!made) { say("Those can't be combined."); return; }
        pe_re_item_name(s_session.items, made, name, sizeof name);
        snprintf(text, sizeof text, "Combined: the %s.", name);
        say(text);
        return;
    }
    if (mode == PICK_PAUSE) {
        if (c == PAUSE_UNSTICK) unstick_scene();
        else if (c == PAUSE_REPORT) request_bug_report();
        else if (c == PAUSE_LAST_ROOM) {
            if (load_checkpoint()) say("There's no checkpoint for the last room yet.");
            else resume();
        } else if (c == PAUSE_TITLE) {                    /* out of the room, then the menu once PE's field is free */
            stop();
            s_resume = 0;
            s_title = TITLE_WAIT_FIELD;
            snprintf(s_status, sizeof s_status, "Opening the RE2 title menu...");
        }
        else if (c == PAUSE_QUIT) { stop(); snprintf(s_status, sizeof s_status, "Back in Parasite Eve. Continue picks the RE game up again."); }
        return;
    }
    if (c < 0) return;                                      /* backed out */
    if (mode == PICK_COMPUTER) {
        if (c == 0 && pe_re_session_computer(&s_session, 1) == PE_RE_COMPUTER_DONE) say(s_session.said);
        return;
    }
    if (mode == PICK_SAVE) {
        if (c == 0) {
            if (!keep_progress()) say("Imported progress cannot be saved in this slot.");
            else if (!pe_re_lose(&s_session.progress, PE_RE2_INK_RIBBON, 1)) s_typing = TYPE_OPEN, s_typed = 0;
        }
        return;
    }
    if (mode == PICK_USE) {
        char name[48];
        pe_re_item_name(s_session.items, s_pick_item[c], name, sizeof name);
        if (pe_re_session_use(&s_session, s_pick_item[c]) == PE_RE_SAID) say(s_session.said);
        else snprintf(s_status, sizeof s_status, "Used the %s.", name);
        return;
    }
    const int slot = s_pick_slot[c];                        /* the box: put in or take out, and stay */
    if (slot >= 0 ? pe_re_box_put(&s_session.progress, slot) : pe_re_box_take(&s_session.progress, -1 - slot)) {
        say(slot >= 0 ? "The item box is full." : "You can't carry any more.");
        return;
    }
    s_pick_at = c;
    open_item_box(s_pick_at);
}

/* The scripts' wall switches (sca_id_set) to the walker: after a tick, and again once a room's shown */
static void apply_collision(int all) {
    if (!re_host()->set_collision_id || (!all && !s_session.run.sca_changed)) return;
    for (unsigned k = 0; k < 256; k++)
        if (s_session.run.sca_id[k]) re_host()->set_collision_id(k, s_session.run.sca_id[k] & 0xFF80u);
    s_session.run.sca_changed = 0;
}

static void show_room(int place) {
    char room[PE_IMP_ID_MAX], why[200] = "";
    pe_re_room_id(s_session.game->game, s_session.stage, s_session.room, room, sizeof room);
    if (re_host()->show_room(room, s_session.logic, s_session.camera, s_session.floor, place, s_session.x, s_session.z,
                         s_session.facing, why, sizeof why)) {
        snprintf(s_status, sizeof s_status, "Stopped: %s.", why);
        pe_re_door_cancel();
        re_host()->hide_room();
        pe_re_session_end(&s_session);
        s_playing = 0;
        pe_re_music_stop();
        return;
    }
    re_host()->player_pose(&s_session.x, &s_session.z, &s_session.facing);
    apply_collision(1);
    sync_scene_controls();
    pe_re_music_update(&s_session);
    snprintf(s_status, sizeof s_status, "Room %x-%02X, camera %d.", (unsigned)s_session.stage + 1,
             (unsigned)s_session.room, s_session.camera);
}

/* A script's cut to a camera: one the room's pictures don't have is let go (logged), not a stop. */
static void show_cut(void) {
    char room[PE_IMP_ID_MAX], why[200] = "";
    pe_re_room_id(s_session.game->game, s_session.stage, s_session.room, room, sizeof room);
    if (re_host()->show_room(room, s_session.logic, s_session.camera, s_session.floor, 1, s_session.x, s_session.z,
                         s_session.facing, why, sizeof why)) {
        pe_mod_log(MOD_ID, "cut to camera %d: %s", s_session.camera, why);
        s_session.camera = s_session.run.camera = s_session.run.camera_old;
        s_session.run.camera_lock = 0;
        show_room(1);
        return;
    }
    re_host()->player_pose(&s_session.x, &s_session.z, &s_session.facing);
    apply_collision(1);
}

static int keep_progress(void) {
    if (!s_enabled && s_registered) s_enabled = re_host()->enable_progress() == 0;
    return s_enabled;
}

static void stop_movie(void) {
    if (!s_movie_playing) return;
    const ReHost *host = re_host();
    host->movie_stop();
    if (RE_HOST_HAS(host, set_native_music_muted)) host->set_native_music_muted(0);
    s_movie_playing = 0;
    sync_scene_controls();
}

typedef struct {
    const char *asset, *claire_asset;
    unsigned start_sector, last_frame, claire_last_frame;
    int silent;
} ReMovie;

enum {
    RE2_MOVIE_OPENING_FIRST = 3,
    RE2_MOVIE_B_ENDING = 14,
    RE2_MOVIE_A_OPENING_SECOND = 15,
    RE2_MOVIE_B_OPENING_SECOND = 16,
    RE2_MOVIE_A_ENDING = 17
};

/* OPENING.C selects the second clip by scenario. FILECODE.H maps the clip IDs to each disc. */
static const unsigned s_scenario_a_opening[] = {RE2_MOVIE_OPENING_FIRST, RE2_MOVIE_A_OPENING_SECOND, 0};
static const unsigned s_scenario_b_opening[] = {RE2_MOVIE_OPENING_FIRST, RE2_MOVIE_B_OPENING_SECOND, 0};

/* Entries from Movie_tbl. The alternate excerpts seek to five
 * sectors per table offset unit. FILECODE.H maps character-specific names. */
static const ReMovie s_movies[] = {
    [1] = {.asset = "re2:movie/r200", .last_frame = 250},
    [2] = {.asset = "re2:movie/r200", .last_frame = 146},
    [RE2_MOVIE_OPENING_FIRST] = {.asset = "re2:movie/opn1stl", .claire_asset = "re2:movie/opn1stc"},
    [4] = {.asset = "re2:movie/r10b", .last_frame = 150},
    [5] = {.asset = "re2:movie/r200", .last_frame = 90},
    [6] = {.asset = "re2:movie/r505", .last_frame = 598},
    [7] = {.asset = "re2:movie/r505", .start_sector = 407 * 5, .last_frame = 598},
    [8] = {.asset = "re2:movie/r108l", .claire_asset = "re2:movie/r108c", .last_frame = 1326},
    [9] = {.asset = "re2:movie/r109", .last_frame = 676},
    [10] = {.asset = "re2:movie/r204l", .claire_asset = "re2:movie/r204c", .last_frame = 500},
    [11] = {.asset = "re2:movie/r409", .claire_asset = "re2:movie/r408", .last_frame = 4287},
    [12] = {.asset = "re2:movie/r700l", .claire_asset = "re2:movie/r700c", .last_frame = 2125, .claire_last_frame = 3043},
    [13] = {.asset = "re2:movie/r703l", .claire_asset = "re2:movie/r703c", .last_frame = 3115, .claire_last_frame = 2345},
    [RE2_MOVIE_B_ENDING] = {.asset = "re2:movie/r704l", .claire_asset = "re2:movie/r704c", .last_frame = 9187},
    [RE2_MOVIE_A_OPENING_SECOND] = {.asset = "re2:movie/opn2ndl", .claire_asset = "re2:movie/opn2ndc"},
    [RE2_MOVIE_B_OPENING_SECOND] = {.asset = "re2:movie/opn2ndrl", .claire_asset = "re2:movie/opn2ndrc"},
    [RE2_MOVIE_A_ENDING] = {.asset = "re2:movie/r704l", .claire_asset = "re2:movie/r704c",
                           .start_sector = 4589 * 5, .last_frame = 9247, .silent = 1},
};

static void movie_input(unsigned buttons) {
    re_host()->consume_native_interact();
    s_cross_held = s_movie_cross = (buttons & RE_HOST_CROSS) != 0;
    s_square_held = (buttons & RE_HOST_SQUARE) != 0;
    s_start_held = (buttons & RE_HOST_START) != 0;
    s_select_held = (buttons & RE_HOST_SELECT) != 0;
    s_session.key = pe_re_scd_keys(buttons);
    s_session.key_trg = 0;
    sync_scene_controls();
}

static int start_movie(unsigned id) {
    const ReHost *host = re_host();
    const ReMovie *movie = id < sizeof s_movies / sizeof *s_movies ? &s_movies[id] : NULL;
    const char *asset = movie ? movie->asset : NULL;
    unsigned last_frame = movie ? movie->last_frame : 0;
    if (movie && s_session.player == 1 && movie->claire_asset) {
        asset = movie->claire_asset;
        if (movie->claire_last_frame) last_frame = movie->claire_last_frame;
    }
    if (s_movie_playing || !asset ||
        !RE_HOST_HAS(host, movie_start) || !RE_HOST_HAS(host, movie_tick) || !RE_HOST_HAS(host, movie_stop) ||
        !host->movie_start(asset, movie->start_sector, last_frame, movie->silent)) {
        pe_mod_log(MOD_ID, "Movie %u unavailable for player %u", id, (unsigned)s_session.player);
        return 0;
    }
    pe_re_music_stop();
    if (RE_HOST_HAS(host, set_native_music_muted)) host->set_native_music_muted(1);
    pe_mod_log(MOD_ID, "Movie %u: playing %s", id, asset);
    host->forget_dialogue();
    s_movie_playing = 1;
    movie_input(host->held_buttons());
    return 1;
}

/* Consume the whole frame, including the press that skips the movie. The
 * caller continues its own sequence on the following frame. */
static int tick_movie(void) {
    if (!s_movie_playing) return 0;
    const ReHost *host = re_host();
    const unsigned buttons = host->held_buttons();
    const int skip = (buttons & RE_HOST_CROSS) && !s_movie_cross;
    movie_input(buttons);
    if (skip || !host->movie_tick()) stop_movie();
    return 1;
}

static int movie_frame(void) {
    if (tick_movie()) return 1;
    const unsigned request = s_session.run.movie_request;
    s_session.run.movie_request = 0;
    return request && start_movie(request - 1);
}

static void stop(void) {
    s_ending = ENDING_NONE;
    stop_movie();
    pe_re_music_stop();
    s_start_pending = 0;
    s_opening_movies = NULL;
    s_title = TITLE_CLOSED;
    s_climbing = 0;
    pe_re_door_cancel();
    re_host()->forget_dialogue();
    s_pick = PICK_NONE;
    s_typing = TYPE_NONE;
    if (s_session.game && (!s_registered || keep_progress())) pe_re_save_encode(&s_session, 0, s_saved);   /* for Continue */
    if (s_playing) re_host()->hide_room();
    pe_re_session_end(&s_session);
    s_playing = 0;
    s_room_reported = 0;
    re_host()->set_placements(NULL, 0);
    s_battle_paused = 0;
    if (re_host()->release_backdrop) re_host()->release_backdrop();
    snprintf(s_status, sizeof s_status, "Not playing.");
    pe_mod_crash_note("pe_re_play", "");
}

static int s_checkpoint_key = -1;              /* the room last written, so a room is written once a visit */
static void write_checkpoint(void) {
    char path[1200];
    static uint8_t bytes[PE_RE_SAVE_SIZE];
    if (!s_playing || !s_session.game) return;
    pe_re_save_encode(&s_session, 1, bytes);
    checkpoint_path(path, sizeof path);
    FILE *f = fopen(path, "wb");
    if (!f) { pe_mod_log("pe_re_play", "checkpoint: can't write %s", path); return; }
    const int ok = fwrite(bytes, 1, sizeof bytes, f) == sizeof bytes;
    if (fclose(f) || !ok) { pe_mod_log("pe_re_play", "checkpoint: write failed"); return; }
    s_checkpoint_key = s_session.stage << 8 | s_session.room;
    update_crash_note(1);                      /* a new room: the crash note says so at once */
}
/* The last room's checkpoint into s_saved, for resume(). 0, or -1 (none, or not a game in progress). */
static int load_checkpoint(void) {
    char path[1200];
    static uint8_t bytes[PE_RE_SAVE_SIZE];
    PeReSaved saved;
    checkpoint_path(path, sizeof path);
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    const size_t got = fread(bytes, 1, sizeof bytes, f);
    fclose(f);
    if (got != sizeof bytes || pe_re_save_decode(bytes, &saved) || !saved.game || !saved.playing) return -1;
    memcpy(s_saved, bytes, sizeof s_saved);
    return 0;
}

static void start_room(void) {
    s_games_started++;
    s_start_pending = 0;
    s_opening_movies = NULL;
    s_playing = 1, s_cross_held = 1;           /* the Cross that chose Start isn't a door press */
    if (s_registered && !keep_progress()) pe_toast("Progress in this game won't be saved: the save's companion has no room.");
    re_host()->set_masks(s_masks);
    re_host()->set_player_size(s_size);
    show_room(0);
    write_checkpoint();
}

static void advance_start(void) {
    while (s_opening_movies && *s_opening_movies) {
        const unsigned id = *s_opening_movies++;
        if (start_movie(id)) {
            snprintf(s_status, sizeof s_status, "Playing the opening cinematic.");
            return;
        }
    }
    start_room();
}

static void start_game(const PeReGame *game, unsigned player, int scenario_b, const unsigned *opening_movies)
{
    char why[200] = "";
    if (s_battle_paused) return;
    stop();
    const ReHost *host = re_host();
    const int error = scenario_b ?
        pe_re_session_start_b(&s_session, s_store, game, player, why, sizeof why) :
        pe_re_session_start(&s_session, s_store, game, player, why, sizeof why);
    if (error) {
        snprintf(s_status, sizeof s_status, "Couldn't start: %s.", why);
        pe_toast(s_status);
        return;
    }
    s_start_pending = 1;
    s_opening_movies = opening_movies;
    if (host->start_from_title && host->start_from_title()) {
        snprintf(s_status, sizeof s_status, "Starting %s...", s_session.game->title);
    } else if (!host->start_ready || host->start_ready()) {
        advance_start();
    } else {
        s_start_pending = 0;
        s_opening_movies = NULL;
        pe_re_session_end(&s_session);
        snprintf(s_status, sizeof s_status, "Start is available at the title or in the free field.");
    }
}

static void start(void)
{
    if (s_battle_paused) return;
    list_games();
    if (s_game_pick >= s_offers) { snprintf(s_status, sizeof s_status, "Import a game first (F1 > Import)."); return; }
    const PeReGame *game = s_offer[s_game_pick];
    if (!game->playable) {
        snprintf(s_status, sizeof s_status, "%s isn't playable yet: only RE2 rooms are.", game->title);
        return;
    }
    start_game(game, (unsigned)s_player, 0, NULL);
}

/* Where the save (or the last Stop) left off. */
static void resume(void) {
    PeReSaved saved;
    char why[200] = "";
    if (s_battle_paused) return;
    s_resume = 0;
    if (pe_re_save_decode(s_saved, &saved) || !saved.game) {
        snprintf(s_status, sizeof s_status, "Nothing to continue in this save.");
        return;
    }
    if (!pe_re_imported(s_store, saved.game)) {
        snprintf(s_status, sizeof s_status, "This save is in %s, which isn't imported here.", saved.game->title);
        return;
    }
    stop();
    if (pe_re_session_resume(&s_session, s_store, &saved, why, sizeof why)) {
        snprintf(s_status, sizeof s_status, "Couldn't continue: %s.", why);
        return;
    }
    s_playing = 1, s_cross_held = 1;
    keep_progress();
    re_host()->set_masks(s_masks);
    re_host()->set_player_size(s_size);
    show_room(1);
    write_checkpoint();
}

enum { TITLE_LEON_A, TITLE_CLAIRE_A, TITLE_LEON_B, TITLE_CLAIRE_B, TITLE_CONTINUE, TITLE_LAST_ROOM, TITLE_BACK };

static void open_title(void)
{
    const ReHost *host = re_host();
    if (s_battle_paused || pe_re_door_busy() || host->dialogue_busy() || s_typing != TYPE_NONE) return;
    if (!pe_re_imported(s_store, pe_re_game_named("re2"))) {
        pe_toast("Import Resident Evil 2 first.");
        return;
    }
    stop();
    s_resume = 0;
    if ((!host->start_from_title || !host->start_from_title()) &&
        host->start_ready && !host->start_ready()) {
        pe_toast("Open the RE2 title menu from the title or the free field.");
        return;
    }
    s_title = TITLE_WAIT_FIELD;
    snprintf(s_status, sizeof s_status, "Opening the RE2 title menu...");
}

static void tick_title(void)
{
    const ReHost *host = re_host();
    if (s_title == TITLE_WAIT_FIELD) {
        if (host->native_menu_active() || (host->start_ready ? !host->start_ready() : !host->field_free())) return;
        static const char *const options[] = {
            "New Game: Leon A", "New Game: Claire A", "New Game: Leon B",
            "New Game: Claire B", "Continue current save", "Continue from last room", "Back"
        };
        if (host->choose("Resident Evil 2", options, sizeof options / sizeof *options, TITLE_LEON_A)) {
            s_title = TITLE_CHOOSING;
            snprintf(s_status, sizeof s_status, "Resident Evil 2 title menu.");
        } else {
            s_title = TITLE_CLOSED;
            pe_toast("The RE2 title menu could not open.");
        }
        return;
    }

    if (s_title == TITLE_STARTING) {
        /* the choice box closes over the next frames: start once the field is free again (a few seconds at most) */
        const int ready = !host->dialogue_busy() && !host->native_menu_active() &&
                          (host->start_ready ? host->start_ready() : host->field_free());
        if (!ready && ++s_title_wait < 300) return;
        s_title = TITLE_CLOSED;
        if (s_title_choice == TITLE_CONTINUE) resume();
        else if (s_title_choice == TITLE_LAST_ROOM) {
            if (load_checkpoint()) pe_toast("There's no checkpoint for the last room yet.");
            else resume();
        } else {
            const int scenario_b = s_title_choice >= TITLE_LEON_B;
            start_game(pe_re_game_named("re2"), (unsigned)s_title_choice % 2, scenario_b,
                       scenario_b ? s_scenario_b_opening : s_scenario_a_opening);
        }
        return;
    }
    const int choice = host->dialogue_choice();
    if (choice == -1 && host->dialogue_busy()) return;
    if (choice >= TITLE_LEON_A && choice <= TITLE_CLAIRE_B) {
        s_title = TITLE_STARTING, s_title_choice = choice, s_title_wait = 0;
        return;
    }
    if (choice == TITLE_CONTINUE) {
        PeReSaved saved;
        if (!pe_re_save_decode(s_saved, &saved) && saved.game == pe_re_game_named("re2")) {
            s_title = TITLE_STARTING, s_title_choice = choice, s_title_wait = 0;
            return;
        }
        pe_toast("No RE2 progress in the current save. Load a card in Parasite Eve first.");
        s_title = TITLE_WAIT_FIELD;
        return;
    }
    if (choice == TITLE_LAST_ROOM) {
        s_title = TITLE_STARTING, s_title_choice = choice, s_title_wait = 0;
        return;
    }
    s_title = TITLE_CLOSED;
    snprintf(s_status, sizeof s_status, "Not playing.");
}

/* The host has put this save's bytes (or zeros) in s_saved. */
static int on_save_read(void *ctx, void *user) {
    PeReSaved saved;
    (void)ctx, (void)user;
    s_ending = ENDING_NONE;
    stop_movie();
    if (s_title != TITLE_CLOSED) re_host()->forget_dialogue();
    s_title = TITLE_CLOSED;
    pe_re_door_cancel();
    pe_re_music_stop();
    s_battle_paused = 0;
    if (re_host()->release_backdrop) re_host()->release_backdrop();
    s_typing = TYPE_NONE;
    re_host()->set_placements(NULL, 0);
    if (s_playing) {                            /* the loaded save replaces what was being played */
        re_host()->forget_dialogue();
        re_host()->hide_room();
        pe_re_session_end(&s_session);
        s_playing = 0;
    }
    if (s_start_pending) {
        pe_re_session_end(&s_session);
        s_start_pending = 0;
        s_opening_movies = NULL;
    }
    s_resume = !pe_re_save_decode(s_saved, &saved) && saved.game && saved.playing;
    snprintf(s_status, sizeof s_status, s_resume ? "Back to %s when the field is free." : "Not playing.",
             saved.game ? saved.game->title : "");
    return 0;
}

static int on_new_game(void *ctx, void *user) {    /* the host has cleared the section */
    (void)ctx, (void)user;
    if (!s_start_pending && s_title != TITLE_WAIT_FIELD) stop();
    memset(s_saved, 0, sizeof s_saved);
    s_resume = 0;
    return 0;
}

/* ENDING.C selects the full B movie or the silent A excerpt. Both go to
 * the results after playback. Keep the field scripts parked from SYS_ENDING. */
static int tick_ending(void)
{
    if (!s_ending && !pe_re_flag(&s_session.progress.flags, 0, 0x0A)) return 0;
    const ReHost *host = re_host();
    if (!s_ending) {
        const int scenario_b = pe_re_flag(&s_session.progress.flags, PE_RE_FG_STATUS, 1);
        s_ending = ENDING_MOVIE;
        if (s_enabled) pe_re_save_encode(&s_session, 1, s_saved);
        if (start_movie(scenario_b ? RE2_MOVIE_B_ENDING : RE2_MOVIE_A_ENDING)) {
            snprintf(s_status, sizeof s_status, "Playing the RE2 ending.");
            pe_mod_log(MOD_ID, "RE2 ending movie started for player %u scenario %c",
                       (unsigned)s_session.player, scenario_b ? 'B' : 'A');
            return 1;
        }
    }
    if (s_ending == ENDING_MOVIE) {
        if (tick_movie()) return 1;
        s_ending = ENDING_RESULTS;
        pe_re_music_results();
        host->forget_dialogue();
        sync_scene_controls();
        const char *character = s_session.player ? "Claire" : "Leon";
        const char scenario = pe_re_flag(&s_session.progress.flags, PE_RE_FG_STATUS, 1) ? 'B' : 'A';
        char title[48];
        snprintf(title, sizeof title, "%s %c complete", character, scenario);
        snprintf(s_status, sizeof s_status, "RE2 results: %s.", title);
        pe_mod_log(MOD_ID, "%s", s_status);
        static const char *const choices[] = {"Return to RE2 title"};
        if (!host->choose(title, choices, 1, 0)) {
            stop();
            s_title = TITLE_WAIT_FIELD;
        }
        return 1;
    }
    if (host->dialogue_choice() != -1 || !host->dialogue_busy()) {
        stop();
        s_title = TITLE_WAIT_FIELD;
    } else {
        pe_re_music_results();
    }
    return 1;
}

static int on_frame(void *ctx, void *user) {
    (void)ctx, (void)user;
    if (s_title != TITLE_CLOSED) {
        tick_title();
        return 0;
    }
    if (s_start_pending) {
        if (tick_movie()) return 0;
        const ReHost *host = re_host();
        if (host->native_menu_active() || (host->start_ready && !host->start_ready())) return 0;
        advance_start();
        if (s_movie_playing) return 0;
    }
    if (s_resume && !s_playing && re_host()->field_free()) resume();
    if (!s_playing) return 0;
    const ReHost *music_host = re_host();
    if (s_battle_paused ||
        (music_host->struct_size >= offsetof(ReHost, native_battle_active) + sizeof music_host->native_battle_active &&
         music_host->native_battle_active && music_host->native_battle_active())) {
        pe_re_music_suspend();
        return 0;
    }
    if (s_typing == TYPE_OPEN && re_host()->room_active() && !re_host()->dialogue_busy() && re_host()->field_free()) {
        if (s_enabled) pe_re_save_encode(&s_session, 1, s_saved);   /* the ribbon already used */
        re_host()->open_save_screen();
        s_typing = TYPE_SCREEN;
        pe_mod_log("pe_re_play", "typewriter: save screen");
        return 0;
    }
    if (s_typing == TYPE_SCREEN && re_host()->room_active() && re_host()->field_free() && !re_host()->native_menu_active()) {
        if (!s_typed) pe_re_give(&s_session.progress, PE_RE2_INK_RIBBON, 1);   /* backed out: not used */
        snprintf(s_status, sizeof s_status, s_typed ? "Saved at the typewriter." : "Not saved.");
        s_typing = TYPE_NONE;
    }
    if (re_host()->native_menu_active()) return 0;    /* PE's own menu (the save screen) has the pad */
    if (!re_host()->room_active()) {                /* PE loaded a room of its own: it's only the shell */
        stop_movie();
        re_host()->forget_dialogue();
        if (!re_host()->field_free()) return 0;         /* once it has settled, the imported room again */
        s_reshown++;
        show_room(1);
        return 0;
    }
    /* The room underneath never hears Cross (no talk, no examine). While a message box is up it's the
     * box's: it takes the pad itself (pe_re_box.c) and turns its pages with that Cross. */
    if (!re_host()->dialogue_busy()) re_host()->consume_native_interact();
    re_host()->set_masks(s_masks);
    re_host()->set_player_size(s_size);
    re_host()->set_placement_markers(s_placement_markers);
    const int encounters = !strcmp(s_session.game->game, "re2");
    re_host()->set_placement_meshes(s_placement_meshes ||
                                    (encounters && s_session.stage == 0 && s_session.room <= 1) ? PE_RE_MESH_ALL :
                                    encounters ? PE_RE_MESH_ENCOUNTERS : PE_RE_MESH_OFF);
    re_host()->set_placement_idle(s_placement_idle);
    if (pe_re_door_busy()) {                     /* a door: nothing else happens until it's through */
        const unsigned buttons = re_host()->held_buttons();
        s_cross_held = (buttons & RE_HOST_CROSS) != 0;
        s_square_held = (buttons & RE_HOST_SQUARE) != 0;
        s_start_held = (buttons & RE_HOST_START) != 0, s_select_held = (buttons & RE_HOST_SELECT) != 0;
        const PeReDoorStep door_step = pe_re_door_step();
        if (door_step == PE_RE_DOOR_SWAP) {
            show_room(1);
            if (s_playing && (s_session.stage << 8 | s_session.room) != s_checkpoint_key) write_checkpoint();
            if (s_playing && s_session.said[0]) say(s_session.said);
        }
        if (s_playing && door_step == PE_RE_DOOR_IDLE) report_enemies();
        if (s_playing && s_enabled) pe_re_save_encode(&s_session, 1, s_saved);
        return 0;
    }
    if (s_ending) {
        tick_ending();
        return 0;
    }
    if (movie_frame() || tick_ending()) return 0;
    s_session.talking = re_host()->dialogue_busy;
    {   /* the scripts' key checks: the pad in the game's key bits (none while a box has it) */
        const int up = re_host()->dialogue_busy();
        const uint16_t held = re_host()->held_buttons();
        if (up || s_box_was_up) s_box_buttons = held;
        else s_box_buttons &= held;
        s_box_was_up = up;
        const uint16_t pad = up ? 0 : held & (uint16_t)~s_box_buttons;
        const uint32_t key = pe_re_scd_keys(pad) & (scene_locked() ? 0x3C00u : 0xFFFFu);
        s_session.key_trg = key & ~s_session.key;
        s_session.key = key;
    }
    picked();
    if (!s_playing) return 0;                    /* the pause menu ended play (Back to Parasite Eve, the title menu) */
    const PeReChange script_change = pe_re_session_tick(&s_session);
    pe_re_music_update(&s_session);
    switch (script_change) {
    case PE_RE_SAID: say(s_session.said); break;
    case PE_RE_ZONE: zone_handed(); break;
    case PE_RE_CAMERA: break;
    default: break;
    }
    if (tick_ending()) return 0;
    if (s_session.moved) {                       /* a cutscene walked her somewhere: she's there */
        if (s_session.floor != re_host()->player_floor()) show_room(1);   /* on another floor: the room again there */
        else {
            re_host()->place_player(s_session.x, s_session.z);
            re_host()->face_player(s_session.facing);
        }
        s_session.moved = 0;
    }
    if (s_playing && script_change == PE_RE_CAMERA) show_cut();
    if (!s_playing || movie_frame()) return 0;
    /* a room entered some other way (a test jump): its checkpoint once she's placed */
    if (re_host()->room_active() && (s_session.stage << 8 | s_session.room) != s_checkpoint_key) write_checkpoint();
    update_crash_note(0);
    if (s_report_pending && !re_host()->dialogue_busy() && !--s_report_pending) bug_report();
    sync_scene_controls();
    const int locked = scene_locked();
    apply_collision(0);
    {   /* a scene that holds her and never moves on (30 s): point the tester at Unstick, once */
        static uint32_t mark;
        static int still, hinted;
        if (locked && !s_movie_playing && !re_host()->dialogue_busy()) {
            const uint32_t now = pe_re_session_scene_mark(&s_session);
            if (now != mark) mark = now, still = 0, hinted = 0;
            else if (++still >= 30 * 60 && !hinted) {
                hinted = 1;
                pe_mod_log("pe_re_play", "a scene has held the player for 30 s without moving on (room %d-%02X)",
                           s_session.stage + 1, (unsigned)s_session.room);
                pe_toast("This scene seems stuck. Select > Unstick scene.");
            }
        } else {
            still = 0;
        }
    }
    {   /* Square: the pocket, to use something here; Start: the status screen; Select: the pause menu */
        const uint16_t held = re_host()->held_buttons();
        const int square = (held & RE_HOST_SQUARE) != 0, start = (held & RE_HOST_START) != 0,
                  select = (held & RE_HOST_SELECT) != 0;
        const int free = !locked && !s_climbing && !re_host()->dialogue_busy() && s_pick == PICK_NONE;
        /* Select works while a scene holds her too (Unstick scene), but not over a box or a movie */
        const int pausable = !s_climbing && !re_host()->dialogue_busy() && s_pick == PICK_NONE && !s_movie_playing &&
                             !s_session.run.movie_request;
        if (square && !s_square_held && free && !(s_box_buttons & RE_HOST_SQUARE)) open_pocket();
        else if (start && !s_start_held && free) open_status(0);
        else if (select && !s_select_held && pausable) open_pause();
        s_square_held = square, s_start_held = start, s_select_held = select;
    }
    const int cross = (re_host()->held_buttons() & RE_HOST_CROSS) != 0;
    const int pressed = cross && !s_cross_held && !locked && !re_host()->dialogue_busy() &&
                        !(s_box_buttons & RE_HOST_CROSS);   /* a box's Cross is the box's */
    s_cross_held = cross;
    const ReHost *host = re_host();
    if (host->struct_size >= offsetof(ReHost, climb) + sizeof host->climb && host->climb) {
        char asset[PE_IMP_ID_MAX] = "";
        if (pressed && !s_climbing && host->field_free()) pe_re_session_player_room_move(&s_session, asset, sizeof asset);
        const int climb = host->climb(pressed, asset, s_session.player_id == 12 ? 12 : s_session.player);
        s_climbing = climb == 1;
        if (s_climbing) return 0;
    }
    double x, z;
    int facing;
    char why[200] = "";
    re_host()->player_pose(&x, &z, &facing);
    s_session.facing = facing, s_session.floor = re_host()->player_floor();
    switch (pe_re_session_step(&s_session, s_store, x, z, pressed, why, sizeof why)) {
    case PE_RE_CAMERA: show_room(1); break;
    case PE_RE_ROOM:                             /* the door's transition puts the next room up at black */
        s_room_reported = 0;
        re_host()->set_placements(NULL, 0);
        pe_re_door_begin(s_session.game->game, &s_session.door);
        snprintf(s_status, sizeof s_status, "Through the door to %x-%02X.%s%s", (unsigned)s_session.stage + 1,
                 (unsigned)s_session.room, s_session.said[0] ? " " : "", s_session.said);
        break;
    case PE_RE_SAID:
        say(s_session.said);
        snprintf(s_status, sizeof s_status, "%s", s_session.said);
        break;
    case PE_RE_ZONE: zone_handed(); break;          /* the pocket, an item box, a typewriter */
    case PE_RE_STAY:
        if (*why) snprintf(s_status, sizeof s_status, "That door: %s.", why);
        break;
    }
    if (s_playing && !pe_re_door_busy()) {
        report_enemies();
        if (!locked && re_host()->field_free() && !re_host()->dialogue_busy()) advance_enemies(x, z, s_session.floor);
    }
    /* the survivors' grace after an escape runs in the field (D18) */
    if (s_playing && s_battle_ready && s_battle.phase == PE_RE_BATTLE_GRACE)
        pe_re_battle_step(&s_battle, &s_session, x, z, 1, NULL, NULL, 0);
    if (s_playing && s_enabled) pe_re_save_encode(&s_session, 1, s_saved);
    return 0;
}

/* Testing: stand in the middle of the room's next doorway, pickup or message zone (those its scripts
 * set up), one after another each time (the camera stays as it is). */
static int s_door_pick, s_door_start = -1;            /* testing: Go to that door */
static void to_zone(uint32_t tag, int kind, const char *what) {
    static uint32_t s_next[10];
    static const PeImpRoomLogic *s_room;
    uint32_t n = 0, *next = &s_next[tag == PE_IMP_LOGIC_PICK ? 1 : tag != PE_IMP_LOGIC_TRIG ? 0 : 2 + (kind & 7)];
    int front = 0, zone_floor = -1;
    double bx = 0, bz = 0;
    int found = 0;
    if (s_battle_paused) return;
    if (!s_playing) {
        snprintf(s_status, sizeof s_status, "This room has no %ss.", what);
        return;
    }
    pe_imp_logic_section(s_session.logic, tag, &n, NULL);
    const uint32_t stored_zones = n;
    if (tag == PE_IMP_LOGIC_TRIG && kind == PE_RE_ZONE_EVENT) {
        uint32_t doors = 0;
        pe_imp_logic_section(s_session.logic, PE_IMP_LOGIC_DOOR, &doors, NULL);
        n += doors;
    }
    if (!n) {
        snprintf(s_status, sizeof s_status, "This room has no %ss.", what);
        return;
    }
    if (s_climbing || scene_locked()) {                                  /* a scene is moving her: it would undo the move */
        snprintf(s_status, sizeof s_status, "A scene is playing: wait for it to end.");
        s_door_start = -1;
        return;
    }
    if (s_room != s_session.logic) s_room = s_session.logic, memset(s_next, 0, sizeof s_next);
    const int picked = tag == PE_IMP_LOGIC_DOOR && s_door_start >= 0;  /* that door only, not the next one */
    if (picked) {
        if ((uint32_t)s_door_start >= n) {
            snprintf(s_status, sizeof s_status, "This room has %u doors (0 to %u).", (unsigned)n, (unsigned)n - 1);
            s_door_start = -1;
            return;
        }
        *next = (uint32_t)s_door_start, s_door_start = -1;
    }
    for (uint32_t k = 0; k < (picked ? 1u : n) && !found; k++) {
        const uint32_t i = (*next + k) % n;
        PeImpDoor d;
        PeImpPickup pk;
        PeImpTrigger t;
        const PeImpZone *zone = &d.zone;
        if (tag == PE_IMP_LOGIC_PICK) {
            if (pe_imp_logic_pickup(s_session.logic, i, &pk) || !pe_re_scd_live(&s_session.run, pk.at) ||
                pe_re_pickup_gone(s_session.logic, &s_session.run, &s_session.progress, &pk))
                continue;
            zone = &pk.zone;
        } else if (tag == PE_IMP_LOGIC_TRIG && i >= stored_zones) {
            PeReZoneHit hit;
            /* Aot_reset can turn a door into a temporary scripted event. */
            if (pe_imp_logic_door(s_session.logic, i - stored_zones, &d) ||
                !pe_re_scd_live(&s_session.run, d.at) ||
                pe_re_zone_by_id(s_session.logic, &s_session.run, &s_session.progress, d.zone.id, &hit) ||
                hit.sce != PE_RE_ZONE_EVENT)
                continue;
            front = (hit.sat & (PE_RE_SAT_UNDER | PE_RE_SAT_FRONT)) == PE_RE_SAT_FRONT;
        } else if (tag == PE_IMP_LOGIC_TRIG) {
            if (pe_imp_logic_trigger(s_session.logic, i, &t) || !pe_re_scd_live(&s_session.run, t.at) ||
                pe_re_zone_kind(&s_session.run, t.zone.id, t.zone.sce, t.data) != kind)
                continue;
            if (kind == PE_RE_ZONE_FLAG && ((t.data[0] | t.data[1] << 8) != PE_RE_FG_USE || !t.data[4])) continue;
            if (kind == PE_RE_ZONE_FLAG) s_use_here = (uint16_t)(t.data[2] | t.data[3] << 8);
            zone = &t.zone;
            front = (t.zone.sat & (PE_RE_SAT_UNDER | PE_RE_SAT_FRONT)) == PE_RE_SAT_FRONT;
        } else if (pe_imp_logic_door(s_session.logic, i, &d) || !pe_re_scd_live(&s_session.run, d.at) ||
                   pe_re_zone_kind(&s_session.run, d.zone.id, d.zone.sce, NULL) != d.zone.sce) {
            continue;
        } else {                                          /* a door checked in front of her: face it too */
            front = (d.zone.sat & (PE_RE_SAT_UNDER | PE_RE_SAT_FRONT)) == PE_RE_SAT_FRONT;
        }
        for (int c = 0; c < 4; c++) bx += zone->x[c] / 4.0, bz += zone->z[c] / 4.0;
        zone_floor = zone->floor & 0x80 ? -1 : zone->floor;
        found = 1, *next = i + 1;
    }
    if (!found && picked) {
        snprintf(s_status, sizeof s_status, "Door %u isn't set up by the room's scripts (yet).", (unsigned)*next);
        return;
    }
    if (!found) { snprintf(s_status, sizeof s_status, "This room has no %ss.", what); return; }
    int face = 0;
    if (front) {                                          /* checked in front of her: stand back, facing it */
        double sx, sz;
        for (int k = 0; k < 16; k++) {                    /* the first free spot round it */
            pe_re_front(bx, bz, k * 256 + 2048, &sx, &sz);
            if (!re_host()->position_blocked(sx, sz) || k == 15) { face = k * 256; break; }
        }
        pe_re_front(bx, bz, face + 2048, &bx, &bz);
    }
    if (zone_floor >= 0 && zone_floor != s_session.floor) {   /* the zone is on another floor: put her on it */
        s_session.x = bx, s_session.z = bz, s_session.floor = zone_floor;
        if (front) s_session.facing = face;
        show_room(1);
        if (!s_playing) return;
    } else {
        re_host()->place_player(bx, bz);
        if (front) re_host()->face_player(face), s_session.facing = face;
    }
    s_session.x = bx, s_session.z = bz;
    snprintf(s_status, sizeof s_status, "At a %s in room %x-%02X: press Cross.", what, (unsigned)s_session.stage + 1,
             (unsigned)s_session.room);
}
static void to_door(void) { to_zone(PE_IMP_LOGIC_DOOR, 0, "door"); }
static void to_that_door(void) { s_door_start = s_door_pick; to_zone(PE_IMP_LOGIC_DOOR, 0, "door"); }
static void to_pickup(void) { to_zone(PE_IMP_LOGIC_PICK, 0, "pickup"); }
static void to_message(void) { to_zone(PE_IMP_LOGIC_TRIG, PE_RE_SCE_MESSAGE, "message"); }
static void to_event(void) { to_zone(PE_IMP_LOGIC_TRIG, PE_RE_ZONE_EVENT, "event zone"); }
static void to_item_spot(void) { to_zone(PE_IMP_LOGIC_TRIG, PE_RE_ZONE_FLAG, "place to use an item"); }
static void to_item_box(void) { to_zone(PE_IMP_LOGIC_TRIG, PE_RE_ZONE_ITEMBOX, "item box"); }
static void to_typewriter(void) { to_zone(PE_IMP_LOGIC_TRIG, PE_RE_ZONE_SAVE, "typewriter"); }
static void give_ribbon(void) {
    if (!s_playing) return;
    if (pe_re_give(&s_session.progress, PE_RE2_INK_RIBBON, 1)) snprintf(s_status, sizeof s_status, "No room for an Ink Ribbon.");
    else snprintf(s_status, sizeof s_status, "Given an Ink Ribbon.");
}
static void give_use_here(void) {           /* the item the last place to use one wants */
    char name[48];
    if (!s_playing || !s_use_here) { snprintf(s_status, sizeof s_status, "Go to a place to use an item first."); return; }
    pe_re_item_name(s_session.items, s_use_here, name, sizeof name);
    if (pe_re_give(&s_session.progress, s_use_here, 1)) snprintf(s_status, sizeof s_status, "No room for the %s.", name);
    else snprintf(s_status, sizeof s_status, "Given the %s: press Square to use it.", name);
}
static void to_ledge(void) {
    const ReHost *host = re_host();
    if (!s_playing || s_battle_paused || s_climbing || scene_locked() || host->struct_size < offsetof(ReHost, place_at_ledge) + sizeof host->place_at_ledge ||
        !host->place_at_ledge) return;
    char asset[PE_IMP_ID_MAX];
    int camera;
    if (pe_re_session_player_room_move(&s_session, asset, sizeof asset) || !host->place_at_ledge(&camera)) {
        snprintf(s_status, sizeof s_status, "No reachable one-level ledge on this floor.");
        return;
    }
    double x, z;
    int facing;
    host->player_pose(&x, &z, &facing);
    pe_re_session_place(&s_session, x, z, facing);
    if (s_session.camera != camera) {
        s_session.camera = camera;
        show_room(1);
        if (!s_playing) return;
    }
    snprintf(s_status, sizeof s_status, "Facing a climbable ledge: press Cross.");
}

static void to_ramp(void) {
    if (!s_playing || s_battle_paused) return;
    if (!re_host()->place_at_ramp || !re_host()->place_at_ramp()) { snprintf(s_status, sizeof s_status, "This room has no slopes or stairs."); return; }
    re_host()->player_pose(&s_session.x, &s_session.z, &s_session.facing);
    s_session.floor = re_host()->player_floor();
    snprintf(s_status, sizeof s_status, "At the foot of a slope or stairs: walk onto it.");
}
static void to_room(void) {
    char why[200] = "";
    if (s_battle_paused || s_movie_playing) return;
    if (!s_playing) { snprintf(s_status, sizeof s_status, "Start a game first."); return; }
    if (pe_re_session_enter(&s_session, s_store, (uint32_t)(s_jump_stage - 1), (uint32_t)s_jump_room, 0, 0, 0, 0, why,
                            sizeof why)) {
        snprintf(s_status, sizeof s_status, "Can't go there: %s.", why);
        return;
    }
    pe_re_door_cancel();
    s_room_reported = 0;
    re_host()->set_placements(NULL, 0);
    show_room(0);
}

static void refresh(void) { s_listed = 0; list_games(); }
static void text_scripts(char *out, unsigned size) {
    const PeReScdRun *r = &s_session.run;
    if (!s_playing || !r->ran) { snprintf(out, size, s_playing ? "Not run (every zone counts)." : "-"); return; }
    snprintf(out, size, "%d tasks running; %d instructions, %d skipped (first 0x%02X), %d over budget; %d zones, %d changed; %d events; F/U zone %d/%d.",
             r->waiting, r->executed, r->skipped, r->skipped ? r->first_skipped : 0, r->stopped, r->nlive, r->nreset,
             s_session.events, s_session.progress.flags.work[PE_RE_WORK_F_ATARI], s_session.progress.flags.work[PE_RE_WORK_U_ATARI]);
}

static void text_pocket(char *out, unsigned size) {
    size_t w = 0;
    out[0] = 0;
    if (!s_playing || !s_session.progress.held) { snprintf(out, size, "Empty."); return; }
    for (int i = 0; i < s_session.progress.held && w + 1 < size; i++) {
        char name[48];
        const PeReHeld *h = &s_session.progress.pocket[i];
        pe_re_item_name(s_session.items, h->item, name, sizeof name);
        int k = h->count > 1 ? snprintf(out + w, size - w, "%s%s x%u", i ? ", " : "", name, (unsigned)h->count)
                             : snprintf(out + w, size - w, "%s%s", i ? ", " : "", name);
        if (k < 0) break;
        w += (size_t)k;
    }
}
static void text_scene(char *out, unsigned size) {
    snprintf(out, size, "%s", !s_playing ? "-" : s_session.run.player_dest.moving ? "Scripted movement" :
                              scene_locked() ? "Input locked" : "Player control");
}
static void text_status(char *out, unsigned size) {
    if (s_playing) snprintf(out, size, "%s x %.0f, z %.0f, floor %d, doors %d, scale %.1f.", s_status, s_session.x,
                            s_session.z, s_session.floor, s_session.doors_taken, re_host()->player_scale());
    else snprintf(out, size, "%s", s_status);
}

/* ---- for testers: the crash note and the bug report ---- */
static const char *scenario_name(void) {
    if (s_session.player_id == 12) return "HUNK";
    const int b = pe_re_flag(&s_session.progress.flags, PE_RE_FG_STATUS, 1);
    return s_session.player ? (b ? "Claire B" : "Claire A") : (b ? "Leon B" : "Leon A");
}
/* What crash reports print under RE play (pe_mod_crash_note), kept current twice a second while playing. */
static void update_crash_note(int force) {
    static unsigned ticks;
    char scene[400], note[512];
    if (!force && ++ticks % 30) return;
    if (!s_playing || !s_session.game) { pe_mod_crash_note("pe_re_play", s_status); return; }
    pe_re_session_describe_scene(&s_session, scene, sizeof scene);
    snprintf(note, sizeof note, "%s %s at %.0f,%.0f floor %d%s%s; %s", s_session.game->game, scenario_name(),
             s_session.x, s_session.z, s_session.floor, scene_locked() ? ", held by a scene" : "",
             s_movie_playing ? ", movie playing" : "", scene);
    pe_mod_crash_note("pe_re_play", note);
}

static void bug_report(void) {
    static char text[16384];
    static uint8_t progress[PE_RE_SAVE_SIZE];
    char where[1200] = "", line[1024];
    size_t n = 0;
    const ReHost *host = re_host();
#define REPORT(...) do { const int w_ = snprintf(text + n, sizeof text - n, __VA_ARGS__); \
                         if (w_ > 0) n = n + (size_t)w_ < sizeof text ? n + (size_t)w_ : sizeof text - 1; } while (0)
    if (!RE_HOST_HAS(host, write_report)) { pe_toast("This build can't write bug reports."); return; }
    REPORT("RE play bug report\n\nWhat happened (write it here before sending):\n\n\n");
    if (s_playing && s_session.game) {
        REPORT("Game: %s, %s (player %d, id %d)\n", s_session.game->title, scenario_name(), s_session.player,
               s_session.player_id);
        REPORT("Room: %d-%02X, camera %d%s, at x %.0f z %.0f floor %d facing %d\n", s_session.stage + 1,
               (unsigned)s_session.room, s_session.camera, s_session.run.camera_lock ? " (held by the scripts)" : "",
               s_session.x, s_session.z, s_session.floor, s_session.facing);
        pe_re_session_describe_scene(&s_session, line, sizeof line);
        REPORT("Scene: %s\n", line);
        text_scene(line, sizeof line);
        REPORT("Control: %s%s%s%s\n", line, s_movie_playing ? ", movie playing" : "",
               pe_re_door_busy() ? ", door busy" : "", host->dialogue_busy() ? ", a box is up" : "");
        text_status(line, sizeof line);
        REPORT("Status: %s\n", line);
        if (s_session.said[0]) REPORT("Last message: %s\n", s_session.said);
        text_pocket(line, sizeof line);
        REPORT("Pocket: %s\nItem box: %d items\n", line, s_session.progress.boxed);
        text_scripts(line, sizeof line);
        REPORT("Scripts: %s\n", line);
        text_enemies(line, sizeof line);
        REPORT("Enemies: %s\n", line);
        REPORT("Doors taken: %d\n", s_session.doors_taken);
        static const struct { unsigned group; const char *name; } groups[] = {
            {1, "Status"}, {3, "Scenario"}, {4, "Common"}, {5, "Room"}, {31, "Key"}};
        for (size_t g = 0; g < sizeof groups / sizeof *groups; g++) {
            REPORT("%s flags:", groups[g].name);
            for (int b = 0; b < 32; b++) REPORT(" %02X", s_session.progress.flags.bits[groups[g].group][b]);
            REPORT("\n");
        }
        pe_re_save_encode(&s_session, 1, progress);
    } else {
        REPORT("Not playing an imported game. Status: %s\n", s_status);
        memcpy(progress, s_saved, sizeof progress);
    }
#undef REPORT
    if (host->write_report(text, progress, sizeof progress, where, sizeof where)) {
        pe_toast("Couldn't write the bug report (see pe_mods.log).");
        return;
    }
    pe_mod_log("pe_re_play", "bug report written: %s", where);
    snprintf(s_status, sizeof s_status, "Bug report written: %s", where);
    say("Bug report saved. Add what happened to report.txt in its folder (F1 > Play imported games shows where).");
}

static int valid_host(const ReHost *host)
{
    return host && RE_HOST_HAS(host, bind_lifecycle) &&
        host->init && host->shutdown && host->show_room && host->hide_room &&
        host->room_active && host->room_fade && host->player_pose && host->player_floor &&
        host->place_player && host->face_player && host->position_blocked &&
        host->set_masks && host->set_player_size && host->player_scale &&
        host->set_placement_markers && host->set_placement_meshes && host->set_placement_idle &&
        host->set_placements && host->say && host->choose && host->dialogue_choice &&
        host->dialogue_busy && host->forget_dialogue && host->field_free && host->held_buttons &&
        host->consume_native_interact && host->native_menu_active;
}

int pe_re_play_init(const char *store) {
    const ReHost *host = re_host();
    if (!valid_host(host)) {
        pe_mod_log(MOD_ID, "missing required RE host callbacks");
        return 1;
    }
    if (!store || strlen(store) >= sizeof s_store) return 1;
    strcpy(s_store, store);
    re_host()->init(MOD_ID, s_store);
    re_host()->bind_lifecycle(MOD_ID, on_frame, on_save_read, on_new_game);
    s_enabled = 0;
    s_registered = host->open_save_screen && RE_HOST_HAS(host, register_progress) &&
                   RE_HOST_HAS(host, enable_progress) && !host->register_progress(&s_progress, on_committed);
    s_room_event = pe_event_id(RE_PLAY_EVENT_ROOM), s_enemies_event = pe_event_id(RE_PLAY_EVENT_ENEMIES);
    pe_service_publish(RE_PLAY_SERVICE, RE_PLAY_SERVICE_VERSION, &s_api, MOD_ID);
    if (!s_registered) pe_mod_log(MOD_ID, "progress section not registered: imported games won't be saved");
    return 0;
}

void pe_re_play_shutdown(void) {
    stop();
    re_host()->shutdown();
}

const PeRePlayControls *pe_re_play_controls(void)
{
    static const PeRePlayControls controls = {
        .pick = &s_game_pick,
        .game_count = game_count,
        .game_name = game_name,
        .player = &s_player,
        .masks = &s_masks,
        .size = &s_size,
        .start = start,
        .open_title = open_title,
        .resume = resume,
        .stop = stop,
        .placement_markers = &s_placement_markers,
        .placement_meshes = &s_placement_meshes,
        .placement_idle = &s_placement_idle,
        .to_door = to_door,
        .to_pickup = to_pickup,
        .to_ramp = to_ramp,
        .to_ledge = to_ledge,
        .to_message = to_message,
        .to_item_spot = to_item_spot,
        .give_use_here = give_use_here,
        .to_typewriter = to_typewriter,
        .give_ribbon = give_ribbon,
        .to_item_box = to_item_box,
        .to_event = to_event,
        .jump_stage = &s_jump_stage,
        .jump_room = &s_jump_room,
        .to_room = to_room,
        .door_pick = &s_door_pick,
        .to_that_door = to_that_door,
        .refresh = refresh,
        .text_scene = text_scene,
        .text_status = text_status,
        .text_pocket = text_pocket,
        .text_scripts = text_scripts,
        .text_enemies = text_enemies,
        .enemy_debug_slot = &s_enemy_debug_slot,
        .text_enemy_movement = text_enemy_movement,
        .bug_report = request_bug_report,
    };
    return &controls;
}
