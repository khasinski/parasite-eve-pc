/* Pictures: PS1 TIM images kept as stored (pe_imp_logic.h), and a TIM decoder.
 * A TIM: u32 0x10, u32 flags (bits 0-1: 0 4-bit, 1 8-bit, 2 15-bit, 3 24-bit; bit 3: a palette
 * follows), [palette block: u32 bytes, u16 x, y, w, h, colours], image block: u32 bytes, u16 x, y,
 * w (in 16-bit units), h, pixels. Colours are 15-bit BGR; 0 is see-through. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pe_imp_pictures.h"

static uint32_t le16(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8; }
static uint32_t le32(const uint8_t *p) { return le16(p) | le16(p + 2) << 16; }

uint32_t pe_imp_tim_size(const uint8_t *tim, uint32_t size) {
    if (size < 20 || le32(tim) != 0x10) return 0;
    uint32_t flags = le32(tim + 4), at = 8;
    if (flags & ~0xFu || (flags & 3) == 3) return 0;
    if (flags & 8) {
        uint32_t n = le32(tim + at);
        if (n < 12 || n > size - at) return 0;
        at += n;
    }
    if (size - at < 12) return 0;
    uint32_t n = le32(tim + at), w = le16(tim + at + 8), h = le16(tim + at + 10);
    if (n < 12 || n > size - at || !w || !h || w > 1024 || h > 512 || n - 12 < w * h * 2) return 0;
    return at + n;
}

uint8_t *pe_imp_tim_decode(const uint8_t *tim, uint32_t size, unsigned *width, unsigned *height) {
    uint32_t total = pe_imp_tim_size(tim, size);
    if (!total) return NULL;
    uint32_t flags = le32(tim + 4), at = 8, bpp = flags & 3, colours = 0;
    const uint8_t *palette = NULL;
    if (flags & 8) {
        uint32_t n = le32(tim + at);
        colours = (n - 12) / 2, palette = tim + at + 12;
        at += n;
    }
    if (bpp < 2 && colours < (bpp ? 256u : 16u)) return NULL;
    uint32_t w16 = le16(tim + at + 8), h = le16(tim + at + 10);
    const uint8_t *px = tim + at + 12;
    unsigned w = bpp == 0 ? w16 * 4 : bpp == 1 ? w16 * 2 : w16;
    uint8_t *rgba = malloc((size_t)w * h * 4);
    if (!rgba) return NULL;
    for (uint32_t y = 0; y < h; y++)
        for (uint32_t x = 0; x < w; x++) {
            uint32_t v;
            if (bpp == 0) v = le16(palette + 2 * ((px[y * w16 * 2 + x / 2] >> (x & 1) * 4) & 15));
            else if (bpp == 1) v = le16(palette + 2 * px[y * w16 * 2 + x]);
            else v = le16(px + (y * w16 + x) * 2);
            uint8_t *o = rgba + ((size_t)y * w + x) * 4;
            o[0] = (uint8_t)((v & 31) << 3), o[1] = (uint8_t)((v >> 5 & 31) << 3), o[2] = (uint8_t)((v >> 10 & 31) << 3);
            o[3] = v ? 255 : 0;
        }
    *width = w, *height = h;
    return rgba;
}

const uint8_t *pe_imp_picture_tim(const PeImpRoomLogic *l, uint32_t i, uint32_t *size) {
    uint32_t count, bytes;
    const uint8_t *p = pe_imp_logic_section(l, PE_IMP_PICTURE_TIMS, &count, &bytes);
    if (!p || i >= count) return NULL;
    uint32_t at = 0;
    for (uint32_t k = 0; k <= i; k++) {
        if (bytes - at < 4) return NULL;
        uint32_t n = le32(p + at);
        if (n > bytes - at - 4) return NULL;
        if (k == i) { if (size) *size = n; return p + at + 4; }
        at += 4 + ((n + 3) & ~3u);
    }
    return NULL;
}

int pe_imp_emit_tims(PeImpSink *sink, const char *id, const char *name, const uint8_t *data, uint32_t size,
                     uint32_t step, char *why, size_t n) {
    return pe_imp_emit_tims_ex(sink, id, name, data, size, step, 0, why, n);
}

/* A palette (its first 16 colours used) of `palette_bytes`, then 4-bit pixels `width` wide (RE1's file
 * pages): cut into pages `page_h` lines tall, each wrapped as a 4-bit TIM with that palette. */
