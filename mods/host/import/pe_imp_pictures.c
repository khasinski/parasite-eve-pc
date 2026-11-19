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
    uint8_t *packed = malloc((size_t)size + 4096);
    uint32_t total = 0, count = 0;
    if (!packed) { if (why && n) snprintf(why, n, "out of memory"); return -1; }
    for (uint32_t at = 0; at + 20 <= size;) {
        uint32_t t = pe_imp_tim_size(data + at, size - at);
        if (!t) { at += step ? step : size; continue; }
        if (total + 8 + t > size + 4096) break;
        for (int k = 0; k < 4; k++) packed[total + k] = (uint8_t)(t >> 8 * k);
        memcpy(packed + total + 4, data + at, t);
        uint32_t padded = (t + 3) & ~3u;
        memset(packed + total + 4 + t, 0, padded - t);
        total += 4 + padded, count++;
        if (!step) break;
        at += (t + step - 1) / step * step;
    }
    if (!count) { free(packed); return 0; }                          /* no pictures in it */
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
