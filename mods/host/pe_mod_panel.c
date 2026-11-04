/* The Mods panel: F1 opens a paused list of every mod's rows (pe_mod_menu.h).
 *
 * Navigation: each page lists its own rows first, then links to its sub-pages (in the order
 * pe_menu_page gives them). The header shows where you are ("PE Enhancements / Economy"); the
 * footer the selected row's description and the keys that apply to it. The panel reopens on the
 * page and row it was closed on and every page remembers its row. The top is always the list of
 * mods (each mod's top page, then Manage mods), even with one mod installed.
 *
 * It sits beside the game's Escape settings panel without touching it: the event filter
 * returns the previous one and passes everything on while closed. Like the settings panel it is
 * modal: opening pauses the host clock and audio and runs its own present loop until it closes.
 * Drawing: ui/pe_ui_raster.c into a surface, shown by ui/pe_ui_gpu.c. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <SDL3/SDL.h>
#include <psyz/audio.h>
#include <psyz/overlay_sdl3.h>
#include <psyz/video.h>

#include "pe_mod_host.h"
#include "pe_translate.h"
#include "pe_mod_menu.h"
#include "ui/pe_ui.h"

void pe_toast_block(int blocked);
void pe_toast_install(void);

/* ---- layout, in design units scaled with the window ---- */
enum {
    PANEL_W = 700, PANEL_PAD = 30, HEADER_H = 100, ROW_H = 42, VISIBLE_ROWS = 12, FOOTER_H = 74,
    VALUE_W = 300, REFERENCE_H = 820, MAX_ENTRIES = 256, MAX_REMEMBERED = 48,
};

/* ---- page model ---- */
typedef struct {
    int row;          /* >= 0: a registry row; -1: a link to a sub-page */
    char page[96];    /* link: its full path */
    int name_at;      /* link: where its last segment starts in page */
    int order;        /* link: sort key */
} Entry;

static struct {
    int open, dirty;
    char path[96];    /* current page, "" = the list of mods */
    int cursor, scroll;
    Entry entries[MAX_ENTRIES];
    int count;
} s_panel;

static struct { char path[96]; int cursor; } s_remembered[MAX_REMEMBERED];
/* A digits row (PE_ROW_DIGITS) being typed in: its digits until Enter writes them, Esc drops them. */
static struct { int row, pos, digit[9]; } s_edit = {-1, 0, {0}};
/* A word beside a digits row after C or Ctrl+V ("COPIED"), until the next key. */
static struct { int row; const char *text; } s_flash = {-1, NULL};
static PeSurface s_surface;
static float s_scale;
static PsyzOverlayEventFilter_SDL3 s_prev_filter;

static int pe_link_index(const char *page) {
    int i;
    for (i = 0; i < s_panel.count; ++i)
        if (s_panel.entries[i].row < 0 && strcmp(s_panel.entries[i].page, page) == 0) return i;
    return -1;
}

static void pe_build_entries(void) {
    const size_t plen = strlen(s_panel.path);
    int i, rows = 0, links, a, b;
    s_panel.count = 0;
    /* Rows of this page first, in registration order. */
    for (i = 0; i < pe_menu_row_count() && s_panel.count < MAX_ENTRIES; ++i)
        if (strcmp(pe_menu_row(i)->page, s_panel.path) == 0) {
            Entry *e = &s_panel.entries[s_panel.count++];
            memset(e, 0, sizeof *e);
            e->row = i;
            ++rows;
        }
    /* Then a link per sub-page that has rows somewhere below it. */
    for (i = 0; i < pe_menu_row_count() && s_panel.count < MAX_ENTRIES; ++i) {
        const char *page = pe_menu_row(i)->page, *rest;
        const PeMenuPageInfo *info;
        Entry *e;
        if (plen == 0) rest = page;
        else if (strncmp(page, s_panel.path, plen) == 0 && page[plen] == '/') rest = page + plen + 1;
        else continue;
        e = &s_panel.entries[s_panel.count];
        memset(e, 0, sizeof *e);
        e->row = -1;
        snprintf(e->page, sizeof e->page, "%.*s", (int)(rest - page + strcspn(rest, "/")), page);
        if (pe_link_index(e->page) >= 0) continue;
        e->name_at = (int)(rest - page);
        info = pe_menu_page_info(e->page);
        e->order = info ? info->order : 1000 + s_panel.count;
        s_panel.count++;
    }
    /* Links by order (stable insertion sort). */
    links = s_panel.count - rows;
    for (a = rows + 1; a < rows + links; ++a) {
        Entry tmp = s_panel.entries[a];
        for (b = a; b > rows && s_panel.entries[b - 1].order > tmp.order; --b) s_panel.entries[b] = s_panel.entries[b - 1];
        s_panel.entries[b] = tmp;
    }
}

