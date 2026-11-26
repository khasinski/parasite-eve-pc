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
enum { RE_HOST_CROSS = 0x0040, RE_HOST_SQUARE = 0x0080, RE_HOST_START = 0x0800, RE_HOST_SELECT = 0x0100 };
#define RE_HOST_DIALOGUE_CHOICES 80

/* Room IDs refer to the shared catalog. Positions are in RE room units.
 * A full turn is 4096 facing units, with 0 facing -z.
 * The adapter may resolve pictures, cameras and masks directly from that catalog.
 * Required callbacks are unmarked. Optional features may leave a callback NULL.
 * Append callbacks and check struct_size before accessing optional tail fields. */
struct PeReEffect;
struct PeReProgress;
typedef struct ReHostProgress {
    const char *key;
    uint16_t version, min_reader, size;
    void *bytes;
    int (*upgrade)(void *candidate, const void *old, unsigned size, unsigned version);
} ReHostProgress;

#define RE_HOST_HAS(host, member) \
    ((host)->struct_size >= offsetof(ReHost, member) + sizeof (host)->member && (host)->member)

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
    int (*place_at_ramp)(void); /* optional testing helper */
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
    void (*open_save_screen)(void); /* optional, paired with progress registration */
    int (*native_menu_active)(void);
    int (*arm_backdrop)(void); /* optional, paired with release_backdrop */
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
    void (*scene_controls)(int locked, int move_mode);
    void (*set_mask_groups)(const uint8_t groups[16]);
    void (*set_native_music_muted)(int muted);
    int (*native_battle_active)(void);
    /* Optional. The adapter persists these bytes and delivers save_read/new_game through
     * bind_lifecycle. Call committed only after a native save succeeds. Return zero on success. */
    int (*register_progress)(const ReHostProgress *progress, ReHostCallback committed);
    int (*enable_progress)(void);
    /* Optional movie renderer. Start returns 1 on success, tick returns 1 while
     * playing, and stop restores the underlying view. The host owns the loaded asset.
     * Zero seek and frame limit request the whole movie. */
    int (*movie_start)(const char *asset, unsigned start_sector, unsigned last_frame, int silent);
    int (*movie_tick)(void);
    void (*movie_stop)(void);
    /* Optional. A tester's bug report: the host writes a new folder holding `text` (report.txt), the progress
     * bytes (progress.bin, which loads like a room checkpoint), a screenshot and the end of its log. Returns 0
     * and the folder in `where`, or -1. */
    int (*write_report)(const char *text, const void *progress, size_t size, char *where, size_t n);
    /* Optional. Whether a room point (x, z, standing on `y`) is in the current camera's picture. */
    int (*in_view)(double x, double y, double z);
    /* Optional. The VRAM the room's drawing uses now, as (x, y, w, h) rectangles into `out` (at most
     * `max`): a battle's own textures keep out of it (docs/RE_PLAY_BATTLES.md, G2). Returns how many. */
    int (*vram_reserved)(int *out, int max);
    /* Optional. In battle: a host position to the room's (to_room) or back, by the backdrop's
     * mapping. 0, or -1 with no battle backdrop. */
    int (*battle_point)(int to_room, double x, double z, double *out_x, double *out_z);
    /* Optional. In battle: mark id at a host position, kind PE_RE_MARK_* (re_play_api.h); 0 clears it. */
    void (*battle_mark)(int id, int kind, double x, double z);
    /* Room pickup: 1 credited, 0 use the room pocket, -1 reserve full. */
    int (*pickup_ammo)(const char *game, uint16_t item, unsigned count);
    /* Optional game-specific weapon grant. Count is the bundled ammunition. */
    int (*pickup_weapon)(const char *game, uint16_t item, unsigned count);
    void (*weapon_loadout)(const char *game, struct PeReProgress *progress);
    void (*weapon_unload)(int inventory_replaced);
    int (*weapon_role)(const void *weapon);
} ReHost;

/* The host links exactly one implementation. The table lasts for the process. */
const ReHost *re_host(void);

#endif
