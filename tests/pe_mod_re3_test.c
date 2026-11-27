/* pe_src_re3's reader (mods/pe_src_re3/pe_src_re3_pld.c).
 *   made-up   a small RE3 person: a skinned arm whose faces split between shoulder and elbow
 *             (a seam vertex no record lists follows its nearest neighbour), a bone-local hand,
 *             placeholders dropped only where skinning replaces them, RE2's bone order out;
 *             truncated copies refused, never read past
 *   disc      with PE_TEST_RE3_DISC (the RE3 .cue): every CD_DATA/PLD/PLxx.PLD read; 14 people,
 *             the empty PL0D and the prop PL0F passed over; Jill and Carlos's face counts as the
 *             Python prototype found them (573, 548) */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pe_imp.h"
#include "pe_src_re3.h"

static uint8_t buf[8192];
static uint32_t at;
static void u8(unsigned v) { buf[at++] = (uint8_t)v; }
static void u16(unsigned v) { u8(v & 255), u8(v >> 8 & 255); }
static void u32(uint32_t v) { u16(v & 0xffff), u16(v >> 16); }
static void s16(int v) { u16((unsigned)(uint16_t)(int16_t)v); }
static void put16(uint32_t where, unsigned v) { buf[where] = (uint8_t)v, buf[where + 1] = (uint8_t)(v >> 8); }
static void put32(uint32_t where, uint32_t v) { put16(where, v & 0xffff), put16(where + 2, v >> 16); }

/* RE3's tree and offsets: chest root, head, arms, waist, legs */
static const int k_parent[15] = {-1, 0, 0, 2, 3, 0, 5, 6, 0, 8, 9, 10, 8, 12, 13};
static const int k_rel[15][3] = {{0, -1839, 0}, {-23, -667, 0}, {-43, -611, -305}, {-2, 484, -71}, {13, 424, -59},
                                 {-43, -611, 305}, {-2, 484, 71}, {13, 424, 59}, {0, 0, 0}, {-27, 213, -149},
                                 {-24, 652, -10}, {-13, 872, -3}, {-27, 213, 149}, {-24, 652, 10}, {-13, 872, 3}};
/* the arm (part 2) in the root's frame: shoulder 0,1, elbow 2, seam 3 (unlisted, nearest is 2) */
static const int k_arm[4][3] = {{-43, -611, -300}, {-43, -500, -300}, {-45, -127, -376}, {-45, -117, -376}};

static uint32_t build(void) {
    memset(buf, 0, sizeof buf);
    at = 0;
    u32(0), u32(5);
    uint32_t edd = at;
    u32(0);
    /* EMR */
    uint32_t emr = at;
    u16(100), u16(0), u16(15), u16(76);
    for (int i = 0; i < 15; i++) for (int c = 0; c < 3; c++) s16(k_rel[i][c]);
    while (at < emr + 100) u8(0);
    uint32_t list = 60;
    for (int i = 0; i < 15; i++) {
        int nc = 0;
        for (int k = 0; k < 15; k++) nc += k_parent[k] == i;
        u16((unsigned)nc), u16(list);
        list += (uint32_t)nc;
    }
    for (int i = 0; i < 15; i++) for (int k = 0; k < 15; k++) if (k_parent[k] == i) u8((unsigned)k);
    while (at & 3) u8(0);
    /* MD2: part 0 a bone-local quad, part 2 the skinned arm, part 4 a bone-local hand quad, the
     * rest 3-vertex placeholders; objects then vertices, normals, faces */
    uint32_t md2 = at;
    u32(0), u32(15);
    uint32_t table = at;
    at += 24 * 15;
    for (int i = 0; i < 15; i++) {
        int vc = (i == 0 || i == 2 || i == 4) ? 4 : 3;
        uint32_t vo = at - (md2 + 8);
        for (int v = 0; v < vc; v++)
            for (int c = 0; c < 4; c++) s16(c == 3 ? 0 : i == 2 ? k_arm[v][c] : (v + 1) * (c + 1) + 10 * i);
        uint32_t no = at - (md2 + 8);
        for (int v = 0; v < vc; v++) s16(4096), s16(v), s16(i), s16(0);
        uint32_t to = at - (md2 + 8), tc = 0, qc = 0;
        if (i == 2 || vc == 3) {                        /* tri: arm 1 2 3, placeholder 0 1 2 */
            unsigned a = i == 2 ? 1 : 0;
            u8(1), u8(2), u16(0x7840), u8(3), u8(4), u8(0x81), u8(a), u8(5), u8(6), u8(a + 1), u8(a + 2);
            tc = 1;
        }
        uint32_t qo = at - (md2 + 8);
        if (vc == 4) {
            u8(7), u8(8), u16(0x7800), u8(9), u8(10), u8(0x80), u8(0), u8(11), u8(12), u8(0), u8(1), u8(13), u8(14), u8(2), u8(3);
            qc = 1;
        }
        uint32_t t = table + 24 * (uint32_t)i;
        put32(t, vo), put32(t + 4, no), put32(t + 8, (uint32_t)vc), put32(t + 12, to), put32(t + 16, qo);
        put16(t + 20, tc), put16(t + 22, qc);
    }
    put32(md2, at - md2);
    /* skin: one group, the arm. Bone 2 moves 0 and 1, bone 3 moves 2; vertex 3 is a seam */
    uint32_t skin = at;
    u16(0x2527), u16(0), u16(16), u16(1);
    u16(8), u16(0), u16(0), u16(0);                    /* records 8 bytes past this entry */
    u8(2), u8(1), u8(0), u8(1), u8(1), u8(3), u8(1), u8(2), u8(2), u8(2), u8(0xff);
    while (at & 3) u8(0);
    /* TIM: 8-bit, two palettes, 16 x 2 */
    uint32_t tim = at;
    u32(0x10), u32(9), u32(12 + 2 * 256 * 2), u16(0), u16(480), u16(256), u16(2);
    for (int r = 0; r < 2; r++) for (int c = 0; c < 256; c++) u16((unsigned)(c + r));
    u32(12 + 32), u16(0), u16(0), u16(8), u16(2);
    for (int k = 0; k < 32; k++) u8((unsigned)k);
    uint32_t dir = at;
    u32(edd), u32(emr), u32(md2), u32(skin), u32(tim);
    put32(0, dir);
    return at;
}

