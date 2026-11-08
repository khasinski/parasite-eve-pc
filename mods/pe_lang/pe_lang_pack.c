/* Reading .pelang packs: v1 header 64 bytes, v2 header 80 bytes plus a CJK bitmap dictionary, v3
 * header 88 bytes plus a table of the pack's own letters after any dictionary, v4 header 112 bytes
 * plus the text renderer's stretches (F1 C6), the dictionary letters' widths and the name letters
 * (two-byte letters in names, pe1/text_render.h).
 * All have a key table sorted by hash, then the text.
 * Every offset is checked once at load, so lookups can trust the table. */
#include "pe_lang_pack.h"
#include "pe_lang_text_data.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HEADER_SIZE 64u
#define ENTRY_SIZE 8u
#define MAX_LETTERS 163u                /* codes 0x4D-0xEF */
#define MARK_SIZE 19u                   /* PeLatinMark */

static uint32_t rd32(const unsigned char *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

uint32_t pe_lang_hash(const char *key) {
    uint32_t h = 0x811C9DC5u;
    for (; *key; ++key) h = (h ^ (unsigned char)*key) * 0x01000193u;
    return h;
}

int pe_lang_pack_uses_glyphs(const PeLangPack *pack, unsigned first, unsigned last) {
    if (!pack || !pack->data || first > last || last >= 0xF1) return 0;
    for (unsigned i = 0; i < pack->count; ++i) {
        const unsigned char *entry = pack->data + pack->entries_at + i * ENTRY_SIZE;
        const unsigned char *p = pack->data + rd32(pack->data + 12) + rd32(entry + 4);
        while (*p != 0xFF) {
            unsigned c = *p++;
            if (c < 0xF1) { if (c >= first && c <= last) return 1; }
            else if (c == 0xF1) p += 3;
            else if (c == 0xFB) { unsigned sub = *p++; if (sub == 7 || sub == 9) ++p; }
            else if (c == 0xFC || c == 0xFD) ++p;
        }
    }
    /* and the letters of its stretches that are one-byte codes */
    if (pack->stretch_count) {
        const unsigned *codes = pack->unicode ? pack->unicode : kPeTextUnicode;
        const unsigned char *index = pack->data + pack->stretches_offset;
        const unsigned char *letters = index + (pack->stretch_count + 1u) * 4u;
        uint32_t total = rd32(index + pack->stretch_count * 4u);
        for (uint32_t k = 0; k < total; ++k) {
            uint32_t cp = rd32(letters + k * 4u);
            for (unsigned c = first; c <= last; ++c) if (codes[c] == cp) return 1;
        }
    }
    return 0;
}

const uint32_t *pe_lang_pack_stretch(const PeLangPack *pack, unsigned id, unsigned *count) {
    if (!pack || !pack->data || id >= pack->stretch_count) return NULL;
    const unsigned char *index = pack->data + pack->stretches_offset;
    uint32_t start = rd32(index + id * 4), end = rd32(index + id * 4 + 4);
    *count = end - start;
    /* 4-byte aligned (checked at load); the hosts are little-endian like the file */
    return (const uint32_t *)(const void *)(index + (pack->stretch_count + 1u + start) * 4u);
}

unsigned pe_lang_pack_name_letter(const PeLangPack *pack, unsigned paged) {
    unsigned page = paged >> 8, byte = paged & 0xFF, id;
    if (!pack || page < 1 || page > 4 || byte >= 0xFA || paged == 0x100) return 0;
    id = (page - 1) * 250u + byte;               /* name letter 1 is FB 01 */
    return id && id <= pack->name_count ? rd32(pack->data + pack->names_offset + (id - 1) * 4) : 0;
}

unsigned pe_lang_pack_glyph_width(const PeLangPack *pack, unsigned index) {
    return pack && pack->widths_offset && index < pack->cjk_count ? pack->data[pack->widths_offset + index] : 12u;
}

int pe_lang_pack_has_letter(const PeLangPack *pack, unsigned code) {
    if (!pack) return 0;
    for (unsigned k = 0; k < pack->letter_count; ++k)
        if (pe_lang_pack_letter(pack, k)[4] == code) return 1;
    return 0;
}

/* One letter record: u32 character, code, advance, base code (FF: drawn here), ops, then the
 * drawn pixels, or three marks for a letter built on the game's own. */
static int valid_letter(const unsigned char *r) {
    const uint32_t cp = rd32(r);
    if (!cp || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF) || r[4] < 0x4D || r[4] > 0xEF) return 0;
    if (r[6] == 0xFF) {
        if (!r[5] || r[5] > 15 || r[7]) return 0;
        for (unsigned j = 8; j < PE_LANG_LETTER_SIZE; ++j) if ((r[j] & 15) > 2 || (r[j] >> 4) > 2) return 0;
        return 1;
    }
    if (r[6] >= 0x4D || r[5] > 8 || r[7] > 7) return 0;
    for (unsigned k = 0; k < 3; ++k) {
        const unsigned char *mark = r + 8 + k * MARK_SIZE;
        if (mark[0] > 8 || mark[1] > 1 || mark[2] > 3) return 0;
    }
    return 1;
}

