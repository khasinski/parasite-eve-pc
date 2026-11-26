/* MDEC frames to RGB (pe_imp_mdec.h), in three steps:
 *   1. the VLC bitstream to MDEC run/level codes: LIBPRESS's DecDCTvlc2 (decompiled in
 *      src/overlays/mdec_diag/vlc_c.c), reworked to keep its state on the stack and to decode a
 *      whole frame at once, with the game's own lookup table
 *   2. run/level codes to 8x8 blocks: dequantised with LIBPRESS's default tables (DecDCTReset),
 *      inverse DCT as psyz's MDEC emulation does it (third_party/psyz, MIT)
 *   3. YCbCr macroblocks (Cr, Cb, Y0-Y3) to RGB, as the MDEC does in 24-bit mode */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pe_imp_mdec.h"

#include <SDL3/SDL.h>

static void (*s_build)(void *);                   /* src/overlays/mdec_diag/vlc_build.c, handed in */

void pe_imp_mdec_set_table(void (*build)(void *table)) { s_build = build; }

#define TABLE_BYTES 0x11000u

static uint8_t *s_table;
static SDL_InitState s_table_once;

static const uint8_t *table(void) {
    if (!s_build) return NULL;
    if (SDL_ShouldInit(&s_table_once)) {
        uint8_t *t = calloc(1, TABLE_BYTES + 64);
        if (t) s_build(t);
        s_table = t;
        SDL_SetInitialized(&s_table_once, t != NULL);
    }
    return s_table;
}

static unsigned rd16(const uint8_t *p) { return p[0] | (unsigned)p[1] << 8; }
static uint32_t rd32(const uint8_t *p) { return rd16(p) | (uint32_t)rd16(p + 2) << 16; }
static unsigned next16(const uint8_t *p, const uint8_t *end) { return p + 2 <= end ? rd16(p) : 0; }

/* step 1: the bitstream to run/level codes (u16 each) into out, at most `cap` of them */
static int vlc(const uint8_t *frame, uint32_t size, uint16_t *out, uint32_t cap, uint32_t *count) {
    const uint8_t *t = table(), *src = frame + 12, *end = frame + size;
    if (!t || size < 12) return -1;
    uint32_t bits = rd16(frame + 8) << 16 | rd16(frame + 10), used = 0;
    uint32_t quant = rd16(frame + 4) << 10, block = rd16(frame + 6) >= 3 ? 1 : 0, pred[3] = {0, 0, 0};
    uint32_t n = 0;
    /* past the end the stream reads as zeros (the reader looks ahead), up to a few words */
#define NEXT16() do { if (used & 16) { used &= 15; if (src + 2 > end + 16) return -1; bits |= next16(src, end) << used; src += 2; } else used &= 15; } while (0)
#define PUT(v) do { if (n >= cap) return -1; out[n++] = (uint16_t)(v); } while (0)
    for (;;) {
        /* DC */
        uint32_t dc = bits >> 22, code;
        if (dc == (block ? 0x3ffu : 0x1ffu)) break;           /* end of frame */
        if (block) {
            const uint8_t *e = t + (block < 3 ? 0x400 : 0) + (bits >> 24) * 4;
            uint32_t length = rd16(e), extra = rd16(e + 2);
            bits <<= length & 31;
            dc = 0;
            if (extra) {
                dc = bits >> ((32 - extra) & 31);
                if (!(bits & 0x80000000u)) dc -= 0xffffffffu >> ((32 - extra) & 31);
                bits <<= extra & 31;
                used += extra;
            }
            used += length;
            unsigned comp = block < 3 ? block - 1 : 2;
            pred[comp] += dc;
            code = quant | ((pred[comp] << 2) & 0x3ff);
            if (++block == 7) block = 1;
        } else {
            code = quant | dc;
            bits <<= 10;
            used += 10;
        }
        NEXT16();
        PUT(code);
        /* AC until the end of block (0xfe00) */
        for (int guard = 0;; guard++) {
            if (guard > 256) return -1;
            const uint8_t *e = t + 0x800 + (bits >> 19) * 8;
            uint32_t packed = rd32(e), extra = 0;
            if (packed) extra = rd32(e + 4);
            else {
                bits <<= 8;
                used += 8;
                NEXT16();
                packed = rd32(t + 0x10800 + (bits >> 23) * 4);
            }
            uint32_t len = packed & 255;
            bits <<= len & 31;
            used += len;
            NEXT16();
            code = packed >> 16;
            int eob = 0;
            for (unsigned i = 0;; ++i) {
                if (code == 0x7c1f) {                       /* escape: the next 16 bits as they are */
                    PUT(bits >> 16);
                    if (src + 2 > end + 16) return -1;
                    bits = bits << 16 | next16(src, end) << used;
                    src += 2;
                    break;
                }
                PUT(code);
                if (code == 0xfe00) { eob = 1; break; }
                if (i == 2 || !extra) break;
                code = i == 0 ? extra & 0xffff : extra >> 16;
                if (i == 1 && !code) break;
            }
            if (eob) break;
        }
    }
#undef NEXT16
#undef PUT
    *count = n;
    return 0;
}

/* step 2 */
static const uint8_t k_scan[64] = {
    0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,  12, 19, 26, 33, 40, 48,
    41, 34, 27, 20, 13, 6,  7,  14, 21, 28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23,
    30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};
