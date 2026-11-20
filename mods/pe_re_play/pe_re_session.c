/* A play session in an imported game. See pe_re_session.h. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pe_re_session.h"

/* First rooms: RE2 and RE3 open in the street (room 1-00); RE1 in the mansion's main hall (1-06).
 * Only RE2 has the cameras and collision the runtime uses so far (design doc, phase 4). */
static const PeReGame k_games[PE_RE_GAMES] = {
    {"re1", "Resident Evil", 0, 0x06, 0},
    {"re2", "Resident Evil 2", 0, 0x00, 1},
    {"re3", "Resident Evil 3", 0, 0x00, 0},
};

const PeReGame *pe_re_game(int i) { return i >= 0 && i < PE_RE_GAMES ? &k_games[i] : NULL; }
const PeReGame *pe_re_game_named(const char *game) {
    for (int i = 0; game && i < PE_RE_GAMES; i++) if (!strcmp(k_games[i].game, game)) return &k_games[i];
    return NULL;
}

void pe_re_logic_id(const char *game, uint32_t stage, uint32_t room, uint32_t player, char *out, size_t n) {
    if (!strcmp(game, "re3")) snprintf(out, n, "re3:logic/%x%02x", (unsigned)stage + 1, (unsigned)room);
    else snprintf(out, n, "%s:logic/%x%02x-%u", game, (unsigned)stage + 1, (unsigned)room, (unsigned)player);
}

void pe_re_room_id(const char *game, uint32_t stage, uint32_t room, char *out, size_t n) {
    snprintf(out, n, "%s:room/%x%02x", game, (unsigned)stage + 1, (unsigned)room);
}

int pe_re_imported(const char *store, const PeReGame *g) {
    char logic[PE_IMP_ID_MAX], room[PE_IMP_ID_MAX];
    PeImpAsset *a = NULL;
    int found = 0;
    if (!store || !g) return 0;
    pe_re_logic_id(g->game, g->stage, g->room, 0, logic, sizeof logic);
    pe_re_room_id(g->game, g->stage, g->room, room, sizeof room);
    const int n = pe_imp_catalog_assets(store, g->game, &a);
    for (int i = 0; i < n; i++) {
        if (!strcmp(a[i].id, logic)) found |= 1;
        if (!strcmp(a[i].id, room)) found |= 2;
    }
    free(a);
    return found == 3;
}

/* ---- zones ---- */
int pe_re_in_quad(const int16_t x[4], const int16_t z[4], double px, double pz) {
    int inside = 0;
    for (int i = 0, j = 3; i < 4; j = i++)               /* even-odd: any four-sided zone */
        if ((z[i] > pz) != (z[j] > pz) && px < (double)(x[j] - x[i]) * (pz - z[i]) / (double)(z[j] - z[i]) + x[i])
            inside = !inside;
    return inside;
}

int pe_re_camera_at(const PeImpRoomLogic *l, int camera, double x, double z) {
    uint32_t n = 0;
    if (!l || !pe_imp_logic_section(l, PE_IMP_LOGIC_CAMZ, &n, NULL)) return camera;
    for (uint32_t i = 0; i < n; i++) {
        PeImpCameraZone c;
        if (pe_imp_logic_camera_zone(l, i, &c) || c.from != camera || c.to == camera) continue;
        if (pe_re_in_quad(c.x, c.z, x, z)) return c.to;
    }
    return camera;
}

int pe_re_door_at(const PeImpRoomLogic *l, double x, double z, PeImpDoor *door) {
    uint32_t n = 0;
    if (!l || !pe_imp_logic_section(l, PE_IMP_LOGIC_DOOR, &n, NULL)) return -1;
    for (uint32_t i = 0; i < n; i++) {
        PeImpDoor d;
        if (pe_imp_logic_door(l, i, &d)) continue;
        if (pe_re_in_quad(d.zone.x, d.zone.z, x, z)) { if (door) *door = d; return (int)i; }
    }
    return -1;
}

int pe_re_free_spot(const PeRe2RoomCollision *c, double *x, double *z, int radius, int reach) {
    if (!pe_imp_re2_blocked(c, *x, *z, radius)) return 1;
    for (int r = 50; r <= reach; r += 50)                  /* rings of 16 points, nearest ring first */
        for (int k = 0; k < 16; k++) {
            static const double dir[16][2] = {{1, 0}, {.924, .383}, {.707, .707}, {.383, .924}, {0, 1}, {-.383, .924},
                                              {-.707, .707}, {-.924, .383}, {-1, 0}, {-.924, -.383}, {-.707, -.707},
                                              {-.383, -.924}, {0, -1}, {.383, -.924}, {.707, -.707}, {.924, -.383}};
            const double nx = *x + r * dir[k][0], nz = *z + r * dir[k][1];
            if (!pe_imp_re2_blocked(c, nx, nz, radius)) { *x = nx, *z = nz; return 1; }
        }
    return 0;
}

/* ---- the session ---- */
static int load(PeReSession *s, const char *store, uint32_t stage, uint32_t room, char *why, size_t n) {
    char id[PE_IMP_ID_MAX];
    PeImpRoomLogic *l = NULL;
    pe_re_logic_id(s->game->game, stage, room, s->player, id, sizeof id);
    if (pe_imp_catalog_roomlogic(store, id, &l, why, n)) {
        if (why && n && !*why) snprintf(why, n, "%s isn't imported", id);
        return -1;
    }
    free(s->logic);
    s->logic = l, s->stage = stage, s->room = room;
    return 0;
}

int pe_re_session_start(PeReSession *s, const char *store, const PeReGame *g, uint32_t player, char *why, size_t n) {
    memset(s, 0, sizeof *s);
    if (!g) { snprintf(why, n, "no such game"); return -1; }
    s->game = g, s->player = player;
    if (load(s, store, g->stage, g->room, why, n)) { s->game = NULL; return -1; }
    return 0;
}

int pe_re_session_enter(PeReSession *s, const char *store, uint32_t stage, uint32_t room, int camera, double x,
                        double z, int facing, char *why, size_t n) {
    if (!s->game) { snprintf(why, n, "no session"); return -1; }
    if (load(s, store, stage, room, why, n)) return -1;
    s->camera = camera;
    pe_re_session_place(s, x, z, facing);
    return 0;
}

void pe_re_session_place(PeReSession *s, double x, double z, int facing) {
    s->x = x, s->z = z, s->facing = facing & 4095;
}

PeReChange pe_re_session_step(PeReSession *s, const char *store, double x, double z, int action, char *why, size_t n) {
    PeImpDoor d;
    if (!s->game || !s->logic) return PE_RE_STAY;
    s->x = x, s->z = z;
    if (action && pe_re_door_at(s->logic, x, z, &d) >= 0) {
        const uint32_t stage = d.stage == PE_RE_STAGE_HERE ? s->stage : d.stage;
        if (!pe_re_session_enter(s, store, stage, d.room, d.camera, d.next_x, d.next_z, d.next_dir, why, n)) {
            s->doors_taken++;
            return PE_RE_ROOM;
        }
        return PE_RE_STAY;
    }
    const int camera = pe_re_camera_at(s->logic, s->camera, x, z);
    if (camera == s->camera) return PE_RE_STAY;
    s->camera = camera;
    return PE_RE_CAMERA;
}

void pe_re_session_end(PeReSession *s) {
    free(s->logic);
    memset(s, 0, sizeof *s);
}
