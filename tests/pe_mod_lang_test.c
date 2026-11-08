/* pe_lang's pack reader: lookups by key and by text table, and every damaged pack refused whole.
 * The packs are built here in the format localization/pelang.py writes. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pe_lang_pack.h"

typedef struct Entry { const char *key; const char *glyphs; } Entry;

static void wr32(unsigned char *p, uint32_t v) {
    p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24);
}

static int by_hash(const void *a, const void *b) {
    uint32_t x = pe_lang_hash(((const Entry *)a)->key), y = pe_lang_hash(((const Entry *)b)->key);
    return x < y ? -1 : x > y;
}

/* A pack of `n` entries (glyph strings without their FF), malloc'd; *size gets its length. */
static unsigned char *make_pack(Entry *entries, unsigned n, size_t *size) {
    size_t text = 64 + 8u * n, total = text;
    qsort(entries, n, sizeof *entries, by_hash);
    for (unsigned i = 0; i < n; ++i) total += strlen(entries[i].glyphs) + 1;
    unsigned char *p = calloc(1, total);
    memcpy(p, "PELG", 4); wr32(p + 4, 1); wr32(p + 8, n); wr32(p + 12, (uint32_t)text);
    memcpy(p + 16, "fr", 2); memcpy(p + 32, "French", 6);
    size_t at = 0;
    for (unsigned i = 0; i < n; ++i) {
        size_t len = strlen(entries[i].glyphs);
        wr32(p + 64 + 8 * i, pe_lang_hash(entries[i].key)); wr32(p + 68 + 8 * i, (uint32_t)at);
        memcpy(p + text + at, entries[i].glyphs, len); p[text + at + len] = 0xFF;
        at += len + 1;
    }
    *size = total;
    return p;
}

static void wr_record(unsigned char *r, uint32_t cp, unsigned code, unsigned advance, unsigned base) {
    memset(r, 0, 80);
    wr32(r, cp); r[4] = (unsigned char)code; r[5] = (unsigned char)advance; r[6] = (unsigned char)base;
}

/* The same pack as version 3, with `letters` records after its text. */
static unsigned char *with_letters(const unsigned char *v1, size_t size, const unsigned char *letters,
                                   unsigned count, size_t *out) {
    size_t total = size + 24 + count * 80u;
    unsigned char *p = calloc(1, total);
    memcpy(p, v1, 64);
    memcpy(p + 88, v1 + 64, size - 64);
    memcpy(p + 24 + size, letters, count * 80u);
    wr32(p + 4, 3); wr32(p + 12, (uint32_t)(v1[12] | v1[13] << 8) + 24);
    wr32(p + 64, (uint32_t)size + 24); wr32(p + 68, 0); wr32(p + 72, 76); wr32(p + 76, 0);
    wr32(p + 80, (uint32_t)size + 24); wr32(p + 84, count);
    *out = total;
    return p;
}

static unsigned char *copy_of(const unsigned char *p, size_t size) {
    unsigned char *c = malloc(size); memcpy(c, p, size); return c;
}

