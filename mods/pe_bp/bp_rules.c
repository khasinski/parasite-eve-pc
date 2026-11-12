#include "bp_rules.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Section I and Fortitude: 3 ranks of +10% (75/100/125); Fortitude 100/150/200. */
static const BpTrackDef defense_defs[BP_DEFENSE_TRACKS] = {
    {"poison", "Poison", {75, 100, 125}},
    {"confusion", "Confusion", {75, 100, 125}},
    {"darkness", "Darkness", {75, 100, 125}},
    {"stiffness", "Stiffness", {75, 100, 125}},
    {"fortitude", "Fortitude", {100, 150, 200}},
};
/* Vanilla armor-mod chances (battle_ailment.h): Poison 70, Confusion 60, Darkness 60,
 * Stiffness lock 50 / slow 70. */
int bp_armor_chance(int track, int slow) {
    static const int chance[BP_RESIST_TRACKS] = {70, 60, 60, 50};
    if (track < 0 || track >= BP_RESIST_TRACKS) return 0;
    return track == 3 && slow ? 70 : chance[track];
}

#define POWER(n) (1u << (n))
/* Section F: tiers A 150/250/350, B 100/200/300, C 50/100/150, Scan one rank of 100. */
static const BpLineDef line_defs[BP_MASTERY_LINES] = {
    {"heal", "Heal", 'A', 3, POWER(0) | POWER(1) | POWER(2), 60,
     {"Healing +15%", "Healing +15%, cost -15%", "Healing +30%, cost -15%"},
     "staying in fights on PE instead of items", "long fights without a save phone nearby"},
    {"detox", "Detox", 'C', 3, POWER(3), 100,
     {"Cost -25%", "Cost -25%, one-use Poison immunity", "Cost -50%, one-use Poison immunity"},
     "not carrying Cure-P", "poison-heavy areas"},
    {"medic", "Medic", 'B', 3, POWER(4), 600,
     {"Cost -15%", "Cost -15%, heals 10% max HP", "Cost -30%, heals 10% max HP"},
     "one answer to every ailment", "enemies that stack several ailments"},
    {"barrier", "Barrier", 'B', 3, POWER(5), 0,
     {"Costs 1/4 of max PE", "Costs 1/5 of max PE", "Costs 1/6 of max PE"},
     "spending PE to save HP", "big single hits you can see coming"},
    {"energy_shot", "Energy Shot", 'B', 3, POWER(6), 0,
     {"Damage +10%", "Damage +20%", "Damage +20%, +10% more above half PE"},
     "fighting with PE instead of guns", "opening fights on a full PE bar"},
    {"scan", "Scan", 'S', 1, POWER(7), 50,
     {"Scan costs no PE", NULL, NULL},
     "knowing weaknesses before committing", "first meetings and multi-part bosses"},
    {"slow", "Slow", 'B', 3, POWER(8), 90,
     {"Neutral-enemy chance 60%", "Neutral-enemy chance 70%", "Neutral-enemy chance 80%"},
     "slowing a dangerous enemy down", "neutral enemies; weak ones are already sure"},
    {"haste", "Haste", 'A', 3, POWER(9), 200,
     {"Duration +25%", "Duration +50%", "Duration +50%, cost -25%"},
     "moving first and dodging", "fights decided by extra turns"},
    {"confuse", "Confuse", 'B', 3, POWER(10), 150,
     {"Neutral-enemy chance 60%", "Neutral-enemy chance 70%", "Neutral-enemy chance 80%"},
     "turning enemies against each other", "neutral enemies in groups"},
    {"gene_heal", "Gene Heal", 'B', 3, POWER(11), 400,
     {"Cost -15%", "Cost -15%, 2 HP per tick", "Cost -30%, 2 HP per tick"},
     "steady healing over time", "long fights with PE to spare"},
    {"preraise", "Preraise", 'B', 3, POWER(12), 1000,
     {"Cost -15%", "Cost -15%, revive with half PE", "Cost -30%, revive with half PE and 75% HP"},
     "a safety net", "boss fights and risky play"},
    {"full_recover", "Full Recover", 'B', 3, POWER(18), 1300,
     {"Cost -10%", "Cost -20%", "Cost -30%"},
     "a big emergency button", "when everything has gone wrong at once"},
};

/* Cast-cost cut (percent) at ranks 1..3; Scan's one rank is free. Barrier is in max-PE parts. */
static const uint8_t line_cut[BP_MASTERY_LINES][BP_MAX_RANK] = {
    {0, 15, 15}, {25, 25, 50}, {15, 15, 30}, {0, 0, 0}, {0, 0, 0}, {100, 100, 100},
    {0, 0, 0}, {0, 0, 25}, {0, 0, 0}, {15, 15, 30}, {15, 15, 30}, {10, 20, 30},
};
int bp_line_cost(int line, int rank, int pe_light, int max_pe) {
    const BpLineDef *d = bp_line_def(line);
    int cost;
    if (!d) return -1;
    if (line == 3) {   /* Barrier: 1/3 of max PE, then 1/4, 1/5, 1/6 */
        if (max_pe <= 0) return -1;
        cost = max_pe / (3 + (rank > 3 ? 3 : rank));
    } else {
        if (!d->vanilla_drain) return -1;
        cost = d->vanilla_drain * (100 - (rank ? line_cut[line][(rank > 3 ? 3 : rank) - 1] : 0)) / 100;
    }
    return pe_light ? cost * 2 / 3 : cost;
}
const BpTrackDef *bp_defense_def(int t) { return t >= 0 && t < BP_DEFENSE_TRACKS ? &defense_defs[t] : NULL; }
const BpLineDef *bp_line_def(int l) { return l >= 0 && l < BP_MASTERY_LINES ? &line_defs[l] : NULL; }

