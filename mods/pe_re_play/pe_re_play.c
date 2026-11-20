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
#include <string.h>

#include "pe_mod_host.h"
#include "pe_mod_menu.h"
#include "pe_hooks.h"
#include "pe1/battle_update.h"
#include "pe1/pad_state.h"
#include "pe_extension_store.h"
#include "pe_game.h"
#include "pe_re_box.h"
#include "pe_re_door.h"
#include "pe_re_save.h"
#include "pe_re_session.h"
#include "pe_re_stage.h"

#define MOD_ID "pe_re_play"
#define PAGE "Play imported games"
#define PAD_CROSS 0x4000u                 /* PS1 pad word, active high once inverted */
#define FIELD_PRESS_CROSS 0x0100u         /* g_FieldPadPressed: Cross (the field's talk / examine) */

static char s_store[1100];
static const PeReGame *s_offer[PE_RE_GAMES];
static int s_offers, s_pick, s_player, s_listed, s_masks = 1, s_size = 100, s_jump_stage = 1, s_jump_room;
static PeReSession s_session;
/* The progress, in the save's companion (pe_re_save.h). Enabled before the first change; kept
 * encoded while playing, and on Stop (so Continue picks it up). Loading a save that was in a game
 * resumes there once the field is free. */
static uint8_t s_saved[PE_RE_SAVE_SIZE];
static const PeExtensionSection s_section = {"pe_re_play/progress", PE_RE_SAVE_VERSION, PE_RE_SAVE_VERSION,
                                             sizeof s_saved, s_saved};
static int s_registered, s_enabled, s_resume;
static int s_playing, s_cross_held, s_reshown;
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
    if (!pe_re_box_say(text)) pe_toast(text);
}

static void show_room(int place) {
    char room[PE_IMP_ID_MAX], why[200] = "";
    pe_re_room_id(s_session.game->game, s_session.stage, s_session.room, room, sizeof room);
    if (pe_re_stage_show(room, s_session.logic, s_session.camera, s_session.floor, place, s_session.x, s_session.z,
                         s_session.facing, why, sizeof why)) {
        snprintf(s_status, sizeof s_status, "Stopped: %s.", why);
        pe_re_stage_hide();
        pe_re_session_end(&s_session);
        s_playing = 0;
        return;
    }
    pe_re_stage_player(&s_session.x, &s_session.z, &s_session.facing);
    snprintf(s_status, sizeof s_status, "Room %x-%02X, camera %d.", (unsigned)s_session.stage + 1,
             (unsigned)s_session.room, s_session.camera);
}

static int keep_progress(void) {
    if (!s_enabled && s_registered) s_enabled = pe_extension_enable(&s_section) == PE_REG_OK;
    return s_enabled;
}

static void stop(void) {
    pe_re_door_cancel();
    pe_re_box_forget();
    if (s_session.game && keep_progress()) pe_re_save_encode(&s_session, 0, s_saved);   /* for Continue */
    if (s_playing) pe_re_stage_hide();
    pe_re_session_end(&s_session);
    s_playing = 0;
    snprintf(s_status, sizeof s_status, "Not playing.");
}

static void start(void) {
    char why[200] = "";
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
    pe_re_stage_set_masks(s_masks);
    pe_re_stage_set_size(s_size);
    show_room(0);
}

/* Where the save (or the last Stop) left off. */
static void resume(void) {
    PeReSaved saved;
    char why[200] = "";
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
    pe_re_stage_set_masks(s_masks);
    pe_re_stage_set_size(s_size);
    show_room(1);
}

