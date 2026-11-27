#ifndef PE_ENCOUNTER_H
#define PE_ENCOUNTER_H
/* Random encounters in a room's loaded script bytecode (pure byte functions, no game state).
 *
 * A random-encounter room runs, per encounter spot: Aya's position, a point-in-polygon test on
 * the spot's floor zone (opcode 119, four corners as immediates), a few flag checks, then a
 * Random -> less-than -> JumpIfZero roll whose success path reaches the battle start (opcode
 * 0x55). Every random encounter on the USA discs has its zone 0x2EC..0x374 bytes before its roll. */
#include <stddef.h>

/* The Random instruction at code[0] is an encounter roll: its success path reaches the battle
 * start within 16 instructions. All lookahead stays inside `bytes`. */
int pe_encounter_roll(const unsigned char *code, size_t bytes);

typedef struct PeEncounterZone {
    size_t roll;     /* offset of the encounter roll (Script_Random) in the blob */
    int corner[8];   /* the floor zone: x0 z0 x1 z1 x2 z2 x3 z3, 16.16 field units */
} PeEncounterZone;

/* Encounter spots of a room's script blob, in blob order. Returns how many were found (only
 * rolls with a zone before them count); writes at most `max`. */
int pe_encounter_find(const unsigned char *blob, size_t bytes, PeEncounterZone *out, int max);

/* A point well inside the zone (the mean of its corners), 16.16. */
void pe_encounter_center(const PeEncounterZone *zone, int *x, int *z);

/* The Random instruction's length: pe_hook PeHookScriptRandom.next is roll + this. */
#define PE_ENCOUNTER_RANDOM_BYTES 20u

#endif
