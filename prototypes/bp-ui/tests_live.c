/* Live backend checks: rules, transactions (atomic refusals) and the live catalog. */
#include "bp_catalog.h"
#include "bp_txn.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while (0)

/* ---- a fake game the executor drives ---- */
static BpSnapshot game;
static int store_ok = 1, spend_ok = 1, writes, recalcs;
static unsigned spent_junk;
static int f_read(BpSnapshot *out, void *u) { (void)u; *out = game; return game.loaded; }
static int f_store(void *u) { (void)u; return store_ok; }
static int f_spend(unsigned n, void *u) { (void)u; if (!spend_ok || n > game.junk) return 0; game.junk -= n; spent_junk += n; return 1; }
static void f_bp(unsigned bp, void *u) { (void)u; game.bp = bp; ++writes; }
static void f_gear(int record, int stat, int value, void *u) {
    (void)u; ++writes;
    for (int i = 0; i < game.gear_count; ++i) if (game.gear[i].record == record) game.gear[i].bonus[stat] = value;
}
static void f_stat(int stat, unsigned value, void *u) { (void)u; ++writes; if (stat == 5) game.active.value = value; else if (stat == 6) game.capacity.value = value; else abort(); }
static void f_rec(const BpRecord *r, void *u) { (void)u; ++writes; game.rec = *r; }
static void f_after(int stats, int gear, void *u) { (void)u; (void)gear; recalcs += stats; }
static unsigned double_price(unsigned base) { return base * 2; }
static const BpGameOps ops = {f_read, NULL, f_store, f_spend, f_bp, f_gear, f_stat, f_rec, f_after, NULL};

static void fresh(void) {
    memset(&game, 0, sizeof game);
    game.features = BP_FEATURE_ALL;
    game.loaded = 1; game.bp = 1000; game.economy = 1; game.junk = 300; game.level = 10;
    game.learned = 1u | 1u << 7 | 1u << 9;    /* Heal 1, Scan, Haste */
    game.active = (BpAyaStat){.value = 150, .next_value = 160, .level = 13, .next_level = 14, .shown = 14, .next_shown = 15};
    game.capacity = (BpAyaStat){.value = 120, .next_value = 130, .level = 12, .next_level = 13, .shown = 14, .next_shown = 15};
    game.active_base = 100; game.capacity_base = 100; game.capacity_reset_ok = 1; game.capacity_steps = 2;
    game.capacity_after_reset = 12;
    game.gear_count = 2;
    game.gear[0] = (BpGear){.record = 3, .armor = 0, .equipped = 1, .item_id = 1, .name = "M92F", .base = {20, 35, 8}, .bonus = {1, 0, 0}};
    game.gear[1] = (BpGear){.record = 7, .armor = 1, .equipped = 1, .item_id = 2, .name = "N Vest", .base = {24, 8, 8}, .bonus = {0, 0, 998}};
    writes = recalcs = 0; spent_junk = 0; store_ok = spend_ok = 1;
}
static BpResult run(const char *id) { return bp_execute(&ops, id); }
#define UNCHANGED(before) CHECK(!memcmp(&(before), &game, sizeof game))

static void fees(void) {
    CHECK(bp_reset_fee_base(0, 300, 0) == 65);
    CHECK(bp_reset_fee_base(0, 750, 7) == 90);       /* track resets never escalate */
    CHECK(bp_reset_fee_base(1, 1200, 0) == 260);
    CHECK(bp_reset_fee_base(1, 1200, 2) == 460);
    CHECK(bp_reset_fee_base(1, 2400, 0) == 320 && bp_reset_fee_base(1, 2400, 2) == 520);
    CHECK(bp_reset_fee_base(1, 0, 9) == 500);         /* base cap */
    CHECK(bp_reset_fee_base(0, 250, 0) == 65);        /* 250 BP rounds up to 300 */
    CHECK(bp_track_spent(0, 3) == 300 && bp_track_spent(BP_FORTITUDE, 3) == 450);
    CHECK(bp_line_spent(0, 3) == 750 && bp_line_spent(1, 3) == 300 && bp_line_spent(2, 3) == 600);
    CHECK(bp_line_spent(5, 1) == 100 && bp_line_price(5, 2) == 0);   /* Scan: one rank */
    CHECK(bp_effective_resist(70, 1) == 73 && bp_effective_resist(50, 3) == 65 && bp_effective_resist(0, 3) == 30);
    CHECK(bp_line_cost(3, 0, 0, 300) == 100 && bp_line_cost(3, 3, 1, 300) == 33);   /* Barrier 1/3 -> 1/6 x 2/3 */
    CHECK(bp_line_cost(5, 1, 0, 0) == 0 && bp_line_cost(1, 3, 1, 0) == 33);         /* Scan free; Detox 50% x 2/3 */
}

