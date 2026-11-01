#ifndef PE_MOD_HOST_H
#define PE_MOD_HOST_H

/* Mod host API: what every mod uses.
 *
 *   Mods     Each mod defines one PeMod and is listed in mods/CMakeLists.txt
 *            (PE_MOD_LIST). The host starts them in dependency order.
 *   Hooks    pe_hook_add() puts a handler on a game hook (pe_hooks.h) or a
 *            mod event. Handlers run in priority order (lower first); a
 *            handler that changes a value sees the previous handler's result.
 *   Services pe_service_publish()/pe_service_get(): a named, versioned table
 *            of functions one mod offers to others (Junk, item tables...).
 *   Events   pe_event_id() names a mod-defined event; pe_event_emit() runs its
 *            handlers. Mods announce things without knowing who listens.
 *   Files    pe_mod_path() builds a path beside the user's preferences.
 *   Saves    pe_save_register() (pe_save.h): per-save state the host saves, loads and carries.
 */

#include <stddef.h>
#include "pe_hooks.h"

/* The mod API's version: raised when something here, in pe_hooks.h, pe_mod_menu.h or
 * pe_services.h changes in a way that breaks mods written for the previous one. */
#define PE_MOD_API_VERSION 1

typedef struct PeMod {
    const char *id;               /* short, unique: "pe_rebalance" */
    const char *name;             /* shown in the Mods panel */
    const char *version;
    const char *const *requires;  /* mod ids started first; NULL-terminated, or NULL */
    int (*init)(void);            /* non-zero: failed, the mod stays off */
    void (*shutdown)(void);
} PeMod;

typedef int (*PeHookFn)(void *ctx, void *user);

enum { PE_PRIORITY_EARLY = -100, PE_PRIORITY_NORMAL = 0, PE_PRIORITY_LATE = 100 };

int pe_hook_add(int id, PeHookFn fn, void *user, int priority, const char *mod_id);

int pe_event_id(const char *name);
int pe_event_emit(int id, void *ctx);

int pe_service_publish(const char *name, int version, const void *api, const char *mod_id);
const void *pe_service_get(const char *name, int version);

int pe_mod_is_running(const char *mod_id);
/* Every mod's init runs (its F1 pages exist), but only active mods touch the game: off in Manage
 * mods, with All mods off, or needing an inactive mod, a mod's handlers, ticks, services and
 * main-menu rows are skipped. pe_mod_current(): the mod whose init is running (owner of what it
 * registers), else NULL. pe_mod_active(): NULL and the host's own are always active. */
const char *pe_mod_current(void);
int pe_mod_active(const char *mod_id);
int pe_mod_interactive(void);
/* Refresh requested-vs-started mod notes; never starts/stops a mod. */
void pe_mod_manage_refresh(void);
int pe_mod_path(char *out, size_t size, const char *file);
void pe_mod_log(const char *mod_id, const char *fmt, ...);
/* Play log (pe_play.log beside the preferences): one "tag key=value ..." row,
 * the same rows as the psxrecomp build's play log, for the analysis tools. Each row is also
 * announced as the PE_EVENT_LOG_LINE event (PeLogLine *), for loggers of their own. */
void pe_log(const char *tag, const char *fmt, ...);
#define PE_EVENT_LOG_LINE "play.log"
typedef struct PeLogLine { const char *tag, *text; } PeLogLine;
/* A one-line message over the game for a few seconds (interactive runs). */
void pe_toast(const char *text);

/* host internals shared by its own files */
/* Around every call into a mod: what crash reports name as the running mod code
 * (what: "hook", "per-frame tick", "F1 row"...; detail: which one, or NULL). */
void pe_mod_call_begin(const char *mod_id, const char *what, const char *detail);
void pe_mod_call_end(void);
void pe_menu_load_settings(void);
void pe_menu_save_settings(void);
/* Explicit UI requests only; running mod set stays unchanged until restart. */
int pe_mod_master_active_now(void);
int pe_mod_master_requested(void);
void pe_mod_master_toggle_next_launch(void);
int pe_mod_request_assists_only(void);
void pe_panel_install(void);

#endif
