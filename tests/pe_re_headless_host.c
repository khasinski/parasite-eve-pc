/* A second host for the production RE play mod. It records presentation and lets the test
 * drive input and lifecycle events without the game, renderer or platform. */
#include <stdio.h>
#include <string.h>

#include "pe_mod_api.h"
#include "pe_mod_menu.h"
#include "pe_re_door.h"
#include "pe_re_save.h"
#include "pe_imp_enemy.h"
#include "pe_services.h"
#include "pe_src_re2.h"
#include "pe_trip_result.h"
#include "re_play_api.h"
#include "re_host.h"
#include "pe_re_play_runtime.h"

extern const PeMod pe_mod_pe_re_play;

static struct {
    char room[PE_IMP_ID_MAX], text[200];
    double x, z;
    int facing, camera, floor, active, shade, curtain, hold, backdrop;
    unsigned rooms, placements, saves;
    int choice, busy, menu, test_wall;
    uint16_t buttons;
    ReHostCallback frame, save_read, new_game, save_committed;
    PeMenuAction start, title, next_door, jump, typewriter, give_ribbon;
    int *scenario, *stage, *room_number;
    const ReHostProgress *progress;
    const PeRePlayApi *api;
    const char *store;
} s;

static void noop(void) {}
static void setting(int value) { (void)value; }
static int unavailable(void) { return 0; }
static int blocked(double x, double z) { (void)z; return s.test_wall && x < -50 && x > -400; }
static void init(const char *mod_id, const char *store) { (void)mod_id; (void)store; }
static void place(double x, double z) { s.x = x; s.z = z; }
static void face(int facing) { s.facing = facing; }
static int floor_now(void) { return s.floor; }
static double scale(void) { return 1; }
static int active(void) { return s.active; }
static int field_free(void) { return !s.menu && !s.busy; }
static int menu_active(void) { return s.menu; }
static void open_save(void) { s.menu = 1; s.saves++; }
static uint16_t buttons(void) { return s.buttons; }

static int show_room(const char *id, const PeImpRoomLogic *logic, int camera, int floor, int use_pose,
                     double x, double z, int facing, char *why, size_t n) {
    if (!logic) {
        snprintf(why, n, "Missing room logic");
        return -1;
    }
    snprintf(s.room, sizeof s.room, "%s", id);
    s.active = 1;
    s.rooms++;
    s.camera = camera;
    s.floor = floor;
    place(use_pose ? x : 0, use_pose ? z : 0);
    face(use_pose ? facing : 0);
    return 0;
}

static void hide(void) { s.active = 0; }
static void fade(int shade, int curtain, int hold) { s.shade = shade; s.curtain = curtain; s.hold = hold; }
static void pose(double *x, double *z, int *facing) { *x = s.x; *z = s.z; *facing = s.facing; }
static void placements(const struct PeReEnemyPlacement *list, int count) {
    (void)list;
    s.placements = (unsigned)count;
}
static int say(const char *text) {
    if (s.busy) return 0;
    snprintf(s.text, sizeof s.text, "%s", text);
    s.busy = 1;
    return 1;
}
static int choose(const char *title, const char *const labels[], int count, int at) {
    return count > 0 && at >= 0 && at < count && labels[at] && say(title);
}
static int choice(void) { const int result = s.choice; s.choice = -1; return result; }
static int busy(void) { return s.busy; }
static void forget(void) { s.busy = 0; s.choice = -1; }
static int arm(void) { return s.backdrop = s.active; }
static void release(void) { s.backdrop = 0; }
static void bind(const char *mod_id, ReHostCallback frame, ReHostCallback save_read, ReHostCallback new_game) {
    (void)mod_id;
    s.frame = frame;
    s.save_read = save_read;
    s.new_game = new_game;
}

static int register_progress(const ReHostProgress *progress, ReHostCallback committed)
{
    s.progress = progress;
    s.save_committed = committed;
    return 0;
}

static int enable_progress(void) { return 0; }

static int s_exploration_only;

