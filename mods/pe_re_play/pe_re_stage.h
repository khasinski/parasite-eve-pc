#ifndef PE_RE_STAGE_H
#define PE_RE_STAGE_H
/* An imported room on the field screen (grown from the pe_rooms prototype): its picture for one
 * camera behind everything, its foreground masks at depth, the game's own camera for drawing Aya,
 * and her position walked in the room's coordinates against its collision. Aya's native position
 * stays where the PE room put her; only her drawing moves. RE2 cameras and collision so far. */
#include <stdint.h>

#include "pe_imp_logic.h"

void pe_re_stage_init(const char *mod_id, const char *store);
/* Show room `room_id` ("re2:room/100") from `camera`, collision from `logic`. `place`: put the
 * player at (x, z) facing `facing`; else find a free spot on the floor in view. 0, or -1 with why. */
int pe_re_stage_show(const char *room_id, const PeImpRoomLogic *logic, int camera, int place, double x, double z,
                     int facing, char *why, size_t n);
void pe_re_stage_hide(void);                       /* the PE room's own picture and camera back */
int pe_re_stage_active(void);                      /* shown, with the player walking */
void pe_re_stage_player(double *x, double *z, int *facing);
void pe_re_stage_place(double x, double z);        /* move the player (testing) */
void pe_re_stage_set_masks(int on);
/* Aya's size in the room, percent of an RE person's height (100: as tall as Leon). */
void pe_re_stage_set_size(int percent);
double pe_re_stage_scale(void);                    /* room units per PE unit, for the status line */
void pe_re_stage_shutdown(void);

#endif
