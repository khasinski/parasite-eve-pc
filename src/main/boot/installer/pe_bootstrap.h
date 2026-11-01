/* SPDX-License-Identifier: MIT -- original native first-run executor. */
#ifndef PE_BOOTSTRAP_H
#define PE_BOOTSTRAP_H
#include <stddef.h>
#include <stdint.h>
#define PE_BOOT_PATH 4096
typedef enum { PE_INSTALL_USE_USB, PE_INSTALL_COPY_DISCS } PeInstallMode;
typedef enum { PE_INSTALL_FAILED=-1, PE_INSTALL_CANCELLED=-2,
    PE_INSTALL_EXISTING=0, PE_INSTALL_READY=1 } PeInstallResult;
typedef struct {
    const char *data_root; /* Existing game's config/log/cards root. */
    const char *disc[2];   /* Explicit owner-selected raw BIN files. */
    const char *copy_root; /* Optional USB target; never a save/card root. */
    const char *native_hd; /* Optional already supplied native pack, not emulator pack. */
    const char *native_characters; /* Optional prepared small native character subset, USB in place. */
    PeInstallMode mode;
} PeInstallRequest;
typedef struct {
    /* Adapter validates supported game/release identity without binding globals. */
    int (*validate_disc)(const char *path, unsigned expected_identity, void *user);
    /* Return zero to cancel. This is also the native UI/event pumping seam. */
    int (*progress)(const char *phase, uint64_t done, uint64_t total, void *user);
    void *user;
    int (*validate_native_hd)(const char *path,void *user);
    int (*validate_characters)(const char *path,void *user);
} PeInstallCallbacks;
typedef struct {
    PeInstallResult result;
    char error[256];
    char config[PE_BOOT_PATH];
    char receipt[PE_BOOT_PATH];
    int config_created; /* Never remove this automatically on later failures. */
    int interrupted_config_possible;
} PeInstallReport;
/* Synchronous finite work; run before game init and call game main only on
 * READY or EXISTING. No process replacement, HTTP, downloads or game launch. */
PeInstallResult pe_bootstrap_install(const PeInstallRequest *, const PeInstallCallbacks *, PeInstallReport *);
/* Actual game adapter: Asset_ValidatePeImage + raw audio sector requirement. */
int pe_bootstrap_validate_game_disc(const char *, unsigned, void *);
#endif