int main(int argc, char **argv) {
    /* Exercise the Rust editor's real output with the game's independent C reader.
     * The editor round-trip demo contains two translated pages and their bank map. */
    if (argc == 3 && !strcmp(argv[1], "--editor-demo")) {
        PeLangPack demo = {0};
        assert(!pe_lang_pack_load(&demo, argv[2]));
        assert(demo.count == 3 && demo.cjk_count == 21);
        assert(!strcmp(demo.lang, "zh-TW"));
        const unsigned char *first = pe_lang_pack_find(&demo, "dlg/M001/009");
        assert(first && first[0] == 0xF1);
        assert(pe_lang_pack_find(&demo, "dlg/M001/014"));
        assert(!pe_lang_pack_find(&demo, "dlg/M001/010"));
        pe_lang_pack_free(&demo);
        puts("Passed Rust editor demo compatibility with the original C pack reader.");
        return 0;
    }
    assert(argc == 1);
    /* The same FNV-1a 32 as pelang.py (localization/tests/test_pelang.py checks these too). */
    assert(pe_lang_hash("") == 0x811C9DC5u && pe_lang_hash("a") == 0xE40C292Cu);

    /* A room's message bank as it sits in memory: pages 0 and 1, then other room data. */
    static const unsigned char bank[] = {0xFE, 0x00, 0x10, 0x11, 0xFF, 0xFE, 0x01, 0xFB, 0x09, 0x02, 0x12, 0xF9,
                                         0x00, 0x00, 0xFE};
    assert(pe_lang_bank_size(bank, sizeof bank) == 12);
    assert(!pe_lang_bank_size(bank + 1, sizeof bank - 1) && !pe_lang_bank_size(bank, 8));  /* no page / cut */
    assert(pe_lang_crc32((const unsigned char *)"123456789", 9) == 0xCBF43926u);  /* the zlib check value */
    char bank_key[32];
    snprintf(bank_key, sizeof bank_key, "bank/%08x", (unsigned)pe_lang_crc32(bank, 12));

    Entry entries[] = {{"item/054", "\x12\x37\x48"}, {"desc/000", "\x11\x30"}, {"dlg/M001/002", "\x13"},
                       {"sys/001", "\x14"}, {"misc/130", "\x15"}, {bank_key, "M001"}};
    size_t size;
    {
        /* A pack's own letters: one drawn, one on the game's letter; fixed codes stay as they were. */
        Entry small[] = {{"sys/001", "\x60\xE0\x10"}};
        size_t small_size, v3_size;
        unsigned char *v1 = make_pack(small, 1, &small_size), letters[160];
        PeLangPack pack;
        wr_record(letters, 0x3B1, 0x60, 8, 0xFF); letters[8] = 0x21;
        wr_record(letters + 80, 0x103, 0xE0, 0, 0x30); letters[88] = 1; letters[90] = 3;
        assert(!pe_lang_pack_parse(&pack, with_letters(v1, small_size, letters, 2, &v3_size), v3_size));
        assert(pack.letter_count == 2 && pack.unicode && pack.count == 1);
        assert(pack.unicode[0x60] == 0x3B1 && pack.unicode[0xE0] == 0x103 && pack.unicode[0x10] == 'A');
        assert(pe_lang_pack_has_letter(&pack, 0xE0) && !pe_lang_pack_has_letter(&pack, 0x61));
        assert(pe_lang_pack_letter(&pack, 1)[4] == 0xE0);
        assert(!memcmp(pe_lang_pack_find(&pack, "sys/001"), "\x60\xE0\x10\xFF", 4));
        pe_lang_pack_free(&pack);

        /* Each of these is refused whole. */
        struct { unsigned at, value; } damage[] = {
            {4, 0x20},            /* a code among the game's own letters */
            {4, 0xF0},            /* past the codes a letter may take */
            {8, 3},               /* a pixel that is no colour */
            {7, 1},               /* ops on a drawn letter */
            {5, 0},               /* a drawn letter with no width */
            {80 + 6, 0x4D},       /* a base letter that isn't the game's */
            {80 + 7, 8},          /* an unknown step */
            {80 + 8, 9},          /* more points than a mark holds */
            {80 + 9, 2},          /* rows that are neither fixed nor above */
            {80 + 10, 4},         /* an unknown anchor */
            {80 + 4, 0x60},       /* a code twice */
        };
        for (unsigned i = 0; i < sizeof damage / sizeof *damage; ++i) {
            unsigned char bad[160];
            memcpy(bad, letters, sizeof bad);
            bad[damage[i].at] = (unsigned char)damage[i].value;
            assert(pe_lang_pack_parse(&pack, with_letters(v1, small_size, bad, 2, &v3_size), v3_size));
        }
        unsigned char *cut = with_letters(v1, small_size, letters, 2, &v3_size);
        assert(pe_lang_pack_parse(&pack, cut, v3_size - 1));                 /* a record cut short */
        cut = with_letters(v1, small_size, letters, 2, &v3_size);
        wr32(cut + 84, 3);
        assert(pe_lang_pack_parse(&pack, cut, v3_size));                     /* the count disagrees */
        cut = with_letters(v1, small_size, letters, 2, &v3_size);
        wr32(cut + 4, 4);
        assert(pe_lang_pack_parse(&pack, cut, v3_size));                     /* a version to come */
        free(v1);
    }
    {
        /* Battle messages (Text_LookupBattleMessage): table 20, "battle/NNN" by BATTLE_TEXT_*. */
        Entry battle[] = {{"battle/002", "\x22\x30"}, {"battle/010", "\x18\x3c"}, {"misc/003", "\x15"}};
        size_t battle_size;
        unsigned char *data = make_pack(battle, 3, &battle_size);
        PeLangPack pack;
        assert(!pe_lang_pack_parse(&pack, data, battle_size));
        assert(!memcmp(pe_lang_pack_table(&pack, 20, 2), "\x22\x30\xFF", 3));
        assert(pe_lang_pack_table(&pack, 20, 10)[0] == 0x18);
        assert(!pe_lang_pack_table(&pack, 20, 3) && !pe_lang_pack_table(&pack, 16, 2));
        assert(pe_lang_pack_table(&pack, 16, 3)[0] == 0x15 && !pe_lang_pack_table(&pack, 24, 2));
        pe_lang_pack_free(&pack);
    }
    unsigned char *good = make_pack(entries, 6, &size);
    PeLangPack pack;
    assert(!pe_lang_pack_parse(&pack, copy_of(good, size), size));
    assert(pack.count == 6 && !strcmp(pack.lang, "fr") && !strcmp(pack.name, "French"));
    const unsigned char *t = pe_lang_pack_find(&pack, "item/054");
    assert(t && t[0] == 0x12 && t[1] == 0x37 && t[2] == 0x48 && t[3] == 0xFF);
    assert(pe_lang_pack_table(&pack, 8, 54) == t);
    assert(pe_lang_pack_table(&pack, 12, 0)[0] == 0x11);
    assert(pe_lang_pack_table(&pack, 4, 1)[0] == 0x14 && pe_lang_pack_table(&pack, 16, 130)[0] == 0x15);
    assert(!pe_lang_pack_table(&pack, 8, 55) && !pe_lang_pack_table(&pack, 4, 54) && !pe_lang_pack_table(&pack, 20, 1));
    assert(pe_lang_pack_find(&pack, "dlg/M001/002")[0] == 0x13 && !pe_lang_pack_find(&pack, "dlg/M001/003"));
    /* Dialogue through the bank: the bank's CRC names it, then the page. */
    assert(pe_lang_pack_page(&pack, pe_lang_crc32(bank, 12), 2)[0] == 0x13);
    assert(!pe_lang_pack_page(&pack, pe_lang_crc32(bank, 12), 3));
    assert(!pe_lang_pack_page(&pack, 0x12345678u, 2));       /* a bank the pack doesn't know */
    assert(!pe_lang_pack_table(NULL, 8, 54) && !pe_lang_pack_page(NULL, 0, 2));
    pe_lang_pack_free(&pack);

    /* From a file, as the mod loads it. */
    FILE *f = fopen("pe_lang_test.pelang", "wb");
    assert(f && fwrite(good, 1, size, f) == size && !fclose(f));
    assert(!pe_lang_pack_load(&pack, "pe_lang_test.pelang") && pack.count == 6);
    pe_lang_pack_free(&pack);
    remove("pe_lang_test.pelang");
    assert(pe_lang_pack_load(&pack, "no-such-pack.pelang") == -1);

    /* Damaged packs are refused whole. */
    unsigned char *bad;
    bad = copy_of(good, size); bad[0] = 'X';                     assert(pe_lang_pack_parse(&pack, bad, size));
    bad = copy_of(good, size); wr32(bad + 4, 2);                 assert(pe_lang_pack_parse(&pack, bad, size));
    bad = copy_of(good, size); wr32(bad + 8, 1000000);           assert(pe_lang_pack_parse(&pack, bad, size));
    bad = copy_of(good, size); wr32(bad + 12, 8);                assert(pe_lang_pack_parse(&pack, bad, size));
    bad = copy_of(good, size); wr32(bad + 68, (uint32_t)size);   assert(pe_lang_pack_parse(&pack, bad, size));
    bad = copy_of(good, size); memcpy(bad + 72, bad + 64, 4);    assert(pe_lang_pack_parse(&pack, bad, size));  /* unsorted */
    bad = copy_of(good, size); bad[size - 1] = 0x20;             assert(pe_lang_pack_parse(&pack, bad, size));  /* no FF */
    assert(pe_lang_pack_parse(&pack, copy_of(good, 40), 40));
    assert(pack.data == NULL && !pe_lang_pack_find(&pack, "item/054"));
    /* Each entry is bounded by the next offset, not an FF in a later entry. */
    bad = copy_of(good, size); wr32(bad + 76, 0);             assert(pe_lang_pack_parse(&pack, bad, size));
    bad = copy_of(good, size); memset(bad + 16, 'x', 16);     assert(pe_lang_pack_parse(&pack, bad, size));
    const char *invalid[] = {"\xFB", "\xFB\x07", "\xFB\x09", "\xFB\x09\x08",
                             "\xFB\x0A", "\xFC", "\xFD", "\xF1", "\xF9\x10", "\xFF\x10"};
    for (unsigned i = 0; i < sizeof invalid / sizeof *invalid; ++i) {
        Entry one[] = {{"dlg/M001/000", invalid[i]}};
        size_t n;
        unsigned char *data = make_pack(one, 1, &n);
        assert(pe_lang_pack_parse(&pack, data, n));
    }
    Entry valid[] = {{"dlg/M001/000", "\x10\xFB\x07\xFF\xFB\x09\x02\xFA\xFB\x08\xF9"}};
    size_t n;
    unsigned char *data = make_pack(valid, 1, &n);
    assert(!pe_lang_pack_parse(&pack, data, n)); /* FF is a legal delay operand, not a terminator. */
    pe_lang_pack_free(&pack);
    free(good);
    puts("Passed language pack checks: lookups by key, table and bank page, bank measure and CRC, "
         "file load, damaged packs refused whole.");
    return 0;
}
