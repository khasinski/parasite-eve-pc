/* Enemy import, step 1: the fight picker. F1 > Fights > pick an area and a fight > Start fight.
 *
 * Parasite Eve keeps a room's enemies (models, stats, behaviour) in that room's own data, and its
 * random battles are the room script's: Aya walks into a floor zone, a roll succeeds and the
 * script starts the battle. A picked fight is that room's own battle, started on purpose:
 *   1. the field script's room transfer to the fight's room (as pe_warp), with the story
 *      variable set to a point where the room has its random battles;
 *   2. Aya stands in the spot's floor zone (found in the loaded room script, pe_encounter.h);
 *   3. that spot's encounter roll is made to succeed, once; the room's own battle follows;
 *   4. once the fight is over (won or run from) and the field is free again, the script
 *      variables the trip changed go back as they were and Aya returns to where she stood.
 * Losing is the game's own Game Over, as in any fight; the picker forgets the trip then.
 * Rewards (EXP, items, BP) won in a picked fight are kept. */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "pe_encounter.h"
#include "pe_enemy_battle.h"
#include "pe_enemy_build.h"
#include "pe_imp_guess.h"
#include "pe_imp_fit.h"
#include <SDL3/SDL.h>
#include "pe_game.h"
#include "pe_mod_host.h"
#include "pe_mod_menu.h"
#include "pe_services.h"
#include "pe_trip_result.h"
#include "re_play_api.h"
#include "pe1/asset_table.h"
#include "pe1/battle_runtime.h"
#include "pe1/field_actor.h"
#include "pe1/field_collision.h"
#include "pe1/game_state.h"
#include "pe1/game_timers.h"
#include "pe1/player_entity.h"
#include "pe1/scene_dispatch.h"
#include "pe1/scene_room.h"
#include "pe1/script_queue.h"

#define MOD_ID "pe_enemy_import"
#define PE_PAGE_FIGHTS "Fights"
#define PE_PROFILE_PAGE "Enemy profile"
#define PE_VAR_STORY 74
#define PE_VAR_COUNT 512
#define PE_VAR_BATTLE_FLAGS 10
#define PE_SKIP_BATTLES 0x80000000u   /* the game's own "no random battles" bit */
#define PE_ROOM_TEMPLATE 0xA8000048u
#define PE_SETTLE_FRAMES 30           /* free field frames before acting in a room */
#define PE_START_FRAMES 600           /* a placed spot that hasn't started a battle by then won't */
#define PE_TRAVEL_FRAMES 1800         /* a room transfer that hasn't settled by then is stuck (an event) */
#define PE_ZONES_MAX 8
#define PE_TOKEN_TITLE 0xA9400048u    /* the title screens (pe_warp) */
#define PE_STREET_FIGHT 1              /* Day 1 Sewer, second spot: the native battle carrier */

typedef struct { const char *name; unsigned story; } PeArea;
typedef struct { unsigned char area; unsigned short map; unsigned char spot; const char *enemies; } PeFight;

/* Story variable per area: a point where its rooms have their random battles (the Warp mod's
 * checkpoints). Each fight below is checked on the real disc by the enemy-import-fights CTest. */
static const PeArea k_areas[] = {
    {"Day 1: Sewer", 0x68u},
    {"Day 2: Central Park", 0xE4u},
    {"Day 3: Police station", 0x178u},
    {"Day 4: Hospital", 0x1D0u},
};
#define PE_AREAS ((int)(sizeof k_areas / sizeof k_areas[0]))

/* map, spot (the room's random battles in script order, from 0), enemies (names from the
 * bestiary's table, told apart by HP). Only fights the enemy-import-fights-all CTest saw start and
 * come back on Disc 1. Left out for now: M111 (the room keeps Aya in a scene at this story point),
 * M126 (no battle starts), M116 and M129 (the battle stops on its victory step), M147 and M156
 * (the field never comes free after the fight). */
static const PeFight k_fights[] = {
    {0, 30, 0, "Rat x2"},
    {0, 30, 1, "Rat x2"},
    {1, 59, 0, "Green Snake, Red Snake"},
    {1, 59, 1, "Green Snake, Red Snake"},
    {1, 61, 0, "Blackbird x2"},
    {1, 63, 0, "Monkey"},
    {1, 63, 1, "Monkey"},
    {1, 65, 0, "Plant x2"},
    {1, 66, 0, "Red Snake x2"},
    {1, 66, 1, "Red Snake x2"},
    {1, 67, 0, "Blackbird x2, Red Snake"},
    {1, 67, 1, "Blackbird x2, Red Snake"},
    {1, 75, 0, "Bear"},
    {1, 78, 0, "Red Snake, Green Snake"},
    {1, 80, 0, "Bear, Blackbird"},
    {1, 81, 0, "Blackbird x4"},
    {1, 83, 0, "Monkey x2"},
    {1, 83, 1, "Monkey x2"},
    {1, 85, 0, "Plant x3"},
    {1, 85, 1, "Plant x3"},
    {2, 100, 0, "Blackbird, Yellow Rat"},
    {2, 100, 1, "Blackbird, Yellow Rat"},
    {2, 104, 0, "Spider x2"},
    {2, 107, 0, "Dogman, Blackbird"},
    {2, 110, 0, "Yellow Rat x2, Blackbird x2"},
    {2, 113, 0, "Yellow Rat, Spider x4"},
    {2, 115, 0, "Spider x6"},
    {2, 115, 1, "Spider x6"},
    {2, 118, 0, "Giant Dogman"},
    {2, 120, 0, "Blackbird, Spider x4"},
    {2, 121, 0, "Yellow Rat x2, Blackbird"},
    {2, 122, 0, "Dogman, Spider x2"},
    {2, 122, 1, "Dogman, Spider x2"},
    {3, 137, 0, "Slime x2, Fly"},
    {3, 139, 0, "Slime x2"},
    {3, 140, 0, "Fly x2, Slime"},
    {3, 141, 0, "Ratman, Slime x2"},
    {3, 145, 0, "Spawn x2"},
    {3, 146, 0, "Ratman, Fly"},
    {3, 149, 0, "Giant Slime"},
    {3, 152, 0, "Spawn x2, Slime x2"},
    {3, 153, 0, "Spawn x2, Ratman"},
    {3, 153, 1, "Spawn x2, Ratman"},
    {3, 154, 0, "Spawn x2, Ratman"},
};
#define PE_FIGHTS ((int)(sizeof k_fights / sizeof k_fights[0]))

