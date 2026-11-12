#include "bp_catalog.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

enum { MAX_PAGES = BP_UI_MAX_PAGES, MAX_ROWS = 640, MAX_LINES = 2560, MAX_PREVIEWS = 384,
       TEXT_BYTES = 96 * 1024, INTERN_BYTES = 16 * 1024 };

static BpUiPage pages[MAX_PAGES];
static BpUiRow rows[MAX_ROWS];
static BpUiLine lines[MAX_LINES];
static BpUiPreview previews[MAX_PREVIEWS];
static BpUiVisual visuals[BP_MAX_GEAR * 4 + 8];
static size_t nvisuals;
static BpUiTab tabs[6];
static BpUiCatalog catalog;
static char text[TEXT_BYTES], pool[INTERN_BYTES];
static size_t npages, nrows, nlines, npreviews, ntext, npool;
static int overflow;
static const BpSnapshot *S;
static BpPriceFn P;

/* ---- storage ---- */
static const char *vtxt(const char *fmt, va_list ap) {
    char *at = text + ntext;
    int n = vsnprintf(at, TEXT_BYTES - ntext, fmt, ap);
    if (n < 0 || (size_t)n + 1 > TEXT_BYTES - ntext) { overflow = 1; return ""; }
    ntext += (size_t)n + 1;
    return at;
}
static const char *txt(const char *fmt, ...) {
    va_list ap; const char *r;
    va_start(ap, fmt); r = vtxt(fmt, ap); va_end(ap);
    return r;
}
/* Append-only and never cleared: interned strings stay valid across rebuilds. */
static const char *intern(const char *fmt, ...) {
    char want[96];
    va_list ap;
    size_t at = 0, n;
    va_start(ap, fmt); vsnprintf(want, sizeof want, fmt, ap); va_end(ap);
    while (at < npool) {
        if (!strcmp(pool + at, want)) return pool + at;
        at += strlen(pool + at) + 1;
    }
    n = strlen(want) + 1;
    if (npool + n > INTERN_BYTES) { overflow = 1; return "overflow"; }
    memcpy(pool + npool, want, n);
    npool += n;
    return pool + npool - n;
}
static BpUiPage *page(const char *id, const char *title, const char *context, const char *wallet,
                      const char *tab, const char *subject) {
    BpUiPage *p;
    if (npages == MAX_PAGES) { overflow = 1; return &pages[MAX_PAGES - 1]; }
    p = &pages[npages++];
    memset(p, 0, sizeof *p);
    p->id = id; p->title = title; p->context = context; p->wallet = wallet;
    p->tab_id = tab; p->subject = subject;
    p->rows = &rows[nrows];
    return p;
}
static BpUiRow *row(BpUiPage *p, const char *id, const char *label, const char *value) {
    BpUiRow *r;
    if (nrows == MAX_ROWS) { overflow = 1; return &rows[MAX_ROWS - 1]; }
    r = &rows[nrows++];
    memset(r, 0, sizeof *r);
    r->id = id; r->label = label; r->value = value ? value : "";
    r->kind = BP_UI_ITEM; r->state = BP_UI_AVAILABLE; r->action = BP_UI_NONE;
    ++p->row_count;
    return r;
}
static void section(BpUiPage *p, const char *id, const char *label, const char *value) {
    row(p, id, label, value)->kind = BP_UI_SECTION;
}
static void line(BpUiRow *r, const char *fmt, ...) {
    va_list ap;
    if (nlines == MAX_LINES || r->detail_count == BP_UI_LINES) { overflow = 1; return; }
    if (!r->detail_count) r->details = &lines[nlines];
    else if (r->details + r->detail_count != &lines[nlines]) { overflow = 1; return; }
    va_start(ap, fmt); lines[nlines++].text = vtxt(fmt, ap); va_end(ap);
    ++r->detail_count;
}
static BpUiPreview *view(BpUiRow *r, const char *title) {
    BpUiPreview *v;
    if (npreviews == MAX_PREVIEWS) { overflow = 1; return &previews[MAX_PREVIEWS - 1]; }
    v = &previews[npreviews++];
    memset(v, 0, sizeof *v);
    v->title = title;
    r->preview = v;
    return v;
}
static void route(BpUiRow *r, const char *target) { r->action = BP_UI_ROUTE; r->target = target; r->value = "Open"; }
static void ask(BpUiRow *r, const char *target, const char *question) {
    r->action = BP_UI_ASK; r->target = target; r->question = question;
}
/* Quote errors become row states with an immediately readable reason. */
static void gate(BpUiRow *r, const BpQuote *q) {
    switch (q->error) {
    case BP_OK: return;
    case BP_ERR_BP: r->state = BP_UI_UNAFFORDABLE; r->reason = txt("Need %u more BP", q->bp_cost - S->bp); break;
    case BP_ERR_JUNK: r->state = BP_UI_UNAFFORDABLE; r->reason = txt("Need %u more Junk", q->junk_fee - S->junk); break;
    case BP_ERR_MAXED: r->state = BP_UI_MAXED; r->reason = bp_error_text(q->error); break;
    case BP_ERR_LOCKED: r->state = BP_UI_LOCKED; r->reason = bp_error_text(q->error); break;
    default: r->state = BP_UI_BLOCKED; r->reason = bp_error_text(q->error); break;
    }
    if (r->action == BP_UI_ASK) r->action = BP_UI_NONE;
}
static BpQuote quote(BpRequestKind kind, int a, int b) {
    BpRequest r = {kind, a, b};
    return bp_quote(S, &r, P);
}
static const char *bp_wallet(void) {
    return S->rec.carried_pool ? txt("BP %u +%u", S->bp, S->rec.carried_pool) : txt("BP %u", S->bp);
}
static const char *junk_wallet(void) { return S->economy ? txt("Junk %u", S->junk) : "Junk off"; }
static const char *upper(const char *s) {
    char buf[64]; size_t i;
    for (i = 0; s[i] && i + 1 < sizeof buf; ++i) buf[i] = (char)(s[i] >= 'a' && s[i] <= 'z' ? s[i] - 32 : s[i]);
    buf[i] = 0;
    return txt("%s", buf);
}
static void pinned_purchase(BpUiPreview *v, const BpQuote *q, unsigned price) {
    v->cost = txt("%u BP", price);
    v->remaining = q->error == BP_ERR_BP ? "Not enough" : txt("%u BP", q->bp_after);
}

