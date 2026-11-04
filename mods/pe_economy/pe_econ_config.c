#include "pe_econ_config.h"

#include <string.h>

_Static_assert(sizeof(PeEconConfig) == 78, "no padding: configurations compare by bytes");

const char *pe_econ_reason(PeEconResult r) {
    switch (r) {
    case PE_ECON_OK: return "OK.";
    case PE_ECON_BAD_ENGINE: return "Unknown reward engine.";
    case PE_ECON_BAD_ROUTE: return "Unknown Junk routing.";
    case PE_ECON_BAD_FLAGS: return "Unknown setting switched on.";
    case PE_ECON_BAD_PRICE: return "Base price must be 75, 100 or 125.";
    case PE_ECON_BAD_CHANCE: return "A chance is above 100%.";
    case PE_ECON_BUNDLE_CHANCES: return "Super and Duper Junk chances add up to more than 100%.";
    case PE_ECON_ROUTE_NEEDS_COUNTER: return "Extra income, empty-steal Junk and redemption need native Junk routed to the counter.";
    case PE_ECON_REDEEM_ZERO: return "Redemption needs a value above 0.";
    case PE_ECON_STEAL_ZERO: return "Empty-steal Junk needs an amount above 0.";
    case PE_ECON_PERFORMANCE_NO_INCOME: return "The performance bonus needs extra income on.";
    case PE_ECON_NEVER_PAYS: return "A reward is on but can never pay. Raise its amount or chance, or switch it off.";
    case PE_ECON_BAD_ALGORITHM: return "These settings need a reward algorithm this version doesn't have.";
    case PE_ECON_BAD_KEY: return "The preset key is malformed.";
    case PE_ECON_BAD_PRESET: return "This preset can't be used.";
    case PE_ECON_BAD_STATE: return "The saved Economy settings are damaged.";
    case PE_ECON_NOT_FREE_FIELD: return "Settings can only change in free field.";
    case PE_ECON_STORE_REFUSED: return "This save's companion can't hold the settings. Nothing changed.";
    }
    return "Unknown result.";
}

static int price_ok(unsigned p) { return p == 75 || p == 100 || p == 125; }

static int key_ok(const char key[48]) {
    const char *end = memchr(key, 0, 48);
    if (!end) return 0;
    for (const char *p = end; p < key + 48; ++p) if (*p) return 0;
    if (end == key) return 1;   /* Custom */
    for (const char *p = key; p < end; ++p) {
        const char ch = *p;
        if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_' || ch == '-' || ch == '.' || ch == '/'))
            return 0;
    }
    /* Registry providers use owner/name; retain namespace.name for existing saves
     * and the built-in pe.* presets. Both parts must be present. */
    const char *separator = memchr(key, '/', (size_t)(end - key));
    if (!separator) separator = memchr(key, '.', (size_t)(end - key));
    return separator && separator != key && separator + 1 != end;
}

static PeEconResult settings_valid(const PeEconConfig *c) {
    if (c->algorithm != PE_ECON_ALGORITHM_1 || c->reserved) return PE_ECON_BAD_ALGORITHM;
    if (c->route > PE_ECON_ROUTE_ITEMS) return PE_ECON_BAD_ROUTE;
    if (c->flags & ~(unsigned)PE_ECON_F_ALL) return PE_ECON_BAD_FLAGS;
    if (!price_ok(c->price_pct)) return PE_ECON_BAD_PRICE;
    if (c->payment_chance > 10000 || c->item_chance > 10000 || c->super_chance > 10000 || c->duper_chance > 10000)
        return PE_ECON_BAD_CHANCE;
    if ((unsigned)c->super_chance + c->duper_chance > 10000) return PE_ECON_BUNDLE_CHANCES;
    if (c->route == PE_ECON_ROUTE_ITEMS &&
        (c->flags & (PE_ECON_F_INCOME | PE_ECON_F_PERFORMANCE | PE_ECON_F_EMPTY_STEAL | PE_ECON_F_SUPER_REDEEM | PE_ECON_F_DUPER_REDEEM)))
        return PE_ECON_ROUTE_NEEDS_COUNTER;
    if (((c->flags & PE_ECON_F_SUPER_REDEEM) && !c->super_value) || ((c->flags & PE_ECON_F_DUPER_REDEEM) && !c->duper_value))
        return PE_ECON_REDEEM_ZERO;
    if ((c->flags & PE_ECON_F_EMPTY_STEAL) && !c->steal_junk) return PE_ECON_STEAL_ZERO;
    if ((c->flags & PE_ECON_F_PERFORMANCE) && !(c->flags & PE_ECON_F_INCOME)) return PE_ECON_PERFORMANCE_NO_INCOME;
    if (((c->flags & PE_ECON_F_INCOME) && (!c->payment_chance || (!c->income && !c->boss_income))) ||
        ((c->flags & PE_ECON_F_BONUS_ITEM) && !c->item_chance && !c->item_guarantee) ||
        ((c->flags & PE_ECON_F_SUPER_DROP) && !c->super_chance) ||
        ((c->flags & PE_ECON_F_DUPER_DROP) && !c->duper_chance) ||
        ((c->flags & PE_ECON_F_PERFORMANCE) && !c->performance_pct))
        return PE_ECON_NEVER_PAYS;
    return PE_ECON_OK;
}

