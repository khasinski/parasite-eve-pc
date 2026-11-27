/* pe_src_re1's reader (mods/pe_src_re1/pe_src_re1_emd.c).
 *   made-up   a small RE1 person: bones and parts come out in RE2's order with their offsets,
 *             faces as stored, the extra 16th object kept last; truncated copies refused
 *   disc      with PE_TEST_RE1_DISC (the Director's Cut Dual Shock .cue): CHAR10-13 and the 20 NPC
 *             and outfit files (EM1020-102E, EM1030-1035) read; CHAR10-13's faces
 *             as counted from the files (689, 685, 681, 697); Chris's head and waist where RE2
 *             keeps them */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pe_imp.h"
#include "pe_src_re1.h"

static uint8_t buf[16384];
static uint32_t at;
static void u8(unsigned v) { buf[at++] = (uint8_t)v; }
static void u16(unsigned v) { u8(v & 255), u8(v >> 8 & 255); }
static void u32(uint32_t v) { u16(v & 0xffff), u16(v >> 16); }
static void s16(int v) { u16((unsigned)(uint16_t)(int16_t)v); }

/* RE1's tree: chest root, head, waist with the legs, arms */
static const int k_parent[15] = {-1, 0, 0, 2, 3, 4, 2, 6, 7, 0, 9, 10, 0, 12, 13};

static uint32_t build(void) {
    memset(buf, 0, sizeof buf);
    at = 0;
    /* EMR at 0 */
    u16(100), u16(0), u16(15), u16(0);
    for (int i = 0; i < 15; i++) s16(i * 10 + 1), s16(i * 10 + 2), s16(i * 10 + 3);
    while (at < 100) u8(0);
    uint32_t list = 60;
    for (int i = 0; i < 15; i++) {
        int nc = 0;
        for (int k = 0; k < 15; k++) nc += k_parent[k] == i;
        u16((unsigned)nc), u16(list);
        list += (uint32_t)nc;
    }
    for (int i = 0; i < 15; i++) for (int k = 0; k < 15; k++) if (k_parent[k] == i) u8((unsigned)k);
    while (at & 3) u8(0);
    uint32_t edd = at;
    u32(0);
    /* TMD: 16 objects, object i one triangle whose first vertex x is i */
    uint32_t tmd = at;
    u32(0x41), u32(0), u32(16);
    uint32_t table = at, body = 28 * 16;
    at += body;
    for (uint32_t i = 0; i < 16; i++) {
        uint32_t vo = at - (tmd + 12);
        for (int v = 0; v < 3; v++) s16((int)i + 100 * v), s16(v), s16(-v), s16(0);
        uint32_t no = at - (tmd + 12);
        for (int v = 0; v < 2; v++) s16(4096), s16(v), s16((int)i), s16(0);
        uint32_t fo = at - (tmd + 12);
        u32(0x34000609), u8(1), u8(2), u16(0x7840), u8(3), u8(4), u16(0x81), u8(5), u8(6), u16(0);
        u16(0), u16(0), u16(1), u16(1), u16(1), u16(2);
        uint32_t keep = at;
        at = table + 28 * i;
        u32(vo), u32(3), u32(no), u32(2), u32(fo), u32(1), u32(0);
        at = keep;
    }
    /* TIM: 8-bit, one palette, 8 x 2 */
    uint32_t tim = at;
    u32(0x10), u32(9), u32(12 + 512), u16(0), u16(480), u16(256), u16(1);
    for (int c = 0; c < 256; c++) u16((unsigned)c);
    u32(12 + 16), u16(0), u16(0), u16(4), u16(2);
    for (int k = 0; k < 16; k++) u8((unsigned)k);
    u32(0), u32(0), u32(edd), u32(tmd), u32(tim);
    return at;
}

