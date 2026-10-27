#include "common.h"
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>
#include <psyz/audio.h>
#include <psyz/overlay_sdl3.h>
#include <psyz/system.h>
#include <psyz/video.h>
#include "pe1/boot.h"
#include "pe1/boot_disc_check.h"
#include "pe1/boot_display.h"
#include "pe1/boot_services.h"
#include "pe1/field_bg_load.h"
#include "pe1/game_state.h"
#include "pe1/game_timers.h"
#include "pe1/pad_state.h"
#include "pe1/psyq_gpu.h"
#include "pe1/scene_dispatch.h"
#include "pe_hooks.h"

/* Desktop preferences belong to the original boot module. The modal loop
 * never ticks the game, PS1 timers, scripts, or audio. */

static struct {
    unsigned scale;
    unsigned volume;
    int aspect;
    int dither;
    int geometry_correction;
    int antialiasing;
    int model_filtering;
    int fullscreen;
    /* 2..PSYZ_SPEED_MAX, used while the speed binding is held */
    unsigned speed_factor;
    unsigned analog_run_threshold;
    /* 16 pad buttons, then the speed hold (unbound by default) */
    int keys[17];
    int buttons[17];
    char discs[2][4096];
    char user_file[4096];
    int enabled;
} host_options;

#define HOST_BINDING_COUNT 17
#define HOST_SPEED_BINDING 16
#define HOST_ANALOG_RUN_ROW (HOST_BINDING_COUNT + 1)
#define HOST_UNBOUND_KEY SDL_SCANCODE_UNKNOWN
#define HOST_UNBOUND_BUTTON -1

static const char *const host_button_names[HOST_BINDING_COUNT] = {
    "L2", "R2", "L1", "R1",
    "Triangle", "Circle", "Cross", "Square",
    "Select", "L3", "R3", "Start",
    "Up", "Right", "Down", "Left",
    "Speed"
};

/* speed_factor while the speed binding is held; every start is 1x. */
static unsigned host_speed = 1;

static void Host_SetSpeed(unsigned speed) {
    if (speed != host_speed || speed != Psyz_VideoGetSpeed()) {
        host_speed = speed;
        Psyz_VideoSetSpeed(speed);
    }
}

static int host_speed_key_held;
static struct { SDL_JoystickID id; int held; } host_speed_pads[16];

static void Host_ResetSpeedHold(void) {
    host_speed_key_held = 0;
    memset(host_speed_pads, 0, sizeof(host_speed_pads));
    Host_SetSpeed(1);
}

typedef enum HostSettingsPage {
    HOST_PAGE_GRAPHICS = 0,
    HOST_PAGE_CONTROLS = 1,
    HOST_PAGE_AUDIO = 2,
    HOST_PAGE_IMAGES = 3
} HostSettingsPage;

typedef enum HostBindDevice {
    HOST_BIND_KEYBOARD = 0,
    HOST_BIND_CONTROLLER = 1
} HostBindDevice;

#include "settings_overlay.h"

static char *Host_Trim(char *s) {
    while (isspace((unsigned char)*s)) {
        ++s;
    }
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) {
        --end;
    }
    *end = '\0';
    return s;
}

/* Preferences use TOML tables, integers, booleans and single-line strings.
 * Unknown keys remain harmless; malformed supported values are diagnosed. */
static int Host_String(char *text, char *out, size_t size) {
    char quote = *text++;
    if (quote != '"' && quote != '\'') {
        return 0;
    }

    size_t n = 0;
    while (*text && *text != quote) {
        unsigned char c = (unsigned char)*text++;
        if (c < 32) {
            return 0;
        }
        if (quote == '"' && c == '\\') {
            c = (unsigned char)*text++;
            if (c == 'n') {
                c = '\n';
            } else if (c == 'r') {
                c = '\r';
            } else if (c == 't') {
                c = '\t';
            } else if (c != '"' && c != '\\') {
                return 0;
            }
        }
        if (n + 1 >= size) {
            return 0;
        }
        out[n++] = (char)c;
    }
    if (*text++ != quote) {
        return 0;
    }
    while (isspace((unsigned char)*text)) {
        ++text;
    }
    if (*text && *text != '#') {
        return 0;
    }
    out[n] = '\0';
    return 1;
}

static int Host_ParseOptionValue(const char *section, const char *key, char *value) {
    if (!strcmp(section, "discs")) {
        int slot = !strcmp(key, "disc1") ? 0 : !strcmp(key, "disc2") ? 1 : -1;
        if (slot >= 0) {
            char decoded[4096];
            if (!Host_String(value, decoded, sizeof(decoded))) {
                return 0;
            }
            memcpy(host_options.discs[slot], decoded, strlen(decoded) + 1);
        }
        return 1;
    }

    int v = 0;
    if (!strncmp(value, "true", 4) &&
        (!value[4] || isspace((unsigned char)value[4]) || value[4] == '#')) {
        v = 1;
    } else if (!strncmp(value, "false", 5) &&
               (!value[5] || isspace((unsigned char)value[5]) || value[5] == '#')) {
        v = 0;
    } else {
        char *end = NULL;
        errno = 0;
        long parsed = strtol(value, &end, 10);
        end = Host_Trim(end);
        if (errno || end == value || (*end && *end != '#') || parsed < -1 || parsed > INT_MAX) {
            return 0;
        }
        v = (int)parsed;
    }
    /* -1 only marks an unbound controller speed binding. */
    if (v < 0 && (strcmp(section, "controller") || strcmp(key, host_button_names[HOST_SPEED_BINDING]))) {
        return 0;
    }

    if (!strcmp(section, "controls")) {
        if (!strcmp(key, "field_run_threshold")) {
            if (v > 100) return 0;
            host_options.analog_run_threshold = (unsigned)v;
        }
    } else if (!strcmp(section, "graphics")) {
        if (!strcmp(key, "internal_resolution")) {
            if (v < 1 || v > 8) {
                return 0;
            }
            host_options.scale = (unsigned)v;
        } else if (!strcmp(key, "fullscreen")) {
            if (v > 1) {
                return 0;
            }
            host_options.fullscreen = v;
        } else if (!strcmp(key, "square_pixels")) {
            if (v > 1) {
                return 0;
            }
            host_options.aspect = v;
        } else if (!strcmp(key, "disable_dithering")) {
            if (v > 1) {
                return 0;
            }
            host_options.dither = v;
        } else if (!strcmp(key, "antialiasing")) {
            if (v > 1) return 0;
            host_options.antialiasing = v;
        } else if (!strcmp(key, "model_filtering")) {
            if (v > 1) return 0;
            host_options.model_filtering = v;
        } else if (!strcmp(key, "geometry_correction")) {
            if (v > 1) {
                return 0;
            }
            host_options.geometry_correction = v;
        }
    } else if (!strcmp(section, "audio") && !strcmp(key, "volume")) {
        if (v > 100) {
            return 0;
        }
        host_options.volume = (unsigned)v;
    } else if (!strcmp(section, "game") && !strcmp(key, "speed")) {
        if (v < 2 || v > PSYZ_SPEED_MAX) {
            return 0;
        }
        host_options.speed_factor = (unsigned)v;
    } else if (!strcmp(section, "keyboard") || !strcmp(section, "controller")) {
        for (unsigned i = 0; i < HOST_BINDING_COUNT; ++i) {
            if (!strcmp(key, host_button_names[i])) {
                int speed = i == HOST_SPEED_BINDING;
                if (!strcmp(section, "keyboard")) {
                    if (!(speed && v == HOST_UNBOUND_KEY) &&
                        (v <= SDL_SCANCODE_UNKNOWN || v >= SDL_SCANCODE_COUNT || v == SDL_SCANCODE_ESCAPE)) {
                        return 0;
                    }
                    host_options.keys[i] = v;
                } else {
                    if (!(speed && v == HOST_UNBOUND_BUTTON) && (v < 0 || v > SDL_GAMEPAD_BUTTON_COUNT + 1)) {
                        return 0;
                    }
                    host_options.buttons[i] = v;
                }
            }
        }
    }
    return 1;
}

static void Host_LoadOptions(const char *path) {
    FILE *file = fopen(path, "r");
    if (!file) {
        return;
    }

    char line[9000];
    char section[64] = "";
    unsigned number = 0;
    while (fgets(line, sizeof(line), file)) {
        ++number;
        char *s = Host_Trim(line);
        if (!*s || *s == '#') {
            continue;
        }
        if (*s == '[') {
            char *end = strchr(s, ']');
            if (!end || (size_t)(end - s - 1) >= sizeof(section)) {
                fprintf(stderr, "%s:%u: invalid preference\n", path, number);
                continue;
            }
            *end = '\0';
            snprintf(section, sizeof(section), "%s", Host_Trim(s + 1));
            continue;
        }

        char *end = strchr(s, '=');
        if (!end) {
            fprintf(stderr, "%s:%u: invalid preference\n", path, number);
            continue;
        }
        *end = '\0';
        char *key = Host_Trim(s);
        char *value = Host_Trim(end + 1);
        if (!Host_ParseOptionValue(section, key, value)) {
            fprintf(stderr, "%s:%u: invalid preference\n", path, number);
        }
    }
    fclose(file);
}

static void Host_WriteString(FILE *file, const char *value) {
    fputc('"', file);
    for (const unsigned char *s = (const unsigned char *)value; *s; ++s) {
        if (*s == '\\' || *s == '"') {
            fputc('\\', file);
        }
        if (*s == '\n') {
            fputs("\\n", file);
        } else if (*s == '\r') {
            fputs("\\r", file);
        } else if (*s == '\t') {
            fputs("\\t", file);
        } else {
            fputc(*s, file);
        }
    }
    fputc('"', file);
}

