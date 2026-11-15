/* The public "bp" service (host/pe_services.h), mod pauses and the "bp.changed" / "bp.effect"
 * events. Pure over BpApiHost: no game globals, so the unit tests drive it with fake game ops.
 * Every commit, the BP screens' included, goes through bp_api_execute. */
#include "pe_bp_internal.h"
#include <stdio.h>
#include <string.h>

/* The public numbers are the rules' own: keep them in step. */
_Static_assert(PE_BP_DEFENSE_COUNT == BP_DEFENSE_TRACKS && PE_BP_MASTERY_COUNT == BP_MASTERY_LINES, "bp counts");
_Static_assert(PE_BP_FEATURE_RESISTANCE == BP_FEATURE_RESISTANCE && PE_BP_FEATURE_FORTITUDE == BP_FEATURE_FORTITUDE &&
               PE_BP_FEATURE_MASTERY == BP_FEATURE_MASTERY && PE_BP_FEATURE_RESPEC == BP_FEATURE_RESPEC, "bp features");
_Static_assert(PE_BP_OK == BP_OK && PE_BP_ERR_NOT_LOADED == BP_ERR_NOT_LOADED && PE_BP_ERR_BP == BP_ERR_BP &&
               PE_BP_ERR_OVERFLOW == BP_ERR_OVERFLOW && PE_BP_ERR_STORE == BP_ERR_STORE &&
               PE_BP_ERR_INACTIVE == BP_ERR_INACTIVE, "bp errors");
_Static_assert(PE_BP_CARRY_OFF == BP_CARRY_OFF && PE_BP_CARRY_KEEP == BP_CARRY_KEEP &&
               PE_BP_CARRY_CONVERT == BP_CARRY_CONVERT, "bp carry modes");

enum { MAX_PAUSES = 8, PAUSE_ID = 32 };
static const BpApiHost *s_host;
static struct { char id[PAUSE_ID]; unsigned features; } s_pauses[MAX_PAUSES];

void bp_api_bind(const BpApiHost *host) { s_host = host; }
#ifdef PE_BP_UNIT_TESTS
void bp_api_reset_for_tests(void) { memset(s_pauses, 0, sizeof s_pauses); }
#endif

static void say(const char *line) { if (s_host && s_host->log) s_host->log(line); }
static unsigned paused(void) {
    unsigned m = 0;
    for (int i = 0; i < MAX_PAUSES; ++i) m |= s_pauses[i].features;
    return m;
}
unsigned bp_api_features(void) {
    unsigned flags = s_host && s_host->flags ? s_host->flags() : 0;
    return flags & ~paused() & BP_FEATURE_ALL;
}
static int readable(void) { return s_host && s_host->readable && s_host->record && s_host->readable(); }
static const BpRecord *record(void) {
    static const BpRecord none;
    return readable() ? s_host->record() : &none;
}
static unsigned cur_bp(void) { return s_host && s_host->bp ? s_host->bp() : 0; }

static void changed(const char *why, const char *by, unsigned bp_before, unsigned pool_before) {
    PeBpChanged e;
    e.why = why; e.by = by;
    e.bp_before = bp_before; e.bp_after = cur_bp();
    e.pool_before = pool_before; e.pool_after = record()->carried_pool;
    if (s_host && s_host->emit) s_host->emit("bp.changed", &e);
}
void bp_api_notify(const char *why, unsigned bp_before, unsigned pool_before) { changed(why, NULL, bp_before, pool_before); }
void bp_api_effect(int kind, int index, int rank, const char *what) {
    PeBpEffect e;
    e.kind = kind; e.index = index; e.rank = rank; e.what = what;
    if (s_host && s_host->emit) s_host->emit("bp.effect", &e);
}

BpResult bp_api_execute(const char *request, const char *by) {
    BpResult r;
    unsigned bp_before = cur_bp(), pool_before = record()->carried_pool;
    char id[48];
    if (!s_host || !s_host->ops) { memset(&r, 0, sizeof r); r.error = BP_ERR_NOT_LOADED; return r; }
    r = bp_execute(s_host->ops, request);
    bp_request_format(&r.request, id, sizeof id);
    if (s_host->log) {
        char line[160];
        snprintf(line, sizeof line, "%s%s%s: %s bp=%u refund=%u junk=%u", id, by ? " by " : "", by ? by : "",
                 r.error ? bp_error_text(r.error) : "ok", r.quote.bp_cost, r.quote.bp_refund, r.quote.junk_fee);
        s_host->log(line);
    }
    if (!r.error) changed(request, by, bp_before, pool_before);
    return r;
}

/* ---- the "bp" v1 table ---- */
static int valid(int kind, int index) {
    return (kind == PE_BP_DEFENSE && index >= 0 && index < BP_DEFENSE_TRACKS) ||
           (kind == PE_BP_MASTERY && index >= 0 && index < BP_MASTERY_LINES);
}
static unsigned feature_of(int kind, int index) {
    return kind == PE_BP_DEFENSE ? bp_track_feature(index) : BP_FEATURE_MASTERY;
}
/* Copies at most the caller's struct_size (0 counts as this build's size), keeping it. */
static void put(void *out, const void *full, size_t size) {
    uint32_t want = *(const uint32_t *)out;
    size_t n = want && want < size ? want : size;
    memcpy(out, full, n);
    *(uint32_t *)out = (uint32_t)n;
}

