/* Requests, quotes and the write plan (bp_quote.h). */
#include "bp_quote.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int bp_line_learned(const BpSnapshot *s, int l) { const BpLineDef *d = bp_line_def(l); return d && (s->learned & d->powers); }

const char *bp_error_text(BpError e) {
    switch (e) {
    case BP_OK: return "Done";
    case BP_ERR_NOT_LOADED: return "Not available right now";
    case BP_ERR_UNKNOWN: return "That upgrade no longer exists";
    case BP_ERR_MAXED: return "Maximum rank";
    case BP_ERR_LOCKED: return "Power not learned yet";
    case BP_ERR_BP: return "Not enough BP";
    case BP_ERR_JUNK: return "Not enough Junk";
    case BP_ERR_OVERFLOW: return "BP would pass 99,999";
    case BP_ERR_EMPTY: return "No BP to refund";
    case BP_ERR_NO_ECONOMY: return "Needs the Junk economy";
    case BP_ERR_BAG: return "Empty the last bag slots first";
    case BP_ERR_UNCHANGED: return "Already set";
    case BP_ERR_INACTIVE: return "Disabled - investment retained";
    case BP_ERR_STORE: return "BP upgrades can't be saved here";
    }
    return "Unavailable";
}

static int parse_int(const char **p, int *out) {
    char *end;
    long v = strtol(*p, &end, 10);
    if (end == *p || v < 0 || v > 1000) return 0;
    *out = (int)v; *p = end; return 1;
}
int bp_request_parse(const char *id, BpRequest *r) {
    const char *p;
    memset(r, 0, sizeof *r);
    if (!id) return 0;
    if (!strcmp(id, "reset.all")) { r->kind = BP_REQ_RESET_ALL; return 1; }
#define PREFIX(s, k) (!strncmp(id, s, sizeof s - 1) && (p = id + sizeof s - 1, r->kind = k, 1))
    if (PREFIX("buy.gear.", BP_REQ_GEAR))
        return parse_int(&p, &r->a) && *p++ == '.' && parse_int(&p, &r->b) && !*p && r->b < 3;
    if (PREFIX("buy.aya.", BP_REQ_AYA)) return parse_int(&p, &r->a) && !*p && (r->a == 5 || r->a == 6);
    if (PREFIX("buy.def.", BP_REQ_DEFENSE)) return parse_int(&p, &r->a) && !*p && r->a < BP_DEFENSE_TRACKS;
    if (PREFIX("buy.line.", BP_REQ_MASTERY)) return parse_int(&p, &r->a) && !*p && r->a < BP_MASTERY_LINES;
    if (PREFIX("reset.def.", BP_REQ_RESET_DEFENSE)) return parse_int(&p, &r->a) && !*p && r->a < BP_DEFENSE_TRACKS;
    if (PREFIX("reset.line.", BP_REQ_RESET_MASTERY)) return parse_int(&p, &r->a) && !*p && r->a < BP_MASTERY_LINES;
    if (PREFIX("set.fort.", BP_REQ_FORT_MODE)) return parse_int(&p, &r->a) && !*p && r->a <= BP_FORT_SAVE_FOR_HP1;
#undef PREFIX
    r->kind = BP_REQ_NONE;
    return 0;
}
void bp_request_format(const BpRequest *r, char *out, size_t n) {
    switch (r->kind) {
    case BP_REQ_GEAR: snprintf(out, n, "buy.gear.%d.%d", r->a, r->b); break;
    case BP_REQ_AYA: snprintf(out, n, "buy.aya.%d", r->a); break;
    case BP_REQ_DEFENSE: snprintf(out, n, "buy.def.%d", r->a); break;
    case BP_REQ_MASTERY: snprintf(out, n, "buy.line.%d", r->a); break;
    case BP_REQ_RESET_DEFENSE: snprintf(out, n, "reset.def.%d", r->a); break;
    case BP_REQ_RESET_MASTERY: snprintf(out, n, "reset.line.%d", r->a); break;
    case BP_REQ_RESET_ALL: snprintf(out, n, "reset.all"); break;
    case BP_REQ_FORT_MODE: snprintf(out, n, "set.fort.%d", r->a); break;
    default: snprintf(out, n, "none"); break;
    }
}