static int Host_SaveOptions(void) {
    if (!host_options.enabled || !*host_options.user_file) {
        return 0;
    }

    char temporary[4120];
    snprintf(temporary, sizeof(temporary), "%s.tmp", host_options.user_file);
    FILE *file = fopen(temporary, "w");
    if (!file) {
        return 0;
    }

    fprintf(
        file,
        "# Parasite Eve desktop preferences\n"
        "[graphics]\n"
        "internal_resolution = %u\n"
        "fullscreen = %s\n"
        "square_pixels = %s\n"
        "disable_dithering = %s\n"
        "geometry_correction = %s\n"
        "antialiasing = %s\n"
        "model_filtering = %s\n\n"
        "[audio]\n"
        "volume = %u\n\n"
        "[game]\n"
        "speed = %u\n\n"
        "[controls]\n"
        "field_run_threshold = %u\n\n"
        "[discs]\n"
        "disc1 = ",
        host_options.scale,
        host_options.fullscreen ? "true" : "false",
        host_options.aspect ? "true" : "false",
        host_options.dither ? "true" : "false",
        host_options.geometry_correction ? "true" : "false",
        host_options.antialiasing ? "true" : "false",
        host_options.model_filtering ? "true" : "false",
        host_options.volume,
        host_options.speed_factor,
        host_options.analog_run_threshold
    );
    Host_WriteString(file, host_options.discs[0]);
    fputs("\ndisc2 = ", file);
    Host_WriteString(file, host_options.discs[1]);

    fputs("\n\n[keyboard]\n", file);
    for (unsigned i = 0; i < HOST_BINDING_COUNT; ++i) {
        fprintf(file, "%s = %d\n", host_button_names[i], host_options.keys[i]);
    }
    fputs("\n[controller]\n", file);
    for (unsigned i = 0; i < HOST_BINDING_COUNT; ++i) {
        fprintf(file, "%s = %d\n", host_button_names[i], host_options.buttons[i]);
    }

    int failed = ferror(file);
    if (fclose(file)) {
        failed = 1;
    }
    if (failed || !SDL_RenamePath(temporary, host_options.user_file)) {
        SDL_RemovePath(temporary);
        return 0;
    }
    return 1;
}

/* Bundled desktop launches have no useful writable working directory.
 * Keep PS1 card files alongside the per-user preferences, without changing
 * relative image paths or the working directory used by headless replays. */
static int Host_CardPath(char *dst, const char *src, int capacity) {
    if (strlen(src) < 5 || src[0] != 'b' || src[1] != 'u' || src[4] != ':' ||
        src[2] < '0' || src[2] > '1' || src[3] < '0' || src[3] > '3') {
        return -1;
    }
    const char *slash = strrchr(host_options.user_file, '/');
    const char *backslash = strrchr(host_options.user_file, '\\');
    if (backslash && (!slash || backslash > slash)) slash = backslash;
    if (!slash) {
        return -1;
    }
    if (capacity <= 0) {
        return 0;
    }

    int n = snprintf(
        dst,
        (size_t)capacity,
        "%.*s/memory-cards/%.4s",
        (int)(slash - host_options.user_file),
        host_options.user_file,
        src
    );
    if (n < 0 || n >= capacity) { dst[0] = '\0'; return 0; }
#ifdef _WIN32
    /* SDL's Win32 calls also need the extended prefix for long paths. Leave
     * short portable paths unchanged, including their usual slash spelling. */
    if (n >= 200 && dst[1] == ':' && (dst[2] == '/' || dst[2] == '\\')) {
        if (n + 4 >= capacity) { dst[0] = '\0'; return 0; }
        memmove(dst + 4, dst, n + 1); memcpy(dst, "\\\\?\\", 4); n += 4;
    } else if (n >= 200 && dst[0] == '\\' && dst[1] == '\\' && strncmp(dst, "\\\\?\\", 4)) {
        if (n + 6 >= capacity) { dst[0] = '\0'; return 0; }
        memmove(dst + 8, dst + 2, n - 1); memcpy(dst, "\\\\?\\UNC\\", 8); n += 6;
    }
    if (!strncmp(dst, "\\\\?\\", 4)) {
        for (char *p = dst; *p; ++p) if (*p == '/') *p = '\\';
        /* SDL's recursive mkdir treats a leading UNC separator as an empty
         * parent. Create ancestors individually, preserving the volume root. */
        char *parent = dst + 4;
        if (!strncmp(parent, "UNC\\", 4)) {
            parent = strchr(parent + 4, '\\');
            if (parent) parent = strchr(parent + 1, '\\');
            if (parent) ++parent;
        } else if (strlen(parent) >= 3 && parent[1] == ':') parent += 3;
        for (char *p = parent; p && *p; ++p) if (*p == '\\') {
            *p = '\0';
            int created = SDL_CreateDirectory(dst);
            *p = '\\';
            if (!created) { dst[0] = '\0'; return 0; }
        }
    }
#endif
    if (!SDL_CreateDirectory(dst)) { dst[0] = '\0'; return 0; }

    const char *name = src + 5;
    if (*name == '*') {
        name = "";
    }
    char separator = '/';
#ifdef _WIN32
    if (!strncmp(dst, "\\\\?\\", 4)) separator = '\\';
#endif
    int added = snprintf(dst + n, (size_t)(capacity - n), "%c%s", separator, name);
    if (added < 0 || added >= capacity - n) {
        dst[0] = '\0';
    }
    return 0;
}

/* Import once, retaining every legacy file and never replacing a newer card.
 * A completed-import marker prevents deleted slots reappearing at next launch. */
typedef struct HostCardImport {
    int ok;
    unsigned count;
    char *created[128];
} HostCardImport;

static SDL_EnumerationResult SDLCALL Host_ImportCard(void *userdata, const char *dirname, const char *name) {
    HostCardImport *import = userdata;
    if (strlen(name) != 20 || strncmp(name, "BASLUS-00662", 11)) return SDL_ENUM_CONTINUE;
    char source[8192], target[4096], device[64], temporary[4160];
    snprintf(source, sizeof(source), "%s%s", dirname, name);
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(source, &info) || info.type != SDL_PATHTYPE_FILE || info.size != 8192)
        return SDL_ENUM_CONTINUE;
    /* Port is encoded by the legacy directory supplied to EnumerateDirectory. */
    size_t length = strlen(dirname);
    unsigned port = length >= 4 && dirname[length - 3] == '1';
    snprintf(device, sizeof(device), "bu%u0:%s", port, name);
    Host_CardPath(target, device, sizeof(target));
    if (!*target) { import->ok = 0; return SDL_ENUM_CONTINUE; }
    if (SDL_GetPathInfo(target, &info)) return SDL_ENUM_CONTINUE;
    if (import->count >= 128) { import->ok = 0; return SDL_ENUM_CONTINUE; }
    size_t bytes = 0;
    void *data = SDL_LoadFile(source, &bytes);
    if (!data || bytes != 8192) { SDL_free(data); import->ok = 0; return SDL_ENUM_CONTINUE; }
    const char *last = strrchr(target, '/');
    const char *backslash = strrchr(target, '\\');
    if (backslash && (!last || backslash > last)) last = backslash;
    snprintf(temporary, sizeof(temporary), "%.*s.pe-save-import-%llu.tmp",
             last ? (int)(last - target + 1) : 0, target, (unsigned long long)SDL_GetTicksNS());
    SDL_IOStream *io = SDL_IOFromFile(temporary, "wbx");
    if (!io) { SDL_free(data); import->ok = 0; return SDL_ENUM_CONTINUE; }
    int written = SDL_WriteIO(io, data, bytes) == bytes && SDL_FlushIO(io);
    if (!SDL_CloseIO(io)) written = 0;
    SDL_free(data);
    if (!written || !SDL_RenamePath(temporary, target)) {
        SDL_RemovePath(temporary);
        import->ok = 0;
    } else {
        char *created = SDL_strdup(target);
        if (!created) { SDL_RemovePath(target); import->ok = 0; }
        else import->created[import->count++] = created;
    }
    return SDL_ENUM_CONTINUE;
}

static int Host_InstallCardPaths(const char *base) {
    char marker[4096], card[4096];
    if (Host_CardPath(card, "bu00:*", sizeof(card)) || !*card) return 0;
    snprintf(marker, sizeof(marker), "%.*s.legacy-imported", (int)strlen(card) - 5, card);
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(marker, &info)) {
        HostCardImport import = {.ok = 1};
        char *cwd = SDL_GetCurrentDirectory();
        const char *roots[] = {cwd, base};
        for (unsigned root = 0; root < 2; ++root) if (roots[root]) {
            for (unsigned port = 0; port < 2; ++port) {
                char source[8192];
                snprintf(source, sizeof(source), "%sbu%u0", roots[root], port);
                if (SDL_GetPathInfo(source, &info) && info.type == SDL_PATHTYPE_DIRECTORY &&
                    !SDL_EnumerateDirectory(source, Host_ImportCard, &import)) import.ok = 0;
            }
        }
        SDL_free(cwd);
        if (import.ok && !SDL_SaveFile(marker, "", 0)) {
            SDL_RemovePath(marker);
            import.ok = 0;
        }
        for (unsigned i = 0; i < import.count; ++i) {
            if (!import.ok) SDL_RemovePath(import.created[i]);
            SDL_free(import.created[i]);
        }
        if (!import.ok) return 0;
    }
    Psyz_AdjustPathCB(Host_CardPath);
    return 1;
}

