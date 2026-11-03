#include <string.h>
#include <limits.h>
#include "pe_economy_policy.h"
static uint32_t s_notice_generation;
static uint64_t s_last_operation;
static int header(const PeApiHeader *h, size_t n) {
    return h && h->version == 1 && h->size == n;
}
PeRegResult pe_economy_policy_register(const char *batch, const PeEconomyPolicyDef *d) {
    if (!d) return PE_REG_BAD_ARGUMENT;
    if (!header(&d->api, sizeof *d)) return PE_REG_BAD_VERSION;
    if (!d->key[0] || !memchr(d->key, 0, sizeof d->key) || !d->name[0] || !memchr(d->name, 0, sizeof d->name))
        return PE_REG_BAD_ARGUMENT;
    if (d->kind == PE_POLICY_MODIFIER) {
        if (!d->modifier || d->observer) return PE_REG_BAD_ARGUMENT;
    } else if (d->kind == PE_POLICY_OBSERVER) {
        if (!d->observer || d->modifier) return PE_REG_BAD_ARGUMENT;
    } else if (d->kind == PE_POLICY_PRESET) {
        const PeEconomyPreset *p = &d->preset;
        if (d->modifier || d->observer || !header(&p->api, sizeof *p)) return PE_REG_BAD_VERSION;
        if (p->bonus_chance_10000 > 10000 || p->vanilla_drops > 1 || p->rare_drops > 1 || p->extra_junk > 1 ||
            p->special_junk > 1 || p->performance_bonus > 1)
            return PE_REG_BAD_ARGUMENT;
    } else return PE_REG_BAD_ARGUMENT;
    PeRegEntry e = {PE_REG_CONTENT, d->key, d, sizeof *d, NULL, 0, PE_SCHEMA_POLICY};
    return pe_reg_submit(batch, &e);
}
const PeEconomyPolicyDef *pe_economy_policy_find(const char *key) {
    uint32_t size;
    const PeEconomyPolicyDef *d =
        pe_reg_find_typed(PE_REG_CONTENT, key, PE_SCHEMA_POLICY, sizeof(PeEconomyPolicyDef));
    size = d ? sizeof *d : 0;
    return d && size == sizeof *d && header(&d->api, sizeof *d) && d->kind >= 1 && d->kind <= 3 &&
                   (d->kind != PE_POLICY_MODIFIER || d->modifier) &&
                   (d->kind != PE_POLICY_OBSERVER || d->observer)
               ? d
               : NULL;
}
const PeEconomyPolicyDef *pe_economy_policy_at(int kind, int index) {
    if (index < 0 || kind < 1 || kind > 3) return NULL;
    for (int i = 0; i < pe_reg_count(PE_REG_CONTENT); ++i) {
        const PeEconomyPolicyDef *d = pe_economy_policy_find(pe_reg_key(PE_REG_CONTENT, i));
        if (d && d->kind == kind && index-- == 0) return d;
    }
    return NULL;
}
int pe_economy_policy_count(int kind) {
    int n = 0;
    while (pe_economy_policy_at(kind, n))
        ++n;
    return n;
}
static PeRegResult context(const PePayoutContext *c) {
    PeRegResult r = pe_reg_access();
    if (r != PE_REG_OK) return r;
    if (!c) return PE_REG_BAD_ARGUMENT;
    if (!header(&c->api, sizeof *c)) return PE_REG_BAD_VERSION;
    if (c->generation != pe_reg_generation()) return PE_REG_STALE_GENERATION;
    if (!c->operation || c->configurable != 1 || c->boss > 1 || c->remainder >= 100000000)
        return PE_REG_BAD_ARGUMENT;
    return PE_REG_OK;
}
PeRegResult pe_economy_policy_preview(const PePayoutContext *c, PePayoutResult *out) {
    PeRegResult r = context(c);
    if (r != PE_REG_OK) return r;
    if (!out) return PE_REG_BAD_ARGUMENT;
    uint64_t n = c->nominal_10000;
    int64_t f = 10000;
    /* Snapshot copied before entering callbacks. A provider never receives the caller's object. */
    PePayoutContext snapshot = *c;
    if (!pe_reg_readonly_begin()) return PE_REG_WRONG_PHASE;
    for (int i = 0; i < pe_reg_count(PE_REG_CONTENT); ++i) {
        const PeEconomyPolicyDef *d = pe_economy_policy_find(pe_reg_key(PE_REG_CONTENT, i));
        if (!d || d->kind != PE_POLICY_MODIFIER) continue;
        PePayoutContribution v = {0, 10000};
        PePayoutContext copy = snapshot;
        r = d->modifier(&copy, &v);
        if (r != PE_REG_OK || v.add_junk > 65535 || v.factor_10000 > 40000) {
            pe_reg_readonly_end();
            return r == PE_REG_OK ? PE_REG_BAD_ARGUMENT : r;
        }
        n += (uint64_t)v.add_junk * 10000u;
        f += (int64_t)v.factor_10000 - 10000;
    }
    pe_reg_readonly_end();
    if (f < 0 || ((uint64_t)f && n > (UINT64_MAX - c->remainder) / (uint64_t)f)) return PE_REG_CONFLICT;
    uint64_t v = n * (uint64_t)f + c->remainder;
    PePayoutResult result = {(uint32_t)(v / 100000000u > 99999 ? 99999 : v / 100000000u),
                             v / 100000000u >= 99999 ? 0 : (uint32_t)(v % 100000000u)};
    *out = result;
    return PE_REG_OK;
}
PeRegResult pe_economy_policy_notify(const PePayoutContext *c, const PePayoutResult *v) {
    PeRegResult r = context(c);
    if (r != PE_REG_OK) return r;
    if (!v || v->junk > 99999 || v->remainder >= 100000000) return PE_REG_BAD_ARGUMENT;
    if (s_notice_generation != c->generation) {
        s_notice_generation = c->generation;
        s_last_operation = 0;
    }
    if (c->operation <= s_last_operation) return PE_REG_CONFLICT;
    PePayoutContext snapshot = *c;
    PePayoutResult result = *v;
    if (!pe_reg_readonly_begin()) return PE_REG_WRONG_PHASE;
    s_last_operation = c->operation;
    for (int i = 0; i < pe_reg_count(PE_REG_CONTENT); ++i) {
        const PeEconomyPolicyDef *d = pe_economy_policy_find(pe_reg_key(PE_REG_CONTENT, i));
        if (d && d->kind == PE_POLICY_OBSERVER) {
            PePayoutContext copy = snapshot;
            PePayoutResult value = result;
            d->observer(&copy, &value);
        }
    }
    pe_reg_readonly_end();
    return PE_REG_OK;
}
void pe_economy_policy_reset(void) {
    if (pe_reg_access() == PE_REG_OK) {
        s_last_operation = 0;
        s_notice_generation = pe_reg_generation();
    }
}

const PeEconomyPolicyApi g_pe_economy_policy_api = {sizeof(PeEconomyPolicyApi), pe_economy_policy_register,
                                                    pe_economy_policy_find,     pe_economy_policy_count,
                                                    pe_economy_policy_at,       pe_economy_policy_preview};