enum { PE_IDLE, PE_LEAVE, PE_ARRIVE, PE_WAIT_BATTLE, PE_FIGHT, PE_AFTER, PE_HOME };
static const char *const k_state_names[] = {
    "Ready", "Leaving", "Going to the fight", "Waiting for the fight", "Fighting", "Fight over", "Going back",
};

static int s_area, s_pick, s_enemy, s_enemy_count, s_room_enemies;
static int s_street_encounters = 1;
static int s_street_latched;
static int16_t s_street_x, s_street_z;
static const char *const k_counts[] = {"1", "2", "3"};
static const char *const k_room_enemies[] = {"Fight too", "Sent away"};
static struct {
    int state, fight, frames, travel, same_room, live_enemies, saw_enemies, imported_room_paused, preview, automatic;
    unsigned origin, target;
    int x, z, yaw;
    uintptr_t roll_next;   /* the forced roll's PeHookScriptRandom.next */
    uint32_t vars[PE_VAR_COUNT];
} s;
static char s_last[96] = "none yet";
static PeTripResult s_trip;         /* the placement used by the current battle trip */
static int s_trip_wait;              /* frames left to see that room again */

/* ---- the list ---- */

static int pe_fight_index(int area, int pick) {
    int i, n = 0;
    for (i = 0; i < PE_FIGHTS; ++i)
        if (k_fights[i].area == area && n++ == pick) return i;
    return -1;
}
static unsigned pe_area_count(void) { return PE_AREAS; }
static const char *pe_area_name(unsigned i) { return i < (unsigned)PE_AREAS ? k_areas[i].name : ""; }
static unsigned pe_pick_count(void) {
    unsigned n = 0;
    for (int i = 0; i < PE_FIGHTS; ++i) n += k_fights[i].area == s_area;
    return n;
}
static const char *pe_fight_label(int i) {
    static char label[8][96];
    static int next;
    char *out = label[next++ & 7];
    const PeFight *f = &k_fights[i];
    int spots = 0;
    for (int k = 0; k < PE_FIGHTS; ++k) spots += k_fights[k].map == f->map;
    if (spots > 1) snprintf(out, sizeof label[0], "M%03u spot %u: %s", f->map, f->spot + 1u, f->enemies);
    else snprintf(out, sizeof label[0], "M%03u: %s", f->map, f->enemies);
    return out;
}
static const char *pe_pick_name(unsigned i) {
    int f = pe_fight_index(s_area, (int)i);
    return f >= 0 ? pe_fight_label(f) : "";
}

/* ---- the game ---- */

static unsigned pe_room_token(int map) {
    unsigned id = PE_ROOM_TEMPLATE;
    int digit[3] = {map / 100, map / 10 % 10, map % 10};
    if (map < 1 || map > 999) return 0;
    for (int i = 0; i < 3; ++i) {
        int k;
        for (k = 0; k < 32 && g_Base32CharTable[k] != '0' + digit[i]; ++k) {}
        if (k == 32) return 0;
        id |= (unsigned)k << ((3 - i) * 5 + 2);   /* chars 2, 3, 4: bits 17, 12, 7 */
    }
    return id;
}

/* The field is free and settled in a loaded room (not a battle, an event, a movie or a transfer). */
static int pe_field_ready(void) {
    unsigned token = (unsigned)g_SceneDispatchToken;
    return pe_game_loaded() && token && (unsigned)g_SceneDispatchCur == token && pe_free_field() &&
           !g_GameState.scene_audio.tracks.movie_active && g_PlayerEntity;
}

static void pe_place(int x, int z, int yaw) {
    FieldActor *p = g_PlayerEntity;
    p->pos_x = p->base_x = x;
    p->pos_z = p->base_z = z;
    p->rot_y = p->saved_rot_y = (short)(yaw & 0xFFF);
    p->collision_face = p->collision_face_mirror = NULL;
    Entity_FindFloor((struct BattleEntity *)p);
}

/* Stand Aya on walkable floor inside the zone: its middle first, then points across it (some
 * zones reach past the floor, and a battle started with no floor under Aya reads through a null
 * floor triangle). 0 when no point of the zone has floor. */
