#ifndef PE_BP_UI_PREVIEW_H
#define PE_BP_UI_PREVIEW_H

/* Presentation-only prototype. No game globals, save records, registration or
 * mutation callbacks belong in this interface. All supplied strings/definitions
 * must outlive the controller. Snapshot values come from the caller. */
#include <stddef.h>

enum { BP_UI_HISTORY = 32, BP_UI_MAX_PAGES = 64, BP_UI_LINES = 16, BP_UI_TEXT = 128 };
typedef enum BpUiState {
    BP_UI_AVAILABLE, BP_UI_UNAFFORDABLE, BP_UI_MAXED,
    BP_UI_LOCKED, BP_UI_INACTIVE, BP_UI_BLOCKED
} BpUiState;
typedef enum BpUiAction {
    BP_UI_NONE, BP_UI_ROUTE, BP_UI_ASK, BP_UI_STAGE, BP_UI_SETTING
} BpUiAction;
typedef enum BpUiInput {
    BP_UI_UP, BP_UI_DOWN, BP_UI_LEFT, BP_UI_RIGHT,
    BP_UI_CONFIRM, BP_UI_CANCEL, BP_UI_PAGE_PREV, BP_UI_PAGE_NEXT, BP_UI_HELP
} BpUiInput;
typedef enum BpUiEvent {
    BP_UI_IDLE, BP_UI_CHANGED, BP_UI_REQUEST_CONFIRM, BP_UI_EXIT
} BpUiEvent;
typedef struct BpUiLine {
    const char *text; /* Already formatted totals: the view never calculates balance. */
} BpUiLine;
typedef enum BpUiRowKind { BP_UI_ITEM, BP_UI_SECTION } BpUiRowKind;
/* Final display totals supplied by a read-only provider, never balance formulas. */
typedef struct BpUiPreview {
    const char *title, *before, *after, *benefit, *note, *cost, *remaining;
} BpUiPreview;
typedef struct BpUiTab { const char *id, *label, *page, *group; } BpUiTab;
/* Optional native look for a row (presentation only; the provider supplies every number).
 * GEAR_ITEM: the bag's own item row (icon, name, ammo) and the vanilla stat block.
 * GEAR_STAT: the vanilla BP screen's stat banner, totals and BASE / PLUS digits. */
typedef enum BpUiVisualKind { BP_UI_VISUAL_NONE, BP_UI_VISUAL_GEAR_ITEM, BP_UI_VISUAL_GEAR_STAT,
    BP_UI_VISUAL_AYA_STAT } BpUiVisualKind;
/* AYA_STAT: the vanilla stat banner, level and level bar (Aya's entries 5-6). */
typedef struct BpUiAyaLevel { int level, sub, distance; } BpUiAyaLevel;
typedef struct BpUiVisual {
    BpUiVisualKind kind;
    int record;                 /* equipment record index */
    int armor, stat;            /* stat 0..2 for GEAR_STAT */
    int equipped;               /* the bag's blue band behind equipped gear */
    int total[3], base[3], bonus[3];   /* now */
    int next_total, next_bonus; /* after one purchase (GEAR_STAT) */
    int bp_after;               /* BP left after it, or -1 when it can't be bought */
    BpUiAyaLevel now, next;     /* AYA_STAT: shown level (1-based), sub-level 0..48, distance */
} BpUiVisual;
typedef struct BpUiRow {
    const char *id, *label, *value;
    BpUiState state;
    BpUiAction action;
    const char *target; /* page ID for routes, request ID otherwise */
    const char *question; /* one-line native Yes/No question */
    const char *reason; /* immediately readable when unavailable */
    const BpUiLine *details;
    size_t detail_count;
    BpUiRowKind kind;
    const BpUiPreview *preview;
    const BpUiVisual *visual;   /* NULL: text rows */
} BpUiRow;
typedef struct BpUiPage {
    const char *id, *title, *context, *wallet;
    const char *previous_category, *next_category;
    const BpUiRow *rows;
    size_t row_count;
    const char *tab_id, *subject;
} BpUiPage;
typedef struct BpUiCatalog {
    const BpUiPage *pages;
    size_t page_count;
    const BpUiTab *tabs;
    size_t tab_count;
    const char *badge; /* header badge; empty hides it, NULL shows "PREVIEW" */
} BpUiCatalog;
/* Optional live backend. Without one, confirmations stay presentation-only no-ops.
 * commit runs the request (it must revalidate and be atomic) and writes the notice;
 * refresh returns the rebuilt catalog. Page and row IDs must stay stable across
 * rebuilds (same pointer or same text) so focus memory survives. */
