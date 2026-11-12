/* Names kept with the save (mods/pe_lang/pe_lang_names.c): a name in the pack's letters and back,
 * plain English, the windows' byte room kept, and finding the pack that wrote a saved name. */
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "pe_lang_names.h"

static void wr32(unsigned char *p, uint32_t v) {
    p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24);
}

int main(void) {
    /* a pack whose name letters are あ (FB 01) and é (FB 02) */
    unsigned char data[8];
    PeLangPack pack;
    memset(&pack, 0, sizeof pack);
    wr32(data, 0x3042); wr32(data + 4, 0xE9);
    pack.data = data; pack.size = sizeof data; pack.names_offset = 0; pack.name_count = 2;

    const uint32_t name[] = {'A', 0x3042, 0xE9, 'a'};
    unsigned char bytes[17];
    uint32_t back[8];
    /* in the pack's letters: English one byte, others two */
    assert(pe_lang_name_encode(&pack, name, 4, bytes, 12) == 6);
    const unsigned char want[] = {0x10, 0xFB, 0x01, 0xFB, 0x02, 0x30, 0xFF};
    assert(!memcmp(bytes, want, sizeof want));
    assert(pe_lang_name_decode(&pack, bytes, 12, back) == 4 && !memcmp(back, name, sizeof name));
    /* the save's own copy: plain English, '?' for the rest */
    assert(pe_lang_name_encode(NULL, name, 4, bytes, 12) == 4);
    const unsigned char plain[] = {0x10, 0x2C, 0x2C, 0x30, 0xFF};
    assert(!memcmp(bytes, plain, sizeof plain));
    /* a pack that lacks a letter can't read the name back */
    pack.name_count = 1;
    assert(pe_lang_name_encode(&pack, name, 4, bytes, 12) == 5);   /* あ, then é as '?' */
    assert(pe_lang_name_decode(&pack, want, 12, back) == -1);
    pack.name_count = 2;
    /* whole letters only, within the window's room (the save name holds 7 bytes) */
    const uint32_t long_name[] = {0x3042, 0x3042, 0x3042, 0x3042, 'b'};
    assert(pe_lang_name_encode(&pack, long_name, 5, bytes, 7) == 6 && bytes[6] == 0xFF);  /* stops at the 4th */
    const uint32_t fits[] = {0x3042, 0x3042, 0x3042, 'b'};
    assert(pe_lang_name_encode(&pack, fits, 4, bytes, 7) == 7 && bytes[6] == 0x31 && bytes[7] == 0xFF);
    /* never more than 8 letters; a byte that is no letter is refused */
    const unsigned char nine[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 0xFF};
    assert(pe_lang_name_decode(&pack, nine, 15, back) == -1);
    const unsigned char command[] = {0x10, 0xF7, 0xFF};
    assert(pe_lang_name_decode(&pack, command, 15, back) == -1);
    assert(pe_lang_name_same(want, want, 12) && !pe_lang_name_same(want, plain, 12));
    /* finding the pack that knows a saved name's letters: the one in use first, then in order */
    unsigned char other_data[8];
    PeLangPack packs[3];
    unsigned count = 0;
    memset(packs, 0, sizeof packs);
    wr32(other_data, 0x3B1); wr32(other_data + 4, 0x3B2);     /* α (FB 01), β (FB 02) */
    packs[0] = pack;                                           /* あ, é */
    packs[1].data = other_data; packs[1].size = 8; packs[1].name_count = 2;
    packs[2].data = other_data; packs[2].size = 8; packs[2].name_count = 1;   /* α only */
    const unsigned char saved[] = {0x10, 0xFB, 0x02, 0xFF};    /* "A" then letter 2 */
    assert(pe_lang_name_paged(saved, 12) && !pe_lang_name_paged(plain, 12));
    assert(pe_lang_name_find_pack(packs, 3, 1, saved, 12, back, &count) == 1 && count == 2 && back[1] == 0x3B2);
    assert(pe_lang_name_find_pack(packs, 3, -1, saved, 12, back, &count) == 0 && back[1] == 0xE9);
    assert(pe_lang_name_find_pack(packs, 3, 2, saved, 12, back, &count) == 0);   /* 2 lacks β: the next */
    assert(pe_lang_name_find_pack(packs, 1, 0, plain, 12, back, &count) == -1);  /* no two-byte letter */
    const unsigned char unknown[] = {0xFB, 0x05, 0xFF};
    assert(pe_lang_name_find_pack(packs, 3, 0, unknown, 12, back, &count) == -1);
    /* a pack fits a name when it has every letter within the room */
    const uint32_t greek[] = {'A', 0x3B1, 0x3B2};
    assert(pe_lang_name_fits(&packs[1], greek, 3, 12) && !pe_lang_name_fits(&packs[2], greek, 3, 12));
    assert(!pe_lang_name_fits(NULL, greek, 3, 12) && pe_lang_name_fits(NULL, name, 1, 12));
    assert(!pe_lang_name_fits(&packs[1], greek, 3, 4));       /* 5 bytes don't fit in 4 */
    /* saving: the pack in use keeps its own letters; one that lacks some gives way to one that has them */
    assert(pe_lang_name_save_pack(packs, 3, &packs[1], greek, 3, 12) == -1);
    assert(pe_lang_name_save_pack(packs, 3, &packs[2], greek, 3, 12) == 1);
    assert(pe_lang_name_save_pack(packs, 3, NULL, greek, 3, 12) == 1);
    assert(pe_lang_name_save_pack(packs, 3, NULL, name, 1, 12) == -1);         /* "A": the game's own */
    const uint32_t nobody[] = {0x5D0};                                         /* no pack has it */
    assert(pe_lang_name_save_pack(packs, 3, &packs[2], nobody, 1, 12) == -1);
    assert(pe_lang_name_save_pack(packs, 3, &packs[2], greek, 0, 12) == -1);
    /* shared numbers (petext/names.py): a Greek pack (Ά = 307, α = 346) and a Russian one (ж = 430)
     * list 0 for numbers they don't use. A name saved with Greek, loaded with Russian chosen: Russian
     * can't read it, so the Greek pack does, and Russian never puts its own letters on it */
    {
        static unsigned char greek_data[430 * 4], russian_data[430 * 4];
        PeLangPack two[2];
        memset(two, 0, sizeof two);
        wr32(greek_data + 306 * 4, 0x386); wr32(greek_data + 345 * 4, 0x3B1);
        wr32(russian_data + 429 * 4, 0x436);
        two[0].data = russian_data; two[0].size = sizeof russian_data; two[0].name_count = 430;
        two[1].data = greek_data; two[1].size = sizeof greek_data; two[1].name_count = 346;
        const unsigned char saved_greek[] = {0xFC, 307 - 250, 0xFC, 346 - 250, 0xFF};   /* Άα */
        assert(pe_lang_name_decode(&two[0], saved_greek, 12, back) == -1);
        assert(pe_lang_name_find_pack(two, 2, 0, saved_greek, 12, back, &count) == 1);
        assert(count == 2 && back[0] == 0x386 && back[1] == 0x3B1);
        /* and the Greek pack writes the same bytes back */
        assert(pe_lang_name_encode(&two[1], back, 2, bytes, 12) == 4 && pe_lang_name_same(bytes, saved_greek, 12));
        /* the Russian pack lacks them: '?' in its own letters, and the save gets the Greek pack's */
        assert(pe_lang_name_save_pack(two, 2, &two[0], back, 2, 12) == 1);
    }
    /* a name entry grid: cells on codes 4D-EF made empty; English letters, name letters (whose
     * second byte may be one of those codes) and control codes stay */
    {
        const unsigned char grid[] = {0x10, 0x4D, 0x0F, 0xFB, 0x4D, 0xEF, 0xF0, 0xF3, 0x4C, 0xFE, 0xA0, 0x80, 0xFF};
        const unsigned char empty[] = {0x10, 0x0F, 0x0F, 0xFB, 0x4D, 0x0F, 0xF0, 0xF3, 0x4C, 0xFE, 0xA0, 0x0F, 0xFF};
        unsigned char out[sizeof grid + 1];
        memset(out, 0xAA, sizeof out);
        assert(pe_lang_grid_blank(grid, out, sizeof grid) == 3);
        assert(!memcmp(out, empty, sizeof empty) && out[sizeof grid] == 0xAA);
        assert(pe_lang_grid_blank(grid, out, sizeof grid - 1) == -1);              /* no FF within room */
        assert(pe_lang_grid_blank(plain, out, 12) == 0 && pe_lang_name_same(out, plain, 12));
    }
    puts("Passed name checks: pack letters both ways, plain English, window room kept, saved name's pack found, "
         "grid cells no pack shares kept empty.");
    return 0;
}
