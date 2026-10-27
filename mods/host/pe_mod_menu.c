/* Mods panel rows: registry, row behaviour and pe_mods.ini.
 *
 * pe_mods.ini holds one "page/label=value" line per saved row. It is read
 * before the mods start, and each row picks up its saved value when it
 * registers; it is written when the panel closes. Lines for rows that no
 * longer exist are kept, so turning a mod off never loses its settings. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>

#include "pe_mod_host.h"
#include "pe_mod_menu.h"

enum { PE_MAX_ROWS = 512, PE_MAX_TICKS = 64, PE_MAX_SAVED = 1024 };

static PeMenuRow s_rows[PE_MAX_ROWS];
static int s_row_count;
static PeMenuPerFrame s_ticks[PE_MAX_TICKS];
static int s_tick_count;
static const char *s_page = PE_MENU_PAGE;

typedef struct {
    char key[192];
    int value;
} PeSaved;
static PeSaved s_saved[PE_MAX_SAVED];
static int s_saved_count;

/* ---- settings file ---- */

static void pe_row_key(const PeMenuRow *row, char *out, size_t size) {
    snprintf(out, size, "%s/%s", row->page, row->label);
}

static PeSaved *pe_saved_find(const char *key) {
    int i;
    for (i = 0; i < s_saved_count; i++)
        if (strcmp(s_saved[i].key, key) == 0) return &s_saved[i];
    return NULL;
}

void pe_menu_load_settings(void) {
    char path[1100], line[256];
    FILE *f;
    if (pe_mod_path(path, sizeof(path), "pe_mods.ini") != 0 || !(f = fopen(path, "r"))) return;
    while (fgets(line, sizeof(line), f) && s_saved_count < PE_MAX_SAVED) {
        char *eq = strrchr(line, '=');
        if (!eq || line[0] == '#') continue;
        *eq = '\0';
        snprintf(s_saved[s_saved_count].key, sizeof(s_saved[0].key), "%s", line);
        s_saved[s_saved_count++].value = atoi(eq + 1);
    }
    fclose(f);
}

static int pe_row_saved(const PeMenuRow *row) {
    return row->kind == PE_ROW_TOGGLE || row->kind == PE_ROW_CHOICE || row->kind == PE_ROW_INT;
}

void pe_menu_save_settings(void) {
    char path[1100], key[192], temporary[1180];
    SDL_IOStream *f;
    int i;
    for (i = 0; i < s_row_count; i++) {
        PeSaved *saved;
        if (!pe_row_saved(&s_rows[i])) continue;
        pe_row_key(&s_rows[i], key, sizeof(key));
        saved = pe_saved_find(key);
        if (!saved && s_saved_count < PE_MAX_SAVED) {
            saved = &s_saved[s_saved_count++];
            snprintf(saved->key, sizeof(saved->key), "%s", key);
        }
        if (saved) saved->value = *s_rows[i].value;
    }
    if (pe_mod_path(path, sizeof(path), "pe_mods.ini") != 0) {
        pe_mod_log(NULL, "cannot resolve pe_mods.ini");
        return;
    }
    snprintf(temporary, sizeof(temporary), "%s.%llu.tmp", path,
             (unsigned long long)SDL_GetTicksNS());
    f = SDL_IOFromFile(temporary, "wbx");
    if (!f) {
        pe_mod_log(NULL, "cannot stage pe_mods.ini: %s", SDL_GetError());
        pe_toast("Could not save mod settings. Changes apply for this session.");
        return;
    }
    const char *header = "# Mods panel settings (page/label=value)\n";
    int ok = SDL_WriteIO(f, header, strlen(header)) == strlen(header);
    for (i = 0; ok && i < s_saved_count; i++) {
        char line[256];
        int length = snprintf(line, sizeof(line), "%s=%d\n", s_saved[i].key, s_saved[i].value);
        ok = length > 0 && length < sizeof(line) && SDL_WriteIO(f, line, length) == length;
    }
    if (ok && !SDL_FlushIO(f)) ok = 0;
    if (!SDL_CloseIO(f)) ok = 0;
    if (!ok || !SDL_RenamePath(temporary, path)) {
        pe_mod_log(NULL, "cannot save pe_mods.ini: %s", SDL_GetError());
        SDL_RemovePath(temporary);
        pe_toast("Could not save mod settings. Changes apply for this session.");
    }

}

