#if defined(PE_CRASH_REPORT_TEST)
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <SDL3/SDL.h>
#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pe1/crash_report.h"
#ifndef _WIN32
#include <sys/stat.h>
#else
#include <windows.h>
#endif
static int blocked_report;
static void crash_test_describe(char *out,size_t size) {
    if(blocked_report)for(;;)SDL_Delay(100);
    snprintf(out,size,"Test room snapshot marker\n");
}
static SDL_EnumerationResult crash_test_file(void *user,const char *directory,const char *name) {
    if(strncmp(name,"crash-",6) || !strstr(name,".txt") || strstr(name,".tmp"))return SDL_ENUM_CONTINUE;
    char path[4096];snprintf(path,sizeof(path),"%s/%s",directory,name);
    size_t size=0;char *data=SDL_LoadFile(path,&size);
    if(data && size) {
        int *counts=user;++counts[0];
        if(strstr(data,"Test room snapshot marker"))++counts[1];
        if(strstr(data,"emergency crash report") && strstr(data,"PC: 0x"))++counts[2];
    }
    fprintf(stderr,"Crash test file: %s (%zu bytes, readable=%d)\n",path,size,data!=NULL);
    SDL_free(data);return SDL_ENUM_CONTINUE;
}
int main(int argc,char **argv) {
    if(argc==3 && !strcmp(argv[1],"--child")) {
        blocked_report=!strcmp(argv[2],"blocked");
        CrashReport_Install(0,crash_test_describe);
        if(!strcmp(argv[2],"normal"))return 0;
        if(!strcmp(argv[2],"stdio")) {
#ifdef _WIN32
            _lock_file(stderr);
#else
            flockfile(stderr);
#endif
        }
#ifdef _WIN32
        if(!strcmp(argv[2],"segv")) {
            volatile int *bad=(volatile int *)(uintptr_t)1;*bad=1;
        }
#endif
        raise(!strcmp(argv[2],"segv")?SIGSEGV:SIGABRT);
        return 90;
    }
    assert(SDL_Init(0));
    const char *modes[]={"normal","abort","segv","stdio","blocked"};
    for(unsigned i=0;i<5;++i) {
        char directory[4096],logs[4096];
        snprintf(directory,sizeof(directory),"%spe-crash-test-%llu-%u",SDL_GetBasePath(),(unsigned long long)SDL_GetTicksNS(),i);
        assert(SDL_CreateDirectory(directory));
        char executable[4096];snprintf(executable,sizeof(executable),"%s/crash-test.exe",directory);
        assert(SDL_CopyFile(argv[0],executable));
#ifndef _WIN32
        assert(!chmod(executable,0700));
#endif
        const char *args[]={executable,"--child",modes[i],NULL};
        SDL_PropertiesID props=SDL_CreateProperties();
        SDL_SetPointerProperty(props,SDL_PROP_PROCESS_CREATE_ARGS_POINTER,(void *)args);
        SDL_SetStringProperty(props,SDL_PROP_PROCESS_CREATE_WORKING_DIRECTORY_STRING,directory);
        SDL_Process *child=SDL_CreateProcessWithProperties(props);SDL_DestroyProperties(props);assert(child);
        int code=0;Uint64 started=SDL_GetTicks();
        while(!SDL_WaitProcess(child,false,&code)) {
            if(SDL_GetTicks()-started>20000){SDL_KillProcess(child,true);assert(!"Crash reporter exceeded its deadline");}
            SDL_Delay(10);
        }
        SDL_DestroyProcess(child);
        assert(i?code!=0:code==0);
        int counts[3]={0};snprintf(logs,sizeof(logs),"%s/logs",directory);
        assert(SDL_EnumerateDirectory(logs,crash_test_file,counts));
        fprintf(stderr,"Crash test %s: exit=%d files=%d full=%d emergency=%d\n",modes[i],code,counts[0],counts[1],counts[2]);
        assert(counts[0]==(i?1:0));
        if(i && i!=4)assert(counts[1]==1);
        if(i==4)assert(counts[2]==1);
    }
    puts("Crash reporting: normal cleanup, abort, SIGSEGV, held stdio lock and blocked reporter fallback passed headlessly");
    return 0;
}
#else
#if defined(PE_MOD_SETTINGS_TEST)
#undef NDEBUG
#include <SDL3/SDL.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../mods/host/pe_mod_host.h"
static int save_fault, save_errors;
static const char *mod_test_directory;
static SDL_IOStream *ModTestOpen(const char *path, const char *mode) {
    if (save_fault == 1) { SDL_SetError("injected open failure"); return NULL; }
    return SDL_IOFromFile(path, mode);
}
static size_t ModTestWrite(SDL_IOStream *io, const void *data, size_t size) {
    return SDL_WriteIO(io, data, save_fault == 2 ? size / 2 : size);
}
static bool ModTestFlush(SDL_IOStream *io) {
    return save_fault != 3 && SDL_FlushIO(io);
}
static bool ModTestClose(SDL_IOStream *io) {
    bool closed = SDL_CloseIO(io);
    return save_fault != 4 && closed;
}
static bool ModTestRename(const char *old, const char *new_path) {
    return save_fault != 5 && SDL_RenamePath(old, new_path);
}
#define SDL_IOFromFile ModTestOpen
#define SDL_WriteIO ModTestWrite
#define SDL_FlushIO ModTestFlush
#define SDL_CloseIO ModTestClose
#define SDL_RenamePath ModTestRename
#include "../mods/host/pe_mod_menu.c"
#undef SDL_IOFromFile
#undef SDL_WriteIO
#undef SDL_FlushIO
#undef SDL_CloseIO
#undef SDL_RenamePath
int pe_mod_path(char *out, size_t capacity, const char *file) {
    int n = snprintf(out, capacity, "%s/%s", mod_test_directory, file);
    return n > 0 && (size_t)n < capacity ? 0 : -1;
}
void pe_mod_log(const char *id, const char *format, ...) {
    (void)id; (void)format; ++save_errors;
}
void pe_toast(const char *text) { (void)text; }
int pe_mod_active(const char *id) { (void)id; return 1; }
const char *pe_mod_current(void) { return NULL; }
void pe_mod_call_begin(const char *mod_id, const char *what, const char *detail) {
    (void)mod_id; (void)what; (void)detail;
}
void pe_mod_call_end(void) {}
int pe_hook_add(int id, PeHookFn fn, void *user, int priority, const char *mod) {
    (void)id; (void)fn; (void)user; (void)priority; (void)mod; return 0;
}
int main(int argc, char **argv) {
    assert(argc == 2); mod_test_directory = argv[1];
    assert(SDL_CreateDirectory(mod_test_directory));
    char path[4096]; assert(!pe_mod_path(path, sizeof(path), "pe_mods.ini"));
    const char *old = "# preserved settings\nOld/Option=7\n";
    assert(SDL_SaveFile(path, old, strlen(old)));
    pe_menu_load_settings();
    static int enabled = 1;
    assert(!pe_menu_register_toggle(NULL, "Test option", "", &enabled, NULL));
    for (save_fault = 1; save_fault <= 5; ++save_fault) {
        int before = save_errors;
        pe_menu_save_settings();
        assert(save_errors > before);
        size_t bytes; char *data = SDL_LoadFile(path, &bytes);
        assert(data && bytes == strlen(old) && !memcmp(data, old, bytes)); SDL_free(data);
        int count = -1;
        char **files = SDL_GlobDirectory(mod_test_directory, "*.tmp", 0, &count);
        assert(files && count == 0); SDL_free(files);
    }
    save_fault = 0; pe_menu_save_settings();
    size_t bytes; char *data = SDL_LoadFile(path, &bytes);
    assert(data && strstr(data, "Old/Option=7") && strstr(data, "Test option=1")); SDL_free(data);
    assert(SDL_RemovePath(path));
    save_fault = 2; pe_menu_save_settings();
    SDL_PathInfo info; assert(!SDL_GetPathInfo(path, &info));
    save_fault = 0; pe_menu_save_settings(); assert(SDL_GetPathInfo(path, &info));
    puts("PASS: atomic mod settings preserve previous bytes on open/write/flush/close/rename failures; first save and unknown settings retained");
    return 0;
}

#elif defined(PE_HOST_SETTINGS_TEST)
extern long SetRCnt(long spec, unsigned short target, long mode);
extern long GetRCnt(long spec);
#undef NDEBUG
#include <SDL3/SDL.h>
#include <assert.h>
#ifdef _WIN32
#include <direct.h>
#define TestChdir _chdir
#else
#include <unistd.h>
#define TestChdir chdir
#endif
#include <stdio.h>

static SDL_Keycode TestLayoutKey(SDL_Scancode scancode, SDL_Keymod modifiers, bool key_event);
static SDL_Keycode test_layout_key;
static bool TestPresent(SDL_Renderer *renderer);
static bool TestWait(SDL_Event *event, Sint32 timeout);
static bool test_card_rename_failure;
static bool TestRename(const char *from, const char *to);
static bool TestMessageBox(SDL_MessageBoxFlags flags, const char *title, const char *message, SDL_Window *window);

#define SDL_RenderPresent TestPresent
#define SDL_WaitEventTimeout TestWait
#define SDL_RenamePath TestRename
#define SDL_ShowSimpleMessageBox TestMessageBox
#define SDL_GetKeyFromScancode TestLayoutKey
#define main original_game_main
#include "../src/main/boot/main.c"
#undef main
#undef SDL_GetKeyFromScancode
#undef SDL_RenderPresent
#undef SDL_WaitEventTimeout
#undef SDL_RenamePath
#undef SDL_ShowSimpleMessageBox
#include <kernel.h>
extern void *firstfile(const char *name, void *entry);
extern void *nextfile(void *entry);

static SDL_Keycode TestLayoutKey(SDL_Scancode scancode, SDL_Keymod modifiers, bool key_event) {
    return test_layout_key ? test_layout_key : SDL_GetKeyFromScancode(scancode, modifiers, key_event);
}

