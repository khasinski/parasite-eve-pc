/* Explicit preview-build access only. Live BP data and transactions come from the pe_bp mod
 * (bp_live_ui.c) when it runs; otherwise the screens show the sample catalog. */
#include "bp_ui_native.h"
#include "bp_live_ui.h"
#include "bp_ui.h"
#include "pe_mod_host.h"
#include "pe_mod_menu.h"
#include "pe_main_menu.h"
#include "pe_game.h"
#include "pe1/menu_widget.h"
#include "pe_workshop_model.h"
#include "pe_services.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void __real_pe_main_menu_init(void);
int __real_pe_host_init(const PeHookInit *init);
void pe_bp_ui_preview_report(void);
static int menu_ready, registered;
static const char *pending_page;
static const char *pending_name = "None";

static void open_browser(MenuWidgetNode *from) {
    pe_bp_ui_preview_open(from, "preview");
}
static const PeMainMenuRow preview_row = {"BP Upgrades", 0, NULL, open_browser, NULL};

static void queue(const char *page, const char *name) {
    pending_page = page; pending_name = name;
    pe_toast("Close F1, then open Aya's menu for the preview.");
}
static int open_wayne_refund(MenuWidgetNode *from) { return pe_bp_ui_preview_open(from,"reset"); }
static const PeWsService refund_service={"bp.refund","Reset BP",
    "Refund Resistance, Mastery and Aya's BP stats for Junk. Weapons and armor excluded.",NULL,open_wayne_refund};
static void queue_bp(void) { queue("guns", "BP upgrades"); }
static void queue_wayne(void) { queue(pe_bp_live_available() ? "reset" : "workshop", "Wayne's Workshop"); }
/* Preview choices are presentation state only; no config or gameplay writes. */
static int preset_index = 1, guide_index;
static const BpUiPage *preset_page(void) { return &bp_ui_demo_f1_catalog()->pages[0]; }
static const BpUiPage *guide_page(void) { return &bp_ui_demo_f1_catalog()->pages[1]; }
static unsigned preset_count(void) { return (unsigned)preset_page()->row_count; }
static unsigned guide_count(void) { return (unsigned)guide_page()->row_count; }
static const char *preset_name(unsigned i) { return i < preset_count() ? preset_page()->rows[i].label : ""; }
static const char *guide_name(unsigned i) { return i < guide_count() ? guide_page()->rows[i].label : ""; }
static int get_preset(void) { return preset_index; }
static int get_guide(void) { return guide_index; }
static int set_preset(int i) { if (i < 0 || (unsigned)i >= preset_count()) return 0; preset_index = i; return 1; }
static int set_guide(int i) { if (i < 0 || (unsigned)i >= guide_count()) return 0; guide_index = i; return 1; }
static void preset_rules(char *out, unsigned size) {
    snprintf(out, size, "%s", preset_page()->rows[preset_index].details[0].text);
}
static void preset_notes(char *out, unsigned size) {
    snprintf(out, size, "%s", preset_page()->rows[preset_index].details[1].text);
}
static int master_enabled(void) {
    for (int i = 0; i < pe_menu_row_count(); ++i) {
        PeMenuRow *row = pe_menu_row(i);
        if (!strcmp(row->page, PE_MENU_MANAGE) && !strcmp(row->label, "All mods") && row->value)
            return *row->value;
    }
    return -1;
}
static void status(char *out, unsigned size) {
    snprintf(out, size, "%s", pending_page ? pending_name : "Choose a preview below");
}
/* Test harness only: PE_BP_UI_TEST_JUNK=n gives n Junk once a game is loaded, so headless
 * runs can exercise a paid refund. Never set by the launcher. */
