#include "bp_ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while (0)
static BpUiController ui;
static void start(const char *id) { CHECK(bp_ui_init(&ui,bp_ui_demo_catalog(),id,7)); }
static void down(unsigned n) { while (n--) bp_ui_input(&ui,BP_UI_DOWN); }
static void seek(size_t at) {
    unsigned guard=100;
    while (ui.selected != at && guard--) bp_ui_input(&ui,ui.selected < at ? BP_UI_DOWN : BP_UI_UP);
    CHECK(ui.selected == at);
}
static void navigation(void) {
    start("aya"); CHECK(ui.selected==1);
    CHECK(bp_ui_input(&ui,BP_UI_UP)==BP_UI_IDLE);
    down(1); CHECK(ui.selected==2);
    down(1); CHECK(ui.selected==4); /* Skip RESISTANCE heading. */
    down(4); CHECK(ui.selected==8 && ui.top==2);
    bp_ui_input(&ui,BP_UI_RIGHT); CHECK(!strcmp(ui.page->id,"mastery"));
    down(2); bp_ui_input(&ui,BP_UI_LEFT); CHECK(ui.selected==8 && ui.top==2);
    down(50); CHECK(ui.selected==ui.page->row_count-1);
    bp_ui_set_visible(&ui,1); CHECK(ui.top==ui.selected);
    bp_ui_set_visible(&ui,0); CHECK(ui.visible==1);
    CHECK(!bp_ui_open(&ui,"missing"));
    start("guns"); down(1); CHECK(ui.selected==2);
    bp_ui_input(&ui,BP_UI_CONFIRM); CHECK(!strcmp(ui.page->id,"gun.m84f") && ui.selected==1);
    bp_ui_input(&ui,BP_UI_CANCEL); CHECK(!strcmp(ui.page->id,"guns") && ui.selected==2);
    bp_ui_input(&ui,BP_UI_CONFIRM); bp_ui_input(&ui,BP_UI_RIGHT);
    CHECK(!strcmp(ui.page->id,"armor") && ui.back_count==0);
    CHECK(bp_ui_input(&ui,BP_UI_CANCEL)==BP_UI_EXIT);
    start("workshop"); down(2); bp_ui_input(&ui,BP_UI_CONFIRM);
    bp_ui_input(&ui,BP_UI_CONFIRM); CHECK(!strcmp(ui.page->id,"reset.one"));
    bp_ui_input(&ui,BP_UI_CANCEL); bp_ui_input(&ui,BP_UI_CANCEL);
    CHECK(!strcmp(ui.page->id,"workshop") && ui.selected==2);
}
static void confirmations_and_safety(void) {
    BpUiFrame f;
    start("aya");seek(4);const BpUiRow *original=&ui.page->rows[4];
    CHECK(bp_ui_buttons(&ui,-1,1,0)==BP_UI_REQUEST_CONFIRM);
    CHECK(ui.pending==original);
    CHECK(bp_ui_buttons(&ui,BP_UI_DOWN,1,0)==BP_UI_IDLE);
    CHECK(!bp_ui_open(&ui,"guns"));
    bp_ui_answer(&ui,1); CHECK(!ui.pending && ui.selected==4 && strstr(ui.notice,"nothing spent"));
    CHECK(bp_ui_buttons(&ui,-1,1,0)==BP_UI_IDLE);
    bp_ui_buttons(&ui,-1,0,0); CHECK(bp_ui_buttons(&ui,-1,1,0)==BP_UI_REQUEST_CONFIRM);
    bp_ui_answer(&ui,0); CHECK(strstr(ui.notice,"unchanged"));
    CHECK(!strcmp(original->value,"1/3") && !strcmp(ui.page->wallet,"BP 450"));
    bp_ui_input(&ui,BP_UI_HELP); CHECK(ui.expanded);
    bp_ui_input(&ui,BP_UI_CANCEL); CHECK(!ui.expanded && !strcmp(ui.page->id,"aya"));
    start("mastery");down(3);bp_ui_frame(&ui,&f);
    CHECK(f.selected->state==BP_UI_MAXED && !strcmp(f.detail[0],"Maximum rank"));
    CHECK(bp_ui_input(&ui,BP_UI_CONFIRM)!=BP_UI_REQUEST_CONFIRM);
    down(2);bp_ui_frame(&ui,&f);CHECK(!strcmp(f.detail[0],"Need 150 more BP"));
    down(1);bp_ui_frame(&ui,&f);CHECK(f.selected->state==BP_UI_LOCKED);
    CHECK(!strcmp(f.selected->label,"???"));
}
static void definitions_and_growth(void) {
    static const BpUiLine detail[]={{"Fourth-rank fixture"}};
    BpUiRow rows[40]; char ids[40][20],error[128];
    BpUiPage page={.id="growth",.title="Growth",.context="Fixture",.wallet="BP 900",.rows=rows,.row_count=40};
    BpUiCatalog c={.pages=&page,.page_count=1};
    for(unsigned i=0;i<40;++i){snprintf(ids[i],sizeof ids[i],"upgrade.%u",i);
        rows[i]=(BpUiRow){.id=ids[i],.label="New upgrade",.value="3/4",.state=BP_UI_AVAILABLE,
            .action=BP_UI_ASK,.target="buy.fixture",.question="Buy rank 4?",.details=detail,.detail_count=1};}
    CHECK(bp_ui_init(&ui,&c,"growth",7));down(39);CHECK(ui.selected==39 && ui.top==33);
    CHECK(bp_ui_input(&ui,BP_UI_CONFIRM)==BP_UI_REQUEST_CONFIRM);bp_ui_answer(&ui,0);
    rows[39].id=rows[0].id;CHECK(!bp_ui_validate(&c,error,sizeof error));
    rows[39].id=ids[39];rows[0].action=BP_UI_ROUTE;rows[0].target="missing";
    CHECK(!bp_ui_validate(&c,error,sizeof error));
    rows[0].action=BP_UI_NONE;rows[0].state=BP_UI_INACTIVE;
    CHECK(!bp_ui_validate(&c,error,sizeof error));rows[0].reason="Inactive";CHECK(bp_ui_validate(&c,error,sizeof error));
    rows[0].kind=BP_UI_SECTION;CHECK(!bp_ui_validate(&c,error,sizeof error));
    page.row_count=0;page.rows=NULL;CHECK(bp_ui_init(&ui,&c,"growth",7));
    {BpUiFrame f;bp_ui_frame(&ui,&f);CHECK(!f.selected && f.detail[0]);}
    CHECK(bp_ui_input(&ui,BP_UI_CONFIRM)==BP_UI_IDLE);
    CHECK(!bp_ui_init(NULL,&c,"growth",7));CHECK(!bp_ui_validate(NULL,error,sizeof error));
}
static void all_fixture_actions(void) {
    const BpUiCatalog *c=bp_ui_demo_catalog();
    for(size_t p=0;p<c->page_count;++p)for(size_t i=0;i<c->pages[p].row_count;++i){
        const BpUiPage *page=&c->pages[p];const BpUiRow *row=&page->rows[i];BpUiFrame f;
        if(!bp_ui_row_selectable(row))continue;
        start(page->id);seek(i);bp_ui_frame(&ui,&f);CHECK(f.selected==row && f.footer);
        BpUiEvent e=bp_ui_input(&ui,BP_UI_CONFIRM);
        if(row->state!=BP_UI_AVAILABLE)CHECK(!ui.pending && e!=BP_UI_REQUEST_CONFIRM);
        else if(row->action==BP_UI_ROUTE){CHECK(!strcmp(ui.page->id,row->target));bp_ui_input(&ui,BP_UI_CANCEL);CHECK(!strcmp(ui.page->id,page->id)&&ui.selected==i);}
        else if(row->action==BP_UI_ASK){CHECK(e==BP_UI_REQUEST_CONFIRM);bp_ui_answer(&ui,1);CHECK(!ui.pending&&ui.selected==i&&strstr(ui.notice,"nothing spent"));}
        else CHECK(!ui.pending&&ui.selected==i);
        if(row->preview && row->preview->cost)CHECK(row->preview->remaining);
    }
    bp_ui_frame(&ui,NULL);
}
static int monospace(const char *s){return(int)strlen(s);}
static void measured_details(void){
    static const BpUiLine lines[]={{"Protection 73% to 76% with armor"},{"AReallyLongUnbrokenDescriptionToken"},{"100 BP - 350 BP remaining"}};
    const BpUiRow row={.id="wide",.label="Very long upgrade name",.value="100 BP",.state=BP_UI_AVAILABLE,
        .action=BP_UI_ASK,.target="buy",.question="Buy?",.details=lines,.detail_count=3};
    const BpUiPage page={.id="wrap",.title="Wrap",.context="Preview",.wallet="450 BP",.rows=&row,.row_count=1};
    const BpUiCatalog c={.pages=&page,.page_count=1};BpUiFrame f;
    char collected[1024]={0},expected[1024]={0};
    const char *source[]={row.label,row.value,lines[0].text,lines[1].text,lines[2].text};
    CHECK(bp_ui_init(&ui,&c,"wrap",7));bp_ui_set_text_metrics(&ui,12,monospace);bp_ui_frame(&ui,&f);
    size_t pages=f.detail_pages;CHECK(pages>1);
    for(size_t i=0;i<pages;++i){bp_ui_frame(&ui,&f);for(unsigned j=0;j<3;++j)if(f.detail[j]){
        const char*s=f.detail[j];CHECK(strlen(s)<=12);while(*s){if(*s!=' ')collected[strlen(collected)]=*s;++s;}}
        bp_ui_input(&ui,BP_UI_PAGE_NEXT);}
    for(unsigned i=0;i<5;++i){const char*s=source[i];while(*s){if(*s!=' ')expected[strlen(expected)]=*s;++s;}}
    CHECK(!strcmp(collected,expected));CHECK(ui.detail_page==pages-1);
    bp_ui_set_text_metrics(&ui,200,monospace);bp_ui_frame(&ui,&f);CHECK(ui.detail_page==0&&f.detail_pages==1);
}
static void refund_tabs(void){
    start("reset.one");CHECK(bp_ui_tab_visible(&ui,4)&&bp_ui_tab_visible(&ui,5));
    for(size_t i=0;i<4;++i)CHECK(!bp_ui_tab_visible(&ui,i));
    CHECK(!bp_ui_category_target(&ui,-1));
    CHECK(!strcmp(bp_ui_category_target(&ui,1),"reset.mastery"));
    seek(4);CHECK(bp_ui_input(&ui,BP_UI_CONFIRM)==BP_UI_REQUEST_CONFIRM);
    bp_ui_answer(&ui,0);bp_ui_input(&ui,BP_UI_RIGHT);CHECK(!strcmp(ui.page->id,"reset.mastery"));
    CHECK(!bp_ui_category_target(&ui,1));bp_ui_input(&ui,BP_UI_LEFT);CHECK(ui.selected==4);
    seek(1);CHECK(ui.page->rows[1].state==BP_UI_BLOCKED);
    CHECK(bp_ui_input(&ui,BP_UI_CONFIRM)!=BP_UI_REQUEST_CONFIRM);
    start("mastery");CHECK(!bp_ui_category_target(&ui,1));
    start("reset.all");seek(4);CHECK(ui.page->rows[4].state==BP_UI_UNAFFORDABLE);
    CHECK(bp_ui_input(&ui,BP_UI_CONFIRM)!=BP_UI_REQUEST_CONFIRM);
}
static void tabs_and_f1(void){
    const BpUiCatalog*c=bp_ui_demo_catalog(),*f1=bp_ui_demo_f1_catalog();
    CHECK(c->tab_count==6 && !strcmp(c->tabs[2].id,"aya"));
    CHECK(bp_ui_validate(f1,NULL,0)&&f1->pages[0].row_count==5&&f1->pages[1].row_count==5);
    start("preview");CHECK(ui.page->row_count==2);
    CHECK(!bp_ui_open(&ui,"resistance")&&!bp_ui_open(&ui,"presets")&&!bp_ui_open(&ui,"guides"));
    BpUiCatalog bad=*c;BpUiTab tabs[6];memcpy(tabs,c->tabs,sizeof tabs);bad.tabs=tabs;tabs[1].id=tabs[0].id;
    CHECK(!bp_ui_validate(&bad,NULL,0));
}
/* Menu sounds, as the game's other menus use them: move, confirm (enter / ask / done), cancel
 * (any back), error (refused). The game's Yes/No box plays its own, so answers add only errors. */
