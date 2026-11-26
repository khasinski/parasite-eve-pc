#ifndef PE_RE_DOOR_H
#define PE_RE_DOOR_H
/* Going through a door, first cut: the room fades to black, the door's own sounds play over a beat
 * of black while the next room is put up underneath, and it fades in. RE2 plays a 3D scene of the
 * door opening (bio2-ds DOOR.C: the DO2 file's door model, a fixed camera and a door script run on
 * the scene engine); that comes once scripts can wait and loop. Sounds: the door's bank
 * (pe_re_door_sound_id), wave 1 as it opens, wave 2 as it closes; in the game the door script
 * places them, so the timing here is ours. */
#include "pe_imp_logic.h"

typedef enum { PE_RE_DOOR_IDLE, PE_RE_DOOR_RUNNING, PE_RE_DOOR_SWAP } PeReDoorStep;

void pe_re_door_begin(const char *game, const PeImpDoor *door);
/* Once a frame while busy: PE_RE_DOOR_SWAP once, at black (show the next room now); RUNNING
 * otherwise; IDLE when done. Drives the stage's fade (pe_re_stage_fade). */
PeReDoorStep pe_re_door_step(void);
int pe_re_door_busy(void);
void pe_re_door_cancel(void);            /* drop it at once: no fade, sounds released */

#endif
