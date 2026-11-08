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
    /* Files written by `pelang.py bundle` (or anyone): open each and say what it holds. */
    if (argc > 1) {
        int failed = 0;
        for (int i = 1; i < argc; ++i) {
            PeLangPack pack;
            PeLangOpen result = pe_lang_bundle_load(&pack, argv[i]);
            if (result == PE_LANG_OPEN_OK) printf("%s: %s (%s), %u entries\n", argv[i], pack.name, pack.lang,
                                                  pack.count);
            else printf("%s: refused, %s\n", argv[i], pe_lang_bundle_error(result));
            failed |= result != PE_LANG_OPEN_OK;
            pe_lang_pack_free(&pack);
        }
        return failed;
    }
    unsigned char *pack_bytes = make_pack(&size);
    PeLangPack pack;

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