static void Host_InitOptions(int enabled) {
    host_options.enabled = enabled;
    host_options.scale = 1;
    host_options.geometry_correction = 1;
    host_options.antialiasing = 0;
    host_options.model_filtering = 0;
    host_options.volume = 100;
    host_options.speed_factor = 2;
    host_options.analog_run_threshold = 85;
    Boot_InitHostBindings();
    for (unsigned i = 0; i < 16; ++i) {
        host_options.keys[i] = Psyz_GetKeyboardBinding_SDL3(i);
        host_options.buttons[i] = Psyz_GetGamepadBinding_SDL3(i);
    }
    host_options.keys[HOST_SPEED_BINDING] = HOST_UNBOUND_KEY;
    host_options.buttons[HOST_SPEED_BINDING] = HOST_UNBOUND_BUTTON;
    if (!enabled) {
        return;
    }

    const char *base = SDL_GetBasePath();
    char path[4096];
    if (base) {
        snprintf(path, sizeof(path), "%sparasite-eve.toml", base);
        Host_LoadOptions(path);
#ifdef __APPLE__
        /* SDL returns Contents/Resources for a bundled macOS executable. */
        if (strstr(base, ".app/Contents/Resources/")) {
            snprintf(path, sizeof(path), "%s../../../parasite-eve.toml", base);
            Host_LoadOptions(path);
        }
#endif
    }

    /* Development/portable launches may keep the file in the working directory. */
    if (!base || strcmp(base, "./")) {
        Host_LoadOptions("parasite-eve.toml");
    }

    const char *override = SDL_GetEnvironmentVariable(SDL_GetEnvironment(), "PE_CONFIG_HOME");
    if (override && *override) {
        SDL_CreateDirectory(override);
        snprintf(host_options.user_file, sizeof(host_options.user_file), "%s/parasite-eve.toml", override);
    } else {
#if defined(__linux__) && !defined(__ANDROID__)
        const char *root = SDL_GetEnvironmentVariable(SDL_GetEnvironment(), "XDG_CONFIG_HOME");
        if (root && *root) {
            snprintf(path, sizeof(path), "%s/parasite-eve", root);
        } else {
            root = SDL_GetEnvironmentVariable(SDL_GetEnvironment(), "HOME");
            if (root) {
                snprintf(path, sizeof(path), "%s/.config/parasite-eve", root);
            }
        }
        if (root) {
            SDL_CreateDirectory(path);
            snprintf(host_options.user_file, sizeof(host_options.user_file), "%s/parasite-eve.toml", path);
        }
#else
        char *pref = SDL_GetPrefPath("", "Parasite Eve");
        if (pref) {
            snprintf(host_options.user_file, sizeof(host_options.user_file), "%sparasite-eve.toml", pref);
            SDL_free(pref);
        }
#endif
    }

    if (*host_options.user_file) {
        Host_LoadOptions(host_options.user_file);
    }
    if (*host_options.user_file && !Host_InstallCardPaths(base)) {
        fprintf(stderr, "Cannot prepare user memory cards; legacy cards remain in use.\n");
    }
}

static void Host_ApplyOptions(SDL_Window *window) {
    g_FieldAnalogRunThreshold = host_options.analog_run_threshold;
    Psyz_VideoSetInternalResolution(host_options.scale);
    Psyz_VideoSetAspectMode((PsyzAspectMode)host_options.aspect);
    Psyz_VideoSetDitheringMode((PsyzDitherMode)host_options.dither);
    Psyz_VideoSetGeometryCorrection(host_options.geometry_correction != 0);
    Psyz_VideoSetAntialiasing(host_options.antialiasing != 0);
    Psyz_VideoSetModelFiltering(host_options.model_filtering != 0);
    Psyz_AudioSetOutputVolume(host_options.volume);
    for (unsigned i = 0; i < 16; ++i) {
        Psyz_SetKeyboardBinding_SDL3(i, (SDL_Scancode)host_options.keys[i]);
        Psyz_SetGamepadBinding_SDL3(i, host_options.buttons[i]);
    }
    if (window) {
        Psyz_SetFullscreen_SDL3(host_options.fullscreen != 0);
    }
}

typedef struct {
    SDL_AtomicInt done;
    char *paths[2];
    int failed;
} HostDiscDialog;

static void SDLCALL Host_DiscChosen(void *userdata, const char *const *files, int filter) {
    HostDiscDialog *dialog = userdata;
    (void)filter;
    if (!files) {
        dialog->failed = 1;
    } else {
        for (unsigned i = 0; files[i]; ++i) {
            if (i >= 2) {
                dialog->failed = 1;
                break;
            }
            if (strlen(files[i]) >= sizeof(host_options.discs[i])) {
                dialog->failed = 1;
                break;
            }
            dialog->paths[i] = SDL_strdup(files[i]);
            if (!dialog->paths[i]) {
                dialog->failed = 1;
            }
        }
    }
    SDL_SetAtomicInt(&dialog->done, 1);
}

static int Host_ChooseDiscs(const char **paths) {
    static const SDL_DialogFileFilter filters[] = {
        {"Parasite Eve - select both Disc 1 and Disc 2 CUE images", "cue;bin"}
    };
    HostDiscDialog dialog = {0};
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        return -1;
    }
    SDL_ShowOpenFileDialog(Host_DiscChosen, &dialog, NULL, filters, 1, NULL, true);
    while (!SDL_GetAtomicInt(&dialog.done)) {
        SDL_PumpEvents();
        SDL_Delay(10);
    }

    int result = dialog.failed ? -1 : dialog.paths[0] ? 1 : 0;
    if (result == 1) {
        for (unsigned i = 0; i < 2; ++i) {
            snprintf(
                host_options.discs[i],
                sizeof(host_options.discs[i]),
                "%s",
                dialog.paths[i] ? dialog.paths[i] : ""
            );
            paths[i] = *host_options.discs[i] ? host_options.discs[i] : NULL;
        }
    }
    for (unsigned i = 0; i < 2; ++i) {
        SDL_free(dialog.paths[i]);
    }
    return result;
}

/* Main owns the two streams; adding an absent volume never replaces one that
 * the original asynchronous archive reader might still be using. */
static PeDisc *host_images;

static int Host_SelectImage(unsigned identity, char *path, size_t size) {
    static const SDL_DialogFileFilter filters[] = {
        {"Parasite Eve USA CUE/BIN image", "cue;bin"}
    };
    HostDiscDialog dialog = {0};
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        return -1;
    }
    SDL_PropertiesID props = SDL_CreateProperties();
    if (!props) {
        return -1;
    }

    char title[100];
    snprintf(title, sizeof(title), "Parasite Eve - select USA Disc %u", identity);
    SDL_SetPointerProperty(props, SDL_PROP_FILE_DIALOG_FILTERS_POINTER, (void *)filters);
    SDL_SetNumberProperty(props, SDL_PROP_FILE_DIALOG_NFILTERS_NUMBER, 1);
    SDL_SetBooleanProperty(props, SDL_PROP_FILE_DIALOG_MANY_BOOLEAN, false);
    SDL_SetStringProperty(props, SDL_PROP_FILE_DIALOG_TITLE_STRING, title);
    SDL_SetPointerProperty(props, SDL_PROP_FILE_DIALOG_WINDOW_POINTER, Psyz_GetWindow_SDL3());
    SDL_ShowFileDialogWithProperties(SDL_FILEDIALOG_OPENFILE, Host_DiscChosen, &dialog, props);
    while (!SDL_GetAtomicInt(&dialog.done)) {
        SDL_PumpEvents();
        SDL_Delay(10);
    }
    SDL_DestroyProperties(props);

    int result = dialog.failed ? -1 : dialog.paths[0] ? 1 : 0;
    if (result == 1) {
        if (strlen(dialog.paths[0]) >= size) {
            result = -1;
        } else {
            snprintf(path, size, "%s", dialog.paths[0]);
        }
    }
    SDL_free(dialog.paths[0]);
    SDL_free(dialog.paths[1]);
    return result;
}

static int Host_InstallImage(unsigned identity, const char *path) {
    if (identity < 1 || identity > 2 || !host_images || g_CdImageVolumes[identity - 1]) {
        return 0;
    }
    unsigned slot = host_images[0].stream ? 1 : 0;
    if (host_images[slot].stream) {
        return 0;
    }

    PeDisc *image = &host_images[slot];
    unsigned found = 0;
    if (!pe_disc_open(image, path)) {
        return 0;
    }

    /* Inspection deliberately leaves the bound image and original tables alone. */
    if (Asset_ValidatePeImage(image, &found) || found != identity) {
        pe_disc_close(image);
        return 0;
    }

    g_CdImageVolumes[identity - 1] = image;
    snprintf(host_options.discs[identity - 1], sizeof(host_options.discs[identity - 1]), "%s", path);
    Host_SaveOptions();
    return 1;
}

static int Host_AddImage(unsigned identity, int required) {
    if (identity < 1 || identity > 2 || !host_images) {
        return 0;
    }
    if (g_CdImageVolumes[identity - 1]) {
        return 1;
    }

    Psyz_PauseHostClock_SDL3(true);
    int paused = Psyz_AudioIsPaused();
    Psyz_AudioPause();
    int result = 0;

    char title[80];
    char message[300];
    char path[4096];
    snprintf(title, sizeof(title), "Parasite Eve - Disc %u required", identity);
    snprintf(
        message,
        sizeof(message),
        "Select the Parasite Eve (USA) Disc %u CUE or BIN image. The current image will remain mounted.",
        identity
    );
    if (required) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, title, message, Psyz_GetWindow_SDL3());
    }

    for (;;) {
        int selected = Host_SelectImage(identity, path, sizeof(path));
        if (selected > 0 && Host_InstallImage(identity, path)) {
            result = 1;
            break;
        }
        if (selected > 0) {
            snprintf(
                message,
                sizeof(message),
                "This is not a valid Parasite Eve (USA) Disc %u image. Please select Disc %u.",
                identity,
                identity
            );
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title, message, Psyz_GetWindow_SDL3());
            continue;
        }
        if (!required) {
            break;
        }
        snprintf(
            message,
            sizeof(message),
            "Parasite Eve (USA) Disc %u is needed to continue. Choose its image, or quit the game. Unsaved progress is lost if you quit.",
            identity
        );
        SDL_MessageBoxButtonData buttons[] = {
            {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Choose image"},
            {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Quit game"}
        };
        SDL_MessageBoxData box = {
            .flags = SDL_MESSAGEBOX_WARNING,
            .window = Psyz_GetWindow_SDL3(),
            .title = title,
            .message = message,
            .numbuttons = 2,
            .buttons = buttons
        };
        int choice = 0;
        if (!SDL_ShowMessageBox(&box, &choice) || choice != 1) {
            break;
        }
    }

    SDL_ResetKeyboard();
    Psyz_PauseHostClock_SDL3(false);
    if (!paused) {
        Psyz_AudioUnpause();
    }
    return result;
}

static int Host_RequestImage(unsigned identity) {
    return Host_AddImage(identity, 1);
}