static int api_status(PeBpStatus *out) {
    PeBpStatus s;
    if (!out) return 0;
    memset(&s, 0, sizeof s);
    s.struct_size = sizeof s;
    s.loaded = (uint8_t)readable();
    s.carry_mode = (uint8_t)(s_host && s_host->carry_mode ? s_host->carry_mode() : 0);
    s.features = bp_api_features();
    s.paused = paused();
    if (s.loaded) {
        s.full_resets = record()->full_resets;
        s.bp = cur_bp();
        s.upgrade_pool = record()->carried_pool;
    }
    put(out, &s, sizeof s);
    return s.loaded;
}
static int api_rank(int kind, int index) {
    if (!valid(kind, index)) return -1;
    return kind == PE_BP_DEFENSE ? record()->defense[index] : record()->mastery[index];
}
static int api_active_rank(int kind, int index) {
    int r = api_rank(kind, index);
    return r > 0 && (bp_api_features() & feature_of(kind, index)) ? r : r < 0 ? -1 : 0;
}
static int api_carried(int kind, int index) {
    const BpRecord *r = record();
    if (!valid(kind, index)) return -1;
    if (kind == PE_BP_DEFENSE) return r->carried_defense[index] < r->defense[index] ? r->carried_defense[index] : r->defense[index];
    return r->carried_mastery[index] < r->mastery[index] ? r->carried_mastery[index] : r->mastery[index];
}
static int api_max_rank(int kind, int index) {
    if (!valid(kind, index)) return 0;
    return kind == PE_BP_DEFENSE ? BP_MAX_RANK : bp_line_def(index)->ranks;
}
static const char *api_key(int kind, int index) {
    if (!valid(kind, index)) return NULL;
    return kind == PE_BP_DEFENSE ? bp_defense_def(index)->id : bp_line_def(index)->id;
}
static const char *api_name(int kind, int index) {
    if (!valid(kind, index)) return NULL;
    return kind == PE_BP_DEFENSE ? bp_defense_def(index)->name : bp_line_def(index)->name;
}
static uint32_t api_price(int kind, int index, int rank) {
    if (!valid(kind, index)) return 0;
    return kind == PE_BP_DEFENSE ? bp_track_price(index, rank) : bp_line_price(index, rank);
}
static void to_public(const BpResult *r, PeBpQuote *out) {
    PeBpQuote q;
    if (!out) return;
    memset(&q, 0, sizeof q);
    q.struct_size = sizeof q;
    q.error = r->error;
    q.bp_cost = r->quote.bp_cost; q.pool_cost = r->quote.pool_cost;
    q.bp_refund = r->quote.bp_refund; q.pool_refund = r->quote.pool_refund;
    q.junk_fee = r->quote.junk_fee;
    q.bp_after = r->quote.bp_after; q.junk_after = r->quote.junk_after;
    q.rank_before = r->quote.rank_before; q.rank_after = r->quote.rank_after;
    put(out, &q, sizeof q);
}
static int api_quote(const char *request, PeBpQuote *out) {
    BpResult r;
    if (!s_host || !s_host->ops) { memset(&r, 0, sizeof r); r.error = BP_ERR_NOT_LOADED; }
    else r = bp_preview(s_host->ops, request);
    to_public(&r, out);
    return r.error;
}
static int api_execute(const char *request, const char *by, PeBpQuote *out) {
    BpResult r = bp_api_execute(request, by);
    to_public(&r, out);
    return r.error;
}
static const char *api_error_text(int error) { return bp_error_text((BpError)error); }
static int api_grant_pool(uint32_t bp, const char *by) {
    BpRecord next;
    unsigned bp_before = cur_bp(), pool_before;
    if (!readable() || !s_host->ops) return BP_ERR_NOT_LOADED;
    next = *record();
    pool_before = next.carried_pool;
    if (!bp) return BP_ERR_UNCHANGED;
    if ((unsigned long)next.carried_pool + bp > BP_MAX) return BP_ERR_OVERFLOW;
    if (!s_host->ops->store_ready || !s_host->ops->store_ready(s_host->ops->user)) return BP_ERR_STORE;
    next.carried_pool += bp;
    s_host->ops->set_record(&next, s_host->ops->user);
    {
        char line[128];
        snprintf(line, sizeof line, "grant%s%s: +%u upgrade-only BP (pool %u -> %u)", by ? " by " : "", by ? by : "",
                 (unsigned)bp, pool_before, (unsigned)next.carried_pool);
        say(line);
    }
    changed("grant", by, bp_before, pool_before);
    return BP_OK;
}
static int api_pause(const char *mod_id, uint32_t features) {
    int free_slot = -1;
    unsigned before = paused();
    if (!mod_id || !*mod_id || strlen(mod_id) >= PAUSE_ID) return -1;
    features &= BP_FEATURE_ALL;
    for (int i = 0; i < MAX_PAUSES; ++i) {
        if (!strcmp(s_pauses[i].id, mod_id)) {
            s_pauses[i].features = features;
            if (!features) s_pauses[i].id[0] = 0;
            free_slot = -2;
            break;
        }
        if (free_slot == -1 && !s_pauses[i].id[0]) free_slot = i;
    }
    if (free_slot >= 0 && features) {
        snprintf(s_pauses[free_slot].id, PAUSE_ID, "%s", mod_id);
        s_pauses[free_slot].features = features;
    } else if (free_slot == -1 && features) return -1;   /* table full */
    if (paused() != before) {
        char line[96];
        snprintf(line, sizeof line, "pause by %s: features %u (paused now %u)", mod_id, (unsigned)features, paused());
        say(line);
    }
    return 0;
}

static const PeBpApi s_public = {
    sizeof(PeBpApi), api_status, api_rank, api_active_rank, api_carried, api_max_rank, api_key, api_name,
    api_price, api_quote, api_execute, api_error_text, api_grant_pool, api_pause
};
const PeBpApi *bp_api_public(void) { return &s_public; }
