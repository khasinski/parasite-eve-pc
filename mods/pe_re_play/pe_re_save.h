#ifndef PE_RE_SAVE_H
#define PE_RE_SAVE_H
/* The imported game's progress, kept with the Parasite Eve save (the host's companion store,
 * pe_extension_store.h: 8 KB for every mod, 2 KB a section). One fixed byte layout, version 3,
 * little-endian, 1,954 bytes (older layouts are prefixes):
 *     0  u8 game (1 re1, 2 re2, 3 re3; 0 none), u8 playing, u8 player, u8 stage, u8 room, u8 camera,
 *        u16 facing (bits 0-11) | floor << 12
 *     8  s32 x, s32 z                                  where the player stands, room units
 *    16  u8 unlocked[32]                               lock flags opened (PeReProgress.unlocked)
 *    48  64 x (u16 item, u16 count)                    the pocket (item 0: empty)
 *   304  flag groups 0-9, 256 bits each                (RE2's scripts use all of them)
 *   624  flag groups 10-35, their first 128 bits each  (RE2's scripts set none higher)
 *  1040  64 x (u16 item, u16 count)                    the item box (item 0: empty)        v2
 *  1296  256 x s16                                     the scripts' work variables          v2
 *  1808  u16 equipped source item, 24 x (u16 item, u16 loaded, u16 ammo item)                v3
 * Pickups taken aren't listed: taking one sets its FG_ITEM flag, which keeps it from being placed. */
#include <stdint.h>

#include "pe_re_session.h"

#define PE_RE_SAVE_VERSION 3
#define PE_RE_SAVE_SIZE 1954
#define PE_RE_SAVE_SIZE_V1 1040
#define PE_RE_SAVE_SIZE_V2 1808

typedef struct {
    const PeReGame *game;                 /* NULL: no imported game in this save */
    int playing;                          /* was in it when saved: resume there */
    uint32_t player, stage, room;
    int camera, facing, floor;
    double x, z;
    PeReProgress progress;
} PeReSaved;

/* `s` (its game NULL: nothing) into `out`; `playing`: the session is on. */
void pe_re_save_encode(const PeReSession *s, int playing, uint8_t out[PE_RE_SAVE_SIZE]);
/* 0, or -1 if the bytes don't make sense (an unknown game, a pocket count past its end). */
int pe_re_save_decode(const uint8_t in[PE_RE_SAVE_SIZE], PeReSaved *out);
/* Raw checkpoint files carry no version header. Accept only complete known layouts. */
int pe_re_save_decode_checkpoint(uint8_t bytes[PE_RE_SAVE_SIZE], size_t size, PeReSaved *out);
/* Old save sections gain zero-initialized fields. Return 0 or -1. */
int pe_re_save_upgrade(void *candidate, const void *old, unsigned size, unsigned version);
/* Start a session where `saved` left off: its room, camera, place and progress (scripts run with
 * its flags). 0, or -1 with why. */
int pe_re_session_resume(PeReSession *s, const char *store, const PeReSaved *saved, char *why, size_t n);

#endif
