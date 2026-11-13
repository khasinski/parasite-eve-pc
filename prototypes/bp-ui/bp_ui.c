#include "bp_ui.h"
#include <stdio.h>
#include <string.h>

static int equal(const char *a, const char *b) { return a && b && !strcmp(a, b); }
static const BpUiPage *find_page(const BpUiCatalog *c, const char *id) {
    size_t i;
    for (i = 0; c && i < c->page_count; ++i)
        if (equal(c->pages[i].id, id)) return &c->pages[i];
    return NULL;
}
static int fail(char *out, size_t n, const char *why) {
    if (out && n) snprintf(out, n, "%s", why);
    return 0;
}
int bp_ui_row_selectable(const BpUiRow *r) { return r && r->kind != BP_UI_SECTION; }
int bp_ui_tab_visible(const BpUiController *u, size_t index) {
    if (!u || !u->page || !u->page->tab_id || index >= u->catalog->tab_count) return 0;
    for (size_t i=0;i<u->catalog->tab_count;++i) if(equal(u->catalog->tabs[i].id,u->page->tab_id)) {
        const char *a=u->catalog->tabs[i].group,*b=u->catalog->tabs[index].group;
        return (!a&&!b)||equal(a,b);
    }
    return 0;
}
const char *bp_ui_category_target(const BpUiController *u, int direction) {
    if (!u || !u->page) return NULL;
    if (u->page->tab_id) {
        for (size_t i=0;i<u->catalog->tab_count;++i) {
            if(!equal(u->catalog->tabs[i].id,u->page->tab_id))continue;
            for(int j=(int)i+(direction<0?-1:1);j>=0&&(size_t)j<u->catalog->tab_count;j+=direction<0?-1:1)
                if(bp_ui_tab_visible(u,(size_t)j))return u->catalog->tabs[j].page;
            return NULL;
        }
        return NULL;
    }
    return direction<0?u->page->previous_category:u->page->next_category;
}
int bp_ui_validate(const BpUiCatalog *c, char *out, size_t n) {
    size_t i, j, k;
    if (!c || !c->pages || !c->page_count || c->page_count > BP_UI_MAX_PAGES)
        return fail(out, n, "Invalid or over-budget page catalog");
    if ((c->tab_count && !c->tabs) || c->tab_count > BP_UI_HISTORY)
        return fail(out, n, "Invalid tab definitions");
    for (i = 0; i < c->tab_count; ++i) {
        if (!c->tabs[i].id || !c->tabs[i].label || !find_page(c, c->tabs[i].page))
            return fail(out, n, "Incomplete tab");
        for (j = 0; j < i; ++j) if (equal(c->tabs[i].id, c->tabs[j].id))
            return fail(out, n, "Duplicate tab ID");
    }
    for (i = 0; i < c->page_count; ++i) {
        const BpUiPage *p = &c->pages[i];
        if (!p->id || !p->id[0] || !p->title || !p->context || !p->wallet ||
            (p->row_count && !p->rows)) return fail(out, n, "Incomplete page");
        for (j = 0; j < i; ++j)
            if (equal(p->id, c->pages[j].id)) return fail(out, n, "Duplicate page ID");
        if ((p->previous_category && !find_page(c, p->previous_category)) ||
            (p->next_category && !find_page(c, p->next_category)))
            return fail(out, n, "Missing category target");
        if (p->tab_id) {
            int found = 0;
            for (j = 0; j < c->tab_count; ++j) found |= equal(p->tab_id, c->tabs[j].id);
            if (!found) return fail(out, n, "Unknown page tab");
        }
        for (j = 0; j < p->row_count; ++j) {
            const BpUiRow *r = &p->rows[j];
            if (!r->id || !r->id[0] || !r->label || !r->value ||
                r->state < BP_UI_AVAILABLE || r->state > BP_UI_BLOCKED ||
                r->action < BP_UI_NONE || r->action > BP_UI_SETTING ||
                r->kind < BP_UI_ITEM || r->kind > BP_UI_SECTION ||
                (r->detail_count && !r->details) || r->detail_count > BP_UI_LINES)
                return fail(out, n, "Incomplete or over-budget row");
            if (r->kind == BP_UI_SECTION && (r->action != BP_UI_NONE || r->state != BP_UI_AVAILABLE))
                return fail(out, n, "Section heading must be non-interactive");
            for (k = 0; k < j; ++k)
                if (equal(r->id, p->rows[k].id)) return fail(out, n, "Duplicate row ID");
            for (k = 0; k < r->detail_count; ++k)
                if (!r->details[k].text) return fail(out, n, "Missing detail text");
            if (r->state != BP_UI_AVAILABLE && (!r->reason || !r->reason[0]))
                return fail(out, n, "Unavailable row needs a reason");
            if (r->action == BP_UI_ROUTE && !find_page(c, r->target))
                return fail(out, n, "Missing route target");
            if (r->action == BP_UI_ASK && (!r->target || !r->question || !r->question[0]))
                return fail(out, n, "Incomplete confirmation request");
        }
    }
    if (out && n) out[0] = 0;
    return 1;
}
static BpUiMemory *memory(BpUiController *u, const char *id) {
    size_t i;
    for (i = 0; i < u->memory_count; ++i)
        if (equal(u->memory[i].page_id, id)) return &u->memory[i];
    if (u->memory_count == BP_UI_HISTORY) return NULL;
    u->memory[u->memory_count].page_id = id;
    return &u->memory[u->memory_count++];
}
static void clamp(BpUiController *u) {
    size_t n = u->page->row_count;
    if (!n) { u->selected = u->top = 0; return; }
    if (u->selected >= n) u->selected = n - 1;
    if (!bp_ui_row_selectable(&u->page->rows[u->selected])) {
        size_t next = u->selected;
        while (next < n && !bp_ui_row_selectable(&u->page->rows[next])) ++next;
        if (next == n) {
            next = u->selected;
            while (next && !bp_ui_row_selectable(&u->page->rows[next])) --next;
        }
        u->selected = next;
    }
    if (u->selected < u->top) u->top = u->selected;
    if (u->selected - u->top >= u->visible) u->top = u->selected - u->visible + 1;
}
static void remember(BpUiController *u) {
    BpUiMemory *m = memory(u, u->page->id);
    if (m) {
        m->row_id = u->page->row_count ? u->page->rows[u->selected].id : NULL;
        m->top = u->top;
    }
}
int bp_ui_open(BpUiController *u, const char *id) {
    const BpUiPage *p;
    BpUiMemory *m;
    size_t i;
    if (!u || u->pending || !(p = find_page(u->catalog, id))) return 0;
    if (u->page) remember(u);
    u->page = p;
    u->selected = u->top = u->detail_page = 0;
    u->expanded = 0;
    u->notice[0] = 0;
    m = memory(u, id);
    if (m) {
        u->top = m->top;
        for (i = 0; i < p->row_count; ++i)
            if (equal(p->rows[i].id, m->row_id)) { u->selected = i; break; }
    }
    clamp(u);
    return 1;
}
int bp_ui_init(BpUiController *u, const BpUiCatalog *c, const char *id, size_t visible) {
    if (!u || !bp_ui_validate(c, NULL, 0)) return 0;
    memset(u, 0, sizeof *u);
    u->catalog = c;
    u->visible = visible ? visible : 1;
    return bp_ui_open(u, id);
}
void bp_ui_set_visible(BpUiController *u, size_t visible) {
    if (!u || !u->page) return;
    u->visible = visible ? visible : 1;
    clamp(u);
}
void bp_ui_set_text_metrics(BpUiController *u, int width, BpUiMeasure measure) {
    if (!u) return;
    u->text_width = width;
    u->measure = width > 0 ? measure : NULL;
    u->detail_page = 0;
}
static void detail_line(const BpUiController *u, const char *text, size_t *count, BpUiFrame *f) {
    const size_t first = u->detail_page * 3;
    if (!u->measure) {
        if (f && *count >= first && *count < first + 3) f->detail[*count - first] = text;
        ++*count; return;
    }
    do {
        char part[BP_UI_TEXT];
        size_t length = 0, space = 0, take;
        while (text[length] && length + 1 < sizeof part) {
            part[length] = text[length]; part[length + 1] = 0;
            if (u->measure(part) > u->text_width) break;
            if (text[length] == ' ') space = length;
            ++length;
        }
        take = length;
        if (text[length] && space) take = space;
        if (!take && text[0]) take = 1; /* Progress even for an impossibly narrow test view. */
        memcpy(part, text, take); part[take] = 0;
        if (f && *count >= first && *count < first + 3) {
            size_t slot = *count - first;
            snprintf(f->wrapped[slot], sizeof f->wrapped[slot], "%s", part);
            f->detail[slot] = f->wrapped[slot];
        }
        ++*count;
        text += take;
        while (*text == ' ') ++text;
    } while (*text);
}
static size_t detail_lines(const BpUiController *u, const BpUiRow *r, BpUiFrame *f) {
    size_t count = 0, i;
    if (!bp_ui_row_selectable(r)) return 0;
    if (r->state != BP_UI_AVAILABLE) detail_line(u, r->reason, &count, f);
    if (!r->preview && u->measure && u->measure(r->label) + u->measure(r->value) + 8 > u->text_width)
        detail_line(u, r->label, &count, f);
    if (u->measure && u->measure(r->value) + 16 > u->text_width)
        detail_line(u, r->value, &count, f);
    for (i = 0; i < r->detail_count; ++i) {
        if (r->state != BP_UI_AVAILABLE && equal(r->details[i].text, r->reason)) continue;
        detail_line(u, r->details[i].text, &count, f);
    }
    return count;
}
int bp_ui_reload(BpUiController *u, const BpUiCatalog *c) {
    char page_id[BP_UI_TEXT], row_id[BP_UI_TEXT];
    const BpUiPage *p;
    size_t top, i;
    if (!u || !u->page || !bp_ui_validate(c, NULL, 0)) return 0;
    snprintf(page_id, sizeof page_id, "%s", u->page->id);
    snprintf(row_id, sizeof row_id, "%s", u->page->row_count ? u->page->rows[u->selected].id : "");
    top = u->top;
    u->catalog = c;
    p = find_page(c, page_id);
    if (!p) {   /* The page left (e.g. the gear was dropped): back out to a surviving page. */
        while (u->back_count && !(p = find_page(c, u->back[u->back_count - 1].page_id))) --u->back_count;
        if (p) --u->back_count;
        else if (c->tab_count) p = find_page(c, c->tabs[0].page);
        else p = &c->pages[0];
        u->page = NULL;
        return bp_ui_open(u, p->id);
    }
    u->page = p;
    u->selected = 0; u->top = top;
    for (i = 0; i < p->row_count; ++i) if (equal(p->rows[i].id, row_id)) { u->selected = i; break; }
    u->detail_page = 0;
    clamp(u);
    return 1;
}
void bp_ui_set_backend(BpUiController *u, const BpUiBackend *b) { if (u) u->backend = b; }
static int commit_request(BpUiController *u, const char *target) {
    char notice[BP_UI_TEXT] = "";
    const BpUiCatalog *next;
    int ok = u->backend->commit(u->backend->user, target, notice, sizeof notice);
    next = u->backend->refresh ? u->backend->refresh(u->backend->user) : NULL;
    if (next) bp_ui_reload(u, next);
    snprintf(u->notice, sizeof u->notice, "%s", notice);
    return ok;
}
/* Sets the sound for this input, then returns the event. */
static BpUiEvent sounded(BpUiController *u, BpUiSound sound, BpUiEvent e) { u->sound = sound; return e; }
BpUiEvent bp_ui_input(BpUiController *u, BpUiInput key) {
    const BpUiRow *r;
    const char *target = NULL;
    size_t old, pages;
    if (u) u->sound = BP_UI_SOUND_NONE;
    if (!u || !u->page || u->pending) return BP_UI_IDLE;
    r = u->page->row_count ? &u->page->rows[u->selected] : NULL;
    old = u->selected;
    if (key == BP_UI_UP && u->selected) {
        size_t next = u->selected;
        do { --next; } while (next && !bp_ui_row_selectable(&u->page->rows[next]));
        if (bp_ui_row_selectable(&u->page->rows[next])) u->selected = next;
    }
    if (key == BP_UI_DOWN && u->selected + 1 < u->page->row_count) {
        size_t next = u->selected + 1;
        while (next < u->page->row_count && !bp_ui_row_selectable(&u->page->rows[next])) ++next;
        if (next < u->page->row_count) u->selected = next;
    }
    if (key == BP_UI_LEFT) target = bp_ui_category_target(u, -1);
    if (key == BP_UI_RIGHT) target = bp_ui_category_target(u, 1);
    if (target) {
        if (u->page->tab_id) while (u->back_count) {
            const BpUiPage *back = find_page(u->catalog, u->back[u->back_count-1].page_id);
            if (!back || !back->tab_id) break;
            --u->back_count;
        }
        bp_ui_open(u, target); return sounded(u, BP_UI_SOUND_MOVE, BP_UI_CHANGED);
    }
    if (old != u->selected) {
        u->detail_page = 0; u->notice[0] = 0; clamp(u); return sounded(u, BP_UI_SOUND_MOVE, BP_UI_CHANGED);
    }
    if (key == BP_UI_HELP) { u->expanded = !u->expanded; u->detail_page = 0; return sounded(u, BP_UI_SOUND_MOVE, BP_UI_CHANGED); }
    pages = (detail_lines(u, r, NULL) + 2) / 3;
    if (key == BP_UI_PAGE_PREV && u->detail_page) { u->expanded = 1; --u->detail_page; return sounded(u, BP_UI_SOUND_MOVE, BP_UI_CHANGED); }
    if (key == BP_UI_PAGE_NEXT && u->detail_page + 1 < pages) { u->expanded = 1; ++u->detail_page; return sounded(u, BP_UI_SOUND_MOVE, BP_UI_CHANGED); }
    if (key == BP_UI_CANCEL) {
        if (u->expanded) { u->expanded = 0; u->detail_page = 0; return sounded(u, BP_UI_SOUND_CANCEL, BP_UI_CHANGED); }
        if (!u->back_count) return sounded(u, BP_UI_SOUND_CANCEL, BP_UI_EXIT);
        bp_ui_open(u, u->back[--u->back_count].page_id);
        return sounded(u, BP_UI_SOUND_CANCEL, BP_UI_CHANGED);
    }
    if (key != BP_UI_CONFIRM) return BP_UI_IDLE;
    if (!bp_ui_row_selectable(r)) return sounded(u, BP_UI_SOUND_ERROR, BP_UI_IDLE);
    if (r->state != BP_UI_AVAILABLE) {
        snprintf(u->notice, sizeof u->notice, "%s", r->reason);
        return sounded(u, BP_UI_SOUND_ERROR, BP_UI_CHANGED);
    }
    if (r->action == BP_UI_ROUTE) {
        if (u->back_count == BP_UI_HISTORY) {
            snprintf(u->notice, sizeof u->notice, "Navigation limit reached");
            return sounded(u, BP_UI_SOUND_ERROR, BP_UI_CHANGED);
        }
        u->back[u->back_count++].page_id = u->page->id;
        bp_ui_open(u, r->target);
        return sounded(u, BP_UI_SOUND_CONFIRM, BP_UI_CHANGED);
    }
    if (r->action == BP_UI_ASK) { u->pending = r; return sounded(u, BP_UI_SOUND_CONFIRM, BP_UI_REQUEST_CONFIRM); }
    if (r->action == BP_UI_SETTING && u->backend && u->backend->commit) {
        int ok = commit_request(u, r->target);
        return sounded(u, ok ? BP_UI_SOUND_CONFIRM : BP_UI_SOUND_ERROR, BP_UI_CHANGED);
    }
    if (r->action == BP_UI_STAGE || r->action == BP_UI_SETTING) {
        snprintf(u->notice, sizeof u->notice, "Preview only - unchanged");
        return sounded(u, BP_UI_SOUND_CONFIRM, BP_UI_CHANGED);
    }
    return sounded(u, BP_UI_SOUND_ERROR, BP_UI_IDLE);   /* a read-only row */
}
BpUiEvent bp_ui_buttons(BpUiController *u, int direction, int yes, int cancel) {
    int accept_edge, back_edge;
    if (!u) return BP_UI_IDLE;
    accept_edge = yes && !u->confirm_down;
    back_edge = cancel && !u->cancel_down;
    u->confirm_down = !!yes; u->cancel_down = !!cancel;
    if (u->pending) return BP_UI_IDLE;
    if (back_edge) return bp_ui_input(u, BP_UI_CANCEL);
    if (accept_edge) return bp_ui_input(u, BP_UI_CONFIRM);
    if (direction >= BP_UI_UP && direction <= BP_UI_RIGHT)
        return bp_ui_input(u, (BpUiInput)direction);
    return BP_UI_IDLE;
}
/* The game's Yes/No box plays confirm (Yes) or cancel (No) itself; only a refused Yes adds one. */
void bp_ui_answer(BpUiController *u, int yes) {
    if (u) u->sound = BP_UI_SOUND_NONE;
    if (!u || !u->pending) return;
    if (u->backend && u->backend->commit) {
        const char *target = u->pending->target;
        u->pending = NULL;
        if (yes) { if (!commit_request(u, target)) u->sound = BP_UI_SOUND_ERROR; }
        else snprintf(u->notice, sizeof u->notice, "Cancelled - unchanged");
        return;
    }
    u->pending = NULL;
    snprintf(u->notice, sizeof u->notice, "%s", yes ?
             "Preview only - nothing spent" : "Cancelled - unchanged");
}
void bp_ui_frame(const BpUiController *u, BpUiFrame *f) {
    if (!f) return;
    memset(f, 0, sizeof *f);
    if (!u || !u->page) return;
    f->page = u->page; f->selected_index = u->selected;
    f->top = u->top; f->visible = u->visible; f->detail_page = u->detail_page;
    f->notice = u->notice;
    f->footer = "Confirm: Preview  Back";
    if (!u->page->row_count) { f->detail[0] = "No entries available"; return; }
    f->selected = &u->page->rows[u->selected];
    if (!bp_ui_row_selectable(f->selected)) { f->selected = NULL; f->detail[0] = "No upgrades available"; return; }
    f->detail_pages = (detail_lines(u, f->selected, f) + 2) / 3;
    if (f->selected->action == BP_UI_NONE) f->footer = "Read only  Back";
    if (f->selected->action == BP_UI_ROUTE) f->footer = "Confirm: Open  Back";
    if (f->selected->action == BP_UI_STAGE) f->footer = "Confirm: Stage  Back";
    if (f->selected->action == BP_UI_SETTING) f->footer = "Confirm: Set  Back";
    if (f->selected->state != BP_UI_AVAILABLE) f->footer = "Unavailable  Back";
}