int pe_imp_emit_pix4(PeImpSink *sink, const char *id, const char *name, const uint8_t *data, uint32_t size,
                     uint32_t palette_bytes, uint32_t width, uint32_t page_h, char *why, size_t n) {
    if (palette_bytes < 32 || size <= palette_bytes || !width || width % 4 || width > 1024 || !page_h || page_h > 512) return 0;
    uint32_t page = width / 2 * page_h, pages = (size - palette_bytes) / page, tim = 8 + 44 + 12 + page;
    if (!pages) return 0;
    uint8_t *packed = malloc((size_t)pages * (4 + tim));
    if (!packed) { if (why && n) snprintf(why, n, "out of memory"); return -1; }
    for (uint32_t p = 0; p < pages; p++) {
        uint8_t *o = packed + (size_t)p * (4 + tim), *t = o + 4;
        for (int k = 0; k < 4; k++) o[k] = (uint8_t)(tim >> 8 * k);
        memset(t, 0, 8), t[0] = 0x10, t[4] = 8;
        uint8_t *c = t + 8;
        memset(c, 0, 12), c[0] = 44, c[8] = 16, c[10] = 1;
        memcpy(c + 12, data, 32);
        uint8_t *img = c + 44;
        uint32_t bytes = 12 + page;
        memset(img, 0, 12);
        for (int k = 0; k < 4; k++) img[k] = (uint8_t)(bytes >> 8 * k);
        img[8] = (uint8_t)(width / 4), img[9] = (uint8_t)(width / 4 >> 8), img[10] = (uint8_t)page_h, img[11] = (uint8_t)(page_h >> 8);
        memcpy(img + 12, data + palette_bytes + (size_t)p * page, page);
    }
    PeImpLogicBuilder *b = pe_imp_logic_begin(0, 0, 0, 0);
    pe_imp_logic_add(b, PE_IMP_PICTURE_TIMS, pages, packed, pages * (4 + tim));
    free(packed);
    PeImpRoomLogic *l = pe_imp_logic_finish(b);
    if (!l) { if (why && n) snprintf(why, n, "%s: too large", id); return -1; }
    if (pe_imp_emit(sink, PE_IMP_PICTURES, id, name, l)) {
        if (why && n) snprintf(why, n, "couldn't catalog %s", id);
        return -1;
    }
    return 0;
}

/* 256 x 256 pages at 4 bits a pixel without a palette (RE2's maps): each wrapped as a 4-bit TIM with
 * a placeholder palette of 16 greys (PE_IMP_PICTURE_GREY marks the record) */