PeEconResult pe_econ_validate(const PeEconConfig *c) {
    if (!c || c->engine > PE_ECON_CONFIGURABLE) return PE_ECON_BAD_ENGINE;
    if (!key_ok(c->key)) return PE_ECON_BAD_KEY;
    if (c->engine == PE_ECON_LEGACY) return PE_ECON_OK;   /* Enhanced's settings live elsewhere */
    return settings_valid(c);
}

/* D1 section 4. Provisional numbers, for playtesting. */
static const struct {
    const char *key, *name;
    uint16_t flags, income, boss_income, item_chance, item_guarantee;
} s_builtin[] = {
    {"pe.enhanced", "Enhanced", 0, 0, 0, 0, 0},
    {"pe.vanilla_drops", "Vanilla drops + inventory QoL", 0, 0, 0, 0, 0},
    {"pe.light", "Light economy", PE_ECON_F_INCOME | PE_ECON_F_SUPER_REDEEM | PE_ECON_F_DUPER_REDEEM, 1, 3, 0, 0},
    {"pe.steady", "Steady Income", PE_ECON_F_INCOME, 3, 10, 0, 0},
    {"pe.treasure", "Treasure Hunter", PE_ECON_F_BONUS_ITEM, 0, 0, 1000, 10},
};
enum { BUILTIN_COUNT = (int)(sizeof s_builtin / sizeof s_builtin[0]) };

int pe_econ_builtin_count(void) { return BUILTIN_COUNT; }
const char *pe_econ_builtin_name(int i) { return i >= 0 && i < BUILTIN_COUNT ? s_builtin[i].name : NULL; }

static void set_key(PeEconConfig *c, const char *key) {
    memset(c->key, 0, sizeof c->key);
    strncpy(c->key, key, sizeof c->key - 1);
}

PeEconResult pe_econ_builtin(int i, uint16_t price_pct, PeEconConfig *out) {
    if (i < 0 || i >= BUILTIN_COUNT || !out) return PE_ECON_BAD_PRESET;
    if (!price_ok(price_pct)) return PE_ECON_BAD_PRICE;
    memset(out, 0, sizeof *out);
    out->engine = i == 0 ? PE_ECON_LEGACY : PE_ECON_CONFIGURABLE;
    out->route = PE_ECON_ROUTE_COUNTER;
    out->algorithm = PE_ECON_ALGORITHM_1;
    out->flags = s_builtin[i].flags;
    out->price_pct = price_pct;
    out->income = s_builtin[i].income;
    out->boss_income = s_builtin[i].boss_income;
    out->payment_chance = 10000;
    out->item_chance = s_builtin[i].item_chance;
    out->item_guarantee = s_builtin[i].item_guarantee;
    out->super_value = 10;     /* shown even while redemption is off */
    out->duper_value = 20;
    set_key(out, s_builtin[i].key);
    return pe_econ_validate(out);
}

