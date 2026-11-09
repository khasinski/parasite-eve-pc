/* Challenges: cheats and playtest assists mark the run assisted, and other mods' rows are read for
 * the Warnings row.
 *
 * No coupling to those mods, only to what every mod shares:
 *   - a switched-on toggle or choice row registered by pe_cheats or pe_assists (whichever are in
 *     the build and running), checked twice a second while a challenge is on;
 *   - any play log row whose tag starts "cheat_" (Win this battle: cheat_win, Warp: cheat_warp),
 *     the convention cheat-like actions log under.
 * The first time per load, a toast says so.
 */
#include <stdio.h>
#include <string.h>

#include "pe_challenges.h"
#include "pe_mod_host.h"
#include "pe_mod_menu.h"

#define CH_SCAN_FRAMES 30

static const char *const kCheatMods[] = {"pe_cheats", "pe_assists"};
static int s_told, s_scan_wait;

void pe_ch_assist_reset(void) { s_told = 0; s_scan_wait = 0; }

static int pe_is_cheat_mod(const char *id) {
    for (size_t i = 0; i < sizeof kCheatMods / sizeof kCheatMods[0]; ++i)
        if (id && strcmp(id, kCheatMods[i]) == 0) return 1;
    return 0;
}

/* A row's value, for saved and live rows alike (0 for actions and info rows). */
static int pe_row_value(const PeMenuRow *row) {
    switch (row->kind) {
    case PE_ROW_TOGGLE: case PE_ROW_CHOICE: case PE_ROW_INT: case PE_ROW_DYNAMIC_CHOICE:
        return row->value ? *row->value : 0;
    case PE_ROW_LIVE_INT: case PE_ROW_LIVE_CHOICE:
        return row->get ? row->get() : 0;
    default:
        return 0;
    }
}

int pe_ch_row_on(const char *mod_id, const char *label) {
    for (int i = 0; i < pe_menu_row_count(); ++i) {
        const PeMenuRow *row = pe_menu_row(i);
        if (!row || !row->mod_id || !row->label) continue;
        if (strcmp(row->mod_id, mod_id) != 0 || strcmp(row->label, label) != 0) continue;
        return pe_menu_row_active(row) && pe_row_value(row) != 0;
    }
    return 0;
}

const char *pe_ch_cheat_row_on(void) {
    for (int i = 0; i < pe_menu_row_count(); ++i) {
        const PeMenuRow *row = pe_menu_row(i);
        if (!row || !row->value || !*row->value) continue;
        if (row->kind != PE_ROW_TOGGLE && row->kind != PE_ROW_CHOICE) continue;
        if (!pe_is_cheat_mod(row->mod_id) || !pe_menu_row_active(row)) continue;
        return row->label;
    }
    return NULL;
}

static void pe_mark(const char *what) {
    const int first = !pe_ch_assisted();
    if (!g_ch_run.active) return;
    if (!(g_ch_run.flags & CH_FLAG_CHEATS))
        pe_log("challenge_assisted", "run=%08x by=%s", (unsigned)g_ch_run.run_id, what);
    g_ch_run.flags |= CH_FLAG_CHEATS;
    if (first) pe_ch_emit_assisted(what);
    if (!s_told) {
        char msg[128];
        snprintf(msg, sizeof msg, "%s with challenges on: run marked assisted", what);
        pe_toast(msg);
        s_told = 1;
    }
}

static int pe_assist_scan(void *ctx, void *user) {
    const char *label;
    (void)ctx; (void)user;
    if (!g_ch_run.active || --s_scan_wait > 0) return 0;
    s_scan_wait = CH_SCAN_FRAMES;
    if ((label = pe_ch_cheat_row_on()) != NULL) pe_mark(label);
    return 0;
}

static const struct { const char *tag, *name; } kCheatLogNames[] = {
    {"cheat_win", "Win this battle"},
    {"cheat_warp", "Warp"},
};

static int pe_assist_log_line(void *ctx, void *user) {
    const PeLogLine *line = ctx;
    const char *name = "A cheat";
    (void)user;
    if (!line || !line->tag || strncmp(line->tag, "cheat_", 6) != 0) return 0;
    for (size_t i = 0; i < sizeof kCheatLogNames / sizeof kCheatLogNames[0]; ++i)
        if (strcmp(line->tag, kCheatLogNames[i].tag) == 0) name = kCheatLogNames[i].name;
    pe_mark(name);
    return 0;
}

void pe_ch_assist_register(const char *mod_id) {
    const int log_event = pe_event_id(PE_EVENT_LOG_LINE);
    pe_hook_add(PE_HOOK_FRAME, pe_assist_scan, NULL, PE_PRIORITY_LATE, mod_id);
    pe_hook_add(PE_HOOK_BATTLE_UPDATE, pe_assist_scan, NULL, PE_PRIORITY_LATE, mod_id);
    if (log_event >= 0) pe_hook_add(log_event, pe_assist_log_line, NULL, PE_PRIORITY_NORMAL, mod_id);
}