const ReHost *re_host(void) {
    static const ReHost host = {
        .struct_size = sizeof host, .init = init, .shutdown = noop, .show_room = show_room,
        .hide_room = hide, .room_active = active, .room_fade = fade, .player_pose = pose,
        .player_floor = floor_now, .place_player = place, .face_player = face, .position_blocked = blocked,
        .place_at_ramp = unavailable, .set_masks = setting, .set_player_size = setting, .player_scale = scale,
        .set_placement_markers = setting, .set_placement_meshes = setting, .set_placement_idle = setting,
        .set_placements = placements, .say = say, .choose = choose, .dialogue_choice = choice,
        .dialogue_busy = busy, .forget_dialogue = forget, .field_free = field_free, .held_buttons = buttons,
        .consume_native_interact = noop, .open_save_screen = open_save, .native_menu_active = menu_active,
        .arm_backdrop = arm, .release_backdrop = release, .bind_lifecycle = bind,
        .register_progress = register_progress, .enable_progress = enable_progress,
    };
    if (!s_exploration_only) return &host;
    static ReHost exploration;
    exploration = host;
    exploration.open_save_screen = NULL;
    exploration.arm_backdrop = NULL;
    exploration.release_backdrop = NULL;
    exploration.register_progress = NULL;
    exploration.enable_progress = NULL;
    return &exploration;
}

/* The shared mod-host services the production mod uses. The registered callbacks and save bytes
 * let the test exercise the same paths as the game menu, encounter service and companion store. */
void pe_mod_panel_close(void) {}
void pe_menu_begin(const char *page) { (void)page; }
int pe_menu_top_page(const char *page, const char *desc, int order) { (void)page; (void)desc; (void)order; return 0; }
int pe_menu_register_action(const char *sub, const char *label, const char *desc, PeMenuAction action) {
    (void)sub; (void)desc;
    if (!strcmp(label, "Start")) s.start = action;
    if (!strcmp(label, "RE2 title menu")) s.title = action;
    if (!strcmp(label, "Go to the next door")) s.next_door = action;
    if (!strcmp(label, "Go to that room")) s.jump = action;
    if (!strcmp(label, "Go to the next typewriter")) s.typewriter = action;
    if (!strcmp(label, "Give an Ink Ribbon")) s.give_ribbon = action;
    return 0;
}
int pe_menu_register_int(const char *sub, const char *label, const char *desc,
                         int *value, int min, int max, int step) {
    (void)sub; (void)desc; (void)min; (void)max; (void)step;
    if (!strcmp(label, "Scenario")) s.scenario = value;
    if (!strcmp(label, "Stage")) s.stage = value;
    if (!strcmp(label, "Room")) s.room_number = value;
    return 0;
}
int pe_menu_register_toggle(const char *sub, const char *label, const char *desc, int *value, PeMenuPerFrame fn) {
    (void)sub; (void)label; (void)desc; (void)value; (void)fn; return 0;
}
int pe_menu_register_info(const char *sub, const char *label, const char *desc, PeMenuText fn) {
    (void)sub; (void)label; (void)desc; (void)fn; return 0;
}
int pe_menu_register_dynamic_choice(const char *sub, const char *label, const char *desc, int *value,
                                    unsigned max, PeMenuChoiceCount count, PeMenuChoiceName name) {
    (void)sub; (void)label; (void)desc; (void)value; (void)max; (void)count; (void)name; return 0;
}
int pe_mod_path(char *out, size_t n, const char *file) { (void)file; return snprintf(out, n, "%s", s.store) >= (int)n; }
void pe_mod_log(const char *mod_id, const char *fmt, ...) { (void)mod_id; (void)fmt; }
void pe_toast(const char *text) { (void)text; }
int pe_event_id(const char *name) {
    return !strcmp(name, RE_PLAY_EVENT_ROOM) ? 2 : 3;
}
int pe_event_emit(int id, void *ctx) { (void)id; (void)ctx; return 0; }
int pe_service_publish(const char *name, int version, const void *api, const char *mod_id) {
    (void)mod_id;
    if (strcmp(name, RE_PLAY_SERVICE) || version != RE_PLAY_SERVICE_VERSION) return -1;
    s.api = api;
    return 0;
}
const void *pe_service_get(const char *name, int version) {
    return version == RE_PLAY_SERVICE_VERSION && !strcmp(name, RE_PLAY_SERVICE) ? s.api : NULL;
}
static void frames(unsigned count, uint16_t held) {
    s.buttons = held;
    for (unsigned i = 0; i < count; i++) s.frame(NULL, NULL);
}

#define EXPECT(expr) do { if (!(expr)) { printf("Headless host failed: %s\n", #expr); return -1; } } while (0)