static void requests(void) {
    BpRequest r; char id[48];
    const char *ok[] = {"buy.gear.12.2", "buy.aya.5", "buy.aya.6", "buy.def.4", "buy.line.11",
                        "reset.def.0", "reset.line.3", "reset.all", "set.fort.1"};
    const char *bad[] = {"buy.gear.12.3", "buy.aya.4", "buy.def.5", "buy.line.12", "set.fort.2",
                         "buy.def.-1", "buy.def.1x", "reset", "", "buy.gear.1"};
    for (unsigned i = 0; i < sizeof ok / sizeof *ok; ++i) {
        CHECK(bp_request_parse(ok[i], &r)); bp_request_format(&r, id, sizeof id); CHECK(!strcmp(id, ok[i]));
    }
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; ++i) CHECK(!bp_request_parse(bad[i], &r));
    CHECK(!bp_request_parse(NULL, &r));
}

static void purchases(void) {
    BpSnapshot before; BpResult r;
    fresh(); r = run("buy.def.0");
    CHECK(!r.error && game.bp == 925 && game.rec.defense[0] == 1 && !spent_junk);
    r = run("buy.def.0"); CHECK(!r.error && game.bp == 825 && game.rec.defense[0] == 2);
    r = run("buy.def.0"); CHECK(!r.error && game.bp == 700 && game.rec.defense[0] == 3);
    before = game; r = run("buy.def.0"); CHECK(r.error == BP_ERR_MAXED); UNCHANGED(before);

    fresh(); game.bp = 74; before = game; r = run("buy.def.0"); CHECK(r.error == BP_ERR_BP); UNCHANGED(before);
    fresh(); game.bp = 75; r = run("buy.def.0"); CHECK(!r.error && game.bp == 0);
    fresh(); game.bp = 99; before = game; r = run("buy.gear.3.0"); CHECK(r.error == BP_ERR_BP); UNCHANGED(before);
    fresh(); before = game; r = run("buy.line.1"); CHECK(r.error == BP_ERR_LOCKED); UNCHANGED(before);   /* Detox unlearned */
    fresh(); r = run("buy.line.0"); CHECK(!r.error && game.bp == 850 && game.rec.mastery[0] == 1);
    fresh(); r = run("buy.line.5"); CHECK(!r.error && game.rec.mastery[5] == 1);
    before = game; r = run("buy.line.5"); CHECK(r.error == BP_ERR_MAXED); UNCHANGED(before);

    /* Store refusal: nothing written, not even BP. */
    fresh(); store_ok = 0; before = game; r = run("buy.def.1"); CHECK(r.error == BP_ERR_STORE && !writes); UNCHANGED(before);
    /* Gear and Aya stats do not need the store. */
    fresh(); store_ok = 0; r = run("buy.gear.3.0"); CHECK(!r.error && game.gear[0].bonus[0] == 2 && game.bp == 900);
    fresh(); r = run("buy.gear.7.2"); CHECK(!r.error && game.gear[1].bonus[2] == 999);
    before = game; r = run("buy.gear.7.2"); CHECK(r.error == BP_ERR_MAXED); UNCHANGED(before);
    fresh(); before = game; r = run("buy.gear.99.0"); CHECK(r.error == BP_ERR_UNKNOWN); UNCHANGED(before);
    fresh(); r = run("buy.aya.5");
    CHECK(!r.error && game.active.value == 160 && game.rec.active_steps == 1 && game.rec.active_points == 10 && recalcs == 1);
    fresh(); r = run("buy.aya.6"); CHECK(!r.error && game.capacity.value == 130 && !game.rec.active_steps);
    fresh(); game.active.maxed = 1; before = game; r = run("buy.aya.5"); CHECK(r.error == BP_ERR_MAXED); UNCHANGED(before);
    /* EX Game pool pays first, then BP. */
    fresh(); game.rec.carried_pool = 50; r = run("buy.def.2");
    CHECK(!r.error && game.rec.carried_pool == 0 && game.bp == 975 && r.quote.pool_cost == 50);
    fresh(); game.rec.carried_pool = 50; r = run("buy.gear.3.1"); CHECK(!r.error && game.rec.carried_pool == 50 && game.bp == 900);
    /* Not loaded. */
    fresh(); game.loaded = 0; before = game; r = run("buy.def.0"); CHECK(r.error == BP_ERR_NOT_LOADED); UNCHANGED(before);
    fresh(); r = run("set.fort.1"); CHECK(!r.error && game.rec.fortitude_mode == 1 && game.bp == 1000);
    before = game; r = run("set.fort.1"); CHECK(r.error == BP_ERR_UNCHANGED); UNCHANGED(before);
}

