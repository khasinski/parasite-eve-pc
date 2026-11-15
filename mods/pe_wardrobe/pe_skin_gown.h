/* Private load-time repair of legacy, Aya-layout Melissa/opera actress skins.
 * Rows are rebuilt because quaternion interpolation couples all three axes: a
 * constant input channel can become sampled. Never overwrite installed files. */
#ifndef PE_SKIN_GOWN_H
#define PE_SKIN_GOWN_H
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

typedef struct {
    const uint8_t *p;
    unsigned size, constant, wide;
} PeGownChannel;
static unsigned pg_u16(const uint8_t *p) { return p[0] | (unsigned)p[1] << 8; }
static uint32_t pg_u32(const uint8_t *p) { return pg_u16(p) | (uint32_t)pg_u16(p + 2) << 16; }
static void pg_put32(uint8_t *p, uint32_t v) {
    for (int i = 0; i < 4; ++i)
        p[i] = (uint8_t)(v >> (8 * i));
}
static int pg_leg(unsigned i) { return (i >= 21 && i <= 23) || (i >= 25 && i <= 27); }
static int pg_legacy(const char *folder) {
    return folder && (!strcmp(folder, "melissa") || !strcmp(folder, "opera_actress"));
}
static int pg_prepared(const uint8_t *meta, unsigned size) {
    return size >= 12 && !memcmp(meta, "PEMT", 4) && !memcmp(meta + 8, "GWN1", 4);
}
static int pg_channels(const uint8_t *p, unsigned n, PeGownChannel c[90]) {
    unsigned at = 12, frames, wide;
    if (n < 12 || p[1] != 28 || !(frames = p[2]))
        return 0;
    wide = (p[0] & 3) == 2;
    for (unsigned i = 0; i < 90; ++i) {
        unsigned w = i < 3 || wide, constant, size;
        if (at > n || n - at < 4)
            return 0;
        constant = w ? pg_u16(p + at) != 0 : p[at] != 0;
        size = constant ? 4 : w ? ((frames >> 1) + 1) * 4 : ((frames >> 2) + 1) * 4;
        if (size > n - at)
            return 0;
        c[i] = (PeGownChannel){p + at, size, constant, w};
        at += size;
    }
    return (int)at;
}
static unsigned pg_sample(PeGownChannel c, unsigned frame) {
    unsigned at = c.wide ? 2 : 1;
    if (!c.constant)
        at += frame * (c.wide ? 2 : 1);
    return c.wide ? pg_u16(c.p + at) : c.p[at] * 16u;
}
/* YXZ is Ry * Rx * Rz. Use quaternions, not independently wrapped Euler axes. */
static void pg_quat(const unsigned a[3], double q[4]) {
    const double k = 3.14159265358979323846 / 4096.;
    double x = a[0] * k, y = a[1] * k, z = a[2] * k;
    double cx = cos(x), sx = sin(x), cy = cos(y), sy = sin(y), cz = cos(z), sz = sin(z);
    q[0] = cy * cx * cz + sy * sx * sz;
    q[1] = cy * sx * cz + sy * cx * sz;
    q[2] = sy * cx * cz - cy * sx * sz;
    q[3] = cy * cx * sz - sy * sx * cz;
}
static void pg_damp(const unsigned a[3], const unsigned rest[3], unsigned out[3]) {
    double r[4], q[4], dot = 0, theta, s0, s1, n = 0;
    pg_quat(rest, r);
    pg_quat(a, q);
    for (int i = 0; i < 4; ++i)
        dot += r[i] * q[i];
    if (dot < 0) {
        dot = -dot;
        for (int i = 0; i < 4; ++i)
            q[i] = -q[i];
    }
    if (dot > 1)
        dot = 1;
    theta = acos(dot);
    if (theta < 1e-6) {
        memcpy(out, rest, 3 * sizeof *out);
        return;
    }
    s0 = sin(.8 * theta) / sin(theta);
    s1 = sin(.2 * theta) / sin(theta);
    for (int i = 0; i < 4; ++i) {
        q[i] = s0 * r[i] + s1 * q[i];
        n += q[i] * q[i];
    }
    n = sqrt(n);
    for (int i = 0; i < 4; ++i)
        q[i] /= n;
    double w = q[0], x = q[1], y = q[2], z = q[3], sx = 2 * (w * x - y * z);
    if (sx > 1)
        sx = 1;
    if (sx < -1)
        sx = -1;
    double angle[3] = {asin(sx), 0, 0};
    if (fabs(sx) > .99999)
        angle[1] = atan2(2 * (w * y - x * z), 1 - 2 * (y * y + z * z));
    else {
        angle[1] = atan2(2 * (x * z + w * y), 1 - 2 * (x * x + y * y));
        angle[2] = atan2(2 * (x * y + w * z), 1 - 2 * (x * x + z * z));
    }
    for (int i = 0; i < 3; ++i)
        out[i] = (unsigned)(int)lround(angle[i] * 4096 / (2 * 3.14159265358979323846)) & 4095;
}

/* Re-encode just the six leg bones; retain header, translation and other bones
 * byte-for-byte, including the weapon hand. At most 255 frames per row. */
