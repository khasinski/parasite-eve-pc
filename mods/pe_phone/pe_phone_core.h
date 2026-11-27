#ifndef PE_PHONE_CORE_H
#define PE_PHONE_CORE_H

/* Phone Calls' pure core: no game state, no host calls, so tests link it alone
 * (tests/pe_mod_phone_test.c).
 *
 *   pe_phone_text.c    ASCII to the game's glyphs, a call laid out as dialogue pages (wrapped with
 *                      whatever glyph widths the caller gives), spotting the phone's save question
 *   pe_phone_record.c  the per-save record of calls heard
 *   pe_phone_pick.c    which call plays: set calls first, then a weighted random one */

#include <stdint.h>

#include "pe_services.h"

/* ---- text and pages (pe_phone_text.c) ---- */

#define PE_PHONE_GLYPH_NAME  0xFAu   /* the player's name, as the game's own text writes it */
#define PE_PHONE_GLYPH_SPACE 0x0Fu
#define PE_PHONE_MAX_PAGES   48
#define PE_PHONE_PAGE_BYTES  240
#define PE_PHONE_MAX_LINES   3       /* lines per box */
#define PE_PHONE_TEXT_MAX_W  272     /* widest line in pixels (the game's own run to about 275) */

enum { PE_PHONE_BAND = 0, PE_PHONE_WINDOW = 1 };   /* the game's dialogue band / a window */

typedef struct PePhonePage {
    uint8_t text[PE_PHONE_PAGE_BYTES];   /* glyphs, F7 between lines, FF at the end */
    uint8_t style;                       /* PE_PHONE_BAND (Aya, ring, click) or PE_PHONE_WINDOW (caller) */
    uint8_t lines;
    uint16_t width;                      /* widest line in pixels, the window's margins included */
} PePhonePage;

typedef struct PePhoneScript {
    PePhonePage pages[PE_PHONE_MAX_PAGES];
    int count;
    int click;                           /* the "*click*" page: the last one */
} PePhoneScript;

/* Pixels a glyph moves the cursor on; PE_PHONE_GLYPH_NAME is the whole name. */
typedef int (*PePhoneAdvance)(uint8_t glyph);

/* Only characters the game's font has: letters, digits, space, . , : ! ? ' - + * % / and "{name}". */
int pe_phone_text_ok(const char *ascii);
/* ASCII (with "{name}") to glyphs; returns how many were written. */
int pe_phone_encode(uint8_t *out, int cap, const char *ascii);
/* The pages of a call: "*dial... ring... ring...*", each line ("{name}: ..." in the band, "Caller:
 * ..." in a window) wrapped to PE_PHONE_TEXT_MAX_W, at most PE_PHONE_MAX_LINES lines a box (a
 * longer line runs on into another box with the speaker's name again), then "*click*". NULL: nobody
 * answers. */
void pe_phone_script_build(PePhoneScript *script, const PePhoneCall *call, PePhoneAdvance advance);
/* After building `call`: boxes wider than the screen allows, lines that ran on into a second box.
 * Returns 1 when either is non-zero. */
int pe_phone_script_problems(const PePhoneScript *script, const PePhoneCall *call, int *too_wide, int *ran_on);
/* A dialogue page's text: 1 the phone's save question ("Will you save your progress?" with a
 * choice of two), 2 the story save (the same, starting with Aya's name), 0 anything else. */
int pe_phone_save_question(const uint8_t *message);
/* A choice offering "Use the phone" (the Day 1 sewer's emergency phone, whose "Use the phone" opens
 * the save screen without the usual question): that option's index, else -1. */
int pe_phone_use_option(const uint8_t *message);

/* ---- the heard record (pe_phone_record.c) ---- */

#define PE_PHONE_LAST  5
#define PE_PHONE_HEARD 16

/* Saved as "pe_phone.heard" v1: keys as 16-bit hashes, so packs can come and go. */
typedef struct PePhoneRecord {
    uint16_t last[PE_PHONE_LAST];     /* newest first */
    uint16_t heard[PE_PHONE_HEARD];   /* once calls heard, oldest first */
    uint8_t last_count, heard_count;
    uint8_t pad[4];
} PePhoneRecord;
_Static_assert(sizeof(PePhoneRecord) == 48, "pe_phone.heard v1 layout");