static int commit_ok;
static int fake_commit(void *user,const char *request,char *notice,size_t size){(void)user;(void)request;snprintf(notice,size,"%s",commit_ok?"Done":"Refused");return commit_ok;}
static void sounds(void){
    #define SOUND(call,want) do{ (void)(call); CHECK(ui.sound==(want)); }while(0)
    start("aya");
    SOUND(bp_ui_input(&ui,BP_UI_UP),BP_UI_SOUND_NONE);              /* already at the top */
    SOUND(bp_ui_input(&ui,BP_UI_DOWN),BP_UI_SOUND_MOVE);
    SOUND(bp_ui_input(&ui,BP_UI_HELP),BP_UI_SOUND_MOVE);
    SOUND(bp_ui_input(&ui,BP_UI_CANCEL),BP_UI_SOUND_CANCEL);         /* closes help */
    CHECK(bp_ui_input(&ui,BP_UI_CANCEL)==BP_UI_EXIT && ui.sound==BP_UI_SOUND_CANCEL);
    start("guns");down(1);
    SOUND(bp_ui_input(&ui,BP_UI_CONFIRM),BP_UI_SOUND_CONFIRM);       /* opens a gun */
    CHECK(!strcmp(ui.page->id,"gun.m84f"));
    SOUND(bp_ui_input(&ui,BP_UI_CANCEL),BP_UI_SOUND_CANCEL);         /* back to the list */
    SOUND(bp_ui_input(&ui,BP_UI_RIGHT),BP_UI_SOUND_MOVE);            /* next tab */
    start("aya");seek(4);
    CHECK(bp_ui_input(&ui,BP_UI_CONFIRM)==BP_UI_REQUEST_CONFIRM && ui.sound==BP_UI_SOUND_CONFIRM);
    SOUND(bp_ui_input(&ui,BP_UI_DOWN),BP_UI_SOUND_NONE);             /* the Yes/No box owns input */
    SOUND(bp_ui_answer(&ui,1),BP_UI_SOUND_NONE);                     /* the box plays its own */
    start("mastery");down(3);CHECK(ui.page->rows[ui.selected].state==BP_UI_MAXED);
    SOUND(bp_ui_input(&ui,BP_UI_CONFIRM),BP_UI_SOUND_ERROR);
    start("reset.all");seek(4);CHECK(ui.page->rows[4].state==BP_UI_UNAFFORDABLE);
    SOUND(bp_ui_input(&ui,BP_UI_CONFIRM),BP_UI_SOUND_ERROR);
    {   /* read-only rows, settings and refused purchases, with a backend */
        static const BpUiLine detail[]={{"Fixture"}};
        BpUiRow rows[3]={
            {.id="info",.label="Info",.value="-",.state=BP_UI_AVAILABLE,.action=BP_UI_NONE,.details=detail,.detail_count=1},
            {.id="set",.label="Mode",.value="A",.state=BP_UI_AVAILABLE,.action=BP_UI_SETTING,.target="set.fort.1",.details=detail,.detail_count=1},
            {.id="buy",.label="Rank",.value="0/3",.state=BP_UI_AVAILABLE,.action=BP_UI_ASK,.target="buy.def.0",.question="Buy?",.details=detail,.detail_count=1}};
        BpUiPage page={.id="fx",.title="Fx",.context="Fixture",.wallet="BP 900",.rows=rows,.row_count=3};
        BpUiCatalog c={.pages=&page,.page_count=1};
        BpUiBackend backend={fake_commit,NULL,NULL};
        CHECK(bp_ui_init(&ui,&c,"fx",7));bp_ui_set_backend(&ui,&backend);
        SOUND(bp_ui_input(&ui,BP_UI_CONFIRM),BP_UI_SOUND_ERROR);     /* read only */
        seek(1);commit_ok=1;SOUND(bp_ui_input(&ui,BP_UI_CONFIRM),BP_UI_SOUND_CONFIRM);
        commit_ok=0;SOUND(bp_ui_input(&ui,BP_UI_CONFIRM),BP_UI_SOUND_ERROR);
        seek(2);commit_ok=1;bp_ui_input(&ui,BP_UI_CONFIRM);SOUND(bp_ui_answer(&ui,1),BP_UI_SOUND_NONE);
        commit_ok=0;bp_ui_input(&ui,BP_UI_CONFIRM);SOUND(bp_ui_answer(&ui,1),BP_UI_SOUND_ERROR);
        bp_ui_input(&ui,BP_UI_CONFIRM);SOUND(bp_ui_answer(&ui,0),BP_UI_SOUND_NONE);   /* No: the box's cancel */
    }
    #undef SOUND
}
int main(void){navigation();confirmations_and_safety();definitions_and_growth();all_fixture_actions();measured_details();tabs_and_f1();refund_tabs();sounds();
    puts("PASS: four tabs, section skipping, equipment return focus, help, measured details, F1-only settings, no-op transactions and menu sounds");return 0;}