int pe_imp_emit_pages4(PeImpSink *sink, const char *id, const char *name, const uint8_t *data, uint32_t size,
                       char *why, size_t n) {
    enum { PAGE = 256 * 256 / 2, TIM = 8 + 12 + 32 + 12 + PAGE };
    uint32_t pages = size / PAGE;
    if (!pages) return 0;
    uint8_t *packed = malloc((size_t)pages * (4 + TIM));
    if (!packed) { if (why && n) snprintf(why, n, "out of memory"); return -1; }
    static const uint8_t k_head[8] = {0x10, 0, 0, 0, 8, 0, 0, 0};
    for (uint32_t p = 0; p < pages; p++) {
        uint8_t *o = packed + p * (4 + TIM), *t = o + 4;
        o[0] = (uint8_t)TIM, o[1] = (uint8_t)(TIM >> 8), o[2] = (uint8_t)(TIM >> 16), o[3] = 0;
        memcpy(t, k_head, 8);
        uint8_t *c = t + 8;                                /* palette block: 44 bytes, 16 x 1 at (0, 0) */
        memset(c, 0, 12), c[0] = 44, c[8] = 16, c[10] = 1;
        for (int k = 0; k < 16; k++) {
            unsigned g = k ? (unsigned)(k * 2 + 1) : 0;   /* 0 stays see-through, as on the PS1 */
            unsigned v = g | g << 5 | g << 10;
            c[12 + 2 * k] = (uint8_t)v, c[13 + 2 * k] = (uint8_t)(v >> 8);
        }
        uint8_t *img = c + 44;                             /* image block: 64 x 256 16-bit units */
        uint32_t bytes = 12 + PAGE;
        memset(img, 0, 12);
        for (int k = 0; k < 4; k++) img[k] = (uint8_t)(bytes >> 8 * k);
        img[8] = 64, img[10] = 0, img[11] = 1;
        memcpy(img + 12, data + (size_t)p * PAGE, PAGE);
    }
    PeImpLogicBuilder *b = pe_imp_logic_begin(0, 0, 0, 0);
    pe_imp_logic_add(b, PE_IMP_PICTURE_TIMS, pages, packed, pages * (4 + TIM));
    pe_imp_logic_add(b, PE_IMP_PICTURE_GREY, 0, NULL, 0);
    free(packed);
    PeImpRoomLogic *l = pe_imp_logic_finish(b);
    if (!l) { if (why && n) snprintf(why, n, "%s: too large", id); return -1; }
    if (pe_imp_emit(sink, PE_IMP_PICTURES, id, name, l)) {
        if (why && n) snprintf(why, n, "couldn't catalog %s", id);
        return -1;
    }
    return 0;
}

static int emit_tims(PeImpSink *sink, const char *id, const char *name, const uint8_t *data, uint32_t size,
                     uint32_t step, int keep_file, int docs, uint32_t first_item, char *why, size_t n) {
    uint8_t *packed = malloc((size_t)size + 4096), *doc = NULL;
    uint32_t total = 0, count = 0, docs_n = 0;
    if (!packed) { if (why && n) snprintf(why, n, "out of memory"); return -1; }
    for (uint32_t at = 0; at + 20 <= size;) {
        uint32_t t = pe_imp_tim_size(data + at, size - at);
        if (!t) { at += step ? step : size; continue; }
        if (total + 8 + t > size + 4096) break;
        for (int k = 0; k < 4; k++) packed[total + k] = (uint8_t)(t >> 8 * k);
        memcpy(packed + total + 4, data + at, t);
        uint32_t padded = (t + 3) & ~3u;
        memset(packed + total + 4 + t, 0, padded - t);
        if (docs && count < 0xFFFF) {
            if ((data[at + 4] & 3) == 1 || !docs_n) {                   /* an 8-bit background starts one */
                uint8_t *more = realloc(doc, 8ull * (docs_n + 1));
                if (!more) { free(doc), free(packed); if (why && n) snprintf(why, n, "out of memory"); return -1; }
                doc = more;
                uint8_t *o = doc + 8 * docs_n;
                uint32_t item = first_item ? first_item + docs_n : 0;
                memset(o, 0, 8);
                o[0] = (uint8_t)count, o[1] = (uint8_t)(count >> 8), o[4] = (uint8_t)item, o[5] = (uint8_t)(item >> 8);
                docs_n++;
            }
            uint8_t *o = doc + 8 * (docs_n - 1);
            uint32_t c = (o[2] | (uint32_t)o[3] << 8) + 1;
            o[2] = (uint8_t)c, o[3] = (uint8_t)(c >> 8);
        }
        total += 4 + padded, count++;
        if (!step) break;
        uint32_t next = at + padded;                                     /* one right after it, else the next step */
        at = next + 20 <= size && pe_imp_tim_size(data + next, size - next) ? next : (uint32_t)(((uint64_t)at + t + step - 1) / step * step);
    }
    if (!count) { free(packed), free(doc); return 0; }               /* no pictures in it */
    PeImpLogicBuilder *b = pe_imp_logic_begin(0, 0, 0, 0);
    pe_imp_logic_add(b, PE_IMP_PICTURE_TIMS, count, packed, total);
    if (docs_n) pe_imp_logic_add(b, PE_IMP_PICTURE_DOCS, docs_n, doc, 8 * docs_n);
    if (keep_file) pe_imp_logic_add(b, PE_IMP_PICTURE_FILE, 0, data, size);
    free(packed), free(doc);
    PeImpRoomLogic *l = pe_imp_logic_finish(b);
    if (!l) { if (why && n) snprintf(why, n, "%s: too large", id); return -1; }
    if (pe_imp_emit(sink, PE_IMP_PICTURES, id, name, l)) {
        if (why && n) snprintf(why, n, "couldn't catalog %s", id);
        return -1;
    }
    return 0;
}