static void made_up(void) {
    uint32_t size = build();
    PeImpModel m;
    char why[200] = "";
    int rc = re1_emd_parse(buf, size, &m, why, sizeof why);
    if (rc) printf("refused: %s\n", why);
    assert(rc == 0 && m.bones == 15 && m.parts == 16);
    /* RE2's order: 1 waist (RE1 2), 2-4 a leg (RE1 6-8), 8 head (RE1 1), 9-11 an arm (RE1 12-14) */
    static const int from[15] = {0, 2, 6, 7, 8, 3, 4, 5, 1, 12, 13, 14, 9, 10, 11};
    static const int parent2[15] = {-1, 0, 1, 2, 3, 1, 5, 6, 0, 0, 9, 10, 0, 12, 13};
    for (int r = 0; r < 15; r++) {
        assert(m.parent[r] == parent2[r]);
        assert(m.rel[r][0] == from[r] * 10 + 1 && m.rel[r][2] == from[r] * 10 + 3);
        assert(m.part[r].tris == 1 && m.part[r].tri[0].v[0][0] == from[r]);
    }
    assert(m.pos[4][1] == (0 + 2 + 6 + 7 + 8) * 10 + 5 * 2);
    const PeImpFace *f = &m.part[15].tri[0];
    assert(f->v[0][0] == 15 && f->v[2][0] == 215 && f->v[1][2] == -1 && f->n[2][1] == 1 && f->n[0][2] == 15);
    assert(f->clut == 0x7840 && f->page == 0x81 && f->uv[0][0] == 1 && f->uv[2][1] == 6);
    assert(m.pal_rows == 1 && m.w == 8 && m.h == 2 && m.image[15] == 15);
    pe_imp_model_free(&m);
    for (uint32_t cut = 0; cut < size; cut++) {
        uint8_t *copy = malloc(cut ? cut : 1);
        memcpy(copy, buf, cut);
        PeImpModel t;
        assert(re1_emd_parse(copy, cut, &t, why, sizeof why) != 0);
        free(copy);
    }
    printf("made-up RE1 person: RE2's order, %u bytes\n", size);
}

static void disc(const char *cue) {
    char why[300] = "";
    PeImpDisc *d = pe_imp_disc_open(cue, why, sizeof why);
    assert(d);
    static const struct { const char *file; int faces; } want[] = {
        {"CHAR10.EMD", 689}, {"CHAR11.EMD", 685}, {"CHAR12.EMD", 681}, {"CHAR13.EMD", 697}};
    int found = 0, people = 0;
    for (int i = 0; i < pe_imp_disc_files(d); i++) {
        const char *name = pe_imp_disc_name(d, i), *s = strrchr(name, '/');
        int npc = s && (!strncmp(s + 1, "EM102", 5) || !strncmp(s + 1, "EM103", 5));
        if (!s || (!strstr(name, "/ENEMY/CHAR1") && !npc)) continue;
        uint8_t *data;
        uint32_t size;
        assert(!pe_imp_disc_read(d, i, &data, &size, why, sizeof why));
        PeImpModel m;
        int rc = re1_emd_parse(data, size, &m, why, sizeof why);
        free(data);
        if (rc) printf("  %s refused: %s\n", name, why);
        assert(rc == 0);
        int faces = 0;
        for (uint32_t p = 0; p < m.parts; p++) faces += (int)m.part[p].tris;
        for (unsigned k = 0; k < sizeof want / sizeof want[0]; k++)
            if (!strcmp(s + 1, want[k].file)) { assert(faces == want[k].faces); found++; }
        if (npc) people++;
        if (!strcmp(s + 1, "CHAR10.EMD")) {
            /* Chris: head (RE1 1) at RE2's 8 with 155 faces, waist (RE1 2) at RE2's 1 */
            assert(m.rel[8][0] == -98 && m.rel[8][1] == -704 && m.part[8].tris == 155);
            assert(m.rel[1][0] == -29 && m.rel[1][1] == -19 && m.part[1].tris == 52 && m.rel[0][1] == -1807);
        }
        printf("  %s: %d faces, %u parts\n", s + 1, faces, m.parts);
        pe_imp_model_free(&m);
    }
    pe_imp_disc_close(d);
    printf("  %d NPC and outfit files read as people\n", people);
    assert(found == 4 && people == 20);
}

int main(void) {
    made_up();
    const char *cue = getenv("PE_TEST_RE1_DISC");
    if (cue && *cue) disc(cue);
    else printf("PE_TEST_RE1_DISC not set: disc part skipped\n");
    printf("re1 reader passed\n");
    return 0;
}