static int pe_place_in_zone(const PeEncounterZone *zone, int yaw) {
    static const int k_steps[] = {4, 2, 6, 3, 5, 1, 7};   /* eighths across, middle first */
    const int *c = zone->corner;
    int x, z;
    pe_encounter_center(zone, &x, &z);
    pe_place(x, z, yaw);
    if (g_PlayerEntity->collision_face) return 1;
    for (int i = 0; i < 7; ++i)
        for (int j = 0; j < 7; ++j) {
            /* bilinear point (u, v) of the quad c0 c1 c2 c3 */
            long long u = k_steps[i], v = k_steps[j];
            long long px = ((8 - u) * (8 - v) * c[0] + (8 - u) * v * c[2] + u * v * c[4] + u * (8 - v) * c[6]) / 64;
            long long pz = ((8 - u) * (8 - v) * c[1] + (8 - u) * v * c[3] + u * v * c[5] + u * (8 - v) * c[7]) / 64;
            pe_place((int)px, (int)pz, yaw);
            if (g_PlayerEntity->collision_face) return 1;
        }
    return 0;
}

static void pe_transfer(unsigned token) {
    int target = (int)token, *args[1] = {&target};
    (void)Script_EncodeBarcode(args);
}

/* The loaded room's script blob (its asset block), or NULL. */
static const unsigned char *pe_room_blob(size_t *bytes) {
    int map = pe_room_number() - 1;
    if (!g_GameState.loaded_scene_assets || map < 0 || map >= SCENE_ROOM_COUNT) return NULL;
    *bytes = (size_t)g_RoomSectorTable[map].roomSectors * 2048;
    return (const unsigned char *)g_GameState.loaded_scene_assets;
}

static void pe_finish(const char *result) {
    pe_enemy_battle_disarm();
    snprintf(s_last, sizeof s_last, "%s: %s", s.automatic ? "RE2 opening street" : pe_fight_label(s.fight), result);
    pe_log("cheat_fight_picker", "result fight=%d map=%u spot=%u %s", s.fight, k_fights[s.fight].map,
           k_fights[s.fight].spot, result);
}

static const PeRePlayApi *pe_imported_room_api(void) {
    const PeRePlayApi *api = pe_service_get(RE_PLAY_SERVICE, RE_PLAY_SERVICE_VERSION);
    return api && api->struct_size >= offsetof(PeRePlayApi, defeated_slot) && api->pause_for_battle &&
           api->resume_after_battle ? api : NULL;
}

static void pe_end_trip(void) {
    pe_enemy_battle_disarm();
    if (s.imported_room_paused) {
        const PeRePlayApi *api = pe_imported_room_api();
        if (api) api->resume_after_battle();
        s.imported_room_paused = 0;
    }
    s.state = PE_IDLE;
}

/* Fight over (or never started): put the script variables back and go home. */
static void pe_go_home(void) {
    memcpy(g_ScriptVariableBank, s.vars, sizeof s.vars);
    if (s.same_room) {
        pe_place(s.x, s.z, s.yaw);
        pe_end_trip();
        pe_toast("Fights: back where you were");
        return;
    }
    pe_transfer(s.origin);
    s.state = PE_HOME;
    s.frames = 0;
}

static void pe_arrived(void) {
    const PeFight *f = &k_fights[s.fight];
    PeEncounterZone zones[PE_ZONES_MAX];
    size_t bytes = 0;
    const unsigned char *blob = pe_room_blob(&bytes);
    int n = blob ? pe_encounter_find(blob, bytes, zones, PE_ZONES_MAX) : 0, x, z;
    if (f->spot >= n || f->spot >= PE_ZONES_MAX) {
        pe_finish("this room has no such battle spot");
        pe_toast("Fights: this room has no such battle spot");
        pe_go_home();
        return;
    }
    x = g_PlayerEntity->pos_x;
    z = g_PlayerEntity->pos_z;
    if (!pe_place_in_zone(&zones[f->spot], g_PlayerEntity->rot_y)) {
        pe_place(x, z, g_PlayerEntity->rot_y);
        pe_finish("no floor in the battle spot");
        pe_toast("Fights: no floor in that battle spot; going back");
        pe_go_home();
        return;
    }
    x = g_PlayerEntity->pos_x;
    z = g_PlayerEntity->pos_z;
    s.roll_next = (uintptr_t)(blob + zones[f->spot].roll) + PE_ENCOUNTER_RANDOM_BYTES;
    s.state = PE_WAIT_BATTLE;
    s.frames = 0;
    pe_log("cheat_fight_picker", "spot map=%u spot=%u of %d at %d,%d", f->map, f->spot, n, x, z);
}

typedef struct {
    const PeEnemyBuildApi *build;
    const PeRePlayApi *play;
    const char *archive;
    char assets[PE_TRIP_FOES_MAX][PE_IMP_ID_MAX];
    char packs[PE_TRIP_FOES_MAX][64];
    int count;
} PeRoomPacks;

static int pe_try_room_pack(const PeEnemyBuildApi *b, const char *asset, const char *entrance, const char *room_move,
                            int quiet, char pack[64]) {
    char why[200] = "";
    if (pe_enemy_pack_name(asset, pack, 64)) return 0;
    if (quiet && (!b || b->struct_size < sizeof *b)) return 0;
    if (b && b->struct_size >= sizeof *b && b->ensure(asset, why, sizeof why)) {
        if (quiet) return 0;
        pe_mod_log(MOD_ID, "the room's %s: no fresh build (%s); an older pack is used if there is one", asset, why);
    }
    return pe_enemy_battle_add_pack(pack, entrance, room_move);
}

