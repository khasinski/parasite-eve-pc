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
#include "pe_imp_internal.h"
#include "pe_imp_logic.h"
#include "pe_imp_rdt.h"
#include "pe_imp_mdec.h"
#include "pe_imp_pictures.h"

extern const PeImpReader g_re2_logic_ps1, g_re1_door_ps1, g_re2_door_ps1, g_re3_door_ps1;
extern const PeImpReader g_re1_items_ps1, g_re2_items_ps1, g_re3_items_ps1, g_re3_logic_ps1;
void MdecDiag_ExpandDecoderTable(void *buffer);   /* the game's, linked in */
extern const PeImpReader g_re1_files_ps1, g_re2_files_ps1, g_re3_files_ps1, g_re2_maps_ps1, g_re3_maps_ps1;

/* a disc's file pictures: every TIM kept decodes; `want` pictures at least
 * (PE_TEST_MOVIE_PPM=<folder> also writes the first 8-bit one as <label>_file.ppm) */
static void scan_files(const char *label, const char *cue, const char *game, const PeImpReader *reader,
                       unsigned want) {
    PeImpScan sc;
    pe_imp_registry_reset();
    assert(!pe_imp_add_reader(reader));
    assert(!pe_imp_scan("pe-rdt-test/files", cue, game, &sc));
    PeImpAsset *a;
    int n = pe_imp_catalog_assets("pe-rdt-test/files", game, &a);
    unsigned records = 0, pictures = 0, decoded = 0, written = 0;
    for (int i = 0; i < n; i++) {
        if (strcmp(a[i].type, "Pictures")) continue;
        char why[200];
        PeImpRoomLogic *l;
        assert(!pe_imp_catalog_pictures("pe-rdt-test/files", a[i].id, &l, why, sizeof why));
        records++;
        uint32_t count = 0;
        pe_imp_logic_section(l, PE_IMP_PICTURE_TIMS, &count, NULL);
        for (uint32_t k = 0; k < count; k++) {
            uint32_t size;
            const uint8_t *tim = pe_imp_picture_tim(l, k, &size);
            unsigned w, h;
            uint8_t *rgba = tim ? pe_imp_tim_decode(tim, size, &w, &h) : NULL;
            pictures++, decoded += rgba != NULL;
            const char *dir = getenv("PE_TEST_MOVIE_PPM");
            if (rgba && !written && dir && *dir && ((tim[4] & 3) == 1 || strstr(label, "map"))) {
                char out[400];
                snprintf(out, sizeof out, "%s/%s_file.ppm", dir, label);
                FILE *o = fopen(out, "wb");
                if (o) {
                    fprintf(o, "P6\n%u %u\n255\n", w, h);
                    for (unsigned p = 0; p < w * h; p++) fwrite(rgba + p * 4, 1, 3, o);
                    fclose(o);
                }
                written = 1;
            }
            free(rgba);
        }
        free(l);
    }
    free(a);
    printf("%s files: %u records, %u pictures, %u decoded (scan: %d files read, %d items, %d refused %s)\n", label,
           records, pictures, decoded, sc.files_read, sc.items, sc.refused, sc.last_error);
    assert(!sc.refused && pictures >= want && decoded == pictures);
}

/* one movie: read from the disc, saved and loaded, a frame from its middle decoded
 * (PE_TEST_MOVIE_PPM=<folder> writes that frame as <name>.ppm) */
