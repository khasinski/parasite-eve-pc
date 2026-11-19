#ifndef PE_IMP_RDT_H
#define PE_IMP_RDT_H
/* What happens in a Resident Evil 1, 2 or 3 room (PS1 RDT): its doors, trigger zones, item pickups and
 * camera switch zones, read from the room's scripts (SCD) and zone table (RVD). Read only; the
 * scripts themselves stay as they are, for a later interpreter. Hostile input is bounded: a script
 * that can't be read is counted and skipped, never followed out of its section.
 *
 * Instruction sizes: IntelOrca's biohazard-utils (Bio2/Bio3ConstantTable.cs),
 * Copyright (c) 2022-2023 Ted John, MIT License. Layouts: biohazard-utils and the reevengi notes. */
#include <stddef.h>
#include <stdint.h>

#include "pe_imp_logic.h"                         /* the game-neutral records this fills */
#include "pe_imp_room.h"                          /* PE_IMP_RE1, PE_IMP_RE2, PE_IMP_RE3 */

enum { PE_IMP_RDT_DOORS = 64, PE_IMP_RDT_AOTS = 160, PE_IMP_RDT_ITEMS = 64, PE_IMP_RDT_CUTS = 160,
       PE_IMP_RDT_ENEMIES = 96 };

/* The reader fills the neutral RoomLogic records (pe_imp_logic.h); these names stay for its callers.
 * Zones are rectangles (stored as corners) or four corners; RE1 doors name no camera (0) and use
 * stage 0xFF for "this stage"; RE1 camera zones flag 1 for the camera's own area. */
typedef PeImpZone PeImpRdtZone;
typedef PeImpDoor PeImpRdtDoor;
typedef PeImpPickup PeImpRdtItem;
typedef PeImpTrigger PeImpRdtAot;
typedef PeImpCameraZone PeImpRdtCut;

typedef struct {
    unsigned game;
    uint32_t procedures, instructions, unreadable; /* script procedures, instructions read, spots skipped */
    uint32_t enemies;                              /* enemy placements seen (kept as their instruction) */
    uint32_t doors, aots, items, cuts;             /* found (may exceed the arrays; extra ones are counted only) */
    PeImpRdtDoor door[PE_IMP_RDT_DOORS];
    PeImpRdtAot aot[PE_IMP_RDT_AOTS];
    PeImpRdtItem item[PE_IMP_RDT_ITEMS];
    PeImpRdtCut cut[PE_IMP_RDT_CUTS];
    uint8_t enemy[PE_IMP_RDT_ENEMIES][PE_IMP_LOGIC_ENEM_BYTES];  /* each placement's instruction, zero-padded */
} PeImpRdt;

/* Called for every instruction of every script procedure, in order. `proc` counts the procedures
 * across the room's scripts (RE2: init then main; RE3: its one script). */
typedef void (*PeImpScdVisit)(void *ctx, uint32_t proc, uint32_t at, const uint8_t *ins, uint32_t size);

/* Section `index` (0-22) of an RE2/RE3 RDT: its offset and length (up to the next section).
 * 0 with the range, or -1 if the section is absent or out of range. */
int pe_imp_rdt_section(const uint8_t *rdt, uint32_t size, unsigned index, uint32_t *offset, uint32_t *length);
/* The bytes of one instruction of `game` (0 for an opcode it doesn't know). */
uint32_t pe_imp_scd_size(unsigned game, uint8_t opcode);
/* Walks every procedure of the room's scripts (RE1: init and main, not its event scripts); returns the procedures walked, or -1 for a bad RDT.
 * *unreadable (if given) counts spots where an unknown opcode stopped a procedure. */
int pe_imp_scd_walk(unsigned game, const uint8_t *rdt, uint32_t size, PeImpScdVisit visit, void *ctx,
                    uint32_t *unreadable);
/* Reads the room. 0, or -1 with a reason. */
int pe_imp_rdt_read(unsigned game, const uint8_t *rdt, uint32_t size, PeImpRdt *out, char *why, size_t n);
/* The room as a RoomLogic record for the store: the decoded doors, trigger zones, pickups, camera
 * zones and enemy placements, plus the collision, messages and scripts as they are. `player`
 * (0 or 1) and the room's stage and number come from the file's name. NULL with a reason. */
PeImpRoomLogic *pe_imp_rdt_logic(unsigned game, uint32_t player, uint32_t stage, uint32_t room,
                                 const uint8_t *rdt, uint32_t size, char *why, size_t n);

/* A room file's stage (0 = stage 1), room and player (RE1 ROOM1060.RDT: stage 0, room 0x06, player 0;
 * RE2 ROOM1051.RDT: player 1; RE3 R10A.ARD: player 0). 0, or -1 if the name isn't a room's. */
int pe_imp_re_room_name(unsigned game, const char *path, uint32_t *stage, uint32_t *room, uint32_t *player);
/* Emits a room file's RoomLogic as "logic/<stage><room>" (RE3) or "logic/<stage><room>-<player>"
 * (RE1, RE2: each player has its own scripts), named with `players` (e.g. {"Chris", "Jill"}).
 * 0 (also for a file that isn't a room's), or -1 with a reason. */
int pe_imp_emit_re_logic(PeImpSink *sink, unsigned game, const char *path, const uint8_t *rdt, uint32_t size,
                         const char *const players[2], char *why, size_t n);

#endif