static void TestKeyboardLayoutLabels(void) {
    HostOverlayState state = {0};
    state.bind_device = HOST_BIND_KEYBOARD;
    int saved = host_options.keys[0];
    host_options.keys[0] = SDL_SCANCODE_Z;
    test_layout_key = SDLK_Y;
    assert(!strcmp(Host_ControlBindingValueName(&state, 0), "Y"));
    test_layout_key = SDLK_W;
    assert(!strcmp(Host_ControlBindingValueName(&state, 0), "W"));
    /* A changed layout takes effect on the next paint, without rebinding. */
    test_layout_key = SDLK_Z;
    assert(!strcmp(Host_ControlBindingValueName(&state, 0), "Z"));
    test_layout_key = 0x0437; /* Cyrillic name is unsupported by this font. */
    assert(!strcmp(Host_ControlBindingValueName(&state, 0), "Z"));
    assert(host_options.keys[0] == SDL_SCANCODE_Z);
    host_options.keys[0] = saved;
    test_layout_key = 0;
    puts("PASS: active keyboard layout labels, live changes and non-Latin fallback");
}

static const char *test_directory;

static void TestUserCardMigration(void) {
    char *cwd = SDL_GetCurrentDirectory();
    char root[4096], source[4096], target[4096], prefs[4096];
    snprintf(root, sizeof(root), "%s/legacy-cards-%llx", test_directory, (unsigned long long)SDL_GetPerformanceCounter());
    assert(SDL_CreateDirectory(root));
    snprintf(source, sizeof(source), "%s/bu00", root); assert(SDL_CreateDirectory(source));
    snprintf(source, sizeof(source), "%s/bu10", root); assert(SDL_CreateDirectory(source));
    const char *names[] = {"BASLUS-006620000000A", "BASLUS-006620000001B"};
    unsigned char data[8192]; memset(data, 0x37, sizeof(data));
    for (unsigned port = 0; port < 2; ++port) {
        snprintf(source, sizeof(source), "%s/bu%u0/%s", root, port, names[port]);
        assert(SDL_SaveFile(source, data, sizeof(data)));
    }
    snprintf(prefs, sizeof(prefs), "%s", host_options.user_file);
    /* Exercise UTF-8 and paths longer than the old 256-byte SDK buffers. */
    snprintf(host_options.user_file, sizeof(host_options.user_file),
        "%s/user-zażółć/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/"
        "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb/"
        "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc/preferences.toml", root);
    assert(!TestChdir(root));
    int prepared = Host_InstallCardPaths(NULL);
    if (!prepared) fprintf(stderr, "card import failed: %s\n", SDL_GetError());
    assert(prepared);
    for (unsigned port = 0; port < 2; ++port) {
        char device[64]; snprintf(device, sizeof(device), "bu%u0:%s", port, names[port]);
        assert(!Host_CardPath(target, device, sizeof(target)));
        assert(strlen(target) > 256);
        int fd = psyz_open(device, 1); assert(fd >= 0);
        unsigned char readback[8192];
        assert(psyz_read(fd, readback, sizeof(readback)) == sizeof(readback));
        assert(!psyz_close(fd)); assert(!memcmp(data, readback, sizeof(data)));
        char pattern[16]; snprintf(pattern, sizeof(pattern), "bu%u0:*", port);
        struct DIRENTRY entry;
        assert(firstfile(pattern, &entry)); assert(entry.size == 8192);
        while (nextfile(&entry)) {}
        /* A newer destination must win over a legacy file with the same name. */
        unsigned char newer[8192]; memset(newer, 0x59, sizeof(newer));
        assert(SDL_SaveFile(target, newer, sizeof(newer)));
        char directory[4096]; snprintf(directory, sizeof(directory), "%s/bu%u0", root, port);
        HostCardImport imported = {.ok = 1};
        assert(SDL_EnumerateDirectory(directory, Host_ImportCard, &imported) && imported.ok && imported.count == 0);
        assert(imported.kept == 1); SDL_free(imported.added);
        size_t size; void *contents = SDL_LoadFile(target, &size);
        assert(contents && size == sizeof(newer) && !memcmp(contents, newer, size)); SDL_free(contents);
        /* Source remains intact. A deleted imported save must stay deleted. */
        snprintf(source, sizeof(source), "%s/bu%u0/%s", root, port, names[port]);
        SDL_PathInfo info; assert(SDL_GetPathInfo(source, &info) && info.size == 8192);
        assert(SDL_RemovePath(target));
        assert(Host_InstallCardPaths(NULL));
        assert(!SDL_GetPathInfo(target, &info));
    }
    char marker[4200];
    assert(!Host_CardPath(target, "bu00:*", sizeof(target)));
    snprintf(marker, sizeof(marker), "%.*s.legacy-imported", (int)strlen(target) - 5, target);
    assert(SDL_RemovePath(marker));
    test_card_rename_failure = true;
    assert(!Host_InstallCardPaths(NULL));
    SDL_PathInfo marker_info; assert(!SDL_GetPathInfo(marker, &marker_info));
    for (unsigned port = 0; port < 2; ++port) {
        char device[64]; snprintf(device, sizeof(device), "bu%u0:%s", port, names[port]);
        assert(!Host_CardPath(target, device, sizeof(target)));
        assert(!SDL_GetPathInfo(target, &marker_info));
    }
    test_card_rename_failure = false;
    assert(Host_InstallCardPaths(NULL));
    assert(SDL_GetPathInfo(marker, &marker_info));
    assert(!TestChdir(cwd)); SDL_free(cwd);
    int same_card = psyz_open("bu00:BASLUS-006620000000A", 1);
    assert(same_card >= 0); assert(!psyz_close(same_card));
    snprintf(host_options.user_file, sizeof(host_options.user_file), "%s", prefs);
    Psyz_AdjustPathCB(NULL);
    puts("PASS: both legacy cards imported once, originals retained, UTF-8/long user paths read and enumerated");
}

static void TestSaveFixture(const char *path, unsigned char fill) {
    unsigned char data[8192]; memset(data, fill, sizeof(data));
    assert(SDL_SaveFile(path, data, sizeof(data)));
}

static int TestFileHasFill(const char *path, unsigned char fill) {
    size_t size; unsigned char *contents = SDL_LoadFile(path, &size);
    int same = contents && size == 8192 && contents[0] == fill && contents[8191] == fill;
    SDL_free(contents);
    return same;
}

/* Prepare a launch folder with an empty legacy bu00 and its own user folder. */
static void TestCardRoot(char *root, size_t size, const char *tag) {
    snprintf(root, size, "%s/%s-%llx", test_directory, tag, (unsigned long long)SDL_GetPerformanceCounter());
    assert(SDL_CreateDirectory(root));
    char legacy[4200]; snprintf(legacy, sizeof(legacy), "%s/bu00", root); assert(SDL_CreateDirectory(legacy));
    snprintf(host_options.user_file, sizeof(host_options.user_file), "%s/user/preferences.toml", root);
    assert(!TestChdir(root));
}

static void TestLateCardImport(void) {
    char *cwd = SDL_GetCurrentDirectory();
    char prefs[4096], root[4096], legacy[4200], current[4096], marker[4200];
    snprintf(prefs, sizeof(prefs), "%s", host_options.user_file);
    TestCardRoot(root, sizeof(root), "late-cards");
    /* First launch: nothing to import, but the marker is written. */
    assert(Host_InstallCardPaths(NULL));
    assert(!Host_CardPath(current, "bu00:*", sizeof(current)));
    snprintf(marker, sizeof(marker), "%.*s.legacy-imported", (int)strlen(current) - 5, current);
    SDL_PathInfo info; assert(SDL_GetPathInfo(marker, &info) && info.size > 0);
    /* A save copied beside the game after that launch is imported at the next one. */
    snprintf(legacy, sizeof(legacy), "%s/bu00/BASLUS-006620000000A", root);
    TestSaveFixture(legacy, 0x41);
    assert(Host_InstallCardPaths(NULL));
    assert(!Host_CardPath(current, "bu00:BASLUS-006620000000A", sizeof(current)));
    assert(TestFileHasFill(current, 0x41) && TestFileHasFill(legacy, 0x41));
    /* Deleted from the current card, it stays deleted. */
    assert(SDL_RemovePath(current));
    assert(Host_InstallCardPaths(NULL));
    assert(!SDL_GetPathInfo(current, &info));
    /* A legacy save never replaces a current save of the same name. */
    assert(!Host_CardPath(current, "bu00:BASLUS-006620000001B", sizeof(current)));
    TestSaveFixture(current, 0x62);
    snprintf(legacy, sizeof(legacy), "%s/bu00/BASLUS-006620000001B", root);
    TestSaveFixture(legacy, 0x42);
    assert(Host_InstallCardPaths(NULL));
    assert(TestFileHasFill(current, 0x62) && TestFileHasFill(legacy, 0x42));
    /* A whole card image beside the game is reported, not copied. */
    snprintf(legacy, sizeof(legacy), "%s/bu00/card.srm", root);
    assert(SDL_SaveFile(legacy, "x", 1));
    assert(Host_InstallCardPaths(NULL));
    assert(!Host_CardPath(current, "bu00:card.srm", sizeof(current)) && !SDL_GetPathInfo(current, &info));
    size_t size; char *list = SDL_LoadFile(marker, &size);
    assert(list && strstr(list, "bu00/BASLUS-006620000000A\n") && strstr(list, "bu00/BASLUS-006620000001B\n"));
    assert(!strstr(list, "card.srm")); SDL_free(list);
    assert(!TestChdir(cwd)); SDL_free(cwd);
    snprintf(host_options.user_file, sizeof(host_options.user_file), "%s", prefs);
    Psyz_AdjustPathCB(NULL);
    puts("PASS: saves copied after the first launch are imported; deleted ones stay deleted; current saves win");
}

