#ifndef PE_DEBUG_INTERNAL_H
#define PE_DEBUG_INTERNAL_H

/* Private contract for the opt-in, singleton game-host debug channel.
 * No gameplay headers or writable transport state are exposed to command modules. */
#include <stddef.h>
#include <stdio.h>

typedef enum PeDebugArguments {
    PE_DEBUG_EXACT, PE_DEBUG_ARGS, PE_DEBUG_OPTIONAL
} PeDebugArguments;

typedef struct PeDebugCommand {
    const char *name;
    PeDebugArguments arguments;
    void (*run)(const char *args);
} PeDebugCommand;

typedef struct PeDebugModule {
    const char *name;
    const PeDebugCommand *commands;
    size_t count;
    void (*install)(void);  /* Called once, only when debug control is enabled. */
    void (*frame)(void);    /* Called after polling on each enabled field frame. */
} PeDebugModule;

void pe_debug_path(char *out, size_t size, const char *name);
void pe_debug_reply(const char *fmt, const char *command, const char *detail);
void pe_debug_json_string(FILE *f, const char *text);
unsigned pe_debug_frame(void);

/* Internal lifecycle/registry access is also used by the separately linked regression test. */
int pe_debug_control_start(const char *dir);
void pe_debug_control_poll(void);
const PeDebugModule *const *pe_debug_modules(size_t *count);

extern const PeDebugModule pe_debug_field_module;
extern const PeDebugModule pe_debug_save_module;
extern const PeDebugModule pe_debug_battle_module;
extern const PeDebugModule pe_debug_rng_module;
extern const PeDebugModule pe_debug_setup_module;
extern const PeDebugModule pe_debug_settings_module;
extern const PeDebugModule pe_debug_trace_module;

#endif