static void copy_field(char *out, const unsigned char *in, size_t size) {
    memcpy(out, in, size);
    out[size - 1] = '\0';
}

/* Validate complete commands, with FF at the exact entry end. F9 may only
 * precede that FF (the compiler preserves the original dialogue page end). */
static int valid_text(const unsigned char *p, size_t n, unsigned glyphs, unsigned stretches) {
    size_t i = 0;
    while (i < n) {
        unsigned c = p[i++];
        if (c == 0xFF) return i == n;
        if (c == 0xF9) return i + 1 == n && p[i] == 0xFF;
        if (c < 0xF1 || c == 0xF7 || c == 0xF8 || c == 0xFA) continue;
        if (c == 0xF1) {
            if (n-i < 3 || (p[i] != 0xC5 && p[i] != 0xC6) || p[i+1] >= 240 || p[i+2] >= 240 ||
                p[i+1]*240u+p[i+2] >= (p[i] == 0xC5 ? glyphs : stretches)) return 0;
            i += 3;
        } else if (c == 0xFB) {
            if (i >= n || p[i] > 9) return 0;
            unsigned sub = p[i++];
            if (sub != 7 && sub != 9) continue;
            if (i >= n || (sub == 9 && (p[i] < 1 || p[i] > 7))) return 0;
            ++i;
        } else if (c == 0xFC || c == 0xFD) {
            if (i >= n) return 0;
            ++i;
        } else return 0;
    }
    return 0;
}

/* A name entry (the default name, the name entry grids): letters only, one byte each or a v4
 * pack's two-byte name letters (FB-FE then a byte below FA, from name letter 1 to `names`). */
static int is_name_key(uint32_t hash) {
    return hash == pe_lang_hash("sys/030") || hash == pe_lang_hash("sys/115") ||
           hash == pe_lang_hash("sys/116") || hash == pe_lang_hash("sys/117");
}
static int valid_name(const unsigned char *p, size_t n, unsigned names) {
    size_t i = 0;
    while (i < n) {
        unsigned c = p[i++];
        if (c == 0xFF) return i == n;
        if (c < 0xF1) continue;
        if (c < 0xFB || c > 0xFE || i >= n || p[i] >= 0xFA) return 0;
        unsigned id = (c - 0xFBu) * 250u + p[i++];
        if (!id || id > names) return 0;
    }
    return 0;
}

