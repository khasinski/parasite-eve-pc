#ifndef PE_RE_SESSION_H
#define PE_RE_SESSION_H
/* A play session in an imported game: which room, which camera, where the player stands, and what
 * the room's zones say about that. Game-neutral and PE-free: it reads only the import store's
 * RoomLogic records (host/import/pe_imp_logic.h), so it runs in tests without the game.
 *
 * Coordinates are the source game's floor coordinates (x, z; y down), facing 0..4095 a turn. */
#include <stddef.h>
#include <stdint.h>

#include "pe_imp_logic.h"
#include "pe_imp_room.h"

#define PE_RE_GAMES 3
#define PE_RE_STAGE_HERE 0xFFu        /* a door's stage: stay on this stage */

typedef struct {
    const char *game;                 /* "re1", "re2", "re3" */
    const char *title;                /* "Resident Evil 2" */
    uint32_t stage, room;             /* the first room (stage 0 = stage 1) */
    int playable;                     /* the runtime can walk its rooms yet (cameras, collision) */
} PeReGame;

const PeReGame *pe_re_game(int i);    /* 0 .. PE_RE_GAMES - 1 */
const PeReGame *pe_re_game_named(const char *game);

/* The store's RoomLogic ID for a room: "re2:logic/100-0" (RE1/RE2 carry the player), "re3:logic/100". */
void pe_re_logic_id(const char *game, uint32_t stage, uint32_t room, uint32_t player, char *out, size_t n);
/* Its background views: "re2:room/100". */
void pe_re_room_id(const char *game, uint32_t stage, uint32_t room, char *out, size_t n);

/* Is the game imported? Its first room's RoomLogic and pictures are in the store. */
int pe_re_imported(const char *store, const PeReGame *g);

/* ---- zones ---- */
int pe_re_in_quad(const int16_t x[4], const int16_t z[4], double px, double pz);
/* The camera to show from `camera` at (x, z): a switch zone's target, else `camera`. */
int pe_re_camera_at(const PeImpRoomLogic *l, int camera, double x, double z);
/* The door whose zone holds (x, z), or -1. */
int pe_re_door_at(const PeImpRoomLogic *l, double x, double z, PeImpDoor *door);

/* The nearest point to (x, z) where a body of `radius` stands clear of the collision (a door's
 * landing point can sit on a wall's edge): rings out to `reach`. 1 and the point, or 0. */
int pe_re_free_spot(const PeRe2RoomCollision *c, double *x, double *z, int radius, int reach);

/* ---- the session ---- */
typedef struct {
    const PeReGame *game;
    uint32_t stage, room, player;
    int camera;
    double x, z;
    int facing;
    PeImpRoomLogic *logic;            /* the room's record (owned) */
    int doors_taken;
} PeReSession;

typedef enum { PE_RE_STAY, PE_RE_CAMERA, PE_RE_ROOM } PeReChange;

/* Start at the game's first room, camera 0, standing at (x, z) chosen by the caller later
 * (pe_re_session_place). 0, or -1 with the reason. */
int pe_re_session_start(PeReSession *s, const char *store, const PeReGame *g, uint32_t player, char *why, size_t n);
/* Load another room (a door's target): the new record replaces the old. 0 or -1. */
int pe_re_session_enter(PeReSession *s, const char *store, uint32_t stage, uint32_t room, int camera, double x,
                        double z, int facing, char *why, size_t n);
void pe_re_session_place(PeReSession *s, double x, double z, int facing);
/* After the player moved to (x, z): switch camera if a zone says so. Pressing `action` in a door's
 * zone takes the door (loads the next room, places the player as the door says). What changed;
 * PE_RE_ROOM can also fail to load, which leaves the session in its room (`why` says). */
PeReChange pe_re_session_step(PeReSession *s, const char *store, double x, double z, int action, char *why, size_t n);
void pe_re_session_end(PeReSession *s);

#endif