/* ---- pages ---- */
static int stat_total(const BpGear *g, int s) { int v = g->base[s] + g->bonus[s]; return v > 999 ? 999 : v; }
static BpUiVisual *gear_visual(BpUiRow *r, const BpGear *g, BpUiVisualKind kind) {
    BpUiVisual *v;
    if (nvisuals == sizeof visuals / sizeof *visuals) { overflow = 1; return NULL; }
    v = &visuals[nvisuals++];
    memset(v, 0, sizeof *v);
    v->kind = kind; v->record = g->record; v->armor = g->armor; v->equipped = g->equipped;
    v->bp_after = kind == BP_UI_VISUAL_GEAR_ITEM ? (int)S->bp : -1;   /* item rows: BP now */
    for (int s = 0; s < 3; ++s) { v->total[s] = stat_total(g, s); v->base[s] = g->base[s]; v->bonus[s] = g->bonus[s]; }
    r->visual = v;
    return v;
}

static void gear_list(int armor) {
    const char *tab = armor ? "armor" : "guns";
    BpUiPage *p = page(intern(tab), armor ? "BONUS POINTS / ARMOR" : "BONUS POINTS / GUNS",
                       armor ? "Choose armor" : "Choose a weapon", bp_wallet(), tab, armor ? "Choose armor" : "Choose a weapon");
    int any = 0;
    section(p, intern("%s.heading", tab), armor ? "ARMOR" : "WEAPONS", "");
    for (int i = 0; i < S->gear_count; ++i) {
        const BpGear *g = &S->gear[i];
        BpUiRow *r;
        BpUiPreview *v;
        if (g->armor != armor) continue;
        any = 1;
        r = row(p, intern("gear.%d", g->record), g->name, NULL);
        route(r, intern("gear.%d", g->record));
        gear_visual(r, g, BP_UI_VISUAL_GEAR_ITEM);
        v = view(r, upper(g->name));
        v->before = txt("%s %d", bp_gear_stat_name(armor, 0), stat_total(g, 0));
        v->benefit = txt("%s %d", bp_gear_stat_name(armor, 1), stat_total(g, 1));
        v->note = txt("%s %d", bp_gear_stat_name(armor, 2), stat_total(g, 2));
        line(r, "Choose this %s, then a stat.", armor ? "armor" : "weapon");
        line(r, "%s%s.", g->equipped ? "Equipped" : "In the bag", armor ? "" : "; bonuses move with Tools");
        line(r, "Weapon and armor BP is never refunded.");
    }
    if (!any) {
        BpUiRow *r = row(p, intern("%s.none", tab), armor ? "No armor" : "No guns", "");
        r->state = BP_UI_BLOCKED;
        r->reason = armor ? "Carry armor in the bag to upgrade it" : "Carry a gun in the bag to upgrade it";
        line(r, "Only gear in Aya's bag is listed.");
    }
}

