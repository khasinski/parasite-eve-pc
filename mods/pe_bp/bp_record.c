/* The BP record: equality, save sections, carried ranks and the EX Game carryover (bp_record.h). */
#include "bp_record.h"
#include <string.h>

/* Ranks 1..carried were carried from the last run; their share of a refund is upgrade-only. */
unsigned bp_carried_track_value(const BpRecord *r, int t) {
    int c = r->carried_defense[t] < r->defense[t] ? r->carried_defense[t] : r->defense[t];
    return bp_track_spent(t, c);
}
unsigned bp_carried_line_value(const BpRecord *r, int l) {
    int c = r->carried_mastery[l] < r->mastery[l] ? r->carried_mastery[l] : r->mastery[l];
    return bp_line_spent(l, c);
}
int bp_record_equal(const BpRecord *a, const BpRecord *b) {
    return !memcmp(a->defense, b->defense, sizeof a->defense) && a->fortitude_mode == b->fortitude_mode &&
           !memcmp(a->mastery, b->mastery, sizeof a->mastery) && a->full_resets == b->full_resets &&
           a->active_steps == b->active_steps && a->active_points == b->active_points &&
           a->carried_pool == b->carried_pool &&
           !memcmp(a->carried_defense, b->carried_defense, sizeof a->carried_defense) &&
           !memcmp(a->carried_mastery, b->carried_mastery, sizeof a->carried_mastery);
}
void bp_carry_encode(const BpRecord *r, uint8_t carry[BP_CARRY_BYTES]) {
    memset(carry, 0, BP_CARRY_BYTES);
    memcpy(carry, r->carried_defense, BP_DEFENSE_TRACKS);
    memcpy(carry + BP_DEFENSE_TRACKS, r->carried_mastery, BP_MASTERY_LINES);
}
int bp_carry_decode(const uint8_t carry[BP_CARRY_BYTES], BpRecord *r) {
    for (int i = 0; i < BP_DEFENSE_TRACKS; ++i) if (carry[i] > r->defense[i]) return 1;
    for (int i = 0; i < BP_MASTERY_LINES; ++i) if (carry[BP_DEFENSE_TRACKS + i] > r->mastery[i]) return 1;
    memcpy(r->carried_defense, carry, BP_DEFENSE_TRACKS);
    memcpy(r->carried_mastery, carry + BP_DEFENSE_TRACKS, BP_MASTERY_LINES);
    return 0;
}
unsigned bp_record_rank_value(const BpRecord *r, int carried_only) {
    unsigned n = 0;
    for (int t = 0; t < BP_DEFENSE_TRACKS; ++t) n += carried_only ? bp_carried_track_value(r, t) : bp_track_spent(t, r->defense[t]);
    for (int l = 0; l < BP_MASTERY_LINES; ++l) n += carried_only ? bp_carried_line_value(r, l) : bp_line_spent(l, r->mastery[l]);
    return n;
}
const char *bp_carry_name(int mode) {
    static const char *const names[BP_CARRY_MODES] = {"Off", "Keep purchased skills", "Convert skills to BP"};
    return mode >= 0 && mode < BP_CARRY_MODES ? names[mode] : "Off";
}
BpRecord bp_ex_carry(const BpRecord *last, int mode) {
    BpRecord n;
    memset(&n, 0, sizeof n);   /* run data always restarts: full resets, Active Time ledger */
    if (mode == BP_CARRY_KEEP) {
        memcpy(n.defense, last->defense, sizeof n.defense);
        memcpy(n.mastery, last->mastery, sizeof n.mastery);
        memcpy(n.carried_defense, last->defense, sizeof n.carried_defense);   /* every rank is now carried */
        memcpy(n.carried_mastery, last->mastery, sizeof n.carried_mastery);
        n.fortitude_mode = last->fortitude_mode;
        n.carried_pool = last->carried_pool;
    } else if (mode == BP_CARRY_CONVERT) {
        unsigned long pool = (unsigned long)last->carried_pool + bp_record_rank_value(last, 0);
        n.carried_pool = (uint32_t)(pool > BP_MAX ? BP_MAX : pool);
        n.fortitude_mode = last->fortitude_mode;
    }
    return n;
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
    for (int i = 0; i < BP_MASTERY_LINES; ++i) if (t.mastery[i] > bp_line_def(i)->ranks) return 1;
    if (t.fortitude_mode > BP_FORT_SAVE_FOR_HP1 || t.carried_pool > BP_MAX) return 1;
    if (t.active_steps > 999 || t.active_points > 9999 || (!t.active_steps != !t.active_points)) return 1;
    *r = t;
    return 0;
}
