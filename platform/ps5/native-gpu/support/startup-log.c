/* Opt-in game diagnostics. No signal handler: the last flushed stage is evidence. */
#include <SDL3/SDL.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <string.h>

extern int pe_session_fd(void);
extern const char *pe_session_path(void);

static int log_fd = -1;
static void stage(const char *fmt, ...) {
    if (log_fd < 0) return;
    int saved_errno = errno;
    char line[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    if (n > 0) {
        size_t len = (size_t)n < sizeof(line) ? (size_t)n : sizeof(line)-1;
        size_t done = 0;
        while (done < len) {
            ssize_t w = write(log_fd, line + done, len - done);
            if (w < 0 && errno == EINTR) continue;
            if (w <= 0) break;
            done += (size_t)w;
        }
        fsync(log_fd);
    }
    errno = saved_errno;
}
static void SDLCALL sdl_log(void *userdata, int category, SDL_LogPriority priority, const char *message) {
    (void)userdata;
    /* Only the observed INFO spam, with bounded state and no new locks.
     * SDL serializes output callbacks. Warnings/errors always pass through. */
    static char last_vsync[512];
    static unsigned suppressed;
    static int last_category;
    int vsync = priority == SDL_LOG_PRIORITY_INFO && message &&
        strstr(message, "[ConfigureVSync]") && strlen(message) < sizeof last_vsync;
    if (vsync && category == last_category && !strcmp(last_vsync, message)) {
        if (++suppressed == 600) {
            stage("SDL ConfigureVSync: suppressed 600 identical INFO messages\n");
            suppressed = 0;
        }
        return;
    }
    if (vsync) {
        if (suppressed) stage("SDL ConfigureVSync: suppressed %u identical INFO messages before changed state\n", suppressed);
        suppressed = 0;
        strcpy(last_vsync, message);
        last_category = category;
    }
    stage("SDL category=%d priority=%d: %s\n", category, (int)priority, message);
}
/* Bounded observation only: do not retry, probe, clear errors or change returns.
 * Capture errno before SDL error formatting/cleanup can obscure the syscall. */
static atomic_uint fs_events;
static int fs_trace(void) {
    return atomic_fetch_add_explicit(&fs_events, 1, memory_order_relaxed) < 128;
}
#define FS_INT_WRAP(name, declaration, arguments, path) \
    int __real_##name declaration; \
    int __wrap_##name declaration { \
        int trace = fs_trace(); \
        if (trace) stage("before fs " #name " path=%.700s\n", path); \
        int result = __real_##name arguments; \
        int error = errno; \
        if (trace) stage("after fs " #name " result=%d errno=%d\n", result, error); \
        errno = error; \
        return result; \
    }
FS_INT_WRAP(stat, (const char *path, struct stat *info), (path, info), path)
FS_INT_WRAP(mkdir, (const char *path, mode_t mode), (path, mode), path)
DIR *pe_game_opendir(const char *);
DIR *__wrap_opendir(const char *path) {
    int trace = fs_trace();
    if (trace) stage("before fs opendir path=%.700s\n", path);
    DIR *result = pe_game_opendir(path);
    int error = errno;
    if (trace) stage("after fs opendir null=%d errno=%d\n", result == NULL, error);
    errno = error;
    return result;
}
struct dirent *pe_game_readdir(DIR *);
struct dirent *__wrap_readdir(DIR *dir) {
    int trace = fs_trace();
    int prior_error = errno;
    struct dirent *result = pe_game_readdir(dir);
    int error = errno;
    if (trace) stage("after fs readdir dir=%p null=%d prior_errno=%d errno=%d (null may be EOF)\n",
                     (void *)dir, result == NULL, prior_error, error);
    errno = error;
    return result;
}
/* API-boundary errors complement the immediate POSIX results above. A success
 * does not make errno meaningful; SDL_GetError is read only on failure. */
#define FS_SDL_WRAP(name, declaration, arguments, path) \
    bool __real_##name declaration; \
    bool __wrap_##name declaration { \
        int trace = fs_trace(); \
        if (trace) stage("before fs " #name " path=%.700s\n", path); \
        bool result = __real_##name arguments; \
        int error = errno; \
        if (trace) stage("after fs " #name " result=%d errno=%d SDL=%.500s\n", \
                         result, error, result ? "" : SDL_GetError()); \
        errno = error; \
        return result; \
    }
FS_SDL_WRAP(SDL_CreateDirectory, (const char *path), (path), path)
FS_SDL_WRAP(SDL_GetPathInfo, (const char *path, SDL_PathInfo *info), (path, info), path)
FS_SDL_WRAP(SDL_EnumerateDirectory,
            (const char *path, SDL_EnumerateDirectoryCallback cb, void *userdata),
            (path, cb, userdata), path)
FS_SDL_WRAP(SDL_SaveFile, (const char *path, const void *data, size_t size),
            (path, data, size), path)
#ifdef PE_APPLICATION_SERVICE_DIAGNOSTICS
/* Observe real backend calls without repeating initialization or changing
 * service ownership. Raw returns do not prove firmware support. */
int __real_sceUserServiceInitialize(void *);
int __wrap_sceUserServiceInitialize(void *params) {
    stage("before sceUserServiceInitialize\n");
    int result = __real_sceUserServiceInitialize(params);
    stage("after sceUserServiceInitialize result=0x%08x\n", (unsigned)result);
    return result;
}
#define SERVICE_WRAP(name) \
    int __real_##name(void); \
    int __wrap_##name(void) { \
        stage("before " #name "\n"); \
        int result = __real_##name(); \
        stage("after " #name " result=0x%08x\n", (unsigned)result); \
        return result; \
    }
SERVICE_WRAP(scePadInit)
SERVICE_WRAP(sceSystemServiceHideSplashScreen)
int __real_sceVideoOutOpen(int, int, int, const void *);
int __wrap_sceVideoOutOpen(int user, int type, int index, const void *params) {
    stage("before sceVideoOutOpen user=0x%x type=%d index=%d\n", (unsigned)user, type, index);
    int result = __real_sceVideoOutOpen(user, type, index, params);
    stage("after sceVideoOutOpen result=0x%08x\n", (unsigned)result);
    return result;
}
#endif
int __real_chdir(const char *);
int __wrap_chdir(const char *path) {
    stage("before chdir path=%.200s\n", path ? path : "(null)");
    int result = __real_chdir(path);
    int error = errno;
    stage("after chdir result=%d errno=%d\n", result, error);
    errno = error;
    return result;
}
int __real_dup2(int, int);
int __wrap_dup2(int from, int to) {
    stage("before dup2 from=%d to=%d\n", from, to);
    int result = __real_dup2(from, to);
    int error = errno;
    stage("after dup2 result=%d errno=%d\n", result, error);
    errno = error;
    return result;
}
FILE *__real_fopen(const char *, const char *);
FILE *__wrap_fopen(const char *path, const char *mode) {
    static atomic_uint remaining = 16;
    unsigned count = atomic_load_explicit(&remaining, memory_order_relaxed);
    while (count && !atomic_compare_exchange_strong_explicit(&remaining,
            &count, count - 1, memory_order_relaxed, memory_order_relaxed)) {}
    if (count) stage("before fopen path=%.200s mode=%.16s\n",
        path ? path : "(null)", mode ? mode : "(null)");
    FILE *result = __real_fopen(path, mode);
    int error = errno;
    if (count) stage("after fopen null=%d errno=%d\n", result == NULL, error);
    errno = error;
    return result;
}
int __real_main(int, char **);
int __wrap_main(int argc, char **argv) {
    log_fd = pe_session_fd();
    const char *path = pe_session_path();
    stage("\n=== PE startup debug: entered main argc=%d session=%s ===\n", argc, path);
    if (log_fd >= 0) {
        int out = dup2(log_fd, STDOUT_FILENO);
        int err = dup2(log_fd, STDERR_FILENO);
        setvbuf(stdout, NULL, _IONBF, 0);
        setvbuf(stderr, NULL, _IONBF, 0);
        stage("stdout redirect=%d stderr redirect=%d\n", out, err);
    }
    stage("before SDL_SetLogOutputFunction\n");
    SDL_SetLogOutputFunction(sdl_log, NULL);
    stage("before game main\n");
    int result = __real_main(argc, argv);
    stage("game main returned=%d\n", result);
    return result;
}
#define INIT_WRAP(name) \
    bool __real_##name(SDL_InitFlags); \
    bool __wrap_##name(SDL_InitFlags flags) { \
        stage("before " #name " flags=0x%x\n", (unsigned)flags); \
        bool ok = __real_##name(flags); \
        stage("after " #name " result=%d error=%s\n", ok, ok ? "" : SDL_GetError()); \
        return ok; \
    }
INIT_WRAP(SDL_Init)
INIT_WRAP(SDL_InitSubSystem)
SDL_Window *__real_SDL_CreateWindow(const char *, int, int, SDL_WindowFlags);
SDL_Window *__wrap_SDL_CreateWindow(const char *title, int w, int h, SDL_WindowFlags flags) {
    stage("before SDL_CreateWindow %dx%d\n", w, h);
    SDL_Window *p = __real_SDL_CreateWindow(title, w, h, flags);
    stage("after SDL_CreateWindow result=%p error=%s\n", (void *)p, p ? "" : SDL_GetError());
    return p;
}
SDL_Surface *__real_SDL_GetWindowSurface(SDL_Window *);
SDL_Surface *__wrap_SDL_GetWindowSurface(SDL_Window *w) {
    static int first = 1;
    int trace = first; first = 0;
    if (trace) stage("before first SDL_GetWindowSurface\n");
    SDL_Surface *s = __real_SDL_GetWindowSurface(w);
    if (trace || !s) stage("after SDL_GetWindowSurface result=%p error=%s\n", (void *)s, s ? "" : SDL_GetError());
    return s;
}
bool __real_SDL_UpdateWindowSurface(SDL_Window *);
bool __wrap_SDL_UpdateWindowSurface(SDL_Window *w) {
    static int first = 1;
    int trace = first; first = 0;
    if (trace) stage("before first SDL_UpdateWindowSurface\n");
    bool ok = __real_SDL_UpdateWindowSurface(w);
    if (trace || !ok) stage("after SDL_UpdateWindowSurface result=%d error=%s\n", ok, ok ? "" : SDL_GetError());
    return ok;
}
SDL_AudioDeviceID __real_SDL_OpenAudioDevice(SDL_AudioDeviceID, const SDL_AudioSpec *);
SDL_AudioDeviceID __wrap_SDL_OpenAudioDevice(SDL_AudioDeviceID id, const SDL_AudioSpec *spec) {
    stage("before SDL_OpenAudioDevice id=%u\n", (unsigned)id);
    SDL_AudioDeviceID p = __real_SDL_OpenAudioDevice(id, spec);
    stage("after SDL_OpenAudioDevice result=%u error=%s\n", (unsigned)p, p ? "" : SDL_GetError());
    return p;
}
SDL_Gamepad *__real_SDL_OpenGamepad(SDL_JoystickID);
SDL_AudioStream *__real_SDL_OpenAudioDeviceStream(SDL_AudioDeviceID, const SDL_AudioSpec *, SDL_AudioStreamCallback, void *);
SDL_AudioStream *__wrap_SDL_OpenAudioDeviceStream(SDL_AudioDeviceID id, const SDL_AudioSpec *spec, SDL_AudioStreamCallback cb, void *userdata) {
    stage("before SDL_OpenAudioDeviceStream id=%u\n", (unsigned)id);
    SDL_AudioStream *p = __real_SDL_OpenAudioDeviceStream(id, spec, cb, userdata);
    stage("after SDL_OpenAudioDeviceStream result=%p error=%s\n", (void *)p, p ? "" : SDL_GetError());
    return p;
}
bool __real_SDL_ResumeAudioStreamDevice(SDL_AudioStream *);
bool __wrap_SDL_ResumeAudioStreamDevice(SDL_AudioStream *stream) {
    stage("before SDL_ResumeAudioStreamDevice\n");
    bool ok = __real_SDL_ResumeAudioStreamDevice(stream);
    stage("after SDL_ResumeAudioStreamDevice result=%d error=%s\n", ok, ok ? "" : SDL_GetError());
    return ok;
}
SDL_Gamepad *__wrap_SDL_OpenGamepad(SDL_JoystickID id) {
    stage("before SDL_OpenGamepad id=%u\n", (unsigned)id);
    SDL_Gamepad *p = __real_SDL_OpenGamepad(id);
    stage("after SDL_OpenGamepad result=%p error=%s\n", (void *)p, p ? "" : SDL_GetError());
    return p;
}
#define VOID_WRAP(name) \
    void __real_##name(void); \
    void __wrap_##name(void) { stage("before " #name "\n"); __real_##name(); stage("after " #name "\n"); }
#define INT_WRAP(name) \
    int __real_##name(void); \
    int __wrap_##name(void) { stage("before " #name "\n"); int r=__real_##name(); stage("after " #name " result=%d\n",r); return r; }
VOID_WRAP(Boot_InitSystem)
VOID_WRAP(Boot_InitGameState)
VOID_WRAP(Boot_InitSubsystems)
INT_WRAP(Asset_OpenPeImage)
INT_WRAP(Scene_LoadSceneData)