static void TestOldCardMarker(void) {
    char *cwd = SDL_GetCurrentDirectory();
    char prefs[4096], root[4096], legacy[4200], current[4096], marker[4200];
    snprintf(prefs, sizeof(prefs), "%s", host_options.user_file);
    TestCardRoot(root, sizeof(root), "old-marker");
    /* An empty 0.7 marker, written after one legacy save arrived and before another. */
    snprintf(legacy, sizeof(legacy), "%s/bu00/BASLUS-006620000000A", root);
    TestSaveFixture(legacy, 0x41);
    SDL_Delay(50);
    assert(!Host_CardPath(current, "bu00:*", sizeof(current)));
    snprintf(marker, sizeof(marker), "%.*s.legacy-imported", (int)strlen(current) - 5, current);
    assert(SDL_SaveFile(marker, "", 0));
    SDL_Delay(50);
    snprintf(legacy, sizeof(legacy), "%s/bu00/BASLUS-006620000001B", root);
    TestSaveFixture(legacy, 0x42);
    assert(Host_InstallCardPaths(NULL));
    SDL_PathInfo info;
    assert(!Host_CardPath(current, "bu00:BASLUS-006620000000A", sizeof(current)) && !SDL_GetPathInfo(current, &info));
    assert(!Host_CardPath(current, "bu00:BASLUS-006620000001B", sizeof(current)) && TestFileHasFill(current, 0x42));
    /* The marker now lists both, so the next launch changes nothing. */
    size_t size; char *list = SDL_LoadFile(marker, &size);
    assert(list && !strncmp(list, HOST_CARD_MARKER_HEADER, strlen(HOST_CARD_MARKER_HEADER)));
    assert(strstr(list, "bu00/BASLUS-006620000000A\n") && strstr(list, "bu00/BASLUS-006620000001B\n"));
    SDL_free(list);
    assert(SDL_RemovePath(current));
    assert(Host_InstallCardPaths(NULL));
    assert(!SDL_GetPathInfo(current, &info));
    assert(!TestChdir(cwd)); SDL_free(cwd);
    snprintf(host_options.user_file, sizeof(host_options.user_file), "%s", prefs);
    Psyz_AdjustPathCB(NULL);
    puts("PASS: a 0.7 marker keeps earlier saves handled and imports saves that arrived after it");
}

static int presents;
static SDL_AtomicInt panel_ready;

static bool TestMessageBox(SDL_MessageBoxFlags flags, const char *title, const char *message, SDL_Window *window) {
    (void)flags;
    (void)window;
    fprintf(stderr, "host-settings: unexpected message box: %s: %s\n", title, message);
    assert(!"Unexpected message box in automated settings test");
    return false;
}

static bool TestRename(const char *from, const char *to) {
    if (test_card_rename_failure && strstr(from, "bu10")) return false;
    bool result = SDL_RenamePath(from, to);
    fprintf(stderr, "host-settings: preferences rename=%d error=%s\n", result, result ? "" : SDL_GetError());
    return result;
}

static bool TestWait(SDL_Event *event, Sint32 timeout) {
    static unsigned reported;
    bool result = SDL_WaitEventTimeout(event, timeout);
    if (result && reported++ < 32) {
        fprintf(
            stderr,
            "host-settings: event=%u scancode=%d repeat=%d\n",
            event->type,
            event->type == SDL_EVENT_KEY_DOWN ? event->key.scancode : 0,
            event->type == SDL_EVENT_KEY_DOWN ? event->key.repeat : 0
        );
    }
    return result;
}

static bool TestPresent(SDL_Renderer *renderer) {
    ++presents;
    if (presents == 1) {
        fprintf(stderr, "host-settings: first panel render\n");
    }
    assert(Psyz_AudioIsPaused());
    assert(Psyz_AudioWaitForTimer() == 0);
    if (presents == 1 || presents == 4 || presents == 7) {
        SDL_Surface *surface = SDL_RenderReadPixels(renderer, NULL);
        assert(surface);
        char path[4096];
        snprintf(path, sizeof(path), "%s/settings-%d.bmp", test_directory, presents);
        assert(SDL_SaveBMP(surface, path));
        SDL_DestroySurface(surface);
    }
    bool result = SDL_RenderPresent(renderer);
    if (result) {
        SDL_SetAtomicInt(&panel_ready, 1);
    }
    return result;
}

static void WaitPanel(void) {
    Uint64 deadline = SDL_GetTicks() + 5000;
    while (!SDL_GetAtomicInt(&panel_ready) && SDL_GetTicks() < deadline) {
        SDL_Delay(1);
    }
    assert(SDL_GetAtomicInt(&panel_ready));
    fprintf(stderr, "host-settings: input producer sees ready panel\n");
}

static void Key(SDL_Scancode key) {
    SDL_Event e;
    SDL_zero(e);
    e.type = SDL_EVENT_KEY_DOWN;
    e.key.scancode = key;
    e.key.down = true;
    assert(SDL_PushEvent(&e));
}

static unsigned requested_image;
static unsigned request_count;
static const char *request_path;

static int SupplyImage(unsigned identity) {
    requested_image = identity;
    ++request_count;
    return request_path ? Host_InstallImage(identity, request_path) : 0;
}

static void DiscImages(void) {
    const char *paths[2] = {getenv("PE_TEST_DISC"), getenv("PE_TEST_DISC2")};
    if (!paths[0] || !paths[1]) {
        assert(!getenv("PE_REQUIRE_GAME_ASSETS"));
        return;
    }

    PeDisc images[2] = {{0}};
    host_images = images;
    assert(pe_disc_open(&images[0], paths[0]));
    g_CdImageVolumes[0] = &images[0];
    g_CdImageVolumes[1] = NULL;
    CdRom_BindImage(&images[0], NULL);
    assert(!Asset_OpenPeImage());

    unsigned identity = 0;
    Pe1GameState before = g_GameState;
    unsigned flags = g_PeImageMountFlags;
    u32 base = g_PeImageBaseLba;
    assert(!Asset_ValidatePeImage(&images[0], &identity) && identity == 1);
    assert(!Host_InstallImage(2, paths[0]));
    assert(!g_CdImageVolumes[1] && !images[1].stream);
    assert(g_CdImage == &images[0] && g_PeImageMountFlags == flags && g_PeImageBaseLba == base);
    assert(!memcmp(&before, &g_GameState, sizeof(before)));

    assert(Host_InstallImage(2, paths[1]));
    assert(g_CdImageVolumes[1] == &images[1] && !strcmp(host_options.discs[1], paths[1]));
    assert(g_CdImage == &images[0] && g_PeImageMountFlags == flags && g_PeImageBaseLba == base);
    assert(!memcmp(&before, &g_GameState, sizeof(before)));

    pe_disc_close(&images[1]);
    g_CdImageVolumes[1] = NULL;
    g_CdImageRequest = SupplyImage;
    request_path = NULL;
    request_count = 0;
    assert(Render_InitDisplayLists(2) == -4 && requested_image == 2 && request_count == 1);
    assert(g_CdImage == &images[0] && images[0].stream);

    request_path = paths[0];
    assert(Render_InitDisplayLists(2) == -4 && !g_CdImageVolumes[1]);
    assert(SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "PSYZ_HEADLESS", "1", true));
    Psyz_VideoSetVsyncMode(PSYZ_VSYNC_LIMITLESS);
    ResetGraph(0);

    request_path = paths[1];
    assert(!Render_InitDisplayLists(2));
    assert(requested_image == 2 && g_CdImage == g_CdImageVolumes[1] && images[0].stream);

    unsigned requests = request_count;
    assert(!Render_InitDisplayLists(1) && request_count == requests);
    CdRom_BindImage(NULL, NULL);
    g_CdImageVolumes[0] = g_CdImageVolumes[1] = NULL;
    pe_disc_close(&images[0]);
    pe_disc_close(&images[1]);

    /* Starting with Disc 2 is valid; the original check requests Disc 1. */
    assert(pe_disc_open(&images[0], paths[1]));
    g_CdImageVolumes[1] = &images[0];
    CdRom_BindImage(&images[0], NULL);
    assert(!Asset_OpenPeImage() && g_PeImageMountFlags == 2);

    request_path = paths[0];
    assert(!Render_InitDisplayLists(1));
    assert(requested_image == 1 && g_CdImage == g_CdImageVolumes[0] && images[0].stream);
    assert(!strcmp(host_options.discs[0], paths[0]) && !strcmp(host_options.discs[1], paths[1]));

    CdRom_BindImage(NULL, NULL);
    g_CdImageVolumes[0] = g_CdImageVolumes[1] = NULL;
    g_CdImageRequest = NULL;
    host_images = NULL;
    pe_disc_close(&images[0]);
    pe_disc_close(&images[1]);
}

static HostOverlayState PromptState(int row) {
    return (HostOverlayState){.open = 1, .row = row, .prompt = 1};
}

static void PromptEvent(HostOverlayState *state, Uint32 type, int code) {
    SDL_Event e;
    SDL_zero(e);
    e.type = type;
    if (type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
        e.gbutton.button = (Uint8)code;
    } else {
        e.key.scancode = (SDL_Scancode)code;
        e.key.down = true;
    }
    Host_ProcessSettingsEvent(state, &e, NULL, NULL);
}

static void PromptClick(HostOverlayState *state, float x, float y) {
    SDL_Event e;
    SDL_zero(e);
    e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    e.button.button = SDL_BUTTON_LEFT;
    e.button.x = x;
    e.button.y = y;
    Host_ProcessSettingsEvent(state, &e, NULL, NULL);
}

/* Skip-movie question: No is preselected, Yes skips, every other way out
 * keeps watching, and the panel paints. */