static void gear_page(const BpGear *g) {
    const char *tab = g->armor ? "armor" : "guns";
    BpUiPage *p = page(intern("gear.%d", g->record), g->armor ? "BONUS POINTS / ARMOR" : "BONUS POINTS / GUNS",
                       g->name, bp_wallet(), tab, g->name);
    section(p, intern("gear.%d.stats", g->record), "UPGRADE / BP", "");
    for (int s = 0; s < 3; ++s) {
        const char *name = bp_gear_stat_name(g->armor, s);
        BpQuote q = quote(BP_REQ_GEAR, g->record, s);
        BpUiRow *r = row(p, intern("gear.%d.%d", g->record, s), name, "100");
        BpUiPreview *v = view(r, upper(name));
        ask(r, intern("buy.gear.%d.%d", g->record, s), txt("Raise %s %s?", g->name, name));
        v->before = txt("%d", stat_total(g, s));
        v->after = txt("%d", stat_total(g, s) + 1 > 999 ? 999 : stat_total(g, s) + 1);
        v->benefit = txt("+1 %s", name);
        v->note = txt("Bonus %+d to %+d", g->bonus[s], g->bonus[s] + 1);
        pinned_purchase(v, &q, BP_STEP_PRICE);
        line(r, "%s %d to %d.", name, stat_total(g, s), stat_total(g, s) + 1);
        line(r, "Base %d, bonus %+d.", g->base[s], g->bonus[s]);
        line(r, "100 BP - %u BP remaining.", q.bp_after);
        line(r, "Tools move gear bonuses; they are never refunded as BP.");
        gate(r, &q);
        if (r->state == BP_UI_MAXED) r->value = "MAX";
        {
            BpUiVisual *gv = gear_visual(r, g, BP_UI_VISUAL_GEAR_STAT);
            if (gv) {
                gv->stat = s;
                gv->next_total = stat_total(g, s) + 1 > 999 ? 999 : stat_total(g, s) + 1;
                gv->next_bonus = g->bonus[s] + 1;
                gv->bp_after = q.error ? -1 : (int)q.bp_after;
            }
        }
    }
}

static void aya_stat_row(BpUiPage *p, int stat) {
    const BpAyaStat *a = stat == 5 ? &S->active : &S->capacity;
    const char *name = stat == 5 ? "Active Time" : "Item Capacity";
    BpQuote q = quote(BP_REQ_AYA, stat, 0);
    BpUiRow *r = row(p, intern("aya.%d", stat), name, txt("%d", a->shown));
    BpUiPreview *v = view(r, upper(name));
    ask(r, intern("buy.aya.%d", stat), txt("Raise %s?", name));
    if (stat == 5) {
        v->before = txt("Lv %d %d%%", a->level + 1, a->sub * 100 / 49);
        v->after = txt("Lv %d %d%%", a->next_level + 1, a->next_sub * 100 / 49);
        v->benefit = a->next_level > a->level ? "Next level reached" : "Toward next level";
        v->note = "Turn gauge speed and Quickdraw";
        line(r, "Active Time level %d (%d%%) to %d (%d%%).", a->level + 1, a->sub * 100 / 49, a->next_level + 1, a->next_sub * 100 / 49);
        line(r, "Fills the turn gauge faster; raises the Quickdraw chance.");
        line(r, "Steps bought on this screen refund in Wayne's full reset.");
    } else {
        v->before = txt("%d slots", a->shown);
        v->after = txt("%d slots", a->next_shown);
        v->benefit = a->next_shown > a->shown ? "One more bag slot" : "Toward the next slot";
        v->note = txt("Level %d to %d", a->level + 1, a->next_level + 1);
        line(r, "Bag %d slots to %d.", a->shown, a->next_shown);
        line(r, "Refunds only in Wayne's full reset.");
    }
    line(r, "100 BP - %u BP remaining.", q.bp_after);
    line(r, "The EX Game resets this stat (vanilla).");
    pinned_purchase(v, &q, BP_STEP_PRICE);
    gate(r, &q);
    if (nvisuals < sizeof visuals / sizeof *visuals) {
        BpUiVisual *av = &visuals[nvisuals++];
        memset(av, 0, sizeof *av);
        av->kind = BP_UI_VISUAL_AYA_STAT;
        av->stat = stat;
        av->record = -1;
        av->now = (BpUiAyaLevel){a->level + 1, a->sub, a->distance};
        av->next = (BpUiAyaLevel){a->next_level + 1, a->next_sub, a->next_distance};
        av->bp_after = q.error ? -1 : (int)q.bp_after;
        r->visual = av;
    } else overflow = 1;
}