static unsigned pg_row(const uint8_t *p, unsigned n, const unsigned rest[29][3], uint8_t *out) {
    PeGownChannel c[90];
    unsigned values[255][3], at = 12, frames;
    if (!pg_channels(p, n, c))
        return 0;
    frames = p[2];
    memcpy(out, p, 12);
    for (unsigned i = 0; i < 90;) {
        unsigned bone = i >= 3 ? (i - 3) / 3 : 0;
        if (i < 3 || !pg_leg(bone)) {
            memcpy(out + at, c[i].p, c[i].size);
            at += c[i++].size;
            continue;
        }
        for (unsigned f = 0; f < frames; ++f) {
            unsigned a[3];
            for (unsigned j = 0; j < 3; ++j)
                a[j] = pg_sample(c[i + j], f);
            pg_damp(a, rest[bone], values[f]);
        }
        for (unsigned j = 0; j < 3; ++j) {
            unsigned wide = c[i + j].wide, constant = 1, sz;
            for (unsigned f = 0; f < frames; ++f) {
                if (!wide)
                    values[f][j] = ((values[f][j] + 8) / 16) & 255;
                if (values[f][j] != values[0][j])
                    constant = 0;
            }
            sz = constant ? 4 : wide ? ((frames >> 1) + 1) * 4 : ((frames >> 2) + 1) * 4;
            memset(out + at, 0, sz);
            out[at] = (uint8_t)constant;
            for (unsigned f = 0; f < (constant ? 1 : frames); ++f) {
                unsigned pos = at + (wide ? 2 + 2 * f : 1 + f), v = values[f][j];
                out[pos] = (uint8_t)v;
                if (wide)
                    out[pos + 1] = (uint8_t)(v >> 8);
            }
            at += sz;
        }
        i += 3;
    }
    return at;
}
/* Container tables: model/day bankRowEntries, or PERR fingerprints. Bounds are
 * checked before dereferencing even optional/malformed animation packs. */
static int pg_table(const uint8_t *p, unsigned n, int rooms, unsigned *at, unsigned *count) {
    if (n < 8)
        return 0;
    if (rooms) {
        if (memcmp(p, "PERR", 4))
            return 0;
        *at = 8;
        *count = pg_u32(p + 4);
    } else {
        unsigned d = pg_u32(p + 4), v;
        if (d > n || n - d < 52)
            return 0;
        v = pg_u32(p + d + 16);
        *at = v & 0x3fffff;
        *count = v >> 22;
    }
    return *at <= n && *count <= (n - *at) / 12;
}
static int pg_rest(const uint8_t *p, unsigned n, unsigned rest[29][3]) {
    unsigned at, count;
    if (!pg_table(p, n, 0, &at, &count))
        return 0;
    for (unsigned i = 0; i < count; ++i) {
        const uint8_t *rec = p + at + 12 * i;
        unsigned off = pg_u32(rec + 4) & 0xffffff;
        PeGownChannel c[90];
        if (rec[7] != 0x15)
            continue;
        if (off > n || !pg_channels(p + off, n - off, c))
            return 0;
        for (unsigned b = 0; b < 29; ++b)
            for (unsigned j = 0; j < 3; ++j)
                rest[b][j] = pg_sample(c[3 + 3 * b + j], 0);
        return 1;
    }
    return 0;
}
static int pg_blob(uint8_t **data, unsigned *size, int rooms, const unsigned rest[29][3]) {
    unsigned table, count, n = *size, used, cap;
    uint8_t *out = NULL, *row = NULL;
    const uint8_t *p = *data;
    if (n > 0xffffff || !pg_table(p, n, rooms, &table, &count))
        return 0;
    used = cap = (n + 3) & ~3u;
    out = malloc(cap);
    row = malloc(0x10000);
    if (!out || !row)
        goto fail;
    memcpy(out, p, n);
    memset(out + n, 0, used - n);
    for (unsigned i = 0; i < count; ++i) {
        unsigned rec = table + 12 * i, off = pg_u32(p + rec + 4), limit = n, len;
        if (!rooms)
            off &= 0xffffff;
        else
            limit = pg_u32(p + rec + 8);
        if (off > n || (rooms && limit > n - off))
            goto fail;
        if (!rooms)
            limit = n - off;
        len = pg_row(p + off, limit, rest, row);
        if (!len)
            goto fail;
        /* 24-bit source offsets are part of the native container format. */
        if (used > 0xffffff - len - 3)
            goto fail;
        unsigned next = (used + len + 3) & ~3u;
        if (next > cap) {
            unsigned grow = next + 0x10000;
            uint8_t *tmp = realloc(out, grow);
            if (!tmp)
                goto fail;
            out = tmp;
            cap = grow;
        }
        memcpy(out + used, row, len);
        memset(out + used + len, 0, next - used - len);
        pg_put32(out + rec + 4, used | (rooms ? 0 : pg_u32(p + rec + 4) & 0xff000000));
        if (rooms)
            pg_put32(out + rec + 8, len);
        else
            pg_put32(out + rec, len);
        used = next;
    }
    free(row);
    free(*data);
    *data = out;
    *size = used;
    return 1;
fail:
    free(row);
    free(out);
    return 0;
}
#endif
