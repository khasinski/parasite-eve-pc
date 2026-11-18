/* Importer core (mods/host/import) and the RE2 player-model reader (mods/pe_src_re2), on made-up
 * data: SHA-256, the JSON reader, file patterns, a made-up PLD (every truncation and seeded
 * corruptions must be refused cleanly), made-up disc images (cooked .iso, raw .bin behind a .cue)
 * scanned into a store and catalog, and the store round trip.
 * With PE_TEST_RE2_DISC set to Resident Evil 2 (USA) disc 2's .cue, also scans the real disc and
 * checks Claire (re2:player/claire) as PR #4's Python reader sees her: 15 bones, 19 parts. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pe_imp.h"
#include "pe_imp_internal.h"
#include "pe_imp_pe.h"
#include "pe_src_re2.h"

static int s_fail;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #c); s_fail = 1; } } while (0)

/* ---- little-endian writer ---- */
typedef struct { uint8_t *b; size_t n, cap; } W;
static void put(W *w, const void *p, size_t n) {
    if (w->n + n > w->cap) { w->cap = (w->n + n) * 2 + 64; w->b = realloc(w->b, w->cap); assert(w->b); }
    memcpy(w->b + w->n, p, n), w->n += n;
}
static void p8(W *w, unsigned v) { uint8_t b = (uint8_t)v; put(w, &b, 1); }
static void p16(W *w, unsigned v) { p8(w, v), p8(w, v >> 8); }
static void p32(W *w, uint32_t v) { p16(w, v & 0xffff), p16(w, v >> 16); }
static void at32(W *w, size_t at, uint32_t v) { for (int i = 0; i < 4; i++) w->b[at + i] = (uint8_t)(v >> 8 * i); }

/* A made-up PLD: 3 bones (0 root, 1 and 2 children of 0), 2 parts (part 0: one triangle,
 * part 1: one quad), a 4x2 8-bit image (w = 2 halfwords) with one 256-colour palette. */
static uint8_t *make_pld(uint32_t *size) {
    W w = {0};
    p32(&w, 0), p32(&w, 0);                         /* directory offset, patched */
    size_t emr = w.n;
    p16(&w, 8 + 18), p16(&w, 0), p16(&w, 3), p16(&w, 0);   /* armature at +26, 3 bones */
    int16_t rel[3][3] = {{0, -100, 0}, {10, 20, 30}, {-10, 20, 30}};
    for (int i = 0; i < 3; i++) for (int c = 0; c < 3; c++) p16(&w, (uint16_t)rel[i][c]);
    /* armature: bone 0 has 2 children at +12; bones 1, 2 none */
    p16(&w, 2), p16(&w, 12), p16(&w, 0), p16(&w, 14), p16(&w, 0), p16(&w, 14);
    p8(&w, 1), p8(&w, 2);
    while (w.n % 4) p8(&w, 0);
    size_t md1 = w.n;
    p32(&w, 0), p32(&w, 0), p32(&w, 4);             /* 4 objects = 2 parts */
    size_t base = w.n, table = w.n;
    for (int i = 0; i < 4 * 7; i++) p32(&w, 0);    /* object table, patched */
    for (int part = 0; part < 2; part++) {
        for (int kind = 0; kind < 2; kind++) {
            int k = kind ? 4 : 3, fc = kind == part ? 1 : 0;
            uint32_t vo = (uint32_t)(w.n - base);
            for (int v = 0; v < 4; v++) p16(&w, (uint16_t)(v * 7 + part)), p16(&w, (uint16_t)-v), p16(&w, 3), p16(&w, 0);
            uint32_t no = (uint32_t)(w.n - base);
            for (int v = 0; v < 4; v++) p16(&w, 0), p16(&w, 4096), p16(&w, 0), p16(&w, 0);
            uint32_t fo = (uint32_t)(w.n - base);
            for (int f = 0; f < fc; f++) for (int j = 0; j < k; j++) p16(&w, (unsigned)j), p16(&w, (unsigned)(k - 1 - j));
            uint32_t to = (uint32_t)(w.n - base);
            for (int f = 0; f < fc; f++) for (int j = 0; j < k; j++) p8(&w, (unsigned)j), p8(&w, 1), p16(&w, j == 0 ? 0x7F00u : j == 1 ? 0x001Fu : 0);
            size_t at = table + (size_t)(part * 2 + kind) * 28;
            uint32_t vals[7] = {vo, 4, no, 4, fo, (uint32_t)fc, to};
            for (int i = 0; i < 7; i++) at32(&w, at + 4 * (size_t)i, vals[i]);
        }
    }
    while (w.n % 4) p8(&w, 0);
    size_t tim = w.n;
    p32(&w, 0x10), p32(&w, 9);                      /* TIM, 8-bit with CLUT */
    p32(&w, 12 + 512), p16(&w, 0), p16(&w, 0), p16(&w, 256), p16(&w, 1);
    for (int c = 0; c < 256; c++) p16(&w, (unsigned)(c * 3));
    p32(&w, 12 + 8), p16(&w, 0), p16(&w, 0), p16(&w, 2), p16(&w, 2);   /* 2 halfwords = 4 px wide, 2 high */
    for (int i = 0; i < 8; i++) p8(&w, (unsigned)(i * 11));
    size_t dir = w.n;
    p32(&w, 0), p32(&w, (uint32_t)emr), p32(&w, (uint32_t)md1), p32(&w, (uint32_t)tim);
    at32(&w, 0, (uint32_t)dir);
    *size = (uint32_t)w.n;
    return w.b;
}