static void defense_row(BpUiPage *p, int t) {
    const BpTrackDef *d = bp_defense_def(t);
    int rank = S->rec.defense[t];
    BpQuote q = quote(BP_REQ_DEFENSE, t, 0);
    BpUiRow *r = row(p, intern("def.%s", d->id), d->name, txt("%d/3", rank));
    BpUiPreview *v = view(r, upper(d->name));
    unsigned price = bp_track_price(t, rank + 1);
    ask(r, intern("buy.def.%d", t), txt("Raise %s to rank %d?", d->name, rank + 1));
    if (t < BP_RESIST_TRACKS) {
        int armor = S->armor_resist[t] ? bp_armor_chance(t, 0) : 0;
        int now = bp_effective_resist(armor, rank), next = bp_effective_resist(armor, rank + 1);
        v->before = txt("%d%%", now);
        v->after = rank < BP_MAX_RANK ? txt("%d%%", next) : NULL;
        v->benefit = t == 3 ? "Lock resistance" : txt("Rank %d to %d", rank, rank + 1);
        if (t == 3) {
            int slow = S->armor_resist[t] ? bp_armor_chance(t, 1) : 0;
            v->note = txt("Slow %d%% to %d%%", bp_effective_resist(slow, rank), bp_effective_resist(slow, rank + 1));
            line(r, "Lock %d%% to %d%%; slow %d%% to %d%%.", now, next, bp_effective_resist(slow, rank), bp_effective_resist(slow, rank + 1));
        } else {
            v->note = S->armor_resist[t] ? "With equipped armor" : "No armor protection";
            line(r, "%s resistance %d%% to %d%%.", d->name, now, next);
        }
        line(r, S->armor_resist[t] ? "Includes the equipped armor's resist mod." : "The equipped armor has no %s mod.", d->name);
        line(r, "Lowers the chance; does not shorten an ailment.");
    } else {
        static const char *const fort[4] = {"No protection", "1 charge, softens", "2 charges, both soften", "2 charges, first negates"};
        v->before = txt("Rank %d", rank);
        v->after = rank < BP_MAX_RANK ? txt("Rank %d", rank + 1) : NULL;
        v->benefit = rank < BP_MAX_RANK ? fort[rank + 1] : fort[3];
        v->note = "Halve HP and HP to 1 only";
        line(r, "Now: %s per battle.", fort[rank]);
        if (rank < BP_MAX_RANK) line(r, "Next: %s per battle.", fort[rank + 1]);
        line(r, "A boss counter: Giant Dogman, Sheeva, the Crab, the Ultimate Being.");
        line(r, "Charges refill each battle.");
    }
    if (rank < BP_MAX_RANK) line(r, "%u BP - %u BP remaining.", price, q.bp_after);
    line(r, "Refunds at Wayne's Workshop.");
    if (rank < BP_MAX_RANK) pinned_purchase(v, &q, price);
    gate(r, &q);
    if (r->state == BP_UI_MAXED) r->value = "MAX";
}

static void aya_page(void) {
    BpUiPage *p = page(intern("aya"), "BONUS POINTS / AYA", "Character and resistance", bp_wallet(), "aya", "Character and resistance");
    BpUiRow *r;
    section(p, intern("aya.character"), "CHARACTER", "");
    aya_stat_row(p, 5);
    aya_stat_row(p, 6);
    section(p, intern("aya.resistance"), "RESISTANCE", "RANK");
    for (int t = 0; t < BP_DEFENSE_TRACKS; ++t) defense_row(p, t);
    r = row(p, intern("fortitude.mode"), "Fortitude mode", NULL);
    route(r, intern("fortitude.settings"));
    r->value = S->rec.fortitude_mode ? "HP to 1" : "Both";
    {
        BpUiPreview *v = view(r, "PROTECTION MODE");
        v->benefit = S->rec.fortitude_mode ? "Save charges for HP to 1" : "Protect against both";
        v->note = "No BP cost";
    }
    line(r, "Choose which effects Fortitude charges protect against.");
}