static int pe_add_room_pack(const char *asset, int already_selected, const char *entrance,
                            const PeReEnemyPlacement *placement, void *ctx) {
    PeRoomPacks *p = ctx;
    char move[PE_IMP_ID_MAX];
    const char *room_move = NULL;
    if (!strncmp(asset, "re2:enemy/em1", 13) && (placement->behaviour & 0x3f) == 10 &&
        p->play->struct_size >= offsetof(PeRePlayApi, encounter_position) && p->play->enemy_room_move &&
        !p->play->enemy_room_move(placement->slot, move, sizeof move)) room_move = move;
    if (already_selected)
        for (int i = 0; i < p->count; i++)
            if (!strcmp(p->assets[i], asset)) return pe_enemy_battle_add_pack(p->packs[i], entrance, room_move);
    char variant[PE_IMP_ID_MAX];
    const char *selected = asset;
    char *pack = p->packs[p->count];
    if (p->archive) {
        const char *archives[] = {p->archive, !strcmp(p->archive, "cdemd1") ? "cdemd3" : "cdemd2"};
        for (unsigned i = 0; i < sizeof archives / sizeof archives[0]; ++i)
            if (snprintf(variant, sizeof variant, "%s-%s", asset, archives[i]) < (int)sizeof variant &&
                pe_try_room_pack(p->build, variant, entrance, room_move, 1, pack)) {
                selected = variant;
                break;
            }
    }
    if (selected == asset && !pe_try_room_pack(p->build, asset, entrance, room_move, 0, pack)) return 0;
    snprintf(p->assets[p->count++], PE_IMP_ID_MAX, "%s", asset);
    pe_mod_log(MOD_ID, "trip from an imported room: selected %s (pack %s)", selected, pack);
    return 1;
}

static int pe_room_creature(const PeRePlayApi *api) {
    PeReRoomEvent room = {sizeof room};
    PeReEnemyPlacement close[PE_TRIP_FOES_MAX];
    if (api->room(&room)) return 0;
    if (s.automatic) {
        double x, z;
        int floor;
        if (api->struct_size < sizeof *api || !api->encounter_position ||
            api->encounter_position(&x, &z, &floor)) return 0;
        room.count = pe_trip_street(&room, x, z, floor, close);
        room.enemies = close;
        if (!room.count) return 0;
    }
    pe_enemy_battle_begin_roster();
    PeRoomPacks packs = {.build = pe_service_get(PE_ENEMY_BUILD_SERVICE, PE_ENEMY_BUILD_SERVICE_VERSION), .play = api};
    if (api->struct_size >= offsetof(PeRePlayApi, enemy_room_move) && api->enemy_archive) packs.archive = api->enemy_archive();
    const int count = pe_trip_arm(&s_trip, &room, pe_add_room_pack, &packs);
    if (!count && !s.automatic) pe_enemy_battle_arm(s_enemy, s_enemy_count + 1, s_room_enemies == 1);
    return count;
}

static void pe_leave(void) {
    const PeFight *f = &k_fights[s.fight];
    if (!pe_field_ready()) return;
    if (g_ScriptVariableBank[PE_VAR_BATTLE_FLAGS] & PE_SKIP_BATTLES) {
        pe_end_trip();
        pe_toast("Fights: turn off Combat > No random battles first");
        return;
    }
    const PeRePlayApi *api = pe_imported_room_api();
    if (s.automatic && (!api || !pe_room_creature(api))) {
        pe_end_trip();
        return;
    }
    if (api) {
        if (!s.preview && !s.automatic) pe_room_creature(api);
        const int paused = api->pause_for_battle();
        if (paused < 0) {
            pe_finish("the imported room is busy");
            pe_end_trip();
            if (!s.automatic) pe_toast("Fights: finish the current room action, then try again");
            return;
        }
        s.imported_room_paused = paused > 0;
    }
    memcpy(s.vars, g_ScriptVariableBank, sizeof s.vars);
    s.origin = (unsigned)g_SceneDispatchToken;
    s.target = pe_room_token(f->map);
    s.x = g_PlayerEntity->pos_x;
    s.z = g_PlayerEntity->pos_z;
    s.yaw = g_PlayerEntity->rot_y;
    s.same_room = s.target == s.origin;
    s.frames = s.travel = 0;
    s.saw_enemies = 0;
    g_ScriptVariableBank[PE_VAR_STORY] = k_areas[f->area].story;
    s.state = PE_ARRIVE;
    if (!s.same_room) pe_transfer(s.target);   /* the transfer never finishes into its own room */
    pe_log("cheat_fight_picker", "start fight=%d map=%u spot=%u from=%08X", s.fight, f->map, f->spot, s.origin);
}

static int pe_live_enemies(void) {
    int n = 0;
    for (int i = 0; i < 7; ++i)
        n += g_BattleEnemySlots[i].active && g_BattleEnemySlots[i].combatant.hpAlive > 0;
    return n;
}

