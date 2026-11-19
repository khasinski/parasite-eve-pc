/* Resident Evil 3 PC (1999) rofs<N>.dat archives. As documented by reevengi-tools (rofs.c):
 *   header  4096 bytes: u32 3, u32 1; at 21 the first folder name (NUL-ended), then u32 folder
 *           offset / 8, u32, then the second folder name
 *   folder  at offset * 8: u32 count, then per file u32 offset / 8, u32 stored size (header and
 *           blocks, not the unpacked length), name (NUL-ended)
 *   file    u16 header length, u16 block count, u32 length, 8 bytes id (XORed with its last byte:
 *           "Hi_Comp" = packed), then block count u32 keys and u32 block lengths; the blocks follow
 *           the header, each encrypted with its key and, when packed, LZSS packed (32 KiB out).
 * Every count and offset is checked against the archive's size. */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pe_imp.h"
#include "pe_imp_rofs.h"

#define DIR_MAX (1u << 20)
#define FILES_MAX 8192
#define BLOCKS_MAX 4096
#define BLOCK_OUT 32768u

static const uint16_t k_base[64] = {
    0x00e6, 0x01a4, 0x00e6, 0x01c5, 0x0130, 0x00e8, 0x03db, 0x008b, 0x0141, 0x018e, 0x03ae, 0x0139,
    0x00f0, 0x027a, 0x02c9, 0x01b0, 0x01f7, 0x0081, 0x0138, 0x0285, 0x025a, 0x015b, 0x030f, 0x0335,
    0x02e4, 0x01f6, 0x0143, 0x00d1, 0x0337, 0x0385, 0x007b, 0x00c6, 0x0335, 0x0141, 0x0186, 0x02a1,
    0x024d, 0x0342, 0x01fb, 0x03e5, 0x01b0, 0x006d, 0x0140, 0x00c0, 0x0386, 0x016b, 0x020b, 0x009a,
    0x0241, 0x00de, 0x015e, 0x035a, 0x025b, 0x0154, 0x0068, 0x02e8, 0x0321, 0x0071, 0x01b0, 0x0232,
    0x02d9, 0x0263, 0x0164, 0x0290,
};

static int fail(char *why, size_t n, const char *fmt, const char *a) {
    if (why && n) snprintf(why, n, fmt, a ? a : "");
    return -1;
}
static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static const char *base_name(const char *p) {
    const char *s = strrchr(p, '/'), *b = strrchr(p, '\\');
    if (b && (!s || b > s)) s = b;
    return s ? s + 1 : p;
}
static void clean(char *s) {
    for (; *s; s++) if (*s == '/' || *s == '\\' || *s < 33 || *s > 126) *s = '_';
}

int pe_imp_rofs_path(const char *path) {
    const char *b = base_name(path);
    for (int i = 0; i < 4; i++) if (tolower((unsigned char)b[i]) != "rofs"[i]) return 0;
    b += 4;
    if (!isdigit((unsigned char)*b)) return 0;
    while (isdigit((unsigned char)*b)) b++;
    return b[0] == '.' && tolower((unsigned char)b[1]) == 'd' && tolower((unsigned char)b[2]) == 'a' &&
           tolower((unsigned char)b[3]) == 't' && !b[4];
}

static uint32_t next(uint32_t *key) {
    *key = *key * 0x5d588b65u + 0x8000000bu;
    return *key >> 24;
}

void pe_imp_rofs_decrypt(uint8_t *buf, uint32_t len, uint32_t key) {
    uint32_t x = next(&key), base = next(&key) % 0x3f, run = 0;
    for (uint32_t i = 0; i < len; i++) {
        if (run > k_base[base]) {
            base = next(&key) % 0x3f;
            x = next(&key);
            run = 0;
        }
        buf[i] ^= (uint8_t)x;
        run++;
    }
}

uint32_t pe_imp_rofs_depack(const uint8_t *src, uint32_t len, uint8_t *dst, uint32_t max) {
    uint8_t tmp[4096 + 256];
    uint32_t nbit = 0, si = 0, ti = 0, out = 0;
    for (int i = 0; i < 256; i++) memset(tmp + 16 * i, i, 16);
    while (si < len && out < max) {
        nbit++;
        uint32_t v = (uint32_t)(src[si] << nbit) & 0xffff;
        si++;
        if (si < len) v |= src[si] >> (8 - nbit);
        if (nbit == 8) si++, nbit = 0;
        if (!(v & 0x100)) {
            dst[out++] = tmp[ti++] = (uint8_t)v;
        } else {
            uint32_t v2 = si < len ? (uint32_t)(src[si] << nbit) & 0xff : 0;
            si++;
            if (si < len) v2 |= src[si] >> (8 - nbit);
            uint32_t ln = (v2 & 0x0f) + 2, start = ((v2 >> 4) & 0xfff) | ((v & 0xff) << 4);
            if (ln > max - out) ln = max - out;
            /* start <= 0xfff, ln <= 17 and ti < 4096: both stay inside tmp */
            uint8_t chunk[17];
            memcpy(chunk, tmp + start, ln);
            memcpy(dst + out, chunk, ln);
            memcpy(tmp + ti, chunk, ln);
            out += ln, ti += ln;
        }
        if (ti >= 4096) ti = 0;
    }
    return out;
}

static int read_at(FILE *f, long long at, void *buf, size_t len) {
    return fseek(f, (long)at, SEEK_SET) || fread(buf, 1, len, f) != len ? -1 : 0;
}

static long long file_size(FILE *f) {
    if (fseek(f, 0, SEEK_END)) return -1;
    return ftell(f);
}