static void fortitude_page(void) {
    BpUiPage *p = page(intern("fortitude.settings"), "FORTITUDE MODE", "Aya", bp_wallet(), NULL, NULL);
    static const char *const names[2] = {"Protect against both", "Save for HP to 1"};
    for (int m = 0; m < 2; ++m) {
        BpUiRow *r = row(p, intern("fortitude.%d", m), names[m], S->rec.fortitude_mode == m ? "Current" : "");
        r->action = BP_UI_SETTING; r->target = intern("set.fort.%d", m);
        line(r, m ? "Halve HP is ignored; charges wait for HP to 1." : "Charges protect against Halve HP and HP to 1.");
        line(r, "No BP cost. Change any time.");
    }
}

static void mastery_page(void) {
    BpUiPage *p = page(intern("mastery"), "BONUS POINTS / PE MASTERY", "Choose a power", bp_wallet(), "mastery", "Learned powers");
    for (int l = 0; l < BP_MASTERY_LINES; ++l) {
        const BpLineDef *d = bp_line_def(l);
        int rank = S->rec.mastery[l], learned = bp_line_learned(S, l);
        BpQuote q = quote(BP_REQ_MASTERY, l, 0);
        BpUiRow *r;
        BpUiPreview *v;
        if (!learned && !rank) {
            r = row(p, intern("line.%s", d->id), "???", "Locked");
            r->state = BP_UI_LOCKED; r->reason = "Learned later";
            v = view(r, "UNLEARNED");
            v->benefit = "Learn this power first";
            v->note = "Name stays hidden";
            line(r, "A power Aya has not learned yet.");
            continue;
        }
        r = row(p, intern("line.%s", d->id), d->name, txt("%d/%d", rank, d->ranks));
        v = view(r, upper(d->name));
        ask(r, intern("buy.line.%d", l), txt("Raise %s to rank %d?", d->name, rank + 1));
        v->before = txt("Rank %d", rank);
        v->after = rank < d->ranks ? txt("Rank %d", rank + 1) : NULL;
        v->benefit = rank < d->ranks ? "Next rank totals:" : "All ranks bought";
        v->note = rank < d->ranks ? d->rank_text[rank] : d->rank_text[d->ranks - 1];
        line(r, "Now: %s.", rank ? d->rank_text[rank - 1] : "no mastery");
        if (rank < d->ranks) line(r, "Next: %s.", d->rank_text[rank]);
        {
            int now = bp_line_cost(l, rank, S->pe_light, S->max_pe);
            int next = rank < d->ranks ? bp_line_cost(l, rank + 1, S->pe_light, S->max_pe) : now;
            if (now >= 0) line(r, "PE cost %d to %d%s.", now, next, S->pe_light ? " (PE Light)" : "");
        }
        line(r, "Choose this if you enjoy %s.", d->for_whom);
        line(r, "Pays off: %s.", d->pays_off);
        if (rank < d->ranks) line(r, "%u BP - %u BP remaining.", bp_line_price(l, rank + 1), q.bp_after);
        if (!learned) line(r, "Dormant until Aya learns it again.");
        if (rank < d->ranks) pinned_purchase(v, &q, bp_line_price(l, rank + 1));
        gate(r, &q);
        if (r->state == BP_UI_MAXED) r->value = "MAX";
    }
}

/* ---- Wayne's refund pages ---- */
static void refund_view(BpUiRow *r, const char *name, const BpQuote *q) {
    BpUiPreview *v = view(r, txt("RESET %s", upper(name)));
    v->before = txt("Rank %d", q->rank_before);
    v->after = "Rank 0";
    v->benefit = txt("Return %u BP", q->bp_refund);
    v->note = txt("BP %u to %u", S->bp, S->bp + q->bp_refund);
    if (q->error != BP_ERR_EMPTY && q->error != BP_ERR_NO_ECONOMY) {
        v->cost = txt("%u Junk", q->junk_fee);
        v->remaining = q->error == BP_ERR_JUNK ? "Not enough" : txt("%u Junk", q->junk_after);
    }
    line(r, "%s rank %d to 0. Return %u BP.", name, q->rank_before, q->bp_refund);
    if (q->bp_refund) line(r, "BP %u to %u. Junk %u to %u.", S->bp, S->bp + q->bp_refund, S->junk, S->junk - (q->junk_fee < S->junk ? q->junk_fee : S->junk));
    line(r, "100%% of the BP is returned. Fee: 50 Junk + 5 per 100 BP.");
    line(r, "Weapons and armor are excluded.");
}