uint16_t pe_phone_key_hash(const char *key);
int pe_phone_record_valid(const PePhoneRecord *record);
int pe_phone_record_recent(const PePhoneRecord *record, const char *key);   /* 1 = played last, 0 = not in the last five */
int pe_phone_record_once(const PePhoneRecord *record, const char *key);     /* a once call heard */
int pe_phone_record_heard(const PePhoneRecord *record, const char *key);    /* either */
void pe_phone_record_played(PePhoneRecord *record, const PePhoneCall *call);

/* ---- call files (pe_phone_file.c) ---- */

/* Calls written as text (mods/pe_phone/calls/pe1.txt explains the format). Parsed calls live in a
 * PePhoneStore until it is cleared; a call with a mistake is left out and reported. */
#define PE_PHONE_STORE_CALLS 256
#define PE_PHONE_STORE_LINES 3072
#define PE_PHONE_STORE_ROOMS 512
#define PE_PHONE_STORE_TEXT  (128 * 1024)

typedef struct PePhoneStore {
    PePhoneCall calls[PE_PHONE_STORE_CALLS];
    PePhoneLine lines[PE_PHONE_STORE_LINES];
    short rooms[PE_PHONE_STORE_ROOMS];
    char text[PE_PHONE_STORE_TEXT];
    int call_count, line_count, room_count, text_used;
} PePhoneStore;

/* A mistake: the file's name, its line (1-based) and what is wrong. */
typedef void (*PePhoneProblem)(void *user, const char *file, int line, const char *what);

/* The player's copy of pe1.txt (in pe_calls). The game writes it with a stamp line first,
 * "# pe1 copy XXXXXXXX ...", the built-in calls' hash: the bytes after that line still hashing to
 * it means nobody edited the file, so the game may bring it up to date. */
enum {
    PE_COPY_CURRENT,        /* unedited and up to date: nothing to do */
    PE_COPY_UPDATE,         /* unedited (or the same calls without a stamp): write the built-in again */
    PE_COPY_EDITED,         /* edited, made from the current calls: keep it */
    PE_COPY_EDITED_NEWER    /* edited (or no stamp), and the game's calls may be newer: keep it, offer pe1.txt.new */
};
int pe_phone_copy_state(const char *copy, unsigned size, const char *builtin, unsigned builtin_size);
/* The stamp line for `builtin`, with its newline; returns its length. */
int pe_phone_copy_stamp(char *out, int cap, const char *builtin, unsigned builtin_size);

void pe_phone_store_clear(PePhoneStore *store);
/* Parses `size` bytes of `src` (named `file` in problems) into the store. Returns the calls added. */
int pe_phone_parse_calls(PePhoneStore *store, const char *file, const char *src, unsigned size,
                         PePhoneProblem problem, void *user);

/* ---- picking (pe_phone_pick.c) ---- */

/* Whether a named condition holds (the service asks the mod that offered it). */
typedef int (*PePhoneHolds)(const char *name);

/* A condition's name: letters, digits, . _ -, 1 to PE_PHONE_CONDITION_NAME - 1 characters. */
#define PE_PHONE_CONDITION_NAME 32
int pe_phone_condition_name_ok(const char *name);
/* Every space-separated name in `needs` holds. Empty or NULL needs: 1; NULL holds: 0 unless empty. */
int pe_phone_needs_hold(const char *needs, PePhoneHolds holds);

/* Does `call` fit here: tone, story day and range, phone room, `after` heard, a once call not heard,
 * its needs held (`holds` NULL: a call with needs never fits). */
int pe_phone_fits(const PePhoneCall *call, const PePhoneContext *context, const PePhoneRecord *record,
                  PePhoneHolds holds);
/* Among `calls`: the highest-priority set call not heard yet; else a weighted random call from the
 * rest, leaving out the last five played (failing that all but the last one, failing that any).
 * `roll` is a random number. NULL when nothing fits. */
const PePhoneCall *pe_phone_pick(const PePhoneCall *const *calls, int count, const PePhoneContext *context,
                                 const PePhoneRecord *record, PePhoneHolds holds, uint32_t roll);

#endif