static void movie(const char *label, const char *cue, const char *path) {
    char why[200];
    PeImpDisc *d = pe_imp_disc_open(cue, why, sizeof why);
    assert(d);
    int i = pe_imp_disc_find(d, path);
    assert(i >= 0);
    uint8_t *str, *xa = NULL;
    uint32_t size, xa_size = 0;
    assert(!pe_imp_disc_read(d, i, &str, &size, why, sizeof why));
    pe_imp_disc_xa(d, i, &xa, &xa_size, why, sizeof why);
    pe_imp_disc_close(d);
    PeImpMovie *m = pe_imp_movie_from_str(str, size, xa, xa_size);
    free(str), free(xa);
    assert(m);
    pe_imp_mkdirs("pe-rdt-test/movie");
    assert(!pe_imp_movie_save(m, "pe-rdt-test/movie", why, sizeof why));
    PeImpMovie *back;
    assert(!pe_imp_movie_load("pe-rdt-test/movie", &back, why, sizeof why));
    assert(back->frames == m->frames && back->frame_bytes == m->frame_bytes && back->audio_records == m->audio_records);
    assert(!memcmp(back->frame_data, m->frame_data, m->frame_bytes));
    uint32_t f = back->frames / 2;
    uint8_t *rgb = malloc((size_t)back->width * back->height * 3);
    pe_imp_mdec_set_table(MdecDiag_ExpandDecoderTable);
    int rc = pe_imp_mdec_decode(back->frame_data + back->offset[f], back->size[f], back->width, back->height, rgb, why, sizeof why);
    if (rc) printf("  frame %u: %s\n", f, why);
    long sum = 0, sum2 = 0, px = (long)back->width * back->height * 3;
    for (long k = 0; k < px; k++) sum += rgb[k], sum2 += (long)rgb[k] * rgb[k];
    double mean = (double)sum / px, var = (double)sum2 / px - mean * mean;
    const char *dir = getenv("PE_TEST_MOVIE_PPM");
    if (dir && *dir) {
        char out[400];
        snprintf(out, sizeof out, "%s/%s.ppm", dir, label);
        FILE *o = fopen(out, "wb");
        if (o) { fprintf(o, "P6\n%u %u\n255\n", back->width, back->height); fwrite(rgb, 1, (size_t)px, o); fclose(o); }
    }
    printf("%s movie %s: %u x %u, %u frames (%u KB), %u sound sectors; frame %u variance %.0f\n", label, path, back->width,
           back->height, back->frames, back->frame_bytes / 1024, back->audio_records, f, var);
    assert(!rc && var > 50 && back->frames > 30 && back->audio_records > 10);
    free(rgb);
    pe_imp_movie_free(m), pe_imp_movie_free(back);
    /* damaged: a cut file is refused */
    FILE *c = fopen("pe-rdt-test/movie/movie.bin", "rb+");
    fseek(c, 16, SEEK_SET), fputc(0xff, c), fputc(0xff, c), fputc(0xff, c), fputc(0x0f, c);
    fclose(c);
    assert(pe_imp_movie_load("pe-rdt-test/movie", &back, why, sizeof why) == -1);
}

/* a disc's item table: names by item number, icons kept; `want` is one item number and its name */
static void scan_items(const char *label, const char *cue, const char *game, const PeImpReader *reader,
                       unsigned want, const char *want_name, unsigned want_icons) {
    PeImpScan sc;
    pe_imp_registry_reset();
    assert(!pe_imp_add_reader(reader));
    assert(!pe_imp_scan("pe-rdt-test/items", cue, game, &sc));
    char why[200], id[32];
    snprintf(id, sizeof id, "%s:items/all", game);
    PeImpRoomLogic *l;
    if (pe_imp_catalog_itemtable("pe-rdt-test/items", id, &l, why, sizeof why)) { printf("  %s\n", why); assert(0); }
    uint32_t names, icons;
    pe_imp_logic_section(l, PE_IMP_ITEM_NAME, &names, NULL);
    pe_imp_logic_section(l, PE_IMP_ITEM_ICN8, &icons, NULL);
    int found = 0, with_icon = 0;
    for (uint32_t i = 0; i < names; i++) {
        PeImpItemName it;
        assert(!pe_imp_item_name(l, i, &it) && it.name[0]);
        with_icon += it.icon != 0xFFFF;
        if (it.item == want) found = !strcmp(it.name, want_name);
    }
    printf("%s items: %u named, %u icons (%d matched to items); item %u is \"%s\"\n", label, names, icons, with_icon,
           want, found ? want_name : "?");
    assert(!sc.refused && found && icons == want_icons && names > 60);
    free(l);
}

