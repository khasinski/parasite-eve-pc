#ifndef PE_RE_RULES_H
#define PE_RE_RULES_H
/* Room rules from the imported records, before a script interpreter: locked doors and their keys,
 * item pickups, and the messages that zones show. Game-neutral where the stored records are; the
 * RE2/RE3 meanings are noted where they come from the data. PE-free, so it runs in tests.
 *
 * Doors (DOOR): lock_type is the item number of the key a door needs, or 254 (this side unlocks it:
 * the bolt is here) / 255 (locked from the other side); lock_id is the lock's flag, shared by both
 * sides (RE2 209->202 is 254 and 202->209 is 255, both lock 131), so opening one opens the pair.
 * Pickups (PICK): item number, count, and a flag the source game sets once taken; flags repeat
 * across rooms, so a pickup is remembered by room and flag.
 * Messages: trigger zones of kind 4 checked with the action button (sat bit 0x10); their first
 * data word is the message's index in the room's TEXT section. */
#include <stddef.h>
#include <stdint.h>

#include "pe_imp_logic.h"
#include "pe_re_scd.h"

#define PE_RE_LOCKS 256
#define PE_RE_TAKEN_MAX 1024
#define PE_RE_POCKET_MAX 64
#define PE_RE_POCKET_SLOTS 8                       /* what the player carries (RE2: eight slots) */
#define PE_RE_BOX_MAX 64                           /* the item box, shared by every box */
#define PE_RE_SCE_MESSAGE 4
#define PE_RE_SAT_CHECK 0x10u
#define PE_RE_LOCK_BOLT 254
#define PE_RE2_INK_RIBBON 0x1E                     /* a typewriter save takes one (SCE_AT.C Save_entrance) */
#define PE_RE_LOCK_OTHER_SIDE 255

typedef struct { uint16_t item, count; } PeReHeld;

#define PE_RE_OWNED_WEAPONS 24
/* IDs belong to the imported game's item table. Native inventory slots are
 * transient and are deliberately never stored with imported progression. */
typedef struct {
    uint16_t item;
    uint16_t loaded;
    uint16_t ammo_item;
} PeReOwnedWeapon;

/* What the player has done in the imported game so far. */
typedef struct PeReProgress {
    uint8_t unlocked[PE_RE_LOCKS / 8];
    uint32_t taken[PE_RE_TAKEN_MAX];                /* stage << 24 | room << 16 | pickup flag */
    int ntaken;
    PeReHeld pocket[PE_RE_POCKET_MAX];             /* items picked up, by item number */
    int held;
    PeReFlags flags;                               /* the game's flags, as its scripts set them */
    PeReHeld box[PE_RE_BOX_MAX];                   /* in the item boxes */
    int boxed;
    PeReOwnedWeapon weapons[PE_RE_OWNED_WEAPONS];
    uint16_t equipped_weapon;                    /* source item ID, 0 for native equipment */
    uint8_t item_group;                            /* the taken-item flags for this room's stage (not saved:
                                                    * set on each room's load; 0 reads as Item_flg) */
} PeReProgress;

int pe_re_has(const PeReProgress *p, uint16_t item);
/* 0, or -1 when the pocket is full: a new item needs one of PE_RE_POCKET_SLOTS; one held
 * already stacks when either comes in a count (ammo); single items (keys, jewels) take a slot each. */
int pe_re_give(PeReProgress *p, uint16_t item, uint16_t count);
/* Out of the pocket: `count` of it, or all when 0 (Sce_Item_lost / Sce_Item_lost2). 0, or -1 if not held. */
int pe_re_lose(PeReProgress *p, uint16_t item, uint16_t count);

/* Ammo in the pocket into the combat reserves: every stack `credit` takes whole (it returns 1, as a
 * room pickup's host credit does) leaves the pocket. Others stay. How many stacks moved. */
int pe_re_move_ammo(PeReProgress *p, const char *game, int (*credit)(const char *, uint16_t, unsigned));
/* The item box: pocket slot `i` into it, or box slot `i` back into the pocket. 0, or -1 (no such
 * slot, the box or the pocket full). */
int pe_re_box_put(PeReProgress *p, int i);
int pe_re_box_take(PeReProgress *p, int i);

/* An item's name from the game's ItemTable (NULL table or unknown: "item <n>"). */
void pe_re_item_name(const PeImpRoomLogic *items, uint16_t item, char *out, size_t n);
/* Message `index` of a room's TEXT, on one line (line breaks and page marks become spaces).
 * 0, or -1 if there is no such message or it's empty. */
int pe_re_message(const PeImpRoomLogic *room, uint32_t index, char *out, size_t n);

typedef enum { PE_RE_DOOR_OPEN, PE_RE_DOOR_UNLOCKED, PE_RE_DOOR_LOCKED } PeReDoorAnswer;
/* May the player go through? Unlocks it when they can (the bolt, or the key in the pocket); `say`
 * gets what to tell them ("" when the door is simply open). */
PeReDoorAnswer pe_re_door_try(PeReProgress *p, const PeImpDoor *d, const PeImpRoomLogic *items, char *say, size_t n);

/* `run`: what the room's scripts set up (pe_re_scd.h); NULL or not run: every record counts. A zone
 * is live when its instruction ran; aot_reset gives a zone id another kind and data (0: gone). */
/* A pickup that's no longer there: no item, taken, or (when the scripts ran) its FG_ITEM flag set
 * (item_aot_set isn't guarded in RE2's scripts: the engine skips an item whose flag is set). */
int pe_re_pickup_gone(const PeImpRoomLogic *room, const PeReScdRun *run, const PeReProgress *p, const PeImpPickup *pk);
/* The pickup at (x, z) not taken yet, or -1. */
int pe_re_pickup_at(const PeImpRoomLogic *room, const PeReScdRun *run, const PeReProgress *p, double x, double z,
                    PeImpPickup *out);
/* Take it: into the pocket, remembered (and its FG_ITEM flag set, as the game does, so the room's
 * script stops placing it); `say` gets "Picked up the Cabin Key." 0 or -1 (pocket full). */
int pe_re_take(PeReProgress *p, const PeImpRoomLogic *room, const PeImpPickup *pk, const PeImpRoomLogic *items,
               char *say, size_t n);
/* A router returns 1 after granting the whole pickup, 0 if it does not handle the
 * item, and -1 if the pickup must stay in the room. The count is the source item's
 * quantity (bundled rounds for a weapon). Only a successful grant consumes the flag. */
typedef int (*PeRePickupRoute)(const char *game, uint16_t item, unsigned count);
int pe_re_take_routed(PeReProgress *p, const PeImpRoomLogic *room, const PeImpPickup *pk,
                      const PeImpRoomLogic *items, char *say, size_t n, const char *game,
                      PeRePickupRoute credit);
int pe_re_take_equipment_or_item(PeReProgress *p, const PeImpRoomLogic *room, const PeImpPickup *pk,
                                 const PeImpRoomLogic *items, char *say, size_t n, const char *game,
                                 PeRePickupRoute weapon, PeRePickupRoute ammo);
/* The message zone checked at (x, z): its message index, or -1. */
int pe_re_message_at(const PeImpRoomLogic *room, const PeReScdRun *run, double x, double z);
/* The kind a zone id has now: its last aot_reset's, else `sce`; data likewise. */
int pe_re_zone_kind(const PeReScdRun *run, uint8_t id, int sce, uint8_t data[6]);

#endif
