/* Native font checks: Latin composition matches Python pixel for pixel;
 * CJK cache pressure preserves queued glyphs, native cells and font borders.
 * All bitmaps are synthetic; no game art or third-party font is required. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pe_lang_font.h"
#include "pe_lang_cjk.h"
#include "pe_lang_font_vectors.h"
#include "pe_lang_letter_vectors.h"

/* latin.synthetic_cell, the stand-in game letter (no game art in tests). */
static void synthetic_cell(unsigned code, unsigned char *out) {
    unsigned char s[144] = {0};
    int top = code >= 0x10 && code <= 0x29 ? 1 : 3, stem = 1 + (int)code % 3, right = 5 + (int)code % 2;
    for (int y = top; y < 9; ++y) {
        s[y * 12 + stem] = 1;
        if ((y + (int)code) % 2) s[y * 12 + right] = 1;
    }
    for (int x = stem; x <= right; ++x) s[(top + 8) / 2 * 12 + x] = 1;
    if (code == 0x38) s[1 * 12 + stem] = 1;
    memcpy(out, s, 144);
    for (int i = 0; i < 144; ++i) {
        int x = i % 12, y = i / 12;
        if (s[i] == 1 && x + 1 < 12 && y + 1 < 12 && out[(y + 1) * 12 + x + 1] == 0) out[(y + 1) * 12 + x + 1] = 2;
    }
}

static void put(unsigned char *page, unsigned code, const unsigned char *cell) {
    unsigned cx = code % 21 * 12, cy = code / 21 * 12;
    for (unsigned i = 0; i < 144; ++i) {
        unsigned char *b = &page[(cy + i / 12) * 128 + (cx + i % 12) / 2];
        unsigned v = cell[i];
        *b = (cx + i % 12) & 1 ? (unsigned char)((*b & 0x0F) | v << 4) : (unsigned char)((*b & 0xF0) | v);
    }
}

static void get(const unsigned char *page, unsigned code, unsigned char *cell) {
    unsigned cx = code % 21 * 12, cy = code / 21 * 12;
    for (unsigned i = 0; i < 144; ++i) {
        unsigned char b = page[(cy + i / 12) * 128 + (cx + i % 12) / 2];
        cell[i] = (cx + i % 12) & 1 ? b >> 4 : b & 15;
    }
}

