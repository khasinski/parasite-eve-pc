/* Opt-in debug-control transport and module dispatch. Gameplay handlers live in debug/.
 * Protocol and command documentation: docs/DEBUG_CONTROL.md. */
#include "debug/pe_debug_internal.h"
#include "pe_mod_host.h"
#include "pe_hooks.h"
#include <stdlib.h>
#include <string.h>

#define PE_POLL_FRAMES 10
static char s_dir[1024];
static unsigned s_frame;

void pe_debug_json_string(FILE *f, const char *s) {
    fputc('"', f);
    for (; *s; ++s) {
        unsigned c = (unsigned char)*s;
        if (c == '"' || c == '\\') { fputc('\\', f); fputc(c, f); }
        else if (c < 32) fprintf(f, "\\u%04x", c);
        else fputc(c, f);
    }
    fputc('"', f);
}

void pe_debug_path(char *out, size_t size, const char *name) {
    snprintf(out, size, "%s/%s", s_dir, name);
}

/* What `snapshot` reports as the lead-up: commands ("> ...") and short answers, oldest dropped. */
#define PE_HISTORY 32
static struct { unsigned frame; char line[160]; } s_history[PE_HISTORY];
static unsigned s_history_next;

static void pe_debug_remember(const char *line) {
    s_history[s_history_next % PE_HISTORY].frame = s_frame;
    snprintf(s_history[s_history_next % PE_HISTORY].line, sizeof s_history[0].line, "%s", line);
    ++s_history_next;
}

void pe_debug_history_write(FILE *f) {
    unsigned start = s_history_next > PE_HISTORY ? s_history_next - PE_HISTORY : 0, i;
    fputc('[', f);
    for (i = start; i < s_history_next; ++i) {
        fprintf(f, "%s{\"frame\":%u,\"line\":", i != start ? "," : "", s_history[i % PE_HISTORY].frame);
        pe_debug_json_string(f, s_history[i % PE_HISTORY].line);
        fputc('}', f);
    }
    fputc(']', f);
}

void pe_debug_reply(const char *fmt, const char *command, const char *detail) {
    char path[1100], line[160];
    FILE *f;
    snprintf(line, sizeof line, fmt, command, detail ? detail : "");
    pe_debug_remember(line);
    pe_debug_path(path, sizeof path, "out");
    if (!(f = fopen(path, "a"))) return;
    fprintf(f, fmt, command, detail ? detail : "");
    fputc('\n', f);
    fclose(f);
}

unsigned pe_debug_frame(void) { return s_frame; }

static void pe_ctl_ping(const char *args) {
    char detail[32];
    (void)args;
    snprintf(detail, sizeof detail, "%u", s_frame);
    pe_debug_reply("ok %s %s", "ping", detail);
}
static const PeDebugCommand s_core_commands[] = {{"ping", PE_DEBUG_EXACT, pe_ctl_ping}};
static const PeDebugModule s_core_module = {"transport", s_core_commands, 1, NULL, NULL};
static const PeDebugModule *const s_modules[] = {
    &s_core_module,
    &pe_debug_field_module,
    &pe_debug_save_module,
    &pe_debug_battle_module,
    &pe_debug_rng_module,
    &pe_debug_setup_module,
    &pe_debug_settings_module,
    &pe_debug_trace_module,
    &pe_debug_ui_module,
    &pe_debug_snapshot_module,
};

const PeDebugModule *const *pe_debug_modules(size_t *count) {
    *count = sizeof s_modules / sizeof *s_modules;
    return s_modules;
}

/* A module owns parsing its arguments; the transport only matches command boundaries.
 * OPTIONAL receives NULL for an absent separator, and "" for an explicitly empty argument. */
static void pe_ctl_run(char *line) {
    size_t m, c;
    line[strcspn(line, "\r\n")] = 0;
    if (!*line) return;
    pe_log("debug_control", "command %s", line);
    {
        char remembered[160];
        snprintf(remembered, sizeof remembered, "> %s", line);
        pe_debug_remember(remembered);
    }
    for (m = 0; m < sizeof s_modules / sizeof *s_modules; ++m) {
        const PeDebugModule *module = s_modules[m];
        for (c = 0; c < module->count; ++c) {
            const PeDebugCommand *command = &module->commands[c];
            size_t n = strlen(command->name);
            if (strncmp(line, command->name, n)) continue;
            if (!line[n] && command->arguments != PE_DEBUG_ARGS) {
                command->run(command->arguments == PE_DEBUG_OPTIONAL ? NULL : "");
                return;
            }
            if (line[n] == ' ' && command->arguments != PE_DEBUG_EXACT) {
                command->run(line + n + 1);
                return;
            }
        }
    }
    pe_debug_reply("err %s %s", line, "unknown command");
}

void pe_debug_control_poll(void) {
    char path[1100], claimed[1100], line[2048];
    FILE *f;
    if (!s_dir[0]) return;
    pe_debug_path(path, sizeof path, "cmd");
    pe_debug_path(claimed, sizeof claimed, "cmd.processing");
    /* Claim before replying: a client may publish its next command as soon
     * as it sees a response. Never remove that newly published command. */
    if (rename(path, claimed)) return;
    if (!(f = fopen(claimed, "r"))) { remove(claimed); return; }
    while (fgets(line, sizeof line, f)) pe_ctl_run(line);
    fclose(f);
    remove(claimed);
}

static int pe_ctl_frame(void *ctx, void *user) {
    size_t m;
    (void)ctx; (void)user;
    ++s_frame;
    if (s_frame % PE_POLL_FRAMES == 0) pe_debug_control_poll();
    for (m = 0; m < sizeof s_modules / sizeof *s_modules; ++m)
        if (s_modules[m]->frame) s_modules[m]->frame();
    return 0;
}

int pe_debug_control_start(const char *dir) {
    size_t m;
    if (!dir || !*dir || strlen(dir) >= sizeof s_dir || s_dir[0]) return 0;
    snprintf(s_dir, sizeof s_dir, "%s", dir);
    pe_hook_add(PE_HOOK_FRAME, pe_ctl_frame, NULL, PE_PRIORITY_NORMAL, "host");
    for (m = 0; m < sizeof s_modules / sizeof *s_modules; ++m)
        if (s_modules[m]->install) s_modules[m]->install();
    pe_mod_log(NULL, "debug control on: %s", s_dir);
    return 1;
}

void pe_debug_control_install(void) {
    (void)pe_debug_control_start(getenv("PE_DEBUG_CONTROL"));
}