static void Host_OverlayRenderFallback(
    SDL_Renderer *renderer,
    SDL_Window *window,
    HostOverlayState *state,
    SDL_Texture **texture,
    int *texW,
    int *texH
) {
    int w = 0, h = 0;
    OverlayLayout layout;
    SDL_GetWindowSizeInPixels(window, &w, &h);
    if (w <= 0) {
        w = 640;
    }
    if (h <= 0) {
        h = 600;
    }
    OverlayComputeLayout(state->page, w, h, &layout);
    s_overlayLayout = layout;
    if (OverlayFontReady() && OverlaySurfaceResize(&s_overlaySurface, layout.w, layout.h)) {
        OverlayPaint(&s_overlaySurface, state, &layout);
        if (*texture == NULL || *texW != layout.w || *texH != layout.h) {
            if (*texture != NULL) {
                SDL_DestroyTexture(*texture);
            }
            *texture = SDL_CreateTexture(
                renderer,
                SDL_PIXELFORMAT_RGBA32,
                SDL_TEXTUREACCESS_STREAMING,
                layout.w,
                layout.h
            );
            *texW = layout.w;
            *texH = layout.h;
            if (*texture != NULL) {
                SDL_SetTextureBlendMode(*texture, SDL_BLENDMODE_BLEND_PREMULTIPLIED);
            }
        }
        if (*texture != NULL) {
            SDL_UpdateTexture(*texture, NULL, s_overlaySurface.pixels, layout.w * 4);
        }
    }
    SDL_SetRenderDrawColor(renderer, 12, 14, 18, 255);
    SDL_RenderClear(renderer);
    if (*texture != NULL) {
        SDL_FRect dst = {
            (float)layout.x, (float)layout.y, (float)layout.w, (float)layout.h
        };
        SDL_RenderTexture(renderer, *texture, NULL, &dst);
    }
    SDL_RenderPresent(renderer);
}

static void Host_SwitchSettingsPage(HostOverlayState *state, int nextPage) {
    int normalized = (nextPage % OVERLAY_PAGE_COUNT + OVERLAY_PAGE_COUNT) % OVERLAY_PAGE_COUNT;
    if (state->page != normalized) {
        state->page = normalized;
        state->row = 0;
        state->scroll = 0;
        state->bind_device = HOST_BIND_KEYBOARD;
        state->capturing_binding = 0;
        state->status = NULL;
        state->dirty = 1;
    }
}

static void Host_ApplySettingsAction(
    HostOverlayState *state,
    int action,
    int activate_only,
    SDL_Window *game
) {
    int pureCount = Host_SettingsPureRowCount(state->page);
    if (state->row >= pureCount) {
        if (activate_only) {
            if (state->row == pureCount) {
                state->open = 0;
            } else {
                state->quit = 1;
                state->open = 0;
            }
        }
        return;
    }

    int declCount = 0;
    const OverlaySetting *settings = OverlayPageSettings(state->page, &declCount);
    if (settings != NULL && state->row >= 0 && state->row < declCount) {
        const OverlaySetting *setting = &settings[state->row];
        int index = setting->get();
        if (state->page == OVERLAY_PAGE_AUDIO) {
            index = ClampIndex(index + action, setting->count);
        } else {
            index = (index + action + setting->count) % setting->count;
        }
        setting->set(index);
    } else if (state->page == HOST_PAGE_IMAGES) {
        if (!activate_only) {
            return;
        }
        if (g_CdImageVolumes[state->row]) {
            state->status = "This image is already available.";
        } else {
            state->status = Host_AddImage((unsigned)state->row + 1, 0)
                                ? "Image added and remembered."
                                : "No image added.";
        }
    } else if (state->page == HOST_PAGE_CONTROLS && state->row == HOST_ANALOG_RUN_ROW) {
        int threshold = (int)host_options.analog_run_threshold + action * 5;
        if (threshold < 0) threshold = 0;
        if (threshold > 100) threshold = 100;
        host_options.analog_run_threshold = (unsigned)threshold;
    } else if (state->row == 0) {
        state->bind_device = (state->bind_device == HOST_BIND_CONTROLLER)
                                 ? HOST_BIND_KEYBOARD
                                 : HOST_BIND_CONTROLLER;
    } else if (state->row == HOST_SPEED_BINDING + 1 && !activate_only) {
        int factor = (int)host_options.speed_factor + action;
        if (factor < 2 || factor > PSYZ_SPEED_MAX) {
            return;
        }
        host_options.speed_factor = (unsigned)factor;
    } else if (activate_only) {
        state->capturing_binding = 1;
        state->status = NULL;
    } else {
        return;
    }
    state->dirty = 1;
    Host_ApplyOptions(game);
}

/* Only the speed binding can be left unbound. */
static void Host_ClearSpeedBinding(HostOverlayState *state) {
    if (state->page != HOST_PAGE_CONTROLS || state->row != HOST_SPEED_BINDING + 1) {
        return;
    }
    if (state->bind_device == HOST_BIND_KEYBOARD) {
        host_options.keys[HOST_SPEED_BINDING] = HOST_UNBOUND_KEY;
    } else {
        host_options.buttons[HOST_SPEED_BINDING] = HOST_UNBOUND_BUTTON;
    }
    state->status = NULL;
    state->dirty = 1;
}

/* The headless control console uses the same actions as the actual menu. */
static void Host_ControlGraphics(int geometry, int antialiasing, int filtering) {
    const int values[3] = {geometry, antialiasing, filtering};
    HostOverlayState state = {.open=1, .page=HOST_PAGE_GRAPHICS};
    for (int i=0;i<3;++i) {
        state.row=4+i;
        if (kGraphics[state.row].get()!=values[i])
            Host_ApplySettingsAction(&state, 1, 0, NULL);
    }
}

static void Host_ProcessSettingsPointer(
    HostOverlayState *state,
    float windowX,
    float windowY,
    int click,
    SDL_Window *game,
    SDL_Window *fallbackWindow
) {
    SDL_Window *targetWin = fallbackWindow ? fallbackWindow : game;
    float density = targetWin ? SDL_GetWindowPixelDensity(targetWin) : 1.0f;
    if (density <= 0.0f) {
        density = 1.0f;
    }
    float scale = s_overlayLayout.scale > 0.0f ? s_overlayLayout.scale : 1.0f;
    float x = (windowX * density - (float)s_overlayLayout.x) / scale;
    float y = (windowY * density - (float)s_overlayLayout.y) / scale;

    for (int p = 0; p < OVERLAY_PAGE_COUNT; p++) {
        OverlayRect tab = OverlayTabRect(p);
        if (x >= tab.x && x < tab.x + tab.w && y >= tab.y && y < tab.y + tab.h) {
            if (click) {
                Host_SwitchSettingsPage(state, p);
            }
            return;
        }
    }

    int totalRows = Host_SettingsTotalRowCount(state->page);
    int hitRow = -1;
    for (int r = 0; r < totalRows; r++) {
        OverlayRect rect = OverlayRowRect(state, r);
        if (x >= rect.x && x < rect.x + rect.w && y >= rect.y && y < rect.y + rect.h) {
            hitRow = r;
            break;
        }
    }
    if (hitRow >= 0 && state->row != hitRow) {
        state->row = hitRow;
        state->dirty = 1;
    }
    if (!click) {
        return;
    }
    int designH = OverlayDesignHeight(Host_SettingsVisibleRowCount(state->page));
    if (x < 0.0f || x >= (float)OVERLAY_PANEL_W || y < 0.0f || y >= (float)designH) {
        state->open = 0;
        return;
    }
    if (state->page == HOST_PAGE_CONTROLS && hitRow == HOST_SPEED_BINDING + 1 &&
        x < (float)(OVERLAY_PAD + OVERLAY_SPEED_STEP_W)) {
        int action = x < (float)(OVERLAY_PAD + OVERLAY_SPEED_STEP_CENTRE) ? -1 : 1;
        Host_ApplySettingsAction(state, action, 0, game);
        return;
    }
    if (hitRow >= 0) {
        int pureCount = Host_SettingsPureRowCount(state->page);
        int isStepper = hitRow < pureCount &&
                        !(state->page == HOST_PAGE_CONTROLS && hitRow >= 1 && hitRow != HOST_ANALOG_RUN_ROW) &&
                        state->page != HOST_PAGE_IMAGES;
        int action = isStepper ? (x < OverlayValueCentre() ? -1 : 1) : 1;
        Host_ApplySettingsAction(state, action, !isStepper, game);
    }
}

static void Host_AnswerPrompt(HostOverlayState *state, int skip) {
    state->skip = skip;
    state->open = 0;
}

/* Row 0 Yes, 1 No; -1 inside the panel between rows, -2 outside it. */
static int Host_PromptRowAt(SDL_Window *game, float windowX, float windowY) {
    float density = game ? SDL_GetWindowPixelDensity(game) : 1.0f;
    if (density <= 0.0f) {
        density = 1.0f;
    }
    float scale = s_overlayLayout.scale > 0.0f ? s_overlayLayout.scale : 1.0f;
    float x = (windowX * density - (float)s_overlayLayout.x) / scale;
    float y = (windowY * density - (float)s_overlayLayout.y) / scale;

    if (x < 0.0f || x >= (float)PROMPT_PANEL_W || y < 0.0f ||
        y >= (float)OverlayPromptDesignHeight()) {
        return -2;
    }
    for (int r = 0; r < PROMPT_ROWS; r++) {
        OverlayRect rect = OverlayPromptRowRect(r);
        if (x >= rect.x && x < rect.x + rect.w && y >= rect.y && y < rect.y + rect.h) {
            return r;
        }
    }
    return -1;
}

/* Skip-movie question: Esc, Circle or a click outside keep watching. */
static void Host_ProcessPromptEvent(
    HostOverlayState *state,
    const SDL_Event *event,
    SDL_Window *game
) {
    if (event->type == SDL_EVENT_KEY_DOWN) {
        int repeat = event->key.repeat != 0;
        switch (event->key.scancode) {
        case SDL_SCANCODE_ESCAPE:
            if (!repeat) {
                Host_AnswerPrompt(state, 0);
            }
            break;
        case SDL_SCANCODE_UP:
        case SDL_SCANCODE_DOWN:
        case SDL_SCANCODE_W:
        case SDL_SCANCODE_S:
            state->row ^= 1;
            state->dirty = 1;
            break;
        case SDL_SCANCODE_RETURN:
        case SDL_SCANCODE_KP_ENTER:
        case SDL_SCANCODE_SPACE:
            if (!repeat) {
                Host_AnswerPrompt(state, state->row == 0);
            }
            break;
        default:
            break;
        }
    } else if (event->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
        switch (event->gbutton.button) {
        case SDL_GAMEPAD_BUTTON_DPAD_UP:
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
            state->row ^= 1;
            state->dirty = 1;
            break;
        case SDL_GAMEPAD_BUTTON_SOUTH:
        case SDL_GAMEPAD_BUTTON_START:
            Host_AnswerPrompt(state, state->row == 0);
            break;
        case SDL_GAMEPAD_BUTTON_EAST:
            Host_AnswerPrompt(state, 0);
            break;
        default:
            break;
        }
    } else if (event->type == SDL_EVENT_MOUSE_MOTION) {
        int row = Host_PromptRowAt(game, event->motion.x, event->motion.y);
        if (row >= 0 && row != state->row) {
            state->row = row;
            state->dirty = 1;
        }
    } else if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
               event->button.button == SDL_BUTTON_LEFT) {
        int row = Host_PromptRowAt(game, event->button.x, event->button.y);
        if (row >= 0) {
            Host_AnswerPrompt(state, row == 0);
        } else if (row == -2) {
            Host_AnswerPrompt(state, 0);
        }
    }
}

