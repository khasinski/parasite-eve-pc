/* pe_lang's .pelang opener: a plain pack, a zipped one, and every damaged zip refused whole.
 * The zips are written here with the same zip library the game reads them with. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pe_lang_bundle.h"

#if PE_LANG_ZIP
#include "miniz.h"
#endif

static void wr32(unsigned char *p, uint32_t v) {
    p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24);
}

/* A one-entry version 1 pack, as pelang.py writes it. */
static unsigned char *make_pack(size_t *size) {
    static const char glyphs[] = "\x12\x37\x48";
    const size_t text = 64 + 8, total = text + sizeof glyphs;
    unsigned char *p = calloc(1, total);
    memcpy(p, "PELG", 4); wr32(p + 4, 1); wr32(p + 8, 1); wr32(p + 12, (uint32_t)text);
    memcpy(p + 16, "es", 2); memcpy(p + 32, "Spanish", 7);
    wr32(p + 64, pe_lang_hash("item/054")); wr32(p + 68, 0);
    memcpy(p + text, glyphs, 3); p[text + 3] = 0xFF;
    *size = total;
    return p;
}

static void expect_item(const PeLangPack *pack) {
    const unsigned char *t = pe_lang_pack_find(pack, "item/054");
    assert(t && !memcmp(t, "\x12\x37\x48\xFF", 4));
    assert(!strcmp(pack->lang, "es") && !strcmp(pack->name, "Spanish"));
}

/* A pictures.bin with these pieces (already in order), as petext/pictures.py writes it. */
typedef struct Piece { uint32_t sector; unsigned offset; const char *english, *new_bytes; } Piece;
static unsigned char *make_pictures(const Piece *pieces, unsigned n, size_t *size) {
    size_t bytes = 0, table = 16 + 16u * n;
    for (unsigned i = 0; i < n; ++i) bytes += strlen(pieces[i].new_bytes);
    unsigned char *p = calloc(1, table + bytes);
    memcpy(p, "PEPX", 4); wr32(p + 4, 1); wr32(p + 8, n); wr32(p + 12, (uint32_t)table);
    size_t at = 0;
    for (unsigned i = 0; i < n; ++i) {
        unsigned char *r = p + 16 + 16 * i;
        size_t len = strlen(pieces[i].new_bytes);
        assert(len == strlen(pieces[i].english));
        wr32(r, pieces[i].sector);
        r[4] = (unsigned char)pieces[i].offset; r[5] = (unsigned char)(pieces[i].offset >> 8);
        r[6] = (unsigned char)len; r[7] = (unsigned char)(len >> 8);
        wr32(r + 8, pe_lang_crc32((const unsigned char *)pieces[i].english, len));
        wr32(r + 12, (uint32_t)at);
        memcpy(p + table + at, pieces[i].new_bytes, len);
        at += len;
    }
    *size = table + bytes;
    return p;
}

static unsigned char *copy_of(const unsigned char *p, size_t size) {
    unsigned char *c = malloc(size); memcpy(c, p, size); return c;
}

static void test_pictures(void) {
    /* Sector 10 has a sign at 100; sector 12 has two versions of one place (Disc 1 and Disc 2). */
    static const Piece pieces[] = {{10, 100, "EXIT", "SALI"}, {12, 2040, "Level!!!", "Nivel!!!"},
                                   {12, 2040, "Time....", "Tiempo.."}};
    size_t size;
    unsigned char *bin = make_pictures(pieces, 3, &size);
    PeLangPictures pictures;
    assert(!pe_lang_pictures_parse(&pictures, copy_of(bin, size), size) && pictures.count == 3);

    /* A read of sectors 9-12: the sign goes in; the place in 12 takes the piece whose English is there. */
    unsigned char *disc = calloc(4, 2048);
    memcpy(disc + 1 * 2048 + 100, "EXIT", 4);
    memcpy(disc + 3 * 2048 + 2040, "Time....", 8);
    assert(pe_lang_pictures_apply(&pictures, 9, 4, disc) == 2);
    assert(!memcmp(disc + 2048 + 100, "SALI", 4) && !memcmp(disc + 3 * 2048 + 2040, "Tiempo..", 8));
    /* Read again: already swapped, so nothing matches and nothing changes. */
    assert(pe_lang_pictures_apply(&pictures, 9, 4, disc) == 0);
    /* Other bytes there (another disc version): left alone. */
    memcpy(disc + 2048 + 100, "EXIT", 4); disc[2048 + 101] = 'Y';
    assert(pe_lang_pictures_apply(&pictures, 10, 1, disc + 2048) == 0 && !memcmp(disc + 2048 + 100, "EYIT", 4));
    /* Reads that don't reach the pieces. */
    assert(pe_lang_pictures_apply(&pictures, 13, 2, disc) == 0 && pe_lang_pictures_apply(&pictures, 11, 1, disc) == 0);
    free(disc);
    pe_lang_pictures_free(&pictures);

    /* Each of these is refused whole. */
    struct { unsigned at; unsigned char value; } damage[] = {
        {0, 'X'},            /* not PEPX */
        {4, 2},              /* a newer version */
        {8, 4},              /* more pieces than there are */
        {12, 0},             /* bytes said to start inside the pieces */
        {16 + 6, 0},         /* a piece with no length */
        {16 + 5, 8},         /* a piece past its sector (offset 2148) */
        {16 + 12, 200},      /* a piece's bytes past the end */
        {16 + 16, 13},       /* out of order: sector 13 before 12 */
    };
    for (unsigned i = 0; i < sizeof damage / sizeof *damage; ++i) {
        unsigned char *bad = copy_of(bin, size);
        bad[damage[i].at] = damage[i].value;
        assert(pe_lang_pictures_parse(&pictures, bad, size) && !pictures.data && !pictures.count);
    }
    assert(pe_lang_pictures_parse(&pictures, copy_of(bin, 15), 15));
    free(bin);
}