static void pe_step(void) {
    if (s.state == PE_IDLE) return;
    if (!pe_save_loaded() || (unsigned)g_SceneDispatchToken == PE_TOKEN_TITLE) {   /* Game Over, the title */
        if (s.state >= PE_FIGHT) pe_finish("lost (Game Over)");
        pe_end_trip();
        return;
    }
    switch (s.state) {
    case PE_LEAVE:
        pe_leave();
        break;
    case PE_ARRIVE:
        if (++s.travel > PE_TRAVEL_FRAMES) {   /* the room plays an event, or the transfer didn't happen */
            pe_finish("the room never left Aya free");
            pe_toast("Fights: that room didn't let Aya go free; going back");
            if (pe_field_ready()) pe_go_home();
            else { memcpy(g_ScriptVariableBank, s.vars, sizeof s.vars); pe_end_trip(); }
            break;
        }
        if (!pe_field_ready() || (unsigned)g_SceneDispatchToken != s.target) { s.frames = 0; break; }
        if (++s.frames >= PE_SETTLE_FRAMES) pe_arrived();
        break;
    case PE_WAIT_BATTLE:
        if (pe_in_battle()) { s.state = PE_FIGHT; s.frames = 0; break; }
        if (++s.frames > PE_START_FRAMES) {
            pe_finish("the fight didn't start");
            pe_toast("Fights: that fight didn't start; going back");
            pe_go_home();
        }
        break;
    case PE_FIGHT:
        if (pe_in_battle()) break;
        pe_finish(s.live_enemies ? "ran away" : "won");
        pe_trip_defeated(&s_trip, s.automatic || !s.live_enemies ? pe_enemy_battle_defeated_mask() : 0);
        s_trip_wait = 1800;                                /* the trip back, a door: half a minute at most */
        s.state = PE_AFTER;
        s.frames = 0;
        break;
    case PE_AFTER:
        if (!pe_field_ready()) { s.frames = 0; break; }
        if (++s.frames >= PE_SETTLE_FRAMES) pe_go_home();
        break;
    case PE_HOME:
        if (!pe_field_ready() || (unsigned)g_SceneDispatchToken != s.origin) { s.frames = 0; break; }
        if (++s.frames >= PE_SETTLE_FRAMES / 3) {
            pe_place(s.x, s.z, s.yaw);
            pe_end_trip();
            pe_toast("Fights: back where you were");
        }
        break;
    default:
        break;
    }
}

static void pe_begin_fight(int f, int preview, int automatic);

static void pe_check_street(void) {
    if (!s_street_encounters || s.state != PE_IDLE || s_trip.armed || !pe_field_ready() ||
        (g_ScriptVariableBank[PE_VAR_BATTLE_FLAGS] & PE_SKIP_BATTLES)) return;
    const PeRePlayApi *api = pe_imported_room_api();
    if (!api || api->struct_size < sizeof *api || !api->encounter_position) return;
    PeReRoomEvent room = {sizeof room};
    if (api->room(&room)) return;
    if (strcmp(room.game, "re2") || room.stage || room.room) {
        s_street_latched = 0;
        return;
    }
    double x, z;
    int floor;
    if (api->encounter_position(&x, &z, &floor)) return;
    if (s_street_latched) {
        const double dx = x - s_street_x, dz = z - s_street_z;
        if (dx * dx + dz * dz < PE_TRIP_STREET_REARM * PE_TRIP_STREET_REARM) return;
        s_street_latched = 0;
    }
    PeReEnemyPlacement close[PE_TRIP_FOES_MAX];
    if (!pe_trip_street(&room, x, z, floor, close)) return;
    s_street_x = close[0].x, s_street_z = close[0].z;
    s_street_latched = 1;
    pe_begin_fight(PE_STREET_FIGHT, 0, 1);
    pe_mod_log(MOD_ID, "opening street: approached zombie slot %u at %d,%d", close[0].slot, s_street_x, s_street_z);
}

static int pe_on_frame(void *ctx, void *user) {
    (void)ctx; (void)user;
    pe_step();
    if (s_trip.armed && s.state == PE_IDLE) {             /* record defeats once the imported room is back */
        const int beaten = pe_trip_report(&s_trip, pe_imported_room_api());
        if (beaten > 0) {
            pe_log("cheat_fight_picker", "imported room: %d selected placements beaten", beaten);
            pe_toast(beaten == 1 ? "Fights: the room's enemy is beaten" : "Fights: the room's enemies are beaten");
        } else if (!beaten && --s_trip_wait <= 0) {
            s_trip.armed = 0;
        }
    }
    pe_check_street();
    return 0;
}

static int pe_on_battle(void *ctx, void *user) {
    (void)ctx; (void)user;
    if (s.state == PE_FIGHT) {
        int live = pe_live_enemies();
        if (live) s.saw_enemies = 1;
        if (s.saw_enemies) s.live_enemies = live;
    }
    return 0;
}

/* Only the picked spot's own encounter roll, only while waiting for it. */
static int pe_on_random(void *ctx, void *user) {
    PeHookScriptRandom *roll = (PeHookScriptRandom *)ctx;
    (void)user;
    if (s.state != PE_WAIT_BATTLE || (uintptr_t)roll->next != s.roll_next || *roll->start != 0) return 0;
    *roll->end = 1;
    return 0;
}

/* Every drawn frame: after a Game Over the field frames stop with the trip still on, and the
 * game goes on to the title. */