static unsigned hundreds_up(unsigned bp) { return (bp + 99u) / 100u; }
unsigned bp_reset_fee_base(int full, unsigned refund, unsigned done) {
    unsigned base = BP_TRACK_RESET_BASE;
    if (full) {
        base = BP_FULL_RESET_BASE + BP_FULL_RESET_STEP * done;
        if (base > BP_FULL_RESET_CAP) base = BP_FULL_RESET_CAP;
    }
    return base + BP_RESET_FEE_PER_100 * hundreds_up(refund);
}

static const BpGear *find_gear(const BpSnapshot *s, int record) {
    for (int i = 0; i < s->gear_count; ++i) if (s->gear[i].record == record) return &s->gear[i];
    return NULL;
}
static void charge(const BpSnapshot *s, BpQuote *q, unsigned price, int upgrade_only) {
    unsigned pool = upgrade_only ? s->rec.carried_pool : 0;
    q->pool_cost = pool < price ? pool : price;
    q->bp_cost = price - q->pool_cost;
    if (q->bp_cost > s->bp) q->error = BP_ERR_BP;
    q->bp_after = s->bp - (q->error ? 0 : q->bp_cost);
    q->junk_after = s->junk;
}
static void refund(const BpSnapshot *s, BpQuote *q, int full, BpPriceFn price) {
    unsigned total = q->bp_refund;
    q->bp_after = s->bp; q->junk_after = s->junk;
    if (!total) { q->error = BP_ERR_EMPTY; return; }
    if (!s->economy) { q->error = BP_ERR_NO_ECONOMY; return; }
    q->junk_fee_base = bp_reset_fee_base(full, total, s->rec.full_resets);
    q->junk_fee = price ? price(q->junk_fee_base) : q->junk_fee_base;
    if (full) q->next_full_fee_base = bp_reset_fee_base(1, 0, s->rec.full_resets + 1u);
    /* Carried ranks refund into the upgrade-only pool (BP-Q19); the rest into BP. */
    if ((unsigned long)s->bp + (total - q->pool_refund) > BP_MAX ||
        (unsigned long)s->rec.carried_pool + q->pool_refund > BP_MAX) { q->error = BP_ERR_OVERFLOW; return; }
    if (q->junk_fee > s->junk) { q->error = BP_ERR_JUNK; return; }
    q->bp_after = s->bp + (total - q->pool_refund);
    q->junk_after = s->junk - q->junk_fee;
}

