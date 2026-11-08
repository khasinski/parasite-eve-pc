/* Which call plays. Pure: the calls, the record and the random number come from the caller. */
#include "pe_phone_core.h"

#define PE_PICK_MAX 128

int pe_phone_fits(const PePhoneCall *c, const PePhoneContext *x, const PePhoneRecord *r) {
    int i;
    if (c->tone < 0 || c->tone > 1 || !(x->tone_mask & (1 << c->tone))) return 0;
    if (c->day_from && (x->day < c->day_from || x->day > (c->day_to ? c->day_to : c->day_from))) return 0;
    if ((c->story_from || c->story_to) && (x->story < c->story_from || x->story > c->story_to)) return 0;
    if (c->rooms && c->room_count > 0) {
        for (i = 0; i < c->room_count && c->rooms[i] != x->room; ++i) {}
        if (i == c->room_count) return 0;
    }
    if (c->after && !pe_phone_record_heard(r, c->after)) return 0;
    if (c->once && pe_phone_record_once(r, c->key)) return 0;
    return 1;
}

static unsigned pe_weight(const PePhoneCall *c) { return c->weight > 0 ? (unsigned)c->weight : 1u; }

const PePhoneCall *pe_phone_pick(const PePhoneCall *const *calls, int count, const PePhoneContext *x,
                                 const PePhoneRecord *r, uint32_t roll) {
    const PePhoneCall *fit[PE_PICK_MAX], *pool[PE_PICK_MAX], *best = NULL;
    unsigned total = 0;
    int n = 0, m = 0, i, pass;
    if (!calls || !x || !r) return NULL;
    for (i = 0; i < count && n < PE_PICK_MAX; ++i) if (calls[i] && pe_phone_fits(calls[i], x, r)) fit[n++] = calls[i];
    /* set calls: the highest priority not heard yet */
    for (i = 0; i < n; ++i)
        if (fit[i]->priority > 0 && !pe_phone_record_heard(r, fit[i]->key) && (!best || fit[i]->priority > best->priority))
            best = fit[i];
    if (best) return best;
    /* the pool: not the last five; failing that, not the last one; failing that, any */
    for (pass = 0; pass < 3 && !m; ++pass)
        for (i = 0; i < n; ++i) {
            const int at = pe_phone_record_recent(r, fit[i]->key);
            if (fit[i]->priority > 0 || (pass == 0 && at) || (pass == 1 && at == 1)) continue;
            pool[m++] = fit[i];
        }
    for (i = 0; i < m; ++i) total += pe_weight(pool[i]);
    if (!total) return NULL;
    roll %= total;
    for (i = 0; i < m; ++i) {
        if (roll < pe_weight(pool[i])) return pool[i];
        roll -= pe_weight(pool[i]);
    }
    return pool[m - 1];
}