static void Host_ProcessSettingsEvent(
    HostOverlayState *state,
    const SDL_Event *event,
    SDL_Window *game,
    SDL_Window *fallbackWindow
) {
    if (event->type == SDL_EVENT_QUIT) {
        state->quit = 1;
        state->open = 0;
        return;
    }
    if (event->type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
        if (fallbackWindow && event->window.windowID == SDL_GetWindowID(fallbackWindow)) {
            state->open = 0;
        } else {
            state->quit = 1;
            state->open = 0;
        }
        return;
    }
    if (state->prompt) {
        Host_ProcessPromptEvent(state, event, game);
        return;
    }

    if (state->capturing_binding) {
        if (event->type == SDL_EVENT_KEY_DOWN && event->key.scancode == SDL_SCANCODE_ESCAPE) {
            state->capturing_binding = 0;
            state->dirty = 1;
            return;
        }
        int binding = -1;
        if (state->bind_device == HOST_BIND_KEYBOARD &&
            event->type == SDL_EVENT_KEY_DOWN && !event->key.repeat) {
            binding = event->key.scancode;
        } else if (state->bind_device == HOST_BIND_CONTROLLER &&
                   event->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
            binding = event->gbutton.button;
        } else if (state->bind_device == HOST_BIND_CONTROLLER &&
                   event->type == SDL_EVENT_GAMEPAD_AXIS_MOTION &&
                   event->gaxis.value > 16000) {
            if (event->gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER) {
                binding = SDL_GAMEPAD_BUTTON_COUNT;
            } else if (event->gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) {
                binding = SDL_GAMEPAD_BUTTON_COUNT + 1;
            }
        }
        if (binding >= 0 &&
            (state->bind_device == HOST_BIND_CONTROLLER || binding > SDL_SCANCODE_UNKNOWN)) {
            int keyboard = state->bind_device == HOST_BIND_KEYBOARD;
            int *bindings = keyboard ? host_options.keys : host_options.buttons;
            int target = state->row - 1;
            int unbound = keyboard ? HOST_UNBOUND_KEY : HOST_UNBOUND_BUTTON;
            state->capturing_binding = 0;
            state->dirty = 1;
            /* Swap duplicates, keeping every original button reachable. An
             * unbound speed binding has nothing to give back in a swap. */
            for (int i = 0; i < HOST_BINDING_COUNT; ++i) {
                if (i != target && bindings[i] == binding && bindings[target] == unbound) {
                    static char message[64];
                    snprintf(message, sizeof(message), "Already used by %s.", host_button_names[i]);
                    state->status = message;
                    return;
                }
            }
            for (int i = 0; i < HOST_BINDING_COUNT; ++i) {
                if (i != target && bindings[i] == binding) {
                    bindings[i] = bindings[target];
                }
            }
            bindings[target] = binding;
            Host_ApplyOptions(game);
        }
        return;
    }

    int totalRows = Host_SettingsTotalRowCount(state->page);
    if (event->type == SDL_EVENT_KEY_DOWN) {
        int repeat = event->key.repeat != 0;
        switch (event->key.scancode) {
        case SDL_SCANCODE_ESCAPE:
            if (!repeat) {
                state->open = 0;
            }
            break;
        case SDL_SCANCODE_TAB:
        case SDL_SCANCODE_PAGEDOWN:
            if (!repeat) {
                Host_SwitchSettingsPage(state, state->page + 1);
            }
            break;
        case SDL_SCANCODE_PAGEUP:
            if (!repeat) {
                Host_SwitchSettingsPage(state, state->page - 1);
            }
            break;
        case SDL_SCANCODE_UP:
            state->row = (state->row + totalRows - 1) % totalRows;
            Host_EnsureControlsRowVisible(state);
            state->dirty = 1;
            break;
        case SDL_SCANCODE_DOWN:
            state->row = (state->row + 1) % totalRows;
            Host_EnsureControlsRowVisible(state);
            state->dirty = 1;
            break;
        case SDL_SCANCODE_LEFT:
            if (!repeat) {
                Host_ApplySettingsAction(state, -1, 0, game);
            }
            break;
        case SDL_SCANCODE_RIGHT:
            if (!repeat) {
                Host_ApplySettingsAction(state, 1, 0, game);
            }
            break;
        case SDL_SCANCODE_RETURN:
        case SDL_SCANCODE_KP_ENTER:
        case SDL_SCANCODE_SPACE:
            if (!repeat) {
                Host_ApplySettingsAction(state, 1, 1, game);
            }
            break;
        case SDL_SCANCODE_DELETE:
        case SDL_SCANCODE_BACKSPACE:
            Host_ClearSpeedBinding(state);
            break;
        default:
            break;
        }
    } else if (event->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
        switch (event->gbutton.button) {
        case SDL_GAMEPAD_BUTTON_EAST:
        case SDL_GAMEPAD_BUTTON_START:
            state->open = 0;
            break;
        case SDL_GAMEPAD_BUTTON_WEST:
            Host_ClearSpeedBinding(state);
            break;
        case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
            Host_SwitchSettingsPage(state, state->page - 1);
            break;
        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
            Host_SwitchSettingsPage(state, state->page + 1);
            break;
        case SDL_GAMEPAD_BUTTON_DPAD_UP:
            state->row = (state->row + totalRows - 1) % totalRows;
            Host_EnsureControlsRowVisible(state);
            state->dirty = 1;
            break;
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
            state->row = (state->row + 1) % totalRows;
            Host_EnsureControlsRowVisible(state);
            state->dirty = 1;
            break;
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
            Host_ApplySettingsAction(state, -1, 0, game);
            break;
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
            Host_ApplySettingsAction(state, 1, 0, game);
            break;
        case SDL_GAMEPAD_BUTTON_SOUTH:
            Host_ApplySettingsAction(state, 1, 1, game);
            break;
        default:
            break;
        }
    } else if (event->type == SDL_EVENT_MOUSE_WHEEL) {
        if (state->page == OVERLAY_PAGE_CONTROLS && event->wheel.y != 0.0f) {
            int step = event->wheel.y > 0.0f ? -1 : 1;
            if (event->wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
                step = -step;
            }
            int nextScroll = Host_ClampControlsScroll(state->scroll + step);
            if (nextScroll != state->scroll) {
                state->scroll = nextScroll;
                state->dirty = 1;
            }
        }
    } else if (event->type == SDL_EVENT_MOUSE_MOTION) {
        Host_ProcessSettingsPointer(state, event->motion.x, event->motion.y, 0, game, fallbackWindow);
    } else if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
               event->button.button == SDL_BUTTON_LEFT) {
        Host_ProcessSettingsPointer(state, event->button.x, event->button.y, 1, game, fallbackWindow);
    }
}

static void Host_Settings(SDL_Window *game) {
    if (game) {
        host_options.fullscreen = (SDL_GetWindowFlags(game) & SDL_WINDOW_FULLSCREEN) != 0;
    }

    int useGpuOverlay = (game != NULL && s_overlayGpuDevice != NULL);
    SDL_Window *fallbackWindow = NULL;
    SDL_Renderer *fallbackRenderer = NULL;
    SDL_Texture *fallbackTexture = NULL;
    int fallbackTexW = 0, fallbackTexH = 0;

    if (!useGpuOverlay) {
        fallbackWindow = SDL_CreateWindow(
            "Parasite Eve - Settings", 640, 600, SDL_WINDOW_HIGH_PIXEL_DENSITY
        );
        if (!fallbackWindow) {
            return;
        }
        fallbackRenderer = SDL_CreateRenderer(fallbackWindow, NULL);
        if (!fallbackRenderer) {
            SDL_DestroyWindow(fallbackWindow);
            return;
        }
        SDL_PumpEvents();
        SDL_FlushEvents(SDL_EVENT_WINDOW_FIRST, SDL_EVENT_WINDOW_LAST);
    }

    Psyz_PauseHostClock_SDL3(true);
    int paused = Psyz_AudioIsPaused();
    Psyz_AudioPause();
    /* The panel draws at normal speed; the menu takes the hold's release. */
    Host_ResetSpeedHold();

    s_overlayState = (HostOverlayState){
        .open = 1,
        .page = HOST_PAGE_GRAPHICS,
        .row = 0,
        .bind_device = HOST_BIND_KEYBOARD,
        .capturing_binding = 0,
        .quit = 0,
        .dirty = 1,
        .status = NULL
    };

    while (s_overlayState.open) {
        if (useGpuOverlay) {
            Host_OverlayRefreshGPU(s_overlayGpuDevice, game, &s_overlayState);
            Psyz_VideoVSync(0);
        } else {
            Host_OverlayRenderFallback(
                fallbackRenderer,
                fallbackWindow,
                &s_overlayState,
                &fallbackTexture,
                &fallbackTexW,
                &fallbackTexH
            );
            SDL_Event event;
            if (SDL_WaitEventTimeout(&event, 20)) {
                Host_ProcessSettingsEvent(&s_overlayState, &event, game, fallbackWindow);
            }
        }
    }

    int quit = s_overlayState.quit;
    s_overlayState.open = 0;

    if (!Host_SaveOptions()) {
        SDL_ShowSimpleMessageBox(
            SDL_MESSAGEBOX_ERROR,
            "Parasite Eve",
            "Could not save preferences. Changes apply for this session.",
            fallbackWindow ? fallbackWindow : game
        );
    }
    if (fallbackTexture != NULL) {
        SDL_DestroyTexture(fallbackTexture);
    }
    if (fallbackRenderer != NULL) {
        SDL_DestroyRenderer(fallbackRenderer);
    }
    if (fallbackWindow != NULL) {
        SDL_DestroyWindow(fallbackWindow);
    }

    /* Avoid a held menu key leaking into the resumed original pad poll. */
    SDL_ResetKeyboard();
    Psyz_PauseHostClock_SDL3(false);
    if (!paused) {
        Psyz_AudioUnpause();
    }
    if (quit) {
        SDL_Event event;
        SDL_zero(event);
        event.type = SDL_EVENT_QUIT;
        SDL_PushEvent(&event);
    }
}