enum { CJK_TEST_GLYPHS = PE_CJK_CACHE_SIZE * 3 + 2 };
typedef struct CjkIo {
    unsigned char page[128*256], original[128*256], writable[128*256];
    unsigned char dictionary[CJK_TEST_GLYPHS*76];
    unsigned uploads, syncs;
    int synchronized, fail_upload;
} CjkIo;
static void cache_sync(void *user) {
    CjkIo *io=user;io->synchronized=1;++io->syncs;
}
static int cache_upload(unsigned cell,const unsigned char *pixels,void *user) {
    CjkIo *io=user;
    assert(io->synchronized && cell>=250 && cell<441);
    if(io->fail_upload){io->fail_upload=0;return -1;}
    unsigned x=cell%21*6,y=cell/21*12;
    for(unsigned row=0;row<12;++row){
        for(unsigned b=0;b<6;++b)assert(io->writable[(y+row)*128+x+b]);
        memcpy(io->page+(y+row)*128+x,pixels+row*6,6);
    }
    ++io->uploads;return 0;
}
static void cache_pixels(const CjkIo *io,unsigned cell,unsigned id) {
    unsigned x=cell%21*6,y=cell/21*12;
    for(unsigned row=0;row<12;++row)
        assert(!memcmp(io->page+(y+row)*128+x,io->dictionary+id*76+4+row*6,6));
}
static void cache_native_pixels(const CjkIo *io) {
    for(unsigned i=0;i<sizeof io->page;++i)
        if(!io->writable[i])assert(io->page[i]==io->original[i]);
}
static void check_cjk_cache(void) {
    static CjkIo io;
    unsigned char filled[144];memset(filled,3,sizeof filled);
    for(unsigned cell=0;cell<240;++cell)put(io.original,cell,filled);
    /* Blank native rename-grid cells F0..F9 are reserved too. */
    put(io.original,300,filled); /* A nonempty candidate is never cache storage. */
    memcpy(io.page,io.original,sizeof io.page);
    for(unsigned cell=250;cell<441;++cell)if(cell!=300){
        unsigned x=cell%21*6,y=cell/21*12;
        for(unsigned row=0;row<12;++row)memset(io.writable+(y+row)*128+x,1,6);
    }
    for(unsigned id=0;id<CJK_TEST_GLYPHS;++id){
        unsigned char *pixels=io.dictionary+id*76+4;
        for(unsigned b=0;b<72;++b)pixels[b]=(unsigned char)(id*37+b*17);
        pixels[0]=(unsigned char)id;pixels[1]=(unsigned char)(id>>8);
    }
    PeLangPack pack={0};pack.data=io.dictionary;pack.size=sizeof io.dictionary;
    pack.cjk_count=CJK_TEST_GLYPHS;
    PeCjkCache cache;pe_cjk_reset(&cache,&pack,io.original);
    const unsigned capacity=PE_CJK_CACHE_SIZE-1;
    assert(cache.count==capacity);
    unsigned cells[PE_CJK_CACHE_SIZE],ids[PE_CJK_CACHE_SIZE];
    for(unsigned frame=0;frame<30;++frame){
        if(frame)pe_cjk_frame(&cache);
        io.synchronized=0;
        for(unsigned i=0;i<capacity;++i){
            ids[i]=(frame*97+(i*17)%capacity)%CJK_TEST_GLYPHS;
            assert(pe_cjk_resolve(&cache,ids[i],&cells[i],cache_sync,cache_upload,&io));
            for(unsigned j=0;j<=i;++j)cache_pixels(&io,cells[j],ids[j]);
        }
        unsigned uploads=io.uploads,again=0;
        assert(pe_cjk_resolve(&cache,ids[0],&again,cache_sync,cache_upload,&io));
        assert(again==cells[0] && io.uploads==uploads);
        static unsigned char before[128*256];memcpy(before,io.page,sizeof before);
        unsigned missing=(frame*97+capacity)%CJK_TEST_GLYPHS;
        assert(!pe_cjk_resolve(&cache,missing,&again,cache_sync,cache_upload,&io));
        assert(io.uploads==uploads && !memcmp(before,io.page,sizeof before));
        cache_native_pixels(&io);
    }
    /* A failed upload must retain the old entry; pinning that old glyph must
     * also protect it from the subsequent successful retry. */
    pe_cjk_reset(&cache,&pack,io.original);io.synchronized=0;
    for(unsigned id=0;id<capacity;++id)
        assert(pe_cjk_resolve(&cache,id,&cells[id],cache_sync,cache_upload,&io));
    pe_cjk_frame(&cache);io.synchronized=0;io.fail_upload=1;
    static unsigned char before_failure[128*256];memcpy(before_failure,io.page,sizeof before_failure);
    unsigned cell;
    assert(!pe_cjk_resolve(&cache,capacity,&cell,cache_sync,cache_upload,&io));
    assert(!memcmp(before_failure,io.page,sizeof io.page));
    assert(pe_cjk_resolve(&cache,0,&cell,cache_sync,cache_upload,&io));
    assert(cell==cells[0]);cache_pixels(&io,cell,0);
    unsigned retry;
    assert(pe_cjk_resolve(&cache,capacity,&retry,cache_sync,cache_upload,&io));
    assert(retry!=cell);cache_pixels(&io,cell,0);cache_pixels(&io,retry,capacity);
    cache_native_pixels(&io);
    /* Reusing a pack-local ID after a language change must upload the new
     * bitmap rather than return a stale cached cell from the previous pack. */
    io.dictionary[4]^=0xFF;
    pe_cjk_reset(&cache,&pack,io.original);io.synchronized=0;
    unsigned uploads=io.uploads;
    assert(pe_cjk_resolve(&cache,0,&cell,cache_sync,cache_upload,&io));
    assert(io.uploads==uploads+1);cache_pixels(&io,cell,0);
    /* Exercise all 191 cells, including the final row, when every candidate
     * is empty. Border pixels and blank native rename cells stay unchanged. */
    memset(filled,0,sizeof filled);put(io.original,300,filled);put(io.page,300,filled);
    for(unsigned row=0;row<12;++row)memset(io.writable+(300/21*12+row)*128+300%21*6,1,6);
    pe_cjk_reset(&cache,&pack,io.original);io.synchronized=0;
    assert(cache.count==PE_CJK_CACHE_SIZE);
    for(unsigned id=0;id<PE_CJK_CACHE_SIZE;++id){
        assert(pe_cjk_resolve(&cache,id,&cell,cache_sync,cache_upload,&io));
        cache_pixels(&io,cell,id);
    }
    assert(!pe_cjk_resolve(&cache,PE_CJK_CACHE_SIZE,&cell,cache_sync,cache_upload,&io));
    static unsigned char occupied[128*256];memset(occupied,0x11,sizeof occupied);
    pe_cjk_reset(&cache,&pack,occupied);
    assert(!pe_cjk_resolve(&cache,0,&cell,cache_sync,cache_upload,&io));
    pe_cjk_reset(&cache,NULL,io.original);
    assert(!pe_cjk_resolve(&cache,0,&cell,cache_sync,cache_upload,&io));
    pe_cjk_reset(&cache,&pack,NULL);
    assert(!pe_cjk_resolve(&cache,0,&cell,cache_sync,cache_upload,&io));
    cache_native_pixels(&io);
    printf("Passed CJK cache checks: %u dictionary glyphs, 30 full frames, 191-cell boundary, pinned overflow, failed upload/retry, pack reset, reserved native pixels.\n",CJK_TEST_GLYPHS);
}

