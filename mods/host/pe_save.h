#ifndef PE_SAVE_H
#define PE_SAVE_H

/* Mods' per-save data, kept by the host: one way for every mod.
 *
 * A mod registers each piece of per-save state once, in its init: a stable name, a layout version
 * and where the state lives in memory. From then on the host does all the work, the way the game
 * saves its own state from memory:
 *
 *   Save       the host copies every running mod's state into the card block's free tail
 *              (0x1C90..0x1FFF, inside the game's CRC) as named records, around the window below.
 *   Load       the host puts each record back into its mod's memory before any mod's own
 *              PE_HOOK_SAVE_READ handler runs. A record from an older layout goes through the
 *              mod's upgrade(); a newer layout this build can't read is kept as it is, so saving
 *              doesn't destroy it, and the mod starts from its defaults.
 *   Mods off   a record whose mod is off, failed to start or isn't in this build is carried from
 *              the loaded save into every later save unchanged; one with release() is loaded and
 *              released instead (see below), then saved from memory.
 *   Newer      a container format (or flags) newer than this build reads is kept as it is: every
 *              save writes it back unchanged and the mods start from their defaults.
 *   New Game   every record is reset and nothing carries over (PE_HOOK_NEW_GAME: the title's New
 *              Game or Tutorial, or the EX Game; the host calls pe_save_new_game() first).
 *   Crash      pe_save_crash_tail() copies the same records from memory without calling any mod
 *              code, for a crash report: a save rebuilt from the report holds every mod's data,
 *              even when a mod caused the crash.
 *
 * Saves from builds before this (fixed blocks PELT, PECT, PESH, PKEY, SORT at their own offsets)
 * are read as version 0 records holding the old block; each owner's upgrade() reads it.
 *
 * The window: tail bytes 0x200..0x287 stay the old PKEY (key pocket) and SORT blocks, which
 * pe_qol still writes there itself, because builds without this host (the official releases)
 * read and write them. The container runs around the window and its CRC leaves it out, so those
 * builds can't damage it; it holds a CRC of the window as it wrote it, and when the window
 * changed since (another build saved here), the window's blocks are newer than the records and
 * reach their owners' upgrade() as version 0. A block whose owner is off carries forward.
 *
 * Tail layout (little-endian, no padding), the container's bytes in order around the window:
 *   container  "PEMC", u8 format 1, u8 flags 0, u16 total bytes (header included),
 *              u32 CRC-32 (IEEE, reflected) of the whole container with this field as zero,
 *              u32 CRC-32 of the window's 0x88 bytes as written with it,
 *              writer marker: u16 mod API version, u16 save writer revision (pe_save.c)
 *   record     u32 id (FNV-1a 32 of the name's bytes), u16 layout version, u16 size,
 *              u32 CRC-32 of the id, version and size fields and the bytes, then size bytes
 * Records are sorted by id, each id once; bytes after the container are zero. When the container's
 * CRC fails (another build wrote over part of it), the records whose own CRC still matches load and
 * only the others reset. */

#include <stddef.h>
#include <stdint.h>

#define PE_SAVE_TAIL_OFFSET 0x1C90
#define PE_SAVE_TAIL_SIZE   (0x2000 - PE_SAVE_TAIL_OFFSET)
#define PE_SAVE_WINDOW_AT   0x200   /* the old PKEY and SORT blocks (tail offsets) */
#define PE_SAVE_WINDOW_END  0x288
#define PE_SAVE_WINDOW_SIZE (PE_SAVE_WINDOW_END - PE_SAVE_WINDOW_AT)
#define PE_SAVE_ROOM        (PE_SAVE_TAIL_SIZE - PE_SAVE_WINDOW_SIZE)   /* the container: 744 bytes */
#define PE_SAVE_HEADER_SIZE 20
#define PE_SAVE_RECORD_HEADER_SIZE 12

typedef struct PeSaveRecord {
    const char *name;     /* "mod.thing", e.g. "pe_qol.keys"; it is the record's id: never rename it */
    unsigned version;     /* layout of data, from 1; raise it when the layout changes */
    void *data;           /* the state itself: saved from here, loaded into here */
    unsigned size;
    /* Optional. After a load: fix up or check data; non-zero = unusable, data is reset. */
    int (*check)(void *data);
    /* Optional. Fill data from an older layout (version 0: the block of a save from before the
     * host kept mods' data); non-zero = can't, data is reset. Without it older layouts reset. */
    int (*upgrade)(void *data, const uint8_t *old, unsigned size, unsigned version);
    /* Optional. Defaults, for a save without this record and for New Game. Without it: zeros. */
    void (*reset)(void *data);
    /* Optional. The mod is off this session: after a load the record goes into data as usual, and
     * on the first free field frame after (the room has settled the game's state) release() gives
     * back to the game what only the record holds (pocketed keys to the bag), so the game plays as
     * the original, and leaves in data what it couldn't. The host saves the record from data. The
     * only mod code that runs for a mod that is off. */
    void (*release)(void *data);
} PeSaveRecord;

/* In the mod's init (it owns the record). The record must stay valid while the game runs.
 * -1: no room left in the tail, a duplicate name or id, or bad fields (logged). */
int pe_save_register(const PeSaveRecord *record);

/* Read-only extraction for outer admission. 1 found, 0 absent, -1 unreadable; no callbacks. */
int pe_save_peek(const uint8_t *block,const char *name,unsigned *version,void *out,unsigned capacity,unsigned *size);

/* Host internals: the mod host calls these. */
void pe_save_install(void);
void pe_save_on_read(const uint8_t *block);     /* PE_HOOK_SAVE_READ, before any mod */
/* Legacy void wrapper: no output is published on refusal. New disk-writing callers use
 * pe_save_try_write (pe_save_admission.h) and MUST abort on a negative result. Native hook
 * dispatch checks admission before/after callbacks and propagates its refusal. */
void pe_save_on_write(uint8_t *block);
void pe_save_new_game(void);
/* Crash report: the records as a save made now would hold them, into block's tail. Reads memory
 * only (no mod code, no allocation, no locks); the carried state is not changed. */
void pe_save_crash_tail(uint8_t *block);
/* Crash report: one line per record of the last crash_tail (name, owner, version, size, source). */
void pe_save_crash_describe(char *out, size_t size, size_t *used);

#endif
