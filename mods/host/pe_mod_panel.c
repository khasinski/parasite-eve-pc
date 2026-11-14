/* The Mods panel: F1 opens a paused list of every mod's rows (pe_mod_menu.h).
 *
 * Navigation: each page lists its own rows first, then links to its sub-pages (in the order
 * pe_menu_page gives them). The header shows the mod big and the page you are on small; the
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
#include "ui/pe_utf8.h"

void pe_toast_block(int blocked);
void pe_toast_install(void);

/* ---- layout, in design units scaled with the window ---- */
enum {
    PANEL_W = 700, PANEL_PAD = 30, HEADER_H = 100, ROW_H = 42, VISIBLE_ROWS = 12, FOOTER_H = 100,
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

/* Every page wears the refined look (pe_paint): a dark window, a framed help box. */
static int pe_design_h(void) { return HEADER_H + pe_visible_rows() * ROW_H + FOOTER_H; }

static float pe_layout_scale(int win_w, int win_h, int design_h) {
    float scale = (float)win_h / (float)REFERENCE_H;
    const float wl = (float)win_w * 0.94f / (float)PANEL_W, hl = (float)win_h * 0.94f / (float)design_h;
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
    else if (e && pe_menu_row(e->row)->kind == PE_ROW_ACTION) action = "Enter run";
    else if (e && pe_menu_row_adjustable(pe_menu_row(e->row))) action = "Left/Right change";
    snprintf(hints, sizeof hints, pe_l10n_ui("Up/Down select   %s%s%s   F1 close"), action, *action ? "   " : "", back);
    return hints;
}

/* ---- the refined look: deep navy window, silver double frame, red ---- */
static const PeColor pe_look_red = {0.90f, 0.14f, 0.15f, 1.0f};
static const PeColor pe_look_steel = {0.42f, 0.58f, 0.80f, 1.0f};
static const PeColor pe_look_silver = {0.80f, 0.86f, 0.95f, 1.0f};
static const PeColor pe_look_text = {0.92f, 0.94f, 0.98f, 1.0f};
static const PeColor pe_look_dim = {0.55f, 0.62f, 0.74f, 1.0f};

/* Whole pixels, so stacked strips meet without seams. */
static void pe_px_rect(int x, int y, int w, int h, PeColor c) {
    if (w > 0 && h > 0) pe_ui_fill_round_rect(&s_surface, (float)x, (float)y, (float)w, (float)h, 0, c);
}

static PeColor pe_mix(PeColor a, PeColor b, float t) {
    PeColor c = {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
    return c;
}

static void pe_vgradient(int x, int y, int w, int h, PeColor top, PeColor bottom) {
    int i;
    for (i = 0; i < h; ++i) pe_px_rect(x, y + i, w, 1, pe_mix(top, bottom, h > 1 ? (float)i / (float)(h - 1) : 0));
}

static void pe_hgradient(int x, int y, int w, int h, PeColor left, PeColor right) {
    int i;
    for (i = 0; i < w; ++i) pe_px_rect(x + i, y, 1, h, pe_mix(left, right, w > 1 ? (float)i / (float)(w - 1) : 0));
}

static void pe_frame(int x, int y, int w, int h, PeColor c) {
    pe_px_rect(x, y, w, 1, c);
    pe_px_rect(x, y + h - 1, w, 1, c);
    pe_px_rect(x, y + 1, 1, h - 2, c);
    pe_px_rect(x + w - 1, y + 1, 1, h - 2, c);
}

/* Bright L-shaped brackets on a box's four corners. */
static void pe_corners(int x, int y, int w, int h, int len, int t, PeColor c) {
    pe_px_rect(x, y, len, t, c);                 pe_px_rect(x, y, t, len, c);
    pe_px_rect(x + w - len, y, len, t, c);       pe_px_rect(x + w - t, y, t, len, c);
    pe_px_rect(x, y + h - t, len, t, c);         pe_px_rect(x, y + h - len, t, len, c);
    pe_px_rect(x + w - len, y + h - t, len, t, c); pe_px_rect(x + w - t, y + h - len, t, len, c);
}

/* A solid triangle `size` tall, its point at (x, y): dir 1 points right, -1 left. */
static void pe_arrow(float x, float y, float size, int dir, PeColor c) {
    const int half = (int)(size * 0.5f + 0.5f), px = (int)(x + 0.5f), py = (int)(y + 0.5f);
    int i;
    for (i = 0; i < half; ++i) {
        const int col = dir > 0 ? px - 1 - i : px + i;      /* i columns back from the point, i + 1 high each way */
        pe_px_rect(col, py - i - 1, 1, 2 * i + 2, c);
    }
}

/* Letter-spaced text (`track` pixels between letters). Returns its width. */
static float pe_spaced_width(const char *text, float em, float track) {
    const char *p = text;
    int letters = 0;
    while (*p) { pe_utf8_next(&p); ++letters; }
    return pe_ui_text_width(text, em) + (letters > 1 ? (float)(letters - 1) * track : 0);
}

static void pe_spaced(float x, float baseline, float em, float track, const char *text, PeColor c, PeAlign align) {
    char one[8];
    if (align != PE_ALIGN_LEFT) x -= pe_spaced_width(text, em, track) * (align == PE_ALIGN_CENTRE ? 0.5f : 1.0f);
    while (*text) {
        const char *next = text;
        size_t n;
        pe_utf8_next(&next);
        n = (size_t)(next - text);
        memcpy(one, text, n);
        one[n] = '\0';
        pe_ui_draw_text(&s_surface, x, baseline, em, one, c, PE_ALIGN_LEFT);
        x += pe_ui_text_width(one, em) + track;
        text = next;
    }
}

static void pe_upper(char *out, size_t size, const char *in) {
    size_t i;
    for (i = 0; in[i] && i + 1 < size; ++i) out[i] = (char)(in[i] >= 'a' && in[i] <= 'z' ? in[i] - 32 : in[i]);
    out[i] = '\0';
}

/* Word-wrap `text` into up to `lines` lines of at most `width` pixels at `em`; the last line takes the rest. */
static void pe_wrapped(float x, float baseline, float step, float width, float em, int lines, const char *text, PeColor c) {
    char line[320];
    int n;
    for (n = 0; n < lines && *text; ++n) {
        size_t fit = 0, i = 0;
        if (n < lines - 1)
            for (;;) {
                size_t next = i;
                while (text[next] && text[next] != ' ') ++next;
                if (next >= sizeof line) break;
                memcpy(line, text, next);
                line[next] = '\0';
                if (fit && pe_ui_text_width(line, em) > width) break;
                fit = next;
                if (!text[next]) break;
                i = next + 1;
            }
        else
            fit = strlen(text);
        if (fit >= sizeof line) fit = sizeof line - 1;
        if (!fit) fit = strlen(text) < sizeof line - 1 ? strlen(text) : sizeof line - 1;
        memcpy(line, text, fit);
        line[fit] = '\0';
        pe_ui_draw_text(&s_surface, x, baseline + step * (float)n, em, line, c, PE_ALIGN_LEFT);
        text += fit;
        while (*text == ' ') ++text;
    }
}

/* A row's ground: the selected one's band and cursor, or the rule above an unselected one. */
static void pe_paint_row_ground(int slot, int selected) {
    const float s = s_scale, top = (HEADER_H + slot * ROW_H) * s, mid = top + ROW_H * 0.5f * s;
    const float label_x = (PANEL_PAD + 16) * s;
    const int band_x = (int)(PANEL_PAD * 0.5f * s), band_y = (int)(top + 4 * s), band_w = (int)((PANEL_W - PANEL_PAD) * s),
              band_h = (int)((ROW_H - 8) * s);
    if (selected) {                                     /* a steel band fading right, hairlines, the red cursor */
        pe_hgradient(band_x, band_y, band_w, band_h, pe_ui_alpha(pe_look_steel, 0.34f), pe_ui_alpha(pe_look_steel, 0.0f));
        pe_hgradient(band_x, band_y, band_w, 1, pe_ui_alpha(pe_look_silver, 0.55f), pe_ui_alpha(pe_look_silver, 0.0f));
        pe_hgradient(band_x, band_y + band_h - 1, band_w, 1, pe_ui_alpha(pe_look_silver, 0.40f),
                     pe_ui_alpha(pe_look_silver, 0.0f));
        pe_arrow(label_x - 6 * s, mid, 12 * s, 1, pe_look_red);
    } else if (slot > 0) {                              /* a faint rule between rows */
        pe_hgradient((int)(label_x), (int)top, (int)((PANEL_W - 2 * PANEL_PAD - 16) * s), 1,
                     pe_ui_alpha(pe_look_silver, 0.07f), pe_ui_alpha(pe_look_silver, 0.0f));
    }
}

static void pe_paint_row_pe(const Entry *e, int slot, int selected) {
    const float s = s_scale, top = (HEADER_H + slot * ROW_H) * s, mid = top + ROW_H * 0.5f * s, base = mid + 6 * s;
    const float label_x = (PANEL_PAD + 16) * s, value_x = (PANEL_W - PANEL_PAD - VALUE_W * 0.5f) * s;
    const int off = e->row >= 0 ? !pe_menu_row_active(pe_menu_row(e->row)) : !pe_page_active(e->page);
    const PeColor label = pe_ui_alpha(pe_look_text, (selected ? 1.0f : 0.78f) * (off ? 0.55f : 1.0f));
    char value[96];
    pe_paint_row_ground(slot, selected);
    if (e->row < 0) {                                   /* link */
        pe_ui_draw_text(&s_surface, label_x, base, 17 * s, e->page + e->name_at, label, PE_ALIGN_LEFT);
        pe_arrow((PANEL_W - PANEL_PAD) * s, mid, 10 * s, 1, selected ? pe_look_red : pe_ui_alpha(pe_look_dim, 0.7f));
        return;
    }
    {
        PeMenuRow *row = pe_menu_row(e->row);
        pe_ui_draw_text(&s_surface, label_x, base, 17 * s, row->label, label, PE_ALIGN_LEFT);
        if (row->kind == PE_ROW_ACTION) {
            if (selected) pe_spaced(value_x, base - 1 * s, 12 * s, 2.5f * s, off ? "OFF" : "PRESS ENTER",
                                    off ? pe_look_dim : pe_look_red, PE_ALIGN_CENTRE);
            return;
        }
        pe_menu_row_value_text(row, value, sizeof value);
        if (row->kind == PE_ROW_INFO) {
            pe_ui_draw_text(&s_surface, value_x, base, 16 * s, value, pe_ui_alpha(pe_look_dim, 0.95f), PE_ALIGN_CENTRE);
            return;
        }
        {                                               /* a framed field for values that change */
            const int fx = (int)(value_x - (VALUE_W * 0.5f - 6) * s), fy = (int)(top + 8 * s),
                      fw = (int)((VALUE_W - 12) * s), fh = (int)((ROW_H - 16) * s);
            pe_px_rect(fx, fy, fw, fh, pe_ui_alpha((PeColor){0, 0, 0, 1}, selected ? 0.45f : 0.25f));
            pe_frame(fx, fy, fw, fh, pe_ui_alpha(pe_look_silver, selected ? 0.38f : 0.12f));
        }
        if (strcmp(value, "On") == 0 || strcmp(value, "Off") == 0) {
            const int on = value[1] == 'n';
            pe_spaced(value_x, base - 1 * s, 14 * s, 3 * s, on ? "ON" : "OFF",
                      on ? (selected ? pe_look_red : pe_ui_alpha(pe_look_red, 0.85f)) : pe_ui_alpha(pe_look_dim, 0.9f),
                      PE_ALIGN_CENTRE);
        } else {
            pe_ui_draw_text(&s_surface, value_x, base, 17 * s, value, selected ? pe_look_text : pe_ui_alpha(pe_look_text, 0.85f),
                            PE_ALIGN_CENTRE);
        }
        if (selected) {
            const float reach = (VALUE_W * 0.5f - 22) * s;
            pe_arrow(value_x - reach, mid, 10 * s, -1, pe_look_red);
            pe_arrow(value_x + reach, mid, 10 * s, 1, pe_look_red);
        }
    }
}

/* The window: navy to black, faint scanlines, a silver double frame with bright corners; the
 * game's name in red at the top. */
static void pe_paint_window(int w, int h) {
    const float s = s_scale;
    int i;
    pe_vgradient(0, 0, w, h, (PeColor){0.055f, 0.075f, 0.140f, 0.97f}, (PeColor){0.010f, 0.014f, 0.032f, 0.97f});
    for (i = 0; i < h; i += 3) pe_px_rect(1, i, w - 2, 1, pe_ui_alpha(pe_look_silver, 0.022f));
    pe_frame(0, 0, w, h, pe_ui_alpha(pe_look_silver, 0.50f));
    pe_frame((int)(5 * s), (int)(5 * s), w - 2 * (int)(5 * s), h - 2 * (int)(5 * s), pe_ui_alpha(pe_look_silver, 0.13f));
    pe_corners(0, 0, w, h, (int)(22 * s), (int)(2 * s) > 1 ? (int)(2 * s) : 2, pe_ui_alpha(pe_look_silver, 0.95f));
    pe_spaced(PANEL_PAD * s, 32 * s, 11 * s, 4 * s, "PARASITE EVE", pe_look_red, PE_ALIGN_LEFT);
}

/* The red rule fading out under the header. */
static void pe_paint_rule(void) {
    const float s = s_scale;
    pe_hgradient((int)(PANEL_PAD * s), (int)(80 * s), (int)((PANEL_W - 2 * PANEL_PAD) * s), (int)(2 * s) > 1 ? (int)(2 * s) : 2,
                 pe_look_red, pe_ui_alpha(pe_look_red, 0.0f));
    pe_px_rect((int)(PANEL_PAD * s), (int)(86 * s), (int)((PANEL_W - 2 * PANEL_PAD) * s), 1, pe_ui_alpha(pe_look_silver, 0.08f));
}

/* Scroll bar: a hairline track, a red thumb. */
static void pe_paint_scroll(int visible, int count, int scroll) {
    const float s = s_scale;
    const float track_top = (HEADER_H + 4) * s, track_h = (visible * ROW_H - 8) * s;
    const float thumb_h = track_h * (float)visible / (float)count;
    const float thumb_top = track_top + (track_h - thumb_h) * (float)scroll / (float)(count - visible);
    pe_px_rect((int)((PANEL_W - 14) * s), (int)track_top, 1, (int)track_h, pe_ui_alpha(pe_look_silver, 0.15f));
    pe_px_rect((int)((PANEL_W - 15) * s), (int)thumb_top, (int)(3 * s), (int)thumb_h, pe_look_red);
}

/* The help box, as the game frames its own: the description, then the keys. */
static void pe_paint_help(float footer, const char *help, const char *keys) {
    const float s = s_scale;
    const int bx = (int)(PANEL_PAD * 0.5f * s), by = (int)((footer + 8) * s), bw = (int)((PANEL_W - PANEL_PAD) * s),
              bh = (int)((FOOTER_H - 20) * s);
    char text[160];
    pe_vgradient(bx, by, bw, bh, (PeColor){0.02f, 0.03f, 0.06f, 0.85f}, (PeColor){0.0f, 0.0f, 0.0f, 0.85f});
    pe_frame(bx, by, bw, bh, pe_ui_alpha(pe_look_silver, 0.30f));
    pe_corners(bx, by, bw, bh, (int)(10 * s), 1, pe_ui_alpha(pe_look_silver, 0.85f));
    pe_wrapped(PANEL_PAD * s, (footer + 31) * s, 19 * s, (PANEL_W - 2 * PANEL_PAD) * s, 14 * s, 2, help, pe_look_text);
    pe_upper(text, sizeof text, keys);
    pe_spaced(PANEL_PAD * s, (footer + 72) * s, 10 * s, 1.6f * s, text, pe_ui_alpha(pe_look_dim, 0.85f), PE_ALIGN_LEFT);
}

static void pe_paint(int w, int h) {
    const float s = s_scale;
    const int visible = pe_visible_rows();
    const float footer = (float)(HEADER_H + visible * ROW_H);
    const char *sub = strchr(s_panel.path, '/');
    char text[160];
    int i;
    pe_paint_window(w, h);
    /* Header: the page in big spaced capitals, a red rule fading out. */
    pe_spaced((PANEL_W - PANEL_PAD) * s, 32 * s, 11 * s, 3 * s, "GAME PAUSED", pe_ui_alpha(pe_look_dim, 0.85f), PE_ALIGN_RIGHT);
    {   /* the mod's name (its top page) big on the left, the page you're on small on the right */
        char page[160] = "";
        float em = 28 * s, track = 5 * s, room = (PANEL_W - 2 * PANEL_PAD) * s;
        if (sub) {
            pe_upper(page, sizeof page, pe_l10n_ui(strrchr(s_panel.path, '/') + 1));
            room -= pe_spaced_width(page, 14 * s, 3 * s) + 32 * s;
        }
        if (!s_panel.path[0]) snprintf(text, sizeof text, "MODS");
        else {
            char top[96];
            snprintf(top, sizeof top, "%.*s", (int)strcspn(s_panel.path, "/"), s_panel.path);
            pe_upper(text, sizeof text, pe_l10n_ui(top));
        }
        while (em > 16 * s && pe_spaced_width(text, em, track) > room) { em -= 1 * s; track = em * 5 / 28; }
        pe_spaced(PANEL_PAD * s, 66 * s, em, track, text, pe_look_text, PE_ALIGN_LEFT);
        if (sub) {
            pe_spaced((PANEL_W - PANEL_PAD) * s, 66 * s, 14 * s, 3 * s, page, pe_look_steel, PE_ALIGN_RIGHT);
            pe_arrow((PANEL_W - PANEL_PAD) * s - pe_spaced_width(page, 14 * s, 3 * s) - 10 * s, 61 * s, 9 * s, -1,
                     pe_ui_alpha(pe_look_steel, 0.8f));
        }
    }
    if (s_panel.path[0] && !pe_page_active(s_panel.path))
        pe_spaced((PANEL_W - PANEL_PAD) * s, sub ? 46 * s : 66 * s, 11 * s, 3 * s, "MOD OFF", pe_look_dim, PE_ALIGN_RIGHT);
    pe_paint_rule();
    if (!s_panel.count)
        pe_ui_draw_text(&s_surface, (PANEL_PAD + 16) * s, (HEADER_H + ROW_H * 0.5f + 6) * s, 16 * s, "Nothing here yet.",
                        pe_look_dim, PE_ALIGN_LEFT);
    for (i = 0; i < visible && s_panel.scroll + i < s_panel.count; ++i)
        pe_paint_row_pe(&s_panel.entries[s_panel.scroll + i], i, s_panel.scroll + i == s_panel.cursor);
    if (s_panel.count > visible) pe_paint_scroll(visible, s_panel.count, s_panel.scroll);
    pe_paint_help(footer, pe_help_text(), pe_key_hints());
}

/* Paint a window `design_h` units tall with `paint` and show it centred, when `*dirty` or the
 * game's window changed size. */
static void pe_show(int *dirty, int design_h, void (*paint)(int w, int h)) {
    static int s_last_w, s_last_h;
    int win_w, win_h, w, h;
    if (!pe_ui_font_ready() || !pe_ui_gpu_window_size(&win_w, &win_h)) return;
    if (win_w != s_last_w || win_h != s_last_h) *dirty = 1;
    if (!*dirty) return;
    s_last_w = win_w;
    s_last_h = win_h;
    s_scale = pe_layout_scale(win_w, win_h, design_h);
    w = (int)ceilf((float)PANEL_W * s_scale);
    h = (int)ceilf((float)design_h * s_scale);
    if (!pe_ui_surface_begin(&s_surface, w, h)) return;
    paint(w, h);
    pe_ui_surface_finish(&s_surface);
    pe_ui_gpu_show(&s_surface, (win_w - w) / 2, (win_h - h) / 2, 0.62f);
    *dirty = 0;
}

static void pe_refresh(void) { pe_show(&s_panel.dirty, pe_design_h(), pe_paint); }

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
        pe_menu_row_accept(pe_menu_row(e->row));
        return;
    }
    snprintf(page, sizeof page, "%s", e->page);
    pe_go(page, -1);
}