static int *pe_remembered_cursor(const char *path) {
    int i, free_at = -1;
    for (i = 0; i < MAX_REMEMBERED; ++i) {
        if (s_remembered[i].path[0] && strcmp(s_remembered[i].path, path) == 0) return &s_remembered[i].cursor;
        if (!s_remembered[i].path[0] && free_at < 0) free_at = i;
    }
    if (free_at < 0) free_at = 0;
    snprintf(s_remembered[free_at].path, sizeof s_remembered[0].path, "%s", path);
    s_remembered[free_at].cursor = 0;
    return &s_remembered[free_at].cursor;
}

static void pe_clamp_cursor(void) {
    if (s_panel.cursor >= s_panel.count) s_panel.cursor = s_panel.count ? s_panel.count - 1 : 0;
    if (s_panel.cursor < 0) s_panel.cursor = 0;
    if (s_panel.cursor < s_panel.scroll) s_panel.scroll = s_panel.cursor;
    if (s_panel.cursor >= s_panel.scroll + VISIBLE_ROWS) s_panel.scroll = s_panel.cursor - VISIBLE_ROWS + 1;
}

static void pe_go(const char *path, int cursor) {
    *pe_remembered_cursor(s_panel.path) = s_panel.cursor;
    snprintf(s_panel.path, sizeof s_panel.path, "%s", path);
    pe_build_entries();
    s_panel.cursor = cursor >= 0 ? cursor : *pe_remembered_cursor(s_panel.path);
    s_panel.scroll = 0;
    pe_clamp_cursor();
}

/* The page to open on: the last one, else the list of mods (always shown, even with one). */
static void pe_open_page(void) {
    pe_build_entries();
    if (s_panel.path[0] && s_panel.count) { pe_clamp_cursor(); return; }
    pe_go("", -1);
}

static int pe_at_top(void) { return !s_panel.path[0]; }   /* the list of mods */

static void pe_go_up(void) {
    char parent[96], came_from[96], *slash;
    int at;
    snprintf(came_from, sizeof came_from, "%s", s_panel.path);
    snprintf(parent, sizeof parent, "%s", came_from);
    slash = strrchr(parent, '/');
    if (slash) *slash = '\0';
    else parent[0] = '\0';
    pe_go(parent, -1);
    if ((at = pe_link_index(came_from)) >= 0) { s_panel.cursor = at; pe_clamp_cursor(); }
}

/* ---- paint ---- */
static int pe_visible_rows(void) {
    const int n = s_panel.count < VISIBLE_ROWS ? s_panel.count : VISIBLE_ROWS;
    return n > 0 ? n : 1;
}

static int pe_design_h(void) { return HEADER_H + pe_visible_rows() * ROW_H + FOOTER_H; }

static float pe_layout_scale(int win_w, int win_h) {
    float scale = (float)win_h / (float)REFERENCE_H;
    const float wl = (float)win_w * 0.94f / (float)PANEL_W, hl = (float)win_h * 0.94f / (float)pe_design_h();
    if (scale > wl) scale = wl;
    if (scale > hl) scale = hl;
    if (scale > 2.5f) scale = 2.5f;
    if (scale < 0.5f) scale = 0.5f;
    return floorf(scale * 8.0f) / 8.0f;
}

static const Entry *pe_selected(void) {
    return s_panel.count ? &s_panel.entries[s_panel.cursor] : NULL;
}

/* Does page `page` (or a page below it) have a row of an active mod? */
static int pe_page_active(const char *page) {
    const size_t n = strlen(page);
    int i;
    for (i = 0; i < pe_menu_row_count(); ++i) {
        const PeMenuRow *row = pe_menu_row(i);
        if (strncmp(row->page, page, n) == 0 && (row->page[n] == '\0' || row->page[n] == '/') && pe_menu_row_active(row))
            return 1;
    }
    return 0;
}

#define PE_OFF_NOTE "Off: turn it on in Manage mods, then restart. "

static const char *pe_help_text(void) {
    pe_mod_manage_refresh();
    static char text[320];
    const Entry *e = pe_selected();
    const PeMenuPageInfo *info;
    const char *description;
    int off;
    if (!e) return pe_l10n_ui("Nothing here yet.");
    if (e->row >= 0) {
        description = pe_menu_row(e->row)->description;
        off = !pe_menu_row_active(pe_menu_row(e->row));
    } else {
        info = pe_menu_page_info(e->page);
        description = info && info->description ? info->description : "Open this page.";
        off = !pe_page_active(e->page);
    }
    if (!off) return description;
    description = pe_l10n_ui(description);
    snprintf(text, sizeof text, "%s%s", PE_OFF_NOTE, description);   /* browse and set up, nothing runs */
    return text;
}