/* ---- registration ---- */

void pe_menu_begin(const char *page) { s_page = page && *page ? page : PE_MENU_PAGE; }

static PeMenuRow *pe_row_new(PeRowKind kind, const char *sub, const char *label,
                             const char *description) {
    PeMenuRow *row;
    if (!label || s_row_count >= PE_MAX_ROWS) return NULL;
    row = &s_rows[s_row_count++];
    memset(row, 0, sizeof(*row));
    row->kind = kind;
    if (sub && *sub) snprintf(row->page, sizeof(row->page), "%s/%s", s_page, sub);
    else snprintf(row->page, sizeof(row->page), "%s", s_page);
    row->label = label;
    row->description = description ? description : "";
    row->mod_id = pe_mod_current();               /* NULL: the host's own */
    return row;
}

int pe_menu_row_active(const PeMenuRow *row) { return pe_mod_active(row->mod_id); }

static void pe_row_restore(PeMenuRow *row) {
    char key[192];
    PeSaved *saved;
    pe_row_key(row, key, sizeof(key));
    if (!(saved = pe_saved_find(key))) return;
    if (row->kind == PE_ROW_TOGGLE) *row->value = saved->value != 0;
    else if (saved->value >= row->minimum && saved->value <= row->maximum) *row->value = saved->value;
}

int pe_menu_register_toggle(const char *sub, const char *label, const char *description,
                            int *flag, PeMenuPerFrame per_frame) {
    PeMenuRow *row;
    if (!flag || !(row = pe_row_new(PE_ROW_TOGGLE, sub, label, description))) return -1;
    row->value = flag;
    row->maximum = 1;
    pe_row_restore(row);
    return per_frame ? pe_menu_register_tick(per_frame) : 0;
}

int pe_menu_register_action(const char *sub, const char *label, const char *description,
                            PeMenuAction action) {
    PeMenuRow *row;
    if (!action || !(row = pe_row_new(PE_ROW_ACTION, sub, label, description))) return -1;
    row->action = action;
    return 0;
}

int pe_menu_register_choice(const char *sub, const char *label, const char *description,
                            int *index, const char *const *names, unsigned count) {
    PeMenuRow *row;
    if (!index || !names || !count || !(row = pe_row_new(PE_ROW_CHOICE, sub, label, description)))
        return -1;
    row->value = index;
    row->names = names;
    row->count = count;
    row->maximum = (int)count - 1;
    pe_row_restore(row);
    return 0;
}

int pe_menu_register_int(const char *sub, const char *label, const char *description,
                         int *value, int minimum, int maximum, int step) {
    PeMenuRow *row;
    if (!value || minimum > maximum || !(row = pe_row_new(PE_ROW_INT, sub, label, description)))
        return -1;
    row->value = value;
    row->minimum = minimum;
    row->maximum = maximum;
    row->step = step > 0 ? step : 1;
    pe_row_restore(row);
    return 0;
}

int pe_menu_register_live_int(const char *sub, const char *label, const char *description,
                              int minimum, int maximum, int step, PeMenuGet get, PeMenuSet set) {
    PeMenuRow *row;
    if (!get || !set || !(row = pe_row_new(PE_ROW_LIVE_INT, sub, label, description))) return -1;
    row->minimum = minimum;
    row->maximum = maximum;
    row->step = step > 0 ? step : 1;
    row->get = get;
    row->set = set;
    return 0;
}

