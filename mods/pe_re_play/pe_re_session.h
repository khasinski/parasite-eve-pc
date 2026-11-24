#ifndef PE_RE_SESSION_H
#define PE_RE_SESSION_H
/* A play session in an imported game: which room, which camera, where the player stands, and what
 * the room's zones say about that. Game-neutral and PE-free: it reads only the import store's
 * RoomLogic records (import/core/pe_imp_logic.h), so it runs in tests without the game.
 *
 * Coordinates are the source game's floor coordinates (x, z; y down), facing 0..4095 a turn. */
#include <stddef.h>
#include <stdint.h>

#include "pe_imp_logic.h"
#include "pe_imp_room.h"
#include "pe_re_rules.h"
#include "pe_re_zone.h"
#include "re_play_api.h"

#define PE_RE_GAMES 3
#define PE_RE_STAGE_HERE 0xFFu        /* a door's stage: stay on this stage */

typedef enum {
    PE_RE_ENEMY_UNCHECKED, PE_RE_ENEMY_SCRIPTED, PE_RE_ENEMY_FAMILY, PE_RE_ENEMY_DORMANT,
    PE_RE_ENEMY_FLOOR, PE_RE_ENEMY_RANGE, PE_RE_ENEMY_CLOSE, PE_RE_ENEMY_BLOCKED, PE_RE_ENEMY_MOVING,
    PE_RE_ENEMY_VARIANT, PE_RE_ENEMY_PRONE, PE_RE_ENEMY_CORPSE, PE_RE_ENEMY_FEEDING
} PeReEnemyState;