static int play(unsigned scenario) {
    PeReRoomEvent room;
    EXPECT(s.start && s.frame && s.save_read && s.new_game && s.scenario && s.api && s.progress);
    *s.scenario = (int)scenario;
    s.start();
    frames(1, 0);
    EXPECT(s.active && !strcmp(s.room, "re2:room/100") && !s.api->room(&room));
    EXPECT(s.title);
    s.title();
    frames(1, 0);
    EXPECT(s.busy && !s.active && !strcmp(s.text, "Resident Evil 2"));
    s.choice = 4;  /* Continue current save */
    s.busy = 0;
    frames(3, 0);   /* the choice, the field free again, the room */
    EXPECT(s.active && !s.api->room(&room) && room.room == 0);
    EXPECT(s.api->enemy_archive && !strcmp(s.api->enemy_archive(), scenario ? "cdemd1" : "cdemd0"));
    PeTripResult trip;
    {   /* a trip from here fights the room's own creature: RE2's kind -> model table picks it */
        char asset[96];
        if (pe_imp_enemy_kind_asset("re2", 0x10, NULL, 0))                /* no game's table yet: the player's pick */
            EXPECT(!pe_trip_arm(&trip, &room, NULL, NULL));
        EXPECT(!pe_re2_enemies_register());
        const PeReEnemyPlacement actors[] = {
            {.kind = 0x2c, .slot = 1}, {.kind = 0x3e, .slot = 2},
            {.kind = 0x3f, .slot = 3}, {.kind = 0x10, .slot = 4}
        };
        PeReRoomEvent mixed = room;
        mixed.count = sizeof actors / sizeof actors[0];
        mixed.enemies = actors;
        EXPECT(pe_trip_arm(&trip, &mixed, NULL, NULL) == 1 && trip.slot[0] == 4);
        EXPECT(room.count > 0);
        EXPECT(!pe_imp_enemy_kind_asset("re2", room.enemies[0].kind, asset, sizeof asset));
        int life = 0;
        EXPECT(!pe_imp_enemy_strength(asset, &life, NULL) && life > 0);   /* and the game's own strength */
        printf("headless host: a trip from the street fights its %s (life %d)\n", asset, life);
    }
    {
        const PeReEnemyPlacement placements[] = {
            {.kind = 0x22, .slot = 0, .behaviour = 0x4402, .killed_flag = 0x18},
            {.kind = 0x27, .slot = 1, .behaviour = 1, .killed_flag = 0xff},
            {.kind = 0x2d, .slot = 2, .killed_flag = 0xff},
            {.kind = 0x29, .slot = 3, .killed_flag = 0xff},
            {.kind = 0x30, .slot = 4, .killed_flag = 0xff},
            {.kind = 0x37, .slot = 5, .killed_flag = 0xff},
            {.kind = 0x27, .slot = 6, .behaviour = 3, .killed_flag = 0x81},
        };
        PeReRoomEvent mixed = {sizeof mixed, "re2", 1, 1, 0, sizeof placements / sizeof placements[0], placements};
        EXPECT(pe_trip_arm(&trip, &mixed, NULL, NULL) == 1 && trip.slot[0] == 6);
    }
    const int placed = room.count;
    const uint8_t flag = room.enemies[0].killed_flag;
    EXPECT(flag != 0xFF);
    int shared = 0;
    for (int i = 0; i < room.count; i++) shared += room.enemies[i].killed_flag == flag;
    EXPECT(pe_trip_arm(&trip, &room, NULL, NULL) > 1 && trip.slot[0] == room.enemies[0].slot);
    EXPECT(s.api->pause_for_battle() == 1 && s.backdrop && !s.active);
    pe_trip_won(&trip, 1);                                 /* a failure to spawn the rest must preserve their slots */
    EXPECT(pe_trip_report(&trip, s.api) == 0 && trip.armed);   /* paused: not yet */
    s.api->resume_after_battle();
    frames(2, 0);
    EXPECT(!s.backdrop && s.active && !s.api->room(&room));
    EXPECT(pe_trip_report(&trip, s.api) == 1 && !trip.armed);
    frames(1, 0);
    EXPECT(!s.api->room(&room) && room.count == placed - shared && room.count > 0);
    for (int i = 0; i < room.count; i++) EXPECT(room.enemies[i].killed_flag != flag);
    {   /* an escape or a loss reports nothing */
        PeTripResult lost;
        EXPECT(pe_trip_arm(&lost, &room, NULL, NULL) > 0);
        pe_trip_won(&lost, 0);
        EXPECT(pe_trip_report(&lost, s.api) == -1 && !lost.armed);
    }
    {   /* An approach picks the closest living zombie first, even if it was placed later. */
        PeReEnemyPlacement nearby[PE_TRIP_FOES_MAX];
        const PeReEnemyPlacement lead = room.enemies[room.count - 1];
        double x, z;
        int floor;
        EXPECT(!s.api->encounter_position(&x, &z, &floor));
        EXPECT(!pe_trip_approach(&room, 100000, 100000, lead.floor, NULL, nearby));
        EXPECT(!pe_trip_approach(&room, lead.x, lead.z, lead.floor + 1, NULL, nearby));
        const int selected = pe_trip_approach(&room, lead.x, lead.z, lead.floor, NULL, nearby);
        EXPECT(selected > 0 && nearby[0].slot == lead.slot);
        /* Escaping after a non-prefix kill leaves the other slots as they were. */
        PeTripResult escaped;
        EXPECT(pe_trip_arm(&escaped, &room, NULL, NULL) > 1);
        const unsigned survivor_slot = escaped.slot[0], defeated_slot = escaped.slot[1];
        pe_trip_defeated(&escaped, 2u);
        EXPECT(pe_trip_report(&escaped, s.api) == 1);
        frames(2, 0);
        EXPECT(!s.api->room(&room));
        int survivor_found = 0;
        for (int i = 0; i < room.count; i++) {
            EXPECT(room.enemies[i].slot != defeated_slot);
            survivor_found |= room.enemies[i].slot == survivor_slot;
        }
        EXPECT(survivor_found);
    }
    const int remaining = room.count;
    PeReSaved saved;
    EXPECT(!pe_re_save_decode(s.progress->bytes, &saved) && saved.playing && saved.player == scenario);
    s.save_read(NULL, NULL);
    EXPECT(!s.active);
    frames(1, 0);
    EXPECT(s.active && !strcmp(s.room, "re2:room/100"));
    EXPECT(!s.api->room(&room) && room.count == remaining);
    {
        const PeReEnemyPlacement actors[] = {
            {.kind = 0x1f, .slot = 0, .x = -500, .killed_flag = 1},
            {.kind = 0x1f, .slot = 1, .x = 600, .killed_flag = 2},
            {.kind = 0x1f, .slot = 2, .x = 200, .behaviour = 0x4002, .killed_flag = 3},
            {.kind = 0x22, .slot = 3, .x = 300, .behaviour = 0x4402, .killed_flag = 4},
            {.kind = 0x1f, .slot = 4, .x = 400, .floor = 1, .killed_flag = 5},
            {.kind = 0x20, .slot = 5, .x = 2200, .killed_flag = 6},
            {.kind = 0x1f, .slot = 6, .x = 100, .killed_flag = 0xff},
            {.kind = 0x3b, .slot = 7, .x = 3100, .behaviour = 0x2008, .killed_flag = 7}
        };
        PeReEnemyPlacement nearby[PE_TRIP_FOES_MAX];
        PeReRoomEvent rpd = {sizeof rpd, "re2", 1, 9, 1, sizeof actors / sizeof actors[0], actors};
        EXPECT(!pe_trip_approach(&rpd, 0, 0, 0, NULL, nearby));
        s.test_wall = 1;
        EXPECT(!s.api->encounter_reachable(0, 0, -500, 0));
        EXPECT(pe_trip_approach(&rpd, 0, 0, 0, s.api->encounter_reachable, nearby) == 3);
        EXPECT(nearby[0].slot == 1 && nearby[1].slot == 5 && nearby[2].slot == 7);
        s.test_wall = 0;
        rpd.stage = 2; rpd.room = 5;
        EXPECT(pe_trip_approach(&rpd, 0, 0, 0, s.api->encounter_reachable, nearby) == PE_TRIP_FOES_MAX);
        EXPECT(nearby[0].slot == 0 && nearby[1].slot == 1 && nearby[2].slot == 5);
    }

    EXPECT(s.next_door);
    s.next_door();
    frames(1, RE_HOST_CROSS);
    EXPECT(s.hold);
    frames(75, 0);
    EXPECT(!s.hold && s.shade == 128 && strcmp(s.room, "re2:room/100"));

    EXPECT(s.jump && s.stage && s.room_number);
    *s.stage = 2;
    *s.room_number = 9;
    s.jump();
    frames(2, 0);
    EXPECT(!s.api->room(&room) && room.stage == 1 && room.room == 9 && room.count == 6);
    const PeReEnemyPlacement anchor = room.enemies[0];
    PeReEnemyPlacement nearby[PE_TRIP_FOES_MAX];
    PeReRoomEvent group = room;
    group.count = pe_trip_approach(&room, anchor.x, anchor.z, anchor.floor, s.api->encounter_reachable, nearby);
    group.enemies = nearby;
    EXPECT(group.count >= 1 && group.count <= PE_TRIP_FOES_MAX && nearby[0].slot == anchor.slot);
    EXPECT(pe_trip_arm(&trip, &group, NULL, NULL) == group.count);
    pe_trip_won(&trip, group.count);
    EXPECT(pe_trip_report(&trip, s.api) == group.count);
    frames(2, 0);
    EXPECT(!s.api->room(&room) && room.count == 6 - group.count);
    s.save_read(NULL, NULL);
    frames(1, 0);
    EXPECT(!s.api->room(&room) && room.stage == 1 && room.room == 9 && room.count == 6 - group.count);
    if (scenario) return 0;

    EXPECT(s.jump && s.stage && s.room_number && s.give_ribbon && s.typewriter && s.save_committed);
    *s.stage = 2;
    *s.room_number = 8;
    s.jump();
    frames(30, 0);
    s.give_ribbon();
    /* Start: RE2's status screen, the pocket, then what to do with the item (Check says what it is) */
    frames(1, RE_HOST_START);
    EXPECT(s.busy && !strncmp(s.text, "Status: Leon  1/8", 17));
    s.busy = 0, s.choice = 0;
    frames(2, 0);
    EXPECT(s.busy && strstr(s.text, "Ink Ribbon"));
    s.busy = 0, s.choice = 2;                                 /* Check */
    frames(2, 0);
    EXPECT(s.busy && strstr(s.text, "Ink Ribbon") && s.text[strlen(s.text) - 1] == '.');
    s.busy = 0;
    /* Select: the pause menu; Resume leaves her playing */
    frames(1, RE_HOST_SELECT);
    EXPECT(s.busy && !strcmp(s.text, "Paused"));
    s.busy = 0, s.choice = 0;
    frames(2, 0);
    EXPECT(s.active && !s.busy);
    s.typewriter();
    frames(1, RE_HOST_CROSS);
    EXPECT(s.busy && !strcmp(s.text, "Use an Ink Ribbon to save?"));
    s.busy = 0;
    s.choice = 0;
    frames(2, 0);
    EXPECT(s.menu && s.saves == 1);
    s.save_committed(NULL, NULL);
    s.menu = 0;
    frames(1, 0);
    EXPECT(!pe_re_save_decode(s.progress->bytes, &saved) && !pe_re_has(&saved.progress, PE_RE2_INK_RIBBON));
    return 0;
}

