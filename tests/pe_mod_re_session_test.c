/* The play session (mods/pe_re_play/pe_re_session.c), without the game: zones, camera switches and
 * doors on a made-up room; then, with PE_TEST_RE2_DISC, RE2 imported and walked door by door from
 * the street: every door must lead to an imported room that has the camera it names, and should
 * put the player on free floor. No disc data is kept. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pe_imp.h"
#include "pe_imp_internal.h"
#include "pe_imp_logic.h"
#include "pe_imp_room.h"
#include "pe_re_save.h"
#include "pe_re_session.h"
#include "pe_src_re2.h"
#include "pe_trip_result.h"
#include "re_encounter_rooms.h"

int re_headless_smoke(const char *store, unsigned scenario);

static int s_fail;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); s_fail = 1; } } while (0)

static void rect(int16_t x[4], int16_t z[4], int x0, int z0, int x1, int z1) {
    x[0] = (int16_t)x0, z[0] = (int16_t)z0, x[1] = (int16_t)x1, z[1] = (int16_t)z0;
    x[2] = (int16_t)x1, z[2] = (int16_t)z1, x[3] = (int16_t)x0, z[3] = (int16_t)z1;
}

#define ROOMS_MAX 512
static const char *s_store = "pe-re-session-test/store";

static int clear_approach(double x, double z, double target_x, double target_z) {
    (void)x; (void)z; (void)target_x; (void)target_z;
    return 1;
}

/* A frame of the room's scripts without a session: the player's scripted walk (plc_dest) is done at
 * once, as the session's walk would finish it, so a scene waiting on her arrival flag goes on. */
static void scene_tick(const PeImpRoomLogic *logic, PeReFlags *flags, const PeReScdHost *host, PeReScdRun *run) {
    pe_re_scd_tick(logic, flags, host, run);
    if (run->player_dest.moving && !run->actor_stopped[0]) {
        run->player_dest.moving = 0;
        run->arrived[0] = run->player_dest.arrival;
    }
}

static void test_mr_x(const PeReGame *game, unsigned player) {
    char id[PE_IMP_ID_MAX];
    PeReSession scene = {.game = game, .stage = 4, .room = 7};
    PeReFlags *flags = &scene.progress.flags;
    PeReEnemyPlacement enemies[PE_RE_ENEMIES_MAX] = {0};
    pe_re_logic_id("re2", scene.stage, scene.room, player, id, sizeof id);
    CHECK(!pe_imp_catalog_roomlogic(s_store, id, &scene.logic, NULL, 0));
    if (!scene.logic) return;
    flags->work[PE_RE_WORK_STAGE] = scene.stage;
    flags->work[PE_RE_WORK_ROOM] = scene.room;
    flags->work[PE_RE_WORK_CUT] = 8;
    pe_re_flag_set(flags, PE_RE_FG_ITEM2, 9, 1);  /* proc 2 checks the sewer lift's item flag */
    pe_re_scd_run(scene.logic, flags, NULL, &scene.run);
    CHECK(pe_re_session_enemies(&scene, enemies, PE_RE_ENEMIES_MAX) == 1);
    CHECK(enemies[0].kind == 0x2a && !pe_re2_mr_x_fightable(&enemies[0]));
    pe_re_scd_exec(scene.logic, &scene.run, 2, 2);
    for (int tick = 0; tick < 20; tick++) pe_re_scd_tick(scene.logic, flags, NULL, &scene.run);
    CHECK(pe_re_session_enemies(&scene, enemies, PE_RE_ENEMIES_MAX) == 1);
    CHECK(pe_re2_mr_x_fightable(&enemies[0]) && enemies[0].killed_flag == 0x8e);
    CHECK(enemies[0].x != -32000 && enemies[0].z != -32000);
    PeReRoomEvent room = {sizeof room, "re2", scene.stage, scene.room, 0, 1, enemies};
    PeReEnemyPlacement nearby[PE_TRIP_FOES_MAX];
    CHECK(!pe_trip_approach(&room, enemies[0].x, enemies[0].z, enemies[0].floor + 1, clear_approach, nearby));
    CHECK(pe_trip_approach(&room, enemies[0].x, enemies[0].z, enemies[0].floor, clear_approach, nearby) == 1);
    CHECK(!pe_re_session_beaten(&scene, enemies[0].slot, enemies[0].killed_flag));
    CHECK(pe_re_flag(flags, PE_RE_FG_ENEMY + 1, 0x8e));
    CHECK(!pe_re_flag(flags, PE_RE_FG_ENEMY, 0xc4));
    CHECK(pe_re_session_enemies(&scene, enemies, PE_RE_ENEMIES_MAX) == 0);
    pe_re_session_end(&scene);
}

static void test_licker(const PeReGame *game, unsigned player) {
    char id[PE_IMP_ID_MAX];
    PeReSession scene = {.game = game, .stage = 1, .room = 4};
    PeReFlags *flags = &scene.progress.flags;
    PeReEnemyPlacement enemies[PE_RE_ENEMIES_MAX] = {0}, nearby[PE_TRIP_FOES_MAX];
    pe_re_logic_id("re2", scene.stage, scene.room, player, id, sizeof id);
    CHECK(!pe_imp_catalog_roomlogic(s_store, id, &scene.logic, NULL, 0));
    if (!scene.logic) return;
    flags->work[PE_RE_WORK_STAGE] = scene.stage;
    flags->work[PE_RE_WORK_ROOM] = scene.room;
    pe_re_scd_run(scene.logic, flags, NULL, &scene.run);
    pe_re_scd_exec(scene.logic, &scene.run, 2, 2);
    scene_tick(scene.logic, flags, NULL, &scene.run);
    const int16_t *licker = scene.run.works[3];
    CHECK(licker[0] && licker[PE_RE_MEMBER_ID] == 0x22 && licker[PE_RE_MEMBER_TYPE] == 0x4510);
    PeReRoomEvent room = {sizeof room, "re2", scene.stage, scene.room, 0, 0, enemies};
    room.count = pe_re_session_enemies(&scene, enemies, PE_RE_ENEMIES_MAX);
    CHECK(!pe_trip_approach(&room, 32000, 32000, 0, clear_approach, nearby));

    pe_re_scd_exec(scene.logic, &scene.run, 2, 9);
    int saw_entrance = 0;
    for (int tick = 0; tick < 600; tick++) {
        scene_tick(scene.logic, flags, NULL, &scene.run);
        if (pe_re_scd_running(&scene.run, 9) && licker[PE_RE_MEMBER_SCE_FREE0] == 1) {
            saw_entrance = 1;
            CHECK(licker[PE_RE_MEMBER_TYPE] == 0x4510);
        }
    }
    CHECK(saw_entrance && licker[PE_RE_MEMBER_TYPE] == 0x0510);
    room.count = pe_re_session_enemies(&scene, enemies, PE_RE_ENEMIES_MAX);
    CHECK(pe_trip_approach(&room, -14869, -3479, 0, clear_approach, nearby) > 0);
    CHECK(nearby[0].kind == 0x22 && nearby[0].slot == 1 && nearby[0].killed_flag == 0x3d && nearby[0].y == -5400);
    CHECK(!pe_re_session_beaten(&scene, 1, 0x3d));
    CHECK(pe_re_flag(flags, PE_RE_FG_ENEMY, 0x3d));
    pe_re_scd_run(scene.logic, flags, NULL, &scene.run);
    pe_re_scd_exec(scene.logic, &scene.run, 2, 2);
    scene_tick(scene.logic, flags, NULL, &scene.run);
    room.count = pe_re_session_enemies(&scene, enemies, PE_RE_ENEMIES_MAX);
    CHECK(!pe_trip_approach(&room, -14869, -3479, 0, clear_approach, nearby));
    pe_re_session_end(&scene);
}

static void test_adult_g(const PeReGame *game, unsigned player) {
    char id[PE_IMP_ID_MAX];
    PeReSession scene = {.game = game, .stage = 2, .room = player ? 9 : 3};
    const unsigned setup = player ? 5 : 11;
    PeReFlags *flags = &scene.progress.flags;
    PeReEnemyPlacement enemies[PE_RE_ENEMIES_MAX], nearby[PE_TRIP_FOES_MAX] = {0};
    pe_re_logic_id("re2", scene.stage, scene.room, player, id, sizeof id);
    CHECK(!pe_imp_catalog_roomlogic(s_store, id, &scene.logic, NULL, 0));
    if (!scene.logic) return;
    flags->work[PE_RE_WORK_STAGE] = scene.stage;
    flags->work[PE_RE_WORK_ROOM] = scene.room;
    pe_re_scd_run(scene.logic, flags, NULL, &scene.run);
    pe_re_scd_exec(scene.logic, &scene.run, 2, setup);
    pe_re_scd_tick(scene.logic, flags, NULL, &scene.run);
    PeReRoomEvent room = {sizeof room, "re2", scene.stage, scene.room, 0, 0, enemies};
    room.count = pe_re_session_enemies(&scene, enemies, PE_RE_ENEMIES_MAX);
    PeTripResult trip;
    CHECK(!pe_trip_arm(&trip, &room, NULL, NULL));
    pe_re_scd_exec(scene.logic, &scene.run, 2, player ? 10 : 16);
    for (int tick = 0; tick < 1200; tick++) pe_re_scd_tick(scene.logic, flags, NULL, &scene.run);
    CHECK(scene.run.works[2][PE_RE_MEMBER_SCE_FREE0] & 1);
    room.count = pe_re_session_enemies(&scene, enemies, PE_RE_ENEMIES_MAX);
    int found = 0;
    for (int i = 0; i < room.count; i++) {
        const PeReEnemyPlacement *adult = &enemies[i];
        if (adult->kind != 0x28 || adult->slot != 0) continue;
        found = 1;
        CHECK(pe_trip_approach(&room, adult->x, adult->z, adult->floor, clear_approach, nearby) == 2);
        CHECK(nearby[0].killed_flag == 0x62 && nearby[1].kind == 0x27 && nearby[1].killed_flag == 0xff);
    }
    CHECK(found);
    CHECK(pe_trip_arm(&trip, &room, NULL, NULL) == 2 && trip.slot[0] == 0 && trip.slot[1] == 1);
    CHECK(!pe_re_session_beaten(&scene, 0, 0x62));
    CHECK(pe_re_flag(flags, PE_RE_FG_ENEMY, 0x62));
    room.count = pe_re_session_enemies(&scene, enemies, PE_RE_ENEMIES_MAX);
    for (int i = 0; i < room.count; i++) CHECK(enemies[i].kind != 0x28 && enemies[i].kind != 0x27);
    pe_re_session_end(&scene);
}

static void test_licker_variants(const PeReGame *game, unsigned player) {
    const struct {
        unsigned stage, room, cut, setup, entrance;
        uint16_t initial, released;
    } placements[] = {
        {0, 0x0b, 0, 4, 13, 0x4011, 0x0011},
        {1, 0x0e, 3, 0, 2, 0x4013, 0x0013},
        {1, 0x01, 0, 2, 4, 0x4402, 0x4402},
    };
    for (int i = 0; i < 3; i++) {
        char id[PE_IMP_ID_MAX];
        PeReSession scene = {.game = game, .stage = placements[i].stage, .room = placements[i].room};
        PeReFlags *flags = &scene.progress.flags;
        pe_re_logic_id("re2", scene.stage, scene.room, player, id, sizeof id);
        CHECK(!pe_imp_catalog_roomlogic(s_store, id, &scene.logic, NULL, 0));
        if (!scene.logic) continue;
        flags->work[PE_RE_WORK_STAGE] = scene.stage;
        flags->work[PE_RE_WORK_ROOM] = scene.room;
        flags->work[PE_RE_WORK_CUT] = (int16_t)placements[i].cut;
        pe_re_flag_set(flags, PE_RE_FG_STATUS, 1, !scene.stage);
        pe_re_scd_run(scene.logic, flags, NULL, &scene.run);
        if (placements[i].setup) pe_re_scd_exec(scene.logic, &scene.run, 2, placements[i].setup);
        pe_re_scd_tick(scene.logic, flags, NULL, &scene.run);
        const int16_t *licker = scene.run.works[2];
        CHECK(licker[0] && licker[PE_RE_MEMBER_ID] == 0x22 && licker[PE_RE_MEMBER_TYPE] == placements[i].initial);
        if (!pe_re_scd_running(&scene.run, placements[i].entrance))
            pe_re_scd_exec(scene.logic, &scene.run, 2, placements[i].entrance);
        for (int tick = 0; tick < 1200; tick++) pe_re_scd_tick(scene.logic, flags, NULL, &scene.run);
        CHECK(licker[PE_RE_MEMBER_SCE_FREE0] == 1 && licker[PE_RE_MEMBER_TYPE] == placements[i].released);
        pe_re_session_end(&scene);
    }
}