static const char *pe_key_hints(void) {
#ifdef PE_PS5_PLUGINS
    return "D-pad select/change   Cross open   Circle back   L1+R1+Touchpad close";
#endif
    const Entry *e = pe_selected();
    const char *back = pe_at_top() ? "Esc close" : "Esc back";
    static char hints[128];
    const char *action = "";
    if (e && e->row < 0) action = "Enter open";
    else if (e && pe_menu_row(e->row)->kind == PE_ROW_DIGITS) {
        if (s_edit.row == e->row) return "0-9 type   Left/Right digit   Up/Down change   Enter done   Esc cancel";
        action = "Enter edit   C copy   Ctrl+V paste";
    }
    else if (e && pe_menu_row(e->row)->kind == PE_ROW_ACTION) action = "Enter run";
    else if (e && pe_menu_row_adjustable(pe_menu_row(e->row))) action = "Left/Right change";
    snprintf(hints, sizeof hints, pe_l10n_ui("Up/Down select   %s%s%s   F1 close"), action, *action ? "   " : "", back);
    return hints;
}

/* "PE Enhancements / Economy / Mod Catalog" */
static void pe_breadcrumb(char *out, size_t size) {
    size_t o = 0;
    const char *p;
    if (!s_panel.path[0]) { snprintf(out, size, "%s", pe_l10n_ui("All mods")); return; }
    for (p = s_panel.path; *p && o + 4 < size;) {
        char segment[96];
        size_t n = strcspn(p, "/");
        if (n >= sizeof segment) break;
        memcpy(segment, p, n); segment[n] = 0;
        const char *label = pe_l10n_ui(segment);
        size_t bytes = strlen(label);
        if (bytes >= size-o) break;
        memcpy(out+o,label,bytes); o += bytes; p += n;
        if (*p == '/') {
            if (o + 4 >= size) break;
            out[o++]=' '; out[o++]='/'; out[o++]=' '; ++p;
        }
    }
    out[o] = '\0';
    if (!pe_page_active(s_panel.path) && o + 7 < size) snprintf(out + o, size - o, "  (off)");
}

/* Two sheets, the back one peeking out top-right: a copy icon `h` tall whose left edge is x, centred on y. */
static void pe_copy_icon(float x, float y, float h, PeColor c) {
    const float k = h / 16.0f, t = 1.6f * k, w = 10 * k, sh = 13 * k, off = 3 * k;
    const float fx = x, fy = y - h * 0.5f + off;                 /* front sheet */
    pe_ui_fill_round_rect(&s_surface, fx, fy, w, t, t * 0.5f, c);
    pe_ui_fill_round_rect(&s_surface, fx, fy + sh - t, w, t, t * 0.5f, c);
    pe_ui_fill_round_rect(&s_surface, fx, fy, t, sh, t * 0.5f, c);
    pe_ui_fill_round_rect(&s_surface, fx + w - t, fy, t, sh, t * 0.5f, c);
    pe_ui_fill_round_rect(&s_surface, fx + off, fy - off, w, t, t * 0.5f, c);           /* back: top and right */
    pe_ui_fill_round_rect(&s_surface, fx + off + w - t, fy - off, t, sh, t * 0.5f, c);
}

/* A digits row's value: a box per digit (the one being typed in lit up), the copy icon after them,
 * and COPIED / PASTED beside it just after. `on`: the accent; `ink`: digits; `rim`: the boxes' frames. */
enum { DIGIT_W = 22, DIGIT_H = 28, DIGIT_GAP = 4 };
static void pe_paint_digits(int index, const PeMenuRow *row, float cx, float mid, float base, int selected, PeColor on,
                            PeColor ink, PeColor rim) {
    const float s = s_scale, total = (row->count * DIGIT_W + (row->count - 1) * DIGIT_GAP) * s;
    const int editing = s_edit.row == index;
    char value[16], one[2] = {0, 0};
    float x = cx - 14 * s - total * 0.5f;
    unsigned i;
    snprintf(value, sizeof value, "%0*d", (int)row->count, *row->value);
    for (i = 0; i < row->count; ++i, x += (DIGIT_W + DIGIT_GAP) * s) {
        const int lit = editing && (int)i == s_edit.pos;
        const float y = mid - DIGIT_H * 0.5f * s;
        one[0] = editing ? (char)('0' + s_edit.digit[i]) : value[i];
        pe_ui_fill_round_rect(&s_surface, x, y, DIGIT_W * s, DIGIT_H * s, 3 * s,
                              lit ? on : pe_ui_alpha(rim, editing || selected ? 0.55f : 0.30f));
        pe_ui_fill_round_rect(&s_surface, x + 1, y + 1, DIGIT_W * s - 2, DIGIT_H * s - 2, 3 * s - 1,
                              lit ? (PeColor){on.r * 0.40f, on.g * 0.40f, on.b * 0.40f, 0.95f} : (PeColor){0.02f, 0.03f, 0.06f, 0.95f});
        pe_ui_draw_text(&s_surface, x + DIGIT_W * 0.5f * s, base, 17 * s, one, lit ? (PeColor){1, 1, 1, 1} : ink,
                        PE_ALIGN_CENTRE);
        if (lit) pe_ui_fill_round_rect(&s_surface, x + 4 * s, y + DIGIT_H * s + 3 * s, (DIGIT_W - 8) * s, 2 * s, 1 * s, on);
    }
    x += (12 - DIGIT_GAP) * s;
    pe_copy_icon(x, mid, 21 * s, selected && !editing ? on : pe_ui_alpha(rim, 0.9f));
    if (s_flash.row == index && s_flash.text)
        pe_ui_draw_text(&s_surface, x + 24 * s, base - 1 * s, 12 * s, s_flash.text, on, PE_ALIGN_LEFT);
}