static void PromptAnswers(void) {
    HostOverlayState state = PromptState(1);
    PromptEvent(&state, SDL_EVENT_KEY_DOWN, SDL_SCANCODE_RETURN);
    assert(!state.open && !state.skip);

    state = PromptState(1);
    PromptEvent(&state, SDL_EVENT_KEY_DOWN, SDL_SCANCODE_UP);
    assert(state.open && state.row == 0 && state.dirty);
    SDL_Event held;
    SDL_zero(held);
    held.type = SDL_EVENT_KEY_DOWN;
    held.key.scancode = SDL_SCANCODE_RETURN;
    held.key.down = true;
    held.key.repeat = true;
    Host_ProcessSettingsEvent(&state, &held, NULL, NULL);
    assert(state.open);
    PromptEvent(&state, SDL_EVENT_KEY_DOWN, SDL_SCANCODE_KP_ENTER);
    assert(!state.open && state.skip);

    state = PromptState(0);
    PromptEvent(&state, SDL_EVENT_KEY_DOWN, SDL_SCANCODE_ESCAPE);
    assert(!state.open && !state.skip);

    state = PromptState(0);
    PromptEvent(&state, SDL_EVENT_GAMEPAD_BUTTON_DOWN, SDL_GAMEPAD_BUTTON_EAST);
    assert(!state.open && !state.skip);

    state = PromptState(1);
    PromptEvent(&state, SDL_EVENT_GAMEPAD_BUTTON_DOWN, SDL_GAMEPAD_BUTTON_DPAD_DOWN);
    assert(state.open && state.row == 0);
    PromptEvent(&state, SDL_EVENT_GAMEPAD_BUTTON_DOWN, SDL_GAMEPAD_BUTTON_START);
    assert(!state.open && state.skip);

    state = PromptState(0);
    PromptEvent(&state, SDL_EVENT_GAMEPAD_BUTTON_DOWN, SDL_GAMEPAD_BUTTON_SOUTH);
    assert(!state.open && state.skip);

    /* Settings keys mean nothing here. */
    state = PromptState(1);
    PromptEvent(&state, SDL_EVENT_KEY_DOWN, SDL_SCANCODE_TAB);
    assert(state.open && state.page == 0 && state.row == 1);

    state = PromptState(1);
    SDL_Event quit;
    SDL_zero(quit);
    quit.type = SDL_EVENT_QUIT;
    Host_ProcessSettingsEvent(&state, &quit, NULL, NULL);
    assert(!state.open && state.quit && !state.skip);

    OverlayLayout saved = s_overlayLayout;
    OverlayFitLayout(PROMPT_PANEL_W, OverlayPromptDesignHeight(), 1280, 960, &s_overlayLayout);
    float scale = s_overlayLayout.scale;
    OverlayRect yes = OverlayPromptRowRect(0);
    OverlayRect no = OverlayPromptRowRect(1);
    state = PromptState(1);
    PromptClick(&state, s_overlayLayout.x + (yes.x + 20.0f) * scale,
                s_overlayLayout.y + (yes.y + yes.h * 0.5f) * scale);
    assert(!state.open && state.skip);
    state = PromptState(0);
    PromptClick(&state, s_overlayLayout.x + (no.x + 20.0f) * scale,
                s_overlayLayout.y + (no.y + no.h * 0.5f) * scale);
    assert(!state.open && !state.skip);
    state = PromptState(0);
    PromptClick(&state, 1.0f, 1.0f);
    assert(!state.open && !state.skip);

    OverlaySurface surface = {0};
    state = PromptState(1);
    assert(OverlayFontReady());
    assert(OverlaySurfaceResize(&surface, s_overlayLayout.w, s_overlayLayout.h));
    OverlayPaintPrompt(&surface, &state, &s_overlayLayout);
    unsigned opaque = 0;
    for (int i = 0; i < surface.w * (surface.h - 1); ++i) {
        opaque += surface.pixels[i * 4 + 3] > 200;
    }
    assert(opaque > (unsigned)(surface.w * (surface.h - 1)) / 2);
    OverlaySurfaceRelease(&surface);
    s_overlayLayout = saved;
    fprintf(stderr, "host-settings: skip-movie question passed\n");
}

static int SDLCALL ClosePanel(void *unused) {
    (void)unused;
    WaitPanel();
    Key(SDL_SCANCODE_ESCAPE);
    return 0;
}

static int SDLCALL QuitPanel(void *unused) {
    (void)unused;
    WaitPanel();
    SDL_Event event;
    SDL_zero(event);
    event.type = SDL_EVENT_QUIT;
    assert(SDL_PushEvent(&event));
    return 0;
}

static int SDLCALL Drive(void *unused) {
    (void)unused;
    WaitPanel();
    Key(SDL_SCANCODE_RIGHT);  /* graphics scale 1 -> 2 */
    Key(SDL_SCANCODE_TAB);    /* controls */
    Key(SDL_SCANCODE_DOWN);   /* L2 */
    Key(SDL_SCANCODE_RETURN);
    Key(SDL_SCANCODE_F);      /* rebind */
    Key(SDL_SCANCODE_TAB);    /* audio */
    Key(SDL_SCANCODE_LEFT);   /* 100 -> 95 */
    Key(SDL_SCANCODE_ESCAPE);
    return 0;
}

static int SDLCALL DriveFocus(void *window) {
    SDL_Delay(30);
    assert(Psyz_AudioIsPaused());
    unsigned tick = (unsigned)Psyz_VideoVSync(-1);
    long counter = GetRCnt(0xf2000002);
    SDL_Delay(90);
    assert((unsigned)Psyz_VideoVSync(-1) == tick);
    assert(GetRCnt(0xf2000002) == counter);

    SDL_Event gained;
    SDL_zero(gained);
    gained.type = SDL_EVENT_WINDOW_FOCUS_GAINED;
    gained.window.windowID = SDL_GetWindowID(window);
    assert(SDL_PushEvent(&gained));
    return 0;
}

/* The production boot-module downloader must reject a broken pack before
 * activation. These fixtures use the real ZIP parser, SHA and file verifier. */
