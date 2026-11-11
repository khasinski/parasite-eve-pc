/* Stable identities and actual preference round trips, without game data. */
#include "pe_lang_selection.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    const char *path = "pe_lang_selection_test.dat";
    PeLangPack packs[3] = {{.lang = "fr", .name = "French"},
                          {.lang = "de", .name = "German"}, {.lang = "fr", .name = "French revised"}};
    PeLangSelection id = {.lang = "fr", .name = "French revised"}, read;
    assert(pe_lang_selection_find(&id, packs, 3) == 3);
    assert(!pe_lang_selection_save(path, &id));
    assert(!pe_lang_selection_load(path, &read));
    assert(!strcmp(read.lang, id.lang) && !strcmp(read.name, id.name));
    PeLangPack reorder[3] = {packs[2], packs[0], packs[1]};
    assert(pe_lang_selection_find(&read, reorder, 3) == 1);
    assert(!pe_lang_selection_find(&read, packs, 2)); /* missing edition never chooses French */
    assert(!pe_lang_selection_load(path, &read));    /* missing pack doesn't erase preference */
    assert(pe_lang_selection_find(&read, packs, 3) == 3);
    memset(&id, 0, sizeof id);
    assert(!pe_lang_selection_save(path, &id));       /* replaces existing file atomically */
    assert(!pe_lang_selection_load(path, &read));
    assert(!pe_lang_selection_find(&read, packs, 3)); /* explicit Game text */
    FILE *f = fopen(path, "wb");
    assert(f && fwrite("bad", 1, 3, f) == 3 && !fclose(f));
    assert(pe_lang_selection_load(path, &read));
    assert(!read.lang[0] && !read.name[0]);
    remove(path);
    assert(pe_lang_selection_load(path, &read));
    /* The first-start question: only with nothing saved, packs to offer and someone to answer. */
    assert(pe_lang_selection_ask(0, 5, 1));
    assert(pe_lang_selection_ask(0, 1, 1));           /* one pack is enough: it or English */
    assert(!pe_lang_selection_ask(1, 5, 1));          /* answered once (English too): never again */
    assert(!pe_lang_selection_ask(0, 0, 1));          /* no packs: nothing to ask */
    assert(!pe_lang_selection_ask(0, 5, 0));          /* headless, or the language mod off */
    puts("Passed stable language identity, reorder, missing pack, preference storage and first-start question checks.");
    return 0;
}
