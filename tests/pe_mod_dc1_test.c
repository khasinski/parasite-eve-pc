/* pe_src_dc1's reader (mods/pe_src_dc1/pe_src_dc1_core.c).
 *   compression  literals and back references, overlapping runs; bad references refused
 *   made-up      a small CORE archive: image tiles unpacked, bones in RE2's order and turned into RE's
 *                frame, a face stitched across two bones goes to the bone most corners follow,
 *                the right hand mirrored from the left; truncated copies refused
 *   disc         with PE_TEST_DC1_DISC (the Dino Crisis .cue): CORE00-03 read, faces as the Python
 *                prototype counted them (468, 471, 468, 465) */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pe_imp.h"
#include "pe_src_dc1.h"

static uint8_t buf[1 << 18];
static uint32_t at;
static void u8(unsigned v) { buf[at++] = (uint8_t)v; }
static void u16(unsigned v) { u8(v & 255), u8(v >> 8 & 255); }
static void u32(uint32_t v) { u16(v & 0xffff), u16(v >> 16); }
static void put32(uint32_t where, uint32_t v) { for (int i = 0; i < 4; i++) buf[where + (uint32_t)i] = (uint8_t)(v >> 8 * i); }

/* "compress" as literals only: a 0xff flag before every 8 bytes */
static uint32_t literal(const uint8_t *src, uint32_t n) {
    uint32_t start = at;
    for (uint32_t i = 0; i < n; i++) {
        if (i % 8 == 0) u8(0xff);
        u8(src[i]);
    }
    return at - start;
}

static void test_lz(void) {
    /* "ab" then a reference 2 back, run 5 (b1 = 3 << 4): abababa; then a literal z */
    const uint8_t s[] = {0x0b, 'a', 'b', 0x02, 0x30, 'z'};
    uint8_t *o;
    uint32_t n;
    assert(!dc1_lz(s, sizeof s, &o, &n) && n == 8 && !memcmp(o, "abababaz", 8));
    free(o);
    const uint8_t bad[] = {0x00, 0x05, 0x00};          /* a reference before any output */
    assert(dc1_lz(bad, sizeof bad, &o, &n) == -1);
}

/* Dino Crisis's tree: root, arm 1-3, arm 4-6, head 7, waist 8, legs 9-11 and 12-14; x is the side */
static const int k_parent[15] = {255, 0, 1, 2, 0, 4, 5, 0, 0, 8, 9, 10, 8, 12, 13};
static const int k_off[15][3] = {{0, -1000, 0}, {100, -200, 0}, {0, 200, 0}, {0, 200, 0}, {-100, -200, 0}, {0, 200, 0},
                                 {0, 200, 0}, {0, -250, 0}, {0, 0, 0}, {60, 150, 0}, {0, 400, 0}, {0, 400, 0},
                                 {-60, 150, 0}, {0, 400, 0}, {0, 400, 0}};

