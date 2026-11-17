/* Debug-control settings commands. See docs/DEBUG_CONTROL.md. */
#include "pe_debug_internal.h"
#include <stdlib.h>
#include <string.h>
#include "pe_mod_host.h"
#include "pe_mod_menu.h"


static void pe_ctl_setting(const char *args) {
    char key[200], row_key[200], detail[240], *end;
    const char *space = strrchr(args, ' ');
    long value;
    int i;
    if (!space || space == args || (size_t)(space - args) >= sizeof key) goto invalid;
    value = strtol(space + 1, &end, 10);
    if (end == space + 1 || *end || value < -2147483647L || value > 2147483647L) goto invalid;
    memcpy(key, args, space - args); key[space - args] = 0;
    for (i = 0; i < pe_menu_row_count(); ++i) {
        PeMenuRow *r = pe_menu_row(i);
        snprintf(row_key, sizeof row_key, "%s/%s", r->page, r->label);
        if (strcmp(key, row_key)) continue;
        if (r->kind == PE_ROW_TOGGLE) { if (value < 0 || value > 1) goto invalid; }
        else if (r->kind == PE_ROW_CHOICE) { if (value < 0 || (unsigned)value >= r->count) goto invalid; }
        else if (r->kind == PE_ROW_INT) { if (value < r->minimum || value > r->maximum) goto invalid; }
        else { pe_debug_reply("err %s %s", "ini-set", "only registered stored options are supported"); return; }
        if (!r->value) goto invalid;
        *r->value = (int)value;
        snprintf(detail, sizeof detail, "%s=%ld restart_required=%d", key, value,
                 !strcmp(r->page, PE_MENU_MANAGE));
        pe_debug_reply("ok %s %s", "ini-set", detail); return;
    }
    pe_debug_reply("err %s %s", "ini-set", "unknown option"); return;
invalid:
    pe_debug_reply("err %s %s", "ini-set", "ini-set PAGE/LABEL VALUE (within the option range)");
}

static void pe_ctl_mod_state(const char *name) {
    char path[1100], value[256];
    FILE *f;
    int i, found = 0;
    for (i = 0; i < pe_menu_row_count(); ++i) {
        PeMenuRow *r = pe_menu_row(i);
        if (r->mod_id && !strcmp(r->mod_id, name)) ++found;
    }
    if (!found && !pe_mod_state_count(name)) {
        pe_debug_reply("err %s %s", "mod-state", "unknown mod id, or no registered rows or state");
        return;
    }
    pe_debug_path(path, sizeof path, "out");
    if (!(f = fopen(path, "a"))) return;
    fputs("ok mod-state {\"id\":", f); pe_debug_json_string(f, name);
    fprintf(f, ",\"running\":%s,\"rows\":[", pe_mod_is_running(name) ? "true" : "false");
    found = 0;
    for (i = 0; i < pe_menu_row_count(); ++i) {
        PeMenuRow *r = pe_menu_row(i);
        if (!r->mod_id || strcmp(r->mod_id, name) || r->kind == PE_ROW_ACTION) continue;
        fprintf(f, "%s{\"page\":", found++ ? "," : ""); pe_debug_json_string(f, r->page);
        fputs(",\"label\":", f); pe_debug_json_string(f, r->label);
        /* Inactive mods' callbacks must not run. Stored values remain readable. */
        if (r->value) snprintf(value, sizeof value, "%d", *r->value);
        else if (pe_menu_row_active(r)) pe_menu_row_value_text(r, value, sizeof value);
        else snprintf(value, sizeof value, "inactive");
        fputs(",\"value\":", f); pe_debug_json_string(f, value); fputc('}', f);
    }
    fputc(']', f);
    /* Live state: only a running mod's providers are called. */
    if (pe_mod_state_count(name)) {
        fputs(",\"live\":", f);
        if (pe_mod_is_running(name)) pe_mod_state_write(f, name);
        else fputs("null", f);
    }
    fputs("}\n", f); fclose(f);
}


static const PeDebugCommand s_commands[] = {
    {"ini-set", PE_DEBUG_ARGS, pe_ctl_setting},
    {"mod-state", PE_DEBUG_ARGS, pe_ctl_mod_state},
};

const PeDebugModule pe_debug_settings_module = {
    "settings", s_commands, sizeof s_commands / sizeof *s_commands, NULL, NULL
};
