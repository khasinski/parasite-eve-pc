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

/* What the player has done in the game so far (kept for the session; saving comes later). */
typedef struct {
    uint8_t unlocked[PE_RE_LOCKS / 8];
    uint32_t taken[PE_RE_TAKEN_MAX];                /* stage << 24 | room << 16 | pickup flag */
    int ntaken;
    PeReHeld pocket[PE_RE_POCKET_MAX];             /* items picked up, by item number */
    int held;
    PeReFlags flags;                               /* the game's flags, as its scripts set them */
    PeReHeld box[PE_RE_BOX_MAX];                   /* in the item boxes */
    int boxed;
    uint8_t item_group;                            /* the taken-item flags for this room's stage (not saved:
                                                    * set on each room's load; 0 reads as Item_flg) */
} PeReProgress;

int pe_re_has(const PeReProgress *p, uint16_t item);
/* 0, or -1 when the pocket is full: a new item needs one of PE_RE_POCKET_SLOTS; one held
 * already stacks when either comes in a count (ammo); single items (keys, jewels) take a slot each. */
int pe_re_give(PeReProgress *p, uint16_t item, uint16_t count);
/* Out of the pocket: `count` of it, or all when 0 (Sce_Item_lost / Sce_Item_lost2). 0, or -1 if not held. */
int pe_re_lose(PeReProgress *p, uint16_t item, uint16_t count);
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
/* The message zone checked at (x, z): its message index, or -1. */
int pe_re_message_at(const PeImpRoomLogic *room, const PeReScdRun *run, double x, double z);
/* The kind a zone id has now: its last aot_reset's, else `sce`; data likewise. */
int pe_re_zone_kind(const PeReScdRun *run, uint8_t id, int sce, uint8_t data[6]);

#endif
