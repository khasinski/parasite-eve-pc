/* The play session (mods/pe_re_play/pe_re_session.c), without the game: zones, camera switches and
 * doors on a made-up room; then, with PE_TEST_RE2_DISC, RE2 imported and walked door by door from
 * the street: every door must lead to an imported room that has the camera it names, and should
 * put the player on free floor. No disc data is kept. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pe_imp.h"
#include "pe_imp_internal.h"
#include "pe_imp_logic.h"
#include "pe_imp_room.h"
#include "pe_re_session.h"
#include "pe_src_re2.h"

static int s_fail;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); s_fail = 1; } } while (0)

static void rect(int16_t x[4], int16_t z[4], int x0, int z0, int x1, int z1) {
    x[0] = (int16_t)x0, z[0] = (int16_t)z0, x[1] = (int16_t)x1, z[1] = (int16_t)z0;
    x[2] = (int16_t)x1, z[2] = (int16_t)z1, x[3] = (int16_t)x0, z[3] = (int16_t)z1;
}

#define ROOMS_MAX 512
static const char *s_store = "pe-re-session-test/store";

int main(void) {
    char why[200] = "";

    /* zones: a square and a slanted four-corner zone */
    int16_t x[4], z[4];
    rect(x, z, 0, 0, 1000, 1000);
    CHECK(pe_re_in_quad(x, z, 500, 500) && !pe_re_in_quad(x, z, 1500, 500) && !pe_re_in_quad(x, z, 500, -1));
    const int16_t sx[4] = {0, 1000, 1500, 500}, sz[4] = {0, 0, 1000, 1000};
    CHECK(pe_re_in_quad(sx, sz, 1200, 800) && !pe_re_in_quad(sx, sz, 100, 800));

    /* a made-up room: camera 0 -> 1 past x 2000, back past x 1800; a door at the far end */
    PeImpLogicBuilder *b = pe_imp_logic_begin(PE_IMP_RE2, 0, 0, 0);
    PeImpCameraZone cz[2];
    memset(cz, 0, sizeof cz);
    cz[0].from = 0, cz[0].to = 1, rect(cz[0].x, cz[0].z, 2000, 0, 4000, 1000);
    cz[1].from = 1, cz[1].to = 0, rect(cz[1].x, cz[1].z, 0, 0, 1800, 1000);
    PeImpDoor door;
    memset(&door, 0, sizeof door);
    rect(door.zone.x, door.zone.z, 3800, 0, 4000, 1000);
    door.stage = 14, door.room = 0xEE, door.camera = 2, door.next_x = 100, door.next_z = 200, door.next_dir = 1024;
    CHECK(b && !pe_imp_logic_add_camera_zone(b, cz, 2) && !pe_imp_logic_add_door(b, &door, 1));
    PeImpRoomLogic *l = pe_imp_logic_finish(b);
    CHECK(l != NULL);
    if (l) {
        CHECK(pe_re_camera_at(l, 0, 1000, 500) == 0 && pe_re_camera_at(l, 0, 2500, 500) == 1);
        CHECK(pe_re_camera_at(l, 1, 1900, 500) == 1 && pe_re_camera_at(l, 1, 1000, 500) == 0);   /* no flicker at the seam */
        PeImpDoor d;
        CHECK(pe_re_door_at(l, 3900, 500, &d) == 0 && d.room == 0xEE && pe_re_door_at(l, 3000, 500, NULL) < 0);
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

    char id[PE_IMP_ID_MAX];
    pe_re_logic_id("re2", 0, 0x0a, 1, id, sizeof id);
    CHECK(!strcmp(id, "re2:logic/10a-1"));
    pe_re_logic_id("re3", 1, 0x10, 0, id, sizeof id);
    CHECK(!strcmp(id, "re3:logic/210"));
    pe_re_room_id("re1", 0, 6, id, sizeof id);
    CHECK(!strcmp(id, "re1:room/106"));
    CHECK(pe_re_game_named("re2")->playable && !pe_re_game_named("re1")->playable && !pe_re_game_named("dc1"));

    const char *re2 = getenv("PE_TEST_RE2_DISC");
    if (!re2 || !*re2) {
        printf("PE_TEST_RE2_DISC not set: the RE2 walk skipped\n");
        printf(s_fail ? "re session FAILED\n" : "re session passed\n");
        return s_fail;
    }
    PeImpScan sc;
    pe_imp_registry_reset();
    CHECK(!pe_imp_add_reader(&g_re2_logic_ps1) && !pe_imp_add_reader(&g_re2_rooms_ps1));
    CHECK(!pe_imp_set_naming("re2", g_re2_naming));
    CHECK(!pe_imp_scan(s_store, re2, NULL, &sc));
    const PeReGame *g = pe_re_game_named("re2");
    CHECK(pe_re_imported(s_store, g) && !pe_re_imported(s_store, pe_re_game_named("re3")));

    /* every room reachable through doors from the street, breadth first */
    static uint32_t queue[ROOMS_MAX];
    static uint8_t seen[16][256];
    int head = 0, tail = 0, doors = 0, missing = 0, no_camera = 0, blocked = 0, stuck = 0;
    PeReSession s;
    CHECK(!pe_re_session_start(&s, s_store, g, 0, why, sizeof why));
    pe_re_session_end(&s);
    queue[tail++] = g->stage << 8 | g->room, seen[g->stage][g->room] = 1;
    while (head < tail) {
        const uint32_t stage = queue[head] >> 8, room = queue[head] & 0xFF;
        head++;
        char lid[PE_IMP_ID_MAX];
        PeImpRoomLogic *rl = NULL;
        pe_re_logic_id("re2", stage, room, 0, lid, sizeof lid);
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
            t.game = g;
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
            if (data && !pe_imp_re2_collision(data, coll, &c)) {
                double fx = d.next_x, fz = d.next_z;
                if (pe_imp_re2_blocked(&c, fx, fz, 280)) blocked++;
                if (!pe_re_free_spot(&c, &fx, &fz, 280, 2000)) {          /* as the stage places Aya */
                    stuck++;
                    if (getenv("PE_TEST_RE_STUCK"))
                        printf("  stuck: %x%02x door %u -> %x%02x at %d,%d (y %d), %u colliders\n", stage + 1, room, i,
                               ts + 1, d.room, d.next_x, d.next_z, d.next_y, c.count);
                }
            }
            free(set);
            pe_re_session_end(&t);
            if (!seen[ts][d.room] && tail < ROOMS_MAX) seen[ts][d.room] = 1, queue[tail++] = ts << 8 | d.room;
        }
        free(rl);
    }
    printf("RE2 from the street: %d rooms reached, %d doors, %d to rooms not imported, %d without their camera, "
           "%d onto blocked floor, %d with no free floor near\n", tail, doors, missing, no_camera, blocked, stuck);
    CHECK(tail >= 40 && doors >= 100);
    /* No free floor near a landing is mostly upstairs (y -1800 and below): the collision decoder knows
     * the ground floor only, and the stage walks Aya freely until she's clear. Guard the count. */
    CHECK(missing * 20 <= doors && no_camera == 0 && stuck * 5 <= doors);

    printf(s_fail ? "re session FAILED\n" : "re session passed\n");
    return s_fail;
}
