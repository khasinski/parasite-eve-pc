/* RE2's zone check. See pe_re_zone.h. Follows bio2-ds SCE_AT.C's Sce_at_check in what it does
 * (read for behaviour, written here independently). */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "pe_re_zone.h"
#include "pe_re_session.h"

void pe_re_front(double x, double z, int facing, double *fx, double *fz) {
    const double a = (facing & 4095) * (acos(-1.0) / 2048.0);
    *fx = x - PE_RE_FRONT_REACH * sin(a);
    *fz = z - PE_RE_FRONT_REACH * cos(a);
}

typedef struct {
    PeImpZone zone;
    PeReZoneHit hit;
} Candidate;

static int by_id(const void *a, const void *b) {
    const Candidate *x = a, *y = b;
    if (x->hit.id != y->hit.id) return x->hit.id < y->hit.id ? -1 : 1;
    if (x->hit.from != y->hit.from) return x->hit.from < y->hit.from ? -1 : 1;
    return x->hit.index < y->hit.index ? -1 : x->hit.index > y->hit.index;
}

/* the zone as the scripts left it: an aot_reset of its id gives its kind, check bits and data */
static void as_left(const PeReScdRun *run, const PeImpZone *z, const uint8_t data[6], uint32_t index,
                    PeReZoneFrom from, Candidate *c) {
    memset(c, 0, sizeof *c);
    c->zone = *z;
    c->hit.id = z->id, c->hit.sce = z->sce, c->hit.sat = z->sat, c->hit.from = from, c->hit.index = index;
    if (data) memcpy(c->hit.data, data, 6);
    for (int i = run && run->ran ? run->nreset - 1 : -1; i >= 0; i--)
        if (run->reset[i].id == z->id) {
            c->hit.sce = run->reset[i].sce, c->hit.sat = run->reset[i].sat;
            memcpy(c->hit.data, run->reset[i].data, 6);
            break;
        }
}

static int counts(const PeReScdRun *run, uint32_t at) { return !run || pe_re_scd_live(run, at); }

static int gather(const PeImpRoomLogic *room, const PeReScdRun *run, const PeReProgress *p, Candidate **out) {
    uint32_t nt = 0, nd = 0, np = 0;
    pe_imp_logic_section(room, PE_IMP_LOGIC_TRIG, &nt, NULL);
    pe_imp_logic_section(room, PE_IMP_LOGIC_DOOR, &nd, NULL);
    pe_imp_logic_section(room, PE_IMP_LOGIC_PICK, &np, NULL);
    Candidate *c = malloc(sizeof *c * (nt + nd + np + 1));
    int n = 0;
    if (!c) return 0;
    for (uint32_t i = 0; i < nt; i++) {
        PeImpTrigger t;
        if (!pe_imp_logic_trigger(room, i, &t) && counts(run, t.at)) as_left(run, &t.zone, t.data, i, PE_RE_FROM_TRIGGER, &c[n++]);
    }
    for (uint32_t i = 0; i < nd; i++) {
        PeImpDoor d;
        if (!pe_imp_logic_door(room, i, &d) && counts(run, d.at)) as_left(run, &d.zone, NULL, i, PE_RE_FROM_DOOR, &c[n++]);
    }
    for (uint32_t i = 0; i < np; i++) {
        PeImpPickup pk;
        if (!pe_imp_logic_pickup(room, i, &pk) && counts(run, pk.at) && !pe_re_pickup_gone(room, run, p, &pk))
            as_left(run, &pk.zone, NULL, i, PE_RE_FROM_PICKUP, &c[n++]);
    }
    qsort(c, (size_t)n, sizeof *c, by_id);
    *out = c;
    return n;
}

int pe_re_zone_by_id(const PeImpRoomLogic *room, const PeReScdRun *run, const PeReProgress *p, uint8_t id,
                     PeReZoneHit *out) {
    Candidate *c = NULL;
    int found = -1;
    if (!room) return -1;
    const int n = gather(room, run, p, &c);
    for (int i = 0; i < n && found < 0; i++) if (c[i].hit.id == id) *out = c[i].hit, found = 0;
    free(c);
    return found;
}

int pe_re_zone_check(const PeImpRoomLogic *room, const PeReScdRun *run, const PeReProgress *p, double x, double z,
                     int facing, int floor, int manual, int16_t *work, PeReZoneHit *hits, int max) {
    Candidate *c = NULL;
    double fx, fz;
    int found = 0;
    if (!room) return 0;
    const int n = gather(room, run, p, &c);
    pe_re_front(x, z, facing, &fx, &fz);
    const unsigned want = manual ? PE_RE_SAT_MANUAL : 0;
    for (int i = 0; i < n; i++) {
        const PeReZoneHit *h = &c[i].hit;
        const PeImpZone *zn = &c[i].zone;
        const int16_t old_f = work ? work[PE_RE_WORK_F_ATARI] : 0, old_u = work ? work[PE_RE_WORK_U_ATARI] : 0;
        if (!(h->sat & PE_RE_SAT_PLAYER) || (h->sat & PE_RE_SAT_MANUAL) != want) continue;
        if (!(zn->floor & 0x80) && zn->floor != floor) continue;
        if (zn->super & 0x80) continue;                    /* on an object or enemy: not tracked */
        if ((h->sat & PE_RE_SAT_UNDER) && pe_re_in_quad(zn->x, zn->z, x, z)) {
            if (work) work[PE_RE_WORK_U_ATARI] = h->id;
        } else if ((h->sat & PE_RE_SAT_FRONT) && pe_re_in_quad(zn->x, zn->z, fx, fz)) {
            if (work) work[PE_RE_WORK_F_ATARI] = h->id;
        } else {
            continue;
        }
        if (!h->sce) {                                     /* kind 0: an examine looks on; Nothing restores */
            if (!manual && work) work[PE_RE_WORK_F_ATARI] = old_f, work[PE_RE_WORK_U_ATARI] = old_u;
            continue;
        }
        if (found < max) hits[found] = *h;
        found++;
        if (manual) break;
    }
    free(c);
    return found < max ? found : max;
}