PeEconResult pe_econ_from_preset(const char *key, const PeEconomyPreset *p, uint16_t price_pct, PeEconConfig *out) {
    if (!key || !p || !out || strlen(key) >= sizeof out->key) return PE_ECON_BAD_PRESET;
    if (!strncmp(key, "pe.", 3)) return PE_ECON_BAD_KEY;    /* the built-in presets' namespace */
    memset(out, 0, sizeof *out);
    out->engine = PE_ECON_CONFIGURABLE;
    out->algorithm = p->algorithm;
    out->route = p->native_to_counter ? PE_ECON_ROUTE_COUNTER : PE_ECON_ROUTE_ITEMS;
    out->flags = (uint16_t)((p->extra_junk ? PE_ECON_F_INCOME : 0) | (p->rare_drops ? PE_ECON_F_BONUS_ITEM : 0) |
                            (p->super_drops ? PE_ECON_F_SUPER_DROP : 0) | (p->duper_drops ? PE_ECON_F_DUPER_DROP : 0) |
                            (p->redeem_super ? PE_ECON_F_SUPER_REDEEM : 0) | (p->redeem_duper ? PE_ECON_F_DUPER_REDEEM : 0) |
                            (p->performance_bonus ? PE_ECON_F_PERFORMANCE : 0) | (p->empty_steal ? PE_ECON_F_EMPTY_STEAL : 0));
    out->price_pct = price_pct;
    out->income = p->battle_junk;
    out->boss_income = p->boss_junk;
    out->payment_chance = p->payment_chance_10000;
    out->item_chance = p->bonus_chance_10000;
    out->item_guarantee = p->bonus_guarantee;
    out->super_chance = p->super_chance_10000;
    out->duper_chance = p->duper_chance_10000;
    out->super_value = p->super_value;
    out->duper_value = p->duper_value;
    out->performance_pct = p->performance_pct;
    out->steal_junk = p->empty_steal_junk;
    set_key(out, key);
    return pe_econ_validate(out);
}

/* `c` with what's behind switched-off controls, the key and the price cleared. */
static void effective(const PeEconConfig *from, PeEconConfig *c) {
    const unsigned f = from->flags;
    *c = *from;
    memset(c->key, 0, sizeof c->key);
    c->price_pct = 0;
    if (c->engine == PE_ECON_LEGACY) {
        const uint8_t engine = c->engine;
        memset(c, 0, sizeof *c);
        c->engine = engine;
        return;
    }
    if (!(f & PE_ECON_F_INCOME)) c->income = c->boss_income = c->payment_chance = 0;
    if (!(f & PE_ECON_F_BONUS_ITEM)) c->item_chance = c->item_guarantee = 0;
    if (!(f & PE_ECON_F_SUPER_DROP)) c->super_chance = 0;
    if (!(f & PE_ECON_F_DUPER_DROP)) c->duper_chance = 0;
    if (!(f & PE_ECON_F_SUPER_REDEEM)) c->super_value = 0;
    if (!(f & PE_ECON_F_DUPER_REDEEM)) c->duper_value = 0;
    if (!(f & PE_ECON_F_PERFORMANCE)) c->performance_pct = 0;
    if (!(f & PE_ECON_F_EMPTY_STEAL)) c->steal_junk = 0;
}

int pe_econ_same_settings(const PeEconConfig *a, const PeEconConfig *b) {
    PeEconConfig x, y;
    effective(a, &x);
    effective(b, &y);
    return !memcmp(&x, &y, sizeof x);
}

int pe_econ_match_builtin(const PeEconConfig *c) {
    for (int i = 0; i < BUILTIN_COUNT; ++i) {
        PeEconConfig preset;
        if (pe_econ_builtin(i, 100, &preset) == PE_ECON_OK && pe_econ_same_settings(c, &preset)) return i;
    }
    return -1;
}

void pe_econ_state_reset(PeEconState *s) {
    memset(s, 0, sizeof *s);
    s->config.engine = PE_ECON_LEGACY;
}

_Static_assert(sizeof(PeEconProgress) == 72, "no padding: progress compares by bytes");

static int progress_ok(const PeEconProgress *g) {
    if (g->remainder > 99999999u || !g->next_sequence || !g->rng_payment || !g->rng_item || !g->rng_bundle) return 0;
    for (int i = 0; i < PE_ECON_SLOTS; ++i) {
        const PeEconPending *e = &g->pending[i];
        int kind_ok;
        if (!e->kind) {
            if (e->origin || e->flags || e->content || e->reserved || e->amount || e->sequence) return 0;
            continue;
        }
        kind_ok = i == PE_ECON_SLOT_ITEM ? e->kind >= PE_ECON_PENDING_ITEM && e->kind <= PE_ECON_PENDING_PERMIT
                : i == PE_ECON_SLOT_BUNDLE ? e->kind == PE_ECON_PENDING_BUNDLE
                                           : e->kind == PE_ECON_PENDING_CURRENCY;
        if (!kind_ok || e->flags || e->reserved || !e->sequence || e->sequence >= g->next_sequence) return 0;
        if (e->kind == PE_ECON_PENDING_CURRENCY && (!e->amount || e->amount > 99999u)) return 0;
    }
    return 1;
}