/* g_BootMovieSkipConfirm: the movie, game clock and audio stay paused until
 * the question is answered. No keeps watching. */
static int Host_ConfirmMovieSkip(void) {
    SDL_Window *game = Psyz_GetWindow_SDL3();
    int paused = Psyz_AudioIsPaused();
    int skip = 0;
    int quit = 0;

    Psyz_PauseHostClock_SDL3(true);
    Psyz_AudioPause();
    Host_ResetSpeedHold();
    if (game != NULL && s_overlayGpuDevice != NULL) {
        s_overlayState = (HostOverlayState){
            .open = 1,
            .row = 1,
            .dirty = 1,
            .prompt = 1
        };
        while (s_overlayState.open) {
            Host_OverlayRefreshGPU(s_overlayGpuDevice, game, &s_overlayState);
            Psyz_VideoVSync(0);
        }
        skip = s_overlayState.skip;
        quit = s_overlayState.quit;
        s_overlayState = (HostOverlayState){0};
    } else {
        SDL_MessageBoxButtonData buttons[] = {
            {0, 1, "Yes"},
            {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT | SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "No"}
        };
        SDL_MessageBoxData box = {
            .flags = SDL_MESSAGEBOX_INFORMATION,
            .window = game,
            .title = "Parasite Eve - Skip cutscene",
            .message = "Do you want to skip the cutscene?",
            .numbuttons = 2,
            .buttons = buttons
        };
        int choice = 0;
        skip = SDL_ShowMessageBox(&box, &choice) && choice == 1;
    }

    /* Avoid the answering key leaking into the resumed original pad poll. */
    SDL_ResetKeyboard();
    Psyz_PauseHostClock_SDL3(false);
    if (!paused) {
        Psyz_AudioUnpause();
    }
    if (quit) {
        SDL_Event event;
        SDL_zero(event);
        event.type = SDL_EVENT_QUIT;
        SDL_PushEvent(&event);
    }
    return skip;
}

static void Host_PauseForFocus(SDL_Window *game) {
    int paused = Psyz_AudioIsPaused();
    Psyz_PauseHostClock_SDL3(true);
    Psyz_AudioPause();

    SDL_Event pending;
    while (SDL_WaitEvent(&pending)) {
        if (pending.type == SDL_EVENT_WINDOW_FOCUS_GAINED &&
            pending.window.windowID == SDL_GetWindowID(game)) {
            break;
        }
        if (pending.type == SDL_EVENT_QUIT ||
            (pending.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
             pending.window.windowID == SDL_GetWindowID(game))) {
            SDL_PushEvent(&pending);
            break;
        }
    }

    SDL_ResetKeyboard();
    /* A release while unfocused is never seen: drop back to 1x. */
    Host_ResetSpeedHold();
    Psyz_PauseHostClock_SDL3(false);
    if (!paused) {
        Psyz_AudioUnpause();
    }
}

/* The game runs at speed_factor while the bound key, button or trigger is held. */
static bool Host_SpeedHoldEvent(const SDL_Event *event) {
    int key = host_options.keys[HOST_SPEED_BINDING];
    int button = host_options.buttons[HOST_SPEED_BINDING];
    SDL_JoystickID device = 0;
    int held = 0, consumed = 0;
    /* The Mods panel can reset PsyZ's speed while consuming release events. */
    if (Psyz_VideoGetSpeed() != host_speed) Host_ResetSpeedHold();
    if (event->type == SDL_EVENT_KEY_DOWN || event->type == SDL_EVENT_KEY_UP) {
        if (key == HOST_UNBOUND_KEY || (int)event->key.scancode != key) return false;
        if (!event->key.repeat) host_speed_key_held = event->type == SDL_EVENT_KEY_DOWN;
        consumed = 1;
    } else if (event->type == SDL_EVENT_GAMEPAD_REMOVED) {
        for (unsigned i = 0; i < 16; ++i)
            if (host_speed_pads[i].id == event->gdevice.which)
                host_speed_pads[i].id = host_speed_pads[i].held = 0;
    } else if (event->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ||
               event->type == SDL_EVENT_GAMEPAD_BUTTON_UP) {
        if (button < 0 || event->gbutton.button != button) return false;
        device = event->gbutton.which;
        held = event->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN;
        consumed = 1;
    } else if (event->type == SDL_EVENT_GAMEPAD_AXIS_MOTION) {
        int axis = button == SDL_GAMEPAD_BUTTON_COUNT ? SDL_GAMEPAD_AXIS_LEFT_TRIGGER
                 : button == SDL_GAMEPAD_BUTTON_COUNT + 1 ? SDL_GAMEPAD_AXIS_RIGHT_TRIGGER : -1;
        if (axis < 0 || event->gaxis.axis != axis) return false;
        device = event->gaxis.which;
        int was_held = 0;
        for (unsigned i = 0; i < 16; ++i)
            if (host_speed_pads[i].id == device) was_held = host_speed_pads[i].held;
        held = event->gaxis.value > (was_held ? 8000 : 16000);
    } else return false;
    if (device) {
        unsigned slot;
        for (slot = 0; slot < 16; ++slot) if (host_speed_pads[slot].id == device) break;
        if (slot == 16 && held)
            for (slot = 0; slot < 16; ++slot) if (!host_speed_pads[slot].id) break;
        if (slot < 16) {
            host_speed_pads[slot].id = held ? device : 0;
            host_speed_pads[slot].held = held;
        }
    }
    int any = host_speed_key_held;
    for (unsigned i = 0; i < 16; ++i) any |= host_speed_pads[i].held;
    Host_SetSpeed(any ? host_options.speed_factor : 1);
    return consumed; /* axis and removal events still reach the pad backend */
}

static bool Host_SettingsEvent(const SDL_Event *event) {
    SDL_Window *game = Psyz_GetWindow_SDL3();
    if (s_overlayState.open) {
        if (event->type == SDL_EVENT_WINDOW_RESIZED ||
            event->type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED ||
            event->type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED ||
            event->type == SDL_EVENT_GAMEPAD_ADDED ||
            event->type == SDL_EVENT_GAMEPAD_REMOVED) {
            s_overlayState.dirty = 1;
            return false;
        }
        Host_ProcessSettingsEvent(&s_overlayState, event, game, NULL);
        return true;
    }
    if (game && event->type == SDL_EVENT_WINDOW_FOCUS_LOST &&
        event->window.windowID == SDL_GetWindowID(game)) {
        Host_PauseForFocus(game);
        return true;
    }
    if (event->type == SDL_EVENT_KEY_DOWN && event->key.scancode == SDL_SCANCODE_ESCAPE) {
        if (!event->key.repeat) {
            Host_Settings(game);
        }
        return true;
    }
    return Host_SpeedHoldEvent(event);
}

static void Host_InstallSettingsOverlay(void) {
    s_prevOverlayInit = Psyz_OverlayInit_SDL3GPU(Host_OverlayInitGPU);
    s_prevOverlayRender = Psyz_OverlayRender_SDL3GPU(Host_OverlayRenderGPU);
    s_prevOverlayDestroy = Psyz_OverlayDestroyCB(Host_OverlayDestroyGPU);
    Psyz_SetOverlayEventFilter_SDL3(Host_SettingsEvent);
}

#define STATE_FLAG_IMAGE_BUSY 0x00100000U
#define STATE_FLAG_READY 0x00000100U

#ifndef PE_HOST_SETTINGS_TEST
static void Host_PrintHelp(const char *program_name) {
    printf(
        "Usage: %s [--disc <USA Disc 1 CUE/BIN>] [--disc2 <USA Disc 2 CUE/BIN>] [--choose-discs] "
        "[--geometry-correction 0|1] [--antialiasing 0|1] [--model-filtering 0|1] "
        "[--headless | --headless-gpu] [--internal-resolution 1..8] [--frame-limit N] [--control-at N] "
        "[--dump-vram PATH] [--dump-frame PATH] [--dump-audio PATH] [--pad-at VBLANK:BUTTON_MASK ...]\n",
        program_name
    );
    printf("Without --disc, opens a native multi-select CUE/BIN dialog. --choose-discs selects new images. ESC opens desktop settings.\n");
    printf("--headless uses native-resolution software rendering; --headless-gpu keeps the actual GPU window hidden and supports internal scaling.\n");
    printf("--dump-frame writes the rendered display as RGB888 PPM at each control/terminal boundary.\n");
    printf("--dump-audio writes stereo 44100 Hz signed-16-bit little-endian PCM; requires headless and PSYZ_AUDIO_CLOCK=video.\n");
    printf("Headless control: --control-at N pauses for stdin commands: advance FRAMES BUTTON_MASK, graphics PGXP AA FILTER (0/1; advances 8 blanks), or quit (requires --frame-limit).\n");
    printf("Headless fixtures only: --diagnostic-hp-at VBLANK:EXPECTED_HP:VALUE (requires --frame-limit; output is marked coverage-only)\n");
}