static void pe_paint_row(const Entry *e, int slot, int selected) {
    const float s = s_scale, top = (HEADER_H + slot * ROW_H) * s, mid = top + ROW_H * 0.5f * s, base = mid + 6 * s;
    const float value_x = (PANEL_W - PANEL_PAD - VALUE_W * 0.5f) * s;
    /* An inactive mod's rows and pages read dimmer: browsable, not running. */
    const int off = e->row >= 0 ? !pe_menu_row_active(pe_menu_row(e->row)) : !pe_page_active(e->page);
    const PeColor label = pe_ui_alpha(pe_ui_text, (selected ? 1.0f : 0.82f) * (off ? 0.55f : 1.0f));
    char value[96];
    if (selected) {
        pe_ui_fill_round_rect(&s_surface, PANEL_PAD / 2 * s, top + 3 * s, (PANEL_W - PANEL_PAD) * s, (ROW_H - 6) * s, 9 * s,
                              pe_ui_alpha(pe_ui_accent, 0.16f));
        pe_ui_fill_round_rect(&s_surface, PANEL_PAD / 2 * s + 4 * s, mid - 10 * s, 3 * s, 20 * s, 1.5f * s, pe_ui_accent);
    }
    if (e->row < 0) {                                   /* link */
        pe_ui_draw_text(&s_surface, PANEL_PAD * s, base, 17 * s, e->page + e->name_at, label, PE_ALIGN_LEFT);
        pe_ui_draw_text(&s_surface, (PANEL_W - PANEL_PAD) * s, base, 17 * s, ">", selected ? pe_ui_accent : pe_ui_muted,
                        PE_ALIGN_RIGHT);
        return;
    }
    {
        PeMenuRow *row = pe_menu_row(e->row);
        pe_ui_draw_text(&s_surface, PANEL_PAD * s, base, 17 * s, row->label, label, PE_ALIGN_LEFT);
        if (row->kind == PE_ROW_ACTION) {
            if (selected) pe_ui_draw_text(&s_surface, value_x, base, 15 * s, off ? "Off" : "Press Enter", off ? pe_ui_muted : pe_ui_accent,
                                          PE_ALIGN_CENTRE);
            return;
        }
        if (row->kind == PE_ROW_DIGITS) {
            pe_paint_digits(e->row, row, value_x, mid, base, selected, pe_ui_accent, pe_ui_text, pe_ui_border);
            return;
        }
        pe_menu_row_value_text(row, value, sizeof value);
        if (row->kind == PE_ROW_INFO) {
            pe_ui_draw_text(&s_surface, value_x, base, 16 * s, value, pe_ui_muted, PE_ALIGN_CENTRE);
            return;
        }
        pe_ui_draw_text(&s_surface, value_x, base, 17 * s, value, selected ? pe_ui_accent : pe_ui_alpha(pe_ui_text, 0.9f),
                        PE_ALIGN_CENTRE);
        if (selected) {
            const float reach = (VALUE_W * 0.5f - 14) * s;
            pe_ui_draw_text(&s_surface, value_x - reach, base, 17 * s, "<", pe_ui_accent, PE_ALIGN_CENTRE);
            pe_ui_draw_text(&s_surface, value_x + reach, base, 17 * s, ">", pe_ui_accent, PE_ALIGN_CENTRE);
        }
    }
}