/* 1-07's entry script swaps cameras 0/6 and 1/7 in its switch zones once Common 0x03 is set (cut_replace),
 * and every room entry rolls the scripts' random value (Sce_random). */
static void test_cut_replace(const PeReGame *game, unsigned player) {
    char id[PE_IMP_ID_MAX];
    PeReSession scene = {.game = game, .stage = 0, .room = 7};
    PeReFlags *flags = &scene.progress.flags;
    if (player) return;                                       /* Leon's room file */
    pe_re_logic_id("re2", scene.stage, scene.room, player, id, sizeof id);
    CHECK(!pe_imp_catalog_roomlogic(s_store, id, &scene.logic, NULL, 0));
    if (!scene.logic) return;
    flags->work[PE_RE_WORK_STAGE] = scene.stage;
    flags->work[PE_RE_WORK_ROOM] = scene.room;
    pe_re_scd_run(scene.logic, flags, NULL, &scene.run);
    CHECK(!scene.run.cut_swapped);
    const int16_t first = flags->work[PE_RE_WORK_RANDOM];
    pe_re_flag_set(flags, 4, 0x03, 1);
    pe_re_scd_run(scene.logic, flags, NULL, &scene.run);
    CHECK(flags->work[PE_RE_WORK_RANDOM] != first);
    CHECK(scene.run.cut_swapped && scene.run.cut_to[0] == 6 && scene.run.cut_to[6] == 0 &&
          scene.run.cut_to[1] == 7 && scene.run.cut_to[7] == 1 && scene.run.cut_to[2] == 2);
    CHECK(scene.run.cut_from[6] == 0 && scene.run.cut_from[7] == 1);
    pe_re_session_end(&scene);
}

static void test_alligator(const PeReGame *game, unsigned player) {
    char id[PE_IMP_ID_MAX];
    PeReSession boss = {.game = game, .stage = 3, .room = 0x0a};
    PeReFlags *flags = &boss.progress.flags;
    pe_re_logic_id("re2", boss.stage, boss.room, player, id, sizeof id);
    CHECK(!pe_imp_catalog_roomlogic(s_store, id, &boss.logic, NULL, 0));
    if (!boss.logic) return;
    flags->work[PE_RE_WORK_STAGE] = boss.stage;
    flags->work[PE_RE_WORK_ROOM] = boss.room;
    pe_re_flag_set(flags, PE_RE_FG_STATUS, 0, player != 0);
    pe_re_scd_run(boss.logic, flags, NULL, &boss.run);
    CHECK(boss.run.works[2][0] && boss.run.works[2][PE_RE_MEMBER_ID] == 0x23);
    CHECK(pe_re_session_ready_boss(&boss) == -1);

    pe_re_scd_exec(boss.logic, &boss.run, 2, 2);
    for (int ticks = 0; ticks < 800 && pe_re_session_ready_boss(&boss) < 0; ticks++) {
        /* Claire A's partner scene waits for the approach to cross these two camera zones. */
        if (player) flags->work[PE_RE_WORK_CUT] = ticks < 300 ? 12 : 10;
        scene_tick(boss.logic, flags, NULL, &boss.run);
    }
    CHECK(pe_re_session_ready_boss(&boss) == 0);
    const PeReFlags entrance_flags = *flags;
    const PeReScdRun entrance_run = boss.run;
    CHECK(!pe_re_session_beaten(&boss, 0, 0));
    scene_tick(boss.logic, flags, NULL, &boss.run);
    CHECK(boss.run.camera == 8 && !boss.run.camera_lock);
    CHECK(pe_re_session_ready_boss(&boss) == -1 && pe_re_flag(flags, PE_RE_FG_ENEMY + 1, 0));
    pe_re_scd_run(boss.logic, flags, NULL, &boss.run);
    CHECK(!boss.run.works[2][0]);

    *flags = entrance_flags;
    boss.run = entrance_run;
    CHECK(!pe_re_session_canister_ready(&boss));
    CHECK(pe_re_session_canister_win(&boss, 0, 0) < 0);
    pe_re_session_release_canister(&boss);
    for (int ticks = 0; ticks < 150 && pe_re_scd_running(&boss.run, 10); ticks++)
        scene_tick(boss.logic, flags, NULL, &boss.run);
    CHECK(pe_re_session_canister_ready(&boss));
    CHECK(pe_re_session_canister_win(&boss, 1, 0) < 0);
    CHECK(!pe_re_session_canister_win(&boss, 0, 0));
    CHECK(pe_re_flag(flags, 5, 0x1d) && pe_re_flag(flags, 5, 0x1e));
    CHECK(pe_re_flag(flags, PE_RE_FG_ENEMY + 1, 0) && pe_re_session_ready_boss(&boss) == -1);
    pe_re_session_end(&boss);
}

static void test_final_tyrant(const PeReGame *game, unsigned player) {
    char id[PE_IMP_ID_MAX];
    PeReSession boss = {.game = game, .stage = 6, .room = 2};
    PeReFlags *flags = &boss.progress.flags;
    pe_re_logic_id("re2", boss.stage, boss.room, player, id, sizeof id);
    CHECK(!pe_imp_catalog_roomlogic(s_store, id, &boss.logic, NULL, 0));
    if (!boss.logic) return;
    flags->work[PE_RE_WORK_STAGE] = boss.stage;
    flags->work[PE_RE_WORK_ROOM] = boss.room;
    pe_re_flag_set(flags, PE_RE_FG_STATUS, 0, player != 0);
    pe_re_flag_set(flags, PE_RE_FG_STATUS, 1, 1);
    pe_re_scd_run(boss.logic, flags, NULL, &boss.run);
    CHECK(boss.run.works[2][0] && boss.run.works[2][PE_RE_MEMBER_ID] == 0x2b);
    CHECK(pe_re_session_final_tyrant(&boss, 0) && !pe_re_session_final_tyrant(&boss, 1));
    CHECK(pe_re_session_ready_boss(&boss) == -1);

    pe_re_scd_exec(boss.logic, &boss.run, 2, 7);
    for (int ticks = 0; ticks < 1000 && pe_re_session_ready_boss(&boss) < 0; ticks++)
        pe_re_scd_tick(boss.logic, flags, NULL, &boss.run);
    CHECK(pe_re_session_ready_boss(&boss) == 0);
    CHECK(!pe_re_session_beaten(&boss, 0, 0x28));
    for (int ticks = 0; ticks < 500 && !pe_re_flag(flags, 4, 0x97); ticks++)
        pe_re_scd_tick(boss.logic, flags, NULL, &boss.run);
    CHECK(pe_re_flag(flags, 4, 0x97) && pe_re_session_ready_boss(&boss) == -1);
    CHECK(!pe_re_session_final_tyrant(&boss, 0));
    pe_re_session_end(&boss);
}

static void test_lift_boss(const PeReGame *game, unsigned player) {
    char id[PE_IMP_ID_MAX];
    PeImpRoomLogic *logic = NULL;
    PeReSession boss = {.game = game, .stage = 5, .room = 0x17};
    PeReFlags *flags = &boss.progress.flags;
    pe_re_logic_id("re2", boss.stage, boss.room, player, id, sizeof id);
    CHECK(!pe_imp_catalog_roomlogic(s_store, id, &logic, NULL, 0));
    if (!logic) return;
    flags->work[PE_RE_WORK_STAGE] = boss.stage;
    flags->work[PE_RE_WORK_ROOM] = boss.room;
    pe_re_scd_run(logic, flags, NULL, &boss.run);
    CHECK(pe_re_session_ready_boss(&boss) == -1);
    pe_re_scd_exec(logic, &boss.run, 2, 7);
    for (int ticks = 0; ticks < 800; ticks++) scene_tick(logic, flags, NULL, &boss.run);
    CHECK(pe_re_session_ready_boss(&boss) == 0);
    CHECK(!pe_re_session_beaten(&boss, 0, 0xff));
    scene_tick(logic, flags, NULL, &boss.run);
    CHECK(pe_re_session_ready_boss(&boss) == -1 && boss.run.em_dead[0] == 2);
    free(logic);
}

/* Run the retail room procedures that order the three scene actors. */
static void test_scene_actors(unsigned player) {
    static const struct {
        unsigned room, kind, procedure;
    } cases[] = {{0x03, 0x2c, 27}, {0x08, 0x3f, 5}, {0x0d, 0x3e, 6}};
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        char id[PE_IMP_ID_MAX];
        PeImpRoomLogic *logic = NULL;
        pe_re_logic_id("re2", 5, cases[i].room, player, id, sizeof id);
        CHECK(!pe_imp_catalog_roomlogic(s_store, id, &logic, NULL, 0));
        if (!logic) continue;
        PeReFlags flags = {0};
        PeReScdRun run;
        PeReSceneActor actor;
        pe_re_scd_run(logic, &flags, NULL, &run);
        if (cases[i].kind == 0x2c) {
            /* Room 603's story checks before it creates the actor and starts its child procedure. */
            pe_re_flag_set(&flags, 1, 1, 1);
            pe_re_flag_set(&flags, 4, 0x73, 0);
            pe_re_flag_set(&flags, 4, 0x5c, 1);
            pe_re_scd_exec(logic, &run, 2, cases[i].procedure);
            pe_re_scd_tick(logic, &flags, NULL, &run);
        }
        const int placed = pe_re_scd_scene_actors(&run, &actor, 1);
        CHECK(placed == 1 && actor.kind == cases[i].kind);
        if (placed != 1) { free(logic); continue; }
        const int original_x = actor.x, original_z = actor.z;
        if (cases[i].kind != 0x2c) pe_re_scd_exec(logic, &run, 2, cases[i].procedure);
        unsigned moves = 0;
        int advanced = 0, moved = 0, reversed = 0, part_moved = 0, last_frame = -1;
        unsigned last_move = 255;
        for (int ticks = 0; ticks < 1300; ticks++) {
            pe_re_scd_tick(logic, &flags, NULL, &run);
            if (!pe_re_scd_scene_actors(&run, &actor, 1)) continue;
            moves |= 1u << actor.move;
            advanced |= actor.frame > 0;
            moved |= actor.x != original_x || actor.z != original_z;
            reversed |= last_move == actor.move && last_frame > actor.frame;
            part_moved |= actor.part_y[0] > 0;
            last_frame = actor.frame;
            last_move = actor.move;
        }
        CHECK(advanced);
        if (cases[i].kind == 0x2c) CHECK(moved && actor.frame == 65);
        if (cases[i].kind == 0x3e) {
            CHECK(moved && reversed && (moves & 0x17) == 0x17);
            CHECK(!pe_re_scd_running(&run, cases[i].procedure));
        }
        if (cases[i].kind == 0x3f) {
            CHECK(part_moved && actor.frame == 59);
            pe_re_flag_set(&flags, 4, 0x6a, 1);
            pe_re_scd_exec(logic, &run, 2, 6);
            for (int ticks = 0; ticks < 8; ticks++) pe_re_scd_tick(logic, &flags, NULL, &run);
            CHECK(pe_re_scd_scene_actors(&run, &actor, 1) == 1 && actor.absolute_parts && actor.part_y[1] == -103);
        }
        printf("RE2 room 6-%02x: scene actor %02x, moves 0x%x, last frame %u\n",
               cases[i].room, cases[i].kind, moves, (unsigned)actor.frame);
        free(logic);
    }
}