static void resets(void) {
    BpSnapshot before; BpResult r;
    fresh(); game.rec.defense[0] = 3;
    r = run("reset.def.0");
    CHECK(!r.error && game.bp == 1300 && game.junk == 235 && spent_junk == 65 && !game.rec.defense[0] && !game.rec.full_resets);
    fresh(); before = game; r = run("reset.def.0"); CHECK(r.error == BP_ERR_EMPTY); UNCHANGED(before);
    fresh(); game.rec.defense[0] = 3; game.junk = 64; before = game; r = run("reset.def.0"); CHECK(r.error == BP_ERR_JUNK); UNCHANGED(before);
    fresh(); game.rec.defense[0] = 3; spend_ok = 0; before = game; r = run("reset.def.0"); CHECK(r.error == BP_ERR_JUNK && !writes); UNCHANGED(before);
    fresh(); game.rec.defense[0] = 3; game.bp = 99800; before = game; r = run("reset.def.0"); CHECK(r.error == BP_ERR_OVERFLOW); UNCHANGED(before);
    fresh(); game.rec.defense[0] = 3; game.economy = 0; before = game; r = run("reset.def.0"); CHECK(r.error == BP_ERR_NO_ECONOMY); UNCHANGED(before);
    fresh(); game.rec.defense[0] = 3; store_ok = 0; before = game; r = run("reset.def.0"); CHECK(r.error == BP_ERR_STORE && !spent_junk); UNCHANGED(before);
    /* Economy preset pricing applies to the fee. */
    {
        BpGameOps priced = ops; priced.price = double_price;
        fresh(); game.rec.mastery[0] = 3;
        r = bp_execute(&priced, "reset.line.0");
        CHECK(!r.error && r.quote.junk_fee_base == 90 && r.quote.junk_fee == 180 && game.junk == 120 && game.bp == 1750);
    }
    /* Full reset: ranks, ledgered Active Time, exact Item Capacity, escalation. */
    fresh(); game.rec.defense[0] = 3; game.rec.defense[4] = 1; game.rec.mastery[0] = 2;
    game.rec.active_steps = 3; game.rec.active_points = 30; game.junk = 1000;
    r = run("reset.all");
    CHECK(!r.error && r.quote.refund_defense == 400 && r.quote.refund_mastery == 400 && r.quote.refund_aya == 500);
    CHECK(r.quote.junk_fee == 200 + 5 * 13 && game.bp == 2300 && game.junk == 1000 - 265);
    CHECK(game.active.value == 120 && game.capacity.value == 100 && game.rec.full_resets == 1 && !game.rec.active_steps && recalcs == 1);
    CHECK(!game.rec.defense[0] && !game.rec.defense[4] && !game.rec.mastery[0]);
    game.rec.defense[1] = 1; game.capacity_steps = 0;   /* the adapter re-measures after a reset */
    r = run("reset.all"); CHECK(!r.error && r.quote.junk_fee == 300 + 5 && game.rec.full_resets == 2);
    /* Bag would shrink onto items. */
    fresh(); game.bag_items_beyond = 2; before = game; r = run("reset.all"); CHECK(r.error == BP_ERR_BAG); UNCHANGED(before);
    /* Capacity that can't be measured is not refunded and not written. */
    fresh(); game.capacity_reset_ok = 0; game.rec.defense[2] = 1; r = run("reset.all");
    CHECK(!r.error && r.quote.refund_aya == 0 && game.capacity.value == 120 && game.bp == 1075);
}