static void TestHdInstaller(void) {
    HdSha sha;char hash[65];HdShaInit(&sha);HdShaEnd(&sha,hash);
    assert(!strcmp(hash,"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    HdShaInit(&sha);HdShaAdd(&sha,"a",1);HdShaAdd(&sha,"bc",2);HdShaEnd(&sha,hash);
    assert(!strcmp(hash,"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    char too_short[4]="old";assert(!HdPath(too_short,sizeof(too_short),"long","path"));assert(!*too_short);
    assert(!HdName("../escape.webp")&&!HdName("CON.png")&&!HdName("folder/file.png")&&!HdName(".webp")&&!HdName("0123456789abcdef.webp:stream"));
    assert(HdName("0123456789abcdef.webp")&&HdName("0123456789abcdef.ref.png")&&HdName("0123456789abcdef-wide.bin"));
    HdShaInit(&sha);HdShaAdd(&sha,"payload",7);HdShaEnd(&sha,hash);
    for(int bad=0;bad<3;++bad) {
        char manifest[1400],zip_path[4096],dest[4096],name[80];
        const char *material=bad==1?"../escape.webp":"0123456789abcdef.webp";
        snprintf(manifest,sizeof(manifest),"{\"version\":\"0.1\",\"atlases\":[{\"file\":\"%s\",\"atlas_sha256\":\"%s\",\"reference_file\":\"0123456789abcdef.ref.png\",\"reference_sha256\":\"%s\"}]}",material,hash,hash);
        mz_zip_archive zip={0};assert(mz_zip_writer_init_heap(&zip,0,0));
        assert(mz_zip_writer_add_mem(&zip,"manifest.json",manifest,strlen(manifest),0));
        assert(mz_zip_writer_add_mem(&zip,material,"payload",7,0));
        assert(mz_zip_writer_add_mem(&zip,"0123456789abcdef.ref.png","payload",7,0));
        void *data=NULL;size_t bytes=0;assert(mz_zip_writer_finalize_heap_archive(&zip,&data,&bytes));assert(mz_zip_writer_end(&zip));
        if(bad==2){for(size_t i=0;i+7<=bytes;++i)if(!memcmp((char *)data+i,"payload",7)){((char *)data)[i]^=1;break;}}
        snprintf(name,sizeof(name),"hd-fixture-%d.zip",bad);assert(HdPath(zip_path,sizeof(zip_path),test_directory,name));assert(SDL_SaveFile(zip_path,data,bytes));mz_free(data);
        snprintf(name,sizeof(name),"hd-fixture-%d",bad);assert(HdPath(dest,sizeof(dest),test_directory,name));assert(SDL_CreateDirectory(dest));
        SDL_SetAtomicInt(&host_hd.cancel,0);assert(HdUnpack(zip_path,dest,"0.1")==!bad);
        if(!bad){assert(HdVerify(dest));SDL_strlcpy(host_hd.active,dest,sizeof(host_hd.active));host_hd.verify_only=1;SDL_SetAtomicInt(&host_hd.cancel,1);HdWorker(NULL);assert(!host_hd.success);assert(strstr(host_hd.result_message,"Cancelled"));SDL_SetAtomicInt(&host_hd.cancel,0);}
        HdClean(dest);SDL_RemovePath(zip_path);
    }
    char unicode_dir[4096],saved_file[4096];
    SDL_strlcpy(saved_file,host_options.user_file,sizeof(saved_file));
    int saved_enabled=host_options.enabled;unsigned saved_volume=host_options.volume;
    assert(HdPath(unicode_dir,sizeof(unicode_dir),test_directory,"settings-zażółć"));
    assert(SDL_CreateDirectory(unicode_dir));
    assert(HdPath(host_options.user_file,sizeof(host_options.user_file),unicode_dir,"parasite-eve.toml"));
    host_options.enabled=1;host_options.volume=37;assert(Host_SaveOptions());
    host_options.volume=88;Host_LoadOptions(host_options.user_file);assert(host_options.volume==37);
    assert(SDL_RemovePath(host_options.user_file));assert(SDL_RemovePath(unicode_dir));
    SDL_strlcpy(host_options.user_file,saved_file,sizeof(host_options.user_file));
    host_options.enabled=saved_enabled;host_options.volume=saved_volume;
    memset(&host_hd,0,sizeof(host_hd));
    puts("HD installer: SHA vectors, paths, valid extraction, ZIP CRC failure and cancellation passed");
}

int main(int argc, char **argv) {
#ifdef _WIN32
    /* CI must report assertions instead of waiting for a CRT dialog. */
    _set_error_mode(_OUT_TO_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
    assert(argc == 2);
    test_directory = argv[1];
    assert(SDL_CreateDirectory(test_directory));
    assert(SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy"));
    assert(SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software"));
    assert(SDL_Init(SDL_INIT_VIDEO));
    Host_InitOptions(0);
    TestHdInstaller();
    TestKeyboardLayoutLabels();
    Psyz_VideoSetVsyncMode(PSYZ_VSYNC_OFF);
    unsigned frozen = (unsigned)Psyz_VideoVSync(-1);
    Psyz_PauseHostClock_SDL3(true);
    SDL_Delay(80);
    assert((unsigned)Psyz_VideoVSync(-1) == frozen);
    Psyz_PauseHostClock_SDL3(true);
    SDL_Delay(40);
    Psyz_PauseHostClock_SDL3(false);
    assert((unsigned)Psyz_VideoVSync(-1) == frozen);
    Psyz_PauseHostClock_SDL3(false);
    assert((unsigned)Psyz_VideoVSync(-1) - frozen <= 1);
    /* 2x speed: the blank count carries on without a jump, then runs at
     * twice the NTSC rate; back at 1x it carries on again. */
    assert(Psyz_VideoSetSpeed(0) == -1 && Psyz_VideoSetSpeed(PSYZ_SPEED_MAX + 1) == -1);
    for (unsigned speed = 2; speed >= 1; --speed) {
        unsigned before = (unsigned)Psyz_VideoVSync(-1);
        assert(Psyz_VideoSetSpeed(speed) == 0 && Psyz_VideoGetSpeed() == speed);
        Uint64 start = SDL_GetTicksNS();
        unsigned counted = (unsigned)Psyz_VideoVSync(-1);
        assert(counted - before <= 1);
        SDL_Delay(150);
        double expected = (double)(SDL_GetTicksNS() - start) / 1e9 * 59.94 * speed;
        double blanks = (double)((unsigned)Psyz_VideoVSync(-1) - counted);
        assert(blanks > expected - 2.0 && blanks < expected + 2.0);
    }
    assert(Psyz_SetKeyboardBinding_SDL3(0, SDL_SCANCODE_ESCAPE) == -1);
    assert(Psyz_SetGamepadBinding_SDL3(16, 0) == -1);
    assert(Psyz_SetGamepadBinding_SDL3(0, -1) == -1);
    assert(Psyz_AudioSetOutputVolume(101) == -1);

    host_options.enabled = 1;
    snprintf(host_options.user_file, sizeof(host_options.user_file), "%s/preferences.toml", test_directory);

    TestUserCardMigration();
    TestLateCardImport();
    TestOldCardMarker();
    snprintf(host_options.user_file, sizeof(host_options.user_file), "%s/preferences.toml", test_directory);

    char card_path[4096];
    char expected_card[4096];
    snprintf(expected_card, sizeof(expected_card), "%s/memory-cards/bu00/BASLUS-00662PE01", test_directory);
    assert(Host_CardPath(card_path, "bu00:BASLUS-00662PE01", sizeof(card_path)) == 0);
    assert(!strcmp(card_path, expected_card));

    Psyz_AdjustPathCB(Host_CardPath);
    Psyz_AdjustPath(card_path, "bu00:BASLUS-00662PE01", sizeof(card_path));
    assert(!strcmp(card_path, expected_card));

    FILE *card_file = fopen(card_path, "wb");
    assert(card_file);
    assert(fwrite("private card fixture", 1, 20, card_file) == 20);
    assert(!fclose(card_file));

    card_file = fopen(card_path, "rb");
    assert(card_file);
    char card_bytes[20];
    assert(fread(card_bytes, 1, sizeof(card_bytes), card_file) == sizeof(card_bytes));
    assert(!memcmp(card_bytes, "private card fixture", sizeof(card_bytes)));
    assert(!fclose(card_file));
    assert(!remove(card_path));
    Psyz_AdjustPathCB(NULL);

    assert(Host_CardPath(card_path, "bu10:*", sizeof(card_path)) == 0);
    snprintf(expected_card, sizeof(expected_card), "%s/memory-cards/bu10/", test_directory);
    assert(!strcmp(card_path, expected_card));

    char tiny[2] = {1, 1};
    assert(!Host_CardPath(tiny, "bu00:test", sizeof(tiny)) && !tiny[0]);
    assert(Host_CardPath(card_path, "other:file", sizeof(card_path)) == -1);

    snprintf(host_options.discs[0], sizeof(host_options.discs[0]), "/tmp/zażółć # \"quotes\" \\ disc.cue");
    assert(Host_SaveOptions());
    host_options.discs[0][0] = 0;
    Host_LoadOptions(host_options.user_file);
    assert(!strcmp(host_options.discs[0], "/tmp/zażółć # \"quotes\" \\ disc.cue"));

    char decoded[80];
    assert(Host_String("'C:\\game\\disc.cue' # comment", decoded, sizeof(decoded)));
    assert(!strcmp(decoded, "C:\\game\\disc.cue"));

    char enabled_effect[] = "true", invalid_effect[] = "2";
    assert(Host_ParseOptionValue("graphics", "antialiasing", enabled_effect));
    assert(Host_ParseOptionValue("graphics", "model_filtering", enabled_effect));
    assert(Host_SaveOptions());
    host_options.antialiasing = host_options.model_filtering = 0;
    Host_LoadOptions(host_options.user_file);
    assert(host_options.antialiasing == 1 && host_options.model_filtering == 1);
    assert(!Host_ParseOptionValue("graphics", "antialiasing", invalid_effect));
    assert(!Host_ParseOptionValue("graphics", "model_filtering", invalid_effect));
    assert(host_options.antialiasing == 1 && host_options.model_filtering == 1);
    host_options.antialiasing = host_options.model_filtering = 0;

    assert(!host_options.widescreen);
    assert(Host_ParseOptionValue("graphics", "widescreen", enabled_effect));
    assert(Host_SaveOptions());
    host_options.widescreen = 0;
    Host_LoadOptions(host_options.user_file);
    assert(host_options.widescreen == 1);
    assert(!Host_ParseOptionValue("graphics", "widescreen", invalid_effect));
    assert(host_options.widescreen == 1);
    /* Widescreen is the third Aspect ratio choice. */
    assert(kGraphics[2].get == GetAspect && kGraphics[2].count == 3 && kGraphics[2].get() == 2);
    kGraphics[2].set(1);
    assert(!host_options.widescreen && host_options.aspect == 1 && kGraphics[2].get() == 1);
    kGraphics[2].set(2);
    assert(host_options.widescreen && !host_options.aspect && kGraphics[2].get() == 2);
    kGraphics[2].set(0);
    assert(!host_options.widescreen && !host_options.aspect && kGraphics[2].get() == 0);
    assert(COUNT_OF(kGraphics) <= Host_SettingsVisibleSettingCount(HOST_PAGE_GRAPHICS));
    assert(host_options.frame_interpolation==0);
    assert(Host_ParseOptionValue("graphics","frame_interpolation",enabled_effect));
    assert(Host_SaveOptions());host_options.frame_interpolation=0;
    Host_LoadOptions(host_options.user_file);assert(host_options.frame_interpolation==1);
    assert(!Host_ParseOptionValue("graphics","frame_interpolation",invalid_effect));
    assert(kGraphics[7].get==GetFrameInterpolation && kGraphics[7].count==2);
    host_options.frame_interpolation=0;

    assert(kGraphics[0].get == GetScale && kGraphics[0].count == 5);
    const unsigned expected_scales[5] = {1, 2, 4, 6, 8};
    for (int i = 0; i < 5; ++i) {
        kGraphics[0].set(i);
        assert(host_options.scale == expected_scales[i] && kGraphics[0].get() == i);
    }
    char odd_scale[] = "5";
    assert(Host_ParseOptionValue("graphics", "internal_resolution", odd_scale));
    assert(host_options.scale == 6 && kGraphics[0].get() == 3);
    kGraphics[0].set(0);
    assert(host_options.scale == 1);

    assert(host_options.dynamic_lighting == 0);
    assert(host_options.effect_geometry_correction == 0);
    assert(Host_ParseOptionValue("graphics", "dynamic_lighting", enabled_effect));
    assert(Host_ParseOptionValue("graphics", "effect_geometry_correction", enabled_effect));
    assert(Host_SaveOptions());
    host_options.dynamic_lighting = host_options.effect_geometry_correction = 0;
    Host_LoadOptions(host_options.user_file);
    assert(host_options.dynamic_lighting == 1 &&
           host_options.effect_geometry_correction == 1);
    assert(!Host_ParseOptionValue("graphics", "dynamic_lighting", invalid_effect));
    assert(!Host_ParseOptionValue("graphics", "effect_geometry_correction", invalid_effect));
    assert(kGraphics[8].get == GetDynamicLighting && kGraphics[8].count == 2 && kGraphics[8].get() == 1);
    assert(kGraphics[9].get == GetEffectGeometryCorrection && kGraphics[9].count == 2 && kGraphics[9].get() == 1);
    Host_ApplyOptions(NULL);
    assert(Psyz_VideoGetDynamicLighting() == 1 &&
           Psyz_VideoGetEffectGeometryCorrection() == 1);
    kGraphics[8].set(0);
    kGraphics[9].set(0);
    Host_ApplyOptions(NULL);
    assert(Psyz_VideoGetDynamicLighting() == 0 &&
           Psyz_VideoGetEffectGeometryCorrection() == 0);
    assert(Host_SaveOptions());

    char invalid[4096];
    snprintf(invalid, sizeof(invalid), "%s/invalid.toml", test_directory);
    FILE *f = fopen(invalid, "w");
    assert(f);
    fputs(
        "[graphics]\ninternal_resolution = 9\nfullscreen = false\n"
        "[audio]\nvolume = 101\n"
        "[keyboard]\nL2 = 41\n"
        "[discs]\ndisc1 = \"unterminated\n",
        f
    );
    fclose(f);
    Host_LoadOptions(invalid);
    assert(host_options.scale == 1 && host_options.volume == 100);
    assert(!strcmp(host_options.discs[0], "/tmp/zażółć # \"quotes\" \\ disc.cue"));

    assert(SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "PSYZ_AUDIO_CLOCK", "video", true));
    assert(Psyz_AudioInit() == 0 && !Psyz_AudioIsPaused());
    fprintf(stderr, "host-settings: audio initialized; testing focus pause\n");
    assert(SetRCnt(0xf2000002, 65535, 0));

    SDL_Window *focus = SDL_CreateWindow("focus fixture", 320, 240, SDL_WINDOW_HIDDEN);
    assert(focus);
    SDL_Thread *focus_thread = SDL_CreateThread(DriveFocus, "focus pause", focus);
    assert(focus_thread);
    Host_PauseForFocus(focus);
    SDL_WaitThread(focus_thread, NULL);
    fprintf(stderr, "host-settings: active audio focus pause passed\n");
    assert(!Psyz_AudioIsPaused());

    Psyz_AudioPause();
    focus_thread = SDL_CreateThread(DriveFocus, "already paused focus", focus);
    assert(focus_thread);
    Host_PauseForFocus(focus);
    SDL_WaitThread(focus_thread, NULL);
    assert(Psyz_AudioIsPaused());
    Psyz_AudioUnpause();
    SDL_DestroyWindow(focus);
    fprintf(stderr, "host-settings: paused audio focus pause passed; opening settings\n");

    int original_key = host_options.keys[0];
    SDL_SetAtomicInt(&panel_ready, 0);
    SDL_Thread *thread = SDL_CreateThread(Drive, "settings input", NULL);
    assert(thread);

    SDL_Event open;
    SDL_zero(open);
    open.type = SDL_EVENT_KEY_DOWN;
    open.key.scancode = SDL_SCANCODE_ESCAPE;
    open.key.down = true;
    assert(Host_SettingsEvent(&open));
    SDL_WaitThread(thread, NULL);
    fprintf(stderr, "host-settings: settings input completed\n");
    assert(!Psyz_AudioIsPaused());
    assert(host_options.scale == 2 && host_options.volume == 95);
    assert(host_options.keys[0] == SDL_SCANCODE_F && original_key != SDL_SCANCODE_F);
    assert(Psyz_GetKeyboardBinding_SDL3(0) == SDL_SCANCODE_F);
    assert(Psyz_AudioGetOutputVolume() == 95);

    host_options.scale = 1;
    host_options.volume = 100;
    host_options.keys[0] = original_key;
    Host_LoadOptions(host_options.user_file);
    assert(host_options.scale == 2 && host_options.volume == 95 && host_options.keys[0] == SDL_SCANCODE_F);

    SDL_Event ignored;
    SDL_zero(ignored);
    ignored.type = SDL_EVENT_KEY_UP;
    assert(!Host_SettingsEvent(&ignored));
    assert(Psyz_SetOverlayEventFilter_SDL3(Host_SettingsEvent) == NULL);
    assert(Psyz_SetOverlayEventFilter_SDL3(NULL) == Host_SettingsEvent);

    /* 2x speed hold: unbound by default (and after a save/load), a key a
     * pad button uses is refused, 2x lasts while the bound key is held, the
     * binding is remembered and can be cleared again. */
    assert(host_options.keys[HOST_SPEED_BINDING] == HOST_UNBOUND_KEY);
    assert(host_options.buttons[HOST_SPEED_BINDING] == HOST_UNBOUND_BUTTON);
    SDL_Event speed_key;
    SDL_zero(speed_key);
    speed_key.type = SDL_EVENT_KEY_DOWN;
    speed_key.key.scancode = SDL_SCANCODE_G;
    speed_key.key.down = true;
    assert(!Host_SettingsEvent(&speed_key) && Psyz_VideoGetSpeed() == 1);
    HostOverlayState speed_state = {
        .open = 1,
        .page = OVERLAY_PAGE_CONTROLS,
        .row = HOST_SPEED_BINDING + 1,
        .capturing_binding = 1
    };
    SDL_Event used = speed_key;
    used.key.scancode = (SDL_Scancode)host_options.keys[4];
    Host_ProcessSettingsEvent(&speed_state, &used, NULL, NULL);
    assert(host_options.keys[HOST_SPEED_BINDING] == HOST_UNBOUND_KEY);
    assert(!strcmp(speed_state.status, "Already used by Triangle.") && !speed_state.capturing_binding);
    speed_state.capturing_binding = 1;
    Host_ProcessSettingsEvent(&speed_state, &speed_key, NULL, NULL);
    assert(host_options.keys[HOST_SPEED_BINDING] == SDL_SCANCODE_G);
    assert(Host_SettingsEvent(&speed_key) && Psyz_VideoGetSpeed() == 2);
    speed_key.key.repeat = true;
    assert(Host_SettingsEvent(&speed_key) && Psyz_VideoGetSpeed() == 2);
    speed_key.key.repeat = false;
    speed_key.type = SDL_EVENT_KEY_UP;
    speed_key.key.down = false;
    assert(Host_SettingsEvent(&speed_key) && Psyz_VideoGetSpeed() == 1);
    /* Left/Right on the speed row steps the hold's speed between 2x and
     * PSYZ_SPEED_MAX, stopping at both ends; it is saved. */
    assert(host_options.speed_factor == 2);
    SDL_Event step;
    SDL_zero(step);
    step.type = SDL_EVENT_KEY_DOWN;
    step.key.down = true;
    step.key.scancode = SDL_SCANCODE_LEFT;
    Host_ProcessSettingsEvent(&speed_state, &step, NULL, NULL);
    assert(host_options.speed_factor == 2);
    step.key.scancode = SDL_SCANCODE_RIGHT;
    for (unsigned i = 0; i < 3; ++i) {
        Host_ProcessSettingsEvent(&speed_state, &step, NULL, NULL);
    }
    assert(host_options.speed_factor == PSYZ_SPEED_MAX && !speed_state.capturing_binding);
    speed_key.type = SDL_EVENT_KEY_DOWN;
    speed_key.key.down = true;
    assert(Host_SettingsEvent(&speed_key) && Psyz_VideoGetSpeed() == PSYZ_SPEED_MAX);
    speed_key.type = SDL_EVENT_KEY_UP;
    speed_key.key.down = false;
    assert(Host_SettingsEvent(&speed_key) && Psyz_VideoGetSpeed() == 1);
    speed_key.type = SDL_EVENT_KEY_DOWN;
    speed_key.key.down = true;
    assert(Host_SaveOptions());
    host_options.keys[HOST_SPEED_BINDING] = HOST_UNBOUND_KEY;
    host_options.speed_factor = 2;
    Host_LoadOptions(host_options.user_file);
    assert(host_options.speed_factor == PSYZ_SPEED_MAX);
    step.key.scancode = SDL_SCANCODE_LEFT;
    Host_ProcessSettingsEvent(&speed_state, &step, NULL, NULL);
    assert(host_options.speed_factor == PSYZ_SPEED_MAX - 1);
    assert(host_options.keys[HOST_SPEED_BINDING] == SDL_SCANCODE_G);
    assert(host_options.buttons[HOST_SPEED_BINDING] == HOST_UNBOUND_BUTTON);
    SDL_Event clear = speed_key;
    clear.key.scancode = SDL_SCANCODE_DELETE;
    Host_ProcessSettingsEvent(&speed_state, &clear, NULL, NULL);
    assert(host_options.keys[HOST_SPEED_BINDING] == HOST_UNBOUND_KEY);
    assert(!Host_SettingsEvent(&speed_key) && Psyz_VideoGetSpeed() == 1);
    assert(Host_SaveOptions());
    host_options.keys[HOST_SPEED_BINDING] = SDL_SCANCODE_G;
    Host_LoadOptions(host_options.user_file);
    assert(host_options.keys[HOST_SPEED_BINDING] == HOST_UNBOUND_KEY);

    /* Mixed sources, independent controllers, removal and modal resets. */
    host_options.keys[HOST_SPEED_BINDING] = SDL_SCANCODE_G;
    host_options.buttons[HOST_SPEED_BINDING] = SDL_GAMEPAD_BUTTON_BACK;
    SDL_Event pad = {0}; pad.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    pad.gbutton.which = 11; pad.gbutton.button = SDL_GAMEPAD_BUTTON_BACK;
    Host_ResetSpeedHold();
    assert(Host_SettingsEvent(&speed_key));
    assert(Host_SettingsEvent(&pad));
    speed_key.type = SDL_EVENT_KEY_UP;
    assert(Host_SettingsEvent(&speed_key) && Psyz_VideoGetSpeed() == host_options.speed_factor);
    pad.gbutton.which = 22; assert(Host_SettingsEvent(&pad));
    SDL_Event removed = {0}; removed.type = SDL_EVENT_GAMEPAD_REMOVED; removed.gdevice.which = 11;
    assert(!Host_SettingsEvent(&removed) && Psyz_VideoGetSpeed() == host_options.speed_factor);
    removed.gdevice.which = 22;
    assert(!Host_SettingsEvent(&removed) && Psyz_VideoGetSpeed() == 1);
    speed_key.type = SDL_EVENT_KEY_DOWN; assert(Host_SettingsEvent(&speed_key));
    pad.gbutton.which = 11; assert(Host_SettingsEvent(&pad));
    assert(!Host_SettingsEvent(&removed) && Psyz_VideoGetSpeed() == host_options.speed_factor);
    removed.gdevice.which = 11;
    assert(!Host_SettingsEvent(&removed) && Psyz_VideoGetSpeed() == host_options.speed_factor);
    speed_key.type = SDL_EVENT_KEY_UP; assert(Host_SettingsEvent(&speed_key) && Psyz_VideoGetSpeed() == 1);
    host_options.buttons[HOST_SPEED_BINDING] = SDL_GAMEPAD_BUTTON_COUNT;
    pad.type = SDL_EVENT_GAMEPAD_AXIS_MOTION; pad.gaxis.which = 33;
    pad.gaxis.axis = SDL_GAMEPAD_AXIS_LEFT_TRIGGER; pad.gaxis.value = 17000;
    assert(!Host_SettingsEvent(&pad) && Psyz_VideoGetSpeed() == host_options.speed_factor);
    pad.gaxis.value = 10000;
    assert(!Host_SettingsEvent(&pad) && Psyz_VideoGetSpeed() == host_options.speed_factor);
    Host_ResetSpeedHold();
    assert(!Host_SettingsEvent(&pad) && Psyz_VideoGetSpeed() == 1);
    pad.gaxis.value = 17000; Host_SettingsEvent(&pad);
    removed.gdevice.which = 33;
    assert(!Host_SettingsEvent(&removed) && Psyz_VideoGetSpeed() == 1);
    Host_SettingsEvent(&pad); Psyz_VideoSetSpeed(1); /* Mods panel consumes releases. */
    pad.gaxis.value = 10000;
    assert(!Host_SettingsEvent(&pad) && Psyz_VideoGetSpeed() == 1);
    Host_ResetSpeedHold();
    host_options.keys[HOST_SPEED_BINDING] = HOST_UNBOUND_KEY;
    host_options.buttons[HOST_SPEED_BINDING] = HOST_UNBOUND_BUTTON;
    puts("PASS: speed hold combines sources, handles two pads, disconnects and resets trigger hysteresis");

    /* Field run threshold is a Controls stepper, not a rebinding row. */
    assert(host_options.analog_run_threshold == 85);
    HostOverlayState analog_state = {.open=1, .page=HOST_PAGE_CONTROLS,
                                     .row=HOST_ANALOG_RUN_ROW};
    Host_EnsureControlsRowVisible(&analog_state);
    assert(OverlayVisibleSlotForRow(&analog_state, HOST_ANALOG_RUN_ROW) >= 0);
    SDL_Event threshold_step;
    SDL_zero(threshold_step);
    threshold_step.type=SDL_EVENT_KEY_DOWN;threshold_step.key.down=true;
    threshold_step.key.scancode=SDL_SCANCODE_LEFT;
    Host_ProcessSettingsEvent(&analog_state,&threshold_step,NULL,NULL);
    assert(host_options.analog_run_threshold == 80 && g_FieldAnalogRunThreshold == 80);
    assert(!analog_state.capturing_binding);
    for (unsigned i=0;i<30;++i) Host_ApplySettingsAction(&analog_state, -1, 0, NULL);
    assert(host_options.analog_run_threshold == 0);
    for (unsigned i=0;i<30;++i) Host_ApplySettingsAction(&analog_state, 1, 0, NULL);
    assert(host_options.analog_run_threshold == 100);
    char valid_threshold[]="65", invalid_threshold[]="101", negative_threshold[]="-1";
    assert(Host_ParseOptionValue("controls", "field_run_threshold", valid_threshold));
    assert(!Host_ParseOptionValue("controls", "field_run_threshold", invalid_threshold));
    assert(!Host_ParseOptionValue("controls", "field_run_threshold", negative_threshold));
    assert(host_options.analog_run_threshold == 65);
    assert(Host_SaveOptions());
    host_options.analog_run_threshold = 85;
    Host_LoadOptions(host_options.user_file);
    Host_ApplyOptions(NULL);
    assert(host_options.analog_run_threshold == 65 && g_FieldAnalogRunThreshold == 65);
    OverlayLayout analog_layout;
    OverlayComputeLayout(HOST_PAGE_CONTROLS,1280,720,&analog_layout);
    OverlaySurface analog_surface={0};
    assert(OverlaySurfaceResize(&analog_surface,analog_layout.w,analog_layout.h));
    OverlayPaint(&analog_surface,&analog_state,&analog_layout);
    SDL_Surface *analog_image=SDL_CreateSurfaceFrom(analog_surface.w,analog_surface.h,
        SDL_PIXELFORMAT_RGBA32,analog_surface.pixels,analog_surface.w*4);
    assert(analog_image);
    char analog_image_path[4096];
    snprintf(analog_image_path,sizeof(analog_image_path),"%s/field-controls.bmp",test_directory);
    assert(SDL_SaveBMP(analog_image,analog_image_path));
    SDL_DestroySurface(analog_image);OverlaySurfaceRelease(&analog_surface);


    assert(Host_SettingsPureRowCount(HOST_PAGE_MODS)==3);
    char invalid_hd_value[]="-1";assert(!Host_ParseOptionValue("mods","hd_backgrounds",invalid_hd_value));
    assert(Host_ParseOptionValue("mods","hd_backgrounds","false"));assert(!host_options.hd_backgrounds);
    assert(Host_ParseOptionValue("mods","hd_backgrounds","true"));assert(host_options.hd_backgrounds);
    SDL_SetAtomicInt(&host_hd.busy,1);SDL_SetAtomicInt(&host_hd.progress,42);
    HostOverlayState mod_state={.open=1,.page=OVERLAY_PAGE_MODS,.row=1};OverlaySurface mod_surface={0};
    OverlayLayout mod_layout;OverlayComputeLayout(OVERLAY_PAGE_MODS,1280,720,&mod_layout);
    assert(OverlaySurfaceResize(&mod_surface,mod_layout.w,mod_layout.h));OverlayPaint(&mod_surface,&mod_state,&mod_layout);
    SDL_Surface *mod_image=SDL_CreateSurfaceFrom(mod_surface.w,mod_surface.h,SDL_PIXELFORMAT_RGBA32,mod_surface.pixels,mod_surface.w*4);
    s_overlayLayout=mod_layout;SDL_SetAtomicInt(&host_hd.cancel,0);
    Host_ProcessSettingsPointer(&mod_state,mod_layout.x+mod_layout.scale*300,
        mod_layout.y+mod_layout.scale*(OVERLAY_HEADER_H+OVERLAY_ROW_H+20),1,NULL,NULL);
    assert(SDL_GetAtomicInt(&host_hd.cancel)==1);SDL_SetAtomicInt(&host_hd.cancel,0);
    assert(mod_image);char mod_image_path[4096];snprintf(mod_image_path,sizeof(mod_image_path),"%s/mod-download.bmp",test_directory);assert(SDL_SaveBMP(mod_image,mod_image_path));SDL_DestroySurface(mod_image);OverlaySurfaceRelease(&mod_surface);SDL_SetAtomicInt(&host_hd.busy,0);

    /* Issue #2: All settings tabs share a fixed panel height and top Y anchor. */
    OverlayLayout base_layout = {0};
    OverlayComputeLayout(OVERLAY_PAGE_GRAPHICS, 1280, 720, &base_layout);
    for (int page = 0; page < OVERLAY_PAGE_COUNT; ++page) {
        OverlayLayout page_layout = {0};
        OverlayComputeLayout(page, 1280, 720, &page_layout);
        assert(page_layout.x == base_layout.x && page_layout.y == base_layout.y);
        assert(page_layout.w == base_layout.w && page_layout.h == base_layout.h);
    }

    /* Issue #3: Mouse hover on Controls never auto-scrolls; wheel & keys step by 1. */
    s_overlayLayout = base_layout;
    HostOverlayState scroll_state = {
        .open = 1,
        .page = OVERLAY_PAGE_CONTROLS,
        .row = 0,
        .scroll = 0
    };
    OverlayRect bottom_visible = OverlayRowRect(&scroll_state, OVERLAY_CONTROLS_VISIBLE_BINDINGS);
    SDL_Event hover;
    SDL_zero(hover);
    hover.type = SDL_EVENT_MOUSE_MOTION;
    hover.motion.x = (float)base_layout.x + (bottom_visible.x + 20.0f) * base_layout.scale;
    hover.motion.y = (float)base_layout.y + (bottom_visible.y + 10.0f) * base_layout.scale;
    Host_ProcessSettingsEvent(&scroll_state, &hover, NULL, NULL);
    assert(scroll_state.row == OVERLAY_CONTROLS_VISIBLE_BINDINGS);
    assert(scroll_state.scroll == 0);

    SDL_Event wheel;
    SDL_zero(wheel);
    wheel.type = SDL_EVENT_MOUSE_WHEEL;
    wheel.wheel.y = -1.0f;
    Host_ProcessSettingsEvent(&scroll_state, &wheel, NULL, NULL);
    assert(scroll_state.scroll == 1);

    SDL_Event down_key;
    SDL_zero(down_key);
    down_key.type = SDL_EVENT_KEY_DOWN;
    down_key.key.scancode = SDL_SCANCODE_DOWN;
    down_key.key.down = true;
    Host_ProcessSettingsEvent(&scroll_state, &down_key, NULL, NULL);
    assert(scroll_state.row == OVERLAY_CONTROLS_VISIBLE_BINDINGS + 1);
    assert(scroll_state.scroll == 1);
    Host_ProcessSettingsEvent(&scroll_state, &down_key, NULL, NULL);
    assert(scroll_state.row == OVERLAY_CONTROLS_VISIBLE_BINDINGS + 2);
    assert(scroll_state.scroll == 2);

    /* Multi-selection callback owns paths and rejects a third image. */
    HostDiscDialog dialog = {0};
    const char *files[] = {"second.cue", "first.cue", NULL};
    Host_DiscChosen(&dialog, files, 0);
    assert(SDL_GetAtomicInt(&dialog.done));
    assert(!dialog.failed && !strcmp(dialog.paths[1], "first.cue"));
    SDL_free(dialog.paths[0]);
    SDL_free(dialog.paths[1]);

    memset(&dialog, 0, sizeof(dialog));
    const char *three[] = {"1.cue", "2.cue", "3.cue", NULL};
    Host_DiscChosen(&dialog, three, 0);
    assert(dialog.failed);
    SDL_free(dialog.paths[0]);
    SDL_free(dialog.paths[1]);

    memset(&dialog, 0, sizeof(dialog));
    const char *cancel[] = {NULL};
    Host_DiscChosen(&dialog, cancel, 0);
    assert(!dialog.failed && !dialog.paths[0]);
    Host_DiscChosen(&dialog, NULL, 0);
    assert(dialog.failed);

    Psyz_AudioPause();
    SDL_SetAtomicInt(&panel_ready, 0);
    thread = SDL_CreateThread(ClosePanel, "close paused settings", NULL);
    assert(thread);
    assert(Host_SettingsEvent(&open));
    SDL_WaitThread(thread, NULL);
    assert(Psyz_AudioIsPaused());

    SDL_SetAtomicInt(&panel_ready, 0);
    thread = SDL_CreateThread(QuitPanel, "quit settings", NULL);
    assert(thread);
    Uint64 quit_started = SDL_GetTicksNS();
    assert(Host_SettingsEvent(&open));
    SDL_WaitThread(thread, NULL);
    assert(Psyz_AudioIsPaused());

    SDL_Event returned;
    unsigned quits = 0;
    Uint64 quit_deadline = SDL_GetTicks() + 1000;
    while (!quits && SDL_GetTicks() < quit_deadline) {
        if (SDL_WaitEventTimeout(&returned, 20) && returned.type == SDL_EVENT_QUIT) {
            ++quits;
            assert(returned.quit.timestamp >= quit_started && returned.quit.timestamp <= SDL_GetTicksNS());
        }
    }
    assert(quits == 1 && !SDL_HasEvent(SDL_EVENT_QUIT));
    Psyz_AudioDestroy();
    fprintf(stderr, "host-settings: paused settings completed; checking disc images\n");
    PromptAnswers();
    DiscImages();
    SDL_Quit();
    puts("PASS: both image identities, missing-image checks, additive mounting, TOML roundtrip, invalid values, modal graphics/controls/audio, persistence, ESC consumption, multi-disc callback, skip-movie question");
    return 0;
}

#else

#include <SDL3/SDL.h>
#include <psyz/input.h>
#include <psyz/overlay_sdl3.h>
#include <stdio.h>
#include "pe1/battle.h"
#include "pe1/boot.h"
#include "pe1/menu_widget.h"
#include "pe1/pad_state.h"
#include "pe1/scene_flags.h"

static int menu_cell_indices(void) {
    MenuWidgetNode node = {0};
    node.grid_width = 1;
    node.cell_mask = -1;
    node.cursor_x = -1;
    if (MenuWidget_GetCellIndex(&node) != -1) {
        return 0;
    }
    node.cursor_x = 0;
    node.cursor_y = -1;
    if (MenuWidget_GetCellIndex(&node) != -1) {
        return 0;
    }
    if (MenuWidget_GetCellIndex(NULL) != -1) {
        return 0;
    }

    const unsigned masks[] = {0, 1, 0x80000000u, 0x55555555u, 0xffffffffu};
    for (unsigned mask = 0; mask < sizeof(masks) / sizeof(masks[0]); ++mask) {
        node.cell_mask = (int)masks[mask];
        for (int index = 0; index < 96; ++index) {
            node.cursor_y = index;
            int expected = (masks[mask] & (1u << (index & 31))) ? index : -1;
            if (MenuWidget_GetCellIndex(&node) != expected) {
                return 0;
            }
        }
    }
    puts("Menu cell selection: absent node/cursor and 480 MIPS-masked index/mask cases passed");
    return 1;
}

static unsigned calls;

static void trigger(struct BattleEntity *ctx, int *index) {
    ++calls;
    ctx->motionX += 1;
    *index = 1;
}

static void fallback(struct BattleEntity *ctx, int *index) {
    (void)index;
    ++calls;
    ctx->motionX += 10;
}

int main(void) {
    if (!menu_cell_indices()) {
        return 45;
    }
    if (!SDL_Init(SDL_INIT_EVENTS)) {
        return 10;
    }

    Boot_ResetPadInput();
    Boot_ResetPadInput();

    const SDL_Scancode keys[] = {SDL_SCANCODE_RETURN, SDL_SCANCODE_KP_ENTER};
    for (unsigned i = 0; i < 2; ++i) {
        SDL_Event event;
        SDL_zero(event);
        event.type = SDL_EVENT_KEY_DOWN;
        event.key.scancode = keys[i];
        event.key.down = true;
        if (!SDL_PushEvent(&event) || (g_PadPackets[0].buttons & 8)) {
            return 11;
        }
        if ((g_PadPackets[0].header & 0xf0ff) != 0x4000) {
            return 12;
        }
        event.type = SDL_EVENT_KEY_UP;
        if (!SDL_PushEvent(&event) || !(g_PadPackets[0].buttons & 8)) {
            return 13;
        }
    }

    if (Psyz_SetKeyboardBinding_SDL3(11, SDL_SCANCODE_F)) {
        return 40;
    }
    Boot_ResetPadInput(); /* Reset must not undo an explicit rebind. */
    if (Psyz_GetKeyboardBinding_SDL3(11) != SDL_SCANCODE_F) {
        return 45;
    }

    SDL_Event remapped;
    SDL_zero(remapped);
    remapped.type = SDL_EVENT_KEY_DOWN;
    remapped.key.scancode = SDL_SCANCODE_RETURN;
    remapped.key.down = true;
    SDL_PushEvent(&remapped);
    if (!(g_PadPackets[0].buttons & 8)) {
        return 41;
    }
    remapped.type = SDL_EVENT_KEY_UP;
    SDL_PushEvent(&remapped);

    remapped.type = SDL_EVENT_KEY_DOWN;
    remapped.key.scancode = SDL_SCANCODE_F;
    SDL_PushEvent(&remapped);
    if (g_PadPackets[0].buttons & 8) {
        return 42;
    }
    remapped.type = SDL_EVENT_KEY_UP;
    SDL_PushEvent(&remapped);
    if (Psyz_SetKeyboardBinding_SDL3(11, SDL_SCANCODE_RETURN)) {
        return 43;
    }

    SDL_Event focus;
    SDL_zero(focus);
    focus.type = SDL_EVENT_WINDOW_FOCUS_LOST;
    SDL_PushEvent(&focus);

    SDL_Event key;
    SDL_zero(key);
    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_RETURN;
    key.key.down = true;
    SDL_PushEvent(&key);
    if (!(g_PadPackets[0].buttons & 8)) {
        return 15;
    }

    focus.type = SDL_EVENT_WINDOW_FOCUS_GAINED;
    SDL_PushEvent(&focus);
    SDL_PushEvent(&key);
    if (g_PadPackets[0].buttons & 8) {
        return 16;
    }
    key.type = SDL_EVENT_KEY_UP;
    SDL_PushEvent(&key);

    /* Exercise native controller polling without opening a video device. */
    const char *headless = SDL_GetEnvironmentVariable(SDL_GetEnvironment(), "PSYZ_HEADLESS");
    char *saved_headless = headless ? SDL_strdup(headless) : NULL;
    SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "PSYZ_HEADLESS", "", true);

    g_BootHostActive = 1;
    Boot_ResetPadInput();
    Boot_BuildRenderFlagTable();
    char controller[PSYZ_PAD_BUF_LEN] = {0};
    controller[1] = 0x41;
    controller[2] = (char)0xff;
    controller[3] = (char)0xbf;
    Psyz_PadsSet(0, controller, sizeof(controller));
    Field_HandleStateTransition();
    if (g_PadPackets[0].buttons & 0x4000) {
        return 44;
    }

    /* Real packet polling reaches the original analog movement decoder. */
    controller[1] = PSYZ_CTRL_ANALOG_PAD;
    controller[2] = controller[3] = (char)0xff;
    controller[4] = controller[5] = (char)128;
    static const struct {unsigned x,y,threshold,analog,run;} stick_cases[] = {
        {128,128,85,0,0}, {133,122,85,0,0},
        {190,128,85,1,0}, {235,128,85,1,0}, {236,128,85,1,1},
        {21,128,85,1,0}, {20,128,85,1,1},
        {205,205,90,1,0}, {205,205,80,1,1},
        {170,128,0,1,1}, {254,128,100,1,0},
        {255,128,100,1,1}, {0,128,100,1,1}
    };
    for (unsigned i=0;i<sizeof(stick_cases)/sizeof(stick_cases[0]);++i) {
        controller[6]=(char)stick_cases[i].x;controller[7]=(char)stick_cases[i].y;
        g_FieldAnalogRunThreshold=stick_cases[i].threshold;
        g_GameStateFlags=0;
        Psyz_PadsSet(0,controller,sizeof(controller));Field_HandleStateTransition();
        if (((g_PadPackets[0].header>>12)==7)!=stick_cases[i].analog ||
            ((g_FieldPadBits&1)!=0)!=stick_cases[i].run) {
            fprintf(stderr,"Analog case %u: header=%04x field=%08x\n",i,g_PadPackets[0].header,g_FieldPadBits);
            return 49;
        }
    }
    /* A field preference must not change the original battle run threshold. */
    g_FieldAnalogRunThreshold=0;controller[6]=(char)190;controller[7]=(char)128;
    g_GameStateFlags=2;Psyz_PadsSet(0,controller,sizeof(controller));Field_HandleStateTransition();
    if (g_FieldPadBits&1) return 50;
    /* The field preference does not affect navigation inside a game menu. */
    MenuWidgetNode analog_menu={0};analog_menu.mode=1;
    g_MenuWidgetActiveListHead=&analog_menu;
    g_GameStateFlags=0;controller[6]=(char)190;
    Psyz_PadsSet(0,controller,sizeof(controller));Field_HandleStateTransition();
    if (g_FieldPadBits&0x79) return 54;
    controller[6]=(char)250;
    Psyz_PadsSet(0,controller,sizeof(controller));Field_HandleStateTransition();
    if (!(g_FieldPadBits&0x10) || (g_FieldPadBits&1)) return 55;
    g_MenuWidgetActiveListHead=NULL;

    /* D-pad and keyboard movement take priority over a deflected stick. */
    g_GameStateFlags=0;controller[2]=(char)0xef;
    Psyz_PadsSet(0,controller,sizeof(controller));Field_HandleStateTransition();
    if ((g_PadPackets[0].header>>12)!=4 || !(g_FieldPadBits&8)) return 51;
    controller[2]=(char)0xff;
    SDL_Event direction;SDL_zero(direction);direction.type=SDL_EVENT_KEY_DOWN;
    direction.key.scancode=SDL_SCANCODE_UP;direction.key.down=true;SDL_PushEvent(&direction);
    Psyz_PadsSet(0,controller,sizeof(controller));Field_HandleStateTransition();
    if ((g_PadPackets[0].header>>12)!=4 || !(g_FieldPadBits&8)) return 52;
    direction.type=SDL_EVENT_KEY_UP;direction.key.down=false;SDL_PushEvent(&direction);
    /* Losing focus releases axes as well as keyboard buttons. */
    direction.type=SDL_EVENT_WINDOW_FOCUS_LOST;SDL_PushEvent(&direction);
    Field_HandleStateTransition();
    if ((g_PadPackets[0].header>>12)!=4 || (g_FieldPadBits&0x79)) return 53;
    g_FieldAnalogRunThreshold=85;
    g_BootHostActive = 0;
    Boot_ResetPadInput();
    if (saved_headless) {
        SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "PSYZ_HEADLESS", saved_headless, true);
        SDL_free(saved_headless);
    } else {
        SDL_UnsetEnvironmentVariable(SDL_GetEnvironment(), "PSYZ_HEADLESS");
    }

    puts("SDL keyboard watch: Return/keypad Enter reach original START; release, focus and repeated reset passed");

    SceneFlagRecord rules[] = {{1 | 4, 3, 4, trigger}, {0, 0, 0, fallback}};
    SceneFlagRecord *table[] = {rules, 0};
    int mode = 0;
    BattleEntity actor = {0};
#define value actor.motionX
    g_FieldPadBits = 3;
    g_FieldPadPressed = 0;
    Scene_CheckFlagBits(&actor, table, &mode);
    if (value != 10 || calls != 1) {
        return 1;
    }

    mode = 0;
    value = 0;
    calls = 0;
    g_FieldPadPressed = 4;
    Scene_CheckFlagBits(&actor, table, &mode);
    if (value != 1 || calls != 1 || mode != 1) {
        return 2;
    }

    /* Callback mode changes do not redirect the current record walk. */
    SceneFlagRecord continued[] = {{0x20 | 0x10, 0, 0, trigger}, {0x20, 0, 0, trigger}, {0, 0, 0, fallback}};
    table[0] = continued;
    mode = 0;
    value = 0;
    calls = 0;
    Scene_CheckFlagBits(&actor, table, &mode);
    if (value != 2 || calls != 2) {
        return 3;
    }

    rules[0].flags = 2 | 8;
    rules[0].mask_any = 4;
    table[0] = rules;
    mode = 0;
    value = 0;
    calls = 0;
    g_FieldPadBits = 4;
    g_FieldPadReleased = 4;
    Scene_CheckFlagBits(&actor, table, &mode);
    if (value != 1 || calls != 1) {
        return 4;
    }

    puts("Original input rule masks, callback continuation and fallback passed");
    return 0;
}

#endif

#endif /* PE_CRASH_REPORT_TEST */
