#ifndef PE_MOD_HOST_H
#define PE_MOD_HOST_H

#include "pe_mod_api.h"
#include "pe_hooks.h"

/* Stop the running mods before releasing the platform or mounted discs.
 * The process-exit fallback can safely call this again. */
void pe_mods_shutdown_now(void);

/* host internals shared by its own files */
/* Around every call into a mod: what crash reports name as the running mod code
 * (what: "hook", "per-frame tick", "F1 row"...; detail: which one, or NULL). */
void pe_mod_call_begin(const char *mod_id, const char *what, const char *detail);
void pe_mod_call_end(void);
void pe_menu_load_settings(void);
void pe_menu_alias_setting(const char *key, const char *legacy);
void pe_menu_save_settings(void);
/* Explicit UI requests only; running mod set stays unchanged until restart. */
int pe_mod_master_active_now(void);
int pe_mod_master_requested(void);
void pe_mod_master_toggle_next_launch(void);
int pe_mod_request_assists_only(void);
void pe_panel_install(void);

#endif