const char *bp_gear_stat_name(int armor, int stat) {
    static const char *const gun[3] = {"Attack", "Range", "Bullets"};
    static const char *const vest[3] = {"Defense", "P. Energy", "Critical"};
    return stat >= 0 && stat < 3 ? (armor ? vest : gun)[stat] : "";
}

unsigned bp_track_price(int t, int rank) {
    const BpTrackDef *d = bp_defense_def(t);
    return d && rank >= 1 && rank <= BP_MAX_RANK ? d->price[rank - 1] : 0;
}
unsigned bp_line_price(int l, int rank) {
    const BpLineDef *d = bp_line_def(l);
    if (!d || rank < 1 || rank > d->ranks) return 0;
    switch (d->tier) {
    case 'A': return 50u + 100u * (unsigned)rank;   /* 150 / 250 / 350 */
    case 'B': return 100u * (unsigned)rank;         /* 100 / 200 / 300 */
    case 'C': return 50u * (unsigned)rank;          /*  50 / 100 / 150 */
    default:  return 100u;                          /* Scan: one rank */
    }
}
unsigned bp_track_spent(int t, int rank) { unsigned n = 0; for (int i = 1; i <= rank; ++i) n += bp_track_price(t, i); return n; }
unsigned bp_line_spent(int l, int rank) { unsigned n = 0; for (int i = 1; i <= rank; ++i) n += bp_line_price(l, i); return n; }
int bp_line_learned(const BpSnapshot *s, int l) { const BpLineDef *d = bp_line_def(l); return d && (s->learned & d->powers); }
int bp_effective_resist(int armor, int rank) {
    int bp = 10 * rank;
    return 100 - (100 - armor) * (100 - bp) / 100;
}

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
    if ((unsigned long)s->bp + total > BP_MAX) { q->error = BP_ERR_OVERFLOW; return; }
    if (q->junk_fee > s->junk) { q->error = BP_ERR_JUNK; return; }
    q->bp_after = s->bp + total;
    q->junk_after = s->junk - q->junk_fee;
}