static void from_hex(const char *hex, unsigned char *out, unsigned size) {
    for (unsigned i = 0; i < size; ++i) {
        unsigned value = 0;
        assert(sscanf(hex + i * 2, "%2x", &value) == 1);
        out[i] = (unsigned char)value;
    }
}

/* A pack's letters: the same pixels as letters.py, whatever a code held before. */
static void check_letters(void) {
    const unsigned count = sizeof kLetterVectors / sizeof *kLetterVectors;
    unsigned char record[80], base[144], out[144];
    assert(count > 200);
    for (unsigned k = 0; k < count; ++k) {
        from_hex(kLetterVectors[k][0], record, sizeof record);
        if (record[6] != 0xFF) synthetic_cell(record[6], base);
        pe_lang_letter_compose(record, base, out);
        for (unsigned i = 0; i < 144; ++i) {
            if (out[i] != kLetterVectors[k][1][i] - '0') {
                fprintf(stderr, "letter %u differs from letters.py at pixel %u (%u, %u): %u vs %c\n", k, i, i % 12,
                        i / 12, out[i], kLetterVectors[k][1][i]);
                exit(1);
            }
        }
    }

    /* On a page: over a cell that already has a letter, leaving the rest of the page alone. */
    static unsigned char page[128 * 256], before[128 * 256];
    unsigned char cell[144], two[160], expect[144];
    for (unsigned code = 0; code < 77; ++code) {
        synthetic_cell(code, cell);
        put(page, code, cell);
    }
    synthetic_cell(3, cell);
    put(page, 0xBC, cell);
    memcpy(before, page, sizeof page);
    from_hex(kLetterVectors[0][0], two, 80);
    from_hex(kLetterVectors[count - 1][0], two + 80, 80);
    two[4] = 0xBC, two[84] = 0xBD;
    assert(pe_lang_letters_fill(two, 2, page, 128, 256) == 2);
    get(page, 0xBC, cell);
    if (two[6] != 0xFF) synthetic_cell(two[6], base);
    pe_lang_letter_compose(two, base, expect);
    assert(!memcmp(cell, expect, 144));
    for (unsigned code = 0; code < 441; ++code) {
        unsigned char a[144], b[144];
        if (code == 0xBC || code == 0xBD) continue;
        get(page, code, a);
        get(before, code, b);
        assert(!memcmp(a, b, 144));
    }

    /* A letter on the game's own adds to that letter's widths; a drawn one has its own. */
    unsigned char advance[78] = {0}, metrics[78] = {0}, r[80] = {0};
    advance[0x34] = 7, metrics[0x34] = 0x72;
    r[6] = 0x34, r[5] = 1;
    assert(pe_lang_letter_advance(r, advance) == 8 && pe_lang_letter_metric(r, metrics) == 0x82);
    metrics[0x34] = 0xF2;
    assert(pe_lang_letter_metric(r, metrics) == 0xF2);
    r[6] = 0xFF, r[5] = 9;
    assert(pe_lang_letter_advance(r, advance) == 9 && pe_lang_letter_metric(r, metrics) == 0x90);
}