typedef struct {
    const char *game;                 /* "re1", "re2", "re3" */
    const char *title;                /* "Resident Evil 2" */
    uint32_t stage, room;             /* the first room (stage 0 = stage 1) */
    int playable;                     /* the runtime can walk its rooms yet (cameras, collision) */
    uint32_t b_stage, b_room;         /* the second scenario's first room (RE2's B), b_room 0: none */
    int16_t b_x, b_z, b_dir;          /* where it puts the player, and her facing (the game's direction) */
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
int pe_re_camera_at(const PeImpRoomLogic *l, int camera, int floor, double x, double z);
/* The door whose zone holds (x, z), or -1. `run` (may be NULL): only doors the room's scripts set
 * up, and not turned into something else by an aot_reset. */
int pe_re_door_at(const PeImpRoomLogic *l, const PeReScdRun *run, double x, double z, PeImpDoor *door);

/* The nearest point to (x, z) where a body of `radius` stands clear of `floor`'s walls (a door's
 * landing point can sit on a wall's edge): rings out to `reach`. 1 and the point, or 0. */
int pe_re_free_spot(const PeRe2RoomCollision *c, int floor, double *x, double *z, int radius, int reach);

/* ---- the session ---- */
typedef struct {
    const PeReGame *game;
    uint32_t stage, room, player;
    int camera;
    double x, z;
    int facing;
    int floor;                        /* 0 ground; floor n stands at y = -1800 * n */
    PeImpRoomLogic *logic;            /* the room's record (owned) */
    PeImpRoomLogic *items;            /* the game's ItemTable, for names (owned; may be NULL) */
    PeReProgress progress;            /* locks opened, pickups taken, the pocket, the game's flags */
    PeReScdRun run;                   /* what this room's scripts set up when the player came in */
    PeReEnemyState enemy_state[16];   /* last field update, indexed by room enemy slot */
    char said[200];                   /* the last thing to tell the player ("" if nothing) */
    PeImpDoor door;                   /* the last door taken (for its transition) */
    int (*talking)(void);             /* a message is up (scripts' wsleeping waits on it); NULL: never */
    int doors_taken;
    const char *store;                /* the import store (for doors scripts set off) */
    uint32_t key, key_trg;            /* the game's key bits this frame (pe_re_scd_keys), for scripts */
    PeReZoneHit zone;                 /* the last zone acted on (PE_RE_ZONE: what the mod handles) */
    int auto_message;                 /* the walk-in message zone she's in (shown once), -1 */
    int events;                       /* events zones started */
    int moved;                        /* a script moved her to (x, z): the mod places her, then clears it */
    int at_zone;                      /* the zone she set off last frame (her At_sce_no), 255 none */
    int16_t cdir_y;                   /* her facing as the scripts last wrote it (unwrapped: a turn may count past 4095) */
    int16_t player_id;                /* the player's Id as the scripts read it (member 6); set for RE2's HUNK (12) */
    int uses;                         /* items used on the room */
} PeReSession;

/* PE_RE_SAID: nothing moved, but `said` has something to show (a locked door, a pickup, a message).
 * PE_RE_ROOM can come with `said` too (the key that opened the door).
 * PE_RE_ZONE: a zone the mod handles (`zone`: a typewriter, an item box). */
typedef enum { PE_RE_STAY, PE_RE_CAMERA, PE_RE_ROOM, PE_RE_SAID, PE_RE_ZONE } PeReChange;

/* Start at the game's first room, camera 0, standing at (x, z) chosen by the caller later
 * (pe_re_session_place). 0, or -1 with the reason. */
int pe_re_session_start(PeReSession *s, const char *store, const PeReGame *g, uint32_t player, char *why, size_t n);
/* The second scenario (RE2's B, after the other character's A): the scenario-B status bit set, from its own
 * first room (TITLE.C's new game). What the first run changed for it ("zapping") isn't carried yet. 0, or -1. */
int pe_re_session_start_b(PeReSession *s, const char *store, const PeReGame *g, uint32_t player, char *why, size_t n);
/* RE2's special run ("The 4th Survivor", HUNK): SYS_TIMER and STAT_SPECIAL_RUN set, player Id 12, from 3-04,
 * camera 3, as TITLE.C's new game puts him (Leon's room files). 0, or -1. */
int pe_re_session_start_hunk(PeReSession *s, const char *store, const PeReGame *g, char *why, size_t n);
/* Load another room (a door's target): the new record replaces the old. 0 or -1. The floor is
 * left as it is: set s->floor (pe_re_floor_of) for a landing on another. */
int pe_re_session_enter(PeReSession *s, const char *store, uint32_t stage, uint32_t room, int camera, double x,
                        double z, int facing, char *why, size_t n);
/* A door's sound in the store: its door file is DOOR<texture, 2 hex digits>.DO2 (the game picks it by
 * the door's Dtex_type: bio2-ds DOOR.C Dtex_file, read for behaviour; 55 and up reuse DOOR00), whose
 * sound bank the importer files as sound/door<nn>; `wave` from 1 ("re2:sound/door0a/wave01"). */
void pe_re_door_sound_id(const char *game, const PeImpDoor *d, unsigned wave, char *out, size_t n);
/* The floor a height stands on (y down, 1,800 units a floor, as the game rounds it: SCA.C). */
int pe_re_floor_of(int y);
int pe_re_session_controls_locked(const PeReSession *s);
void pe_re_session_place(PeReSession *s, double x, double z, int facing);
/* The session's facing (the stage's yaw: 0 faces -z, 1024 -x) for an angle in the game's own terms
 * (a door's landing direction: RE2's Rot_vector turns 0 to +x). */
int pe_re_facing_of(int game_dir);
/* After the player moved to (x, z) (facing s->facing, on s->floor): switch camera if a zone says
 * so. Pressing `action` examines (pe_re_zone.h): the first zone hit acts by its kind: a door
 * (locked: says why; else loads the next room and places the player as the door says), a pickup
 * (into the pocket), a message (its text), an event (starts its script task), a flag change, or
 * a typewriter or item box (PE_RE_ZONE). With no hit, a door, pickup or message zone she stands in
 * still answers. Every frame, zones that don't need an examine act too (events, flags, doors; a
 * message once each time she walks in). What changed; a door whose room fails to load leaves the
 * session where it was (`why` says). */
PeReChange pe_re_session_step(PeReSession *s, const char *store, double x, double z, int action, char *why, size_t n);
void pe_re_session_end(PeReSession *s);
/* The enemies the room's scripts have placed, in the order they ran, skipping any whose killed
 * flag is set (Sce_em_set: Enemy_flg, or Enemy_flg2 from stage 4 or on the special run). How many
 * (up to `max`). */
int pe_re_session_enemies(const PeReSession *s, PeReEnemyPlacement *out, int max);
/* Advance mobile placements on the player's floor and write new coordinates to their script slots. */
int pe_re_session_chase(PeReSession *s, const PeRe2RoomCollision *collision,
                       PeReEnemyPlacement *enemies, int count, double x, double z, int floor);
/* Scripted boss slot ready to fight, or -1 while the entrance is running. */
int pe_re_session_ready_boss(const PeReSession *s);
int pe_re_session_alligator(const PeReSession *s, unsigned slot);
int pe_re_session_final_tyrant(const PeReSession *s, unsigned slot);
int pe_re_session_canister_ready(const PeReSession *s);
void pe_re_session_release_canister(PeReSession *s);
int pe_re_session_canister_win(PeReSession *s, unsigned slot, uint8_t killed_flag);
int pe_re_session_enemy_room_move(const PeReSession *s, unsigned slot, char *asset, size_t size);
int pe_re_session_player_room_move(const PeReSession *s, char *asset, size_t size);
/* Compare the observable fields of two placement lists of the same length. */
int pe_re_enemy_lists_equal(const PeReEnemyPlacement *a, const PeReEnemyPlacement *b, int count);
/* A placed enemy beaten: its killed flag set. 0, or -1 (no game, flag 0xFF). */
int pe_re_session_defeated(PeReSession *s, uint8_t killed_flag);
/* A placed enemy beaten (a battle won): its killed flag set if it has one (not 0xFF), and the
 * scripts see it die (enemy stand-ins: a boss whose death moves the story on). 0, or -1. */
int pe_re_session_beaten(PeReSession *s, uint8_t slot, uint8_t killed_flag);
/* Use `item` from the pocket here, as choosing it on RE2's status screen does: when the room's
 * scripts allow it here (flag group 10, Use_flg, its bit set) it becomes Use_id (work 2) for them
 * to act on (open the way, take the item); else PE_RE_SAID: "You can't use the Red Jewel here." */
PeReChange pe_re_session_use(PeReSession *s, uint16_t item);
/* Combine two pocket items, as RE2's status screen does (Mix): a pair that makes another item (Jaguar
 * Stone L + R, C4 + Detonator) leaves one slot holding the result. The result, or 0 (no such pair,
 * either not held). pe_re_combined only says what the pair makes. */
uint16_t pe_re_combined(const PeReSession *s, uint16_t a, uint16_t b);
uint16_t pe_re_session_combine(PeReSession *s, uint16_t a, uint16_t b);
/* A computer zone (kind 14, handed over as PE_RE_ZONE): what it says (`said`), or that it asks to
 * use a card (PE_RE_COMPUTER_ASK; call again with `use` 1 on yes). RE2's main hall door-lock
 * service so far; others say they don't respond. */
typedef enum { PE_RE_COMPUTER_SAID, PE_RE_COMPUTER_ASK, PE_RE_COMPUTER_DONE } PeReComputer;
PeReComputer pe_re_session_computer(PeReSession *s, int use);
/* A frame of the room's scripts. PE_RE_SAID when one put up a message (`said`), PE_RE_ZONE when one
 * set off a zone the mod handles (aot_on), PE_RE_CAMERA when one cut to another camera (camera
 * zones stay out until it lets go); else PE_RE_STAY. */
PeReChange pe_re_session_tick(PeReSession *s);

#endif