static void test_junk_grant(void) {
    static int done;
    const char *amount = getenv("PE_BP_UI_TEST_JUNK");
    const PeJunkApi *junk;
    if (done || !amount || !amount[0] || !pe_game_loaded() || pe_in_battle()) return;
    done = 1;
    junk = pe_service_get("junk", 1);
    if (junk && junk->give) { junk->give((uint32_t)strtoul(amount, NULL, 10)); printf("BP_UI_TEST_JUNK granted=%s\n", amount); fflush(stdout); }
}
static int on_draw(void *ctx, void *user) {
    MenuWidgetNode *current = MenuWidget_GetCurrentNode();
    (void)ctx; (void)user;
    test_junk_grant();
    if (pending_page && current && current->mode == 2 && current->selected_base == 0 &&
        pe_game_loaded() && !pe_in_battle()) {
        if (pe_bp_ui_preview_open(current, pending_page)) pending_page = NULL;
        else { fprintf(stderr, "BP_UI_OPEN_FAILED page=%s\n", pending_page); fflush(stderr); pending_page = NULL; }
    }
    pe_bp_ui_preview_report();
    return 0;
}
void __wrap_pe_main_menu_init(void) {
    if (!menu_ready) { __real_pe_main_menu_init(); menu_ready = 1; }
}
int __wrap_pe_host_init(const PeHookInit *init) {
    int result = __real_pe_host_init(init);
    if (result || registered) return result;
    registered = 1;
    if(!pe_ws_register_service(&refund_service))abort();
    pe_bp_live_install();
    {   /* Test harness only (PE_BP_TEST_BATTLE=1): see battle_probe.c. */
        void pe_bp_battle_probe_install(void);
        pe_bp_battle_probe_install();
    }
    /* Preview navigation must work in a fresh profile with All mods Off. */
    __wrap_pe_main_menu_init();
    pe_main_menu_add(&preview_row, 10000);
    pe_menu_top_page("BP UI Preview", "Preview screens only. No BP or inventory changes.", -1000);
    pe_menu_begin("BP UI Preview");
    if (pe_menu_register_info(NULL, "Queued preview", "After choosing, close F1 and open Aya's menu.", status) < 0 ||
        pe_menu_register_action(NULL, "Open BP preview", "Close F1, then press Triangle in the field.", queue_bp) < 0 ||
        pe_menu_register_action(NULL, "Open Wayne preview", "Close F1, then press Triangle in the field.", queue_wayne) < 0 ||
        pe_menu_register_live_dynamic_choice(NULL, "Rules preset", "Preview only. Left/Right changes the displayed rules; no live effects.", 16, preset_count, preset_name, get_preset, set_preset) < 0 ||
        pe_menu_register_info(NULL, "Preset rules", "The rules described by the selected preview preset.", preset_rules) < 0 ||
        pe_menu_register_info(NULL, "Preset notes", "Preview information; no BP or settings are changed.", preset_notes) < 0 ||
        pe_menu_register_live_dynamic_choice(NULL, "Playstyle guide", "Advice preview only. Does not spend BP or change rules.", 16, guide_count, guide_name, get_guide, set_guide) < 0 ||
        pe_hook_add(PE_HOOK_FRAME_DRAWN, on_draw, NULL, PE_PRIORITY_LATE, "host") < 0) abort();
    pe_menu_begin(NULL);
    pending_page = getenv("PE_BP_UI_PAGE");
    if (pending_page && !pending_page[0]) pending_page = NULL;
    /* Test the exact registered F1 action without requiring a visible overlay. */
    {
        const char *test = getenv("PE_BP_UI_F1_TEST");
        int found = 0;
        if (test && test[0]) {
            for (int i = 0; i < pe_menu_row_count(); ++i) {
                PeMenuRow *row = pe_menu_row(i);
                if (!strcmp(row->page, "BP UI Preview") && !strcmp(row->label, test)) {
                    if (!pe_menu_row_active(row)) abort();
                    pe_menu_row_accept(row); found = 1; break;
                }
            }
            if (!found) abort();
            printf("BP_UI_F1_ACTION tested=%s\n", test);
        }
    }
    if (getenv("PE_BP_UI_F1_PRESETS_TEST")) {
        const char *labels[] = {"Rules preset", "Playstyle guide"};
        unsigned tested[2] = {0, 0};
        for (unsigned j = 0; j < 2; ++j) for (int i = 0; i < pe_menu_row_count(); ++i) {
            PeMenuRow *row = pe_menu_row(i);
            if (strcmp(row->page, "BP UI Preview") || strcmp(row->label, labels[j])) continue;
            if (!pe_menu_row_active(row) || !row->count_fn || !row->name_fn || !row->get || !row->set) abort();
            int original = row->get();
            for (unsigned k = 0; k < row->count_fn(); ++k) {
                char value[128];
                if (!row->set((int)k) || row->get() != (int)k) abort();
                pe_menu_row_value_text(row, value, sizeof value);
                if (strcmp(value, row->name_fn(k))) abort();
                ++tested[j];
            }
            if (row->set(-1) || row->set((int)row->count_fn()) || !row->set(original)) abort();
        }
        if (tested[0] != 5 || tested[1] != 5) abort();
        printf("BP_UI_F1_PRESETS rules=%u guides=%u native_routes=absent\n", tested[0], tested[1]);
    }
    printf("BP_UI_ACCESS registered F1=BP_UI_Preview main_menu=BP_Upgrades master=%d live=%d\n", master_enabled(), pe_bp_live_available());
    fflush(stdout);
    return 0;
}