/* The host has put this save's bytes (or zeros) in s_saved. */
static int on_save_read(void *ctx, void *user) {
    PeReSaved saved;
    (void)ctx, (void)user;
    pe_re_door_cancel();
    if (s_playing) {                            /* the loaded save replaces what was being played */
        pe_re_box_forget();
        pe_re_stage_hide();
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
    if (s_resume && !s_playing && pe_free_field()) resume();
    if (!s_playing) return 0;
    if (!pe_re_stage_active()) {                /* PE loaded a room of its own: it's only the shell */
        pe_re_box_forget();
        if (!pe_free_field()) return 0;         /* once it has settled, the imported room again */
        s_reshown++;
        show_room(1);
        return 0;
    }
    /* The room underneath never hears Cross (no talk, no examine). While a message box is up it's the
     * box's: it takes the pad itself (pe_re_box.c) and turns its pages with that Cross. */
    if (!pe_re_box_busy()) g_FieldPadPressed &= ~FIELD_PRESS_CROSS;
    pe_re_stage_set_masks(s_masks);
    pe_re_stage_set_size(s_size);
    if (pe_re_door_busy()) {                     /* a door: nothing else happens until it's through */
        s_cross_held = (~(unsigned)g_PadPackets[0].buttons & PAD_CROSS) != 0;
        if (pe_re_door_step() == PE_RE_DOOR_SWAP) {
            show_room(1);
            if (s_playing && s_session.said[0]) say(s_session.said);
        }
        if (s_playing && s_enabled) pe_re_save_encode(&s_session, 1, s_saved);
        return 0;
    }
    s_session.talking = pe_re_box_busy;
    if (pe_re_session_tick(&s_session) == PE_RE_SAID) say(s_session.said);   /* the room's scripts, a frame */
    const int cross = (~(unsigned)g_PadPackets[0].buttons & PAD_CROSS) != 0;
    const int pressed = cross && !s_cross_held && !pe_re_box_busy();   /* a box's Cross is the box's */
    s_cross_held = cross;
    double x, z;
    int facing;
    char why[200] = "";
    pe_re_stage_player(&x, &z, &facing);
    s_session.facing = facing, s_session.floor = pe_re_stage_floor();
    switch (pe_re_session_step(&s_session, s_store, x, z, pressed, why, sizeof why)) {
    case PE_RE_CAMERA: show_room(1); break;
    case PE_RE_ROOM:                             /* the door's transition puts the next room up at black */
        pe_re_door_begin(s_session.game->game, &s_session.door);
        snprintf(s_status, sizeof s_status, "Through the door to %x-%02X.%s%s", (unsigned)s_session.stage + 1,
                 (unsigned)s_session.room, s_session.said[0] ? " " : "", s_session.said);
        break;
    case PE_RE_SAID:
        say(s_session.said);
        snprintf(s_status, sizeof s_status, "%s", s_session.said);
        break;
    case PE_RE_STAY:
        if (*why) snprintf(s_status, sizeof s_status, "That door: %s.", why);
        break;
    }
    if (s_playing && s_enabled) pe_re_save_encode(&s_session, 1, s_saved);
    return 0;
}

/* Testing: stand in the middle of the room's next doorway, pickup or message zone (those its scripts
 * set up), one after another each time (the camera stays as it is). */
static void to_zone(uint32_t tag, const char *what) {
    static uint32_t s_next[3];
    static const PeImpRoomLogic *s_room;
    uint32_t n = 0, *next = &s_next[tag == PE_IMP_LOGIC_PICK ? 1 : tag == PE_IMP_LOGIC_TRIG ? 2 : 0];
    double bx = 0, bz = 0;
    int found = 0;
    if (!s_playing || !pe_imp_logic_section(s_session.logic, tag, &n, NULL)) {
        snprintf(s_status, sizeof s_status, "This room has no %ss.", what);
        return;
    }
    if (s_room != s_session.logic) s_room = s_session.logic, s_next[0] = s_next[1] = s_next[2] = 0;
    for (uint32_t k = 0; k < n && !found; k++) {
        const uint32_t i = (*next + k) % n;
        PeImpDoor d;
        PeImpPickup pk;
        PeImpTrigger t;
        const PeImpZone *zone = &d.zone;
        if (tag == PE_IMP_LOGIC_PICK) {
            if (pe_imp_logic_pickup(s_session.logic, i, &pk) || !pe_re_scd_live(&s_session.run, pk.at) ||
                pe_re_flag(&s_session.progress.flags, PE_RE_FG_ITEM, pk.flag))
                continue;
            zone = &pk.zone;
        } else if (tag == PE_IMP_LOGIC_TRIG) {
            if (pe_imp_logic_trigger(s_session.logic, i, &t) || !pe_re_scd_live(&s_session.run, t.at) ||
                pe_re_zone_kind(&s_session.run, t.zone.id, t.zone.sce, NULL) != PE_RE_SCE_MESSAGE)
                continue;
            zone = &t.zone;
        } else if (pe_imp_logic_door(s_session.logic, i, &d) || !pe_re_scd_live(&s_session.run, d.at) ||
                   pe_re_zone_kind(&s_session.run, d.zone.id, d.zone.sce, NULL) != d.zone.sce) {
            continue;
        }
        for (int c = 0; c < 4; c++) bx += zone->x[c] / 4.0, bz += zone->z[c] / 4.0;
        found = 1, *next = i + 1;
    }
    if (!found) { snprintf(s_status, sizeof s_status, "This room has no %ss.", what); return; }
    pe_re_stage_place(bx, bz);
    s_session.x = bx, s_session.z = bz;
    snprintf(s_status, sizeof s_status, "At a %s in room %x-%02X: press Cross.", what, (unsigned)s_session.stage + 1,
             (unsigned)s_session.room);
}
static void to_door(void) { to_zone(PE_IMP_LOGIC_DOOR, "door"); }
static void to_pickup(void) { to_zone(PE_IMP_LOGIC_PICK, "pickup"); }
static void to_message(void) { to_zone(PE_IMP_LOGIC_TRIG, "message"); }
static void to_ramp(void) {
    if (!s_playing) return;
    if (!pe_re_stage_to_ramp()) { snprintf(s_status, sizeof s_status, "This room has no slopes or stairs."); return; }
    pe_re_stage_player(&s_session.x, &s_session.z, &s_session.facing);
    s_session.floor = pe_re_stage_floor();
    snprintf(s_status, sizeof s_status, "At the foot of a slope or stairs: walk onto it.");
}
static void to_room(void) {
    char why[200] = "";
    if (!s_playing) { snprintf(s_status, sizeof s_status, "Start a game first."); return; }
    if (pe_re_session_enter(&s_session, s_store, (uint32_t)(s_jump_stage - 1), (uint32_t)s_jump_room, 0, 0, 0, 0, why,
                            sizeof why)) {
        snprintf(s_status, sizeof s_status, "Can't go there: %s.", why);
        return;
    }
    show_room(0);
}

static void refresh(void) { s_listed = 0; list_games(); }
static void text_scripts(char *out, unsigned size) {
    const PeReScdRun *r = &s_session.run;
    if (!s_playing || !r->ran) { snprintf(out, size, s_playing ? "Not run (every zone counts)." : "-"); return; }
    snprintf(out, size, "%d tasks running; %d instructions, %d skipped (first 0x%02X), %d over budget; %d zones, %d changed.",
             r->waiting, r->executed, r->skipped, r->skipped ? r->first_skipped : 0, r->stopped, r->nlive, r->nreset);
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
                            s_session.z, s_session.floor, s_session.doors_taken, pe_re_stage_scale());
    else snprintf(out, size, "%s", s_status);
}