static void pe_paint(int w, int h) {
    const float s = s_scale;
    const int visible = pe_visible_rows();
    const float footer = (float)(HEADER_H + visible * ROW_H);
    char crumb[160];
    int i;
    pe_ui_fill_round_rect(&s_surface, 0, 0, (float)w, (float)h, 16 * s, pe_ui_border);
    pe_ui_fill_round_rect(&s_surface, 1, 1, (float)w - 2, (float)h - 2, 15 * s, pe_ui_panel);
    pe_ui_draw_text(&s_surface, PANEL_PAD * s, 42 * s, 26 * s, "Mods", pe_ui_text, PE_ALIGN_LEFT);
    pe_ui_draw_text(&s_surface, (PANEL_W - PANEL_PAD) * s, 40 * s, 14 * s, "GAME PAUSED", pe_ui_muted, PE_ALIGN_RIGHT);
    pe_breadcrumb(crumb, sizeof crumb);
    pe_ui_draw_text(&s_surface, PANEL_PAD * s, 76 * s, 15 * s, crumb, pe_ui_accent, PE_ALIGN_LEFT);
    pe_ui_fill_round_rect(&s_surface, PANEL_PAD * s, (HEADER_H - 8) * s, (PANEL_W - 2 * PANEL_PAD) * s, 1, 0,
                          pe_ui_alpha(pe_ui_text, 0.10f));
    if (!s_panel.count)
        pe_ui_draw_text(&s_surface, PANEL_PAD * s, (HEADER_H + ROW_H * 0.5f + 6) * s, 16 * s, "Nothing here yet.",
                        pe_ui_muted, PE_ALIGN_LEFT);
    for (i = 0; i < visible && s_panel.scroll + i < s_panel.count; ++i)
        pe_paint_row(&s_panel.entries[s_panel.scroll + i], i, s_panel.scroll + i == s_panel.cursor);
    if (s_panel.count > visible) {                      /* scroll bar */
        const float track_top = (HEADER_H + 4) * s, track_h = (visible * ROW_H - 8) * s;
        const float thumb_h = track_h * (float)visible / (float)s_panel.count;
        const float thumb_top = track_top + (track_h - thumb_h) * (float)s_panel.scroll / (float)(s_panel.count - visible);
        pe_ui_fill_round_rect(&s_surface, (PANEL_W - 12) * s, track_top, 4 * s, track_h, 2 * s, pe_ui_alpha(pe_ui_text, 0.10f));
        pe_ui_fill_round_rect(&s_surface, (PANEL_W - 12) * s, thumb_top, 4 * s, thumb_h, 2 * s, pe_ui_alpha(pe_ui_accent, 0.65f));
    }
    pe_ui_fill_round_rect(&s_surface, PANEL_PAD * s, footer * s, (PANEL_W - 2 * PANEL_PAD) * s, 1, 0, pe_ui_alpha(pe_ui_text, 0.10f));
    pe_ui_draw_text(&s_surface, PANEL_PAD * s, (footer + 30) * s, 15 * s, pe_help_text(), pe_ui_text, PE_ALIGN_LEFT);
    pe_ui_draw_text(&s_surface, PANEL_PAD * s, (footer + 56) * s, 13 * s, pe_key_hints(), pe_ui_alpha(pe_ui_muted, 0.8f),
                    PE_ALIGN_LEFT);
}

static void pe_refresh(void) {
    static int s_last_w, s_last_h;
    int win_w, win_h, w, h;
    if (!pe_ui_font_ready() || !pe_ui_gpu_window_size(&win_w, &win_h)) return;
    if (win_w != s_last_w || win_h != s_last_h) s_panel.dirty = 1;
    if (!s_panel.dirty) return;
    s_last_w = win_w;
    s_last_h = win_h;
    s_scale = pe_layout_scale(win_w, win_h);
    w = (int)ceilf((float)PANEL_W * s_scale);
    h = (int)ceilf((float)pe_design_h() * s_scale);
    if (!pe_ui_surface_begin(&s_surface, w, h)) return;
    pe_paint(w, h);
    pe_ui_surface_finish(&s_surface);
    pe_ui_gpu_show(&s_surface, (win_w - w) / 2, (win_h - h) / 2, 0.62f);
    s_panel.dirty = 0;
}

/* ---- input ---- */
static void pe_move(int delta) {
    if (!s_panel.count) return;
    s_panel.cursor = (s_panel.cursor + delta + s_panel.count) % s_panel.count;
    pe_clamp_cursor();
}

static void pe_page_move(int delta) {
    if (!s_panel.count) return;
    s_panel.cursor += delta;
    if (s_panel.cursor < 0) s_panel.cursor = 0;
    pe_clamp_cursor();
}

static void pe_back(void) {
    if (pe_at_top()) s_panel.open = 0;
    else pe_go_up();
}

static void pe_accept(void) {
    const Entry *e = pe_selected();
    char page[96];
    if (!e) return;
    if (e->row >= 0) {
        PeMenuRow *row = pe_menu_row(e->row);
        if (row->kind == PE_ROW_DIGITS) {                /* type it in place */
            unsigned i;
            int v = *row->value;
            for (i = row->count; i-- > 0; v /= 10) s_edit.digit[i] = v % 10;
            s_edit.row = e->row;
            s_edit.pos = 0;
            return;
        }
        pe_menu_row_accept(row);
        return;
    }
    snprintf(page, sizeof page, "%s", e->page);
    pe_go(page, -1);
}

static void pe_step(int direction) {
    const Entry *e = pe_selected();
    if (e && e->row >= 0) pe_menu_row_step(pe_menu_row(e->row), direction);
}