static void test_basics(void) {
    char h[65];
    pe_imp_sha256_hex((const uint8_t *)"", 0, h);
    CHECK(!strcmp(h, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    pe_imp_sha256_hex((const uint8_t *)"abc", 3, h);
    CHECK(!strcmp(h, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    char m56[] = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    pe_imp_sha256_hex((const uint8_t *)m56, strlen(m56), h);
    CHECK(!strcmp(h, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));

    const char *good = "{\"a\": [1, -2, {\"b\": \"x\\u00e9\\n\"}], \"t\": true, \"z\": null}";
    char why[160];
    PjNode *j = pj_parse(good, strlen(good), why, sizeof why);
    CHECK(j && pj_int(pj_at(pj_get(j, "a"), 1), 0) == -2);
    CHECK(j && !strcmp(pj_str(pj_get(pj_at(pj_get(j, "a"), 2), "b"), ""), "x\xc3\xa9\n"));
    pj_free(j);
    const char *bad[] = {"", "{", "[1,]", "{\"a\" 1}", "1.5", "\"\\ud800\"", "[1] 2", "\"a", "{\"a\":tru}"};
    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) {
        j = pj_parse(bad[i], strlen(bad[i]), why, sizeof why);
        CHECK(!j);
        pj_free(j);
    }
    char deep[200];
    memset(deep, '[', sizeof deep);
    CHECK(!pj_parse(deep, sizeof deep, why, sizeof why));

    CHECK(pe_imp_match("PL?/PLD/PL??.PLD", "PL1/PLD/PL01.PLD"));
    CHECK(pe_imp_match("pl?/pld/pl??.pld", "PL0/PLD/PL0C.PLD"));
    CHECK(!pe_imp_match("PL?/PLD/PL??.PLD", "PL1/PLD/PL01W00.PLW"));
    CHECK(!pe_imp_match("PL?/PLD/PL??.PLD", "PL1/PLD/X/PL01.PLD"));
    CHECK(pe_imp_match("*/EM*.EMD", "ENEMY/EM1000.EMD"));
    CHECK(!pe_imp_match("*.EMD", "ENEMY/EM1000.EMD"));
}

static void test_pld(void) {
    uint32_t size;
    uint8_t *pld = make_pld(&size);
    PeImpModel m;
    char why[200] = "";
    int rc = re2_pld_parse(pld, size, &m, why, sizeof why);
    if (rc) fprintf(stderr, "made-up PLD: %s\n", why);
    CHECK(!rc);
    if (!rc) {
        CHECK(m.bones == 3 && m.parent[0] == -1 && m.parent[1] == 0 && m.parent[2] == 0);
        CHECK(m.pos[1][0] == 10 && m.pos[1][1] == -80 && m.pos[2][2] == 30);
        CHECK(m.parts == 2 && m.part[0].tris == 1 && m.part[0].quads == 0 && m.part[1].quads == 1);
        CHECK(m.part[1].quad[0].v[0][0] == 3 * 7 + 1);           /* corner 0 uses vertex k-1 = 3 */
        CHECK(m.part[0].tri[0].clut == 0x7F00 && m.part[0].tri[0].page == 0x001F);
        CHECK(m.part[0].tri[0].n[1][1] == 4096);
        CHECK(m.w == 4 && m.h == 2 && m.image[7] == 77 && m.pal_rows == 1 && m.pal[0][2] == 6);
        /* store round trip */
        CHECK(!pe_imp_mkdirs("pe-import-test/model"));
        CHECK(!pe_imp_model_save(&m, "pe-import-test/model", why, sizeof why));
        PeImpModel back;
        CHECK(!pe_imp_model_load("pe-import-test/model", &back, why, sizeof why));
        CHECK(back.bones == m.bones && back.parts == m.parts && back.w == m.w &&
              !memcmp(back.pos, m.pos, m.bones * sizeof *m.pos) &&
              !memcmp(back.part[1].quad, m.part[1].quad, sizeof *m.part[1].quad) &&
              !memcmp(back.image, m.image, (size_t)m.w * m.h) && !memcmp(back.pal, m.pal, sizeof *m.pal));
        pe_imp_model_free(&back);
        pe_imp_model_free(&m);
    }
    /* every truncation is refused, never read past */
    for (uint32_t cut = 0; cut < size; cut++) {
        uint8_t *t = malloc(cut ? cut : 1);
        memcpy(t, pld, cut);
        CHECK(re2_pld_parse(t, cut, &m, why, sizeof why) != 0);
        free(t);
    }
    /* seeded corruption: refused or parsed, never a crash */
    uint32_t seed = 12345;
    for (int round = 0; round < 3000; round++) {
        uint8_t *t = malloc(size);
        memcpy(t, pld, size);
        for (int k = 0; k < 4; k++) {
            seed = seed * 1103515245u + 12345u;
            t[(seed >> 8) % size] = (uint8_t)(seed >> 3);
        }
        if (!re2_pld_parse(t, size, &m, why, sizeof why)) pe_imp_model_free(&m);
        free(t);
    }
    free(pld);
}

/* ---- made-up disc images: ISO 9660 with PL1/PLD/PL01.PLD ---- */
static void dirrec(W *w, const char *name, uint32_t lba, uint32_t size, int dir) {
    size_t nl = name[0] ? strlen(name) : 1, len = 33 + nl + (nl % 2 == 0);   /* "\0" is the folder itself */
    p8(w, (unsigned)len), p8(w, 0);
    p32(w, lba), p8(w, lba >> 24), p8(w, lba >> 16), p8(w, lba >> 8), p8(w, lba);
    p32(w, size), p8(w, size >> 24), p8(w, size >> 16), p8(w, size >> 8), p8(w, size);
    for (int i = 0; i < 7; i++) p8(w, 0);
    p8(w, dir ? 2 : 0), p8(w, 0), p8(w, 0), p16(w, 1), p8(w, 0), p8(w, 1);
    p8(w, (unsigned)nl);
    put(w, name, nl);
    if (nl % 2 == 0) p8(w, 0);
}

static void sector_pad(W *w) { while (w->n % 2048) p8(w, 0); }

static uint8_t *make_iso(const uint8_t *file, uint32_t fsize, size_t *out) {
    W w = {0};
    for (int i = 0; i < 16 * 2048; i++) p8(&w, 0);
    p8(&w, 1), put(&w, "CD001", 5), p8(&w, 1);                 /* PVD, root record at 156 */
    while (w.n < 16 * 2048 + 156) p8(&w, 0);
    dirrec(&w, "\0", 18, 2048, 1);
    sector_pad(&w);
    p8(&w, 255), put(&w, "CD001", 5), p8(&w, 1);               /* terminator */
    sector_pad(&w);
    /* sector 18: root -> PL1 (19); 19: PL1 -> PLD (20); 20: PLD -> PL01.PLD;1 (21) */
    dirrec(&w, "\0", 18, 2048, 1), dirrec(&w, "\1", 18, 2048, 1), dirrec(&w, "PL1", 19, 2048, 1), sector_pad(&w);
    dirrec(&w, "\0", 19, 2048, 1), dirrec(&w, "\1", 18, 2048, 1), dirrec(&w, "PLD", 20, 2048, 1), sector_pad(&w);
    dirrec(&w, "\0", 20, 2048, 1), dirrec(&w, "\1", 19, 2048, 1), dirrec(&w, "PL01.PLD;1", 21, fsize, 0);
    dirrec(&w, "README.TXT;1", 21, 4, 0), sector_pad(&w);
    put(&w, file, fsize), sector_pad(&w);
    *out = w.n;
    return w.b;
}

static void write_file(const char *path, const void *data, size_t n) {
    FILE *f = fopen(path, "wb");
    assert(f);
    fwrite(data, 1, n, f);
    fclose(f);
}

static void test_sound(void) {
    W iso = {0};
    uint8_t header[3232] = {0}, vb[16] = {0};
    memcpy(header + 128, "pBAV", 4);
    header[128 + 18] = header[128 + 20] = header[128 + 22] = 1;
    header[2722] = 2;
    vb[2] = 0xf1;
    size_t base_size;
    uint8_t *base = make_iso(vb, sizeof vb, &base_size);
    put(&iso, base, 18 * 2048);
    free(base);
    dirrec(&iso, "COMMON", 19, 2048, 1), sector_pad(&iso);
    dirrec(&iso, "SOUND", 20, 2048, 1), sector_pad(&iso);
    dirrec(&iso, "ARMS01.EDH;1", 21, sizeof header, 0);
    dirrec(&iso, "ARMS01.VB;1", 23, sizeof vb, 0);
    dirrec(&iso, "ARMS02.EDH;1", 21, sizeof header, 0);
    dirrec(&iso, "ARMS02.VB;1", 23, sizeof vb, 0), sector_pad(&iso);
    put(&iso, header, sizeof header), sector_pad(&iso);
    put(&iso, vb, sizeof vb), sector_pad(&iso);

    const char *image = "pe-import-test/discs/sound.iso";
    const char *store = "pe-import-test/sound-store";
    CHECK(!pe_imp_mkdirs("pe-import-test/discs"));
    write_file(image, iso.b, iso.n);
    pe_imp_registry_reset();
    CHECK(!pe_imp_add_reader(&g_re2_sound_ps1));
    PeImpScan scan;
    PeImpSound *clip;
    const char *id = "re2:sound/arms01/wave01";
    CHECK(!pe_imp_scan(store, image, NULL, &scan) && !scan.refused);
    CHECK(!pe_imp_catalog_sound(store, id, &clip, NULL, 0));
    CHECK(clip && clip->rate == 44100 && clip->frames == 28 && clip->pcm[0] == 4096 && clip->pcm[1] == -4096);
    free(clip);
    CHECK(!pe_imp_scan(store, image, NULL, &scan) && scan.skipped == 2);
    /* Byte-identical banks with distinct names must keep both catalog IDs on a warm scan. */
    CHECK(!pe_imp_catalog_sound(store, "re2:sound/arms02/wave01", &clip, NULL, 0));
    CHECK(clip && clip->pcm[0] == 4096);
    free(clip);

    /* Only the companion changed. The cached PCM must be replaced. */
    iso.b[23 * 2048 + 2] = 0xe2;
    write_file(image, iso.b, iso.n);
    CHECK(!pe_imp_scan(store, image, NULL, &scan) && !scan.refused);
    CHECK(!pe_imp_catalog_sound(store, id, &clip, NULL, 0));
    CHECK(clip && clip->pcm[0] == 8192 && clip->pcm[1] == -8192);
    free(clip);

    iso.b[23 * 2048] = 0xf0;
    write_file(image, iso.b, iso.n);
    CHECK(!pe_imp_scan(store, image, NULL, &scan) && scan.refused == 2);
    CHECK(pe_imp_catalog_sound(store, id, &clip, NULL, 0) == -1 && !clip);
    free(iso.b);
}

static void test_scan(void) {
    uint32_t psize;
    uint8_t *pld = make_pld(&psize);
    size_t isize;
    uint8_t *iso = make_iso(pld, psize, &isize);
    CHECK(!pe_imp_mkdirs("pe-import-test/discs"));
    write_file("pe-import-test/discs/made-up.iso", iso, isize);
    /* the same as a raw mode 2 .bin behind a .cue */
    W raw = {0};
    for (size_t s = 0; s < isize / 2048; s++) {
        static const uint8_t sync[12] = {0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0};
        put(&raw, sync, 12);
        p8(&raw, 0), p8(&raw, 0), p8(&raw, 0), p8(&raw, 2);
        for (int i = 0; i < 8; i++) p8(&raw, 0);
        put(&raw, iso + s * 2048, 2048);
        for (int i = 0; i < 280; i++) p8(&raw, 0);
    }
    write_file("pe-import-test/discs/Made Up (Track 1).bin", raw.b, raw.n);
    const char *cue = "FILE \"Made Up (Track 1).bin\" BINARY\n  TRACK 01 MODE2/2352\n    INDEX 01 00:00:00\n";
    write_file("pe-import-test/discs/Made Up.cue", cue, strlen(cue));

    char why[200];
    PeImpDisc *d = pe_imp_disc_open("pe-import-test/discs/Made Up.cue", why, sizeof why);
    CHECK(d != NULL);
    if (d) {
        CHECK(pe_imp_disc_files(d) == 2);
        int i = pe_imp_disc_find(d, "pl1/pld/pl01.pld");
        CHECK(i >= 0 && pe_imp_disc_size(d, i) == psize);
        uint8_t *got;
        uint32_t gsize;
        CHECK(i >= 0 && !pe_imp_disc_read(d, i, &got, &gsize, why, sizeof why) && gsize == psize && !memcmp(got, pld, psize));
        if (i >= 0) free(got);
        pe_imp_disc_close(d);
    }

    /* start from an empty store: without source.json the file is read again */
    char sha[65], p[256];
    pe_imp_sha256_hex(pld, psize, sha);
    snprintf(p, sizeof p, "pe-import-test/store/re2/%s/source.json", sha), remove(p);
    snprintf(p, sizeof p, "pe-import-test/store/re2/%s/items.json", sha), remove(p);
    remove("pe-import-test/store/re2/catalog.json");

    pe_imp_registry_reset();
    CHECK(!pe_imp_add_reader(&g_re2_pld_ps1));
    CHECK(pe_imp_add_reader(&g_re2_pld_ps1) == -1);           /* duplicate */
    CHECK(!pe_imp_set_naming("re2", g_re2_naming));
    PeImpScan sc;
    CHECK(!pe_imp_scan("pe-import-test/store", "pe-import-test/discs/made-up.iso", NULL, &sc));
    CHECK(!strcmp(sc.game, "re2") && sc.files_read == 1 && sc.items == 1 && sc.refused == 0);
    if (sc.refused) fprintf(stderr, "scan: %s\n", sc.last_error);
    CHECK(!pe_imp_scan("pe-import-test/store", "pe-import-test/discs/Made Up.cue", NULL, &sc));
    CHECK(sc.skipped == 1 && sc.items == 0);                  /* same file: already in the store */
    PeImpModel m;
    CHECK(!pe_imp_catalog_model("pe-import-test/store", "re2:player/claire", &m, why, sizeof why));
    CHECK(m.bones == 3 && m.parts == 2);
    pe_imp_model_free(&m);
    CHECK(pe_imp_catalog_model("pe-import-test/store", "re2:player/nobody", &m, why, sizeof why) == -1);
    PjNode *cat = pj_load("pe-import-test/store/re2/catalog.json", why, sizeof why);
    const PjNode *e = pj_get(cat, "re2:player/claire");
    /* both images hold the same bytes: one store entry, one source (the store is keyed by hash) */
    CHECK(e && !strcmp(pj_str(pj_get(e, "name"), ""), "Claire") && pj_at(pj_get(e, "sources"), 0) &&
          !pj_at(pj_get(e, "sources"), 1));
    pj_free(cat);

    /* a new naming table: the next scan of the image files Claire under her new ID, and the old
     * one is gone from the catalog (no stale entry beside it) */
    uint32_t before = pe_imp_naming_hash();
    CHECK(!pe_imp_set_naming("re2", "{\"player/pl01\": {\"id\": \"player/claire_b\", \"name\": \"Claire B\"}}"));
    CHECK(pe_imp_naming_hash() != before);
    CHECK(!pe_imp_scan("pe-import-test/store", "pe-import-test/discs/Made Up.cue", NULL, &sc));
    cat = pj_load("pe-import-test/store/re2/catalog.json", why, sizeof why);
    CHECK(cat && !pj_get(cat, "re2:player/claire") && pj_get(cat, "re2:player/claire_b") &&
          !strcmp(pj_str(pj_get(pj_get(cat, "re2:player/claire_b"), "name"), ""), "Claire B"));
    pj_free(cat);
    CHECK(!pe_imp_set_naming("re2", g_re2_naming) && pe_imp_naming_hash() == before);

    /* disc IDs: a game that names one claims only a disc holding it (another game's disc can share
     * its file layout); the ID is part of the hash that makes old scans run again */
    CHECK(pe_imp_add_disc_id("re2", "SLUS_007.48") == -1);               /* the game isn't added yet */
    CHECK(!pe_imp_add_game("re2", "Resident Evil 2", "Resident Evil") && !pe_imp_add_disc_id("re2", "SLUS_007.48"));
    CHECK(pe_imp_naming_hash() != before);
    CHECK(pe_imp_scan("pe-import-test/store", "pe-import-test/discs/Made Up.cue", NULL, &sc) == -1);
    CHECK(strstr(sc.last_error, "no reader recognises") != NULL);
    CHECK(!pe_imp_add_disc_id("re2", "PL1/*/PL01.PLD") && pe_imp_disc_id_count("re2") == 2);
    CHECK(!pe_imp_scan("pe-import-test/store", "pe-import-test/discs/Made Up.cue", NULL, &sc) && !strcmp(sc.game, "re2"));

    /* a broken image: folder records pointing past the end are refused, not followed */
    iso[18 * 2048 + 2 * 34 + 2] = 0xff;                        /* PL1's LBA -> far away */
    write_file("pe-import-test/discs/broken.iso", iso, isize);
    d = pe_imp_disc_open("pe-import-test/discs/broken.iso", why, sizeof why);
    CHECK(d == NULL);
    pe_imp_disc_close(d);
    free(raw.b), free(iso), free(pld);
}

/* to_yxz then rot_yxz gives the rotation back (to the 4096-step grid), gimbal lock included:
 * every pitch, the two quarter turns with every yaw and roll, and seeded random angles. */
static double rot_error(const int a[3]) {
    PeM3 m, back;
    int got[3];
    pe_imp_rot_yxz(a, m);
    pe_imp_to_yxz(m, got);
    pe_imp_rot_yxz(got, back);
    double e = 0;
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) { double d = m[i][j] - back[i][j]; if (d < 0) d = -d; if (d > e) e = d; }
    return e;
}

static void test_to_yxz(void) {
    double worst = 0, e;
    for (int x = 0; x < 4096; x += 16) { int a[3] = {x, 512, 3000}; if ((e = rot_error(a)) > worst) worst = e; }
    for (int y = 0; y < 4096; y += 64)
        for (int z = 0; z < 4096; z += 64) {
            int a[3] = {1024, y, z}, b[3] = {3072, y, z};
            if ((e = rot_error(a)) > worst) worst = e;
            if ((e = rot_error(b)) > worst) worst = e;
        }
    uint32_t seed = 99;
    for (int i = 0; i < 20000; i++) {
        int a[3];
        for (int c = 0; c < 3; c++) seed = seed * 1103515245u + 12345u, a[c] = (int)((seed >> 8) & 4095);
        if ((e = rot_error(a)) > worst) worst = e;
    }
    printf("to_yxz round trip: largest matrix error %.5f\n", worst);
    CHECK(worst < 0.005);                              /* one grid step is 0.0015 rad */
}

static void test_real_disc(void) {
    const char *cue = getenv("PE_TEST_RE2_DISC");
    if (!cue || !*cue) { printf("PE_TEST_RE2_DISC not set: real-disc check skipped\n"); return; }
    pe_imp_registry_reset();
    pe_imp_add_reader(&g_re2_pld_ps1);
    pe_imp_set_naming("re2", g_re2_naming);
    CHECK(!pe_imp_add_reader(&g_re2_sound_ps1));
    PeImpScan sc;
    int rc = pe_imp_scan("pe-import-test/real-store", cue, NULL, &sc);
    printf("real disc: game %s, %d files read, %d items, %d skipped, %d refused %s\n",
           sc.game, sc.files_read, sc.items, sc.skipped, sc.refused, sc.last_error);
    CHECK(!rc && !sc.refused && !strcmp(sc.game, "re2"));
    PeImpModel m;
    char why[200];
    CHECK(!pe_imp_catalog_model("pe-import-test/real-store", "re2:player/claire", &m, why, sizeof why));
    uint32_t faces = 0;
    for (uint32_t p = 0; p < m.parts; p++) faces += m.part[p].tris + m.part[p].quads;
    printf("Claire: %u bones, %u parts, %u faces, %ux%u image, %u palettes\n", m.bones, m.parts, faces, m.w, m.h, m.pal_rows);
    /* as PR #4's Python reader (tools/re2-skin/re2_model.py) reads the same file */
    CHECK(m.bones == 15 && m.parts == 19 && faces == 492 && m.w == 384 && m.h == 256 && m.pal_rows == 3);
    CHECK(m.pos[5][0] == -25 && m.pos[5][1] == -1510 && m.pos[5][2] == 139);
    if (m.parts > 4 && m.part[4].tris) {
        const PeImpFace *f = &m.part[4].tri[0];
        CHECK(f->v[0][0] == -103 && f->v[0][1] == 95 && f->v[0][2] == -53 && f->v[2][1] == -96);
        CHECK(f->n[0][0] == -3512 && f->n[1][2] == 2911 && f->n[2][1] == -1811);
        CHECK(f->uv[0][1] == 186 && f->uv[2][0] == 4 && f->uv[2][1] == 165 && f->clut == 30720 && f->page == 128);
    } else {
        CHECK(!"part 4 has a triangle");
    }
    pe_imp_model_free(&m);
    PeImpSound *clip;
    CHECK(!pe_imp_catalog_sound("pe-import-test/real-store", "re2:sound/arms01/wave02", &clip, why, sizeof why));
    CHECK(clip && clip->frames == 3640);
    free(clip);
}

int main(void) {
    test_basics();
    test_pld();
    test_scan();
    test_sound();
    test_to_yxz();
    test_real_disc();
    if (!s_fail) printf("importer core and RE2 reader passed\n");
    return s_fail;
}
