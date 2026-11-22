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
#include "pe_mod_menu.h"
#include "pe_extension_section.h"
#include "pe_re_door.h"
#include "pe_re_save.h"
#include "pe_re_session.h"
#include "re_host.h"
#include "re_encounter_rooms.h"

#define MOD_ID "pe_re_play"
#define PAGE "Play imported games"

static char s_store[1100];
static const PeReGame *s_offer[PE_RE_GAMES];
static int s_offers, s_pick, s_player, s_listed, s_masks = 1, s_size = 100, s_jump_stage = 1, s_jump_room;
static PeReSession s_session;
/* The progress, in the save's companion (pe_re_save.h). Enabled before the first change; kept
 * encoded while playing, and on Stop (so Continue picks it up). Loading a save that was in a game
 * resumes there once the field is free. */
static uint8_t s_saved[PE_RE_SAVE_SIZE];
static const PeExtensionSection s_section = {"pe_re_play/progress", PE_RE_SAVE_VERSION, 1, sizeof s_saved, s_saved,
                                             pe_re_save_upgrade, NULL, NULL};   /* min reader 1: the host loads it */
static int s_registered, s_enabled, s_resume;
static int s_playing, s_cross_held, s_square_held, s_reshown;
static int s_script_held;                  /* frames left since a script last moved her */
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
static int s_battle_paused;
static int s_placement_markers;
static int s_placement_meshes;
static int s_placement_idle = 1;
static int s_room_event = -1, s_enemies_event = -1;