static int re2_of(int re3) { static const int map[15] = {0, 8, 9, 10, 11, 12, 13, 14, 1, 2, 3, 4, 5, 6, 7}; return map[re3]; }

static void made_up(void) {
    uint32_t size = build();
    PeImpModel m;
    char why[200] = "";
    int rc = re3_pld_parse(buf, size, &m, why, sizeof why);
    if (rc) printf("refused: %s\n", why);
    assert(rc == 0 && m.bones == 15 && m.parts == 15);
    /* RE2's order: waist is bone 1 under the chest, the head 8, the first arm 9-11 */
    assert(m.parent[0] == -1 && m.parent[1] == 0 && m.parent[2] == 1 && m.parent[8] == 0 && m.parent[9] == 0 && m.parent[10] == 9);
    assert(m.rel[9][0] == -43 && m.rel[9][1] == -611 && m.rel[9][2] == -305 && m.pos[10][1] == -1839 - 611 + 484);
    /* the arm's quad (0 1 2 3: bones 2 2 3 3, a tie) stays on the shoulder, in its frame */
    const PeImpPart *up = &m.part[re2_of(2)], *fore = &m.part[re2_of(3)];
    assert(up->quads == 1 && up->tris == 0);
    assert(up->quad[0].v[0][0] == 0 && up->quad[0].v[0][1] == 0 && up->quad[0].v[0][2] == 5);       /* (-43,-611,-300) - (-43,-611,-305) */
    assert(up->quad[0].v[2][1] == -127 + 611 && up->quad[0].clut == 0x7800 && up->quad[0].page == 0x80);
    assert(up->quad[0].uv[3][0] == 13 && up->quad[0].uv[3][1] == 14 && up->quad[0].n[1][1] == 1 && up->quad[0].n[1][2] == 2);
    /* the triangle 1 2 3 (bones 2 3 3, the seam joining the elbow) goes to the elbow */
    assert(fore->tris == 1 && fore->quads == 0);
    assert(fore->tri[0].v[0][1] == -500 - (-611 + 484) && fore->tri[0].v[1][0] == 0 && fore->tri[0].v[2][1] == 10);
    assert(fore->tri[0].clut == 0x7840 && fore->tri[0].page == 0x81 && fore->tri[0].uv[2][0] == 5);
    /* the hand and the chest stay as they are; untouched placeholders stay; bone 3's goes */
    assert(m.part[re2_of(4)].quads == 1 && m.part[re2_of(4)].quad[0].v[0][0] == 1 + 40);
    assert(m.part[0].quads == 1 && m.part[0].quad[0].v[3][2] == 12);
    assert(m.part[re2_of(1)].tris == 1 && m.part[re2_of(14)].tris == 1);
    assert(m.pal_rows == 2 && m.w == 16 && m.h == 2 && m.image[31] == 31 && m.pal[1][255] == 256);
    pe_imp_model_free(&m);

    /* every truncation refused or passed over, never read past */
    for (uint32_t cut = 0; cut < size; cut++) {
        uint8_t *copy = malloc(cut ? cut : 1);
        memcpy(copy, buf, cut);
        PeImpModel t;
        assert(re3_pld_parse(copy, cut, &t, why, sizeof why) != 0);
        free(copy);
    }
    printf("made-up RE3 person: arm split shoulder / elbow, seam to its neighbour, %u bytes\n", size);
}

static void disc(const char *cue) {
    char why[300] = "";
    PeImpDisc *d = pe_imp_disc_open(cue, why, sizeof why);
    assert(d);
    int people = 0, passed = 0, jill = -1, carlos = -1;
    for (int i = 0; i < pe_imp_disc_files(d); i++) {
        const char *name = pe_imp_disc_name(d, i);
        if (strncmp(name, "CD_DATA/PLD/PL", 14) || strlen(name) != 20 || strcmp(name + 16, ".PLD")) continue;
        uint8_t *data;
        uint32_t size;
        assert(!pe_imp_disc_read(d, i, &data, &size, why, sizeof why));
        PeImpModel m;
        int rc = re3_pld_parse(data, size, &m, why, sizeof why);
        free(data);
        if (rc == -2) { printf("  %s passed over: %s\n", name + 12, why); passed++; continue; }
        if (rc) printf("  %s refused: %s\n", name + 12, why);
        assert(rc == 0);
        int faces = 0;
        for (uint32_t p = 0; p < m.parts; p++) faces += (int)(m.part[p].tris + m.part[p].quads);
        if (!strcmp(name + 12, "PL00.PLD")) jill = faces;
        if (!strcmp(name + 12, "PL08.PLD")) carlos = faces;
        people++;
        pe_imp_model_free(&m);
    }
    pe_imp_disc_close(d);
    printf("RE3 disc: %d people (Jill %d faces, Carlos %d), %d passed over\n", people, jill, carlos, passed);
    assert(people == 14 && passed == 2 && jill == 573 && carlos == 548);
}

int main(void) {
    made_up();
    const char *cue = getenv("PE_TEST_RE3_DISC");
    if (cue && *cue) disc(cue);
    else printf("PE_TEST_RE3_DISC not set: disc part skipped\n");
    printf("re3 reader passed\n");
    return 0;
}