/* C (or Ctrl+C, or the pad's west button) copies a selected digits row, Ctrl+V pastes into it. 0: not one. */
static int pe_clipboard(int paste) {
    const Entry *e = pe_selected();
    PeMenuRow *row;
    if (!e || e->row < 0 || (row = pe_menu_row(e->row))->kind != PE_ROW_DIGITS) return 0;
    s_flash.row = e->row;
    if (paste) s_flash.text = pe_menu_row_paste(row) ? "PASTED" : "NO SEED TO PASTE";
    else s_flash.text = pe_menu_row_copy(row) ? "COPIED" : "COPY FAILED";
    return 1;
}

static void pe_edit_done(int keep) {
    PeMenuRow *row = pe_menu_row(s_edit.row);
    unsigned i;
    if (keep) {
        *row->value = 0;
        for (i = 0; i < row->count; ++i) *row->value = *row->value * 10 + s_edit.digit[i];
    }
    s_edit.row = -1;
}

/* Keys while a digits row is being typed in; 1 if the key was the editor's. */
static int pe_edit_event(const SDL_Event *ev) {
    const int n = (int)pe_menu_row(s_edit.row)->count;
    int *d = &s_edit.digit[s_edit.pos], typed = -1;
    if (ev->type == SDL_EVENT_KEY_DOWN) {
        const SDL_Scancode k = ev->key.scancode;
        if (k >= SDL_SCANCODE_1 && k <= SDL_SCANCODE_9) typed = (int)(k - SDL_SCANCODE_1) + 1;
        else if (k >= SDL_SCANCODE_KP_1 && k <= SDL_SCANCODE_KP_9) typed = (int)(k - SDL_SCANCODE_KP_1) + 1;
        else if (k == SDL_SCANCODE_0 || k == SDL_SCANCODE_KP_0) typed = 0;
        if (typed >= 0) {
            *d = typed;
            if (s_edit.pos < n - 1) ++s_edit.pos;
            return 1;
        }
        switch (k) {
        case SDL_SCANCODE_LEFT: case SDL_SCANCODE_BACKSPACE: if (s_edit.pos > 0) --s_edit.pos; return 1;
        case SDL_SCANCODE_RIGHT: if (s_edit.pos < n - 1) ++s_edit.pos; return 1;
        case SDL_SCANCODE_UP: *d = (*d + 1) % 10; return 1;
        case SDL_SCANCODE_DOWN: *d = (*d + 9) % 10; return 1;
        case SDL_SCANCODE_HOME: s_edit.pos = 0; return 1;
        case SDL_SCANCODE_END: s_edit.pos = n - 1; return 1;
        case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER: case SDL_SCANCODE_SPACE:
            if (!ev->key.repeat) pe_edit_done(1);
            return 1;
        case SDL_SCANCODE_ESCAPE: if (!ev->key.repeat) pe_edit_done(0); return 1;
        case SDL_SCANCODE_V:
            if (ev->key.mod & SDL_KMOD_CTRL) { pe_edit_done(0); pe_clipboard(1); }
            return 1;
        case SDL_SCANCODE_F1: pe_edit_done(0); return 0;   /* the panel closes as usual */
        default: return 1;                                 /* nothing else reaches the list meanwhile */
        }
    }
    if (ev->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
        switch (ev->gbutton.button) {
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT: if (s_edit.pos > 0) --s_edit.pos; return 1;
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: if (s_edit.pos < n - 1) ++s_edit.pos; return 1;
        case SDL_GAMEPAD_BUTTON_DPAD_UP: *d = (*d + 1) % 10; return 1;
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN: *d = (*d + 9) % 10; return 1;
        case SDL_GAMEPAD_BUTTON_SOUTH: pe_edit_done(1); return 1;
        case SDL_GAMEPAD_BUTTON_EAST: pe_edit_done(0); return 1;
        case SDL_GAMEPAD_BUTTON_GUIDE: pe_edit_done(0); return 0;
        default: return 1;
        }
    }
    return 0;
}