static void save_encoding(void) {
    BpRecord a, b; uint8_t ranks[BP_RANKS_BYTES], run_[BP_RUN_BYTES];
    memset(&a, 0, sizeof a);
    a.defense[0] = 3; a.defense[4] = 2; a.fortitude_mode = 1; a.mastery[5] = 1; a.mastery[11] = 3;
    a.full_resets = 4; a.active_steps = 7; a.active_points = 71; a.carried_pool = 1234;
    bp_record_encode(&a, ranks, run_);
    CHECK(!bp_record_decode(ranks, run_, &b) && bp_record_equal(&a, &b));
    ranks[0] = 4; CHECK(bp_record_decode(ranks, run_, &b)); ranks[0] = 3;
    ranks[6 + 5] = 2; CHECK(bp_record_decode(ranks, run_, &b)); ranks[6 + 5] = 1;   /* Scan has one rank */
    ranks[5] = 2; CHECK(bp_record_decode(ranks, run_, &b)); ranks[5] = 1;
    run_[2] = 0; run_[3] = 0; CHECK(bp_record_decode(ranks, run_, &b));           /* points without steps */
    memset(ranks, 0, sizeof ranks); memset(run_, 0, sizeof run_); CHECK(!bp_record_decode(ranks, run_, &b));
}

/* ---- the live catalog through the real controller ---- */
static int c_commit(void *u, const char *req, char *notice, size_t n) {
    BpResult r = run(req); (void)u; bp_result_text(&r, &game, notice, n); return !r.error;
}
static const BpUiCatalog *c_refresh(void *u) { (void)u; return bp_catalog_build(&game, NULL); }
static const BpUiBackend backend = {c_commit, c_refresh, NULL};

static size_t row_index(const BpUiPage *p, const char *id) {
    for (size_t i = 0; i < p->row_count; ++i) if (!strcmp(p->rows[i].id, id)) return i;
    CHECK(0); return 0;
}
/* Public roots must never lead to the other transaction family. */
static void route_scope(const BpUiCatalog *c,const char *root,int refund) {
    unsigned char seen[BP_UI_MAX_PAGES]={0};
    BpUiController u;CHECK(bp_ui_init(&u,c,root,7));
    seen[(size_t)(u.page-c->pages)]=1;
    for(size_t pass=0;pass<c->page_count;++pass)for(size_t p=0;p<c->page_count;++p)if(seen[p]) {
        const BpUiPage *page=&c->pages[p];CHECK(bp_ui_open(&u,page->id));
        CHECK(refund ? !strncmp(page->id,"reset",5) : strncmp(page->id,"reset",5)!=0);
        for(size_t t=0;t<c->tab_count;++t)if(bp_ui_tab_visible(&u,t))
            for(size_t n=0;n<c->page_count;++n)if(!strcmp(c->pages[n].id,c->tabs[t].page))seen[n]=1;
        for(size_t r=0;r<page->row_count;++r)if(page->rows[r].action==BP_UI_ROUTE)
            for(size_t n=0;n<c->page_count;++n)if(!strcmp(c->pages[n].id,page->rows[r].target))seen[n]=1;
    }
    CHECK(!bp_ui_open(&u,"preview"));
}