#if PE_LANG_ZIP
/* A zip with the named parts; returns the bytes (mz_free them) and their size. */
typedef struct Part { const char *name; const void *data; size_t size; } Part;
static void *make_zip(const Part *parts, unsigned n, size_t *size) {
    mz_zip_archive zip;
    void *out = NULL;
    memset(&zip, 0, sizeof zip);
    assert(mz_zip_writer_init_heap(&zip, 0, 0));
    for (unsigned i = 0; i < n; ++i)
        assert(mz_zip_writer_add_mem(&zip, parts[i].name, parts[i].data, parts[i].size, MZ_DEFAULT_COMPRESSION));
    assert(mz_zip_writer_finalize_heap_archive(&zip, &out, size));
    mz_zip_writer_end(&zip);
    return out;
}

/* Where `name`'s stored bytes start in the zip (just after its local header). */
static size_t data_at(const unsigned char *zip, size_t size, const char *name) {
    size_t len = strlen(name);
    for (size_t i = 0; i + 30 + len <= size; ++i)
        if (!memcmp(zip + i, "PK\3\4", 4) && zip[i + 26] == len && !memcmp(zip + i + 30, name, len))
            return i + 30 + len + (zip[i + 28] | zip[i + 29] << 8);
    assert(!"part not found");
    return 0;
}
#endif