static void test_room_effects(const char *store) {
    static PeReScdRun run;
    PeReFlags flags = {0};
    PeImpRoomLogic *room = NULL;
    PeReEffect effects[PE_RE_EFFECT_MAX];
    CHECK(!pe_imp_catalog_roomlogic(store, "re2:logic/100-0", &room, NULL, 0));
    if (!room) return;
    uint32_t coll_size = 0;
    const uint8_t *raw = pe_imp_logic_section(room, PE_IMP_LOGIC_COLL, NULL, &coll_size);
    PeRe2RoomCollision collision;
    if (!pe_imp_re2_collision_all(raw, coll_size, &collision)) {
        PeReSession street = {.game = pe_re_game_named("re2")};
        PeReEnemyPlacement zombie = {.slot = 0, .kind = 0x1f, .behaviour = 0x10c6,
                                    .killed_flag = 0x23, .x = 22813, .z = 210};
        street.run.works[2][0] = 1;
        street.run.works[2][PE_RE_MEMBER_ID] = zombie.kind;
        CHECK(pe_imp_re2_blocked_on(&collision, 0, zombie.x, zombie.z, 240));
        CHECK(pe_re_session_chase(&street, &collision, &zombie, 1, 20888, 7, 0) == 1);
        CHECK(!pe_imp_re2_blocked_on(&collision, 0, zombie.x, zombie.z, 240));
    }
    pe_re_scd_run(room, &flags, NULL, &run);
    pe_re_scd_exec(room, &run, 2, 6);
    pe_re_scd_tick(room, &flags, NULL, &run);
    CHECK(pe_re_scd_effects(&run, effects) >= 10 && effects[0].loop);
    pe_re_scd_exec(room, &run, 2, 7);
    pe_re_scd_tick(room, &flags, NULL, &run);
    CHECK(pe_re_scd_effects(&run, effects) == 3);
    free(room);

    room = NULL;
    memset(&flags, 0, sizeof flags);
    flags.work[PE_RE_WORK_ROOM] = 1;
    CHECK(!pe_imp_catalog_roomlogic(store, "re2:logic/101-0", &room, NULL, 0));
    if (!room) return;
    pe_re_scd_run(room, &flags, NULL, &run);
    pe_re_scd_exec(room, &run, 2, 13);
    pe_re_scd_tick(room, &flags, NULL, &run);
    const int count = pe_re_scd_effects(&run, effects);
    CHECK(count > 1 && effects[0].id == 0x13 && !effects[0].loop);
    if (count) {
        /* Exercise the actor transform with one of the real glass instances. */
        const int x = effects[0].x;
        run.effects[0].attachment = 1;
        run.works[0][PE_RE_MEMBER_CDIR_Y] = 0;
        run.works[0][PE_RE_MEMBER_POS_X] = 500;
        CHECK(pe_re_scd_effects(&run, effects) == count && effects[0].x == x + 500);
        run.works[0][PE_RE_MEMBER_POS_X] = 700;
        CHECK(pe_re_scd_effects(&run, effects) == count && effects[0].x == x + 700);
    }
    /* The full room can issue more bursts. Check expiry of this spawn in isolation. */
    memset(run.task, 0, sizeof run.task);
    run.len[1] = 0;
    for (int i = 0; i < 120; i++) pe_re_scd_tick(room, &flags, NULL, &run);
    CHECK(pe_re_scd_effects(&run, effects) == 0);
    free(room);
}

static void test_re2_script_mask_groups(const char *store) {
    static PeReScdRun run;
    PeReFlags flags = {0};
    PeImpRoomLogic *room = NULL;
    CHECK(!pe_imp_catalog_roomlogic(store, "re2:logic/105-0", &room, NULL, 0));
    if (!room) return;
    pe_re_scd_run(room, &flags, NULL, &run);
    /* Room 1-05's entry script disables group 1 on camera 6. */
    CHECK(!pe_re_flag(&flags, PE_RE_FG_PRI + 6, 1));
    CHECK(pe_re_flag(&flags, PE_RE_FG_PRI + 6, 2));
    CHECK(pe_re_flag(&flags, PE_RE_FG_PRI + 5, 1));
    free(room);
}

static void test_kendo_zombies(const char *store) {
    static PeReScdRun run;
    PeReFlags flags = {0};
    PeImpRoomLogic *room = NULL;
    flags.work[PE_RE_WORK_ROOM] = 1;
    CHECK(!pe_imp_catalog_roomlogic(store, "re2:logic/101-0", &room, NULL, 0));
    if (!room) return;
    pe_re_scd_run(room, &flags, NULL, &run);
    /* The opening setup parks all four window actors outside the shot. */
    for (int slot = 1; slot <= 4; slot++) {
        const int16_t *work = run.works[2 + slot];
        CHECK(work[0] && work[PE_RE_MEMBER_ID] == 0x1f);
        CHECK(work[PE_RE_MEMBER_POS_X] == -32000);
    }
    pe_re_scd_exec(room, &run, 2, 10);  /* the shop's window event */
    int entered = 0, advanced = 0, fed = 0, released = 0;
    int last_x = 0, last_z = 0;
    for (int tick = 0; tick < 450; tick++) {
        pe_re_scd_tick(room, &flags, NULL, &run);
        const int16_t *work = run.works[6];  /* first zombie that attacks Kendo */
        if (run.camera == 9 && work[PE_RE_MEMBER_POS_X] > -30000) entered = 1;
        if (work[PE_RE_MEMBER_ROUTINE0] == 8 && run.scene[4].active && run.scene[4].move == 10) {
            if (run.scene[4].elapsed && (last_x != work[PE_RE_MEMBER_POS_X] || last_z != work[PE_RE_MEMBER_POS_Z]))
                advanced = 1;
            last_x = work[PE_RE_MEMBER_POS_X], last_z = work[PE_RE_MEMBER_POS_Z];
        }
        if (advanced && run.scene[4].active && run.scene[4].move == 21) fed = 1;
        if (fed && work[PE_RE_MEMBER_ROUTINE0] == 1 && !(work[PE_RE_MEMBER_TYPE] & 0x4000)) released = 1;
    }
    CHECK(entered && advanced && fed && released);
    free(room);
}

static void test_kendo_player_control(const char *store) {
    static PeReSession s;
    char why[128] = "";
    CHECK(!pe_re_session_start(&s, store, pe_re_game_named("re2"), 0, why, sizeof why));
    if (!s.game) return;
    if (pe_re_session_enter(&s, store, 0, 1, 0, -16000, -14000, 0, why, sizeof why)) {
        CHECK(0);
        pe_re_session_end(&s);
        return;
    }
    int locked = 0, approached = 0, arrived = 0, returned = 0, cut = 0, kendo_animated = 0;
    for (int tick = 0; tick < 1000; tick++) {
        const int moving = s.run.player_dest.moving;
        pe_re_session_step(&s, store, s.x, s.z, 0, why, sizeof why);
        if (pe_re_session_tick(&s) == PE_RE_CAMERA) cut = 1;
        if (pe_re_session_controls_locked(&s)) locked = 1;
        kendo_animated |= s.run.scene[0].active && s.run.scene[0].elapsed > 1;
        if (s.run.player_dest.moving) {
            CHECK(!s.run.arrived[0]);
            if (s.moved && hypot(s.x - s.run.player_dest.x, s.z - s.run.player_dest.z) > 500) approached = 1;
        } else if (moving && s.run.arrived[0]) {
            CHECK(s.x == s.run.player_dest.x && s.z == s.run.player_dest.z);
            arrived = 1;
        }
        s.moved = 0;
        if (locked && !pe_re_session_controls_locked(&s)) {
            returned = 1;
            break;
        }
    }
    CHECK(locked && approached && arrived && cut && returned && kendo_animated);
    pe_re_session_end(&s);
}

static void test_climb_camera_floors(const char *store) {
    for (int player = 0; player < 2; player++) {
        char id[PE_IMP_ID_MAX];
        pe_re_logic_id("re2", 0, 0x18, player, id, sizeof id);
        PeImpRoomLogic *room = NULL;
        CHECK(!pe_imp_catalog_roomlogic(store, id, &room, NULL, 0));
        if (!room) continue;
        /* The dumpster camera's two switch polygons both contain this point. */
        CHECK(pe_re_camera_at(room, 7, 0, -21000, -7000) == 7);
        CHECK(pe_re_camera_at(room, 8, 0, -21000, -7000) == 7);
        CHECK(pe_re_camera_at(room, 7, 1, -21000, -7000) == 8);
        CHECK(pe_re_camera_at(room, 8, 1, -21000, -7000) == 8);
        free(room);
    }
}

static void test_reciprocal_camera_overlap(const char *store) {
    static const struct {
        unsigned stage, room;
        int a, b, x, overlap_z, forward_z, reverse_z;
    } cases[] = {
        {0, 0x0a, 13, 14, -17400, 310, 1000, 0},
        {5, 0x03, 8, 10, -16410, -16490, -18530, -13210},
    };
    for (unsigned i = 0; i < sizeof cases / sizeof *cases; i++) {
        for (int player = 0; player < 2; player++) {
            char id[PE_IMP_ID_MAX];
            pe_re_logic_id("re2", cases[i].stage, cases[i].room, player, id, sizeof id);
            PeImpRoomLogic *room = NULL;
            CHECK(!pe_imp_catalog_roomlogic(store, id, &room, NULL, 0));
            if (!room) continue;
            const int a = cases[i].a, b = cases[i].b, x = cases[i].x, seam = cases[i].overlap_z;
            CHECK(pe_re_camera_at(room, a, 0, x, seam) == a);
            CHECK(pe_re_camera_at(room, a, 0, x, cases[i].forward_z) == b);
            CHECK(pe_re_camera_at(room, b, 0, x, seam) == b);
            CHECK(pe_re_camera_at(room, b, 0, x, cases[i].reverse_z) == a);
            free(room);
        }
    }
}

static void test_enemy_room_bindings(const char *store) {
    static PeReSession s;
    s.game = pe_re_game_named("re2");
    char asset[PE_IMP_ID_MAX];
    /* Kendo's room has player and enemy-slot-zero bindings in the same record. */
    s.room = 1;
    CHECK(!pe_imp_catalog_roomlogic(store, "re2:logic/101-0", &s.logic, NULL, 0));
    if (s.logic) {
        CHECK(!pe_re_session_player_room_move(&s, asset, sizeof asset));
        CHECK(!pe_re_session_enemy_room_move(&s, 0, asset, sizeof asset));
        CHECK(!strcmp(asset, "re2:roomanim/101-0-p0"));
        CHECK(pe_re_session_enemy_room_move(&s, 1, asset, sizeof asset) == -1);
        free(s.logic);
        s.logic = NULL;
    }
    /* The final Tyrant room binds player and partner. No enemy owns this bank. */
    s.stage = 6, s.room = 2;
    CHECK(!pe_imp_catalog_roomlogic(store, "re2:logic/702-0", &s.logic, NULL, 0));
    if (s.logic) {
        CHECK(pe_re_session_enemy_room_move(&s, 0, asset, sizeof asset) == -1);
        free(s.logic);
        s.logic = NULL;
    }
}

static void test_ground_enemy_assets(const char *store) {
    static const unsigned kinds[] = {0x24, 0x2e, 0x39, 0x25};
    for (unsigned i = 0; i < sizeof kinds / sizeof *kinds; i++) {
        char id[PE_IMP_ID_MAX], why[128];
        PeImpModel model = {0};
        PeImpAnimation animation = {0};
        snprintf(id, sizeof id, "re2:enemy/em%02x", kinds[i]);
        CHECK(!pe_imp_catalog_model(store, id, &model, why, sizeof why));
        snprintf(id, sizeof id, "re2:enemy/em%02x/moves", kinds[i]);
        CHECK(!pe_imp_catalog_animation(store, id, &animation, why, sizeof why));
        const unsigned walk = kinds[i] == 0x25 ? 2 : 1;
        CHECK(model.bones == animation.bones && animation.moves > walk && animation.move[walk].frames);
        for (unsigned b = 0; b < model.bones; b++)
            CHECK(model.parent[b] < (int)b && (!b || model.parent[b] >= 0));
        for (unsigned b = 0; b < model.parts && b < model.bones; b++)
            CHECK(!model.part[b].tri_skin && !model.part[b].quad_skin);
        pe_imp_model_free(&model);
        pe_imp_animation_free(&animation);
    }
}