static void catalog(void) {
    static BpUiController ui;
    char error[128];
    const BpUiCatalog *c;
    BpCatalogUsage u;
    fresh(); game.rec.defense[0] = 1;
    c = bp_catalog_build(&game, NULL);
    CHECK(bp_ui_validate(c, error, sizeof error) || (fprintf(stderr, "%s\n", error), 0));
    u = bp_catalog_usage(); CHECK(!u.overflow);
    route_scope(c,"aya",0);route_scope(c,"reset.one",1);
    CHECK(bp_ui_init(&ui, c, "aya", 7)); bp_ui_set_backend(&ui, &backend);
    {   /* Buy Poison rank 2 through a confirmation; focus stays on the row. */
        size_t at = row_index(ui.page, "def.poison");
        while (ui.selected < at) bp_ui_input(&ui, BP_UI_DOWN);
        CHECK(ui.selected == at);
        CHECK(bp_ui_input(&ui, BP_UI_CONFIRM) == BP_UI_REQUEST_CONFIRM);
        bp_ui_answer(&ui, 0); CHECK(game.rec.defense[0] == 1 && strstr(ui.notice, "unchanged"));
        CHECK(bp_ui_input(&ui, BP_UI_CONFIRM) == BP_UI_REQUEST_CONFIRM);
        bp_ui_answer(&ui, 1);
        CHECK(game.rec.defense[0] == 2 && game.bp == 900 && strstr(ui.notice, "Poison rank 2"));
        CHECK(!strcmp(ui.page->id, "aya") && ui.selected == at && !strcmp(ui.page->rows[at].value, "2/3"));
        CHECK(!strcmp(ui.page->wallet, "BP 900"));
    }
    {   /* Gear route, purchase, back keeps the gear selection. */
        CHECK(bp_ui_open(&ui, "guns"));
        CHECK(bp_ui_input(&ui, BP_UI_CONFIRM) == BP_UI_CHANGED && !strcmp(ui.page->id, "gear.3"));
        CHECK(bp_ui_input(&ui, BP_UI_CONFIRM) == BP_UI_REQUEST_CONFIRM); bp_ui_answer(&ui, 1);
        CHECK(game.gear[0].bonus[0] == 2 && !strcmp(ui.page->id, "gear.3"));
        bp_ui_input(&ui, BP_UI_CANCEL); CHECK(!strcmp(ui.page->id, "guns"));
    }
    {   /* Gear that leaves the bag: its open page falls back to the tab. */
        CHECK(bp_ui_open(&ui, "gear.3"));
        game.gear_count = 1; game.gear[0] = game.gear[1];
        CHECK(bp_ui_reload(&ui, bp_catalog_build(&game, NULL)) && ui.page && ui.page->tab_id);
    }
    {   /* Locked mastery shows ???; unaffordable shows the shortfall. */
        fresh(); game.bp = 10; c = bp_catalog_build(&game, NULL);
        CHECK(bp_ui_init(&ui, c, "mastery", 7));
        const BpUiPage *p = ui.page;
        CHECK(!strcmp(p->rows[1].label, "???") && p->rows[1].state == BP_UI_LOCKED);
        CHECK(p->rows[0].state == BP_UI_UNAFFORDABLE && !strcmp(p->rows[0].reason, "Need 140 more BP"));
    }
    {   /* Refund pages: blocked Aya rows, a working track reset with the fee pinned. */
        fresh(); game.rec.defense[0] = 3; c = bp_catalog_build(&game, NULL);
        CHECK(bp_ui_init(&ui, c, "reset.one", 7)); bp_ui_set_backend(&ui, &backend);
        CHECK(ui.page->rows[1].state == BP_UI_BLOCKED);
        size_t at = row_index(ui.page, "refund.def.poison");
        CHECK(!strcmp(ui.page->rows[at].preview->cost, "65 Junk") && !strcmp(ui.page->rows[at].preview->remaining, "235 Junk"));
        while (ui.selected < at) bp_ui_input(&ui, BP_UI_DOWN);
        CHECK(bp_ui_input(&ui, BP_UI_CONFIRM) == BP_UI_REQUEST_CONFIRM); bp_ui_answer(&ui, 1);
        CHECK(!game.rec.defense[0] && game.bp == 1300 && game.junk == 235 && strstr(ui.notice, "Refunded 300 BP"));
        CHECK(ui.page->rows[at].state == BP_UI_BLOCKED);
        CHECK(bp_ui_open(&ui,"reset.one"));
        size_t all_at=row_index(ui.page,"refund.all.open");
        while(ui.selected<all_at)bp_ui_input(&ui,BP_UI_DOWN);
        bp_ui_input(&ui,BP_UI_CONFIRM);CHECK(!strcmp(ui.page->id,"reset.all"));
        bp_ui_input(&ui,BP_UI_CANCEL);CHECK(!strcmp(ui.page->id,"reset.one")&&ui.selected==all_at);
        CHECK(bp_ui_input(&ui,BP_UI_CANCEL)==BP_UI_EXIT);
        game.economy = 0; c = bp_catalog_build(&game, NULL);
        CHECK(bp_ui_init(&ui, c, "reset", 7) && ui.page->rows[0].state == BP_UI_BLOCKED);
    }
    {   /* Fortitude setting commits without a confirmation. */
        fresh(); c = bp_catalog_build(&game, NULL);
        CHECK(bp_ui_init(&ui, c, "fortitude.settings", 7)); bp_ui_set_backend(&ui, &backend);
        bp_ui_input(&ui, BP_UI_DOWN); bp_ui_input(&ui, BP_UI_CONFIRM);
        CHECK(game.rec.fortitude_mode == 1 && !strcmp(ui.page->rows[1].value, "Current"));
    }
    {   /* A full bag of 50 gear items stays inside the storage budget, and IDs stay interned. */
        const char *first;
        fresh(); game.gear_count = BP_MAX_GEAR;
        for (int i = 0; i < BP_MAX_GEAR; ++i)
            game.gear[i] = (BpGear){.record = i, .armor = i % 3 == 0, .name = "Long Gear Name", .base = {100, 100, 100}};
        c = bp_catalog_build(&game, NULL);
        CHECK(bp_ui_validate(c, error, sizeof error) || (fprintf(stderr, "%s\n", error), 0));
        u = bp_catalog_usage(); CHECK(!u.overflow && u.pages <= BP_UI_MAX_PAGES);
        first = c->pages[1].id;
        for (int k = 0; k < 20; ++k) c = bp_catalog_build(&game, NULL);
        u = bp_catalog_usage(); CHECK(!u.overflow && c->pages[1].id == first);
        fprintf(stderr, "catalog usage: pages=%zu rows=%zu lines=%zu previews=%zu text=%zu interned=%zu\n",
                u.pages, u.rows, u.lines, u.previews, u.text, u.interned);
    }
    {   /* Not loaded: everything readable, nothing purchasable. */
        BpSnapshot none; memset(&none, 0, sizeof none);
        c = bp_catalog_build(&none, NULL); CHECK(bp_ui_validate(c, error, sizeof error));
    }
}

