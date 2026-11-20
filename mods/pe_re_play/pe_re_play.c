/* Play imported games: the room runtime (design doc "RE rooms in Parasite Eve", phase 4, the
 * playable slice). Only games imported through F1 > Import are offered; with none, the mod does
 * nothing. It reads only the import store, never a disc.
 *
 *   pe_re_session.c  the session: room, camera, position; camera switch zones and doors (no PE code)
 *   pe_re_stage.c    the room on the field screen: picture, masks, camera, walking with collision
 *   pe_re_play.c     the mod: F1 > Play imported games, and each frame's step
 *
 * The room is played inside whatever PE room Aya stands in (a shell room): she walks the imported
 * room's floor, cameras switch where its zones say, and Cross in a door's zone takes her through
 * to the next room and camera. Not yet: scripts (locks, keys, events), door animations, enemies
 * (reported to the encounter host later), saving. RE2 only for now: RE1 and RE3 rooms need their
 * cameras and collision drawn. PE loading another room of its own ends the session. */
#include <stdio.h>
#include <string.h>

#include "pe_mod_host.h"
#include "pe_mod_menu.h"
#include "pe_hooks.h"
#include "pe1/pad_state.h"
#include "pe_re_session.h"
#include "pe_re_stage.h"

#define MOD_ID "pe_re_play"
#define PAGE "Play imported games"
#define PAD_CROSS 0x4000u                 /* PS1 pad word, active high once inverted */

static char s_store[1100];
static const PeReGame *s_offer[PE_RE_GAMES];
static int s_offers, s_pick, s_player, s_listed, s_masks = 1;
static PeReSession s_session;
static int s_playing, s_cross_held;
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

static void show_room(int place) {
    char room[PE_IMP_ID_MAX], why[200] = "";
    pe_re_room_id(s_session.game->game, s_session.stage, s_session.room, room, sizeof room);
    if (pe_re_stage_show(room, s_session.logic, s_session.camera, place, s_session.x, s_session.z,
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

static void stop(void) {
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
    pe_re_stage_set_masks(s_masks);
    show_room(0);
}

static int on_frame(void *ctx, void *user) {
    (void)ctx, (void)user;
    if (!s_playing) return 0;
    if (!pe_re_stage_active()) {                /* PE moved to a room of its own */
        pe_re_session_end(&s_session);
        s_playing = 0;
        snprintf(s_status, sizeof s_status, "Stopped: Parasite Eve loaded another room.");
        return 0;
    }
    pe_re_stage_set_masks(s_masks);
    const int cross = (~(unsigned)g_PadPackets[0].buttons & PAD_CROSS) != 0;
    const int pressed = cross && !s_cross_held;
    s_cross_held = cross;
    double x, z;
    int facing;
    char why[200] = "";
    pe_re_stage_player(&x, &z, &facing);
    s_session.facing = facing;
    switch (pe_re_session_step(&s_session, s_store, x, z, pressed, why, sizeof why)) {
    case PE_RE_CAMERA: show_room(1); break;
    case PE_RE_ROOM: show_room(1); break;
    case PE_RE_STAY:
        if (*why) snprintf(s_status, sizeof s_status, "That door: %s.", why);
        break;
    }
    return 0;
}

static void refresh(void) { s_listed = 0; list_games(); }
static void text_status(char *out, unsigned size) {
    if (s_playing) snprintf(out, size, "%s x %.0f, z %.0f, doors %d.", s_status, s_session.x, s_session.z, s_session.doors_taken);
    else snprintf(out, size, "%s", s_status);
}

static int init(void) {
    if (pe_mod_path(s_store, sizeof s_store, "import/store")) return 1;
    pe_re_stage_init(MOD_ID, s_store);
    pe_menu_begin(PAGE);
    pe_menu_top_page(PAGE, "Walk the rooms of a game you imported, as Aya.", 63);
    pe_menu_register_dynamic_choice(0, "Game", "A game imported from your own disc (F1 > Import).", &s_pick,
                                    PE_RE_GAMES, game_count, game_name);
    pe_menu_register_int(0, "Scenario", "RE2: 0 Leon, 1 Claire.", &s_player, 0, 1, 1);
    pe_menu_register_toggle(0, "Foreground", "Let the room's foreground cover Aya.", &s_masks, NULL);
    pe_menu_register_action(0, "Start", "Starts in the game's first room. Walk with the pad; Cross in a doorway goes through.", start);
    pe_menu_register_action(0, "Stop", "Back to the Parasite Eve room and camera.", stop);
    pe_menu_register_action(0, "Refresh the list", "Looks for games imported since the game started.", refresh);
    pe_menu_register_info(0, "Status", "Where you are.", text_status);
    pe_hook_add(PE_HOOK_FRAME, on_frame, NULL, PE_PRIORITY_NORMAL, MOD_ID);
    return 0;
}

static void shutdown_play(void) {
    stop();
    pe_re_stage_shutdown();
}

const PeMod pe_mod_pe_re_play = {MOD_ID, "Play imported games", "0.1", NULL, init, shutdown_play};