static void test_ground_enemy_variants(void) {
    static PeReSession s;
    s.game = pe_re_game_named("re2");
    s.room = 1;
    const PeRe2RoomCollision room = {0};
    int16_t *work = s.run.works[2];
    work[0] = 1;
    static const unsigned kinds[] = {0x24, 0x2e, 0x39, 0x25};
    for (unsigned i = 0; i < sizeof kinds / sizeof *kinds; i++) {
        const unsigned kind = kinds[i];
        PeReEnemyPlacement enemy = {.kind = kind};
        work[PE_RE_MEMBER_ID] = kind;
        s.run.em_alerted[0] = 0;
        CHECK(!pe_re_session_chase(&s, &room, &enemy, 1, -2400, 0, 0));
        CHECK(!pe_re_session_chase(&s, &room, &enemy, 1, 2400, 0, 1));
        enemy.behaviour = 0x4000;
        CHECK(!pe_re_session_chase(&s, &room, &enemy, 1, 2400, 0, 0));
        enemy.behaviour = 0;
        work[PE_RE_MEMBER_ROUTINE0] = 8;
        CHECK(!pe_re_session_chase(&s, &room, &enemy, 1, 2400, 0, 0));
        work[PE_RE_MEMBER_ROUTINE0] = 1;
        for (int tick = 0; tick < 6; tick++)
            CHECK(pe_re_session_chase(&s, &room, &enemy, 1, 2400, 0, 0) == 1);
        CHECK(enemy.x > 0 && enemy.z == 0 && enemy.dir == 0);
        if (kind == 0x24 || kind == 0x25) {
            enemy.behaviour = kind == 0x25 ? 3 : 2;   /* mounted variants need their own routines */
            CHECK(!pe_re_session_chase(&s, &room, &enemy, 1, 2400, 0, 0));
        }
    }
}

