/* Reading .pelang packs: v1 header 64 bytes, v2 header 80 bytes plus a CJK bitmap dictionary.
 * Both have a key table sorted by hash, then the text.
 * Every offset is checked once at load, so lookups can trust the table. */
#include "pe_lang_pack.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HEADER_SIZE 64u
#define ENTRY_SIZE 8u

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
    return 0;
}

static void copy_field(char *out, const unsigned char *in, size_t size) {
    memcpy(out, in, size);
    out[size - 1] = '\0';
}

/* Validate complete commands, with FF at the exact entry end. F9 may only
 * precede that FF (the compiler preserves the original dialogue page end). */
static int valid_text(const unsigned char *p, size_t n, unsigned glyphs) {
    size_t i = 0;
    while (i < n) {
        unsigned c = p[i++];
        if (c == 0xFF) return i == n;
        if (c == 0xF9) return i + 1 == n && p[i] == 0xFF;
        if (c < 0xF1 || c == 0xF7 || c == 0xF8 || c == 0xFA) continue;
        if (c == 0xF1) {
            if (n-i < 3 || p[i] != 0xC5 || p[i+1] >= 240 || p[i+2] >= 240 ||
                p[i+1]*240u+p[i+2] >= glyphs) return 0;
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

int pe_lang_pack_parse(PeLangPack *pack, unsigned char *data, size_t size) {
    memset(pack, 0, sizeof *pack);
    if (!data || size < HEADER_SIZE || memcmp(data, "PELG", 4) || (rd32(data + 4) != 1 && rd32(data + 4) != 2))
        goto bad;
    if (size > UINT32_MAX || !data[16] || !data[32] ||
        !memchr(data + 16, 0, 16) || !memchr(data + 32, 0, 32)) goto bad;
    unsigned header = rd32(data+4) == 2 ? 80 : 64;
    if (size < header) goto bad;
    uint32_t glyphs = 0, text_end = (uint32_t)size;
    if (header == 80) {
        text_end = rd32(data+64); glyphs = rd32(data+68);
        if (!glyphs || glyphs > 57600 || rd32(data+72) != 76 || rd32(data+76) ||
            text_end > size || size-text_end != (size_t)glyphs*76) goto bad;
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
            !valid_text(data + text + at, end - at, glyphs)) goto bad;
    }
    pack->data = data;
    pack->size = size;
    pack->count = count;
    pack->entries_at = header;
    pack->cjk_count = glyphs;
    pack->cjk_offset = text_end;
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
    free(pack->data);
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
    const char *prefix = table == 4 ? "sys" : table == 8 ? "item" : table == 12 ? "desc" : table == 16 ? "misc" : NULL;
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