static int pe_on_drawn(void *ctx, void *user) {
    (void)ctx; (void)user;
    if (s.state != PE_IDLE && ((unsigned)g_SceneDispatchToken == PE_TOKEN_TITLE || !pe_save_loaded())) {
        pe_finish(s.state >= PE_FIGHT ? "lost (Game Over)" : "the game ended");
        pe_end_trip();
    }
    return 0;
}

static int pe_on_new_game(void *ctx, void *user) {
    (void)ctx; (void)user;
    s_trip.armed = 0;
    s_street_latched = 0;
    pe_end_trip();
    return 0;
}

static int pe_ready_fight(void) {
    int f = pe_fight_index(s_area, s_pick);
    if (!pe_game_loaded()) { pe_toast("Fights: load a game first"); return -1; }
    if (s.state != PE_IDLE) { pe_toast("Fights: a fight is already on its way"); return -1; }
    return f;
}

static void pe_begin_fight(int f, int preview, int automatic) {
    s.fight = f;
    s.live_enemies = 0;
    s.preview = preview;
    s.automatic = automatic;
    s_trip.armed = 0;
    s.state = PE_LEAVE;   /* on the next free field frame, once the panel closes */
}

static void pe_start(void) {
    const int f = pe_ready_fight();
    if (f < 0) return;
    pe_enemy_battle_arm(s_enemy, s_enemy_count + 1, s_room_enemies == 1);
    pe_begin_fight(f, 0, 0);
}

static struct {
    char path[1240], asset[PE_IMP_ID_MAX], store[1100];
    PeImpEnemyGuess profile;
    PeImpAnimation moves;
    SDL_Time modified;
    int hp, speed, move, loaded;
} s_edit;
static char s_edit_status[200] = "Choose an imported enemy on the Fights page.";

static void pe_edit_message(const char *message) {
    snprintf(s_edit_status, sizeof s_edit_status, "%s", message);
    pe_toast(s_edit_status);
}

static void pe_edit_load(void) {
    char dir[1200], id[PE_IMP_ID_MAX], why[200];
    PeEnemyPack pack;
    pe_imp_animation_free(&s_edit.moves);
    s_edit.loaded = 0;
    if (pe_enemy_battle_pack_path(s_enemy, dir, sizeof dir)) {
        pe_edit_message("Choose an imported enemy on the Fights page.");
        return;
    }
    if (pe_enemy_pack_load(&pack, dir, why, sizeof why)) { pe_edit_message(why); return; }
    snprintf(s_edit.asset, sizeof s_edit.asset, "%s", pack.source);
    pe_enemy_pack_free(&pack);
    const char *colon = strchr(s_edit.asset, ':');
    if (!colon || pe_mod_path(s_edit.store, sizeof s_edit.store, "import/store")) {
        pe_edit_message("Build this enemy from a scanned disc before editing its profile.");
        return;
    }
    snprintf(s_edit.path, sizeof s_edit.path, "%s/profile.json", dir);
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(s_edit.path, &info)) { pe_edit_message("This pack has no editable profile."); return; }
    if (pe_imp_enemy_profile_load(s_edit.path, &s_edit.profile, why, sizeof why)) { pe_edit_message(why); return; }
    snprintf(id, sizeof id, "%.*s:%s", (int)(colon - s_edit.asset), s_edit.asset, s_edit.profile.p.moves);
    if (pe_imp_catalog_animation(s_edit.store, id, &s_edit.moves, why, sizeof why)) { pe_edit_message(why); return; }
    s_edit.modified = info.modify_time;
    s_edit.hp = s_edit.profile.p.hp;
    s_edit.speed = s_edit.profile.p.speed;
    s_edit.move = -1;
    s_edit.loaded = 1;
    pe_edit_message("Profile loaded. Changes are held until you apply them.");
}

static int pe_edit_hp(void) { return s_edit.hp; }
static int pe_edit_speed(void) { return s_edit.speed; }
static int pe_edit_move(void) { return s_edit.move; }
static int pe_edit_set_hp(int value) { if (!s_edit.loaded) return 0; s_edit.hp = value; return 1; }
static int pe_edit_set_speed(int value) { if (!s_edit.loaded) return 0; s_edit.speed = value; return 1; }
static int pe_edit_set_move(int value) {
    if (!s_edit.loaded || value < -1 || value >= (int)s_edit.moves.moves) return 0;
    const int step = value < s_edit.move ? -1 : 1;
    while (value >= 0 && value < (int)s_edit.moves.moves && !s_edit.moves.move[value].frames) value += step;
    if (value >= (int)s_edit.moves.moves) return 0;
    s_edit.move = value;
    return 1;
}

static int pe_edit_attack_move(void) {
    if (s_edit.move >= 0) return s_edit.move;
    const PeImpEnemyProfile *p = &s_edit.profile.p;
    for (int i = 0; p->attack[0].anim && i < PE_IMP_ENEMY_ANIMS && p->anim[i].name; i++)
        if (!strcmp(p->attack[0].anim, p->anim[i].name)) return p->anim[i].move;
    return -1;
}