static void refund_pages(void) {
    BpUiPage *p;
    BpUiRow *r;
    BpQuote all = quote(BP_REQ_RESET_ALL, 0, 0);
    const char *wallet = junk_wallet();

    p = page(intern("reset"), "WORKSHOP / RESET BP", "Weapon/armor BP excluded", wallet, NULL, NULL);
    r = row(p, intern("reset.one.open"), "Reset one upgrade", NULL);
    route(r, intern("reset.one"));
    line(r, "Refund one Resistance track or Mastery line.");
    line(r, "50 Junk + 5 Junk per 100 BP refunded.");
    line(r, "Weapons and armor are never refunded.");
    r = row(p, intern("reset.all.open"), "Reset all eligible", NULL);
    route(r, intern("reset.all"));
    line(r, "Refund every rank and Aya's two BP stats at once.");
    line(r, "%u Junk base + 5 per 100 BP; rises after each full reset.", bp_reset_fee_base(1, 0, S->rec.full_resets));
    if (!S->economy) {
        for (BpUiRow *x = (BpUiRow *)p->rows; x < p->rows + p->row_count; ++x) {
            x->state = BP_UI_BLOCKED; x->reason = "Needs the Junk economy"; x->action = BP_UI_NONE;
        }
    }

    p = page(intern("reset.one"), "WORKSHOP / BP REFUND", "Weapon/armor BP excluded", wallet, "refund.aya", "Resistance");
    section(p, intern("refund.character"), "CHARACTER", "");
    for (int stat = 5; stat <= 6; ++stat) {
        r = row(p, intern("refund.aya.%d", stat), stat == 5 ? "Active Time" : "Item Capacity", "Full reset");
        r->state = BP_UI_BLOCKED; r->reason = "Full reset only";
        {
            BpUiPreview *v = view(r, "FULL RESET ONLY");
            v->benefit = "Included in full reset";
            v->note = "Select Reset all";
        }
        line(r, "Aya's two stats are refunded only by Reset all eligible.");
    }
    section(p, intern("refund.resistance"), "RESISTANCE", "BP BACK");
    for (int t = 0; t < BP_DEFENSE_TRACKS; ++t) {
        const BpTrackDef *d = bp_defense_def(t);
        BpQuote q = quote(BP_REQ_RESET_DEFENSE, t, 0);
        r = row(p, intern("refund.def.%s", d->id), d->name, txt("%u", q.bp_refund));
        ask(r, intern("reset.def.%d", t), txt("Reset %s for %u Junk?", d->name, q.junk_fee));
        refund_view(r, d->name, &q);
        gate(r, &q);
    }

    r = row(p, intern("refund.all.open"), "Reset all eligible", "Open");
    route(r, intern("reset.all"));
    line(r, "Refund eligible Aya stats, Resistance and PE Mastery together.");
    line(r, "Weapons and armor are excluded. Preview the full fee before confirming.");

    p = page(intern("reset.mastery"), "WORKSHOP / BP REFUND", "Weapon/armor BP excluded", wallet, "refund.mastery", "PE Mastery");
    section(p, intern("refund.powers"), "PE MASTERY", "BP BACK");
    for (int l = 0; l < BP_MASTERY_LINES; ++l) {
        const BpLineDef *d = bp_line_def(l);
        BpQuote q = quote(BP_REQ_RESET_MASTERY, l, 0);
        if (!bp_line_learned(S, l) && !S->rec.mastery[l]) continue;
        r = row(p, intern("refund.line.%s", d->id), d->name, txt("%u", q.bp_refund));
        ask(r, intern("reset.line.%d", l), txt("Reset %s for %u Junk?", d->name, q.junk_fee));
        refund_view(r, d->name, &q);
        gate(r, &q);
    }
    if (p->row_count == 1) {
        r = row(p, intern("refund.line.none"), "No powers", "");
        r->state = BP_UI_BLOCKED; r->reason = "No Mastery to refund";
        line(r, "Mastery lines appear once Aya learns a power.");
    }

    r = row(p, intern("refund.all.open"), "Reset all eligible", "Open");
    route(r, intern("reset.all"));
    line(r, "Refund eligible Aya stats, Resistance and PE Mastery together.");
    line(r, "Weapons and armor are excluded. Preview the full fee before confirming.");

    p = page(intern("reset.all"), "RESET ALL ELIGIBLE", "Weapon/armor BP excluded", wallet, NULL, NULL);
    section(p, intern("refund.all.heading"), "SCOPE", "BP BACK");
    r = row(p, intern("refund.all.aya"), "Aya stats", txt("%u", all.refund_aya));
    line(r, "Active Time: %u steps bought on this screen.", all.active_steps);
    line(r, S->capacity_reset_ok ? "Item Capacity: %u steps (exact from the save)." : "Item Capacity: can't be measured; not refunded.", all.capacity_steps);
    line(r, "Active Time bought on the vanilla BP screen is not refunded.");
    if (all.capacity_steps) line(r, "Bag %d slots after the reset.", S->capacity_after_reset);
    r = row(p, intern("refund.all.defense"), "Resistance", txt("%u", all.refund_defense));
    line(r, "All four ailments and Fortitude go back to rank 0.");
    r = row(p, intern("refund.all.mastery"), "PE Mastery", txt("%u", all.refund_mastery));
    line(r, "Every Mastery line goes back to rank 0.");
    r = row(p, intern("refund.all.confirm"), "Reset all", txt("%u", all.bp_refund));
    ask(r, intern("reset.all"), txt("Reset all for %u Junk?", all.junk_fee));
    {
        BpUiPreview *v = view(r, "RESET ALL ELIGIBLE");
        v->benefit = txt("Return %u BP", all.bp_refund);
        v->note = txt("BP %u to %u", S->bp, S->bp + all.bp_refund);
        if (all.bp_refund && S->economy) {
            v->cost = txt("%u Junk", all.junk_fee);
            v->remaining = all.error == BP_ERR_JUNK ? "Not enough" : txt("%u Junk", all.junk_after);
        }
    }
    line(r, "Aya stats %u, Resistance %u, Mastery %u BP.", all.refund_aya, all.refund_defense, all.refund_mastery);
    if (all.bp_refund) line(r, "BP %u to %u. Cost %u Junk.", S->bp, S->bp + all.bp_refund, all.junk_fee);
    line(r, "Next full reset base: %u Junk plus the refund fee.", bp_reset_fee_base(1, 0, S->rec.full_resets + 1u));
    if (all.error == BP_ERR_BAG) line(r, "%d items sit in bag slots the reset would remove.", S->bag_items_beyond);
    line(r, "Weapons and armor are excluded.");
    gate(r, &all);
}

