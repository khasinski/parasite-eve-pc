/* The few SDL3 calls the game modules make (save files, environment,
 * ticks, event watches), on newlib stdio for KallistiOS. SDL itself is not
 * built for the Dreamcast; the game only compiles against its headers.
 *
 * Users: src/main/memcard/save_flow.c, src/main/boot/init.c,
 * src/main/render/mesh_entry.c, src/overlays/menu_memcard/page_flip.c. */
#include <stdio.h>
#include <stdlib.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_timer.h>
#include <kos/timer.h>

struct SDL_IOStream {
    FILE *file;
};

SDL_IOStream *SDL_IOFromFile(const char *file, const char *mode) {
    if (!file || !mode) {
        return NULL;
    }
    FILE *f = fopen(file, mode);
    if (!f) {
        return NULL;
    }
    SDL_IOStream *io = malloc(sizeof(*io));
    if (!io) {
        fclose(f);
        return NULL;
    }
    io->file = f;
    return io;
}

size_t SDL_ReadIO(SDL_IOStream *io, void *ptr, size_t size) {
    return io ? fread(ptr, 1, size, io->file) : 0;
}

size_t SDL_WriteIO(SDL_IOStream *io, const void *ptr, size_t size) {
    return io ? fwrite(ptr, 1, size, io->file) : 0;
}

Sint64 SDL_GetIOSize(SDL_IOStream *io) {
    if (!io) {
        return -1;
    }
    long at = ftell(io->file);
    if (at < 0 || fseek(io->file, 0, SEEK_END) != 0) {
        return -1;
    }
    long size = ftell(io->file);
    fseek(io->file, at, SEEK_SET);
    return size;
}

bool SDL_FlushIO(SDL_IOStream *io) {
    return io && fflush(io->file) == 0;
}

bool SDL_CloseIO(SDL_IOStream *io) {
    if (!io) {
        return false;
    }
    int ok = fclose(io->file) == 0;
    free(io);
    return ok;
}

bool SDL_RemovePath(const char *path) { return path && remove(path) == 0; }

bool SDL_RenamePath(const char *oldpath, const char *newpath) {
    return oldpath && newpath && rename(oldpath, newpath) == 0;
}

/* No environment on the console: every variable is unset. */
const char *SDL_getenv(const char *name) {
    (void)name;
    return NULL;
}

SDL_Environment *SDL_GetEnvironment(void) { return NULL; }

const char *SDL_GetEnvironmentVariable(SDL_Environment *env, const char *name) {
    (void)env;
    (void)name;
    return NULL;
}

Uint64 SDL_GetTicksNS(void) { return timer_ns_gettime64(); }

/* There is no SDL event queue, so watches never fire. */
bool SDL_AddEventWatch(SDL_EventFilter filter, void *userdata) {
    (void)filter;
    (void)userdata;
    return true;
}

void SDL_RemoveEventWatch(SDL_EventFilter filter, void *userdata) {
    (void)filter;
    (void)userdata;
}