/* a disc's door files scanned into a store: DoorScenes (and RE2's door sounds) */
static void scan_doors(const char *label, const char *cue, const char *game, const PeImpReader *reader,
                       int want_sound) {
    PeImpScan sc;
    pe_imp_registry_reset();
    assert(!pe_imp_add_reader(reader));
    assert(!pe_imp_scan("pe-rdt-test/doors", cue, game, &sc));
    if (sc.refused) printf("  refused: %s\n", sc.last_error);
    PeImpAsset *a;
    int n = pe_imp_catalog_assets("pe-rdt-test/doors", game, &a), doors = 0, sounds = 0;
    char first[PE_IMP_ID_MAX] = "";
    for (int i = 0; i < n; i++) {
        if (!strcmp(a[i].type, "DoorScene")) { if (!doors++) snprintf(first, sizeof first, "%s", a[i].id); }
        else if (!strcmp(a[i].type, "Sound") && strstr(a[i].id, "door")) sounds++;
    }
    free(a);
    char why[200];
    PeImpRoomLogic *l;
    assert(doors && !pe_imp_catalog_doorscene("pe-rdt-test/doors", first, &l, why, sizeof why));
    uint32_t scene = 0, vab = 0;
    assert(pe_imp_logic_section(l, PE_IMP_DOOR_SCN0, NULL, &scene) && scene > 1000);
    pe_imp_logic_section(l, PE_IMP_DOOR_VAB0, NULL, &vab);
    printf("%s doors: %d DoorScenes from %u files, %d door sounds; %s: scene %u bytes, sound bank %u bytes\n",
           label, doors, sc.files_read, sounds, first, scene, vab);
    assert(!sc.refused && doors > 10 && (!want_sound || (sounds > doors && vab > 1000)));
    free(l);
}

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

/* the made-up RE2 room as a stored RoomLogic: saved, loaded, every record read back; names parsed;
 * damaged stored files refused */
static void made_up_logic(const uint8_t *b, uint32_t size) {
    char why[200];
    PeImpRoomLogic *l = pe_imp_rdt_logic(PE_IMP_RE2, 1, 0, 5, b, size, why, sizeof why);
    assert(l && l->game == PE_IMP_RE2 && l->player == 1 && l->room == 5);
    pe_imp_mkdirs("pe-rdt-test/logic");
    assert(!pe_imp_logic_save(l, "pe-rdt-test/logic", why, sizeof why));
    PeImpRoomLogic *back;
    assert(!pe_imp_logic_load("pe-rdt-test/logic", &back, why, sizeof why));
    assert(back->size == l->size && !memcmp(back->payload, l->payload, l->size) && back->player == 1);
    uint32_t records, bytes;
    assert(pe_imp_logic_section(back, PE_IMP_LOGIC_DOOR, &records, &bytes) && records == 2 && bytes == 80);
    assert(pe_imp_logic_section(back, PE_IMP_LOGIC_ENEM, &records, NULL) && records == 1);
    assert(pe_imp_logic_section(back, PE_IMP_LOGIC_SCD0, NULL, &bytes) && bytes == 0x100);   /* the script, as is */
    assert(!pe_imp_logic_section(back, PE_IMP_LOGIC_TAG('N', 'O', 'N', 'E'), NULL, NULL));
    PeImpDoor d;
    assert(!pe_imp_logic_door(back, 0, &d) && d.room == 6 && d.lock_id == 138 && d.zone.x[1] == 500 && d.next_z == -400);
    assert(!pe_imp_logic_door(back, 1, &d) && d.zone.corners == 4 && d.zone.z[3] == 60);
    assert(pe_imp_logic_door(back, 2, &d) == -1);
    PeImpPickup k;
    PeImpTrigger t;
    PeImpCameraZone c;
    assert(!pe_imp_logic_pickup(back, 0, &k) && k.item == 0x2D && k.flag == 12);
    assert(!pe_imp_logic_trigger(back, 0, &t) && t.zone.sce == 5 && t.data[0] == 0xAA);
    assert(!pe_imp_logic_camera_zone(back, 1, &c) && c.from == 1 && c.to == 0);
    free(back);
    /* a damaged file: every cut and a scrambled section table are refused or read in bounds */
    FILE *f = fopen("pe-rdt-test/logic/roomlogic.bin", "rb+");
    assert(f);
    fseek(f, 28 + 8, SEEK_SET);
    fputc(0xff, f), fputc(0xff, f), fputc(0xff, f), fputc(0x7f, f);    /* first section's length: huge */
    fclose(f);
    assert(pe_imp_logic_load("pe-rdt-test/logic", &back, why, sizeof why) == -1);
    free(l);

    uint32_t st, rm, pl;
    assert(!pe_imp_re_room_name(PE_IMP_RE1, "PSXUSA/STAGE1/ROOM1061.RDT", &st, &rm, &pl) && st == 0 && rm == 6 && pl == 1);
    assert(!pe_imp_re_room_name(PE_IMP_RE2, "PL1/RDT/ROOM20A1.RDT", &st, &rm, &pl) && st == 1 && rm == 0x0A && pl == 1);
    assert(!pe_imp_re_room_name(PE_IMP_RE3, "CD_DATA/STAGE1/R10C.ARD", &st, &rm, &pl) && st == 0 && rm == 0x0C && pl == 0);
    assert(pe_imp_re_room_name(PE_IMP_RE2, "PL0/RDT/ROOMXYZ0.RDT", &st, &rm, &pl) == -1);
    assert(pe_imp_re_room_name(PE_IMP_RE3, "CD_DATA/STAGE1/R10.ARD", &st, &rm, &pl) == -1);
    printf("made-up RoomLogic: saved, loaded and read back; damaged file refused; room names parsed\n");
}