PeEconResult pe_econ_state_validate(const PeEconState *s) {
    PeEconResult r;
    if (!s || s->initialized > 1) return PE_ECON_BAD_STATE;
    if (!s->initialized) {   /* never left Enhanced: exactly the reset state */
        PeEconState fresh;
        pe_econ_state_reset(&fresh);
        return memcmp(&s->config, &fresh.config, sizeof fresh.config) ||
                       memcmp(&s->progress, &fresh.progress, sizeof fresh.progress)
                   ? PE_ECON_BAD_STATE
                   : PE_ECON_OK;
    }
    r = pe_econ_validate(&s->config);
    if (r == PE_ECON_OK) r = settings_valid(&s->config);
    if (r != PE_ECON_OK) return r;
    return progress_ok(&s->progress) ? PE_ECON_OK : PE_ECON_BAD_STATE;
}

static void wr16(uint8_t *p, unsigned v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void wr32(uint8_t *p, uint32_t v) { wr16(p, v & 0xFFFFu); wr16(p + 2, v >> 16); }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return rd16(p) | (uint32_t)rd16(p + 2) << 16; }

void pe_econ_encode(const PeEconState *s, uint8_t out[PE_ECON_RECORD_SIZE]) {
    const PeEconConfig *c = &s->config;
    const PeEconProgress *g = &s->progress;
    memset(out, 0, PE_ECON_RECORD_SIZE);
    if (!s->initialized) return;
    out[0] = c->engine;
    out[1] = c->route;
    wr16(out + 2, c->flags);
    wr16(out + 4, c->price_pct);
    wr16(out + 6, c->income);
    wr16(out + 8, c->boss_income);
    wr16(out + 10, c->payment_chance);
    wr16(out + 12, c->item_chance);
    wr16(out + 14, c->item_guarantee);
    wr16(out + 16, c->super_chance);
    wr16(out + 18, c->duper_chance);
    wr16(out + 20, c->super_value);
    wr16(out + 22, c->duper_value);
    wr16(out + 24, c->performance_pct);
    wr16(out + 26, c->steal_junk);
    out[28] = (uint8_t)strnlen(c->key, sizeof c->key);
    out[29] = c->algorithm;
    memcpy(out + 32, c->key, sizeof c->key);
    wr32(out + 80, g->rng_payment);
    wr32(out + 84, g->rng_item);
    wr32(out + 88, g->rng_bundle);
    wr16(out + 92, g->dry);
    out[94] = g->effect_progress;
    out[95] = g->permit_progress;
    wr32(out + 96, g->remainder);
    wr32(out + 100, g->next_sequence);
    for (int i = 0; i < PE_ECON_SLOTS; ++i) {
        uint8_t *p = out + 104 + i * 16;
        p[0] = g->pending[i].kind;
        p[1] = g->pending[i].origin;
        wr16(p + 2, g->pending[i].flags);
        wr16(p + 4, g->pending[i].content);
        wr16(p + 6, g->pending[i].reserved);
        wr32(p + 8, g->pending[i].amount);
        wr32(p + 12, g->pending[i].sequence);
    }
    wr32(out + 152, 1u);
}

