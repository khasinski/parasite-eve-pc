#ifndef PE_IMP_LOGIC_H
#define PE_IMP_LOGIC_H
/* RoomLogic: what happens in an imported room, the contract between the importer (which builds it
 * once, at scan time, from the source game's room files) and the room runtime (which reads only
 * this, never a disc or a source file). Game-neutral records for the fixed layout, plus the
 * source's scripts, messages and collision kept as they are for that game's interpreter.
 *
 * Stored as roomlogic.bin, little-endian:
 *   "RML1", u32 version (1), u32 game, u32 player, u32 stage, u32 room, u32 sections
 *   then each section: u32 tag, u32 records, u32 bytes, the bytes, padded to 4.
 * Record sections (fixed record sizes) and raw sections (records = 0):
 *   DOOR  40-byte doors           TRIG  28-byte trigger zones    PICK  32-byte item pickups
 *   CAMZ  20-byte camera zones    ENEM  24-byte enemy placements (the source instruction, as is)
 *   COLL  collision (source)      MSG0 / MSG1  messages (source)
 *   SCD0 / SCD1 / SCD2  scripts (source; which part each is depends on the game: see pe_imp_rdt.c)
 * A runtime skips tags it doesn't know, so sections can be added without breaking it. */
#include <stddef.h>
#include <stdint.h>

#include "pe_imp.h"

#define PE_IMP_ROOMLOGIC_VERSION 1
#define PE_IMP_ROOMLOGIC_MAX (2u << 20)

#define PE_IMP_LOGIC_TAG(a, b, c, d) ((uint32_t)(a) | (uint32_t)(b) << 8 | (uint32_t)(c) << 16 | (uint32_t)(d) << 24)
enum {
    PE_IMP_LOGIC_DOOR = PE_IMP_LOGIC_TAG('D', 'O', 'O', 'R'), PE_IMP_LOGIC_TRIG = PE_IMP_LOGIC_TAG('T', 'R', 'I', 'G'),
    PE_IMP_LOGIC_PICK = PE_IMP_LOGIC_TAG('P', 'I', 'C', 'K'), PE_IMP_LOGIC_CAMZ = PE_IMP_LOGIC_TAG('C', 'A', 'M', 'Z'),
    PE_IMP_LOGIC_ENEM = PE_IMP_LOGIC_TAG('E', 'N', 'E', 'M'), PE_IMP_LOGIC_COLL = PE_IMP_LOGIC_TAG('C', 'O', 'L', 'L'),
    PE_IMP_LOGIC_MSG0 = PE_IMP_LOGIC_TAG('M', 'S', 'G', '0'), PE_IMP_LOGIC_MSG1 = PE_IMP_LOGIC_TAG('M', 'S', 'G', '1'),
    PE_IMP_LOGIC_SCD0 = PE_IMP_LOGIC_TAG('S', 'C', 'D', '0'), PE_IMP_LOGIC_SCD1 = PE_IMP_LOGIC_TAG('S', 'C', 'D', '1'),
    PE_IMP_LOGIC_SCD2 = PE_IMP_LOGIC_TAG('S', 'C', 'D', '2')
};
enum { PE_IMP_LOGIC_DOOR_BYTES = 40, PE_IMP_LOGIC_TRIG_BYTES = 28, PE_IMP_LOGIC_PICK_BYTES = 32,
       PE_IMP_LOGIC_CAMZ_BYTES = 20, PE_IMP_LOGIC_ENEM_BYTES = 24 };

/* ---- the game-neutral records ---- */
/* A zone on the floor: a rectangle (stored as its corners) or four corners. */
typedef struct {
    uint8_t id, sce, sat, floor, super, corners;  /* sce: what kind of zone; corners: 4 if four-point */
    int16_t x[4], z[4];
} PeImpZone;

typedef struct {
    PeImpZone zone;
    int16_t next_x, next_y, next_z, next_dir;     /* where it leads, and which way the player faces */
    uint8_t stage, room, camera, next_floor;      /* stage (0 = stage 1; 0xFF = this stage), room, first camera */
    uint8_t texture, animation, sound, lock_id, lock_type, free;
    uint32_t at;                                  /* the source instruction's offset (debugging) */
} PeImpDoor;

typedef struct {
    PeImpZone zone;
    uint16_t item, count, flag;                   /* the source game's item number */
    uint8_t model, action;
    uint32_t at;
} PeImpPickup;

typedef struct {
    PeImpZone zone;
    uint8_t data[6];                              /* what the zone does; read by its kind (zone.sce) */
    uint32_t at;
} PeImpTrigger;

typedef struct {
    uint16_t flag;                                /* RE1: 1 = the camera's own area */
    uint8_t from, to;                             /* the camera you're in, the camera it switches to */
    int16_t x[4], z[4];
} PeImpCameraZone;

/* ---- the stored record ---- */
typedef struct {
    uint32_t game, player, stage, room;           /* game: PE_IMP_RE1..3 (pe_imp_room.h); stage 0 = stage 1 */
    uint32_t size;                                /* bytes of sections */
    uint8_t payload[];                            /* the sections, as stored */
} PeImpRoomLogic;