static uint32_t build(void) {
    static uint8_t model[4096], image[256 * 256 + 8];
    memset(buf, 0, sizeof buf), memset(model, 0, sizeof model);
    /* image: tile t (64 x 32, row by row) filled with t */
    for (uint32_t i = 0; i < 256 * 256; i++) image[i] = (uint8_t)(i / 2048);
    /* model block at 0x80180000: header at 16, bones, vertices (3 per bone), normals, faces */
    uint32_t B = 0x80180000u, h = 16, pv = h + 24 + 20 * 15, pn = pv + 8 * 45, ft = pn + 8 * 45, fq = ft + 16 * 15;
    uint32_t w = 0;
#define M16(o, v) (model[(o)] = (uint8_t)(v), model[(o) + 1] = (uint8_t)((unsigned)(v) >> 8))
#define M32(o, v) (M16(o, (v) & 0xffff), M16((o) + 2, (uint32_t)(v) >> 16))
    M32(h, B + pv), M32(h + 4, B + pn), M32(h + 8, B + ft), M32(h + 12, B + fq);
    uint32_t tris = 0;
    for (int k = 0; k < 15; k++) {
        uint32_t e = h + 24 + 20 * (uint32_t)k;
        M16(e, k_off[k][0]), M16(e + 2, k_off[k][1]), M16(e + 4, k_off[k][2]);
        model[e + 6] = (uint8_t)k_parent[k], model[e + 7] = 1;
        for (uint32_t v = 0; v < 3; v++) {
            M16(pv + 8 * (3 * (uint32_t)k + v), (int)v * 10 + 1), M16(pv + 8 * (3 * (uint32_t)k + v) + 2, k), M16(pv + 8 * (3 * (uint32_t)k + v) + 4, (int)v + 5);
            M16(pn + 8 * (3 * (uint32_t)k + v), 4096), M16(pn + 8 * (3 * (uint32_t)k + v) + 4, 7);
        }
        if (k == 3) continue;                      /* the right hand holds a gun: no faces */
        uint32_t t = ft + 16 * tris;
        M32(e + 8, B + t), M16(e + 16, 1);
        M16(t, 3 * k), M16(t + 2, 3 * k + 1), M16(t + 4, 3 * k + 2);
        model[t + 6] = 1, model[t + 7] = 2, model[t + 8] = 3, model[t + 9] = 4, M16(t + 10, 0x7db0);
        model[t + 12] = 5, model[t + 13] = 6, M16(t + 14, 0x8e);
        tris++;
    }
    /* the waist (8) has a quad: corners 0-2 are the thigh's (9), corner 3 its own: the thigh takes it */
    uint32_t e8 = h + 24 + 20 * 8;
    M32(e8 + 12, B + fq), M16(e8 + 18, 1);
    M16(fq, 27), M16(fq + 2, 28), M16(fq + 4, 29), M16(fq + 6, 24);
    model[fq + 8] = 9, model[fq + 9] = 9, M16(fq + 10, 0x7db0), model[fq + 18] = 7, model[fq + 19] = 7, M16(fq + 14, 0x8e);
    M16(h + 16, tris), M16(h + 18, 1), M16(h + 20, 15), M16(h + 22, 0);
    w = fq + 20;
    /* archive: index, then the image, palette and model blocks, 2 KiB aligned */
    at = 0;
    for (int i = 3; i < 128; i++) memcpy(buf + 16 * i, "dummy header    ", 16);
    at = 0x800;
    uint32_t isz = literal(image, sizeof image);
    put32(0, 8), put32(4, isz), put32(8, 0x380), put32(12, 0x01e00080);
    at = (at + 2047) / 2048 * 2048;
    for (int r = 0; r < 2; r++) for (int c = 0; c < 256; c++) u16((unsigned)(c + r));
    put32(16, 2), put32(20, 1024), put32(24, 0x01f60300), put32(28, 0x00020100);
    at = (at + 2047) / 2048 * 2048;
    uint32_t msz = literal(model, w);
    put32(32, 7), put32(36, msz), put32(40, B), put32(44, 0);
    return at;                                      /* the file ends with the model's last byte */
}

/* RE2's part i is Dino Crisis's part from[i] */
static const int k_from[15] = {0, 8, 9, 10, 11, 12, 13, 14, 7, 1, 2, 3, 4, 5, 6};