static void preset_lifecycle(void){
    CHECK(bp_preset_flags(BP_PRESET_VANILLA_QOL)==BP_FEATURE_RESPEC);
    for(int i=0;i<BP_PRESET_CUSTOM;i++)CHECK(bp_config_preset(bp_preset_flags(i))==i);
    CHECK(bp_config_preset(0)==BP_PRESET_CUSTOM&&!bp_config_valid(16));
    fresh();game.rec.defense[0]=2;game.rec.mastery[0]=1;
    unsigned bp=game.bp,junk=game.junk;BpRecord original=game.rec;BpGear gear=game.gear[0];
    uint8_t ranks[BP_RANKS_BYTES],runbytes[BP_RUN_BYTES],r2[BP_RANKS_BYTES],b2[BP_RUN_BYTES];
    bp_record_encode(&game.rec,ranks,runbytes);
    for(int i=0;i<30;i++){
        game.features=bp_preset_flags(i%BP_PRESET_CUSTOM);
        BpRecord reloaded;CHECK(!bp_record_decode(ranks,runbytes,&reloaded));game.rec=reloaded;
        CHECK(bp_record_equal(&game.rec,&original)&&game.bp==bp&&game.junk==junk);
        CHECK(!memcmp(&gear,&game.gear[0],sizeof gear));
        bp_record_encode(&game.rec,r2,b2);CHECK(!memcmp(ranks,r2,sizeof ranks)&&!memcmp(runbytes,b2,sizeof runbytes));
    }
    game.features=BP_FEATURE_RESPEC;BpSnapshot before=game;
    CHECK(run("buy.def.0").error==BP_ERR_INACTIVE);UNCHANGED(before);
    CHECK(run("buy.line.0").error==BP_ERR_INACTIVE);UNCHANGED(before);
    CHECK(run("set.fort.1").error==BP_ERR_INACTIVE);UNCHANGED(before);
    BpUiController ui;const BpUiCatalog *c=bp_catalog_build(&game,NULL);
    CHECK(bp_ui_init(&ui,c,"aya",7));size_t at=row_index(ui.page,"def.poison");
    CHECK(ui.page->rows[at].state==BP_UI_INACTIVE&&!ui.page->rows[at].preview->cost);
    CHECK(bp_ui_init(&ui,c,"mastery",7));CHECK(ui.page->rows[0].state==BP_UI_INACTIVE);
    CHECK(run("reset.def.0").error==BP_OK&&game.rec.defense[0]==0); /* inactive investments remain refundable */
    unsigned returned=game.bp;game.features=BP_FEATURE_ALL;CHECK(game.bp==returned&&game.rec.defense[0]==0&&game.rec.mastery[0]==1);
    game.features=0;before=game;CHECK(run("reset.all").error==BP_ERR_INACTIVE);UNCHANGED(before);
    CHECK(run("buy.gear.3.0").error==BP_OK); /* equipment ignores feature toggles */
}