int pe_menu_register_live_choice(const char *sub, const char *label, const char *description,
                                 const char *const *names, unsigned count, PeMenuGet get,
                                 PeMenuSet set) {
    PeMenuRow *row;
    if (!names || !count || !get || !set ||
        !(row = pe_row_new(PE_ROW_LIVE_CHOICE, sub, label, description)))
        return -1;
    row->names = names;
    row->count = count;
    row->maximum = (int)count - 1;
    row->get = get;
    row->set = set;
    return 0;
}

int pe_menu_register_dynamic_choice(const char *sub, const char *label, const char *description,
                                    int *index, unsigned max_count, PeMenuChoiceCount count_fn,
                                    PeMenuChoiceName name_fn) {
    PeMenuRow *row;
    if (!index || !count_fn || !name_fn ||
        !(row = pe_row_new(PE_ROW_DYNAMIC_CHOICE, sub, label, description)))
        return -1;
    row->value = index;
    row->count = max_count;
    row->count_fn = count_fn;
    row->name_fn = name_fn;
    return 0;
}

int pe_menu_register_info(const char *sub, const char *label, const char *description, PeMenuText text) {
    PeMenuRow *row;
    if (!text || !(row = pe_row_new(PE_ROW_INFO, sub, label, description))) return -1;
    row->text_fn = text;
    return 0;
}

/* ---- page metadata ---- */
enum { PE_MAX_PAGES = 64 };
static PeMenuPageInfo s_pages[PE_MAX_PAGES];
static int s_page_count;

static int pe_page_info_set(const char *path, const char *description, int order);

int pe_menu_page(const char *sub, const char *description, int order) {
    char path[96];
    if (!sub || !*sub) return -1;
    snprintf(path, sizeof path, "%s/%s", s_page, sub);
    return pe_page_info_set(path, description, order);
}

int pe_menu_top_page(const char *page, const char *description, int order) {
    return page && *page ? pe_page_info_set(page, description, order) : -1;
}

static int pe_page_info_set(const char *path, const char *description, int order) {
    PeMenuPageInfo *p;
    int i;
    for (i = 0; i < s_page_count && strcmp(s_pages[i].path, path) != 0; ++i) {}
    if (i == s_page_count) {
        if (s_page_count >= PE_MAX_PAGES) return -1;
        ++s_page_count;
    }
    p = &s_pages[i];
    snprintf(p->path, sizeof p->path, "%s", path);
    p->description = description;
    p->order = order;
    return 0;
}

const PeMenuPageInfo *pe_menu_page_info(const char *path) {
    int i;
    for (i = 0; i < s_page_count; ++i)
        if (strcmp(s_pages[i].path, path) == 0) return &s_pages[i];
    return NULL;
}

int pe_menu_row_adjustable(const PeMenuRow *row) {
    return row->kind != PE_ROW_ACTION && row->kind != PE_ROW_INFO;
}

static const char *s_tick_mod[PE_MAX_TICKS];   /* each tick's mod: inactive mods' are skipped */

static int pe_menu_tick(void *ctx, void *user) {
    int i;
    (void)ctx;
    (void)user;
    for (i = 0; i < s_tick_count; i++)
        if (pe_mod_active(s_tick_mod[i])) s_ticks[i]();
    return 0;
}

int pe_menu_register_tick(PeMenuPerFrame fn) {
    if (!fn || s_tick_count >= PE_MAX_TICKS) return -1;
    if (s_tick_count == 0) pe_hook_add(PE_HOOK_FRAME, pe_menu_tick, NULL, PE_PRIORITY_NORMAL, "menu");
    s_tick_mod[s_tick_count] = pe_mod_current();
    s_ticks[s_tick_count++] = fn;
    return 0;
}

/* ---- row behaviour ---- */

int pe_menu_row_count(void) { return s_row_count; }
PeMenuRow *pe_menu_row(int index) { return index >= 0 && index < s_row_count ? &s_rows[index] : NULL; }