static int pe_edit_draft(PeImpEnemyProfile *draft) {
    *draft = s_edit.profile.p;
    draft->hp = s_edit.hp;
    draft->speed = s_edit.speed;
    if (s_edit.move < 0) return 0;
    const char *original = draft->attack[0].anim;
    int shared = original && (!strcmp(original, "idle") || !strcmp(original, "walk") ||
                              !strcmp(original, "hurt") || !strcmp(original, "die"));
    for (int j = 1; original && j < PE_IMP_ENEMY_ATTACKS && draft->attack[j].anim; ++j)
        if (!strcmp(original, draft->attack[j].anim)) shared = 1;
    int i = 0;
    while (i < PE_IMP_ENEMY_ANIMS && draft->anim[i].name &&
           (!original || shared || strcmp(draft->anim[i].name, original))) i++;
    if (i == PE_IMP_ENEMY_ANIMS) { pe_edit_message("This profile has no free animation role for an edited attack."); return -1; }
    if (!original) draft->attack[0].power = 20;
    if (!draft->anim[i].name) {
        for (int j = 0; j < i; ++j)
            if (!strcmp(draft->anim[j].name, "edited_attack")) {
                pe_edit_message("The edited attack name is already in use.");
                return -1;
            }
        draft->anim[i].name = "edited_attack";
    }
    draft->attack[0].anim = draft->anim[i].name;
    draft->anim[i].move = s_edit.move;
    draft->attack[0].hit = draft->attack[0].range = 0;
    return 0;
}

static void pe_edit_preview(void) {
    if (!s_edit.loaded) { pe_edit_message("Load an enemy profile first."); return; }
    const int move = pe_edit_attack_move(), f = pe_ready_fight();
    if (f < 0) return;
    if (move < 0) { pe_edit_message("Choose an attack move first."); return; }
    char why[200];
    PeImpModel model;
    if (pe_imp_fit(s_edit.store, s_edit.asset, PE_IMP_USE_ENEMY, &model, NULL, why, sizeof why)) {
        pe_edit_message(why);
        return;
    }
    const PeImpEnemyProfile *profile = &s_edit.profile.p;
    PeImpEnemyProfile preview = {.name = profile->name, .height = profile->height, .yaw = profile->yaw,
        .hp = s_edit.hp, .defense = profile->defense, .charge = profile->charge, .radius = profile->radius,
        .anim = {{"idle", 0}, {"preview", move}}};
    for (int i = 0; i < PE_IMP_ENEMY_ANIMS && profile->anim[i].name; ++i)
        if (!strcmp(profile->anim[i].name, "idle")) preview.anim[0].move = profile->anim[i].move;
    PeEnemyPack pack;
    PeEnemyBuildInfo info;
    const int rc = pe_enemy_build(&model, &s_edit.moves, &preview, NULL, 0, NULL, &pack, &info, why, sizeof why);
    pe_imp_model_free(&model);
    if (rc) { pe_edit_message(why); return; }
    pack.role[PE_ANIM_IDLE] = pe_enemy_pack_anim(&pack, "preview");
    if (pe_enemy_battle_preview(&pack)) {
        pe_enemy_pack_free(&pack);
        pe_edit_message("That move could not be previewed in the current game state.");
        return;
    }
    pe_mod_log(MOD_ID, "previewing %s source move %d", s_edit.asset, move);
    pe_begin_fight(f, 1, 0);
    pe_mod_panel_close();
}

static void pe_edit_apply(void) {
    if (!s_edit.loaded) { pe_edit_message("Load an enemy profile first."); return; }
    if (s.state != PE_IDLE || pe_in_battle()) { pe_edit_message("Finish the current fight before rebuilding."); return; }
    const PeEnemyBuildApi *api = pe_service_get(PE_ENEMY_BUILD_SERVICE, PE_ENEMY_BUILD_SERVICE_VERSION);
    if (!api || api->struct_size < sizeof *api) { pe_edit_message("The importer is unavailable."); return; }
    SDL_PathInfo info;
    char why[200], temporary[1300], backup[1300];
    if (!SDL_GetPathInfo(s_edit.path, &info) || info.modify_time != s_edit.modified) {
        pe_edit_message("The profile changed on disk. Load it again before editing.");
        return;
    }
    if (api->ensure(s_edit.asset, why, sizeof why)) { pe_edit_message(why); return; }
    if (!SDL_GetPathInfo(s_edit.path, &info) || info.modify_time != s_edit.modified) {
        pe_edit_message("The importer refreshed this profile. Load it again to edit the new version.");
        return;
    }
    PeImpEnemyProfile draft;
    if (pe_edit_draft(&draft)) return;
    snprintf(temporary, sizeof temporary, "%s.edit.tmp", s_edit.path);
    snprintf(backup, sizeof backup, "%s.edit.bak", s_edit.path);
    if (pe_imp_enemy_profile_save(&draft, "Edited in Fights > Enemy profile.", temporary, why, sizeof why)) {
        pe_edit_message(why);
        return;
    }
    if (!SDL_CopyFile(s_edit.path, backup) || !SDL_RenamePath(temporary, s_edit.path)) {
        SDL_RemovePath(temporary);
        pe_edit_message("Could not replace profile.json.");
        return;
    }
    if (api->ensure(s_edit.asset, why, sizeof why)) {
        char ignored[200];
        if (SDL_RenamePath(backup, s_edit.path)) api->ensure(s_edit.asset, ignored, sizeof ignored);
        s_edit.loaded = 0;
        pe_edit_message(why);
        return;
    }
    SDL_RemovePath(backup);
    if (SDL_GetPathInfo(s_edit.path, &info)) s_edit.modified = info.modify_time;
    if (pe_imp_enemy_profile_load(s_edit.path, &s_edit.profile, why, sizeof why)) s_edit.loaded = 0;
    s_edit.move = -1;
    pe_edit_message("Profile saved and enemy pack rebuilt.");
}