static int init(void) {
    if (pe_mod_path(s_store, sizeof s_store, "import/store")) return 1;
    pe_re_stage_init(MOD_ID, s_store);
    pe_re_box_init(MOD_ID);
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
    pe_menu_register_action("Testing", "Go to the next door", "Puts Aya in the room's doorways, one after another.", to_door);
    pe_menu_register_action("Testing", "Go to the next pickup", "Puts Aya on the room's items, one after another.", to_pickup);
    pe_menu_register_action("Testing", "Go to the next ramp", "Puts Aya at the foot of the room's slopes and stairs, one after another.", to_ramp);
    pe_menu_register_action("Testing", "Go to the next message", "Puts Aya where the room's messages are read, one after another.", to_message);
    pe_menu_register_int("Testing", "Stage", "The room to jump to: its stage.", &s_jump_stage, 1, 7, 1);
    pe_menu_register_int("Testing", "Room", "The room to jump to: its number in the stage (1-05 is 5).", &s_jump_room, 0, 63, 1);
    pe_menu_register_action("Testing", "Go to that room", "Jumps there, camera 0, keeping what's been picked up.", to_room);
    pe_menu_register_action(0, "Refresh the list", "Looks for games imported since the game started.", refresh);
    pe_menu_register_info(0, "Status", "Where you are, or the last thing said.", text_status);
    pe_menu_register_info(0, "Pocket", "What you've picked up in this game.", text_pocket);
    pe_menu_register_info(0, "Scripts", "What this room's own scripts ran when you came in.", text_scripts);
    pe_hook_add(PE_HOOK_FRAME, on_frame, NULL, PE_PRIORITY_NORMAL, MOD_ID);
    s_registered = pe_extension_register(&s_section) >= 0;
    if (!s_registered) pe_mod_log(MOD_ID, "progress section not registered: imported games won't be saved");
    pe_hook_add(PE_HOOK_SAVE_READ, on_save_read, NULL, PE_PRIORITY_NORMAL, MOD_ID);
    pe_hook_add(PE_HOOK_NEW_GAME, on_new_game, NULL, PE_PRIORITY_NORMAL, MOD_ID);
    return 0;
}

static void shutdown_play(void) {
    stop();
    pe_re_stage_shutdown();
}

const PeMod pe_mod_pe_re_play = {MOD_ID, "Play imported games", "0.1", NULL, init, shutdown_play};