int pe_lang_pack_parse(PeLangPack *pack, unsigned char *data, size_t size) {
    memset(pack, 0, sizeof *pack);
    if (!data || size < HEADER_SIZE || memcmp(data, "PELG", 4) || rd32(data + 4) < 1 || rd32(data + 4) > 4)
        goto bad;
    if (size > UINT32_MAX || !data[16] || !data[32] ||
        !memchr(data + 16, 0, 16) || !memchr(data + 32, 0, 32)) goto bad;
    const uint32_t version = rd32(data+4);
    unsigned header = version == 4 ? 112 : version == 3 ? 88 : version == 2 ? 80 : 64;
    if (size < header) goto bad;
    uint32_t glyphs = 0, text_end = (uint32_t)size, letters = 0, letters_at = (uint32_t)size;
    uint32_t sections_end = (uint32_t)size;     /* where the letter table must end */
    uint32_t stretches = 0, stretches_at = 0, pool = 0, widths_at = 0, names = 0, names_at = 0;
    if (version >= 2) {
        text_end = rd32(data+64); glyphs = rd32(data+68);
        if (version == 4) {
            /* stretches: count+1 rising u32 starts into a pool of u32 Unicode letters, then the
             * dictionary letters' widths (one byte each) when widths_at is set */
            stretches_at = rd32(data+88); stretches = rd32(data+92); pool = rd32(data+96);
            widths_at = rd32(data+100);
            /* then the name letters: u32 Unicode each, name letter 1 first (4-byte aligned) */
            names_at = rd32(data+104); names = rd32(data+108);
            if (stretches > 57600 || stretches_at > size || stretches_at % 4 || pool > (size >> 2)) goto bad;
            size_t after = (size_t)stretches_at + ((size_t)stretches + 1) * 4 + (size_t)pool * 4;
            size_t widths_end = after + (widths_at ? glyphs : 0);
            if (names > PE_LANG_NAME_LETTERS || after > size || (widths_at && widths_at != after) ||
                widths_end > size || names_at < widths_end || names_at - widths_end > 3 || names_at % 4 ||
                size - names_at != (size_t)names * 4) goto bad;
            for (unsigned k = 0; k < names; ++k) {
                uint32_t cp = rd32(data + names_at + k * 4);
                if (!cp || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) goto bad;
            }
            const unsigned char *index = data + stretches_at, *letters_in = index + (stretches + 1) * 4;
            if (rd32(index) || rd32(index + stretches * 4) != pool) goto bad;
            for (unsigned k = 0; k < stretches; ++k) {
                uint32_t start = rd32(index + k * 4), end = rd32(index + k * 4 + 4);
                if (end < start || end - start > PE_LANG_STRETCH_MAX) goto bad;
            }
            for (unsigned k = 0; k < pool; ++k) {
                uint32_t cp = rd32(letters_in + k * 4);
                if (!cp || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) goto bad;
            }
            for (unsigned k = 0; widths_at && k < glyphs; ++k)
                if (!data[widths_at + k] || data[widths_at + k] > 12) goto bad;
            sections_end = stretches_at;
        }
        if (version >= 3) {
            letters_at = rd32(data+80); letters = rd32(data+84);
            /* v4 pads the letter table's end to the stretches' 4-byte start */
            size_t table_end = (size_t)letters_at + (size_t)letters*PE_LANG_LETTER_SIZE;
            if ((version == 3 && !letters) || letters > MAX_LETTERS || letters_at > sections_end ||
                table_end > sections_end || sections_end - table_end > (version == 4 ? 3u : 0u)) goto bad;
            for (unsigned k = 0; k < letters; ++k) {
                const unsigned char *r = data+letters_at+k*PE_LANG_LETTER_SIZE;
                if (!valid_letter(r) || (k && r[4] <= r[4-(int)PE_LANG_LETTER_SIZE])) goto bad;
            }
        }
        if ((version == 2 && !glyphs) || glyphs > 57600 || rd32(data+72) != 76 || rd32(data+76) ||
            text_end > letters_at || letters_at-text_end != (size_t)glyphs*76) goto bad;
        uint32_t previous = 0;
        for (unsigned k=0; k<glyphs; ++k) {
            const unsigned char *g = data+text_end+k*76;
            uint32_t cp = rd32(g);
            if (cp <= previous || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) goto bad;
            previous = cp;
            for (unsigned j=4;j<76;++j) if ((g[j]&15)>2 || (g[j]>>4)>2) goto bad;
        }
    }
    uint32_t count = rd32(data + 8), text = rd32(data + 12);
    if (count > (size - header) / ENTRY_SIZE || text < header + count * ENTRY_SIZE || text > text_end)
        goto bad;
    for (uint32_t i = 0; i < count; ++i) {
        const unsigned char *e = data + header + i * ENTRY_SIZE;
        uint32_t at = rd32(e + 4);
        if (i && rd32(e) <= rd32(e - ENTRY_SIZE)) goto bad;     /* sorted, no duplicate hashes */
        uint32_t end = i + 1 < count ? rd32(e + ENTRY_SIZE + 4) : (uint32_t)(text_end - text);
        if ((!i && at) || at >= end || end > text_end - text ||
            !(names && is_name_key(rd32(e)) ? valid_name(data + text + at, end - at, names)
                                            : valid_text(data + text + at, end - at, glyphs, stretches)))
            goto bad;
    }
    pack->data = data;
    pack->size = size;
    pack->count = count;
    pack->entries_at = header;
    pack->cjk_count = glyphs;
    pack->cjk_offset = text_end;
    pack->letters_offset = letters_at;
    pack->letter_count = letters;
    pack->stretch_count = stretches;
    pack->stretches_offset = stretches_at;
    pack->widths_offset = widths_at;
    pack->names_offset = names_at;
    pack->name_count = names;
    if (letters) {
        pack->unicode = malloc(sizeof kPeTextUnicode);
        if (!pack->unicode) goto bad;
        memcpy(pack->unicode, kPeTextUnicode, sizeof kPeTextUnicode);
        for (unsigned k = 0; k < letters; ++k) {
            const unsigned char *r = data+letters_at+k*PE_LANG_LETTER_SIZE;
            pack->unicode[r[4]] = rd32(r);
        }
    }
    copy_field(pack->lang, data + 16, sizeof pack->lang);
    copy_field(pack->name, data + 32, sizeof pack->name);
    return 0;
bad:
    free(data);
    memset(pack, 0, sizeof *pack);
    return -1;
}