static void pe_edit_text(char *out, unsigned size) { snprintf(out, size, "%s", s_edit_status); }
static void pe_edit_name(char *out, unsigned size) {
    snprintf(out, size, "%s", s_edit.loaded ? s_edit.profile.p.name : "-");
}

static void pe_status_text(char *out, unsigned size) { snprintf(out, size, "%s", k_state_names[s.state]); }
static void pe_last_text(char *out, unsigned size) { snprintf(out, size, "%s", s_last); }
static void pe_enemy_text(char *out, unsigned size) { pe_enemy_status_text(out, size); }

static int enemy_import_init(void) {
    pe_enemy_battle_init();
    if (pe_enemy_target_register()) pe_mod_log(MOD_ID, "the importer refused the enemies builder");
    pe_hook_add(PE_HOOK_FRAME, pe_on_frame, NULL, PE_PRIORITY_LATE, MOD_ID);
    pe_hook_add(PE_HOOK_BATTLE_UPDATE, pe_on_battle, NULL, PE_PRIORITY_NORMAL, MOD_ID);
    pe_hook_add(PE_HOOK_SCRIPT_RANDOM, pe_on_random, NULL, PE_PRIORITY_LATE, MOD_ID);
    pe_hook_add(PE_HOOK_NEW_GAME, pe_on_new_game, NULL, PE_PRIORITY_NORMAL, MOD_ID);
    pe_hook_add(PE_HOOK_SAVE_READ, pe_on_new_game, NULL, PE_PRIORITY_NORMAL, MOD_ID);
    pe_hook_add(PE_HOOK_FRAME_DRAWN, pe_on_drawn, NULL, PE_PRIORITY_NORMAL, MOD_ID);

    pe_menu_begin(PE_PAGE_FIGHTS);
    pe_menu_top_page(PE_PAGE_FIGHTS, "Start a chosen fight to test it.", 55);
    pe_menu_register_dynamic_choice(0, "Area", "Where the fight is from.", &s_area, PE_AREAS,
                                    pe_area_count, pe_area_name);
    pe_menu_register_dynamic_choice(0, "Fight", "The room and its battle spot.", &s_pick, PE_FIGHTS,
                                    pe_pick_count, pe_pick_name);
    pe_menu_register_dynamic_choice(0, "Imported enemy", "An imported enemy (pe_enemies folder) that joins "
                                    "the fight as an enemy of its own, with its own moves.", &s_enemy, PE_ENEMY_PACKS_MAX + 1,
                                    pe_enemy_choice_count, pe_enemy_choice_name);
    pe_menu_register_choice(0, "How many", "How many of the imported enemy join.", &s_enemy_count,
                            k_counts, 3);
    pe_menu_register_choice(0, "Room's enemies", "Whether the room's own enemies fight too, or are sent "
                            "away so the imported enemy fights alone.", &s_room_enemies, k_room_enemies, 2);
    pe_menu_register_toggle(0, "RE2 street encounters", "Approaching zombies in RE2's opening street starts a fight.",
                            &s_street_encounters, NULL);
    pe_menu_register_action(0, "Start fight",
                            "Takes Aya to the fight (once the panel closes) and back afterwards. The story "
                            "is set to the fight's point for the trip.",
                            pe_start);
    pe_menu_register_info(0, "Status", "What the picker is doing.", pe_status_text);
    pe_menu_register_info(0, "Last fight", "How the last picked fight ended.", pe_last_text);
    pe_menu_register_info(0, "Imported status", "The imported enemy in the picked fight.", pe_enemy_text);
    pe_menu_page(PE_PROFILE_PAGE, "Tune an imported enemy and preview its attack in the selected fight.", 10);
    pe_menu_register_action(PE_PROFILE_PAGE, "Load selected enemy", "Loads the enemy selected on the Fights page.", pe_edit_load);
    pe_menu_register_info(PE_PROFILE_PAGE, "Editing", "The loaded profile.", pe_edit_name);
    pe_menu_register_live_int(PE_PROFILE_PAGE, "HP", "Maximum hit points.", 1, 99999, 1, pe_edit_hp, pe_edit_set_hp);
    pe_menu_register_live_int(PE_PROFILE_PAGE, "Speed", "Movement per frame.", 0, 200, 1, pe_edit_speed, pe_edit_set_speed);
    pe_menu_register_live_int(PE_PROFILE_PAGE, "Attack move", "Source animation index for the first attack. -1 keeps the current move.", -1,
                              PE_IMP_ANIM_MOVES_MAX - 1, 1, pe_edit_move, pe_edit_set_move);
    pe_menu_register_action(PE_PROFILE_PAGE, "Preview attack", "Loops the move in the selected fight. You can defeat the preview or escape.", pe_edit_preview);
    pe_menu_register_action(PE_PROFILE_PAGE, "Apply and rebuild", "Saves and rebuilds the enemy pack.", pe_edit_apply);
    pe_menu_register_info(PE_PROFILE_PAGE, "Editor status", "Profile editor status.", pe_edit_text);
    pe_menu_begin(0);
    return 0;
}

const PeMod pe_mod_pe_enemy_import = {MOD_ID, "Enemy import", "0.1", NULL, enemy_import_init, NULL};
