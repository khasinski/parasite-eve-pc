/* Names in the pack's letters, kept with the save (pe_lang_names.h). */
#include "pe_lang_names.h"

#include <string.h>
#include "pe_lang_text_data.h"

#define ENGLISH_CODES 0x4Du        /* the game's own letters, codes 00-4C */
#define QUESTION 0x2Cu

static int is_page(unsigned b) { return b >= 0xFB && b <= 0xFE; }

static int english_code(uint32_t letter) {
    for (unsigned code = 0; code < ENGLISH_CODES; ++code)
        if (kPeTextUnicode[code] && kPeTextUnicode[code] == letter) return (int)code;
    return -1;
}

int pe_lang_name_decode(const PeLangPack *pack, const unsigned char *bytes, unsigned room, uint32_t *out) {
    const unsigned *codes = pack && pack->unicode ? pack->unicode : kPeTextUnicode;
    unsigned at = 0, n = 0;
    while (at < room && bytes[at] != 0xFF) {
        uint32_t letter;
        if (n == PE_LANG_NAME_MAX) return -1;
        if (is_page(bytes[at]) && at + 1 < room && bytes[at + 1] < 0xFA) {
            letter = pe_lang_pack_name_letter(pack, (bytes[at] - 0xFAu) << 8 | bytes[at + 1]);
            at += 2;
        } else if (bytes[at] < 0xF1) {
            letter = codes[bytes[at]];
            at += 1;
        } else return -1;
        if (!letter) return -1;
        out[n++] = letter;
    }
    return (int)n;
}

unsigned pe_lang_name_encode(const PeLangPack *pack, const uint32_t *letters, unsigned count,
                             unsigned char *out, unsigned room) {
    unsigned at = 0;
    for (unsigned k = 0; k < count && k < PE_LANG_NAME_MAX; ++k) {
        int code = english_code(letters[k]);
        unsigned id = 0;
        if (code < 0 && pack)
            for (unsigned n = 1; n <= pack->name_count && !id; ++n) {
                unsigned paged = (0xFBu + n / 250 - 0xFA) << 8 | n % 250;
                if (pe_lang_pack_name_letter(pack, paged) == letters[k]) id = n;
            }
        if (id) {
            if (at + 2 > room) break;
            out[at++] = (unsigned char)(0xFB + id / 250);
            out[at++] = (unsigned char)(id % 250);
        } else {
            if (at + 1 > room) break;
            out[at++] = (unsigned char)(code >= 0 ? (unsigned)code : QUESTION);
        }
    }
    out[at] = 0xFF;
    return at;
}

int pe_lang_name_same(const unsigned char *a, const unsigned char *b, unsigned room) {
    for (unsigned i = 0; i <= room; ++i) {
        if (a[i] != b[i]) return 0;
        if (a[i] == 0xFF) return 1;
    }
    return 1;
}

int pe_lang_name_paged(const unsigned char *bytes, unsigned room) {
    for (unsigned at = 0; at + 1 < room && bytes[at] != 0xFF; ++at)
        if (is_page(bytes[at]) && bytes[at + 1] < 0xFA) return 1;
    return 0;
}

int pe_lang_name_fits(const PeLangPack *pack, const uint32_t *letters, unsigned count, unsigned room) {
    unsigned char bytes[32];
    uint32_t back[PE_LANG_NAME_MAX];
    int n;
    if (count > PE_LANG_NAME_MAX || room >= sizeof bytes) return 0;
    pe_lang_name_encode(pack, letters, count, bytes, room);
    n = pe_lang_name_decode(pack, bytes, room, back);
    return n == (int)count && !memcmp(back, letters, count * sizeof *letters);
}

int pe_lang_name_find_pack(const PeLangPack *packs, unsigned count, int first, const unsigned char *bytes,
                           unsigned room, uint32_t *out, unsigned *letters_out) {
    if (!pe_lang_name_paged(bytes, room)) return -1;
    for (int k = -1; k < (int)count; ++k) {
        int i = k < 0 ? first : k, n;
        if (i < 0 || (k >= 0 && k == first) || !packs[i].name_count) continue;
        if ((n = pe_lang_name_decode(&packs[i], bytes, room, out)) >= 0) {
            *letters_out = (unsigned)n;
            return i;
        }
    }
    return -1;
}

int pe_lang_name_save_pack(const PeLangPack *packs, unsigned count, const PeLangPack *active,
                           const uint32_t *letters, unsigned letter_count, unsigned room) {
    if (!letter_count || pe_lang_name_fits(active, letters, letter_count, room)) return -1;
    for (unsigned i = 0; i < count; ++i)
        if (pe_lang_name_fits(&packs[i], letters, letter_count, room)) return (int)i;
    return -1;
}