int main(void) {
    check_cjk_cache();
    check_letters();
    assert(sizeof kLatinVectors / sizeof *kLatinVectors == PE_LATIN_COUNT);
    for (unsigned k = 0; k < PE_LATIN_COUNT; ++k) {
        const PeLatinGlyph *g = &kPeLatinGlyphs[k];
        unsigned char base[144], out[144];
        if (g->base != 0xFF) synthetic_cell(g->base, base);
        pe_lang_font_compose(g, g->base != 0xFF ? base : NULL, out);
        for (unsigned i = 0; i < 144; ++i) {
            if (out[i] != kLatinVectors[k][i] - '0') {
                fprintf(stderr, "glyph %02X differs from latin.py at pixel %u (%u, %u): %u vs %c\n",
                        g->code, i, i % 12, i / 12, out[i], kLatinVectors[k][i]);
                return 1;
            }
        }
    }

    /* A page holding the base letters (codes 0-76) and one glyph already in a Latin cell. */
    static unsigned char page[128 * 256], before[128 * 256];
    unsigned char cell[144], other[144];
    for (unsigned code = 0; code < 77; ++code) {
        synthetic_cell(code, cell);
        put(page, code, cell);
    }
    synthetic_cell(0x10, other);
    put(page, kPeLatinGlyphs[5].code, other);
    memcpy(before, page, sizeof page);
    assert(pe_lang_font_fill(page, 128, 256) == PE_LATIN_COUNT - 1);
    get(page, kPeLatinGlyphs[5].code, cell);
    assert(!memcmp(cell, other, 144));                         /* never drawn over */
    for (unsigned k = 0; k < PE_LATIN_COUNT; ++k) {
        if (k == 5) continue;
        get(page, kPeLatinGlyphs[k].code, cell);
        for (unsigned i = 0; i < 144; ++i) assert(cell[i] == kLatinVectors[k][i] - '0');
    }
    for (unsigned code = 0; code < 441; ++code) {             /* every other cell as it was */
        int latin = 0;
        for (unsigned k = 0; k < PE_LATIN_COUNT; ++k) latin |= kPeLatinGlyphs[k].code == code;
        if (latin) continue;
        unsigned char a[144], b[144];
        get(page, code, a);
        get(before, code, b);
        assert(!memcmp(a, b, 144));
    }
    assert(pe_lang_font_fill(page, 128, 256) == 0);            /* a second upload adds nothing */

    /* Widths: a composed letter advances like its base; a drawn sign by its ink. */
    unsigned char advance[78], metrics[78];
    for (unsigned i = 0; i < 78; ++i) advance[i] = (unsigned char)(i % 9 + 3), metrics[i] = (unsigned char)(0x10 * (i % 12));
    for (unsigned k = 0; k < PE_LATIN_COUNT; ++k) {
        const PeLatinGlyph *g = &kPeLatinGlyphs[k];
        unsigned a = pe_lang_font_advance(g, advance);
        assert(g->base == 0xFF ? a >= 3 && a <= 12 : a == advance[g->base]);
        assert(g->base == 0xFF || pe_lang_font_metric(g, metrics) == metrics[g->base]);
    }
    printf("Passed Latin Extended checks: %d glyphs match latin.py, page fill writes only empty cells, widths.\n",
           PE_LATIN_COUNT);
    return 0;
}
