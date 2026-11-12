#include "bp_txn.h"
#include <stdio.h>
#include <string.h>

BpResult bp_execute(const BpGameOps *ops, const char *id) {
    BpResult res;
    BpSnapshot before, after;
    memset(&res, 0, sizeof res);
    if (!ops || !ops->read || !bp_request_parse(id, &res.request)) { res.error = BP_ERR_UNKNOWN; return res; }
    if (!ops->read(&before, ops->user)) { res.error = BP_ERR_NOT_LOADED; return res; }
    res.quote = bp_quote(&before, &res.request, ops->price);
    if (res.quote.error) { res.error = res.quote.error; return res; }
    after = before;
    if ((res.error = bp_apply(&after, &res.request, &res.quote)) != BP_OK) return res;

    /* Refusable steps. */
    if (!bp_record_equal(&before.rec, &after.rec) &&
        (!ops->store_ready || !ops->store_ready(ops->user))) { res.error = BP_ERR_STORE; return res; }
    if (res.quote.junk_fee && (!ops->junk_spend || !ops->junk_spend(res.quote.junk_fee, ops->user))) {
        res.error = BP_ERR_JUNK; return res;
    }

    /* Infallible writes. */
    {
        int stats = 0, gear = -1;
        if (before.bp != after.bp) ops->set_bp(after.bp, ops->user);
        if (res.request.kind == BP_REQ_GEAR) {
            for (int i = 0; i < after.gear_count; ++i)
                if (after.gear[i].record == res.request.a) {
                    ops->set_gear_bonus(res.request.a, res.request.b, after.gear[i].bonus[res.request.b], ops->user);
                    gear = res.request.a;
                }
        }
        if (before.active.value != after.active.value) { ops->set_aya_stat(5, after.active.value, ops->user); stats = 1; }
        if (before.capacity.value != after.capacity.value) { ops->set_aya_stat(6, after.capacity.value, ops->user); stats = 1; }
        if (!bp_record_equal(&before.rec, &after.rec)) ops->set_record(&after.rec, ops->user);
        if (ops->after) ops->after(stats, gear, ops->user);
    }
    return res;
}

void bp_result_text(const BpResult *r, const BpSnapshot *s, char *out, size_t n) {
    const BpRequest *q = &r->request;
    if (r->error) { snprintf(out, n, "%s - nothing changed", bp_error_text(r->error)); return; }
    switch (q->kind) {
    case BP_REQ_GEAR: case BP_REQ_AYA:
        snprintf(out, n, "Upgraded. %u BP left", s ? s->bp : r->quote.bp_after); break;
    case BP_REQ_DEFENSE:
        snprintf(out, n, "%s rank %d. %u BP left", bp_defense_def(q->a)->name, r->quote.rank_after, r->quote.bp_after); break;
    case BP_REQ_MASTERY:
        snprintf(out, n, "%s rank %d. %u BP left", bp_line_def(q->a)->name, r->quote.rank_after, r->quote.bp_after); break;
    case BP_REQ_RESET_DEFENSE: case BP_REQ_RESET_MASTERY: case BP_REQ_RESET_ALL:
        snprintf(out, n, "Refunded %u BP for %u Junk", r->quote.bp_refund, r->quote.junk_fee); break;
    case BP_REQ_FORT_MODE: snprintf(out, n, "Fortitude setting changed"); break;
    default: snprintf(out, n, "Done"); break;
    }
}
