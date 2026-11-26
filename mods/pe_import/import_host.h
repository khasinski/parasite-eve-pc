#ifndef IMPORT_HOST_H
#define IMPORT_HOST_H
/* What the import mod (F1 > Import) needs from the game it runs in, as re_host.h is for the RE play
 * mod. The menu and the automatic import (pe_import.c, pe_import_job.c) are game-neutral; each game
 * supplies one adapter (PE1: hosts/pe1/import_host_pe1.c). Append new callbacks at the end. */
#include <stddef.h>
#include <stdint.h>

typedef struct ImportHost {
    uint32_t struct_size;
    /* Once, at the mod's start: hand the importer the game's own tables (PE1: its MDEC table). */
    void (*init)(void);
    /* The game's data its builder targets read, opened only when something is built: passed to
     * each build as PeImpBuild.host (PE1: a PeImpPe1Build, Aya's data and the rooms from the open
     * discs). NULL with the reason in why. */
    const void *(*open_targets)(char *why, size_t n);
    void (*close_targets)(const void *targets);
    /* Run `frame` once a frame, late (the menu polls its worker; PE1: PE_HOOK_FRAME). */
    void (*bind_frame)(const char *mod_id, int (*frame)(void *event, void *user));
} ImportHost;

/* The running game's adapter. */
const ImportHost *import_host(void);

#endif