/* a real scan of RE2's room files into a store: the runtime's view, through the catalog */
static void scan_re2(const char *cue) {
    PeImpScan sc;
    pe_imp_registry_reset();
    assert(!pe_imp_add_reader(&g_re2_logic_ps1));
    assert(!pe_imp_scan("pe-rdt-test/store", cue, "re2", &sc));
    PeImpAsset *a;
    int n = pe_imp_catalog_assets("pe-rdt-test/store", "re2", &a), rooms = 0, sounds = 0;
    for (int i = 0; i < n; i++) {
        rooms += !strcmp(a[i].type, "RoomLogic");
        sounds += !strcmp(a[i].type, "Sound") && strstr(a[i].id, "room");
    }
    free(a);
    char why[200], id[32];
    PeImpRoomLogic *l = NULL;
    for (int p = 0; p < 2 && !l; p++) {                  /* disc 1 is Leon's (player 0), disc 2 Claire's */
        snprintf(id, sizeof id, "re2:logic/105-%d", p);
        pe_imp_catalog_roomlogic("pe-rdt-test/store", id, &l, why, sizeof why);
    }
    assert(l);
    uint32_t doors;
    pe_imp_logic_section(l, PE_IMP_LOGIC_DOOR, &doors, NULL);
    int to_106 = 0;
    for (uint32_t k = 0; k < doors; k++) {
        PeImpDoor d;
        assert(!pe_imp_logic_door(l, k, &d));
        to_106 |= d.stage == 0 && d.room == 6;
    }
    uint32_t whole = 0;
    assert(pe_imp_logic_section(l, PE_IMP_LOGIC_RDT0, NULL, &whole) && whole > 10000);
    printf("RE2 scan: %d RoomLogic entries and %d room sounds from %u files; %s has %u doors%s, whole room %u bytes\n",
           rooms, sounds, sc.files_read, id, doors, to_106 ? ", one to room 106" : "", whole);
    assert(rooms > 100 && !sc.refused && doors >= 2 && to_106 && sounds > rooms);
    free(l);
}

/* RE3: the room archive's sound entries are kept with the room */
static void scan_re3(const char *cue) {
    PeImpScan sc;
    pe_imp_registry_reset();
    assert(!pe_imp_add_reader(&g_re3_logic_ps1));
    assert(!pe_imp_scan("pe-rdt-test/store", cue, "re3", &sc));
    char why[200];
    PeImpRoomLogic *l;
    if (pe_imp_catalog_roomlogic("pe-rdt-test/store", "re3:logic/100", &l, why, sizeof why)) { printf("  %s\n", why); assert(0); }
    uint32_t entries = 0, bytes = 0, whole = 0;
    assert(pe_imp_logic_section(l, PE_IMP_LOGIC_ARDS, &entries, &bytes) && entries >= 1);
    assert(pe_imp_logic_section(l, PE_IMP_LOGIC_RDT0, NULL, &whole) && whole > 10000);
    PeImpAsset *a;
    int count = pe_imp_catalog_assets("pe-rdt-test/store", "re3", &a), sounds = 0, banks = 0;
    char last[24] = "";
    for (int i = 0; i < count; i++) {
        if (strcmp(a[i].type, "Sound") || !strstr(a[i].id, ":sound/room")) continue;
        sounds++;
        const char *slash = strrchr(a[i].id, '/');                  /* re3:sound/room100/wave01: bank up to it */
        if (slash && strncmp(last, a[i].id, (size_t)(slash - a[i].id)))
            snprintf(last, sizeof last, "%.*s", (int)(slash - a[i].id), a[i].id), banks++;
    }
    free(a);
    printf("RE3 scan: logic/100 keeps %u archive entries (%u bytes) and its whole room (%u bytes); %d room sounds in %d banks%s%s\n",
           entries, bytes, whole, sounds, banks, sc.refused ? ", refused: " : "", sc.refused ? sc.last_error : "");
    assert(!sc.refused && sounds > 100 && banks > 50);
    free(l);
}

