/* Mod host: starts the mods, owns the hook table, services and events.
 *
 * The game calls pe_hook_dispatch() from its PE_HOOK lines (pe_hooks.h).
 * PE_HOOK_INIT starts the host: settings are read, mods start in dependency
 * order, and the Mods panel is installed for interactive runs. Hooks before
 * that do nothing. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef PE_PS5_PLUGINS
#include <SDL3/SDL_log.h>
#endif

#include "pe_game.h"
#include "pe_main_menu.h"
#include "pe_mod_host.h"
#include "pe_mod_menu.h"
#include "pe_pages.h"
#include "pe_present.h"
#include "pe_save.h"

#define PE_INV_CMD_START_NEW_GAME 1117   /* the EX Game's start (Inv_DispatchCommand) */

extern const PeMod *const g_pe_mods[]; /* generated: pe_mod_list.c */

enum { PE_MAX_HANDLERS = 256, PE_MAX_EVENTS = 64, PE_MAX_SERVICES = 32, PE_MAX_MODS = 32 };

typedef struct {
    int id, priority, order;
    PeHookFn fn;
    void *user;
    const char *mod_id;
    int active;      /* its mod is active (set when the mods have started) */
} PeHandler;

typedef struct {
    const char *name;
    int version;
    const void *api;
    const char *mod_id;
} PeService;

static PeHandler s_handlers[PE_MAX_HANDLERS];
static int s_handler_count, s_handler_order;
static const char *s_events[PE_MAX_EVENTS];
static int s_event_count;
static PeService s_services[PE_MAX_SERVICES];
static int s_service_count;
static const PeMod *s_running[PE_MAX_MODS];
static int s_running_count;
static int s_started, s_interactive;
static char s_prefs_dir[1024];
static FILE *s_log;

/* ---- files and log ---- */

int pe_mod_path(char *out, size_t size, const char *file) {
    int n = snprintf(out, size, "%s%s", s_prefs_dir, file);
    return n > 0 && (size_t)n < size ? 0 : -1;
}

