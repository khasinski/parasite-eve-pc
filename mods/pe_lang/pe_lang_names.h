#ifndef PE_LANG_NAMES_H
#define PE_LANG_NAMES_H

/* Names in the pack's letters, kept with the save (pe1/text_render.h, petext/names.py).
 *
 * A name in the game's name windows is the game's own letters, one byte each, and two-byte name
 * letters (FB-FE then a byte) that only the pack that wrote them knows. The save keeps those bytes
 * as they are (the mods' save room is full, so there is no record for a Unicode copy). pe_lang
 * holds each name in Unicode while the game runs, so a language change puts it in the new pack's
 * letters; after a load it finds the pack that knows the name's letters, the one in use first.
 * When saving with a pack that lacks some of the name's letters, the save's copy gets the letters
 * of a pack that has them all. Plain data: no game state, tested on its own. */

#include <stdint.h>

#include "pe_lang_pack.h"

#define PE_LANG_NAME_MAX 8u     /* letters in a name (g_SaveMetadataWindowLength) */

/* The letters of `bytes` (to FF, at most `room` bytes) as Unicode into out; their count, or -1
 * when one isn't known (a name letter the pack lacks, or a byte that is no letter). */
int pe_lang_name_decode(const PeLangPack *pack, const unsigned char *bytes, unsigned room, uint32_t *out);

/* `letters` in the pack's letters (NULL: the game's English only) into out, ending in FF: English
 * letters one byte, others the pack's name letters, '?' for the rest; whole letters only, at
 * most `room` bytes before the FF. Returns the bytes before the FF. */
unsigned pe_lang_name_encode(const PeLangPack *pack, const uint32_t *letters, unsigned count,
                             unsigned char *out, unsigned room);

/* Whether the name (to FF, at most `room` bytes) has a two-byte name letter. */
int pe_lang_name_paged(const unsigned char *bytes, unsigned room);

/* Whether `pack` holds every one of `letters` within `room` bytes (no '?', none left out). */
int pe_lang_name_fits(const PeLangPack *pack, const uint32_t *letters, unsigned count, unsigned room);

/* The pack among packs[0..count) with name letters that reads every letter of `bytes`, a name
 * with a two-byte letter: packs[first] first (when first >= 0), then the others in order. Returns
 * its index and the letters in out (their count in *letters_out); -1 when none reads it, or the
 * name has no two-byte letter (whichever pack is in use reads that one, like the game's names). */
int pe_lang_name_find_pack(const PeLangPack *packs, unsigned count, int first, const unsigned char *bytes,
                           unsigned room, uint32_t *out, unsigned *letters_out);

/* Saving: the pack whose letters the save's copy of a name gets. -1: the window's bytes as they
 * are (the pack in use, `active` or NULL for the game's own letters, holds them all, or no pack
 * does); else the first of packs[0..count) that holds every letter within `room`. */
int pe_lang_name_save_pack(const PeLangPack *packs, unsigned count, const PeLangPack *active,
                           const uint32_t *letters, unsigned letter_count, unsigned room);

/* Whether a name's bytes (to FF) are the same. */
int pe_lang_name_same(const unsigned char *a, const unsigned char *b, unsigned room);

#endif