static void battle_numbers(void) {
    BpFortitudePlan p;
    /* Halve HP, rank 1: one charge, Aya keeps 3/4 instead of 1/2. */
    p = bp_fortitude_plan(BP_FX_HALVE_HP, 100, 0, 1, BP_FORT_BOTH, 0);
    CHECK(p.use && !p.negate && p.result_hp == 75 && p.vanilla_hp == 50);
    p = bp_fortitude_plan(BP_FX_HALVE_HP, 100, 0, 1, BP_FORT_BOTH, 1); CHECK(!p.use);     /* no charge left */
    p = bp_fortitude_plan(BP_FX_HALVE_HP, 100, 1, 3, BP_FORT_BOTH, 0); CHECK(!p.use);     /* Barrier blocks it anyway */
    p = bp_fortitude_plan(BP_FX_HALVE_HP, 1, 0, 3, BP_FORT_BOTH, 0); CHECK(!p.use);       /* no improvement */
    p = bp_fortitude_plan(BP_FX_HALVE_HP, 100, 0, 2, BP_FORT_SAVE_FOR_HP1, 0); CHECK(!p.use);
    /* HP to 1: soften to 1/4 (min 1) only when it helps; at 1 HP vanilla goes to -1. */
    p = bp_fortitude_plan(BP_FX_HP_TO_1, 100, 0, 2, BP_FORT_SAVE_FOR_HP1, 1); CHECK(p.use && p.result_hp == 25);
    p = bp_fortitude_plan(BP_FX_HP_TO_1, 6, 0, 2, BP_FORT_BOTH, 0); CHECK(!p.use && p.result_hp == 1);
    p = bp_fortitude_plan(BP_FX_HP_TO_1, 1, 0, 1, BP_FORT_BOTH, 0); CHECK(p.use && p.result_hp == 1 && p.vanilla_hp == -1);
    /* Rank 3: the first charge negates (buff strip too), the second softens. */
    p = bp_fortitude_plan(BP_FX_HP_TO_1, 6, 0, 3, BP_FORT_BOTH, 0); CHECK(p.use && p.negate && p.result_hp == 6);
    p = bp_fortitude_plan(BP_FX_HP_TO_1, 80, 0, 3, BP_FORT_BOTH, 1); CHECK(p.use && !p.negate && p.result_hp == 20);
    p = bp_fortitude_plan(BP_FX_HP_TO_1, 80, 0, 3, BP_FORT_BOTH, 2); CHECK(!p.use);
    p = bp_fortitude_plan(16, 80, 0, 3, BP_FORT_BOTH, 0); CHECK(!p.use);                  /* HP to 3/4 excluded */
    p = bp_fortitude_plan(BP_FX_HALVE_HP, 80, 0, 0, BP_FORT_BOTH, 0); CHECK(!p.use);
    CHECK(bp_line_for_power(0) == 0 && bp_line_for_power(2) == 0 && bp_line_for_power(6) == 4 && bp_line_for_power(18) == 11);
    CHECK(bp_line_for_power(13) == -1 && bp_line_for_power(19) == -1);
    CHECK(bp_line_cut(0, 1) == 0 && bp_line_cut(0, 2) == 15 && bp_line_cut(5, 1) == 100 && bp_line_cut(7, 3) == 25);
    CHECK(bp_heal_bonus(1) == 15 && bp_heal_bonus(3) == 30 && bp_haste_bonus(2) == 50);
    /* 50% + 50% x reroll = 60 / 70 / 80%. */
    for (int r = 1; r <= 3; ++r) CHECK(50 + 50 * bp_neutral_reroll(r) / 100 == bp_neutral_chance(r));
    CHECK(bp_neutral_chance(3) == 80 && bp_energy_shot_bonus(3, 1) == 30 && bp_energy_shot_bonus(3, 0) == 20);
}