int pe_imp_emit_tims_ex(PeImpSink *sink, const char *id, const char *name, const uint8_t *data, uint32_t size,
                        uint32_t step, int keep_file, char *why, size_t n) {
    return emit_tims(sink, id, name, data, size, step, keep_file, 0, 0, why, n);
}

int pe_imp_emit_documents(PeImpSink *sink, const char *id, const char *name, const uint8_t *data, uint32_t size,
                          uint32_t step, uint32_t first_item, char *why, size_t n) {
    return emit_tims(sink, id, name, data, size, step, 0, 1, first_item, why, n);
}

int pe_imp_emit_pix8(PeImpSink *sink, const char *id, const char *name, const uint8_t *pixels, uint32_t size,
                     const uint8_t *palette, uint32_t width, uint32_t height, char *why, size_t n) {
    if (!width || width % 2 || width > 1024 || !height || height > 512) return 0;
    uint32_t page = width * height, count = size / page, tim = 8 + 12 + 512 + 12 + page;
    if (!count) return 0;
    uint8_t *packed = malloc((size_t)count * (4 + ((tim + 3) & ~3u)));
    if (!packed) { if (why && n) snprintf(why, n, "out of memory"); return -1; }
    uint32_t total = 0;
    for (uint32_t p = 0; p < count; p++) {
        uint8_t *o = packed + total, *t = o + 4;
        for (int k = 0; k < 4; k++) o[k] = (uint8_t)(tim >> 8 * k);
        memset(t, 0, 8), t[0] = 0x10, t[4] = 9;                         /* 8-bit, with a palette */
        uint8_t *c = t + 8;
        memset(c, 0, 12), c[0] = (uint8_t)(524 & 255), c[1] = (uint8_t)(524 >> 8), c[9] = 1, c[10] = 1;   /* 256 x 1 */
        memcpy(c + 12, palette, 512);
        uint8_t *img = c + 524;
        uint32_t bytes = 12 + page;
        memset(img, 0, 12);
        for (int k = 0; k < 4; k++) img[k] = (uint8_t)(bytes >> 8 * k);
        img[8] = (uint8_t)(width / 2), img[9] = (uint8_t)(width / 2 >> 8), img[10] = (uint8_t)height, img[11] = (uint8_t)(height >> 8);
        memcpy(img + 12, pixels + (size_t)p * page, page);
        uint32_t padded = (tim + 3) & ~3u;
        memset(t + tim, 0, padded - tim);
        total += 4 + padded;
    }
    PeImpLogicBuilder *b = pe_imp_logic_begin(0, 0, 0, 0);
    pe_imp_logic_add(b, PE_IMP_PICTURE_TIMS, count, packed, total);
    free(packed);
    PeImpRoomLogic *l = pe_imp_logic_finish(b);
    if (!l) { if (why && n) snprintf(why, n, "%s: too large", id); return -1; }
    if (pe_imp_emit(sink, PE_IMP_PICTURES, id, name, l)) {
        if (why && n) snprintf(why, n, "couldn't catalog %s", id);
        return -1;
    }
    return 0;
}