typedef struct BpUiBackend {
    int (*commit)(void *user, const char *request, char *notice, size_t size);
    const BpUiCatalog *(*refresh)(void *user);
    void *user;
} BpUiBackend;
typedef struct BpUiMemory {
    const char *page_id, *row_id;
    size_t top;
} BpUiMemory;
typedef int (*BpUiMeasure)(const char *text);
typedef struct BpUiController {
    const BpUiCatalog *catalog;
    const BpUiPage *page;
    BpUiMemory memory[BP_UI_HISTORY], back[BP_UI_HISTORY];
    size_t memory_count, back_count, selected, top, visible, detail_page;
    const BpUiRow *pending;
    int confirm_down, cancel_down, expanded;
    int text_width;
    BpUiMeasure measure;
    char notice[BP_UI_TEXT];
    const BpUiBackend *backend;
} BpUiController;
typedef struct BpUiFrame {
    const BpUiPage *page;
    const BpUiRow *selected;
    size_t selected_index, top, visible, detail_page, detail_pages;
    const char *detail[3];
    const char *footer, *notice;
    char wrapped[3][BP_UI_TEXT];
} BpUiFrame;

/* Validation rejects duplicate IDs, missing routes and over-budget catalogs.
 * Capacity limits fail explicitly: no silently dropped services or rows. */
int bp_ui_row_selectable(const BpUiRow *row);
int bp_ui_tab_visible(const BpUiController *ui, size_t index);
const char *bp_ui_category_target(const BpUiController *ui, int direction);
int bp_ui_validate(const BpUiCatalog *catalog, char *error, size_t size);
int bp_ui_init(BpUiController *ui, const BpUiCatalog *catalog,
               const char *page_id, size_t visible);
int bp_ui_open(BpUiController *ui, const char *page_id);
BpUiEvent bp_ui_input(BpUiController *ui, BpUiInput input);
/* Feed actual held action state, including empty/release frames. Directions may
 * repeat; Confirm/Cancel are edge-triggered and consumed across modal transitions. */
BpUiEvent bp_ui_buttons(BpUiController *ui, int direction,
                        int confirm_held, int cancel_held);
/* Without a backend Yes is a no-op outside UI notice/focus. With one, Yes commits the
 * pending request, then the catalog is rebuilt and focus restored by ID. */
void bp_ui_answer(BpUiController *ui, int yes);
void bp_ui_set_backend(BpUiController *ui, const BpUiBackend *backend);
/* Swap in a rebuilt catalog, keeping page/row focus by ID (falls back to the first tab). */
int bp_ui_reload(BpUiController *ui, const BpUiCatalog *catalog);
void bp_ui_frame(const BpUiController *ui, BpUiFrame *out);
void bp_ui_set_visible(BpUiController *ui, size_t visible);
/* Optional renderer metrics: paginates complete details at the real font width. */
void bp_ui_set_text_metrics(BpUiController *ui, int width, BpUiMeasure measure);
const BpUiCatalog *bp_ui_demo_catalog(void);
/* F1-only settings definitions; never reachable through native UI routes. */
const BpUiCatalog *bp_ui_demo_f1_catalog(void);
#endif
