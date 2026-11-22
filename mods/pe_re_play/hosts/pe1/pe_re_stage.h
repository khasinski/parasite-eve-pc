#ifndef PE_RE_STAGE_H
#define PE_RE_STAGE_H
/* An imported room on the field screen (grown from the pe_rooms prototype): its picture for one
 * camera behind everything, its foreground masks at depth, the game's own camera for drawing Aya,
 * and her position walked in the room's coordinates against its collision. Aya's native position
 * stays where the PE room put her; only her drawing moves. RE2 cameras and collision so far. */
#include <stdint.h>

#include "pe_imp_logic.h"
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
void pe_re_stage_set_masks(int on);
void pe_re_stage_set_placement_markers(int on);
void pe_re_stage_set_placement_meshes(int on);
void pe_re_stage_set_placement_idle(int on);
void pe_re_stage_set_placements(const PeReEnemyPlacement *placements, int count);
struct PeReSceneActor;
void pe_re_stage_set_scene_actors(const struct PeReSceneActor *actors, int count);
/* A door's transition: the picture's brightness (128 full, 0 black), a black curtain in front of
 * everything (Aya too), and no walking while `hold`. */
void pe_re_stage_fade(int shade, int curtain, int hold);
/* Aya's size in the room, percent of an RE person's height (100: as tall as Leon). */
void pe_re_stage_set_size(int percent);
void pe_re_stage_collision_id(unsigned slot, unsigned id);   /* a script's wall switch (Sca_id_set) */
double pe_re_stage_scale(void);                    /* room units per PE unit, for the status line */
void pe_re_stage_shutdown(void);

/* A battle trip: the room shown now (its picture, camera, Aya's place) is armed as the backdrop
 * for the battle that follows in a PE room; during that battle it replaces the PE room's picture
 * and camera. 1 when armed (the room must be shown). Release it when the trip ends. */
int pe_re_stage_backdrop_arm(void);
void pe_re_stage_backdrop_release(void);
int pe_re_stage_backdrop_shown(void);

#endif