BpQuote bp_quote(const BpSnapshot *s, const BpRequest *r, BpPriceFn price) {
    BpQuote q;
    memset(&q, 0, sizeof q);
    q.bp_after = s ? s->bp : 0; q.junk_after = s ? s->junk : 0;
    if (!s || !s->loaded) { q.error = BP_ERR_NOT_LOADED; return q; }
    if((r->kind==BP_REQ_DEFENSE && !(s->features&bp_track_feature(r->a))) ||
       (r->kind==BP_REQ_MASTERY && !(s->features&BP_FEATURE_MASTERY)) ||
       (r->kind==BP_REQ_FORT_MODE && !(s->features&BP_FEATURE_FORTITUDE)) ||
       ((r->kind==BP_REQ_RESET_DEFENSE||r->kind==BP_REQ_RESET_MASTERY||r->kind==BP_REQ_RESET_ALL) && !(s->features&BP_FEATURE_RESPEC))) {q.error=BP_ERR_INACTIVE;return q;}
    switch (r->kind) {
    case BP_REQ_GEAR: {
        const BpGear *g = find_gear(s, r->a);
        if (!g) { q.error = BP_ERR_UNKNOWN; return q; }
        if (g->bonus[r->b] >= BP_GEAR_BONUS_MAX) { q.error = BP_ERR_MAXED; return q; }
        charge(s, &q, BP_STEP_PRICE, 0);
        return q;
    }
    case BP_REQ_AYA: {
        const BpAyaStat *a = r->a == 5 ? &s->active : &s->capacity;
        if (a->maxed) { q.error = BP_ERR_MAXED; return q; }
        charge(s, &q, BP_STEP_PRICE, 0);
        return q;
    }
    case BP_REQ_DEFENSE: {
        int rank = s->rec.defense[r->a];
        q.rank_before = rank; q.rank_after = rank + 1;
        if (rank >= BP_MAX_RANK) { q.error = BP_ERR_MAXED; return q; }
        charge(s, &q, bp_track_price(r->a, rank + 1), 1);
        return q;
    }
    case BP_REQ_MASTERY: {
        int rank = s->rec.mastery[r->a];
        q.rank_before = rank; q.rank_after = rank + 1;
        if (!bp_line_learned(s, r->a)) { q.error = BP_ERR_LOCKED; return q; }
        if (rank >= bp_line_def(r->a)->ranks) { q.error = BP_ERR_MAXED; return q; }
        charge(s, &q, bp_line_price(r->a, rank + 1), 1);
        return q;
    }
    case BP_REQ_RESET_DEFENSE:
        q.rank_before = s->rec.defense[r->a];
        q.bp_refund = q.refund_defense = bp_track_spent(r->a, q.rank_before);
        q.pool_refund = bp_carried_track_value(&s->rec, r->a);
        refund(s, &q, 0, price);
        return q;
    case BP_REQ_RESET_MASTERY:
        q.rank_before = s->rec.mastery[r->a];
        q.bp_refund = q.refund_mastery = bp_line_spent(r->a, q.rank_before);
        q.pool_refund = bp_carried_line_value(&s->rec, r->a);
        refund(s, &q, 0, price);
        return q;
    case BP_REQ_RESET_ALL:
        for (int t = 0; t < BP_DEFENSE_TRACKS; ++t) q.refund_defense += bp_track_spent(t, s->rec.defense[t]);
        for (int l = 0; l < BP_MASTERY_LINES; ++l) q.refund_mastery += bp_line_spent(l, s->rec.mastery[l]);
        q.pool_refund = bp_record_rank_value(&s->rec, 1);
        q.active_steps = s->rec.active_steps;
        q.capacity_steps = s->capacity_reset_ok ? s->capacity_steps : 0;
        q.refund_aya = BP_STEP_PRICE * (q.active_steps + q.capacity_steps);
        q.bp_refund = q.refund_aya + q.refund_defense + q.refund_mastery;
        refund(s, &q, 1, price);
        if (!q.error && q.capacity_steps && s->bag_items_beyond) q.error = BP_ERR_BAG;
        return q;
    case BP_REQ_FORT_MODE:
        if (s->rec.fortitude_mode == r->a) q.error = BP_ERR_UNCHANGED;
        return q;
    default:
        q.error = BP_ERR_UNKNOWN;
        return q;
    }
}

BpError bp_apply(BpSnapshot *s, const BpRequest *r, const BpQuote *q) {
    if (q->error) return q->error;
    switch (r->kind) {
    case BP_REQ_GEAR: {
        BpGear *g = (BpGear *)find_gear(s, r->a);
        if (!g) return BP_ERR_UNKNOWN;
        ++g->bonus[r->b];
        break;
    }
    case BP_REQ_AYA:
        if (r->a == 5) {
            s->rec.active_points = (uint16_t)(s->rec.active_points + (s->active.next_value - s->active.value));
            ++s->rec.active_steps;
            s->active.value = s->active.next_value;
        } else s->capacity.value = s->capacity.next_value;
        break;
    case BP_REQ_DEFENSE: ++s->rec.defense[r->a]; break;
    case BP_REQ_MASTERY: ++s->rec.mastery[r->a]; break;
    case BP_REQ_RESET_DEFENSE: s->rec.defense[r->a] = s->rec.carried_defense[r->a] = 0; break;
    case BP_REQ_RESET_MASTERY: s->rec.mastery[r->a] = s->rec.carried_mastery[r->a] = 0; break;
    case BP_REQ_RESET_ALL:
        memset(s->rec.defense, 0, sizeof s->rec.defense);
        memset(s->rec.mastery, 0, sizeof s->rec.mastery);
        memset(s->rec.carried_defense, 0, sizeof s->rec.carried_defense);
        memset(s->rec.carried_mastery, 0, sizeof s->rec.carried_mastery);
        s->active.value -= s->rec.active_points;
        if (q->capacity_steps) s->capacity.value = s->capacity_base;
        s->rec.active_points = s->rec.active_steps = 0;
        if (s->rec.full_resets < 255) ++s->rec.full_resets;
        break;
    case BP_REQ_FORT_MODE: s->rec.fortitude_mode = (uint8_t)r->a; break;
    default: return BP_ERR_UNKNOWN;
    }
    s->rec.carried_pool = s->rec.carried_pool - q->pool_cost + q->pool_refund;
    s->bp = q->bp_after;
    s->junk = q->junk_after;
    return BP_OK;
}