/* the NUL-ended name at `p` in buf[0..len), 1..max-1 characters; its length or 0 */
static size_t name_at(const uint8_t *buf, size_t len, size_t p, char *out, size_t max) {
    size_t k = 0;
    while (p + k < len && buf[p + k]) k++;
    if (p + k >= len || !k || k >= max) return 0;
    memcpy(out, buf + p, k);
    out[k] = 0;
    clean(out);
    return k;
}

int pe_imp_rofs_list(const char *archive, char names[2][32], PeImpRofsEntry **out, int *count, char *why, size_t n) {
    *out = NULL, *count = 0;
    FILE *f = fopen(archive, "rb");
    if (!f) return fail(why, n, "can't open %s", base_name(archive));
    uint8_t hdr[4096], *dir = NULL;
    PeImpRofsEntry *e = NULL;
    long long size = file_size(f);
    int rc = -1;
    if (size < (long long)sizeof hdr || read_at(f, 0, hdr, sizeof hdr) || le32(hdr) != 3 || le32(hdr + 4) != 1) {
        fail(why, n, "%s is not a rofs archive", base_name(archive));
        goto done;
    }
    size_t p = 21, k = name_at(hdr, sizeof hdr, p, names[0], 32);
    if (!k) { fail(why, n, "%s: bad folder name", base_name(archive)); goto done; }
    p += k + 1;
    if (p + 8 >= sizeof hdr) { fail(why, n, "%s: bad header", base_name(archive)); goto done; }
    uint64_t at = (uint64_t)le32(hdr + p) * 8;
    p += 8;
    if (!name_at(hdr, sizeof hdr, p, names[1], 32)) { fail(why, n, "%s: bad folder name", base_name(archive)); goto done; }
    uint64_t avail = (uint64_t)size > at ? (uint64_t)size - at : 0;
    uint32_t dn = avail > DIR_MAX ? DIR_MAX : (uint32_t)avail;
    if (dn < 4 || !(dir = malloc(dn)) || read_at(f, (long long)at, dir, dn)) {
        fail(why, n, "%s: folder outside the archive", base_name(archive));
        goto done;
    }
    uint32_t files = le32(dir), q = 4;
    if (files > FILES_MAX) { fail(why, n, "%s: too many files", base_name(archive)); goto done; }
    if (!(e = calloc(files ? files : 1, sizeof *e))) { fail(why, n, "%s", "out of memory"); goto done; }
    for (uint32_t i = 0; i < files; i++) {
        if ((uint64_t)q + 9 > dn) { fail(why, n, "%s: folder cut short", base_name(archive)); goto done; }
        uint64_t off = (uint64_t)le32(dir + q) * 8;
        e[i].size = le32(dir + q + 4);
        q += 8;
        size_t len = name_at(dir, dn, q, e[i].name, sizeof e[i].name);
        if (!len || off + 16 > (uint64_t)size) { fail(why, n, "%s: bad file entry", base_name(archive)); goto done; }
        e[i].offset = (uint32_t)off;
        q += (uint32_t)len + 1;
    }
    *out = e, *count = (int)files, e = NULL;
    rc = 0;
done:
    free(e), free(dir);
    fclose(f);
    return rc;
}

int pe_imp_rofs_read(const char *archive, uint32_t offset, uint32_t want, uint8_t **data, uint32_t *got,
                     char *why, size_t n) {
    *data = NULL, *got = 0;
    if (want > PE_IMP_FILE_MAX) return fail(why, n, "%s", "file too large");
    FILE *f = fopen(archive, "rb");
    if (!f) return fail(why, n, "can't open %s", base_name(archive));
    long long size = file_size(f);
    uint8_t head[16], *tab = NULL, *blk = NULL, *buf = NULL;
    int rc = -1;
    if (read_at(f, offset, head, sizeof head)) { fail(why, n, "%s: file outside the archive", base_name(archive)); goto done; }
    uint32_t hoff = head[0] | head[1] << 8, keys = head[2] | head[3] << 8, length = le32(head + 4);
    char id[9];
    for (int i = 0; i < 8; i++) id[i] = (char)(head[8 + i] ^ head[15]);
    id[8] = 0;
    int packed = !strcmp(id, "Hi_Comp");
    if (!keys || keys > BLOCKS_MAX || length > PE_IMP_FILE_MAX || 16u + 8u * keys > hoff) {
        fail(why, n, "%s: bad file header", base_name(archive));
        goto done;
    }
    if (!(tab = malloc(8u * keys)) || read_at(f, (long long)offset + 16, tab, 8u * keys)) {
        fail(why, n, "%s: bad block table", base_name(archive));
        goto done;
    }
    if (!(buf = malloc(length + 1u)) || !(blk = malloc(BLOCK_OUT * 2))) { fail(why, n, "%s", "out of memory"); goto done; }
    uint32_t used = 0;
    long long at = (long long)offset + hoff;
    for (uint32_t k = 0; k < keys && used < length; k++) {
        uint32_t key = le32(tab + 4 * k), len = le32(tab + 4 * (keys + k));
        if (len > BLOCK_OUT * 2 || at + len > size || read_at(f, at, blk, len)) {
            fail(why, n, "%s: block outside the archive", base_name(archive));
            goto done;
        }
        at += len;
        pe_imp_rofs_decrypt(blk, len, key);
        if (packed) {
            used += pe_imp_rofs_depack(blk, len, buf + used, length - used < BLOCK_OUT ? length - used : BLOCK_OUT);
        } else {
            uint32_t c = len < length - used ? len : length - used;
            memcpy(buf + used, blk, c);
            used += c;
        }
    }
    if (used != length) { fail(why, n, "%s: a file came out short", base_name(archive)); goto done; }
    *data = buf, *got = length, buf = NULL;
    rc = 0;
done:
    free(tab), free(blk), free(buf);
    fclose(f);
    return rc;
}