void pe_mod_log(const char *mod_id, const char *fmt, ...) {
    char line[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    fprintf(stderr, "[mods] %s: %s\n", mod_id ? mod_id : "host", line);
    if (s_log) {
        fprintf(s_log, "%s: %s\n", mod_id ? mod_id : "host", line);
        fflush(s_log);
    }
}

void pe_log(const char *tag, const char *fmt, ...) {
    static FILE *play;
    static int tried, event = -1;
    char path[1100], text[1100];
    va_list args;
    PeLogLine line;
    va_start(args, fmt);
    vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    if (!play && !tried) {
        tried = 1;
#ifdef PE_PS5_PLUGINS
        /* Keep play events in the same opt-in session as CRT and mod output. */
        (void)path;
#else
        if (pe_mod_path(path, sizeof(path), "pe_play.log") == 0) { play = fopen(path, "a"); }
#endif
    }
    if (play) {
        fprintf(play, "%s %s\n", tag, text);
        fflush(play);
    }
#ifdef PE_PS5_PLUGINS
    fprintf(stderr, "[play] %s %s\n", tag, text);
#endif
    if (event < 0) { event = pe_event_id(PE_EVENT_LOG_LINE); }
    line.tag = tag;
    line.text = text;
    pe_event_emit(event, &line);                 /* e.g. the detailed play log copies it */
}

int pe_mod_interactive(void) { return s_interactive; }

/* ---- what mod code is running, for crash reports ----
 * Each call into a mod (start, shutdown, a hook or event handler, a per-frame tick, an F1 row)
 * is marked here, innermost last: two stores per call. */
enum { PE_MAX_CALLS = 8 };
static struct { const char *mod_id, *what, *detail; } s_calls[PE_MAX_CALLS];
static int s_call_depth;

void pe_mod_call_begin(const char *mod_id, const char *what, const char *detail) {
    if (s_call_depth < PE_MAX_CALLS) {
        s_calls[s_call_depth].mod_id = mod_id;
        s_calls[s_call_depth].what = what;
        s_calls[s_call_depth].detail = detail;
    }
    s_call_depth++;
}

void pe_mod_call_end(void) {
    if (s_call_depth > 0) s_call_depth--;
}

/* ---- hooks and events ---- */

int pe_hook_add(int id, PeHookFn fn, void *user, int priority, const char *mod_id) {
    int i;
    if (!fn || id < 0 || id >= PE_HOOK_COUNT + s_event_count || s_handler_count >= PE_MAX_HANDLERS)
        return -1;
    /* Keep the table sorted by (id, priority, registration order). */
    for (i = s_handler_count; i > 0; i--) {
        PeHandler *prev = &s_handlers[i - 1];
        if (prev->id < id || (prev->id == id && prev->priority <= priority)) break;
        s_handlers[i] = *prev;
    }
    s_handlers[i] = (PeHandler){id, priority, s_handler_order++, fn, user, mod_id, 1};
    s_handler_count++;
    return 0;
}

int pe_hook_dispatch(int id, void *ctx);

int pe_event_id(const char *name) {
    int i;
    for (i = 0; i < s_event_count; i++)
        if (strcmp(s_events[i], name) == 0) return PE_HOOK_COUNT + i;
    if (s_event_count >= PE_MAX_EVENTS) return -1;
    s_events[s_event_count] = name;
    return PE_HOOK_COUNT + s_event_count++;
}

int pe_event_emit(int id, void *ctx) {
    return id >= PE_HOOK_COUNT ? pe_hook_dispatch(id, ctx) : -1;
}

/* ---- services ---- */

int pe_service_publish(const char *name, int version, const void *api, const char *mod_id) {
    if (pe_service_get(name, version) || s_service_count >= PE_MAX_SERVICES) return -1;
    s_services[s_service_count++] = (PeService){name, version, api, mod_id};
    pe_mod_log(mod_id, "service %s v%d", name, version);
    return 0;
}

/* An inactive mod's services aren't offered: its code must not run through another mod. */
const void *pe_service_get(const char *name, int version) {
    int i;
    for (i = 0; i < s_service_count; i++)
        if (s_services[i].version == version && strcmp(s_services[i].name, name) == 0)
            return pe_mod_active(s_services[i].mod_id) ? s_services[i].api : NULL;
    return NULL;
}

/* ---- start-up ----
 * Every mod's init runs, so its F1 pages are there to browse and set up; but only ACTIVE mods
 * touch the game: the handlers, per-frame ticks, services and main-menu rows of an inactive mod
 * (off in Manage mods, All mods off, or a mod it needs inactive) are skipped. Mods' inits only
 * register, so the game plays as the original with every mod inactive. */
static const char *s_inited_ids[PE_MAX_MODS];
static int s_inited_count;
static const char *s_current_mod;          /* the mod whose init is running */

int pe_mod_is_running(const char *mod_id) {
    int i;
    if (!mod_id) return 0;
    for (i = 0; i < s_running_count; i++)
        if (strcmp(s_running[i]->id, mod_id) == 0) return 1;
    return 0;
}

const char *pe_mod_current(void) { return s_current_mod; }

/* The host's own (NULL, "host", "menu") are always active; a mod while it is starting too. */
int pe_mod_active(const char *mod_id) {
    int i;
    if (!mod_id || !s_started) return 1;
    for (i = 0; i < s_inited_count; i++)
        if (strcmp(s_inited_ids[i], mod_id) == 0) return pe_mod_is_running(mod_id);
    return 1;
}

static int pe_inited(const char *mod_id) {
    int i;
    for (i = 0; i < s_inited_count; i++) if (strcmp(s_inited_ids[i], mod_id) == 0) return 1;
    return 0;
}

static int pe_requires_inited(const PeMod *mod) {
    const char *const *req;
    for (req = mod->requires; req && *req; req++)
        if (!pe_inited(*req)) return 0;
    return 1;
}

static int pe_requires_met(const PeMod *mod) {
    const char *const *req;
    for (req = mod->requires; req && *req; req++)
        if (!pe_mod_is_running(*req)) return 0;
    return 1;
}

static void pe_shutdown_mods(void) {
    while (s_running_count > 0) {
        const PeMod *mod = s_running[--s_running_count];
        if (mod->shutdown) {
            pe_mod_call_begin(mod->id, "shutdown", NULL);
            mod->shutdown();
            pe_mod_call_end();
        }
    }
    if (s_log) fclose(s_log);
    s_log = NULL;
}

void pe_mods_shutdown_now(void){pe_shutdown_mods();}

/* ---- Manage mods (F1): every built-in mod, on or off from the next start ----
 * "All mods" first, OFF by default: until a player turns it on, no mod starts and the game plays
 * as the original (only F1 and this page are there, to turn it on). Then a toggle per mod. All are
 * saved in pe_mods.ini like any row and read before the mods start, so a mod turned off doesn't
 * start at all (nor do the mods that need it). */
static int s_all_on;
static int s_mod_on[PE_MAX_MODS];
static char s_mod_note[PE_MAX_MODS][192];
static char s_all_note[192];
static int s_manage_count, s_manage_ready, s_boot_all;
static int s_boot_mod_on[PE_MAX_MODS], s_boot_state[PE_MAX_MODS];
enum { PE_MOD_RUNNING, PE_MOD_OFF, PE_MOD_NEEDS, PE_MOD_FAILED, PE_MOD_ALL_OFF };
static int pe_manage_requested(int i, unsigned visiting) {
    const char *const *req; int j;
    if(!s_all_on || !s_mod_on[i] || (visiting & (1u << i)))return 0;
    visiting |= 1u << i;
    for(req=g_pe_mods[i]->requires;req && *req;req++) {
        for(j=0;j<s_manage_count;j++)if(!strcmp(*req,g_pe_mods[j]->id))break;
        if(j==s_manage_count || !pe_manage_requested(j,visiting))return 0;
    }
    return 1;
}
void pe_mod_manage_refresh(void) {
    static int logged_all=-1,logged_mods[PE_MAX_MODS];
    static unsigned log_budget=64;
    static const char *const now[]={"running","Off","dependency blocked","failed to start","Off (master was Off)"};
    int i,pending=0,changed;
    if(!s_manage_ready)return;
    changed=logged_all!=s_all_on;
    pending=s_all_on!=s_boot_all;
    for(i=0;i<s_manage_count;i++) {
        if(s_mod_on[i]!=s_boot_mod_on[i])pending=1;
        if(s_mod_on[i]!=logged_mods[i])changed=1;
    }
    snprintf(s_all_note,sizeof(s_all_note),"Master now: %s. Next launch: %s. %s",s_boot_all?"On":"Off",s_all_on?"On":"Off",pending?"Restart game to apply.":"No pending change.");
    for(i=0;i<s_manage_count;i++) {
        int requested=pe_manage_requested(i,0);
        snprintf(s_mod_note[i],sizeof(s_mod_note[i]),"%s %s. Now: %s. Next launch: %s. %s",g_pe_mods[i]->id,g_pe_mods[i]->version,now[s_boot_state[i]],requested?"enabled":(!s_all_on?"master Off":(!s_mod_on[i]?"Off":"dependency blocked")),pending?"Restart game to apply.":"Startup activation; settings do not hot-start mods.");
    }
#ifdef PE_PS5_PLUGINS
    if(changed && log_budget) {
        --log_budget;
        SDL_Log("[PS5 mods] master_now=%d master_next_launch=%d restart_required=%d activation=startup-only prefs=pe_mods.ini",s_boot_all,s_all_on,pending);
        for(i=0;i<s_manage_count;i++)SDL_Log("[PS5 mods] id=%s startup_state=%d requested_toggle=%d next_dependencies_enabled=%d",g_pe_mods[i]->id,s_boot_state[i],s_mod_on[i],pe_manage_requested(i,0));
    }
#else
    (void)changed;(void)log_budget;
#endif
    logged_all=s_all_on;for(i=0;i<s_manage_count;i++)logged_mods[i]=s_mod_on[i];
}

static void pe_manage_register(int mod_count) {
    int i;
    pe_menu_top_page(PE_MENU_MANAGE, "Turn mods on or off. Changes apply the next time the game starts.", 100000);
    pe_menu_begin(PE_MENU_MANAGE);
#if defined(PE_PS5_PLUGINS) && defined(PE_PS5_GPU_NATIVE)
    s_all_on = 1; /* Temporary native debug default ONLY; restored explicit Off wins. */
#else
    s_all_on = 0;
#endif
    s_manage_count=mod_count;
    pe_menu_register_toggle(0, "All mods", s_all_note, &s_all_on, NULL);
    for (i = 0; i < mod_count; i++) {
        s_mod_on[i] = 1;
#ifdef PE_PS5_PLUGINS
        /* Balance changes require an explicit per-mod choice on PS5. */
        if (!strcmp(g_pe_mods[i]->id, "pe_rebalance")) s_mod_on[i] = 0;
#endif
        pe_menu_register_toggle(0, g_pe_mods[i]->name, s_mod_note[i], &s_mod_on[i], NULL);
    }
    pe_menu_begin(0);
}

/* Startup state remains immutable; requested values are refreshed while paused. */
static void pe_manage_notes(int mod_count, const int *state) {
    int i;s_boot_all=s_all_on;
    for(i=0;i<mod_count;i++){s_boot_mod_on[i]=s_mod_on[i];s_boot_state[i]=state[i];}
    s_manage_ready=1;pe_mod_manage_refresh();
}

static void pe_start_mods(void) {
    int mod_count = 0, tried[PE_MAX_MODS] = {0}, state[PE_MAX_MODS] = {0}, progress = 1, i;

    while (g_pe_mods[mod_count] && mod_count < PE_MAX_MODS) mod_count++;
    pe_manage_register(mod_count);
    if (!s_all_on) pe_mod_log(NULL, "All mods off (Manage mods): pages browsable, the game plays as the original");
    /* Init every mod once the mods it needs have, in that order; active only if on and every mod
     * it needs is active. */
    while (progress) {
        progress = 0;
        for (i = 0; i < mod_count; i++) {
            const PeMod *mod = g_pe_mods[i];
            int ok;
            if (tried[i] || !pe_requires_inited(mod)) continue;
            tried[i] = progress = 1;
            s_current_mod = mod->id;
            pe_mod_call_begin(mod->id, "start", NULL);
            ok = !mod->init || mod->init() == 0;
            pe_mod_call_end();
            s_current_mod = NULL;
            if (!ok) { state[i] = PE_MOD_FAILED; pe_mod_log(mod->id, "init failed, mod off"); continue; }
            s_inited_ids[s_inited_count++] = mod->id;
            if (!s_all_on) state[i] = PE_MOD_ALL_OFF;
            else if (!s_mod_on[i]) state[i] = PE_MOD_OFF;
            else if (!pe_requires_met(mod)) state[i] = PE_MOD_NEEDS;
            else {
                s_running[s_running_count++] = mod;
                pe_mod_log(mod->id, "started %s %s", mod->name, mod->version);
                continue;
            }
            pe_mod_log(mod->id, "inactive (%s): pages only", state[i] == PE_MOD_NEEDS ? "a mod it needs is off" : "off");
        }
    }
    for (i = 0; i < mod_count; i++)
        if (!tried[i]) {
            state[i] = PE_MOD_NEEDS;
            pe_mod_log(g_pe_mods[i]->id, "missing a required mod, mod off");
        }
    pe_manage_notes(mod_count, state);
    atexit(pe_shutdown_mods);
}

static void pe_host_start(const PeHookInit *init) {
    const char *slash;
    char log_path[1100];

    s_interactive = init && init->interactive;
    if (init && init->prefs_file && *init->prefs_file) {
        const char *a = strrchr(init->prefs_file, '/'), *b = strrchr(init->prefs_file, '\\');
#ifdef PE_PS5_PLUGINS
        /* PS5 config paths use absolute POSIX paths, checked before startup. */
        slash = a;
        (void)b;
#else
        slash = a > b ? a : b;
#endif
        if (slash && (size_t)(slash - init->prefs_file + 1) < sizeof(s_prefs_dir))
            snprintf(s_prefs_dir, sizeof(s_prefs_dir), "%.*s",
                     (int)(slash - init->prefs_file + 1), init->prefs_file);
    }
#ifndef PE_PS5_PLUGINS
    if (pe_mod_path(log_path, sizeof(log_path), "pe_mods.log") == 0)
        s_log = fopen(log_path, "w");
#else
    /* PS5 diagnostics already reach stderr and its per-launch session sink.
     * Do not truncate a second shared log behind the manager's back. */
    (void)log_path;
#endif
    pe_mod_log(NULL, "mod host started (mod API %d, %s)", PE_MOD_API_VERSION, s_interactive ? "interactive" : "headless");
    pe_menu_load_settings();
    pe_pages_describe();
    pe_start_mods();
    if (s_all_on) {                              /* off: nothing but F1 touches the game */
        pe_main_menu_init();
        pe_equip_sync_init();
        pe_present_install();
    }
    pe_save_install();                           /* always: a save made with mods off keeps their data too */
    if (s_interactive) pe_panel_install();
    s_started = 1;
    {                                            /* inactive mods' handlers stay registered, skipped */
        int i;
        for (i = 0; i < s_handler_count; i++) s_handlers[i].active = pe_mod_active(s_handlers[i].mod_id);
    }
}

int pe_host_init(const PeHookInit *init) {
#ifdef PE_PS5_PLUGINS
    const char *slash;
    /* Never fall back to cwd when an invalid/oversized preference directory
     * cannot fit the host's existing bounded path storage. */
    if (!init || !init->prefs_file || init->prefs_file[0] != '/' ||
        !(slash = strrchr(init->prefs_file, '/')) ||
        (size_t)(slash - init->prefs_file + 1) >= sizeof(s_prefs_dir)) {
        fprintf(stderr, "[mods] PS5 host disabled: invalid absolute preferences path\n");
        return -1;
    }
#endif
    if (!s_started) pe_host_start(init);
    return 0;
}

static const char *pe_hook_name(int id) {
    static const char *const names[PE_HOOK_COUNT] = {
        [PE_HOOK_INIT] = "init",
        [PE_HOOK_TEXT_GLYPH] = "text glyph",
        [PE_HOOK_SCENE_START] = "scene start",
        [PE_HOOK_FRAME] = "frame",
        [PE_HOOK_HELP_TEXT] = "help text",
        [PE_HOOK_INV_COMMAND] = "inventory command",
        [PE_HOOK_ITEM_ADD] = "item add",
        [PE_HOOK_SAVE_WRITE] = "save write",
        [PE_HOOK_SAVE_READ] = "save read",
        [PE_HOOK_ITEM_COUNT] = "item count",
        [PE_HOOK_PAD_READ] = "pad read",
        [PE_HOOK_LIST_ROWS] = "list rows",
        [PE_HOOK_MAIN_MENU_ACTION] = "main menu action",
        [PE_HOOK_SCRIPT_RANDOM] = "script random",
        [PE_HOOK_INVENTORY_SORTED] = "inventory sorted",
        [PE_HOOK_MAIN_MENU_ICON] = "main menu icon",
        [PE_HOOK_ITEM_REMOVE] = "item remove",
        [PE_HOOK_INVENTORY_REBUILD] = "inventory rebuild",
        [PE_HOOK_STORAGE_TRANSFER] = "storage transfer",
        [PE_HOOK_SLOT_DRAWN] = "slot drawn",
    };
    if (id >= 0 && id < PE_HOOK_COUNT) return names[id] ? names[id] : "?";
    return id - PE_HOOK_COUNT < s_event_count ? s_events[id - PE_HOOK_COUNT] : "?";
}

int pe_hook_dispatch(int id, void *ctx) {
    int i, handled = 0;
    if (!s_started) return 0;
    /* The mods' saved records go back before any mod's own load handler sees the save. */
    if (id == PE_HOOK_SAVE_READ && ctx) pe_save_on_read((const uint8_t *)ctx);
    /* A new game: every record resets, and nothing carries over, before any mod hears of it. */
    if (id == PE_HOOK_NEW_GAME) pe_save_new_game();
    for (i = 0; i < s_handler_count; i++) {
        int result;
        if (s_handlers[i].id < id) continue;
        if (s_handlers[i].id > id) break;
        if (!s_handlers[i].active) continue;
        pe_mod_call_begin(s_handlers[i].mod_id, id < PE_HOOK_COUNT ? "hook" : "event", pe_hook_name(id));
        result = s_handlers[i].fn(ctx, s_handlers[i].user);
        pe_mod_call_end();
        if (result) handled = 1;
    }
    /* After every mod's own save handler: the records go into the save. */
    if (id == PE_HOOK_SAVE_WRITE && ctx) pe_save_on_write((uint8_t *)ctx);
    /* The EX Game starts with this script command; the title's New Game sends PE_HOOK_NEW_GAME
     * itself. Mods hear both the one way. */
    if (!handled && id == PE_HOOK_INV_COMMAND && ctx && ((const PeHookInvCommand *)ctx)->command == PE_INV_CMD_START_NEW_GAME)
        pe_hook_dispatch(PE_HOOK_NEW_GAME, &(PeHookNewGame){PE_NEW_GAME_EX});
    return handled;
}

/* ---- crash report room snapshot: save tail ----
 * The host's records fill the block as a real save would write them (pe_save.h), from memory: no
 * mod code runs, so no mod's save handler runs each room. Called when a room starts, on
 * the game thread, never inside the crash handler. Not while mod code is running, since its data
 * may be half changed. */
int pe_mod_snapshot_save_tail(unsigned char *block) {
    if (s_call_depth) return -1;
    pe_save_crash_tail(block);
    return 0;
}

/* ---- crash report Mods section (runs inside the crash handler: reads only) ---- */

static const char *pe_mod_label(const char *mod_id) {
    int i;
    if (!mod_id || !strcmp(mod_id, "host") || !strcmp(mod_id, "menu")) return "the mod host";
    for (i = 0; g_pe_mods[i] && i < PE_MAX_MODS; i++)
        if (!strcmp(g_pe_mods[i]->id, mod_id)) return g_pe_mods[i]->name;
    return mod_id;
}

void pe_mod_crash_describe(char *out, size_t size, int on_game_thread) {
    size_t n = 0;
    int i, depth = s_call_depth < PE_MAX_CALLS ? s_call_depth : PE_MAX_CALLS;
#define PE_OUT(...) \
    do { \
        int w_ = n < size ? snprintf(out + n, size - n, __VA_ARGS__) : 0; \
        if (w_ > 0) n += (size_t)w_ < size - n ? (size_t)w_ : size - n - 1; \
    } while (0)

    PE_OUT("Mod API %d, mod host %s, All mods %s\r\n", PE_MOD_API_VERSION, s_started ? "started" : "not started yet",
           s_all_on ? "on" : "off (mods did not touch the game)");
    for (i = 0; g_pe_mods[i] && i < PE_MAX_MODS; i++)
        PE_OUT("  %s: %s\r\n", g_pe_mods[i]->name, s_mod_note[i][0] ? s_mod_note[i] : g_pe_mods[i]->id);

    if (!depth) {
        PE_OUT("Mod code running at the crash: none.%s\r\n",
               s_all_on ? " A mod can still have changed game data earlier: if this repeats, try the same steps "
                          "with All mods off." : "");
    } else {
        const char *mod_id = s_calls[depth - 1].mod_id;
        PE_OUT("Mod code running at the crash%s (innermost first):\r\n",
               on_game_thread ? "" : " on the game thread (the crash itself was on another thread)");
        for (i = depth - 1; i >= 0; i--)
            PE_OUT("  %s (%s): %s%s%s\r\n", pe_mod_label(s_calls[i].mod_id), s_calls[i].mod_id ? s_calls[i].mod_id : "host",
                   s_calls[i].what, s_calls[i].detail ? " " : "", s_calls[i].detail ? s_calls[i].detail : "");
        if (s_call_depth > PE_MAX_CALLS) PE_OUT("  (%d more calls not kept)\r\n", s_call_depth - PE_MAX_CALLS);
        if (on_game_thread && mod_id && strcmp(mod_id, "host") && strcmp(mod_id, "menu"))
            PE_OUT("Most likely cause: the mod \"%s\" (%s), in its %s%s%s. The call stack below shows which of its "
                   "functions.\r\n", pe_mod_label(mod_id), mod_id, s_calls[depth - 1].what,
                   s_calls[depth - 1].detail ? " " : "", s_calls[depth - 1].detail ? s_calls[depth - 1].detail : "");
    }

    /* Saved F1 rows (pe_mods.ini), read directly so no mod code runs here. */
    int settings = 0;
    PE_OUT("Mod settings (F1, saved in pe_mods.ini):\r\n");
    for (i = 0; i < pe_menu_row_count(); i++) {
        const PeMenuRow *row = pe_menu_row(i);
        if (!row || !row->value) continue;
        const int v = *row->value;
        switch (row->kind) {
        case PE_ROW_TOGGLE:
            PE_OUT("  %s/%s = %s\r\n", row->page, row->label, v ? "On" : "Off");
            break;
        case PE_ROW_CHOICE:
            PE_OUT("  %s/%s = %s\r\n", row->page, row->label,
                   v >= 0 && (unsigned)v < row->count ? row->names[v] : "?");
            break;
        case PE_ROW_INT:
            PE_OUT("  %s/%s = %d\r\n", row->page, row->label, v);
            break;
        case PE_ROW_DYNAMIC_CHOICE:
            PE_OUT("  %s/%s = choice %d\r\n", row->page, row->label, v);
            break;
        default:
            continue;
        }
        settings++;
    }
    if (!settings) PE_OUT("  none\r\n");
    PE_OUT("Mods' save data (the room snapshot): ");
    pe_save_crash_describe(out, size, &n);
#undef PE_OUT
}

#if PE_NATIVE_STATE_CPU_COMPONENTS
#include <stdint.h>
#include "pe_state_mod_contract.h"
extern int pe_mod_state_panel_closed(void);
extern int pe_main_menu_state_empty(void);
extern int pe_present_state_empty(void);
extern int pe_save_state_idle(void);
#if PE_NATIVE_STATE_ASSISTS
extern int pe_assists_state_options(uint32_t [3]);
#endif
#if PE_NATIVE_STATE_QOL
#include "pe_checkpoint_qol.h"
#endif
#if PE_NATIVE_STATE_ECONOMY
#include "pe_checkpoint_economy.h"
#endif
#if PE_NATIVE_STATE_COLLECTION
#include "pe_checkpoint_collection.h"
#endif
#if PE_NATIVE_STATE_REBALANCE
#include "pe_checkpoint_rebalance.h"
#endif
extern int pe_save_checkpoint_walk(const unsigned char *,unsigned char *,size_t,int);
extern void pe_equip_sync_state_invalidate(void);
static int s_state_blocker;
static const char*s_state_blocker_mod;
static char s_state_blocker_text[128];
int pe_mod_state_options(uint32_t out[4]){s_state_blocker=0;s_state_blocker_mod=0;
#define STATE_DENY(reason) do{s_state_blocker=(reason);return 0;}while(0)
 if(!out||!s_started)STATE_DENY(1);if(s_call_depth||s_current_mod)STATE_DENY(2);if(!pe_mod_state_panel_closed())STATE_DENY(3);if(!pe_save_state_idle())STATE_DENY(4);
 for(int i=0;i<s_manage_count;i++){const char *id=g_pe_mods[i]->id;if(strcmp(id,"pe_assists")&&strcmp(id,"pe_qol")&&strcmp(id,"pe_rebalance")&&strcmp(id,"pe_economy")&&strcmp(id,"pe_collection"))STATE_DENY(5);}
 for(int i=0;i<s_handler_count;i++)if(s_handlers[i].active&&s_handlers[i].mod_id){const char *id=s_handlers[i].mod_id;if(strcmp(id,"host")&&strcmp(id,"menu")&&strcmp(id,"present")&&strcmp(id,"pe_assists")
#if PE_NATIVE_STATE_QOL
 &&strcmp(id,"pe_qol")
#endif
#if PE_NATIVE_STATE_ECONOMY
 &&strcmp(id,"pe_economy")
#endif
#if PE_NATIVE_STATE_COLLECTION
 &&strcmp(id,"pe_collection")
#endif
#if PE_NATIVE_STATE_REBALANCE
 &&strcmp(id,"pe_rebalance")
#endif
 ){s_state_blocker_mod=id;STATE_DENY(6);}}
 if(!s_boot_all&&!s_all_on&&!s_running_count){out[0]=out[1]=out[2]=out[3]=0;return 1;}
 if(!s_boot_all||!s_all_on)STATE_DENY(6);
 for(int i=0;i<s_manage_count;i++)if(s_mod_on[i]!=s_boot_mod_on[i])STATE_DENY(10);
 unsigned mask=0;for(int i=0;i<s_running_count;i++){if(!strcmp(s_running[i]->id,"pe_assists"))mask|=1u;
#if PE_NATIVE_STATE_QOL
 else if(!strcmp(s_running[i]->id,"pe_qol"))mask|=2u;
#endif
#if PE_NATIVE_STATE_ECONOMY
 else if(!strcmp(s_running[i]->id,"pe_economy"))mask|=4u;
#endif
#if PE_NATIVE_STATE_COLLECTION
 else if(!strcmp(s_running[i]->id,"pe_collection"))mask|=8u;
#endif
#if PE_NATIVE_STATE_REBALANCE
 else if(!strcmp(s_running[i]->id,"pe_rebalance"))mask|=16u;
#endif
 else {s_state_blocker_mod=s_running[i]->id;STATE_DENY(6);}}
 if(!pe_main_menu_state_empty()||!pe_present_state_empty())STATE_DENY(7);
 out[0]=2u|(mask<<8);out[1]=out[2]=out[3]=0;
#if PE_NATIVE_STATE_ASSISTS
 if((mask&1)&&!pe_assists_state_options(out+1))STATE_DENY(8);
#else
 if(mask&1)STATE_DENY(5);
#endif
#if PE_NATIVE_STATE_QOL
 if((mask&2)&&!pe_qol_checkpoint_ready())STATE_DENY(9);
#endif
#if PE_NATIVE_STATE_ECONOMY
 if((mask&4)&&!pe_economy_checkpoint_ready())STATE_DENY(11);
#endif
#if PE_NATIVE_STATE_COLLECTION
 if((mask&8)&&!pe_collection_checkpoint_ready())STATE_DENY(12);
#endif
#if PE_NATIVE_STATE_REBALANCE
 if((mask&16)&&!pe_rebalance_checkpoint_ready())STATE_DENY(13);
#endif
 return 1;
#undef STATE_DENY
}
const char *pe_mod_state_blocker(void){uint32_t exact[4];if(pe_mod_state_options(exact))return "Experimental current-room checkpoint";switch(s_state_blocker){case 1:return "Mods host is not initialized";case 2:return "A mod callback is still running";case 3:return "Close the Mods panel before checkpointing";case 4:return "Pending or live mod-save data is not checkpoint-owned";case 5:return "This build contains an unowned mod; use the supported mod list";case 6:if(s_state_blocker_mod){snprintf(s_state_blocker_text,sizeof s_state_blocker_text,"%s still needs checkpoint ownership",s_state_blocker_mod);return s_state_blocker_text;}return "The current startup mod configuration is not checkpoint-owned";case 7:return "Close the custom mod screen or picture first";case 9:return "Finish the QOL inventory/key-screen job first";case 10:return "A mod activation change is pending; restore it or restart";case 13:
#if PE_NATIVE_STATE_REBALANCE
return pe_rebalance_checkpoint_blocker();
#else
return "Rebalance checkpoint ownership is not included";
#endif
case 12:
#if PE_NATIVE_STATE_COLLECTION
return pe_collection_checkpoint_blocker();
#else
return "Collection ownership is not included in this build";
#endif
case 11:return "Finish the Economy/Wayne menu or item-list transaction first";default:return "Assist settings are invalid";}}
int pe_mod_state_walk(const unsigned char *in,unsigned char *out,size_t bytes,int apply){
 if(bytes!=PE_MOD_STATE_BYTES||(!in&&!out))return 0;uint32_t exact[4];if(!pe_mod_state_options(exact))return 0;
 if(apply&&(!in||!pe_mod_state_walk(in,0,bytes,0)))return 0;
 unsigned caps=0;
#if PE_NATIVE_STATE_QOL
 caps|=1u;
#endif
#if PE_NATIVE_STATE_ECONOMY
 caps|=2u;
#endif
#if PE_NATIVE_STATE_COLLECTION
 caps|=4u;
#endif
#if PE_NATIVE_STATE_REBALANCE
 caps|=8u;
#endif
 if(out){memset(out,0,bytes);memcpy(out,PE_MOD_STATE_MAGIC,8);for(unsigned i=0;i<4;i++)out[8+i]=(unsigned char)(PE_MOD_STATE_BYTES>>(i*8));out[12]=(unsigned char)caps;}
 else if(memcmp(in,PE_MOD_STATE_MAGIC,8)||((uint32_t)in[8]|(uint32_t)in[9]<<8|(uint32_t)in[10]<<16|(uint32_t)in[11]<<24)!=PE_MOD_STATE_BYTES||in[12]!=caps||in[13]||in[14]||in[15])return 0;
 if(!pe_save_checkpoint_walk(in?in+16:0,out?out+16:0,1536,apply))return 0;
#if PE_NATIVE_STATE_QOL
 if(!pe_qol_checkpoint_walk(in?in+1552:0,out?out+1552:0,512,apply))return 0;
#else
 if(in)for(unsigned i=1552;i<2064;i++)if(in[i])return 0;
#endif
#if PE_NATIVE_STATE_ECONOMY
 if(!pe_economy_checkpoint_walk(in?in+2064:0,out?out+2064:0,4096,apply))return 0;
#else
 if(in)for(unsigned i=2064;i<6160;i++)if(in[i])return 0;
#endif
#if PE_NATIVE_STATE_COLLECTION
 if(!pe_collection_checkpoint_walk(in?in+6160:0,out?out+6160:0,1024,apply))return 0;
#else
 if(in)for(unsigned i=6160;i<7184;i++)if(in[i])return 0;
#endif
#if PE_NATIVE_STATE_REBALANCE
 if(!pe_rebalance_checkpoint_walk(in?in+7184:0,out?out+7184:0,3088,apply))return 0;
#else
 if(in)for(unsigned i=7184;i<PE_MOD_STATE_BYTES;i++)if(in[i])return 0;
#endif
 return 1;
}
int pe_mod_state_idle(void){uint32_t exact[4];return pe_mod_state_options(exact);}
void pe_mod_state_after_restore(void){/* Pure derived-cache reset on both target and rollback outcomes. */pe_equip_sync_state_invalidate();}
#endif

#ifdef PE_PS5_GPU_NATIVE
int pe_mod_master_active_now(void){return s_boot_all;}
int pe_mod_master_requested(void){return s_all_on;}
void pe_mod_master_toggle_next_launch(void){if(!s_manage_ready)return;s_all_on=!s_all_on;pe_mod_manage_refresh();pe_menu_save_settings();}
int pe_mod_request_assists_only(void){if(!s_manage_ready)return 0;int found=0;for(int i=0;i<s_manage_count;i++)if(!strcmp(g_pe_mods[i]->id,"pe_assists"))found=1;if(!found)return 0;for(int i=0;i<s_manage_count;i++)s_mod_on[i]=!strcmp(g_pe_mods[i]->id,"pe_assists");s_all_on=1;pe_mod_manage_refresh();pe_menu_save_settings();return 1;}
#endif
