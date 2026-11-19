/* Debug-control snapshot: everything a failed test needs to explain itself, in one JSON file.
 * A test runner asks for it on a timeout or failed check. See docs/DEBUG_CONTROL.md. */
#include "pe_debug_internal.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define PE_SNAPSHOT_VERSION 1

static void pe_ctl_snapshot(const char *args) {
    char name[64], file[80], path[1100], temp[1100];
    FILE *f;
    size_t i;
    snprintf(name, sizeof name, "%s", args && *args ? args : "");
    for (i = 0; name[i]; ++i)
        if (!isalnum((unsigned char)name[i]) && name[i] != '-' && name[i] != '_') {
            pe_debug_reply("err %s %s", "snapshot", "snapshot [NAME] (letters, digits, - and _; up to 40)");
            return;
        }
    if (i > 40) { pe_debug_reply("err %s %s", "snapshot", "snapshot [NAME] (letters, digits, - and _; up to 40)"); return; }
    snprintf(file, sizeof file, i ? "snapshot-%s.json" : "snapshot.json", name);
    pe_debug_path(path, sizeof path, file);
    snprintf(temp, sizeof temp, "%s.tmp", path);
    if (!(f = fopen(temp, "w"))) { pe_debug_reply("err %s %s", "snapshot", "can't write the file"); return; }
    fprintf(f, "{\"version\":%d,\"frame\":%u,\"name\":", PE_SNAPSHOT_VERSION, pe_debug_frame());
    pe_debug_json_string(f, name);
    fputs(",\n\"room\":", f);     pe_debug_room_write(f);
    fputs(",\n\"ui\":", f);       pe_debug_ui_write(f);
    fputs(",\n\"battle\":", f);   pe_debug_battle_write(f);
    fputs(",\n\"bag\":", f);      pe_debug_bag_write(f);
    fputs(",\n\"actors\":", f);   pe_debug_actors_write(f);
    fputs(",\n\"mods\":", f);     pe_mod_state_write_running(f);
    fputs(",\n\"history\":", f);  pe_debug_history_write(f);
    fputs("}\n", f);
    if (fclose(f)) { remove(temp); pe_debug_reply("err %s %s", "snapshot", "can't write the file"); return; }
    remove(path);                 /* Windows rename does not replace */
    if (rename(temp, path)) { remove(temp); pe_debug_reply("err %s %s", "snapshot", "can't write the file"); return; }
    pe_debug_reply("ok %s %s", "snapshot", file);
}

static const PeDebugCommand s_commands[] = {
    {"snapshot", PE_DEBUG_OPTIONAL, pe_ctl_snapshot},
};

const PeDebugModule pe_debug_snapshot_module = {
    "snapshot", s_commands, sizeof s_commands / sizeof *s_commands, NULL, NULL
};