static int api_room(PeReRoomEvent *out) {
    if (!out || !s_playing || !s_room_reported || !re_host()->room_active() || pe_re_door_busy()) return -1;
    memset(out, 0, sizeof *out);
    out->struct_size = sizeof *out;
    out->game = s_session.game->game, out->stage = s_session.stage, out->room = s_session.room;
    out->count = s_nenemies, out->enemies = s_enemies;
    return 0;
}
static int api_defeated(uint8_t killed_flag) {
    if (!s_playing || !s_room_reported || !re_host()->room_active() || pe_re_door_busy()) return -1;
    for (int i = 0; i < s_nenemies; i++) {
        if (s_enemies[i].killed_flag == killed_flag)
            return pe_re_session_defeated(&s_session, killed_flag);
    }
    return -1;
}
static int api_defeated_slot(uint8_t slot) {
    if (!s_playing || !s_room_reported || !re_host()->room_active() || pe_re_door_busy()) return -1;
    for (int i = 0; i < s_nenemies; i++)
        if (s_enemies[i].slot == slot) return pe_re_session_beaten(&s_session, slot, s_enemies[i].killed_flag);
    return -1;
}
static int pause_for_battle(void);
static void resume_after_battle(void);
static int encounter_position(double *x, double *z, int *floor);
static int encounter_reachable(double x, double z, double target_x, double target_z);
static void advance_enemies(double x, double z, int floor);
static int encounter_boss(void) { return s_playing ? pe_re_session_ready_boss(&s_session) : -1; }
static int prepare_boss(uint8_t slot);
static int api_canister_defeat(uint8_t slot) {
    if (!s_playing || !s_room_reported || !re_host()->room_active() || pe_re_door_busy()) return -1;
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
static const PeRePlayApi s_api = {
    sizeof s_api, api_room, api_defeated, pause_for_battle, resume_after_battle, api_defeated_slot, api_enemy_archive,
    api_enemy_room_move, encounter_position, encounter_reachable, encounter_boss, prepare_boss, api_canister_defeat,
    advance_enemies
};

static void report_enemies(void) {
    PeReEnemyPlacement placements[PE_RE_ENEMIES_MAX];
    const int fresh = !s_room_reported;
    const int n = pe_re_session_enemies(&s_session, placements, PE_RE_ENEMIES_MAX);
    const ReHost *host = re_host();
    if (host->struct_size >= offsetof(ReHost, set_scene_actors) + sizeof host->set_scene_actors &&
        host->set_scene_actors) {
        PeReSceneActor actors[16];
        const int count = pe_re_scd_scene_actors(&s_session.run, actors, 16);
        host->set_scene_actors(actors, count);
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

/* ---- the pocket and the item box ---- */
enum { PICK_NONE, PICK_USE, PICK_BOX, PICK_SAVE, PICK_COMPUTER, PICK_ALLIGATOR };
/* A typewriter save: the ribbon is used when she says yes; PE's own save screen opens on the next
 * free frame (as its save phones' script opcode opens it); backing out without a save gives the
 * ribbon back once the room is up again. */
enum { TYPE_NONE, TYPE_OPEN, TYPE_SCREEN };
static int s_typing, s_typed;
static int s_pick, s_pick_at;

static int prepare_boss(uint8_t slot) {
    if (pe_re_session_final_tyrant(&s_session, slot)) return PE_RE_BOSS_LAUNCHER;
    if (!pe_re_session_alligator(&s_session, slot)) return PE_RE_BOSS_STANDARD;
    if (pe_re_scd_running(&s_session.run, 10)) return PE_RE_BOSS_WAIT;
    if (pe_re_session_canister_ready(&s_session)) return PE_RE_BOSS_CANISTER;
    if (s_session.run.alligator_chosen) return PE_RE_BOSS_STANDARD;
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
        s_typing != TYPE_NONE || s_script_held || s_pick != PICK_NONE) return -1;
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
    if (s_battle_paused || !re_host()->room_active() || !re_host()->field_free() ||
        pe_re_door_busy() || re_host()->dialogue_busy() || s_typing != TYPE_NONE) return -1;
    re_host()->player_pose(&s_session.x, &s_session.z, &s_session.facing);
    s_session.floor = re_host()->player_floor();
    s_battle_paused = 1;
    re_host()->arm_backdrop();                  /* the battle plays in front of this room */
    re_host()->hide_room();
    snprintf(s_status, sizeof s_status, "Paused for battle.");
    return 1;
}

static void resume_after_battle(void) {
    if (!s_battle_paused) return;
    s_battle_paused = 0;
    re_host()->release_backdrop();
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
        if (!pe_re_has(&s_session.progress, PE_RE2_INK_RIBBON))
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
    if (c < 0) return;                                      /* backed out */
    if (mode == PICK_COMPUTER) {
        if (c == 0 && pe_re_session_computer(&s_session, 1) == PE_RE_COMPUTER_DONE) say(s_session.said);
        return;
    }
    if (mode == PICK_SAVE) {
        if (c == 0 && !pe_re_lose(&s_session.progress, PE_RE2_INK_RIBBON, 1)) s_typing = TYPE_OPEN, s_typed = 0;
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
        return;
    }
    re_host()->player_pose(&s_session.x, &s_session.z, &s_session.facing);
    apply_collision(1);
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
    if (!s_enabled && s_registered) s_enabled = pe_extension_enable(&s_section) == PE_REG_OK;
    return s_enabled;
}

static void stop(void) {
    pe_re_door_cancel();
    re_host()->forget_dialogue();
    s_typing = TYPE_NONE;
    if (s_session.game && keep_progress()) pe_re_save_encode(&s_session, 0, s_saved);   /* for Continue */
    if (s_playing) re_host()->hide_room();
    pe_re_session_end(&s_session);
    s_playing = 0;
    s_room_reported = 0;
    re_host()->set_placements(NULL, 0);
    s_battle_paused = 0;
    re_host()->release_backdrop();
    snprintf(s_status, sizeof s_status, "Not playing.");
}

static void start(void) {
    char why[200] = "";
    if (s_battle_paused) return;
    list_games();
    stop();
    if (s_pick >= s_offers) { snprintf(s_status, sizeof s_status, "Import a game first (F1 > Import)."); return; }
    if (!s_offer[s_pick]->playable) {
        snprintf(s_status, sizeof s_status, "%s isn't playable yet: only RE2 rooms are.", s_offer[s_pick]->title);
        return;
    }
    if (pe_re_session_start(&s_session, s_store, s_offer[s_pick], (uint32_t)s_player, why, sizeof why)) {
        snprintf(s_status, sizeof s_status, "Couldn't start: %s.", why);
        return;
    }
    s_playing = 1, s_cross_held = 1;           /* the Cross that chose Start isn't a door press */
    if (!keep_progress()) pe_toast("Progress in this game won't be saved: the save's companion has no room.");
    re_host()->set_masks(s_masks);
    re_host()->set_player_size(s_size);
    show_room(0);
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
}

/* The host has put this save's bytes (or zeros) in s_saved. */
static int on_save_read(void *ctx, void *user) {
    PeReSaved saved;
    (void)ctx, (void)user;
    pe_re_door_cancel();
    s_battle_paused = 0;
    re_host()->release_backdrop();
    s_typing = TYPE_NONE;
    re_host()->set_placements(NULL, 0);
    if (s_playing) {                            /* the loaded save replaces what was being played */
        re_host()->forget_dialogue();
        re_host()->hide_room();
        pe_re_session_end(&s_session);
        s_playing = 0;
    }
    s_resume = !pe_re_save_decode(s_saved, &saved) && saved.game && saved.playing;
    snprintf(s_status, sizeof s_status, s_resume ? "Back to %s when the field is free." : "Not playing.",
             saved.game ? saved.game->title : "");
    return 0;
}

static int on_new_game(void *ctx, void *user) {    /* the host has cleared the section */
    (void)ctx, (void)user;
    stop();
    memset(s_saved, 0, sizeof s_saved);
    s_resume = 0;
    return 0;
}

static int on_frame(void *ctx, void *user) {
    (void)ctx, (void)user;
    if (s_resume && !s_playing && re_host()->field_free()) resume();
    if (!s_playing || s_battle_paused) return 0;
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
                                    (encounters && pe_re2_opening_street(s_session.stage, s_session.room)) ? PE_RE_MESH_ALL :
                                    encounters ? PE_RE_MESH_ENCOUNTERS : PE_RE_MESH_OFF);
    re_host()->set_placement_idle(s_placement_idle);
    if (pe_re_door_busy()) {                     /* a door: nothing else happens until it's through */
        const unsigned buttons = re_host()->held_buttons();
        s_cross_held = (buttons & RE_HOST_CROSS) != 0;
        s_square_held = (buttons & RE_HOST_SQUARE) != 0;
        const PeReDoorStep door_step = pe_re_door_step();
        if (door_step == PE_RE_DOOR_SWAP) {
            show_room(1);
            if (s_playing && s_session.said[0]) say(s_session.said);
        }
        if (s_playing && door_step == PE_RE_DOOR_IDLE) report_enemies();
        if (s_playing && s_enabled) pe_re_save_encode(&s_session, 1, s_saved);
        return 0;
    }
    s_session.talking = re_host()->dialogue_busy;
    {   /* the scripts' key checks: the pad in the game's key bits (none while a box has it) */
        const uint16_t pad = re_host()->dialogue_busy() ? 0 : re_host()->held_buttons();
        const uint32_t key = pe_re_scd_keys(pad);
        s_session.key_trg = key & ~s_session.key;
        s_session.key = key;
    }
    picked();
    switch (pe_re_session_tick(&s_session)) {     /* the room's scripts, a frame */
    case PE_RE_SAID: say(s_session.said); break;
    case PE_RE_ZONE: zone_handed(); break;
    case PE_RE_CAMERA: show_cut(); break;
    default: break;
    }
    {   /* SYS_ENDING (script bit 0x0A of System_flg): the scenario is over; its ending isn't played here */
        static int s_ending_said;
        const int ending = pe_re_flag(&s_session.progress.flags, 0, 0x0A);
        if (ending && !s_ending_said) say("The end. (The game's ending would play here.)");
        s_ending_said = ending;
    }
    if (s_session.moved) {                       /* a cutscene walked her somewhere: she's there */
        if (s_session.floor != re_host()->player_floor()) show_room(1);   /* on another floor: the room again there */
        else re_host()->place_player(s_session.x, s_session.z);
        s_session.moved = 0;
        s_script_held = 30;
    } else if (s_script_held) {
        s_script_held--;
    }
    apply_collision(0);
    {   /* Square: the pocket, to use something here */
        const int square = (re_host()->held_buttons() & RE_HOST_SQUARE) != 0;
        if (square && !s_square_held && !re_host()->dialogue_busy()) open_pocket();
        s_square_held = square;
    }
    const int cross = (re_host()->held_buttons() & RE_HOST_CROSS) != 0;
    const int pressed = cross && !s_cross_held && !re_host()->dialogue_busy();   /* a box's Cross is the box's */
    s_cross_held = cross;
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
    if (s_playing && !pe_re_door_busy()) report_enemies();
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
    if (!s_playing || !pe_imp_logic_section(s_session.logic, tag, &n, NULL)) {
        snprintf(s_status, sizeof s_status, "This room has no %ss.", what);
        return;
    }
    if (s_script_held) {                                  /* a scene is moving her: it would undo the move */
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
static void to_ramp(void) {
    if (!s_playing || s_battle_paused) return;
    if (!re_host()->place_at_ramp()) { snprintf(s_status, sizeof s_status, "This room has no slopes or stairs."); return; }
    re_host()->player_pose(&s_session.x, &s_session.z, &s_session.facing);
    s_session.floor = re_host()->player_floor();
    snprintf(s_status, sizeof s_status, "At the foot of a slope or stairs: walk onto it.");
}
static void to_room(void) {
    char why[200] = "";
    if (s_battle_paused) return;
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
static void text_status(char *out, unsigned size) {
    if (s_playing) snprintf(out, size, "%s x %.0f, z %.0f, floor %d, doors %d, scale %.1f.", s_status, s_session.x,
                            s_session.z, s_session.floor, s_session.doors_taken, re_host()->player_scale());
    else snprintf(out, size, "%s", s_status);
}

static int init(void) {
    if (pe_mod_path(s_store, sizeof s_store, "import/store")) return 1;
    re_host()->init(MOD_ID, s_store);
    pe_menu_begin(PAGE);
    pe_menu_top_page(PAGE, "Walk the rooms of a game you imported, as Aya.", 63);
    pe_menu_register_dynamic_choice(0, "Game", "A game imported from your own disc (F1 > Import).", &s_pick,
                                    PE_RE_GAMES, game_count, game_name);
    pe_menu_register_int(0, "Scenario", "RE2: 0 Leon, 1 Claire.", &s_player, 0, 1, 1);
    pe_menu_register_toggle(0, "Foreground", "Let the room's foreground cover Aya.", &s_masks, NULL);
    pe_menu_register_int(0, "Aya's size", "Percent of an RE person's height (100: as tall as Leon).", &s_size, 50, 200, 5);
    pe_menu_register_action(0, "Start", "Starts in the game's first room. Walk with the pad; Cross in a doorway goes through.", start);
    pe_menu_register_action(0, "Continue", "Back where you left this save's imported game.", resume);
    pe_menu_register_action(0, "Stop", "Back to the Parasite Eve room and camera; Continue picks it up again.", stop);
    pe_menu_register_toggle("Testing", "Placement markers", "Mark enemy positions using the room camera.", &s_placement_markers, NULL);
    pe_menu_register_toggle("Testing", "Placement meshes", "Draw imported enemy meshes at their placements.", &s_placement_meshes, NULL);
    pe_menu_register_toggle("Testing", "Placement idle animation", "Animate supported placement meshes in place.", &s_placement_idle, NULL);
    pe_menu_register_action("Testing", "Go to the next door", "Puts Aya in the room's doorways, one after another.", to_door);
    pe_menu_register_action("Testing", "Go to the next pickup", "Puts Aya on the room's items, one after another.", to_pickup);
    pe_menu_register_action("Testing", "Go to the next ramp", "Puts Aya at the foot of the room's slopes and stairs, one after another.", to_ramp);
    pe_menu_register_action("Testing", "Go to the next message", "Puts Aya where the room's messages are read, one after another.", to_message);
    pe_menu_register_action("Testing", "Go to the next place to use an item", "Puts Aya facing where the room takes an item (a jewel in a statue), one after another.", to_item_spot);
    pe_menu_register_action("Testing", "Give the item used there", "Puts the item that place takes in the pocket.", give_use_here);
    pe_menu_register_action("Testing", "Go to the next typewriter", "Puts Aya at the room's typewriter: press Cross.", to_typewriter);
    pe_menu_register_action("Testing", "Give an Ink Ribbon", "One Ink Ribbon into the pocket, for a typewriter.", give_ribbon);
    pe_menu_register_action("Testing", "Go to the next item box", "Puts Aya at the room's item box: press Cross.", to_item_box);
    pe_menu_register_action("Testing", "Go to the next event zone", "Puts Aya where the room's events start (examined: press Cross), one after another.", to_event);
    pe_menu_register_int("Testing", "Stage", "The room to jump to: its stage.", &s_jump_stage, 1, 7, 1);
    pe_menu_register_int("Testing", "Room", "The room to jump to: its number in the stage (1-05 is 5).", &s_jump_room, 0, 63, 1);
    pe_menu_register_action("Testing", "Go to that room", "Jumps there, camera 0, keeping what's been picked up.", to_room);
    pe_menu_register_int("Testing", "Door", "A door of this room by its number (0 first), for Go to that door.", &s_door_pick, 0, 31, 1);
    pe_menu_register_action("Testing", "Go to that door", "Puts Aya in that doorway (or the next one the scripts set up).", to_that_door);
    pe_menu_register_action(0, "Refresh the list", "Looks for games imported since the game started.", refresh);
    pe_menu_register_info(0, "Status", "Where you are, or the last thing said.", text_status);
    pe_menu_register_info(0, "Pocket", "What you've picked up in this game.", text_pocket);
    pe_menu_register_info(0, "Scripts", "What this room's own scripts ran when you came in.", text_scripts);
    pe_menu_register_info(0, "Enemies", "Where this room's scripts put enemies: reported to an encounter mod, not fought here.", text_enemies);
    re_host()->bind_lifecycle(MOD_ID, on_frame, on_save_read, on_new_game);
    s_registered = pe_extension_register(&s_section) >= 0;
    pe_hook_add(pe_event_id(PE_EVENT_SAVE_COMMITTED), on_committed, NULL, PE_PRIORITY_NORMAL, MOD_ID);
    s_room_event = pe_event_id(RE_PLAY_EVENT_ROOM), s_enemies_event = pe_event_id(RE_PLAY_EVENT_ENEMIES);
    pe_service_publish(RE_PLAY_SERVICE, RE_PLAY_SERVICE_VERSION, &s_api, MOD_ID);
    if (!s_registered) pe_mod_log(MOD_ID, "progress section not registered: imported games won't be saved");
    return 0;
}

static void shutdown_play(void) {
    stop();
    re_host()->shutdown();
}

const PeMod pe_mod_pe_re_play = {MOD_ID, "Play imported games", "0.1", NULL, init, shutdown_play};
