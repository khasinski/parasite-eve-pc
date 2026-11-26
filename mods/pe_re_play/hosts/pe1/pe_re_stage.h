#ifndef PE_RE_STAGE_H
#define PE_RE_STAGE_H
/* An imported room on the field screen (grown from the pe_rooms prototype): its picture for one
 * camera behind everything, its foreground masks at depth, the game's own camera for drawing Aya,
 * and her position walked in the room's coordinates against its collision. Aya's native position
 * stays where the PE room put her; only her drawing moves. RE2 cameras and collision so far. */
#include <stdint.h>

#include "pe_imp_logic.h"
#include "pe_imp_room.h"
#include "pe_services.h"
#include "re_play_api.h"

void pe_re_stage_init(const char *mod_id, const char *store);
/* Show room `room_id` ("re2:room/100") from `camera`, collision of `floor` from `logic`. `place`:
 * put the player at (x, z) facing `facing`; else find a free spot on the floor in view. 0, or -1
 * with why. */
int pe_re_stage_show(const char *room_id, const PeImpRoomLogic *logic, int camera, int floor, int place, double x,
                     double z, int facing, char *why, size_t n);
void pe_re_stage_hide(void);                       /* the PE room's own picture and camera back */
int pe_re_stage_active(void);                      /* shown, with the player walking */
void pe_re_stage_player(double *x, double *z, int *facing);
int pe_re_stage_floor(void);                       /* the floor she's on now (ramps change it) */
int pe_re_stage_to_ramp(void);                     /* testing: the foot of the next slope or stairs; 0 if none */
void pe_re_stage_place(double x, double z);        /* move the player (testing) */
void pe_re_stage_face(int facing);                 /* turn her (testing; 0 faces -z) */
int pe_re_stage_blocked(double x, double z);       /* a wall at (x, z) on her floor (testing) */
const PeRe2RoomCollision *pe_re_stage_collision(void);
void pe_re_stage_set_masks(int on);
void pe_re_stage_set_mask_groups(const uint8_t groups[16]);
void pe_re_stage_set_placement_markers(int on);
void pe_re_stage_set_placement_meshes(int on);
void pe_re_stage_set_placement_idle(int on);
void pe_re_stage_set_placements(const PeReEnemyPlacement *placements, int count);
struct PeReSceneActor;
struct PeReEffect;
void pe_re_stage_set_effects(const PeImpRoomLogic *logic, const struct PeReEffect *effects, int count);
int pe_re_stage_climb(int pressed, const char *asset, unsigned player);
int pe_re_stage_to_ledge(int *camera);
void pe_re_stage_set_scene_actors(const struct PeReSceneActor *actors, int count);
/* A door's transition: the picture's brightness (128 full, 0 black), a black curtain in front of
 * everything (Aya too), and no walking while `hold`. */
void pe_re_stage_scene_controls(int locked, int move_mode);
void pe_re_stage_fade(int shade, int curtain, int hold);
/* Aya's size in the room, percent of an RE person's height (100: as tall as Leon). */
void pe_re_stage_set_size(int percent);
void pe_re_stage_collision_id(unsigned slot, unsigned id);   /* a script's wall switch (Sca_id_set) */
double pe_re_stage_scale(void);
/* In battle (the backdrop on): show another camera of the room. 0, or -1. */
int pe_re_stage_battle_camera(int camera);
/* The VRAM the room's drawing uses: (x, y, w, h) rectangles into out, at most max; returns how many. */
int pe_re_stage_vram_reserved(int *out, int max);
/* In battle: host <-> room positions by the backdrop's mapping. 0, or -1 with no backdrop. */
int pe_re_stage_battle_point(int to_room, double x, double z, double *out_x, double *out_z);
/* In battle: mark id (0 .. PE_RE_STAGE_MARKS - 1) of kind PE_RE_MARK_* at a host position; 0 clears it. */
#define PE_RE_STAGE_MARKS 4
#ifndef PE_RE_MARK_TARGET
#define PE_RE_MARK_TARGET 1   /* the chosen target: an edge arrow while it is off the picture (D10) */
#define PE_RE_MARK_CUE 2      /* a joining enemy (D19) */
#endif
void pe_re_stage_battle_mark(int id, int kind, double x, double z);
int pe_re_stage_in_view(double x, double y, double z);                    /* room units per PE unit, for the status line */
void pe_re_stage_shutdown(void);
int pe_re_stage_movie_start(const char *asset, unsigned start_sector, unsigned last_frame, int silent);
int pe_re_stage_movie_tick(void);
void pe_re_stage_movie_stop(void);

/* A battle trip: the room shown now (its picture, camera, Aya's place) is armed as the backdrop
 * for the battle that follows in a PE room; during that battle it replaces the PE room's picture
 * and camera. 1 when armed (the room must be shown). Release it when the trip ends. */
int pe_re_stage_backdrop_arm(void);
void pe_re_stage_backdrop_release(void);
int pe_re_stage_backdrop_shown(void);
/* RE2's ammo types (pe_re_ammo.c), registered with "ammo_types" once it is up. */
void pe_re_ammo_register(void);
struct ItemDataRecord;
const char *pe_re_weapon_ammo_type(const struct ItemDataRecord *weapon);
int pe_re_ammo_pickup(const char *game, uint16_t item, unsigned count);

#endif