static int Host_ParsePadEventArg(const char *spec, int argc) {
    char *end = NULL;
    char *mask_end = NULL;
    errno = 0;
    unsigned long tick = strtoul(spec, &end, 10);
    if (!tick || tick > INT_MAX || errno || *end != ':') {
        fprintf(stderr, "Invalid input frame\n");
        return 0;
    }
    unsigned long buttons = strtoul(end + 1, &mask_end, 0);
    if (mask_end == end + 1 || *mask_end || errno || buttons > 65535 ||
        (g_BootHostPadEventCount && tick < g_BootHostPadEvents[g_BootHostPadEventCount - 1].vblank)) {
        fprintf(stderr, "Invalid input buttons/order\n");
        return 0;
    }
    if (!g_BootHostPadEvents) {
        /* There cannot be more events than command-line arguments. */
        g_BootHostPadEvents = calloc((size_t)argc, sizeof(*g_BootHostPadEvents));
        if (!g_BootHostPadEvents) {
            fprintf(stderr, "Cannot allocate input schedule\n");
            return 0;
        }
    }
    g_BootHostPadEvents[g_BootHostPadEventCount++] = (BootHostPadEvent){(u32)tick, (u16)buttons};
    return 1;
}

static int Host_ParseHealthEventArg(const char *cursor, int argc) {
    unsigned long values[3];
    for (unsigned field = 0; field < 3; ++field) {
        char *end = NULL;
        errno = 0;
        if (*cursor < '0' || *cursor > '9') {
            fprintf(stderr, "Invalid diagnostic HP event\n");
            return 0;
        }
        values[field] = strtoul(cursor, &end, 10);
        if (errno ||
            (field ? values[field] > USHRT_MAX : (!values[field] || values[field] > INT_MAX)) ||
            *end != (field == 2 ? '\0' : ':')) {
            fprintf(stderr, "Invalid diagnostic HP event\n");
            return 0;
        }
        cursor = end + 1;
    }
    if (g_BootHostHealthEventCount &&
        values[0] <= g_BootHostHealthEvents[g_BootHostHealthEventCount - 1].vblank) {
        fprintf(stderr, "Diagnostic HP events must have strictly increasing frames\n");
        return 0;
    }
    if (!g_BootHostHealthEvents) {
        g_BootHostHealthEvents = calloc((size_t)argc, sizeof(*g_BootHostHealthEvents));
        if (!g_BootHostHealthEvents) {
            fprintf(stderr, "Cannot allocate diagnostic HP schedule\n");
            return 0;
        }
    }
    g_BootHostHealthEvents[g_BootHostHealthEventCount++] = (BootHostHealthEvent){
        (u32)values[0],
        (u16)values[1],
        (u16)values[2]
    };
    return 1;
}

int main(int argc, char **argv) {
    int mode;
    u32 token;
    Pe1GameState *state;
    PeDisc images[2] = {{0}};
    const char *paths[2] = {NULL, NULL};
    int result = EXIT_FAILURE;
    int interactive = 1;
    int choose_discs = 0;

    const char *environment_headless = SDL_GetEnvironmentVariable(SDL_GetEnvironment(), "PSYZ_HEADLESS");
    if (environment_headless && *environment_headless) {
        interactive = 0;
    }
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--headless") || !strcmp(argv[i], "--headless-gpu") || !strcmp(argv[i], "--help")) {
            interactive = 0;
        }
    }

    Host_InitOptions(interactive);
    unsigned internal_resolution = host_options.scale;
    int resolution_argument = 0;
    int headless_argument = 0;
    unsigned effect_arguments = 0;

    for (int arg = 1; arg < argc; ++arg) {
        if (!strcmp(argv[arg], "--help")) {
            Host_PrintHelp(argv[0]);
            return EXIT_SUCCESS;
        }
        if (!strcmp(argv[arg], "--choose-discs")) {
            choose_discs = 1;
        } else if (!strcmp(argv[arg], "--headless") || !strcmp(argv[arg], "--headless-gpu")) {
            if (headless_argument++) {
                fprintf(stderr, "Duplicate or conflicting headless mode\n");
                return EXIT_FAILURE;
            }
            const char *backend = !strcmp(argv[arg], "--headless-gpu") ? "gpu" : "1";
            if (!SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "PSYZ_HEADLESS", backend, true)) {
                return EXIT_FAILURE;
            }
        } else if (!strcmp(argv[arg], "--internal-resolution")) {
            if (resolution_argument++ || arg + 1 >= argc) {
                fprintf(stderr, "Missing or duplicate internal resolution\n");
                return EXIT_FAILURE;
            }
            char *end = NULL;
            errno = 0;
            const char *value = argv[++arg];
            unsigned long scale = strtoul(value, &end, 10);
            if (*value < '1' || *value > '9' || *end || errno || scale > PSYZ_INTERNAL_RES_MAX) {
                fprintf(stderr, "Invalid internal resolution\n");
                return EXIT_FAILURE;
            }
            internal_resolution = (unsigned)scale;
        } else if (!strcmp(argv[arg], "--geometry-correction") ||
                   !strcmp(argv[arg], "--antialiasing") ||
                   !strcmp(argv[arg], "--model-filtering")) {
            unsigned effect = !strcmp(argv[arg], "--geometry-correction") ? 1u :
                              !strcmp(argv[arg], "--antialiasing") ? 2u : 4u;
            if ((effect_arguments & effect) || arg + 1 >= argc ||
                (strcmp(argv[arg+1], "0") && strcmp(argv[arg+1], "1"))) {
                fprintf(stderr, "Missing, invalid or duplicate graphics effect (use 0 or 1)\n");
                return EXIT_FAILURE;
            }
            effect_arguments |= effect;
            int enabled = argv[++arg][0] == '1';
            if (effect == 1u) host_options.geometry_correction = enabled;
            else if (effect == 2u) host_options.antialiasing = enabled;
            else host_options.model_filtering = enabled;
        } else if (!strcmp(argv[arg], "--frame-limit")) {
            char *end = NULL;
            errno = 0;
            unsigned long limit = arg + 1 < argc ? strtoul(argv[++arg], &end, 10) : 0;
            if (!limit || limit > INT_MAX || errno || *end) {
                fprintf(stderr, "Invalid frame limit\n");
                return EXIT_FAILURE;
            }
            g_BootHostVBlankLimit = (u32)limit;
        } else if (!strcmp(argv[arg], "--control-at")) {
            char *end = NULL;
            errno = 0;
            unsigned long tick = arg + 1 < argc ? strtoul(argv[++arg], &end, 10) : 0;
            if (!tick || tick > INT_MAX || errno || *end || g_BootHostControlAt) {
                fprintf(stderr, "Invalid control boundary\n");
                return EXIT_FAILURE;
            }
            g_BootHostControlAt = (u32)tick;
        } else if (!strcmp(argv[arg], "--dump-vram")) {
            if (arg + 1 >= argc || g_BootHostVramPath) {
                fprintf(stderr, "Missing or duplicate capture path\n");
                return EXIT_FAILURE;
            }
            g_BootHostVramPath = argv[++arg];
        } else if (!strcmp(argv[arg], "--dump-frame")) {
            if (arg + 1 >= argc || g_BootHostFramePath) {
                fprintf(stderr, "Missing or duplicate rendered capture path\n");
                return EXIT_FAILURE;
            }
            g_BootHostFramePath = argv[++arg];
        } else if (!strcmp(argv[arg], "--dump-audio")) {
            if (arg + 1 >= argc || g_BootHostAudioPath) {
                fprintf(stderr, "Missing or duplicate audio capture path\n");
                return EXIT_FAILURE;
            }
            g_BootHostAudioPath = argv[++arg];
        } else if (!strcmp(argv[arg], "--pad-at")) {
            if (arg + 1 >= argc) {
                fprintf(stderr, "Invalid input schedule\n");
                return EXIT_FAILURE;
            }
            if (!Host_ParsePadEventArg(argv[++arg], argc)) {
                return EXIT_FAILURE;
            }
        } else if (!strcmp(argv[arg], "--diagnostic-hp-at")) {
            if (arg + 1 >= argc) {
                fprintf(stderr, "Invalid diagnostic HP schedule\n");
                return EXIT_FAILURE;
            }
            if (!Host_ParseHealthEventArg(argv[++arg], argc)) {
                return EXIT_FAILURE;
            }
        } else if (!strcmp(argv[arg], "--disc") || !strcmp(argv[arg], "--disc2")) {
            int slot = !strcmp(argv[arg], "--disc2");
            if (paths[slot] || arg + 1 >= argc) {
                fprintf(stderr, "Missing or duplicate image argument\n");
                return EXIT_FAILURE;
            }
            paths[slot] = argv[++arg];
        } else {
            fprintf(stderr, "Unknown argument: %s\n", argv[arg]);
            return EXIT_FAILURE;
        }
    }

    if (interactive) {
        if (!paths[0] && !paths[1]) {
            for (unsigned i = 0; i < 2; ++i) {
                if (*host_options.discs[i]) {
                    paths[i] = host_options.discs[i];
                }
            }
        }
        if (choose_discs || (!paths[0] && !paths[1])) {
            int selected = Host_ChooseDiscs(paths);
            if (selected <= 0) {
                return selected < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
            }
        }
    }
    if (!paths[0] && !paths[1]) {
        fprintf(stderr, "Supply --disc or --disc2 with a USA CUE/BIN image\n");
        return EXIT_FAILURE;
    }

    const char *headless_mode = SDL_GetEnvironmentVariable(SDL_GetEnvironment(), "PSYZ_HEADLESS");
    if ((host_options.antialiasing || host_options.model_filtering) &&
        headless_mode && !strcmp(headless_mode, "1")) {
        fprintf(stderr, "Graphics enhancements require --headless-gpu\n");
        return EXIT_FAILURE;
    }
    if (internal_resolution > 1 && headless_mode && !strcmp(headless_mode, "1")) {
        fprintf(stderr, "Software headless supports only internal resolution 1; use --headless-gpu\n");
        return EXIT_FAILURE;
    }
    if (g_BootHostVramPath && !g_BootHostVBlankLimit) {
        fprintf(stderr, "VRAM capture requires --frame-limit\n");
        return EXIT_FAILURE;
    }
    if (g_BootHostFramePath && !g_BootHostVBlankLimit) {
        fprintf(stderr, "Rendered capture requires --frame-limit\n");
        return EXIT_FAILURE;
    }
    if (g_BootHostFramePath && g_BootHostVramPath && !strcmp(g_BootHostFramePath, g_BootHostVramPath)) {
        fprintf(stderr, "Capture paths must differ\n");
        return EXIT_FAILURE;
    }
    if (g_BootHostAudioPath) {
        const char *clock = SDL_GetEnvironmentVariable(SDL_GetEnvironment(), "PSYZ_AUDIO_CLOCK");
        if (!g_BootHostVBlankLimit || !headless_mode ||
            (strcmp(headless_mode, "1") && strcmp(headless_mode, "gpu")) ||
            !clock || strcmp(clock, "video")) {
            fprintf(stderr, "Audio capture requires headless mode, --frame-limit and PSYZ_AUDIO_CLOCK=video\n");
            return EXIT_FAILURE;
        }
        if ((g_BootHostVramPath && !strcmp(g_BootHostAudioPath, g_BootHostVramPath)) ||
            (g_BootHostFramePath && !strcmp(g_BootHostAudioPath, g_BootHostFramePath))) {
            fprintf(stderr, "Capture paths must differ\n");
            return EXIT_FAILURE;
        }
    }
    if (g_BootHostControlAt) {
        const char *headless = SDL_GetEnvironmentVariable(SDL_GetEnvironment(), "PSYZ_HEADLESS");
        if (!headless || (strcmp(headless, "1") && strcmp(headless, "gpu")) ||
            !g_BootHostVBlankLimit || g_BootHostControlAt >= g_BootHostVBlankLimit) {
            fprintf(stderr, "Control requires headless mode and a later frame limit\n");
            return EXIT_FAILURE;
        }
        if (g_BootHostPadEventCount &&
            g_BootHostPadEvents[g_BootHostPadEventCount - 1].vblank > g_BootHostControlAt) {
            fprintf(stderr, "Input schedule extends beyond control boundary\n");
            return EXIT_FAILURE;
        }
    }
    if (g_BootHostHealthEventCount) {
        const char *headless = SDL_GetEnvironmentVariable(SDL_GetEnvironment(), "PSYZ_HEADLESS");
        if (!headless || strcmp(headless, "1") || !g_BootHostVBlankLimit ||
            g_BootHostHealthEvents[g_BootHostHealthEventCount - 1].vblank > g_BootHostVBlankLimit) {
            fprintf(stderr, "Diagnostic HP schedule requires headless mode and a frame limit covering every event\n");
            return EXIT_FAILURE;
        }
    }