int main(int argc, char **argv) {
    size_t size;
    /* --apply file.pelang in out: in holds {u32 sector, 2048 bytes} records read from a disc;
     * out gets them after the file's pictures are put in, as the game does on a read. */
    if (argc == 5 && !strcmp(argv[1], "--apply")) {
        PeLangPack pack;
        unsigned char record[4 + 2048];
        unsigned applied = 0;
        FILE *in = fopen(argv[3], "rb"), *out = fopen(argv[4], "wb");
        assert(in && out && pe_lang_bundle_load(&pack, argv[2]) == PE_LANG_OPEN_OK);
        while (fread(record, 1, sizeof record, in) == sizeof record) {
            uint32_t sector = record[0] | record[1] << 8 | record[2] << 16 | (uint32_t)record[3] << 24;
            applied += pe_lang_pictures_apply(&pack.pictures, sector, 1, record + 4);
            assert(fwrite(record, 1, sizeof record, out) == sizeof record);
        }
        fclose(in);
        fclose(out);
        printf("%u of %u pieces applied\n", applied, pack.pictures.count);
        pe_lang_pack_free(&pack);
        return 0;
    }
    /* Files written by `pelang.py bundle` (or anyone): open each and say what it holds. */
    if (argc > 1) {
        int failed = 0;
        for (int i = 1; i < argc; ++i) {
            PeLangPack pack;
            PeLangOpen result = pe_lang_bundle_load(&pack, argv[i]);
            if (result == PE_LANG_OPEN_OK) printf("%s: %s (%s), %u entries, %u picture pieces\n", argv[i], pack.name,
                                                  pack.lang, pack.count, pack.pictures.count);
            else printf("%s: refused, %s\n", argv[i], pe_lang_bundle_error(result));
            failed |= result != PE_LANG_OPEN_OK;
            pe_lang_pack_free(&pack);
        }
        return failed;
    }
    unsigned char *pack_bytes = make_pack(&size);
    PeLangPack pack;

    test_pictures();

    /* A plain pack opens as before, and the bytes are only borrowed. */
    assert(pe_lang_bundle_parse(&pack, pack_bytes, size) == PE_LANG_OPEN_OK);
    expect_item(&pack);
    pe_lang_pack_free(&pack);
    assert(pe_lang_bundle_parse(&pack, pack_bytes, 0) == PE_LANG_OPEN_UNREADABLE);
    assert(pe_lang_bundle_parse(&pack, (const unsigned char *)"PELG", 4) == PE_LANG_OPEN_BAD_PACK);
    assert(pe_lang_bundle_load(&pack, "no such file.pelang") == PE_LANG_OPEN_UNREADABLE);

#if PE_LANG_ZIP
    {
        /* A zipped pack, with a part this reader doesn't know left alone. */
        static const char readme[] = "Made by someone.";
        Part parts[] = {{"readme.txt", readme, sizeof readme - 1}, {PE_LANG_BUNDLE_TEXT, pack_bytes, size},
                        {"pictures/M351.tim", readme, 4}};
        size_t zip_size;
        unsigned char *zip = make_zip(parts, 3, &zip_size);
        assert(pe_lang_bundle_parse(&pack, zip, zip_size) == PE_LANG_OPEN_OK);
        expect_item(&pack);
        pe_lang_pack_free(&pack);

        /* The same through a file, as the folder scan and the picker read it. */
        const char *path = "pe_mod_lang_bundle_test.pelang";
        FILE *f = fopen(path, "wb");
        assert(f && fwrite(zip, 1, zip_size, f) == zip_size);
        fclose(f);
        assert(pe_lang_bundle_load(&pack, path) == PE_LANG_OPEN_OK);
        expect_item(&pack);
        pe_lang_pack_free(&pack);
        remove(path);

        /* A pack damaged inside the zip fails the zip's own check. */
        unsigned char *bad = malloc(zip_size);
        memcpy(bad, zip, zip_size);
        bad[data_at(bad, zip_size, PE_LANG_BUNDLE_TEXT) + 2] ^= 0x55;
        assert(pe_lang_bundle_parse(&pack, bad, zip_size) == PE_LANG_OPEN_BAD_ZIP);
        /* So does a zip cut short. */
        assert(pe_lang_bundle_parse(&pack, zip, zip_size - 10) == PE_LANG_OPEN_BAD_ZIP);
        free(bad);
        mz_free(zip);
    }
    {
        /* No pack in it, or under another case or in a folder: refused. */
        Part wrong[] = {{"TEXT.BIN", pack_bytes, size}, {"es/text.bin", pack_bytes, size}};
        size_t zip_size;
        void *zip = make_zip(wrong, 2, &zip_size);
        assert(pe_lang_bundle_parse(&pack, zip, zip_size) == PE_LANG_OPEN_BAD_ZIP);
        mz_free(zip);
    }
    {
        /* Pictures come with the text; a file with damaged pictures is refused whole. */
        static const Piece one[] = {{10, 100, "EXIT", "SALI"}};
        size_t pictures_size, zip_size;
        unsigned char *pictures_bin = make_pictures(one, 1, &pictures_size);
        Part parts[] = {{PE_LANG_BUNDLE_TEXT, pack_bytes, size}, {PE_LANG_BUNDLE_PICTURES, pictures_bin, pictures_size}};
        void *zip = make_zip(parts, 2, &zip_size);
        assert(pe_lang_bundle_parse(&pack, zip, zip_size) == PE_LANG_OPEN_OK);
        expect_item(&pack);
        assert(pack.pictures.count == 1);
        pe_lang_pack_free(&pack);
        mz_free(zip);
        pictures_bin[4] = 9;
        zip = make_zip(parts, 2, &zip_size);
        assert(pe_lang_bundle_parse(&pack, zip, zip_size) == PE_LANG_OPEN_BAD_PICTURES && !pack.data);
        mz_free(zip);
        free(pictures_bin);
    }
    {
        /* A zip whose pack is not a pack. */
        Part junk[] = {{PE_LANG_BUNDLE_TEXT, "not a pack at all", 17}};
        size_t zip_size;
        void *zip = make_zip(junk, 1, &zip_size);
        assert(pe_lang_bundle_parse(&pack, zip, zip_size) == PE_LANG_OPEN_BAD_PACK);
        mz_free(zip);
    }
    puts("Passed plain and zipped .pelang files, and damaged zips refused.");
#else
    assert(pe_lang_bundle_parse(&pack, (const unsigned char *)"PK\3\4rest", 8) == PE_LANG_OPEN_NO_ZIP);
    puts("Passed plain .pelang files; zipped ones are refused in this build.");
#endif
    free(pack_bytes);
    return 0;
}