int pe_lang_pack_load(PeLangPack *pack, const char *path) {
    FILE *f = fopen(path, "rb");
    long size;
    unsigned char *data = NULL;
    memset(pack, 0, sizeof *pack);
    if (!f) return -1;
    if (!fseek(f, 0, SEEK_END) && (size = ftell(f)) > 0 && size < (64L << 20) && !fseek(f, 0, SEEK_SET) &&
        (data = malloc((size_t)size)) && fread(data, 1, (size_t)size, f) == (size_t)size) {
        fclose(f);
        return pe_lang_pack_parse(pack, data, (size_t)size);
    }
    free(data);
    fclose(f);
    return -1;
}

void pe_lang_pack_free(PeLangPack *pack) {
    if (pack->utf8) for (unsigned i=0;i<pack->count;++i) free(pack->utf8[i]);
    free(pack->utf8);
    free(pack->unicode);
    free(pack->data);
    free(pack->pictures.data);
    memset(pack, 0, sizeof *pack);
}

const unsigned char *pe_lang_pack_find(const PeLangPack *pack, const char *key) {
    if (!pack || !pack->data || !key) return NULL;
    uint32_t want = pe_lang_hash(key), lo = 0, hi = pack->count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        const unsigned char *e = pack->data + pack->entries_at + mid * ENTRY_SIZE;
        uint32_t h = rd32(e);
        if (h == want) return pack->data + rd32(pack->data + 12) + rd32(e + 4);
        if (h < want) lo = mid + 1;
        else hi = mid;
    }
    return NULL;
}

const unsigned char *pe_lang_pack_table(const PeLangPack *pack, unsigned table, unsigned index) {
    char key[32];
    const char *prefix = table == 4 ? "sys" : table == 8 ? "item" : table == 12 ? "desc" : table == 16 ? "misc" : table == 20 ? "battle" : NULL;
    if (!prefix || index > 999) return NULL;
    snprintf(key, sizeof key, "%s/%03u", prefix, index);
    return pe_lang_pack_find(pack, key);
}

size_t pe_lang_bank_size(const unsigned char *bank, size_t max) {
    size_t i = 0;
    if (!bank || max < 2 || bank[0] != 0xFE) return 0;
    while (i + 1 < max && bank[i] == 0xFE) {
        size_t j = i + 2;
        while (j < max && bank[j] != 0xFF && bank[j] != 0xF9) {
            if (bank[j] == 0xFC || bank[j] == 0xFD) j += 2;
            else if (bank[j] == 0xFB && j + 1 < max) j += bank[j + 1] == 7 || bank[j + 1] == 9 ? 3 : 2;
            else j += 1;
        }
        if (j >= max) return 0;
        i = j + 1;
    }
    return i;
}

uint32_t pe_lang_crc32(const unsigned char *data, size_t size) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int k = 0; k < 8; ++k) crc = crc >> 1 ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

const unsigned char *pe_lang_pack_page(const PeLangPack *pack, uint32_t bank_crc, unsigned page) {
    char key[32], name[16];
    const unsigned char *bank;
    size_t n = 0;
    snprintf(key, sizeof key, "bank/%08x", (unsigned)bank_crc);
    if (page > 255 || !(bank = pe_lang_pack_find(pack, key))) return NULL;
    while (n + 1 < sizeof name && bank[n] != 0xFF) {
        name[n] = (char)bank[n];
        ++n;
    }
    name[n] = '\0';
    snprintf(key, sizeof key, "dlg/%s/%03u", name, page);
    return pe_lang_pack_find(pack, key);
}