mount_images:
    for (unsigned slot = 0; slot < 2; ++slot) {
        if (!paths[slot]) {
            continue;
        }
        if (!pe_disc_open(&images[slot], paths[slot])) {
            fprintf(stderr, "Cannot open image: %s\n", images[slot].error);
            goto image_error;
        }
        CdRom_BindImage(&images[slot], NULL);
        int mounted = Asset_OpenPeImage();
        unsigned identity = g_PeImageMountFlags;
        if (mounted || (identity != 1 && identity != 2) || g_CdImageVolumes[identity - 1]) {
            fprintf(stderr, "Invalid or duplicate USA disc image (status %d)\n", mounted);
            goto image_error;
        }
        g_CdImageVolumes[identity - 1] = &images[slot];
    }
    CdRom_BindImage(g_CdImageVolumes[0] ? g_CdImageVolumes[0] : g_CdImageVolumes[1], NULL);
    goto images_ready;

image_error:
    CdRom_BindImage(NULL, NULL);
    g_CdImageVolumes[0] = g_CdImageVolumes[1] = NULL;
    pe_disc_close(&images[0]);
    pe_disc_close(&images[1]);
    if (!interactive) {
        goto shutdown;
    }
    SDL_ShowSimpleMessageBox(
        SDL_MESSAGEBOX_ERROR,
        "Parasite Eve",
        "Cannot mount these images. Select the USA Disc 1 and Disc 2 CUE files together. Cancel to quit.",
        NULL
    );
    {
        int selected = Host_ChooseDiscs(paths);
        if (selected > 0) {
            goto mount_images;
        }
        if (!selected) {
            result = EXIT_SUCCESS;
        }
        goto shutdown;
    }

images_ready:;
    host_options.scale = internal_resolution;
    g_BootHostGraphicsOptions = Host_ControlGraphics;
    if (interactive) {
        /* Copy both before replacing picker storage: selection can be reversed. */
        char ordered[2][4096] = {{0}};
        for (unsigned i = 0; i < 2; ++i) {
            const char *path = NULL;
            for (unsigned j = 0; j < 2; ++j) {
                if (g_CdImageVolumes[i] == &images[j]) {
                    path = paths[j];
                }
            }
            snprintf(ordered[i], sizeof(ordered[i]), "%s", path ? path : "");
        }
        memcpy(host_options.discs, ordered, sizeof(ordered));
        host_images = images;
        g_CdImageRequest = Host_RequestImage;
        g_BootMovieSkipConfirm = Host_ConfirmMovieSkip;
        host_options.scale = internal_resolution;
        Host_InstallSettingsOverlay();
        Host_ApplyOptions(NULL);
        Host_SaveOptions();
    }

    g_BootHostActive = 1;
    PE_HOOK(PE_HOOK_INIT, (&(PeHookInit){interactive, host_options.user_file}));
    if (Psyz_VideoSetInternalResolution(internal_resolution) < 0) {
        fprintf(stderr, "Cannot select internal resolution\n");
        goto shutdown;
    }
    if (!interactive) {
        if (internal_resolution > 1 || (effect_arguments & 1u) ||
            host_options.antialiasing || host_options.model_filtering)
            Psyz_VideoSetGeometryCorrection(host_options.geometry_correction != 0);
        Psyz_VideoSetAntialiasing(host_options.antialiasing);
        Psyz_VideoSetModelFiltering(host_options.model_filtering);
    }
    Boot_InitSystem();
    if (Psyz_VideoGetInternalResolution() != internal_resolution) {
        fprintf(stderr, "Requested internal resolution unavailable\n");
        goto shutdown;
    }
    if (interactive) {
        SDL_Window *window = Psyz_GetWindow_SDL3();
        Host_ApplyOptions(window);
    }
    mode = 0;

    for (;;) {
        state = &g_GameState;
        Boot_SyncShutdown();

        int image_status = Asset_OpenPeImage();
        if (image_status != 0) {
            fprintf(stderr, "Game image mount failed: %d\n", image_status);
            goto shutdown;
        }

        Boot_InitGameState();
        Boot_InitSubsystems();
        int startup_status = Scene_LoadSceneData();
        if (startup_status < 0) {
            fprintf(stderr, "Scene startup failed: %d\n", startup_status);
            goto shutdown;
        }

        g_SceneDispatchToken = 0xA9400048;

        do {
            if (g_GameState.flags & STATE_FLAG_IMAGE_BUSY) {
                int display_status = Render_InitDisplayLists(mode);
                if (display_status < 0) {
                    fprintf(stderr, "Game image selection failed: %d\n", display_status);
                    goto shutdown;
                }
                g_GameState.flags &= ~STATE_FLAG_IMAGE_BUSY;
            }

            Scene_LoadFieldBg();
            token = g_SceneDispatchToken;
            g_SceneDispatchCur = token;

            switch (token) {
            case 0xA8000048: {
                int overlay_status = Boot_LoadOverlayTables();
                if (overlay_status < 0) {
                    fprintf(stderr, "Boot assets failed: %d\n", overlay_status);
                    goto shutdown;
                }
                if (FxCommon_RunFrameLoop() < 0) {
                    fprintf(stderr, "Effect scene startup failed\n");
                    goto shutdown;
                }
                g_GameState.flags |= 1;
                break;
            }
            case 0xAA108448:
                if ((g_GameState.display_list_modes & 2) == 0) {
                    mode = 2;
                    g_GameState.flags |= STATE_FLAG_IMAGE_BUSY;
                    break;
                }
                if (CdRom_LoadBootAudio() < 0) {
                    fprintf(stderr, "Boot display assets failed\n");
                    goto shutdown;
                }
                if (BootDisplay_RunDisplay() < 0) {
                    fprintf(stderr, "Boot display failed\n");
                    goto shutdown;
                }
                g_SceneDispatchToken = 0xA80830C8;
                g_GameState.flags |= 1;
                break;
            case 0xA9400048: {
                int title_status = CdRom_LoadInitialImage();
                if (title_status < 0) {
                    fprintf(stderr, "Title assets failed: %d\n", title_status);
                    goto shutdown;
                }
                Draw_InitDisplay(MenuCard_RunListScreen());
                g_GameState.flags |= 3;
                break;
            }
            default: {
                int frame_status = Boot_RunFrame();
                if (frame_status < 0) {
                    fprintf(stderr, "Scene frame startup failed: %d\n", frame_status);
                    goto shutdown;
                }
                break;
            }
            }

            if (g_SceneDispatchToken != 0xA80651C8 && g_SceneDispatchToken != 0xA8065248 &&
                g_SceneDispatchToken != 0xA80652C8 && g_SceneDispatchToken != 0xA80660C8 &&
                g_SceneDispatchToken != 0xA8066148 && g_SceneDispatchToken != 0xA80661C8 &&
                g_SceneDispatchToken != 0xA8066348) {
                if (g_PlayTimeFrameCounter < 600) {
                    if ((g_GameState.display_list_modes & 1) == 0) {
                        mode = 1;
                        g_GameState.flags |= STATE_FLAG_IMAGE_BUSY;
                    }
                } else if ((g_GameState.display_list_modes & 2) == 0) {
                    mode = 2;
                    g_GameState.flags |= STATE_FLAG_IMAGE_BUSY;
                }
            }
        } while ((state->flags & STATE_FLAG_READY) == 0);

        VSync(0);
        SetDispMask(0);
        state->flags &= ~STATE_FLAG_READY;
    }

shutdown:
    if (g_BootDisplayPlaybackActive) {
        BootDisplay_WaitForCdAndRestoreCallbacks();
    }
    Psyz_AudioDestroy();
    CdRom_BindImage(NULL, NULL);
    g_CdImageVolumes[0] = g_CdImageVolumes[1] = NULL;
    g_CdImageRequest = NULL;
    g_BootMovieSkipConfirm = NULL;
    host_images = NULL;
    pe_disc_close(&images[0]);
    pe_disc_close(&images[1]);
    free(g_BootHostHealthEvents);
    g_BootHostHealthEvents = NULL;
    g_BootHostHealthEventCount = g_BootHostHealthApplied = 0;
    free(g_BootHostPadEvents);
    g_BootHostPadEvents = NULL;
    g_BootHostPadEventCount = 0;
    return result;
}
#endif