/* an RE1 room: camera zones (one the camera's own area), one script procedure with a door to
 * stage 2 room 5 and an item, then the end of the chain */
static void made_up_re1(PeImpRdt *r) {
    static uint8_t b[0x400];
    memset(b, 0, sizeof b);
    uint32_t rvd = 0x100, init = 0x200, main_scd = 0x300;
    put32(b + 0x48 + 0 * 4, rvd);
    put32(b + 0x48 + 6 * 4, init);
    put32(b + 0x48 + 7 * 4, main_scd);
    put16(b + rvd, 9), put16(b + rvd + 2, 0);                       /* camera 0's own area */
    put16(b + rvd + 20, 1), put16(b + rvd + 22, 0);                 /* 0 -> 1 */
    put16(b + rvd + 40, 0xffff);
    uint8_t *p = b + init;
    put16(p, 2 + 26 + 26);
    p += 2;
    p[0] = 0x0C, p[1] = 3;
    put16(p + 2, 100), put16(p + 4, 200), put16(p + 6, 300), put16(p + 8, 400);
    p[12] = 7, p[14] = 11, p[15] = (uint8_t)(3 << 5 | 5);
    put16(p + 16, 1000), put16(p + 22, 2048), p[24] = 2;
    p += 26;
    p[0] = 0x18, p[1] = 4, p[10] = 0x2A, p[11] = 15;
    char why[100];
    assert(!pe_imp_rdt_read(PE_IMP_RE1, b, sizeof b, r, why, sizeof why));
    assert(r->procedures == 1 && r->unreadable == 0 && r->doors == 1 && r->items == 1 && r->cuts == 2);
    const PeImpRdtDoor *d = &r->door[0];
    assert(d->zone.id == 3 && d->zone.x[1] == 400 && d->zone.z[2] == 600 && d->animation == 7 && d->lock_id == 11);
    assert(d->stage == 2 && d->room == 5 && d->next_x == 1000 && d->next_dir == 2048 && d->lock_type == 2);
    assert(r->item[0].item == 0x2A && r->item[0].count == 15);
    assert(r->cut[0].flag == 1 && r->cut[1].flag == 0 && r->cut[1].from == 0 && r->cut[1].to == 1);
    for (int round = 0; round < 2000; round++) {                   /* scrambled: stays in bounds */
        static uint8_t c[0x400];
        memcpy(c, b, sizeof c);
        for (int k = 0; k < 8; k++) c[rand() % (int)sizeof c] = (uint8_t)rand();
        pe_imp_rdt_read(PE_IMP_RE1, c, sizeof c, r, why, sizeof why);
    }
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
    assert(pe_imp_rdt_read(9, b, size, &r, why, sizeof why) == -1);
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
    made_up_re1(&r);
    printf("made-up room: doors, item, zone, enemy and camera zones read; damaged rooms stay in bounds\n");
    made_up_logic(b, size);
}