static void pe_step(int direction) {
    const Entry *e = pe_selected();
    if (e && e->row >= 0) pe_menu_row_step(pe_menu_row(e->row), direction);
}

static int s_open_key, s_open_button = -1, s_open_trigger_held;
void pe_mod_panel_set_open_binding(int key, int button) {
    s_open_key = key;
    s_open_button = button;
    s_open_trigger_held = 0;
}
static int pe_open_binding_event(const SDL_Event *ev) {
    if (ev->type == SDL_EVENT_KEY_DOWN)
        return s_open_key > SDL_SCANCODE_UNKNOWN && (int)ev->key.scancode == s_open_key && !ev->key.repeat;
    if (ev->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN)
        return s_open_button >= 0 && s_open_button < SDL_GAMEPAD_BUTTON_COUNT && ev->gbutton.button == s_open_button;
    if (ev->type == SDL_EVENT_GAMEPAD_AXIS_MOTION && s_open_button >= SDL_GAMEPAD_BUTTON_COUNT &&
        ev->gaxis.axis == (s_open_button == SDL_GAMEPAD_BUTTON_COUNT ? SDL_GAMEPAD_AXIS_LEFT_TRIGGER
                                                                    : SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)) {
        const int was = s_open_trigger_held;
        s_open_trigger_held = ev->gaxis.value > (was ? 8000 : 16000);
        return s_open_trigger_held && !was;
    }
    return 0;
}