const BpUiCatalog *bp_catalog_build(const BpSnapshot *s, BpPriceFn price) {
    static const BpSnapshot empty;
    S = s ? s : &empty; P = price;
    npages = nrows = nlines = npreviews = ntext = nvisuals = 0; overflow = 0;
    gear_list(0);
    gear_list(1);
    aya_page();
    mastery_page();
    fortitude_page();
    refund_pages();
    for (int i = 0; i < S->gear_count; ++i) gear_page(&S->gear[i]);
    tabs[0] = (BpUiTab){intern("guns"), "GUNS", intern("guns"), NULL};
    tabs[1] = (BpUiTab){intern("armor"), "ARMOR", intern("armor"), NULL};
    tabs[2] = (BpUiTab){intern("aya"), "AYA", intern("aya"), NULL};
    tabs[3] = (BpUiTab){intern("mastery"), "PE MASTERY", intern("mastery"), NULL};
    tabs[4] = (BpUiTab){intern("refund.aya"), "AYA", intern("reset.one"), "refund"};
    tabs[5] = (BpUiTab){intern("refund.mastery"), "PE MASTERY", intern("reset.mastery"), "refund"};
    catalog.pages = pages; catalog.page_count = npages;
    catalog.tabs = tabs; catalog.tab_count = 6;
    catalog.badge = "LIVE";
    return &catalog;
}

BpCatalogUsage bp_catalog_usage(void) {
    BpCatalogUsage u = {npages, nrows, nlines, npreviews, ntext, npool, overflow};
    return u;
}