static void disc(const char *label, const char *cue, unsigned game) {
    char why[200];
    PeImpDisc *d = pe_imp_disc_open(cue, why, sizeof why);
    assert(d);
    static PeImpRdt r;
    uint32_t rooms = 0, procs = 0, bad = 0, doors = 0, aots = 0, items = 0, cuts = 0, enemies = 0, over = 0;
    uint64_t logic_bytes = 0;
    uint32_t no_collision = 0;
    for (int i = 0; i < pe_imp_disc_files(d); i++) {
        const char *name = pe_imp_disc_name(d, i);
        int is_room = game == PE_IMP_RE1   ? pe_imp_match("*/STAGE?/ROOM*.RDT", name)
                      : game == PE_IMP_RE2 ? pe_imp_match("PL?/RDT/ROOM*.RDT", name)
                                           : pe_imp_match("CD_DATA/STAGE?/R*.ARD", name);
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
            const char *show = getenv("PE_TEST_RDT_ROOM");          /* e.g. ROOM1060: print its doors */
            if (show && *show && strstr(name, show))
                for (uint32_t k = 0; k < r.doors && k < PE_IMP_RDT_DOORS; k++) {
                    const PeImpRdtDoor *dr = &r.door[k];
                    printf("  %s door %u -> stage %d room %02X at (%d, %d, %d) facing %d, camera %u, animation %u, lock %u/%u\n",
                           name, k, dr->stage == 0xFF ? -1 : dr->stage + 1, dr->room, dr->next_x, dr->next_y, dr->next_z,
                           dr->next_dir, dr->camera, dr->animation, dr->lock_id, dr->lock_type);
                }
            PeImpRoomLogic *l = pe_imp_rdt_logic(game, 0, 0, 0, rdt, rdt_size, why, sizeof why);
            uint32_t ld = 0;
            assert(l);
            no_collision += !pe_imp_logic_section(l, PE_IMP_LOGIC_COLL, NULL, NULL);
            pe_imp_logic_section(l, PE_IMP_LOGIC_DOOR, &ld, NULL);
            assert(ld == r.doors);
            logic_bytes += l->size;
            free(l);
            for (uint32_t k = 0; k < r.doors && k < PE_IMP_RDT_DOORS; k++)
                assert(r.door[k].stage < 8 || (game == PE_IMP_RE1 && r.door[k].stage == 0xFF));  /* a stage */
        }
        free(data);
    }
    pe_imp_disc_close(d);
    printf("%s: %u rooms, %u procedures, %u unreadable spots, %u doors, %u zones, %u items, %u enemy placements, %u camera zones, %u rooms over the caps\n",
           label, rooms, procs, bad, doors, aots, items, enemies, cuts, over);
    printf("%s: RoomLogic records %llu KB in all; %u rooms without collision\n", label,
           (unsigned long long)(logic_bytes / 1024), no_collision);
    assert(no_collision * 10 < rooms);
    assert(rooms > 100 && doors > 200 && cuts > 500 && !over);
    if (game != PE_IMP_RE3) assert(!bad);
    else assert(bad * 100 < procs);
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    made_up();
    const char *re1 = getenv("PE_TEST_RE1_DISC"), *re2 = getenv("PE_TEST_RE2_DISC"), *re3 = getenv("PE_TEST_RE3_DISC");
    if (re1 && *re1) disc("RE1", re1, PE_IMP_RE1), scan_doors("RE1", re1, "re1", &g_re1_door_ps1, 0),
                     scan_items("RE1", re1, "re1", &g_re1_items_ps1, 2, "Beretta", 76),
                     movie("RE1", re1, "PSXUSA/ZMOVIE/OJ.STR"),
                     scan_files("RE1", re1, "re1", &g_re1_files_ps1, 19);
    else printf("PE_TEST_RE1_DISC not set: RE1 rooms skipped\n");
    if (re2 && *re2) disc("RE2", re2, PE_IMP_RE2), scan_re2(re2), scan_doors("RE2", re2, "re2", &g_re2_door_ps1, 1),
                     scan_items("RE2", re2, "re2", &g_re2_items_ps1, 7, "Shotgun", 106),
                     movie("RE2", re2, "ZMOVIE/R200.STR"),
                     scan_files("RE2", re2, "re2", &g_re2_files_ps1, 191),
                     scan_files("RE2 map", re2, "re2", &g_re2_maps_ps1, 20);
    else printf("PE_TEST_RE2_DISC not set: RE2 rooms skipped\n");
    if (re3 && *re3) disc("RE3", re3, PE_IMP_RE3), scan_doors("RE3", re3, "re3", &g_re3_door_ps1, 0),
                     scan_items("RE3", re3, "re3", &g_re3_items_ps1, 1, "Combat Knife", 228), scan_re3(re3), movie("RE3", re3, "CD_DATA/ZMOVIE/INS05.STR"),
                     scan_files("RE3", re3, "re3", &g_re3_files_ps1, 183),
                     scan_files("RE3 map", re3, "re3", &g_re3_maps_ps1, 18);
    else printf("PE_TEST_RE3_DISC not set: RE3 rooms skipped\n");
    printf("RDT passed\n");
    return 0;
}