int re_headless_smoke(const char *store, unsigned scenario) {
    memset(&s, 0, sizeof s);
    EXPECT(re_host()->struct_size == sizeof(ReHost));
    if (!store) {
        const PeImpDoor door = {0};
        pe_re_door_begin("re2", &door);
        EXPECT(s.hold && s.shade == 128);
        unsigned swaps = 0;
        while (pe_re_door_busy()) {
            if (pe_re_door_step() == PE_RE_DOOR_SWAP) {
                EXPECT(s.curtain && s.hold && !s.shade);
                swaps++;
            }
        }
        EXPECT(swaps == 1 && !s.hold && !s.curtain && s.shade == 128);
        return 0;
    }
    s.store = store;
    EXPECT(!pe_mod_pe_re_play.init());
    const int result = play(scenario);
    pe_mod_pe_re_play.shutdown();
    if (result) return result;

    memset(&s, 0, sizeof s);
    s.store = store;
    s_exploration_only = 1;
    const int init_result = pe_re_play_init(store);
    int explored = 0;
    if (!init_result) {
        const PeRePlayControls *controls = pe_re_play_controls();
        *controls->player = (int)scenario;
        controls->start();
        frames(2, 0);
        explored = s.active && !s.start && !s.progress && !s.saves && !strcmp(s.room, "re2:room/100");
        controls->open_title();
        frames(1, 0);
        explored = explored && s.busy && !s.active;
        s.choice = 2 + (int)scenario;  /* select the B scenario on the imported disc */
        s.busy = 0;
        frames(3, 0);
        explored = explored && s.active && !strcmp(s.room, "re2:room/104");
        pe_re_play_shutdown();
    }
    s_exploration_only = 0;
    EXPECT(explored);
    return 0;
}
