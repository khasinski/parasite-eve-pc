#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../mods/pe_wardrobe/pe_skin_collision.h"

static void w32(uint8_t *b, size_t at, uint32_t n) {
    unsigned i;
    for (i = 0; i < 4; ++i) b[at+i] = (uint8_t)(n >> (8*i));
}

static void fixture(uint8_t *b, unsigned radius) {
    memset(b, 0xa5, 256);
    w32(b, 4, 8);                    /* directory */
    w32(b, 20, (1u << 22) | 60);    /* root table */
    w32(b, 60, 184);                 /* model length */
    w32(b, 64, 72);                  /* model offset / slot zero */
    memset(b + 72, 0, 28);
    b[74] = 1;                      /* one part */
    b[78] = 2;                      /* two vertices */
    b[80] = 1;                      /* one primitive descriptor */
    b[154] = (uint8_t)radius;        /* section14.pad */
    b[155] = 0;
}

static void rejected_unchanged(uint8_t *b, size_t n) {
    uint8_t before[256];
    memcpy(before, b, sizeof before);
    assert(!pe_skin_normalize_movement(b, n));
    assert(!memcmp(before, b, sizeof before));
}

int main(void) {
    uint8_t b[256], before[256];
    unsigned radii[] = {98, 95, 74, 63, 50};
    size_t r, i;
    for (r = 0; r < sizeof radii / sizeof radii[0]; ++r) {
        fixture(b, radii[r]);
        memcpy(before, b, sizeof before);
        assert(pe_skin_normalize_movement(b, sizeof b));
        assert(b[154] == 63 && b[155] == 0);
        for (i = 0; i < sizeof b; ++i)
            if (i != 154 && i != 155) assert(b[i] == before[i]);
        memcpy(before, b, sizeof before);
        assert(pe_skin_normalize_movement(b, sizeof b));
        assert(!memcmp(before, b, sizeof b));
    }
    assert(!pe_skin_normalize_movement(NULL, 256));
    fixture(b, 98); rejected_unchanged(b, 7);
    fixture(b, 98); w32(b, 4, UINT32_MAX); rejected_unchanged(b, 256);
    fixture(b, 98); w32(b, 20, 60); rejected_unchanged(b, 256);
    fixture(b, 98); w32(b, 20, (1u<<22)|250); rejected_unchanged(b, 256);
    fixture(b, 98); w32(b, 64, 0x01000048); rejected_unchanged(b, 256);
    fixture(b, 98); w32(b, 60, 30); rejected_unchanged(b, 256);
    fixture(b, 98); b[74] = 0; rejected_unchanged(b, 256);
    fixture(b, 98); b[78] = b[79] = 255; rejected_unchanged(b, 256);
    puts("Skin movement collision: radius-only changes, idempotence and malformed input checks passed.");
    return 0;
}
