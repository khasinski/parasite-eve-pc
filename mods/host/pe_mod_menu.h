#ifndef PE_MOD_MENU_H
#define PE_MOD_MENU_H

/* Rows in the Mods panel (F1). The same calls as the psxrecomp build's menu,
 * so ported mods keep their registration code.
 *
 *   page         Each mod's rows live under its own top page, set with
 *                pe_menu_begin("My Mod") before registering (default
 *                PE_MENU_PAGE).
 *   sub          Sub-page below that page, or 0 for the page itself; may
 *                contain '/' for deeper pages.
 *   label        Row text.
 *   description  One line shown when the row is selected.
 *   flag/value   Owned by the mod (static). Toggle, choice and number rows
 *                are saved in pe_mods.ini and restored at start-up.
 *   per_frame    Optional; runs every field frame, whether or not the panel
 *                is open, and decides from *flag what to do.
 *   action       Runs once, with the game paused, when the row is chosen.
 *
 * All return 0 on success, -1 if the registry is full or an argument is bad. */

#define PE_MENU_PAGE "PE Enhancements"

typedef void (*PeMenuPerFrame)(void);
typedef void (*PeMenuAction)(void);
typedef int (*PeMenuGet)(void);
typedef int (*PeMenuSet)(int value);
typedef unsigned (*PeMenuChoiceCount)(void);
typedef const char *(*PeMenuChoiceName)(unsigned index);

void pe_menu_begin(const char *page);

int pe_menu_register_toggle(const char *sub, const char *label, const char *description,
                            int *flag, PeMenuPerFrame per_frame);
int pe_menu_register_action(const char *sub, const char *label, const char *description,
                            PeMenuAction action);
int pe_menu_register_choice(const char *sub, const char *label, const char *description,
                            int *index, const char *const *names, unsigned count);
int pe_menu_register_int(const char *sub, const char *label, const char *description,
                         int *value, int minimum, int maximum, int step);
/* Live rows read and write the game directly; they are not saved. set()
 * returns non-zero if it accepted the value. */
int pe_menu_register_live_int(const char *sub, const char *label, const char *description,
                              int minimum, int maximum, int step, PeMenuGet get, PeMenuSet set);
int pe_menu_register_live_choice(const char *sub, const char *label, const char *description,
                                 const char *const *names, unsigned count, PeMenuGet get,
                                 PeMenuSet set);
/* A choice whose list depends on other rows; *index resets to 0 when the
 * list length changes. name_fn returns strings that outlive the call. */
int pe_menu_register_dynamic_choice(const char *sub, const char *label, const char *description,
                                    int *index, unsigned max_count, PeMenuChoiceCount count_fn,
                                    PeMenuChoiceName name_fn);
int pe_menu_register_tick(PeMenuPerFrame fn);
/* A read-only line: text() fills the value shown on the right (Junk, prices, results). */
typedef void (*PeMenuText)(char *out, unsigned size);
int pe_menu_register_info(const char *sub, const char *label, const char *description, PeMenuText text);
/* Describe sub-page `sub` of the current page: the footer line when its link is selected,
 * and its place among its sibling pages (lower first; pages without one keep their order). */
int pe_menu_page(const char *sub, const char *description, int order);
/* The same for a mod's top page as the list of mods shows it (F1's first screen). Mods without
 * one are listed after those that have one, in start-up order. */
int pe_menu_top_page(const char *page, const char *description, int order);
#define PE_MENU_MANAGE "Manage mods"   /* the host's page, last on the list */

/* Open an existing page synchronously; unknown pages fall back to Manage mods. */
int pe_mod_panel_open_page(const char *page);

/* ---- for the panel ---- */

typedef enum PeRowKind {
    PE_ROW_TOGGLE, PE_ROW_ACTION, PE_ROW_CHOICE, PE_ROW_INT,
    PE_ROW_LIVE_INT, PE_ROW_LIVE_CHOICE, PE_ROW_DYNAMIC_CHOICE, PE_ROW_INFO
} PeRowKind;

typedef struct PeMenuRow {
    PeRowKind kind;
    char page[96]; /* full path: "PE Enhancements/Item Editor" */
    const char *label, *description;
    int *value;
    int minimum, maximum, step;
    const char *const *names;
    unsigned count, last_count;
    PeMenuAction action;
    PeMenuGet get;
    PeMenuSet set;
    PeMenuChoiceCount count_fn;
    PeMenuChoiceName name_fn;
    PeMenuText text_fn;
    const char *mod_id;  /* the mod that registered it (NULL: the host) */
} PeMenuRow;

/* The row's mod is active: settings of an inactive mod can still be set (saved), but its live
 * rows and actions don't run. */
int pe_menu_row_active(const PeMenuRow *row);

/* Page metadata (pe_menu_page), by full path, or NULL. */
typedef struct PeMenuPageInfo {
    char path[96];
    const char *description;
    int order;
} PeMenuPageInfo;
const PeMenuPageInfo *pe_menu_page_info(const char *path);
int pe_menu_row_adjustable(const PeMenuRow *row);   /* left/right change it */

int pe_menu_row_count(void);
PeMenuRow *pe_menu_row(int index);
void pe_menu_row_value_text(PeMenuRow *row, char *out, unsigned size);
void pe_menu_row_step(PeMenuRow *row, int direction); /* left/right */
void pe_menu_row_accept(PeMenuRow *row);              /* Enter */

/* Optional native settings owner, installed only while its modal is open. */
void pe_mod_panel_set_native_close(void (*close_callback)(void));
#endif