static void pe_panel_event(const SDL_Event *ev) {
    if (pe_open_binding_event(ev)) {
        s_panel.open = 0;
        return;
    }
    if (ev->type == SDL_EVENT_QUIT || ev->type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
        s_panel.open = 0;
        SDL_PushEvent((SDL_Event *)ev);
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

/* ---- a question before the game goes on: one of a list of rows ----
 * In the panel's look and modal like it, for a choice the game needs before it loads anything
 * (pe_lang: the language, the first start with packs). A row is the only way out: Escape and F1
 * do nothing here. Closing the game's window leaves it unanswered. */
static struct {
    int open, dirty, cursor, scroll, result, count;
    const char *const *names;
    const char *const *descriptions; /* the title look: one per row, or NULL */
    const char *title, *help;
} s_choose;

/* Title rows are larger than panel rows; cap their viewport instead of shrinking long lists. */
static int pe_choose_visible(void) {
    const int limit = s_choose.descriptions ? 6 : VISIBLE_ROWS;
    return s_choose.count < limit ? s_choose.count : limit;
}

static void pe_paint_choose(int w, int h) {
    const float s = s_scale;
    const int visible = pe_choose_visible();
    char text[160];
    int i;
    pe_paint_window(w, h);
    pe_upper(text, sizeof text, s_choose.title);
    pe_spaced(PANEL_PAD * s, 66 * s, 28 * s, 5 * s, text, pe_look_text, PE_ALIGN_LEFT);
    pe_paint_rule();
    for (i = 0; i < visible; ++i) {
        const int at = s_choose.scroll + i, selected = at == s_choose.cursor;
        const float mid = (HEADER_H + i * ROW_H + ROW_H * 0.5f) * s;
        pe_paint_row_ground(i, selected);
        pe_ui_draw_text(&s_surface, (PANEL_PAD + 16) * s, mid + 6 * s, 17 * s, s_choose.names[at],
                        pe_ui_alpha(pe_look_text, selected ? 1.0f : 0.78f), PE_ALIGN_LEFT);
    }
    if (s_choose.count > visible) pe_paint_scroll(visible, s_choose.count, s_choose.scroll);
    pe_paint_help((float)(HEADER_H + visible * ROW_H), s_choose.help, "Up/Down select   Enter choose");
}

static void pe_choose_event(const SDL_Event *ev) {
    const int n = s_choose.count, visible = pe_choose_visible();
    int accept = 0;
    if (ev->type == SDL_EVENT_QUIT || ev->type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
        s_choose.result = -1;
        s_choose.open = 0;
        SDL_PushEvent((SDL_Event *)ev);
        return;
    }
    if (ev->type == SDL_EVENT_KEY_DOWN) {
        switch (ev->key.scancode) {
        case SDL_SCANCODE_UP: case SDL_SCANCODE_KP_8: s_choose.cursor = (s_choose.cursor + n - 1) % n; break;
        case SDL_SCANCODE_DOWN: case SDL_SCANCODE_KP_2: s_choose.cursor = (s_choose.cursor + 1) % n; break;
        case SDL_SCANCODE_HOME: s_choose.cursor = 0; break;
        case SDL_SCANCODE_END: s_choose.cursor = n - 1; break;
        case SDL_SCANCODE_RETURN:
        case SDL_SCANCODE_KP_ENTER:
        case SDL_SCANCODE_SPACE: accept = !ev->key.repeat; break;
        default: return;
        }
    } else if (ev->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
        switch (ev->gbutton.button) {
        case SDL_GAMEPAD_BUTTON_DPAD_UP: s_choose.cursor = (s_choose.cursor + n - 1) % n; break;
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN: s_choose.cursor = (s_choose.cursor + 1) % n; break;
        case SDL_GAMEPAD_BUTTON_SOUTH: accept = 1; break;
        default: return;
        }
    } else {
        return;
    }
    if (s_choose.cursor < s_choose.scroll) s_choose.scroll = s_choose.cursor;
    if (s_choose.cursor >= s_choose.scroll + visible) s_choose.scroll = s_choose.cursor - visible + 1;
    s_choose.dirty = 1;
    if (accept) {
        s_choose.result = s_choose.cursor;
        s_choose.open = 0;
    }
}

/* ---- the same question in the title menu's look ----
 * Centred words like the title's New Game / Continue / Tutorial, a soft green glow behind the
 * selected one, and that row's description under the list. No panel frame: a dark veil over
 * the (dimmed) screen. */
enum { TITLE_TOP = 64, TITLE_ROW_H = 46, TITLE_DESC_H = 132 };
static const PeColor pe_title_glow = {0.30f, 0.95f, 0.50f, 1.0f};

static int pe_title_design_h(int count) { return TITLE_TOP + count * TITLE_ROW_H + TITLE_DESC_H; }

static void pe_paint_title(int w, int h) {
    const float s = s_scale;
    const float cx = (float)w * 0.5f;
    const float desc_top = (float)(TITLE_TOP + pe_choose_visible() * TITLE_ROW_H);
    int i;
    pe_ui_fill_round_rect(&s_surface, 0, 0, (float)w, (float)h, 18 * s, (PeColor){0.0f, 0.0f, 0.0f, 0.72f});
    if (s_choose.title[0]) {
        char text[160];
        pe_upper(text, sizeof text, s_choose.title);
        pe_spaced(cx, 40 * s, 13 * s, 4 * s, text, pe_ui_alpha(pe_look_dim, 0.9f), PE_ALIGN_CENTRE);
    }
    for (i = 0; i < pe_choose_visible(); ++i) {
        const int at = s_choose.scroll + i, selected = at == s_choose.cursor;
        const float em = 27 * s, mid = (TITLE_TOP + i * TITLE_ROW_H + TITLE_ROW_H * 0.5f) * s;
        if (selected) {   /* the title's glow: soft green layers behind the word */
            const float tw = pe_ui_text_width(s_choose.names[at], em);
            int k;
            for (k = 0; k < 4; ++k) {
                const float grow = (float)(3 - k) * 7 * s;
                pe_ui_fill_round_rect(&s_surface, cx - tw * 0.5f - 18 * s - grow, mid - 17 * s - grow * 0.45f,
                                      tw + 36 * s + 2 * grow, 34 * s + grow * 0.9f, 17 * s + grow * 0.45f,
                                      pe_ui_alpha(pe_title_glow, 0.10f + 0.07f * (float)k));
            }
        }
        pe_ui_draw_text(&s_surface, cx, mid + 9 * s, em, s_choose.names[at],
                        pe_ui_alpha(pe_look_text, selected ? 1.0f : 0.70f), PE_ALIGN_CENTRE);
    }
    if (s_choose.count > pe_choose_visible()) {
        char range[40];
        snprintf(range, sizeof range, "%d-%d / %d", s_choose.scroll + 1,
                 s_choose.scroll + pe_choose_visible(), s_choose.count);
        pe_ui_draw_text(&s_surface, (PANEL_W - PANEL_PAD) * s, (desc_top - 2) * s,
                        11 * s, range, pe_look_dim, PE_ALIGN_RIGHT);
    }
    pe_px_rect((int)(PANEL_PAD * s), (int)((desc_top + 4) * s), (int)((PANEL_W - 2 * PANEL_PAD) * s), 1,
               pe_ui_alpha(pe_look_silver, 0.18f));
    if (s_choose.descriptions && s_choose.descriptions[s_choose.cursor])
        pe_wrapped(PANEL_PAD * s, (desc_top + 32) * s, 21 * s, (PANEL_W - 2 * PANEL_PAD) * s, 15 * s, 3,
                   s_choose.descriptions[s_choose.cursor], pe_look_text);
    {
        char keys[96];
        pe_upper(keys, sizeof keys, s_choose.help[0] ? s_choose.help : "Up/Down select   Enter choose");
        pe_spaced(cx, (desc_top + TITLE_DESC_H - 18) * s, 10 * s, 1.6f * s, keys, pe_ui_alpha(pe_look_dim, 0.85f),
                  PE_ALIGN_CENTRE);
    }
}

static int pe_choose_run(const char *title, const char *help, const char *const *names,
                         const char *const *descriptions, unsigned count, int initial);

int pe_menu_choose(const char *title, const char *help, const char *const *names, unsigned count, int initial) {
    return pe_choose_run(title, help, names, NULL, count, initial);
}
int pe_menu_choose_title(const char *title, const char *keys, const char *const *names,
                         const char *const *descriptions, unsigned count, int initial) {
    if (!descriptions) return -1;
    return pe_choose_run(title, keys, names, descriptions, count, initial);
}

static int pe_choose_run(const char *title, const char *help, const char *const *names,
                         const char *const *descriptions, unsigned count, int initial) {
    const int audio_paused = Psyz_AudioIsPaused();
    if (!names || !count || count > MAX_ENTRIES || s_panel.open || s_choose.open || !pe_mod_interactive()) return -1;
    if (!pe_ui_gpu_ready()) {
        pe_mod_log(NULL, "question unavailable: no GPU overlay");
        return -1;
    }
    memset(&s_choose, 0, sizeof s_choose);
    s_choose.descriptions = descriptions;
    s_choose.names = names;
    s_choose.count = (int)count;
    s_choose.title = title ? title : "";
    s_choose.help = help ? help : "";
    s_choose.cursor = initial > 0 && initial < (int)count ? initial : 0;
    if (s_choose.cursor >= pe_choose_visible()) s_choose.scroll = s_choose.cursor - pe_choose_visible() + 1;
    s_choose.result = -1;
    pe_toast_block(1);
    Psyz_PauseHostClock_SDL3(true);
    Psyz_AudioPause();
    s_choose.open = s_choose.dirty = 1;
    while (s_choose.open) {
        if (s_choose.descriptions)
            pe_show(&s_choose.dirty, pe_title_design_h(pe_choose_visible()), pe_paint_title);
        else
            pe_show(&s_choose.dirty, HEADER_H + pe_choose_visible() * ROW_H + FOOTER_H, pe_paint_choose);
        Psyz_VideoVSync(0);
    }
    pe_ui_gpu_hide();
    SDL_ResetKeyboard();     /* a held key must not reach the resumed pad poll */
    Psyz_PauseHostClock_SDL3(false);
    if (!audio_paused) Psyz_AudioUnpause();
    pe_toast_block(0);
    return s_choose.result;
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
    if (s_choose.open) {
        if (ev->type == SDL_EVENT_WINDOW_RESIZED || ev->type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED ||
            ev->type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED) {
            s_choose.dirty = 1;
            return false;
        }
        pe_choose_event(ev);
        return true;
    }
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
        (ev->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && ev->gbutton.button == SDL_GAMEPAD_BUTTON_GUIDE) ||
        pe_open_binding_event(ev)) {
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

void pe_mod_panel_close(void) { s_panel.open = 0; }

int pe_mod_panel_open(void) {
    if (!pe_ui_gpu_ready() || s_panel.open) return 0;
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