static void pe_panel_event(const SDL_Event *ev) {
    if (ev->type == SDL_EVENT_QUIT || ev->type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
        s_panel.open = 0;
        SDL_PushEvent((SDL_Event *)ev);
        return;
    }
    if (ev->type == SDL_EVENT_KEY_DOWN || ev->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) s_flash.row = -1;
    if (s_edit.row >= 0 && pe_edit_event(ev)) {
        s_panel.dirty = 1;
        return;
    }
    if (ev->type == SDL_EVENT_KEY_DOWN && !ev->key.repeat &&
        (ev->key.scancode == SDL_SCANCODE_C || (ev->key.scancode == SDL_SCANCODE_V && (ev->key.mod & SDL_KMOD_CTRL))) &&
        pe_clipboard(ev->key.scancode == SDL_SCANCODE_V)) {
        s_panel.dirty = 1;
        return;
    }
    if (ev->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && ev->gbutton.button == SDL_GAMEPAD_BUTTON_WEST && pe_clipboard(0)) {
        s_panel.dirty = 1;
        return;
    }
    if (ev->type == SDL_EVENT_KEY_DOWN) {
        switch (ev->key.scancode) {
        case SDL_SCANCODE_F1: s_panel.open = 0; break;
        case SDL_SCANCODE_ESCAPE:
        case SDL_SCANCODE_BACKSPACE: if (!ev->key.repeat) pe_back(); break;
        case SDL_SCANCODE_UP: case SDL_SCANCODE_KP_8: pe_move(-1); break;
        case SDL_SCANCODE_DOWN: case SDL_SCANCODE_KP_2: pe_move(1); break;
        case SDL_SCANCODE_PAGEUP: pe_page_move(-VISIBLE_ROWS); break;
        case SDL_SCANCODE_PAGEDOWN: pe_page_move(VISIBLE_ROWS); break;
        case SDL_SCANCODE_HOME: s_panel.cursor = 0; pe_clamp_cursor(); break;
        case SDL_SCANCODE_END: s_panel.cursor = s_panel.count - 1; pe_clamp_cursor(); break;
        case SDL_SCANCODE_LEFT: case SDL_SCANCODE_KP_4: pe_step(-1); break;
        case SDL_SCANCODE_RIGHT: case SDL_SCANCODE_KP_6: pe_step(1); break;
        case SDL_SCANCODE_RETURN:
        case SDL_SCANCODE_KP_ENTER:
        case SDL_SCANCODE_SPACE: if (!ev->key.repeat) pe_accept(); break;
        default: return;
        }
    } else if (ev->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
        switch (ev->gbutton.button) {
        case SDL_GAMEPAD_BUTTON_DPAD_UP: pe_move(-1); break;
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN: pe_move(1); break;
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT: pe_step(-1); break;
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: pe_step(1); break;
#ifndef PE_PS5_PLUGINS
        case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: pe_page_move(-VISIBLE_ROWS); break;
        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: pe_page_move(VISIBLE_ROWS); break;
#endif
        case SDL_GAMEPAD_BUTTON_SOUTH: pe_accept(); break;
        case SDL_GAMEPAD_BUTTON_EAST: pe_back(); break;
        case SDL_GAMEPAD_BUTTON_GUIDE: s_panel.open = 0; break;
        default: return;
        }
    } else {
        return;
    }
    pe_build_entries();      /* an action may add or remove rows (e.g. a dynamic list) */
    pe_clamp_cursor();
    s_panel.dirty = 1;
}

static void pe_panel_run(void) {
    const int audio_paused = Psyz_AudioIsPaused();
    if (!pe_ui_gpu_ready()) {
        pe_mod_log(NULL, "Mods panel unavailable: no GPU overlay");
        return;
    }
    pe_toast_block(1);
    Psyz_PauseHostClock_SDL3(true);
    Psyz_AudioPause();
    Psyz_VideoSetSpeed(1);
    s_edit.row = -1;
    s_flash.row = -1;
    s_panel.open = 1;
    s_panel.dirty = 1;
    pe_open_page();
    while (s_panel.open) {
        pe_refresh();
        Psyz_VideoVSync(0);
    }
    *pe_remembered_cursor(s_panel.path) = s_panel.cursor;
    pe_ui_gpu_hide();
    pe_menu_save_settings();
    SDL_ResetKeyboard();     /* a held key must not reach the resumed pad poll */
    Psyz_PauseHostClock_SDL3(false);
    if (!audio_paused) Psyz_AudioUnpause();
    pe_toast_block(0);
#if PE_NATIVE_STATE_CPU_COMPONENTS
    extern void CPE_StateUi_AfterModalClosed(void);
    CPE_StateUi_AfterModalClosed();
#endif
}

#ifdef PE_PS5_PLUGINS
/* Chord state belongs to one physical controller. Partial chord presses
 * retain normal gameplay meaning while closed. Full release rearms toggle. */
