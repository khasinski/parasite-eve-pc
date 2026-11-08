#ifndef PE_LANG_PACK_H
#define PE_LANG_PACK_H

/* A .pelang language pack (localization/pelang.py writes them; docs/LOCALIZATION.md):
 * translations by key ("item/054", "dlg/M001/002"), already in the game's glyph code, each
 * ending in FF. Plain data: no SDL and no game state, so it is tested on its own. */

#include <stddef.h>
#include <stdint.h>

#define PE_LANG_PACK_VERSION 1u /* original format; readers additionally accept CJK v2 and letter table v3 */
#define PE_LANG_LETTER_SIZE 80u /* one record of a pack's letter table, see pe_lang_letter_compose */
#define PE_LANG_STRETCH_MAX 96u /* letters in one F1 C6 stretch (PE_TEXT_ITEM_LETTERS) */
#define PE_LANG_NAME_LETTERS 999u /* two-byte name letters, FB 01 .. FE F9 */
#define PE_LANG_EXTRA_MAX 57600u  /* v4: table letters with no one-byte code */

typedef struct PeLangPack {
    unsigned char *data;    /* the whole file, owned by the pack */
    size_t size;
    unsigned count;
    unsigned entries_at, cjk_offset, cjk_count;
    unsigned letters_offset, letter_count; /* the pack's own letters, by rising code */
    /* v4: then its letters with no code (code byte 0), by rising Unicode: past the 163 codes they
     * reach the screen only in stretches and name letters, through the letter cache */
    unsigned extra_offset, extra_count;
    unsigned stretches_offset, stretch_count; /* v4: the text renderer's stretches */
    unsigned widths_offset;  /* v4: one width per dictionary letter, or 0 (all 12) */
    unsigned names_offset, name_count; /* v4: the name letters' Unicode, letter 1 first */
    unsigned *unicode; /* 241 codes when it has any (owned), else NULL: the fixed codes hold */
    char lang[16], name[32];
    char **utf8; /* optional decoded plain interface labels, owned; never serialized */
} PeLangPack;

/* FNV-1a 32 of the key's bytes, as pelang.py hashes it. */
uint32_t pe_lang_hash(const char *key);
/* Whether a validated pack draws a plain glyph in this inclusive range. Command operands
 * and extra-page glyph operands do not select a single-byte font cell. */
int pe_lang_pack_uses_glyphs(const PeLangPack *pack, unsigned first, unsigned last);

/* The record of letter `index` of the pack's table, and whether a code is one of those letters.
 * A code in the table means something only in this pack, so it must never reach a save. */
static inline const unsigned char *pe_lang_pack_letter(const PeLangPack *pack, unsigned index) {
    return pack->data + pack->letters_offset + index * PE_LANG_LETTER_SIZE;
}
int pe_lang_pack_has_letter(const PeLangPack *pack, unsigned code);
/* The record of the table's letter with no code `index`, and the index of Unicode `letter` among
 * them (-1 when it isn't one). */
static inline const unsigned char *pe_lang_pack_extra(const PeLangPack *pack, unsigned index) {
    return pack->data + pack->extra_offset + index * PE_LANG_LETTER_SIZE;
}
int pe_lang_pack_extra_index(const PeLangPack *pack, unsigned letter);

/* Stretch `id` (F1 C6 hi lo, hi * 240 + lo): its Unicode letters in reading order, *count of them;
 * NULL when the pack has no such stretch. */
const uint32_t *pe_lang_pack_stretch(const PeLangPack *pack, unsigned id, unsigned *count);
/* The Unicode letter of a two-byte name letter, `paged` = (page byte - FA) << 8 | byte, where
 * letter n (from 1) is page byte FB + (n / 250), byte n % 250; 0 when the pack has none. */
unsigned pe_lang_pack_name_letter(const PeLangPack *pack, unsigned paged);
/* How far the pen moves after dictionary letter `index` (12 for packs without widths). */
unsigned pe_lang_pack_glyph_width(const PeLangPack *pack, unsigned index);

/* Take `data` (malloc'd, `size` bytes) as a pack after checking every offset in it. Returns 0 and
 * owns the data, or -1 and frees it: a damaged pack is never half used. */
int pe_lang_pack_parse(PeLangPack *pack, unsigned char *data, size_t size);
int pe_lang_pack_load(PeLangPack *pack, const char *path);
void pe_lang_pack_free(PeLangPack *pack);

/* The translation of `key` (glyph bytes ending in FF), or NULL when the pack has none. */
const unsigned char *pe_lang_pack_find(const PeLangPack *pack, const char *key);

/* The translation for a resident text table lookup (PE_HOOK_TEXT_LOOKUP): table 4 is system
 * messages ("sys/NNN"), 8 item names ("item/NNN"), 12 item descriptions ("desc/NNN"), 16 the
 * rest ("misc/NNN"); NULL for anything else. */
const unsigned char *pe_lang_pack_table(const PeLangPack *pack, unsigned table, unsigned index);

/* A room's message bank, as extract_text.py measures it: pages FE <id> text FF|F9, one after
 * another, until a page end not followed by FE. Returns the bytes the pages span (at most
 * `max`), or 0 when `bank` doesn't start with a page or runs past `max`. */
size_t pe_lang_bank_size(const unsigned char *bank, size_t max);
uint32_t pe_lang_crc32(const unsigned char *data, size_t size);   /* the zlib CRC-32 */

/* The translation of page `page` of the bank whose CRC32 is `bank_crc` ("dlg/<bank key>/PPP",
 * the bank key from the pack's "bank/<crc32>" entry), or NULL. */
const unsigned char *pe_lang_pack_page(const PeLangPack *pack, uint32_t bank_crc, unsigned page);

#endif
