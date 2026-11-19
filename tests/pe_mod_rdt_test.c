/* RE2/RE3 room logic (mods/host/import/pe_imp_rdt.c).
 *   made-up   a room with one script holding a door, a four-point door, an item, a trigger zone and
 *             an enemy, plus two camera switch zones, reads back field for field; a cut-short or
 *             scrambled room is read without leaving its bounds
 *   disc      PE_TEST_RE2_DISC / PE_TEST_RE3_DISC: every room's scripts walked; prints procedures,
 *             doors, zones, items, camera zones; RE2 must read with no unreadable spots */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pe_imp.h"
#include "pe_imp_rdt.h"

static void put16(uint8_t *p, int v) { p[0] = (uint8_t)v, p[1] = (uint8_t)((unsigned)v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, (int)(v & 0xffff)), put16(p + 2, (int)(v >> 16)); }

static uint32_t made_up_room(uint8_t *b, uint32_t cap) {
    memset(b, 0, cap);
    uint32_t rvd = 0x100, scd = 0x200, end = 0x300;
    put32(b + 8 + 8 * 4, rvd);
    put32(b + 8 + 16 * 4, scd);
    put32(b + 8 + 18 * 4, end);                       /* a later section bounds the script */
    /* camera zones: 0 -> 1, 1 -> 0, terminator */
    uint8_t *z = b + rvd;
    put16(z, 0xff01), z[2] = 0, z[3] = 1;
    for (int i = 0; i < 4; i++) put16(z + 4 + 4 * i, 100 * i), put16(z + 6 + 4 * i, -100 * i);
    z += 20;
    put16(z, 0xff01), z[2] = 1, z[3] = 0;
    put16(z + 20, 0xffff);
    /* script: one procedure */
    put16(b + scd, 2);
    uint8_t *p = b + scd + 2;
    p[0] = 0x3B, p[1] = 7, p[2] = 1, p[3] = 0x1, p[4] = 0, p[5] = 0;            /* door_aot_se */
    put16(p + 6, -1000), put16(p + 8, 2000), put16(p + 10, 1500), put16(p + 12, 1800);
    put16(p + 14, 300), put16(p + 16, 0), put16(p + 18, -400), put16(p + 20, 1024);
    p[22] = 0, p[23] = 6, p[24] = 3, p[25] = 0, p[26] = 2, p[27] = 5, p[28] = 9, p[29] = 138, p[30] = 1, p[31] = 0;
    p += 32;
    p[0] = 0x68, p[1] = 8;                                                     /* door_aot_set_4p */
    for (int i = 0; i < 4; i++) put16(p + 6 + 4 * i, 10 * i), put16(p + 8 + 4 * i, 20 * i);
    p[30] = 1, p[31] = 2;                                                       /* stage 1 room 2 */
    p += 40;
    p[0] = 0x4E, p[1] = 3;                                                     /* item_aot_set */
    put16(p + 6, 50), put16(p + 8, 60), put16(p + 10, 70), put16(p + 12, 80);
    put16(p + 14, 0x2D), put16(p + 16, 1), put16(p + 18, 12), p[20] = 4, p[21] = 1;
    p += 22;
    p[0] = 0x2C, p[1] = 4, p[2] = 5;                                           /* aot_set (typewriter kind) */
    p[14] = 0xAA;
    p += 20;
    p[0] = 0x44;                                                               /* sce_em_set */
    p += 22;
    p[0] = 1;                                                                  /* evt_end */
    return cap;
}

