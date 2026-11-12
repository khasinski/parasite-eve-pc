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
#include "pe_pages.h"
#define BP_DEBUG_PAGE PE_MENU_PAGE "/" PE_PAGE_DEBUG_BP
#define BP_SETTINGS_PAGE PE_MENU_PAGE "/" PE_PAGE_BP
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void __real_pe_main_menu_init(void);
int __real_pe_host_init(const PeHookInit *init);
void pe_bp_ui_preview_report(void);
static int menu_ready, registered;
static const char *pending_page;
static const char *pending_name = "None";

/* The classic BP icon is action 8. No extra Aya menu row is registered.
 * Leave vanilla allocation available when the live BP module is disabled. */
static int on_main_bp(void *ctx, void *user) {
    const PeHookMenuAction *action=(const PeHookMenuAction *)ctx;
    (void)user;
    if(action->action!=8 || !pe_game_loaded() || pe_in_battle() || !pe_bp_live_available())return 0;
    if(!pe_bp_ui_preview_open(action->list,"aya"))return 0;
    printf("BP_UI_ENTRY source=classic_bp page=aya\n");fflush(stdout);return 1;
}

static void queue(const char *page, const char *name) {
    pending_page = page; pending_name = name;
    pe_toast("Close F1, then open Aya's menu for the preview.");
}
static int open_wayne_refund(MenuWidgetNode *from) { return pe_bp_ui_preview_open(from,"reset.one"); }
static const PeWsService refund_service={"bp.refund","Reset BP",
    "Refund Resistance, Mastery and Aya's BP stats for Junk. Weapons and armor excluded.",pe_bp_live_refunds_available,open_wayne_refund};
static void queue_bp(void) { queue("aya", "BP upgrades"); }
static void queue_wayne(void) { queue("reset.one", "Wayne's BP refunds"); }
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
extern int g_MenuItemContextFlag;
static int on_draw(void *ctx, void *user) {
    MenuWidgetNode *current = MenuWidget_GetCurrentNode();
    (void)ctx; (void)user;
    test_junk_grant();
    if(current&&current->mode==2&&current->selected_base==0){
        static int last=-2;unsigned mask=(unsigned)g_MenuItemContextFlag&0xefu;int row=0;
        while(mask){row+=(int)(mask&1u);mask>>=1;}
        if(!(g_MenuItemContextFlag&0x100))row=-1;
        if(row!=last){printf("BP_UI_MAIN bp_row=%d\n",row);fflush(stdout);last=row;}
    }
    if (pending_page && current && current->mode == 2 && current->selected_base == 0 &&
        pe_game_loaded() && !pe_in_battle()) {
        if ((!strcmp(pending_page,"reset.one") ? open_wayne_refund(current) : pe_bp_ui_preview_open(current, pending_page))) pending_page = NULL;
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
    if(pe_hook_add(PE_HOOK_MAIN_MENU_ACTION,on_main_bp,NULL,PE_PRIORITY_NORMAL,"host")<0)abort();
    /* Debug tools live inside the suite; the BP mod owns its real settings page. */
    pe_menu_begin(BP_DEBUG_PAGE);
    if (pe_menu_register_info(NULL, "Queued preview", "After choosing, close F1 and open Aya's menu.", status) < 0 ||
        pe_menu_register_action(NULL, "Open BP preview", "Close F1, then press Triangle in the field.", queue_bp) < 0 ||
        pe_menu_register_action(NULL, "Open Wayne preview", "Close F1, then press Triangle in the field.", queue_wayne) < 0 ||
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
                if (!strcmp(row->page, BP_DEBUG_PAGE) && !strcmp(row->label, test)) {
                    if (!pe_menu_row_active(row)) abort();
                    pe_menu_row_accept(row); found = 1; break;
                }
            }
            if (!found) abort();
            printf("BP_UI_F1_ACTION tested=%s\n", test);
        }
    }
    if (getenv("PE_BP_UI_F1_PRESETS_TEST")) {
        const char *labels[] = {"Choose BP preset", "Playstyle guide"};
        const char *paths[] = {BP_SETTINGS_PAGE,BP_SETTINGS_PAGE};
        unsigned tested[2] = {0, 0};
        for (unsigned j=0;j<2;++j)for(int i=0;i<pe_menu_row_count();++i){
            PeMenuRow *row=pe_menu_row(i);
            if(strcmp(row->page,paths[j])||strcmp(row->label,labels[j]))continue;
            if((!row->count_fn&&!row->count)||(!row->name_fn&&!row->names))abort();
            int original=row->get?row->get():*row->value;
            unsigned count=row->count_fn?row->count_fn():row->count;
            if(!pe_menu_row_active(row)&&row->get){tested[j]=count;continue;}
            for(unsigned k=0;k<count;++k){
                char value[128];
                if(row->set){if(!row->set((int)k))abort();}else *row->value=(int)k;
                pe_menu_row_value_text(row,value,sizeof value);
                if(strcmp(value,row->name_fn?row->name_fn(k):row->names[k]))abort();++tested[j];
            }
            if(row->set){if(row->set(-1)||row->set((int)count)||!row->set(original))abort();}
            else *row->value=original;
        }
        if(tested[0]!=5||tested[1]!=5)abort();
        printf("BP_UI_F1_PRESETS rules=%u guides=%u native_routes=absent\n", tested[0], tested[1]);
    }
    if(getenv("PE_BP_UI_SUITE_TEST")){
        extern const PeMod *const g_pe_mods[];
        unsigned compiled=0,running=0,settings=0,debug=0;
        const char *categories[]={"Movement","Combat","Economy","Inventory","Collection"};
        unsigned found[5]={0};
        for(unsigned i=0;g_pe_mods[i];++i){++compiled;running+=pe_mod_is_running(g_pe_mods[i]->id)!=0;}
        for(int i=0;i<pe_menu_row_count();++i){
            PeMenuRow *row=pe_menu_row(i);
            if(!strncmp(row->page,"BP UI Preview",13))abort();
            if(!strcmp(row->page,BP_SETTINGS_PAGE)){++settings;if(!row->mod_id||strcmp(row->mod_id,"pe_bp"))abort();}
            if(!strcmp(row->page,BP_DEBUG_PAGE))++debug;
            for(unsigned c=0;c<5;++c){char page[96];snprintf(page,sizeof page,"%s/%s",PE_MENU_PAGE,categories[c]);
                if(!strncmp(row->page,page,strlen(page)))++found[c];}
        }
        for(unsigned c=0;c<5;++c)if(!found[c])abort();
        if(compiled<15||settings<7||debug<3||!pe_menu_page_info(BP_SETTINGS_PAGE))abort();
        if(master_enabled()==1&&running!=compiled)abort();
        printf("BP_UI_SUITE compiled=%u running=%u rows=%d settings=%u debug=%u page=%s\n",compiled,running,pe_menu_row_count(),settings,debug,BP_SETTINGS_PAGE);
    }
    printf("BP_UI_ACCESS registered F1=PE_Enhancements/Bonus_Points main_menu=classic_BP master=%d live=%d\n", master_enabled(), pe_bp_live_available());
    fflush(stdout);
    return 0;
}