/* Building one: start, add sections, finish. Returns NULL if over PE_IMP_ROOMLOGIC_MAX. */
typedef struct PeImpLogicBuilder PeImpLogicBuilder;
PeImpLogicBuilder *pe_imp_logic_begin(uint32_t game, uint32_t player, uint32_t stage, uint32_t room);
int pe_imp_logic_add(PeImpLogicBuilder *b, uint32_t tag, uint32_t records, const void *bytes, uint32_t size);
int pe_imp_logic_add_door(PeImpLogicBuilder *b, const PeImpDoor *doors, uint32_t n);
int pe_imp_logic_add_trigger(PeImpLogicBuilder *b, const PeImpTrigger *t, uint32_t n);
int pe_imp_logic_add_pickup(PeImpLogicBuilder *b, const PeImpPickup *p, uint32_t n);
int pe_imp_logic_add_camera_zone(PeImpLogicBuilder *b, const PeImpCameraZone *c, uint32_t n);
PeImpRoomLogic *pe_imp_logic_finish(PeImpLogicBuilder *b);   /* frees the builder; NULL on failure */

/* Reading one: a section's bytes (NULL if absent) and record count; then each record by index. */
const uint8_t *pe_imp_logic_section(const PeImpRoomLogic *l, uint32_t tag, uint32_t *records, uint32_t *bytes);
int pe_imp_logic_door(const PeImpRoomLogic *l, uint32_t i, PeImpDoor *out);
int pe_imp_logic_trigger(const PeImpRoomLogic *l, uint32_t i, PeImpTrigger *out);
int pe_imp_logic_pickup(const PeImpRoomLogic *l, uint32_t i, PeImpPickup *out);
int pe_imp_logic_camera_zone(const PeImpRoomLogic *l, uint32_t i, PeImpCameraZone *out);

int pe_imp_logic_save(const PeImpRoomLogic *l, const char *folder, char *why, size_t n);
int pe_imp_logic_load(const char *folder, PeImpRoomLogic **l, char *why, size_t n);
int pe_imp_catalog_roomlogic(const char *root, const char *asset_id, PeImpRoomLogic **l, char *why, size_t n);

/* DoorScene: a door transition as the source game keeps it (the 3D door, its animation and texture,
 * its sounds), in the same sectioned container, stored as doorscene.bin. `room` is the door's
 * number, `stage` its stage (RE3 keeps doors per stage; else 0), `player` 0. Sections:
 *   VAB0  the door's sound bank (RE2: a VAB, header then samples)
 *   SCN0  the scene as stored (RE2: the block after the sounds; RE1, RE3: the whole file)
 * A per-game door module in the runtime reads SCN0; the sounds are also cataloged as Sounds. */
#define PE_IMP_DOORSCENE_VERSION 1
enum { PE_IMP_DOOR_VAB0 = PE_IMP_LOGIC_TAG('V', 'A', 'B', '0'), PE_IMP_DOOR_SCN0 = PE_IMP_LOGIC_TAG('S', 'C', 'N', '0') };
int pe_imp_door_save(const PeImpRoomLogic *l, const char *folder, char *why, size_t n);
int pe_imp_door_load(const char *folder, PeImpRoomLogic **l, char *why, size_t n);
int pe_imp_catalog_doorscene(const char *root, const char *asset_id, PeImpRoomLogic **l, char *why, size_t n);

/* ItemTable: a game's items, in the same sectioned container, stored as itemtable.bin. `game` as
 * above, the rest 0. Sections:
 *   NAME  48-byte records: u16 item number (as pickups and scripts use it), u16 icon (index into
 *         ICN8, 0xFFFF if not known yet), 44-byte name (UTF-8, NUL-padded)
 *   ICN8  the inventory icons as stored: 40 x 30, 8 bits per pixel, records = icons. Their palette
 *         isn't known yet, so they have no colours.
 * The names are from IntelOrca's biohazard-utils lists (MIT). */
#define PE_IMP_ITEMTABLE_VERSION 1
enum { PE_IMP_ITEM_NAME = PE_IMP_LOGIC_TAG('N', 'A', 'M', 'E'), PE_IMP_ITEM_ICN8 = PE_IMP_LOGIC_TAG('I', 'C', 'N', '8'),
       PE_IMP_ITEM_NAME_BYTES = 48, PE_IMP_ITEM_ICON_W = 40, PE_IMP_ITEM_ICON_H = 30 };
typedef struct { uint16_t item, icon; char name[44]; } PeImpItemName;
int pe_imp_item_name(const PeImpRoomLogic *table, uint32_t i, PeImpItemName *out);
int pe_imp_items_save(const PeImpRoomLogic *l, const char *folder, char *why, size_t n);
int pe_imp_items_load(const char *folder, PeImpRoomLogic **l, char *why, size_t n);
int pe_imp_catalog_itemtable(const char *root, const char *asset_id, PeImpRoomLogic **l, char *why, size_t n);

#endif