/* LIBPRESS's default quantisation table (both luma and chroma) and DCT matrix (mdec_iq, mdec_coef) */
static const uint32_t k_iq[16] = {0x13101002, 0x16161310, 0x16161616, 0x1B1A181A, 0x1A1A1B1B, 0x1B1B1A1A,
                                  0x1D1D1D1B, 0x1D222222, 0x1B1B1D1D, 0x20201D1D, 0x26252222, 0x22232325,
                                  0x28262623, 0x30302828, 0x38382E2E, 0x5345453A};
static const uint32_t k_coef[32] = {
    0x5A825A82, 0x5A825A82, 0x5A825A82, 0x5A825A82, 0x6A6D7D8A, 0x18F8471C, 0xB8E3E707, 0x82759592,
    0x30FB7641, 0x89BECF04, 0xCF0489BE, 0x764130FB, 0xE7076A6D, 0xB8E38275, 0x7D8A471C, 0x959218F8,
    0xA57D5A82, 0x5A82A57D, 0xA57D5A82, 0x5A82A57D, 0x8275471C, 0x6A6D18F8, 0xE7079592, 0xB8E37D8A,
    0x89BE30FB, 0xCF047641, 0x7641CF04, 0x30FB89BE, 0xB8E318F8, 0x82756A6D, 0x95927D8A, 0xE707471C};

static int fshift(int64_t v, unsigned s) {
    int64_t d = INT64_C(1) << s;
    return (int)(v >= 0 ? v / d : -((-v + d - 1) / d));
}
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static int sbits(unsigned v, unsigned b) { unsigned s = 1u << (b - 1); return (int)(v & (s - 1)) - (int)(v & s); }

static void idct(int block[64]) {
    int work[64];
    for (unsigned pass = 0; pass < 2; ++pass) {
        for (unsigned y = 0; y < 8; ++y)
            for (unsigned x = 0; x < 8; ++x) {
                int64_t sum = 0;
                for (unsigned z = 0; z < 8; ++z) {
                    unsigned k = x + z * 8;
                    int16_t c = (int16_t)(k & 1 ? k_coef[k / 2] >> 16 : k_coef[k / 2] & 0xffff);
                    sum += (int64_t)block[y + z * 8] * fshift(c, 3);
                }
                work[x + y * 8] = fshift(sum + 0xfff, 13);
            }
        memcpy(block, work, sizeof work);
    }
    for (unsigned i = 0; i < 64; ++i) block[i] = sbits((unsigned)block[i], 9);
}

static int block_of(const uint16_t *codes, uint32_t n, uint32_t *at, int block[64]) {
    const uint8_t *q = (const uint8_t *)k_iq;
    unsigned code;
    do {
        if (*at >= n) return 0;
        code = codes[(*at)++];
    } while (code == 0xfe00);
    memset(block, 0, 64 * sizeof *block);
    unsigned scale = code >> 10, index = 0;
    int value = sbits(code, 10) * q[0];
    for (;;) {
        if (!scale) value = sbits(code, 10) * 2;
        block[scale ? k_scan[index] : index] = clampi(value, -1024, 1023);
        if (index == 63) break;
        if (*at >= n) return -1;
        code = codes[(*at)++];
        index += (code >> 10) + 1;
        if (index >= 64) break;
        value = fshift((int64_t)sbits(code, 10) * q[index] * scale + 4, 3);
    }
    idct(block);
    return 1;
}

int pe_imp_mdec_decode(const uint8_t *frame, uint32_t size, unsigned w, unsigned h, uint8_t *rgb, char *why, size_t n) {
    if (size < 12 || rd16(frame + 2) != 0x3800 || (rd16(frame + 6) != 2 && rd16(frame + 6) != 3) || !w || !h ||
        w % 16 || h % 16 || w > 1024 || h > 1024) {
        if (why && n) snprintf(why, n, "MDEC: not a version 2 or 3 frame");
        return -1;
    }
    uint32_t cap = (w / 16) * (h / 16) * 6 * 65, count = 0;
    uint16_t *codes = malloc(cap * sizeof *codes);
    if (!codes) { if (why && n) snprintf(why, n, "out of memory"); return -1; }
    if (vlc(frame, size, codes, cap, &count)) {
        free(codes);
        if (why && n) snprintf(why, n, "MDEC: the bitstream doesn't decode");
        return -1;
    }
    memset(rgb, 0, (size_t)w * h * 3);
    uint32_t at = 0;
    int blocks[6][64];
    for (unsigned mx = 0; mx < w / 16; mx++)
        for (unsigned my = 0; my < h / 16; my++) {
            for (int b = 0; b < 6; b++) {
                int r = block_of(codes, count, &at, blocks[b]);
                if (r == 0) { free(codes); return 0; }       /* the frame ends early: the rest stays black */
                if (r < 0) {
                    free(codes);
                    if (why && n) snprintf(why, n, "MDEC: a block is cut short");
                    return -1;
                }
            }
            for (unsigned y = 0; y < 16; ++y)
                for (unsigned x = 0; x < 16; ++x) {
                    unsigned chroma = x / 2 + y / 2 * 8;
                    int cr = blocks[0][chroma], cb = blocks[1][chroma];
                    int lum = blocks[2 + x / 8 + y / 8 * 2][x % 8 + y % 8 * 8];
                    int c[3] = {lum + fshift(359 * cr + 128, 8), lum + fshift(-88 * cb - 183 * cr + 128, 8),
                                lum + fshift(454 * cb + 128, 8)};
                    uint8_t *px = rgb + ((size_t)(my * 16 + y) * w + mx * 16 + x) * 3;
                    for (int k = 0; k < 3; k++) px[k] = (uint8_t)(clampi(c[k], -128, 127) + 128);
                }
        }
    free(codes);
    return 0;
}
