#ifndef RE_HOST_H
#define RE_HOST_H

#include <stddef.h>
#include <stdint.h>

#include "pe_imp_logic.h"
#include "pe_imp_room.h"

struct PeReEnemyPlacement;
struct PeReSceneActor;
typedef int (*ReHostCallback)(void *event, void *user);

/* Held buttons, active high in RE's byte order. */
enum { RE_HOST_CROSS = 0x0040, RE_HOST_SQUARE = 0x0080 };
#define RE_HOST_DIALOGUE_CHOICES 80

/* Room IDs refer to the shared catalog. Positions are in RE room units.
 * A full turn is 4096 facing units, with 0 facing -z.
 * The adapter may resolve pictures, cameras and masks directly from that catalog.
 * All callbacks in this revision are required. Append new callbacks at the end. */
struct PeReEffect;
typedef struct ReHost {
    uint32_t struct_size;
    void (*init)(const char *mod_id, const char *store);
    void (*shutdown)(void);
    /* With place set, use the given pose. Otherwise choose a free spot in view.
     * Return 0 on success or -1 with an explanation in why. */
    int (*show_room)(const char *room_id, const PeImpRoomLogic *logic, int camera, int floor,
                     int place, double x, double z, int facing, char *why, size_t n);
    void (*hide_room)(void);
    int (*room_active)(void);
    /* Brightness is 0 to 128. A curtain covers actors too. Hold stops movement. */
    void (*room_fade)(int shade, int curtain, int hold);
    void (*player_pose)(double *x, double *z, int *facing);
    int (*player_floor)(void);
    void (*place_player)(double x, double z);
    void (*face_player)(int facing);
    int (*position_blocked)(double x, double z);
    void (*set_collision_id)(unsigned slot, unsigned id);  /* optional: a script switched box `slot` (Id top bits) */
    int (*place_at_ramp)(void);
    void (*set_masks)(int on);
    void (*set_player_size)(int percent);
    double (*player_scale)(void);
    void (*set_placement_markers)(int on);
    void (*set_placement_meshes)(int on);
    void (*set_placement_idle)(int on);
    void (*set_placements)(const struct PeReEnemyPlacement *placements, int count);
    /* Return 1 when the dialogue opened, or 0 when it is unavailable. */
    int (*say)(const char *text);
    int (*choose)(const char *title, const char *const labels[], int count, int at);
    /* Read the result once. -1 means no result yet and -2 means canceled. */
    int (*dialogue_choice)(void);
    int (*dialogue_busy)(void);
    void (*forget_dialogue)(void);
    int (*field_free)(void);
    uint16_t (*held_buttons)(void);
    void (*consume_native_interact)(void);
    void (*open_save_screen)(void);
    int (*native_menu_active)(void);
    int (*arm_backdrop)(void);
    void (*release_backdrop)(void);
    void (*bind_lifecycle)(const char *mod_id, ReHostCallback frame, ReHostCallback save_read,
                           ReHostCallback new_game);
    /* Optional. The snapshot contains the script's current scene poses. */
    void (*set_scene_actors)(const struct PeReSceneActor *actors, int count);
    /* Optional. Current collision, including wall switches from the room scripts. */
    const PeRe2RoomCollision *(*room_collision)(void);
    /* Optional. Queue native New Game if the host is at its title. */
    int (*start_from_title)(void);
    /* Optional. The native field has its player and renderer ready. */
    int (*start_ready)(void);
    /* Optional. World-space effect instances after resolving script attachments. */
    void (*set_effects)(const PeImpRoomLogic *logic, const struct PeReEffect *effects, int count);
    /* 1 while traversal owns the player, 2 on the landing frame. */
    int (*climb)(int pressed, const char *asset, unsigned player);
    int (*place_at_ledge)(int *camera);
} ReHost;

/* The host links exactly one implementation. The table lasts for the process. */
const ReHost *re_host(void);

#endif
