/* Stable language preference. Separate from packs and generic numeric F1 rows. */
#include "pe_lang_selection.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

int pe_lang_selection_find(const PeLangSelection *id, const PeLangPack *packs, unsigned count) {
    if (!id->lang[0]) return 0;
    for (unsigned i = 0; i < count; ++i)
        if (!strcmp(id->lang, packs[i].lang) && !strcmp(id->name, packs[i].name)) return (int)i + 1;
    return 0; /* Missing pack: game text; don't substitute another language. */
}

int pe_lang_selection_load(const char *path, PeLangSelection *id) {
    unsigned char record[52];
    memset(id, 0, sizeof *id);
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    int ok = fread(record, 1, sizeof record, f) == sizeof record && fgetc(f) == EOF &&
             !ferror(f) && !memcmp(record, "PLS1", 4) &&
             memchr(record + 4, 0, 16) && memchr(record + 20, 0, 32);
    fclose(f);
    if (!ok) return -1;
    memcpy(id->lang, record + 4, 16);
    memcpy(id->name, record + 20, 32);
    return 0;
}

int pe_lang_selection_save(const char *path, const PeLangSelection *id) {
    char temporary[4096];
    unsigned char record[52] = {'P', 'L', 'S', '1'};
    if (!memchr(id->lang, 0, sizeof id->lang) || !memchr(id->name, 0, sizeof id->name)) return -1;
    memcpy(record + 4, id->lang, 16);
    memcpy(record + 20, id->name, 32);
    int len = snprintf(temporary, sizeof temporary, "%s.%llu.tmp", path, (unsigned long long)SDL_GetTicksNS());
    if (len < 0 || len >= (int)sizeof temporary) return -1;
    SDL_IOStream *f = SDL_IOFromFile(temporary, "wbx");
    if (!f) return -1;
    int ok = SDL_WriteIO(f, record, sizeof record) == sizeof record && SDL_FlushIO(f);
    if (!SDL_CloseIO(f)) ok = 0;
    if (!ok || !SDL_RenamePath(temporary, path)) {
        SDL_RemovePath(temporary);
        return -1;
    }
    return 0;
}