PeEconResult pe_econ_decode(const uint8_t in[PE_ECON_RECORD_SIZE], PeEconState *out) {
    static const uint8_t zero[PE_ECON_RECORD_SIZE];
    PeEconState s;
    PeEconResult r;
    if (!memcmp(in, zero, sizeof zero)) {
        pe_econ_state_reset(out);
        return PE_ECON_OK;
    }
    if (rd32(in + 152) != 1u || rd16(in + 30) || rd32(in + 156)) return PE_ECON_BAD_STATE;
    memset(&s, 0, sizeof s);
    s.initialized = 1;
    s.config.engine = in[0];
    s.config.route = in[1];
    s.config.flags = rd16(in + 2);
    s.config.price_pct = rd16(in + 4);
    s.config.income = rd16(in + 6);
    s.config.boss_income = rd16(in + 8);
    s.config.payment_chance = rd16(in + 10);
    s.config.item_chance = rd16(in + 12);
    s.config.item_guarantee = rd16(in + 14);
    s.config.super_chance = rd16(in + 16);
    s.config.duper_chance = rd16(in + 18);
    s.config.super_value = rd16(in + 20);
    s.config.duper_value = rd16(in + 22);
    s.config.performance_pct = rd16(in + 24);
    s.config.steal_junk = rd16(in + 26);
    s.config.algorithm = in[29];
    memcpy(s.config.key, in + 32, sizeof s.config.key);
    if (in[28] != strnlen(s.config.key, sizeof s.config.key)) return PE_ECON_BAD_KEY;
    s.progress.rng_payment = rd32(in + 80);
    s.progress.rng_item = rd32(in + 84);
    s.progress.rng_bundle = rd32(in + 88);
    s.progress.dry = rd16(in + 92);
    s.progress.effect_progress = in[94];
    s.progress.permit_progress = in[95];
    s.progress.remainder = rd32(in + 96);
    s.progress.next_sequence = rd32(in + 100);
    for (int i = 0; i < PE_ECON_SLOTS; ++i) {
        const uint8_t *p = in + 104 + i * 16;
        s.progress.pending[i].kind = p[0];
        s.progress.pending[i].origin = p[1];
        s.progress.pending[i].flags = rd16(p + 2);
        s.progress.pending[i].content = rd16(p + 4);
        s.progress.pending[i].reserved = rd16(p + 6);
        s.progress.pending[i].amount = rd32(p + 8);
        s.progress.pending[i].sequence = rd32(p + 12);
    }
    r = pe_econ_state_validate(&s);
    if (r != PE_ECON_OK) return r;
    *out = s;
    return PE_ECON_OK;
}

static PeEconTransition transition_of(const PeEconState *s, const PeEconConfig *t) {
    if (s->config.engine == PE_ECON_LEGACY)
        return t->engine == PE_ECON_LEGACY ? PE_ECON_NO_CHANGE : PE_ECON_ENTER_NEW;
    if (t->engine == PE_ECON_LEGACY) return PE_ECON_RETURN_ENHANCED;
    return memcmp(&s->config, t, sizeof *t) ? PE_ECON_WITHIN_NEW : PE_ECON_NO_CHANGE;
}

void pe_econ_preview(const PeEconState *s, const PeEconConfig *t, const PeEconContext *ctx, PeEconPreview *out) {
    const int was_new = s->config.engine == PE_ECON_CONFIGURABLE;
    memset(out, 0, sizeof *out);
    out->result = pe_econ_validate(t);
    if (out->result != PE_ECON_OK) return;
    out->transition = transition_of(s, t);
    out->price_from = was_new ? s->config.price_pct : ctx->enhanced_price_pct;
    out->price_to = t->engine == PE_ECON_CONFIGURABLE ? t->price_pct : ctx->enhanced_price_pct;
    if (out->transition == PE_ECON_NO_CHANGE) return;
    if (t->engine == PE_ECON_CONFIGURABLE) {
        if (t->flags & PE_ECON_F_SUPER_REDEEM) out->redeem_super = ctx->held_super;
        if (t->flags & PE_ECON_F_DUPER_REDEEM) out->redeem_duper = ctx->held_duper;
        out->redeem_junk = out->redeem_super * t->super_value + out->redeem_duper * t->duper_value;
    }
    if (out->transition == PE_ECON_ENTER_NEW) {
        out->freezes_legacy_carried = ctx->legacy_carried != 0;
        out->first_opt_in = !s->initialized;
    }
}

PeEconResult pe_econ_apply(PeEconState *s, const PeEconConfig *t, int free_field, int (*enable_store)(void)) {
    PeEconTransition move;
    const PeEconResult valid = pe_econ_validate(t);
    if (valid != PE_ECON_OK) return valid;
    move = transition_of(s, t);
    if (move == PE_ECON_NO_CHANGE) return PE_ECON_OK;
    if (!free_field) return PE_ECON_NOT_FREE_FIELD;
    if (move == PE_ECON_ENTER_NEW && (!enable_store || !enable_store())) return PE_ECON_STORE_REFUSED;
    if (move == PE_ECON_RETURN_ENHANCED) {
        s->config.engine = PE_ECON_LEGACY;   /* the new path's settings wait for the next return */
        return PE_ECON_OK;
    }
    s->config = *t;
    if (!s->initialized) s->progress.next_sequence = 1;
    s->initialized = 1;
    return PE_ECON_OK;
}
