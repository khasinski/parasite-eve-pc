/* A second host for the production RE play mod. It records presentation and lets the test
 * drive input and lifecycle events without the game, renderer or platform. */
#include <stdio.h>
#include <string.h>

#include "pe_extension_section.h"
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

extern const PeMod pe_mod_pe_re_play;

static struct {
    char room[PE_IMP_ID_MAX], text[200];
    double x, z;
    int facing, camera, floor, active, shade, curtain, hold, backdrop;
    unsigned rooms, placements, saves;
    int choice, busy, menu;
    uint16_t buttons;
    ReHostCallback frame, save_read, new_game, save_committed;
    PeMenuAction start, next_door, jump, typewriter, give_ribbon;
    int *scenario, *stage, *room_number;
    const PeExtensionSection *progress;
    const PeRePlayApi *api;
    const char *store;
} s;

static void noop(void) {}
static void setting(int value) { (void)value; }
static int unavailable(void) { return 0; }
static int blocked(double x, double z) { (void)x; (void)z; return 0; }
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
    };
    return &host;
}

/* The shared mod-host services the production mod uses. The registered callbacks and save bytes
 * let the test exercise the same paths as the game menu, encounter service and companion store. */
void pe_menu_begin(const char *page) { (void)page; }
int pe_menu_top_page(const char *page, const char *desc, int order) { (void)page; (void)desc; (void)order; return 0; }
int pe_menu_register_action(const char *sub, const char *label, const char *desc, PeMenuAction action) {
    (void)sub; (void)desc;
    if (!strcmp(label, "Start")) s.start = action;
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
    if (!strcmp(name, PE_EVENT_SAVE_COMMITTED)) return 1;
    return !strcmp(name, RE_PLAY_EVENT_ROOM) ? 2 : 3;
}
int pe_hook_add(int id, PeHookFn fn, void *user, int priority, const char *mod_id) {
    (void)user; (void)priority; (void)mod_id;
    if (id != 1) return -1;
    s.save_committed = fn;
    return 0;
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
int pe_extension_register(const PeExtensionSection *section) { s.progress = section; return 0; }
PeRegResult pe_extension_enable(const PeExtensionSection *section) {
    (void)section;
    return PE_REG_OK;
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
    int pick;
    {   /* a trip from here fights the room's own creature: RE2's kind -> model table picks it */
        char asset[96], want[96];
        if (pe_imp_enemy_kind_asset("re2", 0x10, NULL, 0))                /* no game's table yet: the player's pick */
            EXPECT(pe_trip_creature(&room, asset, sizeof asset) == -1);
        EXPECT(!pe_re2_enemies_register());
        pick = pe_trip_creature(&room, asset, sizeof asset);
        EXPECT(pick >= 0 && pick < room.count && room.enemies[pick].kind < 0x40);
        snprintf(want, sizeof want, "re2:enemy/em%02x", (unsigned)room.enemies[pick].kind);
        EXPECT(!strcmp(asset, want));
        int life = 0;
        EXPECT(!pe_imp_enemy_strength(asset, &life, NULL) && life > 0);   /* and the game's own strength */
        printf("headless host: a trip from the street fights its %s (placement %d, life %d)\n", asset, pick, life);
    }
    PeTripResult trip;                                     /* a battle trip from here, won (pe_enemy_import) */
    const int placed = room.count;
    const uint8_t flag = room.enemies[pick].killed_flag;
    EXPECT(flag != 0xFF);
    int shared = 0;
    for (int i = 0; i < room.count; i++) shared += room.enemies[i].killed_flag == flag;
    EXPECT(pe_trip_arm(&trip, &room, pick));
    EXPECT(s.api->pause_for_battle() == 1 && s.backdrop && !s.active);
    pe_trip_won(&trip, 1);
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
        EXPECT(pe_trip_arm(&lost, &room, 0));
        pe_trip_won(&lost, 0);
        EXPECT(pe_trip_report(&lost, s.api) == -1 && !lost.armed);
    }
    PeReSaved saved;
    EXPECT(!pe_re_save_decode(s.progress->data, &saved) && saved.playing && saved.player == scenario);
    s.save_read(NULL, NULL);
    EXPECT(!s.active);
    frames(1, 0);
    EXPECT(s.active && !strcmp(s.room, "re2:room/100"));

    EXPECT(s.next_door);
    s.next_door();
    frames(1, RE_HOST_CROSS);
    EXPECT(s.hold);
    frames(75, 0);
    EXPECT(!s.hold && s.shade == 128 && strcmp(s.room, "re2:room/100"));
    if (scenario) return 0;

    EXPECT(s.jump && s.stage && s.room_number && s.give_ribbon && s.typewriter && s.save_committed);
    *s.stage = 2;
    *s.room_number = 8;
    s.jump();
    frames(30, 0);
    s.give_ribbon();
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
    EXPECT(!pe_re_save_decode(s.progress->data, &saved) && !pe_re_has(&saved.progress, PE_RE2_INK_RIBBON));
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
    return result;
}
