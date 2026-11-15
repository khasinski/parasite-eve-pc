#include <assert.h>
#include <stdio.h>
#include "../mods/pe_wardrobe/pe_skin_gown.h"

static unsigned make_row(uint8_t *p, int wide) {
    unsigned at = 12;
    memset(p, 0, 4096);
    p[0] = wide ? 2 : 0;
    p[1] = 28;
    p[2] = 4;
    for (unsigned i = 0; i < 90; ++i) {
        int w = i < 3 || wide;
        /* Mixed channels, including constants on axes coupled to moving axes. */
        p[at] = 1;
        if (w) {
            p[at + 2] = 128;
            p[at + 3] = 1;
        } else
            p[at + 1] = 24;
        if (i == 3 + 21 * 3) {
            p[at] = 0;
            for (unsigned f = 0; f < 4; ++f) {
                unsigned v = wide ? 4000 + f * 32 : 250 + f * 2;
                unsigned pos = at + (w ? 2 + 2 * f : 1 + f);
                p[pos] = (uint8_t)v;
                if (w)
                    p[pos + 1] = (uint8_t)(v >> 8);
            }
            at += w ? 12 : 8;
        } else
            at += 4;
    }
    return at;
}
static void rotation_test(void) {
    unsigned a[3] = {0, 16, 0}, r[3] = {0, 4080, 0}, out[3];
    pg_damp(a, r, out);
    assert(out[1] >= 4080 && out[1] <= 4095);
    /* Equivalent Euler triples straddling a gimbal representation change. */
    unsigned e[3] = {1025, 2048, 2048}, rest[3] = {1023, 0, 0};
    pg_damp(e, rest, out);
    double q[4], rq[4], dot = 0;
    pg_quat(out, q);
    pg_quat(rest, rq);
    for (int i = 0; i < 4; ++i)
        dot += q[i] * rq[i];
    assert(fabs(dot) > .9999);
}
static void row_test(int wide) {
    uint8_t row[4096], out[65536];
    PeGownChannel before[90], after[90];
    unsigned rest[29][3] = {{0}}, n = make_row(row, wide), len;
    assert(pg_channels(row, n, before));
    len = pg_row(row, n, rest, out);
    assert(len && pg_channels(out, len, after));
    assert(!memcmp(row, out, 12));
    for (unsigned i = 0; i < 90; ++i) {
        if (i < 3 || !pg_leg((i - 3) / 3)) {
            assert(before[i].size == after[i].size);
            assert(!memcmp(before[i].p, after[i].p, before[i].size));
        }
    }
    for (unsigned f = 0; f < 4; ++f) {
        unsigned a[3], expected[3];
        for (int j = 0; j < 3; ++j)
            a[j] = pg_sample(before[66 + j], f);
        pg_damp(a, rest[21], expected);
        for (int j = 0; j < 3; ++j) {
            unsigned want = wide ? expected[j] : (((expected[j] + 8) / 16) & 255) * 16;
            assert(pg_sample(after[66 + j], f) == want);
        }
    }
    for (unsigned size = 0; size < n; ++size)
        assert(!pg_row(row, size, rest, out));
    row[1] = 27;
    assert(!pg_row(row, n, rest, out));
    row[1] = 28;
    row[2] = 0;
    assert(!pg_row(row, n, rest, out));
}
static void blob_test(int rooms) {
    uint8_t row[4096];
    unsigned n = make_row(row, 1), size = 128 + n, rest[29][3] = {{0}};
    uint8_t *p = calloc(1, size), *original;
    unsigned table = rooms ? 8 : 64;
    if (rooms) {
        memcpy(p, "PERR", 4);
        pg_put32(p + 4, 2);
    } else {
        pg_put32(p + 4, 8);
        pg_put32(p + 24, table | (2u << 22));
    }
    /* Aliased rows must each be repaired from the original, never twice. */
    for (unsigned i = 0; i < 2; ++i) {
        pg_put32(p + table + 12 * i, rooms ? 0x12345678 : n);
        pg_put32(p + table + 12 * i + 4, 128 | (rooms ? 0 : 0x15000000));
        pg_put32(p + table + 12 * i + 8, n);
    }
    memcpy(p + 128, row, n);
    if (!rooms)
        assert(pg_rest(p, size, rest));
    original = malloc(size);
    memcpy(original, p, size);
    assert(pg_blob(&p, &size, rooms, rest));
    unsigned off = pg_u32(p + table + 4) & 0xffffff, off2 = pg_u32(p + table + 16) & 0xffffff;
    assert(off >= 128 + n && off2 > off);
    assert(!memcmp(p + 128, row, n));
    unsigned len = rooms ? pg_u32(p + table + 8) : pg_u32(p + table);
    assert(!memcmp(p + off, p + off2, len));
    if (rooms)
        assert(pg_u32(p + table) == 0x12345678);
    free(p);
    /* Reject a malformed offset without modifying/replacing the source. */
    p = original;
    size = 128 + n;
    pg_put32(p + table + 4, 0xffffff);
    assert(!pg_blob(&p, &size, rooms, rest) && p == original && size == 128 + n);
    free(p);
}
int main(int argc, char **argv) {
    rotation_test();
    row_test(0);
    row_test(1);
    blob_test(0);
    blob_test(1);
    assert(pg_legacy("melissa") && pg_legacy("opera_actress"));
    assert(!pg_legacy("daniel") && !pg_legacy("@custom/melissa"));
    const uint8_t prepared[12] = {'P', 'E', 'M', 'T', 18, 0, 0, 0, 'G', 'W', 'N', '1'};
    assert(pg_prepared(prepared, 12));
    for (unsigned n = 0; n < 12; ++n)
        assert(!pg_prepared(prepared, n));
    assert(!pg_prepared(NULL, 0));
    /* Optional locally owned disc assets; never required by CI or committed. */
    if (argc == 3) {
        const char *names[] = {"model.bin", "day1.bin", "day2.bin", "day3.bin", "day4.bin",
                               "day5.bin",  "day6.bin", "day7.bin", "day8.bin", "rooms.bin"};
        unsigned rest[29][3];
        for (unsigned i = 0; i < 10; ++i) {
            char path[2048];
            snprintf(path, sizeof path, "%s/%s", argv[1], names[i]);
            FILE *f = fopen(path, "rb");
            assert(f);
            fseek(f, 0, SEEK_END);
            unsigned n = (unsigned)ftell(f);
            rewind(f);
            uint8_t *p = malloc(n);
            assert(fread(p, 1, n, f) == n);
            fclose(f);
            if (!i)
                assert(pg_rest(p, n, rest));
            assert(pg_blob(&p, &n, i == 9, rest));
            snprintf(path, sizeof path, "%s/%s", argv[2], names[i]);
            f = fopen(path, "wb");
            assert(f);
            assert(fwrite(p, 1, n, f) == n);
            fclose(f);
            free(p);
        }
    }
    puts("gown rotation, channel preservation, containers and malformed rows: OK");
    return 0;
}