static void made_up(void) {
    static uint8_t b[0x400];
    uint32_t size = made_up_room(b, sizeof b);
    static PeImpRdt r;
    char why[100];
    assert(!pe_imp_rdt_read(PE_IMP_RE2, b, size, &r, why, sizeof why));
    assert(r.procedures == 1 && r.unreadable == 0 && r.instructions == 6);
    assert(r.doors == 2 && r.items == 1 && r.aots == 1 && r.enemies == 1 && r.cuts == 2);
    const PeImpRdtDoor *d = &r.door[0];
    assert(d->zone.id == 7 && d->zone.corners == 0 && d->zone.x[0] == -1000 && d->zone.x[1] == 500 && d->zone.z[2] == 3800);
    assert(d->next_x == 300 && d->next_z == -400 && d->next_dir == 1024 && d->stage == 0 && d->room == 6);
    assert(d->camera == 3 && d->animation == 5 && d->sound == 9 && d->lock_id == 138 && d->lock_type == 1);
    assert(r.door[1].zone.corners == 4 && r.door[1].zone.z[3] == 60 && r.door[1].stage == 1 && r.door[1].room == 2);
    assert(r.item[0].item == 0x2D && r.item[0].count == 1 && r.item[0].flag == 12 && r.item[0].zone.x[0] == 50);
    assert(r.aot[0].zone.sce == 5 && r.aot[0].data[0] == 0xAA);
    assert(r.cut[0].from == 0 && r.cut[0].to == 1 && r.cut[0].x[3] == 300 && r.cut[1].from == 1);

    /* not a room */
    assert(pe_imp_rdt_read(PE_IMP_RE2, b, 40, &r, why, sizeof why) == -1);
    assert(pe_imp_rdt_read(1, b, size, &r, why, sizeof why) == -1);
    /* cut short anywhere, and scrambled: read within bounds (fails or returns), never past them */
    for (uint32_t cut = 0; cut < size; cut += 7) {
        uint8_t *c = malloc(cut ? cut : 1);
        memcpy(c, b, cut);
        pe_imp_rdt_read(PE_IMP_RE2, c, cut, &r, why, sizeof why);
        free(c);
    }
    srand(7);
    for (int round = 0; round < 2000; round++) {
        static uint8_t c[0x400];
        memcpy(c, b, sizeof c);
        for (int k = 0; k < 8; k++) c[rand() % (int)sizeof c] = (uint8_t)rand();
        pe_imp_rdt_read(round & 1 ? PE_IMP_RE3 : PE_IMP_RE2, c, sizeof c, &r, why, sizeof why);
    }
    printf("made-up room: doors, item, zone, enemy and camera zones read; damaged rooms stay in bounds\n");
}

static void disc(const char *label, const char *cue, unsigned game) {
    char why[200];
    PeImpDisc *d = pe_imp_disc_open(cue, why, sizeof why);
    assert(d);
    static PeImpRdt r;
    uint32_t rooms = 0, procs = 0, bad = 0, doors = 0, aots = 0, items = 0, cuts = 0, enemies = 0, over = 0;
    for (int i = 0; i < pe_imp_disc_files(d); i++) {
        const char *name = pe_imp_disc_name(d, i);
        int is_room = game == PE_IMP_RE2 ? pe_imp_match("PL?/RDT/ROOM*.RDT", name) : pe_imp_match("CD_DATA/STAGE?/R*.ARD", name);
        if (!is_room) continue;
        uint8_t *data;
        uint32_t size;
        if (pe_imp_disc_read(d, i, &data, &size, why, sizeof why) || !size) continue;
        const uint8_t *rdt = data;
        uint32_t rdt_size = size;
        if (game == PE_IMP_RE3 && pe_imp_re3_ard_rdt(data, size, &rdt, &rdt_size)) { free(data); continue; }
        if (!pe_imp_rdt_read(game, rdt, rdt_size, &r, why, sizeof why)) {
            rooms++, procs += r.procedures, bad += r.unreadable, doors += r.doors, aots += r.aots;
            items += r.items, cuts += r.cuts, enemies += r.enemies;
            if (r.doors > PE_IMP_RDT_DOORS || r.aots > PE_IMP_RDT_AOTS || r.items > PE_IMP_RDT_ITEMS || r.cuts > PE_IMP_RDT_CUTS) over++;
            for (uint32_t k = 0; k < r.doors && k < PE_IMP_RDT_DOORS; k++)
                assert(r.door[k].stage < 8);                              /* a stage, not garbage */
        }
        free(data);
    }
    pe_imp_disc_close(d);
    printf("%s: %u rooms, %u procedures, %u unreadable spots, %u doors, %u zones, %u items, %u enemy placements, %u camera zones, %u rooms over the caps\n",
           label, rooms, procs, bad, doors, aots, items, enemies, cuts, over);
    assert(rooms > 100 && doors > 200 && cuts > 500 && !over);
    if (game == PE_IMP_RE2) assert(!bad);
    else assert(bad * 100 < procs);
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    made_up();
    const char *re2 = getenv("PE_TEST_RE2_DISC"), *re3 = getenv("PE_TEST_RE3_DISC");
    if (re2 && *re2) disc("RE2", re2, PE_IMP_RE2);
    else printf("PE_TEST_RE2_DISC not set: RE2 rooms skipped\n");
    if (re3 && *re3) disc("RE3", re3, PE_IMP_RE3);
    else printf("PE_TEST_RE3_DISC not set: RE3 rooms skipped\n");
    printf("RDT passed\n");
    return 0;
}