static SDL_JoystickID s_chord_pad;
static unsigned s_chord_bits;
static int s_chord_latched, s_pad_drain, s_direct_drain;
int pe_ps5_mods_capture_pad(SDL_Gamepad *pad) {
    int i;
    if (s_panel.open) return 1;
    if (s_direct_drain) {
        int count, held = 0;
        SDL_JoystickID *ids = SDL_GetGamepads(&count);
        if (!ids) return 1;
        for (int n = 0; n < count && !held; ++n) {
            SDL_Gamepad *other = SDL_GetGamepadFromID(ids[n]);
            if (!other) continue;
            for (i = 0; i < SDL_GAMEPAD_BUTTON_COUNT; ++i)
                if (SDL_GetGamepadButton(other,(SDL_GamepadButton)i)) held = 1;
            for (i = 0; i < SDL_GAMEPAD_AXIS_COUNT; ++i) {
                int v = SDL_GetGamepadAxis(other,(SDL_GamepadAxis)i);
                if (v > 8000 || v < -8000) held = 1;
            }
        }
        SDL_free(ids);
        if (held) return 1;
        s_direct_drain = 0;
    }
    if (!s_pad_drain || SDL_GetGamepadID(pad) != s_chord_pad) return 0;
    for (i = 0; i < SDL_GAMEPAD_BUTTON_COUNT; ++i)
        if (SDL_GetGamepadButton(pad, (SDL_GamepadButton)i)) return 1;
    for (i = 0; i < SDL_GAMEPAD_AXIS_COUNT; ++i) {
        int value = SDL_GetGamepadAxis(pad, (SDL_GamepadAxis)i);
        if (value > 8000 || value < -8000) return 1;
    }
    s_pad_drain = 0;
    return 0;
}
static int pe_ps5_chord(const SDL_Event *ev) {
    unsigned bit = 0;
    if (ev->type == SDL_EVENT_GAMEPAD_REMOVED && ev->gdevice.which == s_chord_pad) {
        s_chord_pad = 0; s_chord_bits = 0;
        s_chord_latched = s_pad_drain = 0;
        return 0;
    }
    if (ev->type != SDL_EVENT_GAMEPAD_BUTTON_DOWN && ev->type != SDL_EVENT_GAMEPAD_BUTTON_UP) return 0;
    switch (ev->gbutton.button) {
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: bit = 1; break;
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: bit = 2; break;
    case SDL_GAMEPAD_BUTTON_TOUCHPAD: bit = 4; break;
    default: return 0;
    }
    if (s_chord_pad && s_chord_pad != ev->gbutton.which && (s_chord_bits || s_chord_latched)) return 0;
    s_chord_pad = ev->gbutton.which;
    if (ev->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) s_chord_bits |= bit;
    else s_chord_bits &= ~bit;
    if (!s_chord_bits) {
        int consumed = s_chord_latched;
        s_chord_latched = 0;
        return consumed;
    }
    if (s_chord_latched) return 1;
    if (s_chord_bits != 7) return 0;
    s_chord_latched = s_pad_drain = 1;
    if (s_panel.open) s_panel.open = 0;
    else {
#ifdef PE_PS5_GPU_NATIVE
        extern int CPE_HostOpenCheatsTab(void);CPE_HostOpenCheatsTab();
#else
        pe_panel_run();
#endif
    }
    return 1;
}
#endif
static bool pe_filter(const SDL_Event *ev) {
#ifdef PE_PS5_PLUGINS
    if (pe_ps5_chord(ev)) return true;
    /* Device lifecycle must still reach PsyZ while the panel is modal. */
    if (ev->type == SDL_EVENT_GAMEPAD_REMOVED || ev->type == SDL_EVENT_GAMEPAD_ADDED) return false;
#endif
    if (s_panel.open) {
        if (ev->type == SDL_EVENT_WINDOW_RESIZED || ev->type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED ||
            ev->type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED) {
            s_panel.dirty = 1;
            return false;
        }
        pe_panel_event(ev);
        return true;
    }
    if (s_prev_filter && s_prev_filter(ev)) {
        return true;
    }
    if ((ev->type == SDL_EVENT_KEY_DOWN && ev->key.scancode == SDL_SCANCODE_F1 && !ev->key.repeat) ||
        (ev->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && ev->gbutton.button == SDL_GAMEPAD_BUTTON_GUIDE)) {
#ifdef PE_PS5_GPU_NATIVE
        extern int CPE_HostOpenRegistryPage(const char *);CPE_HostOpenRegistryPage(NULL);
#else
        pe_panel_run();
#endif
        return true;
    }
    return false;
}

int pe_mod_panel_open_page(const char *page) {
#ifdef PE_PS5_GPU_NATIVE
    extern int CPE_HostOpenRegistryPage(const char *);return CPE_HostOpenRegistryPage(page);
#endif
    int i, found = 0;
    if (!pe_ui_gpu_ready() || s_panel.open) return 0;
    if (page) for (i = 0; i < pe_menu_row_count(); ++i)
        if (!strcmp(pe_menu_row(i)->page, page)) { found = 1; break; }
    pe_go(found ? page : PE_MENU_MANAGE, 0);
#ifdef PE_PS5_PLUGINS
    s_direct_drain = 1; /* release the closing button before original pad polling */
#endif
    pe_panel_run();
    return 1;
}

void pe_panel_install(void) {
    pe_ui_gpu_install();
    pe_toast_install();
    s_prev_filter = Psyz_SetOverlayEventFilter_SDL3(pe_filter);
    pe_mod_log(NULL, "Mods panel on F1");
}

#if PE_NATIVE_STATE_CPU_COMPONENTS
int pe_mod_state_panel_closed(void){return !s_panel.open;}
#endif

#if PE_NATIVE_STATE_CPU_COMPONENTS
static void (*s_native_close)(void);
void pe_mod_panel_set_native_close(void (*callback)(void)){s_native_close=callback;}
void pe_mod_panel_close(void){s_panel.open=0;if(s_native_close)s_native_close();}
#endif