BpQuote bp_quote(const BpSnapshot *s, const BpRequest *r, BpPriceFn price) {
    BpQuote q;
    memset(&q, 0, sizeof q);
    q.bp_after = s ? s->bp : 0; q.junk_after = s ? s->junk : 0;
    if (!s || !s->loaded) { q.error = BP_ERR_NOT_LOADED; return q; }
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
        refund(s, &q, 0, price);
        return q;
    case BP_REQ_RESET_MASTERY:
        q.rank_before = s->rec.mastery[r->a];
        q.bp_refund = q.refund_mastery = bp_line_spent(r->a, q.rank_before);
        refund(s, &q, 0, price);
        return q;
    case BP_REQ_RESET_ALL:
        for (int t = 0; t < BP_DEFENSE_TRACKS; ++t) q.refund_defense += bp_track_spent(t, s->rec.defense[t]);
        for (int l = 0; l < BP_MASTERY_LINES; ++l) q.refund_mastery += bp_line_spent(l, s->rec.mastery[l]);
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

int bp_record_equal(const BpRecord *a, const BpRecord *b) {
    return !memcmp(a->defense, b->defense, sizeof a->defense) && a->fortitude_mode == b->fortitude_mode &&
           !memcmp(a->mastery, b->mastery, sizeof a->mastery) && a->full_resets == b->full_resets &&
           a->active_steps == b->active_steps && a->active_points == b->active_points &&
           a->carried_pool == b->carried_pool;
}
static void put16(uint8_t *p, unsigned v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, v & 0xFFFFu); put16(p + 2, v >> 16); }
static unsigned get16(const uint8_t *p) { return p[0] | (unsigned)p[1] << 8; }
static uint32_t get32(const uint8_t *p) { return get16(p) | (uint32_t)get16(p + 2) << 16; }
void bp_record_encode(const BpRecord *r, uint8_t ranks[BP_RANKS_BYTES], uint8_t run[BP_RUN_BYTES]) {
    memset(ranks, 0, BP_RANKS_BYTES); memset(run, 0, BP_RUN_BYTES);
    memcpy(ranks, r->defense, BP_DEFENSE_TRACKS);
    ranks[5] = r->fortitude_mode;
    memcpy(ranks + 6, r->mastery, BP_MASTERY_LINES);
    run[0] = r->full_resets;
    put16(run + 2, r->active_steps); put16(run + 4, r->active_points); put32(run + 6, r->carried_pool);
}
int bp_record_decode(const uint8_t ranks[BP_RANKS_BYTES], const uint8_t run[BP_RUN_BYTES], BpRecord *r) {
    BpRecord t;
    memset(&t, 0, sizeof t);
    memcpy(t.defense, ranks, BP_DEFENSE_TRACKS);
    t.fortitude_mode = ranks[5];
    memcpy(t.mastery, ranks + 6, BP_MASTERY_LINES);
    t.full_resets = run[0];
    t.active_steps = (uint16_t)get16(run + 2); t.active_points = (uint16_t)get16(run + 4);
    t.carried_pool = get32(run + 6);
    for (int i = 0; i < BP_DEFENSE_TRACKS; ++i) if (t.defense[i] > BP_MAX_RANK) return 1;
    for (int i = 0; i < BP_MASTERY_LINES; ++i) if (t.mastery[i] > line_defs[i].ranks) return 1;
    if (t.fortitude_mode > BP_FORT_SAVE_FOR_HP1 || t.carried_pool > BP_MAX) return 1;
    if (t.active_steps > 999 || t.active_points > 9999 || (!t.active_steps != !t.active_points)) return 1;
    *r = t;
    return 0;
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
    case BP_REQ_RESET_DEFENSE: s->rec.defense[r->a] = 0; break;
    case BP_REQ_RESET_MASTERY: s->rec.mastery[r->a] = 0; break;
    case BP_REQ_RESET_ALL:
        memset(s->rec.defense, 0, sizeof s->rec.defense);
        memset(s->rec.mastery, 0, sizeof s->rec.mastery);
        s->active.value -= s->rec.active_points;
        if (q->capacity_steps) s->capacity.value = s->capacity_base;
        s->rec.active_points = s->rec.active_steps = 0;
        if (s->rec.full_resets < 255) ++s->rec.full_resets;
        break;
    case BP_REQ_FORT_MODE: s->rec.fortitude_mode = (uint8_t)r->a; break;
    default: return BP_ERR_UNKNOWN;
    }
    s->rec.carried_pool -= q->pool_cost;
    s->bp = q->bp_after;
    s->junk = q->junk_after;
    return BP_OK;
}

/* ---- battle effects ---- */
int bp_fortitude_charges(int rank) { return rank <= 0 ? 0 : rank == 1 ? 1 : 2; }
BpFortitudePlan bp_fortitude_plan(int effect, int hp, int barrier, int rank, int mode, int used) {
    BpFortitudePlan p = {0, 0, hp, hp};
    if (effect != BP_FX_HALVE_HP && effect != BP_FX_HP_TO_1) return p;
    /* What the game does without protection (enemy_actions.c). */
    if (effect == BP_FX_HALVE_HP) p.vanilla_hp = barrier || hp < 2 ? hp : hp >> 1;
    else p.vanilla_hp = hp >= 2 ? 1 : hp == 1 ? -1 : hp;
    p.result_hp = p.vanilla_hp;
    if (used >= bp_fortitude_charges(rank) || hp <= 0) return p;
    if (effect == BP_FX_HALVE_HP && mode == BP_FORT_SAVE_FOR_HP1) return p;
    if (rank >= 3 && used == 0) {
        p.negate = 1; p.result_hp = hp;
    } else if (effect == BP_FX_HALVE_HP) {
        p.result_hp = barrier || hp < 2 ? hp : hp - hp / 4;
    } else {
        p.result_hp = hp / 4 > 1 ? hp / 4 : 1;
    }
    p.use = p.result_hp > p.vanilla_hp;
    if (!p.use) { p.negate = 0; p.result_hp = p.vanilla_hp; }
    return p;
}
int bp_line_for_power(int power) {
    static const signed char map[20] = {0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, -1, -1, -1, -1, -1, 11, -1};
    return power >= 0 && power < 20 ? map[power] : -1;
}
int bp_line_cut(int line, int rank) {
    if (line < 0 || line >= BP_MASTERY_LINES || rank <= 0) return 0;
    return line_cut[line][(rank > 3 ? 3 : rank) - 1];
}
int bp_heal_bonus(int rank) { return rank <= 0 ? 0 : rank < 3 ? 15 : 30; }
int bp_haste_bonus(int rank) { return rank <= 0 ? 0 : rank == 1 ? 25 : 50; }
int bp_neutral_chance(int rank) { return rank <= 0 ? 50 : 50 + 10 * (rank > 3 ? 3 : rank); }
/* 50 + 50% x reroll = chance, so reroll = 2 x (chance - 50): 20 / 40 / 60. */
int bp_neutral_reroll(int rank) { return (bp_neutral_chance(rank) - 50) * 2; }
int bp_energy_shot_bonus(int rank, int above_half) {
    if (rank <= 0) return 0;
    return rank == 1 ? 10 : rank == 2 ? 20 : 20 + (above_half ? 10 : 0);
}
