/* Call files: the calls players and modders can read, change and add to.
 *
 * The game's own calls are mods/pe_phone/calls/pe1.txt, built into the game (CMake turns it into
 * g_pe_calls_pe1_txt). At start the "pe_calls" folder beside the preferences is read:
 *   - the first time it doesn't exist, it is made with a copy of pe1.txt, to edit;
 *   - pe1.txt there replaces the built-in calls (delete it to go back to them);
 *   - every other *.txt there adds its calls (a key used twice is a mistake).
 * Files are read in name order. Mistakes go to pe_mods.log with the file and line; a call with a
 * mistake is left out, the rest load. F1 > Phone Calls > Reload calls reads them again. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "pe_mod_host.h"
#include "pe_phone.h"

#define PE_CALLS_DIR   "pe_calls"
#define PE_BUILTIN     "pe1.txt"
#define PE_FILE_LIMIT  (512u * 1024u)

extern const unsigned char g_pe_calls_pe1_txt[];   /* generated from calls/pe1.txt */
extern const unsigned g_pe_calls_pe1_txt_size;

static PePhoneStore s_store;
static int s_problems, s_files;
static char s_dir[1100];

static void pe_problem(void *user, const char *file, int line, const char *what) {
    (void)user;
    pe_mod_log(PE_PHONE_MOD_ID, "%s line %d: %s", file, line, what);
    ++s_problems;
}

static int pe_cmp(const void *a, const void *b) { return strcmp(*(const char *const *)a, *(const char *const *)b); }

static int pe_ends_txt(const char *s) {
    const size_t n = strlen(s);
    return n > 4 && SDL_strcasecmp(s + n - 4, ".txt") == 0;
}

static void pe_write_builtin(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fwrite(g_pe_calls_pe1_txt, 1, g_pe_calls_pe1_txt_size, f);
    fclose(f);
    pe_mod_log(PE_PHONE_MOD_ID, "wrote %s: the game's calls, to edit", path);
}

static void pe_read_file(const char *path, const char *name) {
    size_t size = 0;
    char *data = SDL_LoadFile(path, &size);
    if (!data) { pe_mod_log(PE_PHONE_MOD_ID, "%s: can't read it", name); ++s_problems; return; }
    if (size > PE_FILE_LIMIT) { pe_mod_log(PE_PHONE_MOD_ID, "%s: over 512 KB, left out", name); ++s_problems; }
    else {
        const int n = pe_phone_parse_calls(&s_store, name, data, (unsigned)size, pe_problem, NULL);
        pe_mod_log(PE_PHONE_MOD_ID, "%s: %d calls", name, n);
        ++s_files;
    }
    SDL_free(data);
}

int pe_phone_packs_load(void) {
    char path[1300];
    char **list = NULL;
    int count = 0, i, own_pe1 = 0, added = 0;
    SDL_PathInfo info;
    pe_phone_calls_drop_loaded();
    pe_phone_store_clear(&s_store);
    s_problems = s_files = 0;
    if (pe_mod_path(s_dir, sizeof s_dir, PE_CALLS_DIR) == 0) {
        if (!SDL_GetPathInfo(s_dir, &info)) {                       /* first time: a copy to edit */
            if (SDL_CreateDirectory(s_dir)) {
                snprintf(path, sizeof path, "%s/%s", s_dir, PE_BUILTIN);
                pe_write_builtin(path);
            }
        }
        list = SDL_GlobDirectory(s_dir, "*", 0, &count);
    } else {
        s_dir[0] = 0;
    }
    if (list) qsort(list, (size_t)count, sizeof *list, pe_cmp);    /* the same order on every system */
    for (i = 0; list && i < count; ++i)
        if (!SDL_strcasecmp(list[i], PE_BUILTIN)) own_pe1 = 1;
    if (!own_pe1)
        (void)pe_phone_parse_calls(&s_store, "pe1.txt (built in)", (const char *)g_pe_calls_pe1_txt,
                                   g_pe_calls_pe1_txt_size, pe_problem, NULL);
    for (i = 0; list && i < count; ++i) {
        if (list[i][0] == '.' || !pe_ends_txt(list[i])) continue;
        snprintf(path, sizeof path, "%s/%s", s_dir, list[i]);
        if (SDL_GetPathInfo(path, &info) && info.type == SDL_PATHTYPE_FILE) pe_read_file(path, list[i]);
    }
    SDL_free(list);
    for (i = 0; i < s_store.call_count; ++i)
        if (pe_phone_calls_add_loaded(&s_store.calls[i]) == 0) ++added;
        else ++s_problems;
    pe_mod_log(PE_PHONE_MOD_ID, "calls: %d loaded from %s%d file(s), %d problem(s)", added,
               own_pe1 ? "" : "the built-in pe1.txt and ", s_files, s_problems);
    return s_problems;
}

int pe_phone_packs_problems(void) { return s_problems; }

int pe_phone_packs_open_folder(void) {
    char url[1400];
    unsigned i, n = 0;
    SDL_PathInfo info;
    if (!s_dir[0]) return 0;
    if (!SDL_GetPathInfo(s_dir, &info)) (void)SDL_CreateDirectory(s_dir);
    n = (unsigned)snprintf(url, sizeof url, "file:///");
    for (i = 0; s_dir[i] && n + 4 < sizeof url; ++i) {
        const char c = s_dir[i];
        if (c == '\\') url[n++] = '/';
        else if (c == ' ') { memcpy(url + n, "%20", 3); n += 3; }
        else url[n++] = c;
    }
    url[n] = 0;
    return SDL_OpenURL(url);
}