static void made_up(void) {
    uint32_t size = build();
    PeImpModel m;
    char why[200] = "";
    int rc = dc1_core_parse(buf, size, &m, why, sizeof why);
    if (rc) printf("refused: %s\n", why);
    assert(rc == 0 && m.bones == 15 && m.parts == 15 && m.w == 256 && m.h == 256 && m.pal_rows == 2);
    /* the image: tile row 1, column 2 is tile 6 */
    assert(m.image[40 * 256 + 130] == 6 && m.image[0] == 0 && m.image[255 * 256 + 255] == 31 && m.pal[1][3] == 4);
    /* bones: RE2's order, turned: Dino Crisis (x, y, z) -> (z, y, -x) */
    static const int parent2[15] = {-1, 0, 1, 2, 3, 1, 5, 6, 0, 0, 9, 10, 0, 12, 13};
    for (int r = 0; r < 15; r++) {
        int k = k_from[r];
        assert(m.parent[r] == parent2[r]);
        assert(m.rel[r][0] == k_off[k][2] && m.rel[r][1] == k_off[k][1] && m.rel[r][2] == -k_off[k][0]);
    }
    assert(m.rel[9][2] == -100 && m.rel[2][2] == -60);   /* DC's +x side is RE2's z- side: arm 9, leg 2 */
    /* each bone's own triangle, turned: vertex (v*10+1, k, v+5) -> (v+5, k, -(v*10+1)) */
    const PeImpPart *head = &m.part[8];
    assert(head->tris == 1 && head->tri[0].v[1][0] == 6 && head->tri[0].v[1][1] == 7 && head->tri[0].v[1][2] == -11);
    assert(head->tri[0].n[0][0] == 7 && head->tri[0].n[0][2] == -4096 && head->tri[0].clut == 0x7db0);
    assert(head->tri[0].uv[0][0] == 1 && head->tri[0].uv[1][1] == 4 && head->tri[0].uv[2][0] == 5);
    /* the waist's quad has three thigh corners: it goes to the thigh (RE2 2), in its frame; its
     * waist corner is moved by the thigh's offset (60, 150, 0) -> (0, 150, -60) */
    const PeImpPart *thigh = &m.part[2], *waist = &m.part[1];
    assert(thigh->quads == 1 && waist->quads == 0 && waist->tris == 1);
    assert(thigh->quad[0].v[3][0] == 5 && thigh->quad[0].v[3][1] == 8 - 150 && thigh->quad[0].v[3][2] == -1 + 60);
    assert(thigh->quad[0].v[0][1] == 9 && thigh->quad[0].uv[3][0] == 7);
    /* the right hand (RE2 11) is the left hand (RE2 14) mirrored: corners 0 2 1, z negated */
    const PeImpPart *rh = &m.part[11], *lh = &m.part[14];
    assert(rh->tris == 1 && lh->tris == 1);
    for (int j = 0; j < 3; j++) {
        static const int o[3] = {0, 2, 1};
        assert(rh->tri[0].v[j][0] == lh->tri[0].v[o[j]][0] && rh->tri[0].v[j][2] == -lh->tri[0].v[o[j]][2]);
        assert(rh->tri[0].uv[j][0] == lh->tri[0].uv[o[j]][0]);
    }
    pe_imp_model_free(&m);
    for (uint32_t cut = 0; cut < size; cut += 61) {
        uint8_t *copy = malloc(cut ? cut : 1);
        memcpy(copy, buf, cut);
        PeImpModel t;
        assert(dc1_core_parse(copy, cut, &t, why, sizeof why) != 0);
        free(copy);
    }
    printf("made-up CORE archive: tiles, bones turned into RE's frame, stitched quad, mirrored hand\n");
}

static void disc(const char *cue) {
    char why[300] = "";
    PeImpDisc *d = pe_imp_disc_open(cue, why, sizeof why);
    assert(d);
    static const struct { const char *file; int faces; } want[] = {
        {"CORE00.DAT", 468}, {"CORE01.DAT", 471}, {"CORE02.DAT", 468}, {"CORE03.DAT", 465}};
    int found = 0;
    for (int i = 0; i < pe_imp_disc_files(d); i++) {
        const char *name = pe_imp_disc_name(d, i), *s = strrchr(name, '/');
        if (!s || strncmp(s + 1, "CORE0", 5)) continue;
        uint8_t *data;
        uint32_t size;
        assert(!pe_imp_disc_read(d, i, &data, &size, why, sizeof why));
        PeImpModel m;
        int rc = dc1_core_parse(data, size, &m, why, sizeof why);
        free(data);
        if (rc) printf("  %s refused: %s\n", name, why);
        assert(rc == 0);
        int faces = 0;
        for (uint32_t p = 0; p < m.parts; p++) faces += (int)(m.part[p].tris + m.part[p].quads);
        for (unsigned k = 0; k < sizeof want / sizeof want[0]; k++)
            if (!strcmp(s + 1, want[k].file)) { assert(faces == want[k].faces); found++; }
        printf("  %s: %d faces, root at %d\n", s + 1, faces, (int)m.pos[0][1]);
        pe_imp_model_free(&m);
    }
    pe_imp_disc_close(d);
    assert(found == 4);
}

int main(void) {
    test_lz();
    made_up();
    const char *cue = getenv("PE_TEST_DC1_DISC");
    if (cue && *cue) disc(cue);
    else printf("PE_TEST_DC1_DISC not set: disc part skipped\n");
    printf("dc1 reader passed\n");
    return 0;
}