/* EX Game carryover: three modes, carried ranks refund upgrade-only (BP-Q19), save section. */
static void ex_carryover(void) {
    BpRecord last, n;
    BpResult r;
    BpSnapshot before;
    uint8_t carry[BP_CARRY_BYTES];
    memset(&last, 0, sizeof last);
    last.defense[0] = 3; last.defense[BP_FORTITUDE] = 2; last.mastery[0] = 2; last.mastery[5] = 1;
    last.fortitude_mode = BP_FORT_SAVE_FOR_HP1; last.full_resets = 2; last.active_steps = 3; last.active_points = 30;
    last.carried_pool = 40;
    CHECK(bp_record_rank_value(&last, 0) == 300 + 250 + 400 + 100 && bp_record_rank_value(&last, 1) == 0);

    n = bp_ex_carry(&last, BP_CARRY_OFF);   /* ranks and mod data reset; BP is the game's own */
    { BpRecord zero; memset(&zero, 0, sizeof zero); CHECK(bp_record_equal(&n, &zero)); }

    n = bp_ex_carry(&last, BP_CARRY_KEEP);
    CHECK(n.defense[0] == 3 && n.defense[BP_FORTITUDE] == 2 && n.mastery[0] == 2 && n.mastery[5] == 1);
    CHECK(n.carried_defense[0] == 3 && n.carried_mastery[0] == 2 && n.carried_mastery[5] == 1);
    CHECK(n.fortitude_mode == BP_FORT_SAVE_FOR_HP1 && n.carried_pool == 40);
    CHECK(!n.full_resets && !n.active_steps && !n.active_points);   /* run data restarts */
    CHECK(bp_record_rank_value(&n, 1) == 1050);

    n = bp_ex_carry(&last, BP_CARRY_CONVERT);   /* never both: ranks gone, value in the pool */
    CHECK(!n.defense[0] && !n.mastery[0] && !n.carried_defense[0] && n.carried_pool == 40 + 1050);
    last.carried_pool = BP_MAX - 10;
    CHECK(bp_ex_carry(&last, BP_CARRY_CONVERT).carried_pool == BP_MAX);   /* capped */
    last.carried_pool = 40;

    /* Keep, then buy Heal rank 3 with this run's BP: refunding Heal splits 400 pool / 350 BP. */
    fresh(); game.rec = bp_ex_carry(&last, BP_CARRY_KEEP); game.rec.carried_pool = 0; game.bp = 1000;
    r = run("buy.line.0"); CHECK(!r.error && game.bp == 650 && game.rec.mastery[0] == 3 && game.rec.carried_mastery[0] == 2);
    r = run("reset.line.0");
    CHECK(!r.error && r.quote.bp_refund == 750 && r.quote.pool_refund == 400 && r.quote.junk_fee == 90);
    CHECK(game.bp == 1000 && game.rec.carried_pool == 400 && !game.rec.mastery[0] && !game.rec.carried_mastery[0]);
    /* Upgrade-only BP buys a rank but never a gun stat. */
    game.bp = 0;
    r = run("buy.def.1"); CHECK(!r.error && game.rec.carried_pool == 325 && game.bp == 0 && r.quote.pool_cost == 75);
    before = game; r = run("buy.gear.3.0"); CHECK(r.error == BP_ERR_BP); UNCHANGED(before);
    /* Full reset: every carried rank's share to the pool, the rest to BP. */
    fresh(); game.rec = bp_ex_carry(&last, BP_CARRY_KEEP); game.junk = 1000; game.capacity_steps = 0;
    r = run("reset.all");
    CHECK(!r.error && r.quote.pool_refund == 1050 && game.rec.carried_pool == 40 + 1050 && game.bp == 1000);
    CHECK(!game.rec.carried_defense[0] && !game.rec.carried_mastery[5]);
    /* Pool overflow is refused before any Junk is taken. */
    fresh(); game.rec = bp_ex_carry(&last, BP_CARRY_KEEP); game.rec.carried_pool = BP_MAX - 100;
    before = game; r = run("reset.def.0"); CHECK(r.error == BP_ERR_OVERFLOW && !spent_junk); UNCHANGED(before);

    /* Save section round trip; a carried count above its rank is refused. */
    n = bp_ex_carry(&last, BP_CARRY_KEEP);
    bp_carry_encode(&n, carry);
    { BpRecord back = n; memset(back.carried_defense, 0, sizeof back.carried_defense); memset(back.carried_mastery, 0, sizeof back.carried_mastery);
      CHECK(!bp_carry_decode(carry, &back) && bp_record_equal(&back, &n)); }
    { BpRecord bad = n; bad.defense[0] = 1; CHECK(bp_carry_decode(carry, &bad)); }
    { BpRecord other = n; other.carried_mastery[0] = 1; CHECK(!bp_record_equal(&other, &n)); }
}

int main(void) {
    fees(); requests(); purchases(); resets(); save_encoding(); battle_numbers(); catalog(); preset_lifecycle(); ex_carryover();
    puts("PASS: BP rules, atomic transactions, save encoding and live catalog");
    return 0;
}
