#ifndef RE_PLAY_API_H
#define RE_PLAY_API_H
/* The "re_play" service v1 and its events: what the RE play mod (pe_re_play) tells other mods
 * about an imported game being played. Game-neutral, so an encounter mod for any host game
 * includes this header and nothing of the RE play mod's or the host game's. (Moved out of
 * pe_services.h; same names, layout and version.)
 *
 * The runtime only reports where the room's scripts put enemies; an encounter policy (another mod)
 * decides what happens: placement battles, random encounters, nothing. Events (pe_event_id):
 *   "re_play.room"     (const PeReRoomEvent *) the player came into a room (a door, a jump, a
 *                      loaded save), with the enemies its scripts placed. Door arrivals are
 *                      published after the transition completes.
 *   "re_play.enemies"  (const PeReRoomEvent *) the current placement list after an addition,
 *                      removal or replacement. An empty list clears the previous placements.
 * Positions are the source game's room units (x, z on the floor, y down, 1,800 a floor). A placed
 * enemy's killed flag is the game's own: report a defeat with defeated() and the room's scripts stop
 * placing it, as after a kill in the source game (RE2: Enemy_flg, Enemy_flg2 from stage 4). */
#include <stdint.h>

#define RE_PLAY_SERVICE "re_play"
#define RE_PLAY_SERVICE_VERSION 1
#define RE_PLAY_EVENT_ROOM "re_play.room"
#define RE_PLAY_EVENT_ENEMIES "re_play.enemies"

typedef struct PeReEnemyPlacement {
    uint8_t slot;                  /* the game's enemy slot (Em_no) */
    uint8_t kind;                  /* the game's id (RE2: 0x10-0x3F creatures; 0x40 and up are people) */
    uint16_t behaviour;            /* its Type word: pose and AI variant */
    uint8_t floor, sound, model;
    uint8_t killed_flag;           /* 0xFF: none (always placed) */
    int16_t x, y, z, dir;          /* dir 0..4095 */
    int16_t motion, ctr;
} PeReEnemyPlacement;

typedef struct PeReRoomEvent {
    uint32_t struct_size;
    const char *game;              /* "re2" */
    uint32_t stage, room;          /* stage 0 = stage 1 */
    int fresh;                     /* 1: came in; 0: the placement list changed */
    int count;
    const PeReEnemyPlacement *enemies;
} PeReRoomEvent;

typedef struct PeRePlayApi {
    uint32_t struct_size;
    /* The reported room and its placements (valid until the next frame); 0, or -1 if unavailable. */
    int (*room)(PeReRoomEvent *out);
    /* Set a currently placed enemy's killed flag. The next room update reflects the defeat.
     * 0 on success, or -1 if the room or flag is unavailable or the flag is 0xFF. */
    int (*defeated)(uint8_t killed_flag);
    /* Optional in v1. Pause the imported room for a native battle trip. Returns 1 when
     * paused, 0 with no running imported room, or -1 if the room is busy. */
    int (*pause_for_battle)(void);
    /* Release the pause. The imported room returns on the next free field frame. */
    void (*resume_after_battle)(void);
    /* Optional in v1 (check struct_size). A currently placed enemy beaten, by its slot: its killed
     * flag is set if it has one, and the room's scripts see it die (a boss with no killed flag whose
     * death moves the story on). 0, or -1 if no placed enemy has that slot. */
    int (*defeated_slot)(uint8_t slot);
    /* Optional in v1. Preferred archive suffix for the current room's enemy assets.
     * NULL keeps the catalogue's default. The returned string is static. */
    const char *(*enemy_archive)(void);
    /* Optional: animation assigned to this enemy slot by the current room. */
    int (*enemy_room_move)(unsigned slot, char *asset, size_t size);
} PeRePlayApi;

#endif
