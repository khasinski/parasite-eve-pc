#ifndef PE_IMP_RDT_H
#define PE_IMP_RDT_H
/* What happens in a Resident Evil 2 or 3 room (PS1 RDT): its doors, trigger zones, item pickups and
 * camera switch zones, read from the room's scripts (SCD) and zone table (RVD). Read only; the
 * scripts themselves stay as they are, for a later interpreter. Hostile input is bounded: a script
 * that can't be read is counted and skipped, never followed out of its section.
 *
 * Instruction sizes: IntelOrca's biohazard-utils (Bio2/Bio3ConstantTable.cs),
 * Copyright (c) 2022-2023 Ted John, MIT License. Layouts: biohazard-utils and the reevengi notes. */
#include <stddef.h>
#include <stdint.h>

#include "pe_imp_room.h"                          /* PE_IMP_RE2, PE_IMP_RE3 */

enum { PE_IMP_RDT_DOORS = 64, PE_IMP_RDT_AOTS = 160, PE_IMP_RDT_ITEMS = 64, PE_IMP_RDT_CUTS = 160 };

/* A trigger zone on the floor: a rectangle (x, z, x + w, z + d) or four corners. */
typedef struct {
    uint8_t id, sce, sat, floor, super, corners;  /* corners: 4 for a four-point zone, else 0 */
    int16_t x[4], z[4];                           /* the corners, both forms (a rectangle's too) */
} PeImpRdtZone;

typedef struct {
    PeImpRdtZone zone;
    int16_t next_x, next_y, next_z, next_dir;     /* where it leads, and which way Aya faces */
    uint8_t stage, room, camera, next_floor;      /* stage as stored (0 = stage 1), room, first camera */
    uint8_t texture, animation, sound, lock_id, lock_type, free;
    uint32_t at;                                  /* offset of the instruction in the RDT */
} PeImpRdtDoor;

typedef struct {
    PeImpRdtZone zone;
    uint16_t item, count, flag;
    uint8_t model, action;
    uint32_t at;
} PeImpRdtItem;

typedef struct {
    PeImpRdtZone zone;
    uint8_t data[6];                              /* what the zone does: its kind (sce) says how to read these */
    uint32_t at;
} PeImpRdtAot;

typedef struct {
    uint16_t flag;
    uint8_t from, to;                             /* camera you're in, camera it switches to */
    int16_t x[4], z[4];
} PeImpRdtCut;

typedef struct {
    unsigned game;
    uint32_t procedures, instructions, unreadable; /* script procedures, instructions read, spots skipped */
    uint32_t enemies;                              /* enemy placements seen (left to the encounter host) */
    uint32_t doors, aots, items, cuts;             /* found (may exceed the arrays; extra ones are counted only) */
    PeImpRdtDoor door[PE_IMP_RDT_DOORS];
    PeImpRdtAot aot[PE_IMP_RDT_AOTS];
    PeImpRdtItem item[PE_IMP_RDT_ITEMS];
    PeImpRdtCut cut[PE_IMP_RDT_CUTS];
} PeImpRdt;

/* Called for every instruction of every script procedure, in order. `proc` counts the procedures
 * across the room's scripts (RE2: init then main; RE3: its one script). */
typedef void (*PeImpScdVisit)(void *ctx, uint32_t proc, uint32_t at, const uint8_t *ins, uint32_t size);

/* Section `index` (0-22) of an RE2/RE3 RDT: its offset and length (up to the next section).
 * 0 with the range, or -1 if the section is absent or out of range. */
int pe_imp_rdt_section(const uint8_t *rdt, uint32_t size, unsigned index, uint32_t *offset, uint32_t *length);
/* The bytes of one instruction of `game` (0 for an opcode it doesn't know). */
uint32_t pe_imp_scd_size(unsigned game, uint8_t opcode);
/* Walks every procedure of the room's scripts; returns the procedures walked, or -1 for a bad RDT.
 * *unreadable (if given) counts spots where an unknown opcode stopped a procedure. */
int pe_imp_scd_walk(unsigned game, const uint8_t *rdt, uint32_t size, PeImpScdVisit visit, void *ctx,
                    uint32_t *unreadable);
/* Reads the room. 0, or -1 with a reason. */
int pe_imp_rdt_read(unsigned game, const uint8_t *rdt, uint32_t size, PeImpRdt *out, char *why, size_t n);

#endif