int main(int argc, char **argv) {
    if (argc == 3 && !strcmp(argv[1], "--host-store")) return re_headless_smoke(argv[2], 0) != 0;
    test_ground_enemy_variants();
    const char *effect_store = getenv("PE_TEST_RE2_STORE");
    if (effect_store && *effect_store) {
        test_room_effects(effect_store);
        test_kendo_zombies(effect_store);
        test_re2_script_mask_groups(effect_store);
        test_kendo_player_control(effect_store);
        test_climb_camera_floors(effect_store);
        test_reciprocal_camera_overlap(effect_store);
        test_enemy_room_bindings(effect_store);
        test_ground_enemy_assets(effect_store);
    }
    char why[200] = "";

    {   /* Story triggers depend on the room's live work fields, including the train's parked flag. */
        PeReSession boss = {.game = pe_re_game_named("re2"), .stage = 4, .room = 9};
        int16_t *work = boss.run.works[2];
        work[0] = 1;
        work[PE_RE_MEMBER_ID] = 0x31;
        work[PE_RE_MEMBER_SCE_FREE0] = 1;
        CHECK(pe_re_session_ready_boss(&boss) == -1);
        work[PE_RE_MEMBER_SCE_FREE0] = 3;
        CHECK(pe_re_session_ready_boss(&boss) == -1);
        work[PE_RE_MEMBER_SCE_FREE0] = 4;
        CHECK(pe_re_session_ready_boss(&boss) == 0);
        work[PE_RE_MEMBER_ID] = 0x33;
        work[PE_RE_MEMBER_SCE_FREE0] = 3;
        CHECK(pe_re_session_ready_boss(&boss) == -1);
        work[PE_RE_MEMBER_SCE_FREE1] = 1;
        CHECK(pe_re_session_ready_boss(&boss) == 0);
        boss.stage = 5; boss.room = 0x17;
        work[PE_RE_MEMBER_ID] = 0x34;
        work[PE_RE_MEMBER_SCE_FREE0] = 0;
        CHECK(pe_re_session_ready_boss(&boss) == -1);
        boss.run.em_ready[0] = 1;
        CHECK(pe_re_session_ready_boss(&boss) == 0);
        CHECK(!pe_re_session_beaten(&boss, 0, 0xff) && pe_re_session_ready_boss(&boss) == -1);
        boss.stage = 6; boss.room = 4;
        boss.run.em_dead[0] = 0;
        work[PE_RE_MEMBER_ID] = 0x36;
        work[PE_RE_MEMBER_SCE_FREE0] = 1;
        work[PE_RE_MEMBER_TYPE] = (int16_t)0x8000;
        CHECK(pe_re_session_ready_boss(&boss) == -1);
        work[PE_RE_MEMBER_TYPE] = 0;
        CHECK(pe_re_session_ready_boss(&boss) == 0);
    }

    /* A script can replace or move a placement without changing the list length. */
    {
        const PeReEnemyPlacement original[] = {{.slot = 1, .kind = 3, .killed_flag = 8, .x = 20}};
        PeReEnemyPlacement updated[] = {original[0]};
        CHECK(pe_re_enemy_lists_equal(original, updated, 1));
        updated[0].x = 21;
        CHECK(!pe_re_enemy_lists_equal(original, updated, 1));
        updated[0] = original[0];
        updated[0].kind = 4;
        CHECK(!pe_re_enemy_lists_equal(original, updated, 1));
        CHECK(pe_re_enemy_lists_equal(original, updated, 0));
    }

    /* zones: a square and a slanted four-corner zone */
    int16_t x[4], z[4];
    rect(x, z, 0, 0, 1000, 1000);
    CHECK(pe_re_in_quad(x, z, 500, 500) && !pe_re_in_quad(x, z, 1500, 500) && !pe_re_in_quad(x, z, 500, -1));
    const int16_t sx[4] = {0, 1000, 1500, 500}, sz[4] = {0, 0, 1000, 1000};
    CHECK(pe_re_in_quad(sx, sz, 1200, 800) && !pe_re_in_quad(sx, sz, 100, 800));

    /* the game's zone check (pe_re_zone.h) on made-up zones */
    {
        double fx, fz;
        pe_re_front(0, 0, 0, &fx, &fz);
        CHECK(fx > -1 && fx < 1 && fz > -621 && fz < -619);    /* facing 0: -z */
        pe_re_front(0, 0, 1024, &fx, &fz);
        CHECK(fx > -621 && fx < -619 && fz > -1 && fz < 1);    /* 1024: -x */
        CHECK(pe_re_scd_keys(0x0040) == (1u << 6 | 1u << 7 | 1u << 12));   /* Cross */
        CHECK(pe_re_scd_keys(0x1000) == (1u << 0 | 1u << 4) && pe_re_scd_keys(0x0020) == 1u << 14);
        PeImpTrigger tz[4];
        memset(tz, 0, sizeof tz);
        tz[0].zone.id = 3, tz[0].zone.sce = PE_RE_ZONE_MESSAGE, tz[0].zone.sat = 0x31;      /* examine, in front */
        rect(tz[0].zone.x, tz[0].zone.z, 0, 0, 1000, 1000);
        tz[0].data[0] = 7;
        tz[1].zone.id = 1, tz[1].zone.sce = PE_RE_ZONE_NOTHING, tz[1].zone.sat = 0x31;      /* kind 0: only F_atari */
        rect(tz[1].zone.x, tz[1].zone.z, 0, 0, 1000, 1000);
        tz[2].zone.id = 4, tz[2].zone.sce = PE_RE_ZONE_EVENT, tz[2].zone.sat = 0x41;        /* walk-in, underfoot */
        rect(tz[2].zone.x, tz[2].zone.z, 0, 1000, 1000, 2000);
        tz[2].data[0] = 10, tz[2].data[3] = 2;
        tz[3].zone.id = 5, tz[3].zone.sce = PE_RE_ZONE_FLAG, tz[3].zone.sat = 0x41, tz[3].zone.floor = 1;   /* upstairs */
        rect(tz[3].zone.x, tz[3].zone.z, 0, 1000, 1000, 2000);
        PeImpLogicBuilder *zb = pe_imp_logic_begin(PE_IMP_RE2, 0, 0, 0);
        CHECK(zb && !pe_imp_logic_add_trigger(zb, tz, 4));
        PeImpRoomLogic *zl = pe_imp_logic_finish(zb);
        CHECK(zl != NULL);
        if (zl) {
            PeReProgress zp;
            PeReZoneHit hit[4];
            memset(&zp, 0, sizeof zp);
            int16_t *w = zp.flags.work;
            w[0] = w[1] = -1;
            /* standing in it facing away: the front point is outside; facing it from 620 off: a hit */
            CHECK(pe_re_zone_check(zl, NULL, &zp, 500, 500, 2048, 0, 1, w, hit, 1) == 0 && w[0] == -1);
            CHECK(pe_re_zone_check(zl, NULL, &zp, 500, 1100, 0, 0, 1, w, hit, 1) == 1 && hit[0].id == 3 &&
                  hit[0].data[0] == 7 && w[0] == 3);           /* zone 1 (kind 0) hit first, looked past */
            CHECK(pe_re_zone_check(zl, NULL, &zp, 500, 1100, 0, 0, 0, w, hit, 4) == 1 && hit[0].id == 4 && w[1] == 4);
            CHECK(pe_re_zone_check(zl, NULL, &zp, 500, 1500, 0, 1, 0, w, hit, 4) == 1 && hit[0].id == 5);   /* floor 1 */
            CHECK(pe_re_zone_check(zl, NULL, &zp, 500, 5000, 0, 0, 0, w, hit, 4) == 0);
            free(zl);
        }
    }

    /* the pocket (eight slots), losing items, the item box, using an item */
    {
        static PeReProgress ip;
        memset(&ip, 0, sizeof ip);
        for (uint16_t it = 1; it <= PE_RE_POCKET_SLOTS; it++) CHECK(!pe_re_give(&ip, it, 1));
        CHECK(pe_re_give(&ip, 99, 1) < 0 && !pe_re_give(&ip, 3, 5) && ip.held == PE_RE_POCKET_SLOTS);   /* full; stacks */
        CHECK(!pe_re_lose(&ip, 3, 2) && pe_re_has(&ip, 3) && ip.pocket[2].count == 4);
        CHECK(!pe_re_lose(&ip, 3, 0) && !pe_re_has(&ip, 3) && ip.held == 7 && ip.pocket[2].item == 4);   /* moved up */
        CHECK(pe_re_lose(&ip, 3, 0) < 0);
        CHECK(!pe_re_box_put(&ip, 0) && ip.boxed == 1 && ip.box[0].item == 1 && !pe_re_has(&ip, 1) && ip.held == 6);
        CHECK(!pe_re_give(&ip, 1, 2) && !pe_re_box_put(&ip, ip.held - 1) && ip.boxed == 1 && ip.box[0].count == 3);
        CHECK(!pe_re_box_take(&ip, 0) && ip.boxed == 0 && pe_re_has(&ip, 1) && pe_re_box_take(&ip, 0) < 0);
        while (ip.held < PE_RE_POCKET_SLOTS) pe_re_give(&ip, (uint16_t)(50 + ip.held), 1);
        ip.box[0].item = 77, ip.box[0].count = 1, ip.boxed = 1;
        CHECK(pe_re_box_take(&ip, 0) < 0 && ip.boxed == 1);   /* pocket full: stays in the box */
        static PeReSession us;
        memset(&us, 0, sizeof us);
        us.game = pe_re_game_named("re2");
        pe_re_give(&us.progress, 0x40, 1);
        us.progress.flags.work[PE_RE_WORK_USE_ID] = -1;
        CHECK(pe_re_session_use(&us, 0x40) == PE_RE_SAID && strstr(us.said, "can't use") &&
              us.progress.flags.work[PE_RE_WORK_USE_ID] == -1);
        pe_re_flag_set(&us.progress.flags, PE_RE_FG_USE, 0x40, 1);
        CHECK(pe_re_session_use(&us, 0x40) == PE_RE_STAY && us.progress.flags.work[PE_RE_WORK_USE_ID] == 0x40);
        CHECK(pe_re_session_use(&us, 0x41) == PE_RE_STAY && us.uses == 1);   /* not held: nothing */
    }

    /* a made-up room: camera 0 -> 1 past x 2000, back past x 1800; a door at the far end */
    PeImpLogicBuilder *b = pe_imp_logic_begin(PE_IMP_RE2, 0, 0, 0);
    PeImpCameraZone cz[4];                  /* as RE2 stores them: each camera's own area first */
    memset(cz, 0, sizeof cz);
    for (int i = 0; i < 4; i++) cz[i].flag = 0xff01;  /* enabled on every floor */
    cz[0].from = 0, cz[0].to = 0, rect(cz[0].x, cz[0].z, 0, 0, 2100, 1000);
    cz[1].from = 0, cz[1].to = 1, rect(cz[1].x, cz[1].z, 2000, 0, 4000, 1000);
    cz[2].from = 1, cz[2].to = 0, rect(cz[2].x, cz[2].z, 0, 0, 4000, 1000);   /* camera 1's area, not a switch */
    cz[3].from = 1, cz[3].to = 0, rect(cz[3].x, cz[3].z, 0, 0, 1800, 1000);
    PeImpDoor door;
    memset(&door, 0, sizeof door);
    rect(door.zone.x, door.zone.z, 3800, 0, 4000, 1000);
    door.stage = 14, door.room = 0xEE, door.camera = 2, door.next_x = 100, door.next_z = 200, door.next_dir = 1024;
    CHECK(b && !pe_imp_logic_add_camera_zone(b, cz, 4) && !pe_imp_logic_add_door(b, &door, 1));
    PeImpRoomLogic *l = pe_imp_logic_finish(b);
    CHECK(l != NULL);
    if (l) {
        CHECK(pe_re_camera_at(l, 0, 0, 1000, 500) == 0 && pe_re_camera_at(l, 0, 0, 2500, 500) == 1);
        CHECK(pe_re_camera_at(l, 1, 0, 1900, 500) == 1 && pe_re_camera_at(l, 1, 0, 1000, 500) == 0);   /* no flicker at the seam */
        CHECK(pe_re_camera_at(l, 1, 0, 3000, 500) == 1);          /* in camera 1's own area: stays */
        PeImpDoor d;
        CHECK(pe_re_door_at(l, NULL, 3900, 500, &d) == 0 && d.room == 0xEE && pe_re_door_at(l, NULL, 3000, 500, NULL) < 0);
        PeReSession s;                                          /* steps on a session holding it */
        memset(&s, 0, sizeof s);
        s.game = pe_re_game_named("re2"), s.logic = l;
        CHECK(pe_re_session_step(&s, s_store, 1000, 500, 0, why, sizeof why) == PE_RE_STAY && s.camera == 0);
        CHECK(pe_re_session_step(&s, s_store, 2500, 500, 0, why, sizeof why) == PE_RE_CAMERA && s.camera == 1);
        CHECK(pe_re_session_step(&s, s_store, 3900, 500, 0, why, sizeof why) == PE_RE_STAY);   /* no Cross */
        CHECK(pe_re_session_step(&s, s_store, 3900, 500, 1, why, sizeof why) == PE_RE_STAY && *why);   /* room F-EE: no such room */
        printf("made-up door: %s\n", why);
        pe_re_session_end(&s);                                  /* frees l */
    }

    /* rules on made-up records: a keyed door, a bolted pair, a pickup, a message */
    PeReProgress pr;
    memset(&pr, 0, sizeof pr);
    PeImpDoor kd;
    char say[200];
    memset(&kd, 0, sizeof kd);
    kd.lock_id = 138, kd.lock_type = 88;
    CHECK(pe_re_door_try(&pr, &kd, NULL, say, sizeof say) == PE_RE_DOOR_LOCKED && !strcmp(say, "It's locked. It needs the item 88."));
    CHECK(!pe_re_give(&pr, 88, 1) && pe_re_has(&pr, 88));
    CHECK(pe_re_door_try(&pr, &kd, NULL, say, sizeof say) == PE_RE_DOOR_UNLOCKED && strstr(say, "Used the"));
    CHECK(pe_re_door_try(&pr, &kd, NULL, say, sizeof say) == PE_RE_DOOR_OPEN && !*say);   /* stays open */
    PeImpDoor far = kd, near = kd;                              /* one lock, two sides */
    far.lock_id = near.lock_id = 131, far.lock_type = PE_RE_LOCK_OTHER_SIDE, near.lock_type = PE_RE_LOCK_BOLT;
    CHECK(pe_re_door_try(&pr, &far, NULL, say, sizeof say) == PE_RE_DOOR_LOCKED && strstr(say, "other side"));
    CHECK(pe_re_door_try(&pr, &near, NULL, say, sizeof say) == PE_RE_DOOR_UNLOCKED);
    CHECK(pe_re_door_try(&pr, &far, NULL, say, sizeof say) == PE_RE_DOOR_OPEN);
    b = pe_imp_logic_begin(PE_IMP_RE2, 0, 0, 5);
    PeImpPickup pk[2];
    PeImpTrigger tg;
    memset(pk, 0, sizeof pk), memset(&tg, 0, sizeof tg);
    rect(pk[0].zone.x, pk[0].zone.z, 0, 0, 500, 500), pk[0].item = 88, pk[0].count = 1, pk[0].flag = 4;
    pk[1] = pk[0];                                              /* the same pickup on another branch */
    rect(tg.zone.x, tg.zone.z, 1000, 0, 1500, 500), tg.zone.sce = PE_RE_SCE_MESSAGE, tg.zone.sat = 0x31, tg.data[0] = 1;
    static const char text[] = "\nFirst.\0A paper,#\nnothing useful.\0";
    CHECK(b && !pe_imp_logic_add_pickup(b, pk, 2) && !pe_imp_logic_add_trigger(b, &tg, 1) &&
          !pe_imp_logic_add(b, PE_IMP_LOGIC_TEXT, 2, text, sizeof text - 1));
    l = pe_imp_logic_finish(b);
    CHECK(l != NULL);
    if (l) {
        PeReProgress q;
        PeImpPickup got;
        memset(&q, 0, sizeof q);
        CHECK(pe_re_pickup_at(l, NULL, &q, 100, 100, &got) == 0 && got.item == 88);
        CHECK(!pe_re_take(&q, l, &got, NULL, say, sizeof say) && !strcmp(say, "Picked up the item 88.") && pe_re_has(&q, 88));
        CHECK(pe_re_pickup_at(l, NULL, &q, 100, 100, NULL) < 0);     /* both copies gone: same flag */
        CHECK(pe_re_message_at(l, NULL, 1200, 100) == 1 && pe_re_message_at(l, NULL, 600, 100) < 0);
        CHECK(!pe_re_message(l, 1, say, sizeof say) && !strcmp(say, "A paper, nothing useful."));
        CHECK(!pe_re_message(l, 0, say, sizeof say) && !strcmp(say, "First.") && pe_re_message(l, 2, say, sizeof say));
        free(l);
    }

    /* ramps: a slope rising toward +x to floor 1; stairs rising toward -z from floor 1 to floor 2 */
    static PeRe2RoomCollision ramps;
    memset(&ramps, 0, sizeof ramps);
    ramps.count = 3;
    ramps.entries[0] = (PeRe2Collider){0, 0, 1000, 500, 11, (uint16_t)(1u << 6 | 0u << 4), 1};
    ramps.entries[1] = (PeRe2Collider){2000, 0, 2500, 1000, 12, (uint16_t)(2u << 6 | 3u << 4), 2};
    ramps.entries[2] = (PeRe2Collider){-500, 0, -400, 500, 0, 0, 2, 0x8000, 3};   /* a wall on floor 1 only */
    double gy = 1;
    CHECK(pe_imp_re2_ground(&ramps, 0, 250, &gy) && gy == 0);
    CHECK(pe_imp_re2_ground(&ramps, 400, 250, &gy) && gy == -720 && pe_re_floor_of((int)gy) == 0);
    CHECK(pe_imp_re2_ground(&ramps, 500, 250, &gy) && gy == -900 && pe_re_floor_of((int)gy) == 1);   /* halfway: up, as the game rounds */
    CHECK(pe_imp_re2_ground(&ramps, 1000, 250, &gy) && gy == -1800 && pe_re_floor_of((int)gy) == 1);
    CHECK(pe_imp_re2_ground(&ramps, 2200, 1000, &gy) && gy == -1800);   /* stairs' bottom: their lowest floor */
    CHECK(pe_imp_re2_ground(&ramps, 2200, 0, &gy) && gy == -3600 && pe_re_floor_of((int)gy) == 2);
    CHECK(!pe_imp_re2_ground(&ramps, 1500, 250, &gy));
    CHECK(!pe_imp_re2_blocked_on(&ramps, 0, 500, 250, 50));           /* ramps never block */
    CHECK(pe_imp_re2_blocked_on(&ramps, 1, -450, 250, 50) && !pe_imp_re2_blocked_on(&ramps, 0, -450, 250, 50));

    /* Field chase uses the encounter snapshot and the same floor-filtered walls as walking. */
    PeReSession chase = {.game = pe_re_game_named("re2"), .stage = 1};
    PeReEnemyPlacement foes[] = {
        {.slot = 0, .kind = 0x1f, .killed_flag = 1},
        {.slot = 1, .kind = 0x20, .killed_flag = 2, .behaviour = 2, .z = 400},
        {.slot = 2, .kind = 0x1f, .killed_flag = 3, .floor = 1, .y = -1800},
        {.slot = 3, .kind = 0x1f, .killed_flag = 4, .behaviour = 2},
        {.slot = 4, .kind = 0x1f, .killed_flag = 5, .behaviour = 0x4000},
        {.slot = 5, .kind = 0x1f, .killed_flag = 6},
    };
    for (unsigned i = 0; i < sizeof foes / sizeof *foes; i++) {
        chase.run.works[2 + i][0] = 1;
        chase.run.works[2 + i][PE_RE_MEMBER_ID] = foes[i].kind;
    }
    chase.run.task[2].status = 1;
    chase.run.task[2].work = 7;
    static PeRe2RoomCollision chase_room = {
        .count = 1, .entries = {{600, -5000, 660, 5000, 0, 0, 1, 0x8000, 0}}
    };
    CHECK(pe_re_session_chase(&chase, &chase_room, foes, 6, 2300, 0, 0) == 2);
    CHECK(foes[0].x > 0 && foes[1].x > foes[0].x && foes[0].dir == 0);
    CHECK(foes[2].x == 0 && foes[3].x == 0 && foes[4].x == 0 && foes[5].x == 0);
    CHECK(chase.run.works[2][PE_RE_MEMBER_POS_X] == foes[0].x);
    for (int tick = 0; tick < 64; tick++) pe_re_session_chase(&chase, &chase_room, foes, 6, 2300, 0, 0);
    CHECK(foes[0].x < 360 && foes[1].x < 390);  /* each body stops short of the wall */
    pe_imp_re2_collision_set_id(&chase_room, 0, 0);  /* the room script opens it */
    for (int tick = 0; tick < 64; tick++) pe_re_session_chase(&chase, &chase_room, foes, 6, 2300, 0, 0);
    CHECK(foes[0].x > 660 && foes[1].x > 660);
    CHECK(pe_re_session_chase(&chase, &chase_room, foes, 6, 2300, 0, 1) == 1);
    CHECK(foes[2].x > 0 && foes[2].y == -1800);

    /* Use the behaviour words from the opening street's room script. */
    chase.stage = 0;
    foes[4].behaviour = 0x1040;
    foes[5].behaviour = 0x10c6;
    chase.run.task[2].status = 0;
    CHECK(pe_re_session_chase(&chase, &chase_room, foes + 4, 2, 2300, 0, 0) == 2);
    foes[5].behaviour = 0x50c2;  /* the prone zombie stays down */
    const int prone_x = foes[5].x;
    CHECK(pe_re_session_chase(&chase, &chase_room, foes + 4, 2, 2300, 0, 0) == 1);
    CHECK(foes[5].x == prone_x);
    CHECK(chase.enemy_state[4] == PE_RE_ENEMY_MOVING && chase.enemy_state[5] == PE_RE_ENEMY_PRONE);

    chase.room = 1;
    foes[4].behaviour = 0;
    chase.run.works[6][PE_RE_MEMBER_ROUTINE0] = 8;
    CHECK(!pe_re_session_chase(&chase, &chase_room, foes + 4, 1, 2300, 0, 0));
    CHECK(chase.enemy_state[4] == PE_RE_ENEMY_SCRIPTED);
    chase.run.works[6][PE_RE_MEMBER_ROUTINE0] = 1;
    CHECK(pe_re_session_chase(&chase, &chase_room, foes + 4, 1, 2300, 0, 0) == 1);
    const int before_cut = foes[4].x;
    chase.camera++;
    CHECK(pe_re_session_chase(&chase, &chase_room, foes + 4, 1, 2300, 0, 0) == 1);
    CHECK(foes[4].x > before_cut && chase.run.works[6][PE_RE_MEMBER_POS_X] == foes[4].x);
    const int before_idle = foes[4].x;
    const double stationary_aya = before_idle + 3800; /* outside acquisition, inside retention */
    for (int tick = 0; tick < 3; tick++)
        CHECK(pe_re_session_chase(&chase, &chase_room, foes + 4, 1, stationary_aya, 0, 0) == 1);
    CHECK(foes[4].x > before_idle && chase.run.em_alerted[foes[4].slot]);

    chase.stage = 1;
    foes[4].kind = 0x22;
    chase.run.works[6][PE_RE_MEMBER_ID] = 0x22;
    foes[4].behaviour = 0x4510;
    CHECK(!pe_re_session_chase(&chase, &chase_room, foes + 4, 1, 3100, 0, 0));
    foes[4].behaviour = 0x0510;
    CHECK(pe_re_session_chase(&chase, &chase_room, foes + 4, 1, 3100, 0, 0) == 1);

    /* An idle zombie does not acquire Aya from behind on distance alone.
     * After its idle check, the first turn is bounded and the step is forward. */
    foes[4].kind = 0x1f, foes[4].behaviour = 0, foes[4].dir = 0;
    chase.run.works[6][PE_RE_MEMBER_ID] = 0x1f;
    chase.run.em_alerted[4] = 0;
    chase.run.em_idle_ticks[4] = 0;
    const int zombie_x = foes[4].x;
    CHECK(!pe_re_session_chase(&chase, &chase_room, foes + 4, 1, zombie_x - 2500, 0, 0));
    CHECK(foes[4].x == zombie_x && foes[4].dir == 0);
    chase.run.em_idle_ticks[4] = 299;
    CHECK(pe_re_session_chase(&chase, &chase_room, foes + 4, 1, zombie_x - 2500, 0, 0) == 1);
    CHECK(foes[4].dir == 4096 - 24 && foes[4].x > zombie_x);

    /* A no-defeat-flag crawler animates and advances as field scenery. */
    foes[4].behaviour = 0x2003, foes[4].killed_flag = 0xff, foes[4].dir = 0;
    CHECK(!pe_re2_proximity_enemy(&foes[4], 0));
    CHECK(pe_re_session_chase(&chase, &chase_room, foes + 4, 1, foes[4].x + 2400, 0, 0) == 1);
    CHECK(chase.enemy_state[4] == PE_RE_ENEMY_MOVING);
    foes[4].behaviour = 5;
    CHECK(!pe_re_session_chase(&chase, &chase_room, foes + 4, 1, 2300, 0, 0));
    CHECK(chase.enemy_state[4] == PE_RE_ENEMY_CORPSE);
    foes[4].behaviour = 8;
    CHECK(!pe_re_session_chase(&chase, &chase_room, foes + 4, 1, 2300, 0, 0));
    CHECK(chase.enemy_state[4] == PE_RE_ENEMY_FEEDING);
    foes[4].behaviour = 0x0a;
    CHECK(!pe_re_session_chase(&chase, &chase_room, foes + 4, 1, 2300, 0, 0));
    CHECK(chase.enemy_state[4] == PE_RE_ENEMY_VARIANT);

    char id[PE_IMP_ID_MAX];
    pe_re_logic_id("re2", 0, 0x0a, 1, id, sizeof id);
    CHECK(!strcmp(id, "re2:logic/10a-1"));
    pe_re_logic_id("re3", 1, 0x10, 0, id, sizeof id);
    CHECK(!strcmp(id, "re3:logic/210"));
    pe_re_room_id("re1", 0, 6, id, sizeof id);
    CHECK(!strcmp(id, "re1:room/106"));
    CHECK(pe_re_game_named("re2")->playable && !pe_re_game_named("re1")->playable && !pe_re_game_named("dc1"));

    CHECK(!re_headless_smoke(NULL, 0));
    const char *re2 = getenv("PE_TEST_RE2_DISC");
    if (!re2 || !*re2) {
        printf("PE_TEST_RE2_DISC not set: the RE2 walk skipped\n");
        printf(s_fail ? "re session FAILED\n" : "re session passed\n");
        return s_fail;
    }
    PeImpScan sc;
    pe_imp_registry_reset();
    CHECK(!pe_imp_add_reader(&g_re2_logic_ps1) && !pe_imp_add_reader(&g_re2_rooms_ps1) && !pe_imp_add_reader(&g_re2_items_ps1) &&
          !pe_imp_add_reader(&g_re2_door_ps1));
    CHECK(!pe_imp_set_naming("re2", g_re2_naming));
    CHECK(!pe_imp_scan(s_store, re2, NULL, &sc));
    const PeReGame *g = pe_re_game_named("re2");
    CHECK(pe_re_imported(s_store, g) && !pe_re_imported(s_store, pe_re_game_named("re3")));

    PeReSession probe;
    const unsigned scenario = pe_re_session_start(&probe, s_store, g, 0, why, sizeof why) != 0;
    pe_re_session_end(&probe);
    CHECK(!re_headless_smoke(s_store, scenario));
    test_scene_actors(scenario);
    test_mr_x(g, scenario);
    test_licker(g, scenario);
    test_licker_variants(g, scenario);
    test_adult_g(g, scenario);
    test_cut_replace(g, scenario);
    test_alligator(g, scenario);
    test_lift_boss(g, scenario);
    test_final_tyrant(g, scenario);

    /* every room reachable through doors from the street, breadth first */
    static uint32_t queue[ROOMS_MAX];
    static uint8_t seen[16][256];
    int head = 0, tail = 0, doors = 0, missing = 0, no_camera = 0, blocked = 0, stuck = 0, facing_known = 0, facing_re = 0, facing_stage = 0;
    PeReSession s;
    CHECK(!pe_re_session_start(&s, s_store, g, scenario, why, sizeof why));
    pe_re_session_end(&s);
    queue[tail++] = g->stage << 8 | g->room, seen[g->stage][g->room] = 1;
    while (head < tail) {
        const uint32_t stage = queue[head] >> 8, room = queue[head] & 0xFF;
        head++;
        char lid[PE_IMP_ID_MAX];
        PeImpRoomLogic *rl = NULL;
        pe_re_logic_id("re2", stage, room, scenario, lid, sizeof lid);
        if (pe_imp_catalog_roomlogic(s_store, lid, &rl, NULL, 0)) continue;
        uint32_t n = 0;
        pe_imp_logic_section(rl, PE_IMP_LOGIC_DOOR, &n, NULL);
        for (uint32_t i = 0; i < n; i++) {
            PeImpDoor d;
            if (pe_imp_logic_door(rl, i, &d)) continue;
            doors++;
            const uint32_t ts = d.stage == PE_RE_STAGE_HERE ? stage : d.stage;
            if (ts >= 16) { missing++; continue; }
            PeReSession t;                                  /* take the door as the runtime does */
            memset(&t, 0, sizeof t);
            t.game = g, t.player = scenario;
            if (pe_re_session_enter(&t, s_store, ts, d.room, d.camera, d.next_x, d.next_z, d.next_dir, NULL, 0)) {
                missing++;
                continue;
            }
            char rid[PE_IMP_ID_MAX];
            PeImpImageSet *set = NULL;
            pe_re_room_id("re2", ts, d.room, rid, sizeof rid);
            if (pe_imp_catalog_imageset(s_store, rid, &set, NULL, 0) || d.camera >= set->count) no_camera++;
            uint32_t coll = 0;
            const uint8_t *data = pe_imp_logic_section(t.logic, PE_IMP_LOGIC_COLL, NULL, &coll);
            PeRe2RoomCollision c;
            if (data && !pe_imp_re2_collision_all(data, coll, &c)) {
                const int fl = pe_re_floor_of(d.next_y);
                double fx = d.next_x, fz = d.next_z;
                if (pe_imp_re2_blocked_on(&c, (unsigned)fl, fx, fz, 280)) blocked++;
                if (!pe_re_free_spot(&c, fl, &fx, &fz, 280, 2000)) {      /* as the stage places Aya */
                    stuck++;
                    if (getenv("PE_TEST_RE_STUCK"))
                        printf("  stuck: %x%02x door %u -> %x%02x at %d,%d (y %d), %u colliders\n", stage + 1, room, i,
                               ts + 1, d.room, d.next_x, d.next_z, d.next_y, c.count);
                }
            }
            {   /* which way she faces on landing: away from the door back, into the room */
                uint32_t nb = 0;
                double best = 1e18, vx = 0, vz = 0;
                pe_imp_logic_section(t.logic, PE_IMP_LOGIC_DOOR, &nb, NULL);
                for (uint32_t k = 0; k < nb; k++) {
                    PeImpDoor b;
                    if (pe_imp_logic_door(t.logic, k, &b) || b.room != room) continue;
                    double bx = 0, bz = 0;
                    for (int c4 = 0; c4 < 4; c4++) bx += b.zone.x[c4] / 4.0, bz += b.zone.z[c4] / 4.0;
                    const double dd = (bx - d.next_x) * (bx - d.next_x) + (bz - d.next_z) * (bz - d.next_z);
                    if (dd < best) best = dd, vx = d.next_x - bx, vz = d.next_z - bz;
                }
                if (best < 1e18 && (vx || vz)) {
                    const double a = (d.next_dir & 4095) * (acos(-1.0) / 2048.0);
                    facing_known++;
                    facing_re += vx * cos(a) - vz * sin(a) > 0;          /* RE2's Rot_vector: 0 faces +x */
                    const double y = pe_re_facing_of(d.next_dir) * (acos(-1.0) / 2048.0);
                    facing_stage += -vx * sin(y) - vz * cos(y) > 0;      /* as the session turns her: 0 faces -z */
                }
            }
            free(set);
            pe_re_session_end(&t);
            if (!seen[ts][d.room] && tail < ROOMS_MAX) seen[ts][d.room] = 1, queue[tail++] = ts << 8 | d.room;
        }
        free(rl);
    }
    /* every door's sound bank is in the store (as the importer files them) */
    {
        PeImpAsset *assets = NULL;
        const int na = pe_imp_catalog_assets(s_store, "re2", &assets);
        int with = 0, without = 0, waves[4] = {0};
        for (uint32_t st = 0; st < 7; st++)
            for (uint32_t rm = 0; rm < 64; rm++) {
                char lid[PE_IMP_ID_MAX];
                PeImpRoomLogic *rl = NULL;
                pe_re_logic_id("re2", st, rm, scenario, lid, sizeof lid);
                if (pe_imp_catalog_roomlogic(s_store, lid, &rl, NULL, 0)) continue;
                uint32_t n = 0;
                pe_imp_logic_section(rl, PE_IMP_LOGIC_DOOR, &n, NULL);
                for (uint32_t i = 0; i < n; i++) {
                    PeImpDoor d;
                    char sid[PE_IMP_ID_MAX];
                    int found = 0;
                    if (pe_imp_logic_door(rl, i, &d)) continue;
                    for (unsigned w = 1; w <= 4; w++) {
                        pe_re_door_sound_id("re2", &d, w, sid, sizeof sid);
                        for (int a = 0; a < na; a++) if (!strcmp(assets[a].id, sid)) { found = 1, waves[w - 1]++; break; }
                    }
                    found ? with++ : without++;
                }
                free(rl);
            }
        free(assets);
        printf("door sounds: %d doors with their bank, %d without; waves 1-4 present %d %d %d %d\n", with, without,
               waves[0], waves[1], waves[2], waves[3]);
        CHECK(with > 0 && without * 10 <= with);
    }
    printf("door landings facing into the room: %d of %d by RE2's angle, %d by the stage's\n", facing_re, facing_known,
           facing_stage);
    printf("RE2 from the street: %d rooms reached, %d doors, %d to rooms not imported, %d without their camera, "
           "%d onto blocked floor, %d with no free floor near\n", tail, doors, missing, no_camera, blocked, stuck);
    CHECK(tail >= 40 && doors >= 100);
    CHECK(facing_known > 100 && facing_stage == facing_re && facing_stage * 10 >= facing_known * 9);

    /* The detailed key, script and placement expectations below describe Leon A. */
    if (scenario) {
        CHECK(missing * 20 <= doors && no_camera == 0 && stuck * 20 <= doors);
        printf(s_fail ? "Claire room walk FAILED\n" : "Claire room walk passed\n");
        return s_fail;
    }

    /* RE2's own rules: 1-05 holds the Cabin Key and the door to 1-06 that needs it */
    PeReSession r;
    CHECK(!pe_re_session_start(&r, s_store, g, 0, why, sizeof why) && r.items);
    if (r.logic && !pe_re_session_enter(&r, s_store, 0, 0x05, 0, 0, 0, 0, why, sizeof why)) {
        PeImpPickup key;
        PeImpDoor cabin;
        int ki = -1, di = -1;
        uint32_t n = 0;
        pe_imp_logic_section(r.logic, PE_IMP_LOGIC_PICK, &n, NULL);
        for (uint32_t i = 0; i < n && ki < 0; i++) if (!pe_imp_logic_pickup(r.logic, i, &key) && key.item == 88) ki = (int)i;
        pe_imp_logic_section(r.logic, PE_IMP_LOGIC_DOOR, &n, NULL);
        for (uint32_t i = 0; i < n && di < 0; i++) if (!pe_imp_logic_door(r.logic, i, &cabin) && cabin.lock_type == 88) di = (int)i;
        CHECK(ki >= 0 && di >= 0);
        if (ki >= 0 && di >= 0) {
            double dx = 0, dz = 0, kx = 0, kz = 0;
            for (int k = 0; k < 4; k++) dx += cabin.zone.x[k] / 4.0, dz += cabin.zone.z[k] / 4.0, kx += key.zone.x[k] / 4.0, kz += key.zone.z[k] / 4.0;
            CHECK(pe_re_session_step(&r, s_store, dx, dz, 1, why, sizeof why) == PE_RE_SAID);
            printf("1-05 door: %s\n", r.said);
            CHECK(!strcmp(r.said, "It's locked. It needs the Cabin Key."));
            CHECK(pe_re_session_step(&r, s_store, kx, kz, 1, why, sizeof why) == PE_RE_SAID);
            printf("1-05 pickup: %s\n", r.said);
            CHECK(!strcmp(r.said, "Picked up the Cabin Key.") && pe_re_has(&r.progress, 88));
            CHECK(pe_re_session_step(&r, s_store, kx, kz, 1, why, sizeof why) != PE_RE_SAID || strcmp(r.said, "Picked up the Cabin Key."));
            CHECK(pe_re_session_step(&r, s_store, dx, dz, 1, why, sizeof why) == PE_RE_ROOM && r.room == 0x06);
            printf("1-05 door again: %s -> room %x-%02X\n", r.said, (unsigned)r.stage + 1, (unsigned)r.room);
            CHECK(!strcmp(r.said, "Used the Cabin Key."));
        }
    }
    if (!pe_re_session_enter(&r, s_store, 0, 0x01, 0, 0, 0, 0, why, sizeof why)) {   /* 1-01's papers */
        char m[200];
        CHECK(!pe_re_message(r.logic, 13, m, sizeof m) && !strcmp(m, "Paper dated Sep. 18th. It looks irrelevant."));
        CHECK(!pe_re_message(r.logic, 19, m, sizeof m) && !strcmp(m, "Gun cases and related paraphernalia."));
        CHECK(r.run.ran && r.run.nlive > 0);
    }
    /* 1-05 again: the Cabin Key was taken, so its FG_ITEM flag is set: the script still runs its
     * item_aot_set (RE2 doesn't guard them), and the flag keeps it from being placed, as the engine does */
    if (!pe_re_session_enter(&r, s_store, 0, 0x05, 0, 0, 0, 0, why, sizeof why)) {
        uint32_t n = 0;
        int placed = 0, found = 0;
        pe_imp_logic_section(r.logic, PE_IMP_LOGIC_PICK, &n, NULL);
        for (uint32_t i = 0; i < n; i++) {
            PeImpPickup pk, again;
            if (pe_imp_logic_pickup(r.logic, i, &pk) || pk.item != 88) continue;
            placed += pe_re_scd_live(&r.run, pk.at);
            double kx = 0, kz = 0;
            for (int k = 0; k < 4; k++) kx += pk.zone.x[k] / 4.0, kz += pk.zone.z[k] / 4.0;
            found += pe_re_pickup_at(r.logic, &r.run, &r.progress, kx, kz, &again) >= 0 && again.item == 88;
        }
        printf("1-05 after the key: its item_aot_set ran %d times, pickable %d\n", placed, found);
        CHECK(r.run.ran && placed >= 1 && !found && pe_re_flag(&r.progress.flags, PE_RE_FG_ITEM, 4));
    }
    /* saved and loaded: the bytes round-trip, and a resumed session is where it was, with what it had */
    if (r.game) {
        static uint8_t bytes[PE_RE_SAVE_SIZE], again[PE_RE_SAVE_SIZE];
        PeReSaved saved;
        PeReSession back;
        r.x = 1234, r.z = -5678, r.facing = 1024, r.camera = 0;
        r.progress.box[0].item = 0x40, r.progress.box[0].count = 2, r.progress.boxed = 1;   /* v2: the box, work */
        r.progress.flags.work[20] = 1234;
        pe_re_save_encode(&r, 1, bytes);
        {
            static uint8_t up[PE_RE_SAVE_SIZE];
            PeReSaved old;
            CHECK(!pe_re_save_upgrade(up, bytes, PE_RE_SAVE_SIZE_V1, 1) && !pe_re_save_decode(up, &old) &&
                  old.room == 0x05 && pe_re_has(&old.progress, 88) && !old.progress.boxed && !old.progress.flags.work[20]);
            CHECK(pe_re_save_upgrade(up, bytes, PE_RE_SAVE_SIZE_V1, 3) < 0);
        }
        CHECK(!pe_re_save_decode(bytes, &saved) && saved.game == g && saved.playing && saved.room == 0x05 &&
              saved.x == 1234 && saved.z == -5678 && saved.facing == 1024 && pe_re_has(&saved.progress, 88));
        CHECK(!pe_re_session_resume(&back, s_store, &saved, why, sizeof why) && back.room == 0x05 && back.x == 1234);
        CHECK(pe_re_has(&back.progress, 88) && pe_re_flag(&back.progress.flags, PE_RE_FG_ITEM, 4));
        CHECK(!memcmp(back.progress.unlocked, r.progress.unlocked, sizeof r.progress.unlocked));
        CHECK(back.progress.boxed == 1 && back.progress.box[0].item == 0x40 && back.progress.box[0].count == 2 &&
              back.progress.flags.work[20] == 1234);
        back.progress.flags.work[PE_RE_WORK_RANDOM] = r.progress.flags.work[PE_RE_WORK_RANDOM];   /* re-rolled on entry */
        pe_re_save_encode(&back, 1, again);
        CHECK(!memcmp(bytes, again, sizeof bytes));               /* stable */
        PeImpDoor cabin;                                          /* the cabin door stays open */
        uint32_t n = 0;
        int di = -1;
        pe_imp_logic_section(back.logic, PE_IMP_LOGIC_DOOR, &n, NULL);
        for (uint32_t i = 0; i < n && di < 0; i++) if (!pe_imp_logic_door(back.logic, i, &cabin) && cabin.lock_type == 88) di = (int)i;
        if (di >= 0) {
            double dx = 0, dz = 0;
            for (int k = 0; k < 4; k++) dx += cabin.zone.x[k] / 4.0, dz += cabin.zone.z[k] / 4.0;
            CHECK(pe_re_session_step(&back, s_store, dx, dz, 1, why, sizeof why) == PE_RE_ROOM && back.room == 0x06 && !back.said[0]);
        }
        printf("saved %d bytes; resumed in %x-%02X with the Cabin Key, its door open\n", PE_RE_SAVE_SIZE,
               (unsigned)saved.stage + 1, (unsigned)saved.room);
        pe_re_session_end(&back);
        memset(bytes, 0, sizeof bytes);
        CHECK(!pe_re_save_decode(bytes, &saved) && !saved.game);  /* a save without a game */
        bytes[0] = 9;
        CHECK(pe_re_save_decode(bytes, &saved) < 0);
    }
    pe_re_session_end(&r);

    /* using an item where a room takes it, end to end: 1-08's valve spot. Standing facing it, the
     * every-frame zone check sets its Use_flg bit and F_atari; using the item sets Use_id; the next
     * frame's handler (main procedure 1) sees both and starts the room's event, which switches the
     * spot off (aot_reset). Away from it, the item can't be used. */
    {
        static PeReSession v;
        if (!pe_re_session_start(&v, s_store, pe_re_game_named("re2"), 0, why, sizeof why) &&
            !pe_re_session_enter(&v, s_store, 0, 0x08, 0, 0, 0, 0, why, sizeof why)) {
            uint32_t n = 0;
            PeImpTrigger spot;
            int have = 0;
            pe_imp_logic_section(v.logic, PE_IMP_LOGIC_TRIG, &n, NULL);
            for (uint32_t i = 0; i < n && !have; i++)
                if (!pe_imp_logic_trigger(v.logic, i, &spot) && pe_re_scd_live(&v.run, spot.at) &&
                    spot.zone.sce == PE_RE_ZONE_FLAG && (spot.data[0] | spot.data[1] << 8) == PE_RE_FG_USE)
                    have = 1;
            CHECK(have);
            if (have) {
                const uint16_t item = (uint16_t)(spot.data[2] | spot.data[3] << 8);
                double cx = 0, cz = 0;
                for (int c = 0; c < 4; c++) cx += spot.zone.x[c] / 4.0, cz += spot.zone.z[c] / 4.0;
                pe_re_give(&v.progress, item, 1);
                for (int f = 0; f < 30; f++) pe_re_session_tick(&v);     /* the room settles */
                v.facing = 0, v.floor = 0;
                pe_re_session_step(&v, s_store, cx, cz + 3000, 0, why, sizeof why);   /* far off */
                CHECK(pe_re_session_use(&v, item) == PE_RE_SAID);
                pe_re_session_tick(&v);
                pe_re_session_step(&v, s_store, cx, cz + PE_RE_FRONT_REACH, 0, why, sizeof why);   /* facing it */
                CHECK(v.progress.flags.work[PE_RE_WORK_F_ATARI] == spot.zone.id && pe_re_flag(&v.progress.flags, PE_RE_FG_USE, item));
                const int resets = v.run.nreset;
                CHECK(pe_re_session_use(&v, item) == PE_RE_STAY);
                int cuts = 0, held = 0;
                for (int f = 0; f < 5; f++) cuts += pe_re_session_tick(&v) == PE_RE_CAMERA;
                CHECK(v.run.nreset > resets && pe_re_zone_kind(&v.run, spot.zone.id, spot.zone.sce, NULL) == 0);
                CHECK(v.progress.flags.work[PE_RE_WORK_USE_ID] == -1);   /* cleared after the frame */
                for (int f = 0; f < 600; f++) {                  /* the event plays: its camera cuts */
                    cuts += pe_re_session_tick(&v) == PE_RE_CAMERA;
                    held += v.run.camera_lock;
                }
                CHECK(cuts >= 2 && held > 0);
                printf("1-08 item %u used at zone %u: %d zones switched by its event, %d camera cuts, last camera %d\n",
                       (unsigned)item, (unsigned)spot.zone.id, v.run.nreset - resets, cuts, v.camera);
            }
        } else {
            printf("1-08: %s\n", why);
            CHECK(0);
        }
        pe_re_session_end(&v);
    }

    /* enemies: the street's placements reported; one beaten stays gone when she comes back */
    {
        static PeReSession v;
        PeReEnemyPlacement e[PE_RE_ENEMIES_MAX];
        if (!pe_re_session_start(&v, s_store, pe_re_game_named("re2"), 0, why, sizeof why)) {
            for (int f = 0; f < 30; f++) pe_re_session_tick(&v);
            const int n = pe_re_session_enemies(&v, e, PE_RE_ENEMIES_MAX);
            int flagged = -1;
            for (int i = 0; i < n && flagged < 0; i++) if (e[i].killed_flag != 0xFF) flagged = i;
            printf("RE2 street: %d enemies placed (first kind 0x%02X at %d,%d)\n", n, n ? e[0].kind : 0, n ? e[0].x : 0,
                   n ? e[0].z : 0);
            CHECK(n > 0 && flagged >= 0);
            if (flagged >= 0) {
                const uint8_t flag = e[flagged].killed_flag;
                CHECK(!pe_re_session_defeated(&v, flag) && pe_re_session_enemies(&v, e, PE_RE_ENEMIES_MAX) == n - 1);
                CHECK(!pe_re_session_enter(&v, s_store, v.stage, v.room, 0, v.x, v.z, 0, why, sizeof why));
                for (int f = 0; f < 30; f++) pe_re_session_tick(&v);
                int back = 0;
                const int m = pe_re_session_enemies(&v, e, PE_RE_ENEMIES_MAX);
                for (int i = 0; i < m; i++) back |= e[i].killed_flag == flag;
                CHECK(m == n - 1 && !back);
                uint8_t saved[PE_RE_SAVE_SIZE];
                PeReSaved decoded;
                pe_re_save_encode(&v, 1, saved);
                const int decoded_ok = !pe_re_save_decode(saved, &decoded);
                CHECK(decoded_ok);
                pe_re_session_end(&v);
                const int resumed = decoded_ok ? pe_re_session_resume(&v, s_store, &decoded, why, sizeof why) : -1;
                CHECK(!resumed);
                if (!resumed) {
                    for (int f = 0; f < 30; f++) pe_re_session_tick(&v);
                    const int remaining = pe_re_session_enemies(&v, e, PE_RE_ENEMIES_MAX);
                    CHECK(remaining == n - 1);
                    for (int i = 0; i < remaining; i++) CHECK(e[i].killed_flag != flag);
                }
            }
            CHECK(pe_re_session_defeated(&v, 0xFF) < 0);
        }
        pe_re_session_end(&v);
    }

    /* a typewriter and an item box (2-08): examined from in front, they're handed to the mod */
    {
        static PeReSession v;
        if (!pe_re_session_start(&v, s_store, pe_re_game_named("re2"), 0, why, sizeof why) &&
            !pe_re_session_enter(&v, s_store, 1, 0x08, 0, 0, 0, 0, why, sizeof why)) {
            for (int f = 0; f < 30; f++) pe_re_session_tick(&v);
            const int want[2] = {PE_RE_ZONE_SAVE, PE_RE_ZONE_ITEMBOX};
            for (int w = 0; w < 2; w++) {
                uint32_t n = 0;
                PeImpTrigger tz;
                int have = 0;
                pe_imp_logic_section(v.logic, PE_IMP_LOGIC_TRIG, &n, NULL);
                for (uint32_t i = 0; i < n && !have; i++)
                    have = !pe_imp_logic_trigger(v.logic, i, &tz) && pe_re_scd_live(&v.run, tz.at) &&
                           pe_re_zone_kind(&v.run, tz.zone.id, tz.zone.sce, NULL) == want[w];
                CHECK(have);
                if (!have) continue;
                double cx = 0, cz = 0;
                for (int c = 0; c < 4; c++) cx += tz.zone.x[c] / 4.0, cz += tz.zone.z[c] / 4.0;
                v.facing = 0, v.floor = 0;
                pe_re_session_tick(&v);
                const PeReChange ch = pe_re_session_step(&v, s_store, cx, cz + PE_RE_FRONT_REACH, 1, why, sizeof why);
                printf("2-08 kind %d zone %u (sat %02x floor %02x super %02x): change %d, zone kind %u\n", want[w],
                       (unsigned)tz.zone.id, (unsigned)tz.zone.sat, (unsigned)tz.zone.floor, (unsigned)tz.zone.super,
                       (int)ch, (unsigned)v.zone.sce);
                CHECK(ch == PE_RE_ZONE && v.zone.sce == want[w]);
            }
        } else {
            printf("2-08: %s\n", why);
            CHECK(0);
        }
        pe_re_session_end(&v);
    }

    /* every room's scripts, as a new game (Leon A): what ran, what was skipped */
    PeReProgress fresh;
    int rooms = 0, ran = 0, live_doors = 0, all_doors = 0, live_items = 0, all_items = 0;
    long executed = 0, skipped = 0, stopped = 0;
    int placed = 0, enem_records = 0;
    int kinds[16] = {0}, manual = 0, events = 0, events_found = 0, events_started = 0, use_zones = 0, use_found = 0;
    for (uint32_t st = 0; st < 7; st++)
        for (uint32_t rm = 0; rm < 64; rm++) {
            char lid[PE_IMP_ID_MAX];
            PeImpRoomLogic *rl = NULL;
            pe_re_logic_id("re2", st, rm, 0, lid, sizeof lid);
            if (pe_imp_catalog_roomlogic(s_store, lid, &rl, NULL, 0)) continue;
            static PeReScdRun run;
            memset(&fresh, 0, sizeof fresh);
            pe_re_scd_run(rl, &fresh.flags, NULL, &run);
            for (int f = 0; f < 120; f++) pe_re_scd_tick(rl, &fresh.flags, NULL, &run);   /* four seconds of play */
            rooms++, ran += run.ran, executed += run.executed, skipped += run.skipped, stopped += run.stopped;
            placed += run.nenemy;
            { uint32_t ne = 0; pe_imp_logic_section(rl, PE_IMP_LOGIC_ENEM, &ne, NULL); enem_records += (int)ne; }
            if (run.messages && getenv("PE_TEST_RE_MESSAGES"))
                printf("  %x%02x: message_on %d times in 4 s, %d tasks running, %d over budget\n", st + 1, rm,
                       run.messages, run.waiting, run.stopped);
            uint32_t n = 0;
            pe_imp_logic_section(rl, PE_IMP_LOGIC_DOOR, &n, NULL);
            for (uint32_t i = 0; i < n; i++) {
                PeImpDoor d;
                if (!pe_imp_logic_door(rl, i, &d)) all_doors++, live_doors += pe_re_scd_live(&run, d.at);
            }
            pe_imp_logic_section(rl, PE_IMP_LOGIC_PICK, &n, NULL);
            for (uint32_t i = 0; i < n; i++) {
                PeImpPickup pk;
                if (!pe_imp_logic_pickup(rl, i, &pk)) all_items++, live_items += pe_re_scd_live(&run, pk.at);
            }
            /* the zones the scripts left: by kind; each event zone found where the game looks for it,
             * and its task starting */
            pe_imp_logic_section(rl, PE_IMP_LOGIC_TRIG, &n, NULL);
            for (uint32_t i = 0; i < n; i++) {
                PeImpTrigger tr;
                if (pe_imp_logic_trigger(rl, i, &tr) || !pe_re_scd_live(&run, tr.at)) continue;
                const int k = pe_re_zone_kind(&run, tr.zone.id, tr.zone.sce, tr.data);
                kinds[k & 15]++, manual += (tr.zone.sat & 0x10) != 0;
                if (k == PE_RE_ZONE_FLAG && (tr.data[0] | tr.data[1] << 8) == PE_RE_FG_USE && tr.data[4]) {
                    /* an item usable here: a flag-change zone that sets its Use_flg bit (SCE_AT.C Sce_Flg_chg
                     * writes through the same flag table as the scripts' set) while she stands in it */
                    double cx = 0, cz = 0;
                    PeReZoneHit hit[16];
                    for (int c = 0; c < 4; c++) cx += tr.zone.x[c] / 4.0, cz += tr.zone.z[c] / 4.0;
                    const int under = (tr.zone.sat & PE_RE_SAT_UNDER) != 0;
                    const int nh = pe_re_zone_check(rl, &run, &fresh, cx, under ? cz : cz + PE_RE_FRONT_REACH, 0,
                                                    tr.zone.floor & 0x80 ? 0 : tr.zone.floor, 0, NULL, hit, 16);
                    int found = 0;
                    for (int h = 0; h < nh; h++) found |= hit[h].id == tr.zone.id;
                    use_zones++, use_found += found;
                    if (getenv("PE_TEST_RE_EVENTS"))
                        printf("  %x%02x zone %u: item %u usable here, sat %02x%s\n", st + 1, rm, (unsigned)tr.zone.id,
                               (unsigned)(tr.data[2] | tr.data[3] << 8), (unsigned)tr.zone.sat, found ? "" : " NOT FOUND");
                }
                if (k != PE_RE_ZONE_EVENT || !(tr.zone.sat & 1) || (tr.zone.super & 0x80)) continue;
                events++;
                double cx = 0, cz = 0;
                for (int c = 0; c < 4; c++) cx += tr.zone.x[c] / 4.0, cz += tr.zone.z[c] / 4.0;
                const int under = (tr.zone.sat & PE_RE_SAT_UNDER) != 0;
                PeReZoneHit hit[16];
                int16_t work[4] = {-1, -1, -1, -1};
                const int floor = tr.zone.floor & 0x80 ? 0 : tr.zone.floor;
                const int nh = pe_re_zone_check(rl, &run, &fresh, cx, under ? cz : cz + PE_RE_FRONT_REACH, 0, floor,
                                                (tr.zone.sat & 0x10) != 0, work, hit, 16);
                int found = 0;
                for (int h = 0; h < nh; h++) found |= hit[h].id == tr.zone.id;
                found |= work[under ? 1 : 0] == tr.zone.id;   /* an examine stopped at an earlier zone */
                events_found += found;
                const unsigned level = (unsigned)tr.data[0] | (unsigned)tr.data[1] << 8, proc = tr.data[3];
                pe_re_scd_exec(rl, &run, level, proc);
                events_started += pe_re_scd_running(&run, proc);
                if (getenv("PE_TEST_RE_EVENTS"))
                    printf("  %x%02x zone %u: event level %u proc %u sat %02x floor %02x%s%s\n", st + 1, rm,
                           (unsigned)tr.zone.id, level, proc, (unsigned)tr.zone.sat, (unsigned)tr.zone.floor,
                           found ? "" : " NOT FOUND", pe_re_scd_running(&run, proc) ? "" : " not started");
            }
            free(rl);
        }
    printf("RE2 zones, new game: %d message, %d event, %d flag, %d save, %d item box, %d other; %d of them examined; "
           "event zones found %d of %d, started %d\n", kinds[PE_RE_ZONE_MESSAGE], kinds[PE_RE_ZONE_EVENT],
           kinds[PE_RE_ZONE_FLAG], kinds[PE_RE_ZONE_SAVE], kinds[PE_RE_ZONE_ITEMBOX],
           kinds[0] + kinds[3] + kinds[7] + kinds[8] + kinds[11] + kinds[12] + kinds[13] + kinds[14], manual,
           events_found, events, events_started);
    CHECK(events > 0 && events_found * 10 >= events * 9 && events_started * 10 >= events * 9);
    CHECK(kinds[PE_RE_ZONE_SAVE] > 0 && kinds[PE_RE_ZONE_ITEMBOX] > 0);
    printf("RE2 enemies, new game: %d placements run of %d in the rooms' scripts\n", placed, enem_records);
    CHECK(placed > 50 && placed <= enem_records);
    printf("RE2 item use, new game: %d zones make an item usable, %d found where she stands; %d pocket zones\n",
           use_zones, use_found, kinds[PE_RE_ZONE_STATUS]);
    CHECK(use_zones > 5 && use_found * 10 >= use_zones * 9);
    printf("RE2 scripts, new game: %d rooms, %d ran; %ld instructions run, %ld skipped, %ld tasks over the frame budget "
           "(4 s of play); doors set up %d of %d, pickups %d of %d\n", rooms, ran, executed, skipped, stopped,
           live_doors, all_doors, live_items, all_items);
    CHECK(rooms >= 100 && ran == rooms && live_doors * 10 >= all_doors * 7 && live_items > 0 && live_items < all_items);
    /* Landings are checked against their own floor's collision (1,800 units a floor). A few still
     * have no free floor near (stairs and slopes count as solid for now; the stage walks Aya freely
     * until she's clear): 7 of 263 on disc 1, was 40 before floors. Guard the count. */
    CHECK(missing * 20 <= doors && no_camera == 0 && stuck * 20 <= doors);

    printf(s_fail ? "re session FAILED\n" : "re session passed\n");
    return s_fail;
}