static unsigned pe_dynamic_count(PeMenuRow *row) {
    unsigned count = row->count_fn();
    if (count > row->count) count = row->count;
    if (count != row->last_count) {
        row->last_count = count;
        *row->value = 0;
    }
    if (*row->value < 0 || (unsigned)*row->value >= count) *row->value = 0;
    return count;
}

void pe_menu_row_value_text(PeMenuRow *row, char *out, unsigned size) {
    int v;
    /* An inactive mod's rows that read the game show that it is off, not stale numbers. */
    if (!pe_menu_row_active(row) && (row->kind == PE_ROW_LIVE_INT || row->kind == PE_ROW_LIVE_CHOICE ||
                                     row->kind == PE_ROW_ACTION)) {
        snprintf(out, size, "Off");
        return;
    }
    switch (row->kind) {
    case PE_ROW_TOGGLE:
        snprintf(out, size, "%s", *row->value ? "On" : "Off");
        break;
    case PE_ROW_ACTION:
        snprintf(out, size, "Run");
        break;
    case PE_ROW_CHOICE:
    case PE_ROW_LIVE_CHOICE:
        v = row->kind == PE_ROW_CHOICE ? *row->value : row->get();
        snprintf(out, size, "%s", v >= 0 && (unsigned)v < row->count ? row->names[v] : "?");
        break;
    case PE_ROW_INT:
        snprintf(out, size, "%d", *row->value);
        break;
    case PE_ROW_LIVE_INT:
        snprintf(out, size, "%d", row->get());
        break;
    case PE_ROW_DYNAMIC_CHOICE:
        if (pe_dynamic_count(row) == 0) snprintf(out, size, "(none)");
        else snprintf(out, size, "%s", row->name_fn((unsigned)*row->value));
        break;
    case PE_ROW_INFO:
        out[0] = '\0';
        row->text_fn(out, size);
        break;
    }
}

static int pe_wrap(int v, int count) { return count > 0 ? ((v % count) + count) % count : 0; }

static int pe_clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

void pe_menu_row_step(PeMenuRow *row, int direction) {
    unsigned count;
    /* An inactive mod's settings can be set up (saved); what writes the game can't run. */
    if (!pe_menu_row_active(row) && (row->kind == PE_ROW_LIVE_INT || row->kind == PE_ROW_LIVE_CHOICE)) return;
    switch (row->kind) {
    case PE_ROW_TOGGLE:
        *row->value = !*row->value;
        break;
    case PE_ROW_CHOICE:
        *row->value = pe_wrap(*row->value + direction, (int)row->count);
        break;
    case PE_ROW_LIVE_CHOICE:
        row->set(pe_wrap(row->get() + direction, (int)row->count));
        break;
    case PE_ROW_INT:
        *row->value = pe_clamp(*row->value + direction * row->step, row->minimum, row->maximum);
        break;
    case PE_ROW_LIVE_INT:
        row->set(pe_clamp(row->get() + direction * row->step, row->minimum, row->maximum));
        break;
    case PE_ROW_DYNAMIC_CHOICE:
        count = pe_dynamic_count(row);
        if (count) *row->value = pe_wrap(*row->value + direction, (int)count);
        break;
    case PE_ROW_ACTION:
    case PE_ROW_INFO:
        break;
    }
}

/* Enter: run an action, flip a toggle, cycle a choice. Numbers change with left/right only. */
void pe_menu_row_accept(PeMenuRow *row) {
    switch (row->kind) {
    case PE_ROW_ACTION:
        if (!pe_menu_row_active(row)) {
            pe_toast("This mod is off: turn it on in Manage mods, then restart");
            break;
        }
        pe_mod_log(NULL, "action %s/%s", row->page, row->label);
        row->action();
        break;
    case PE_ROW_TOGGLE:
    case PE_ROW_CHOICE:
    case PE_ROW_LIVE_CHOICE:
    case PE_ROW_DYNAMIC_CHOICE:
        pe_menu_row_step(row, 1);
        break;
    default:
        break;
    }
}
