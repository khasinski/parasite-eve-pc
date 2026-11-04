/* Seed generator: which item each spot gives. No game code here (the tests run it alone).
 *
 * Keys first, by assumed fill: each key goes to a spot that can be reached while holding every key
 * not placed yet, so whatever order the player finds them in, the next one is reachable. Then the
 * other items are shuffled over the spots left, per story day on Easy. Every try is checked
 * (pe_rand_check); a failed try re-rolls from the same seed, so a seed always gives one layout.
 * Only a PRNG of our own with fixed constants, table order and integer maths: the same seed is the
 * same layout on every machine and build of the same logic version.
 *
 * Key logic (pe_randomizer.h): a needs bit is met when its key is held and its key's own spot has
 * been reached. On Medium a key moves within its own story day, or to a chest of an earlier day that
 * stays open for as long as the key's lock can ask for it (late); on Hard to any chest of its day or
 * earlier the player can still walk to and open whenever the lock asks (hard). Only off days
 * where every key spot has needs data; a key spot without data counts as needing every key of the
 * days before it, which is all the game asks while that day's keys stay where they are. With
 * PE_RAND_MOVE_KEYS at 0 no key moves on any difficulty. */
#include <string.h>

#include "pe_randomizer.h"

#define PE_RAND_TRIES 1000
#define PE_RAND_MAX_SPOTS 512
#define PE_RAND_DAYS 8

/* PCG32 (O'Neill), XSH RR. */
typedef struct PeRng { uint64_t state, inc; } PeRng;

static uint32_t rng_next(PeRng *r) {
    uint64_t old = r->state;
    uint32_t x, rot;
    r->state = old * 6364136223846793005ull + r->inc;
    x = (uint32_t)(((old >> 18) ^ old) >> 27);
    rot = (uint32_t)(old >> 59);
    return (x >> rot) | (x << ((32 - rot) & 31));
}

static void rng_init(PeRng *r, uint64_t seed, uint64_t stream) {
    r->state = 0;
    r->inc = (stream << 1) | 1;
    rng_next(r);
    r->state += seed;
    rng_next(r);
}

static uint32_t rng_below(PeRng *r, uint32_t n) {   /* unbiased 0..n-1 */
    uint32_t floor = (0u - n) % n, x;
    do x = rng_next(r); while (x < floor);
    return x % n;
}

/* The table seen through the key logic. */
typedef struct PeLogic {
    const PeRandSpot *spots;
    unsigned count, nkeys, keys[PE_RAND_MAX_KEYS];   /* keys[k]: the spot of key k */
    uint32_t all;
    uint32_t needs[PE_RAND_MAX_SPOTS];               /* PE_RAND_NEEDS_UNKNOWN: unreachable for keys */
    unsigned char day_complete[PE_RAND_DAYS];
} PeLogic;

static void logic_init(PeLogic *l, const PeRandSpot *spots, unsigned count) {
    memset(l, 0, sizeof *l);
    l->spots = spots;
    l->count = count;
    for (unsigned i = 0; i < count && l->nkeys < PE_RAND_MAX_KEYS; ++i)
        if (spots[i].flags & PE_RAND_KEY) l->keys[l->nkeys++] = i;
    l->all = l->nkeys == 32 ? 0xFFFFFFFFu : (1u << l->nkeys) - 1;
    for (unsigned d = 1; d < PE_RAND_DAYS; ++d) l->day_complete[d] = 1;
    for (unsigned k = 0; k < l->nkeys; ++k) {
        const PeRandSpot *s = &spots[l->keys[k]];
        if (s->needs == PE_RAND_NEEDS_UNKNOWN || !s->day || s->day >= PE_RAND_DAYS) l->day_complete[s->day % PE_RAND_DAYS] = 0;
    }
    l->day_complete[0] = 0;
    for (unsigned i = 0; i < count; ++i) l->needs[i] = spots[i].needs;
    for (unsigned k = 0; k < l->nkeys; ++k) {
        unsigned i = l->keys[k];
        if (spots[i].needs != PE_RAND_NEEDS_UNKNOWN) continue;
        l->needs[i] = 0;
        for (unsigned j = 0; j < l->nkeys; ++j)
            if (spots[l->keys[j]].day < spots[i].day) l->needs[i] |= 1u << j;
    }
}

static int key_of_item(const PeLogic *l, unsigned item) {
    for (unsigned k = 0; k < l->nkeys; ++k)
        if (l->spots[l->keys[k]].vanilla == item) return (int)k;
    return -1;
}

static int reachable(const PeLogic *l, unsigned spot, uint32_t met) {
    return l->needs[spot] != PE_RAND_NEEDS_UNKNOWN && !(l->needs[spot] & ~met);
}

/* Bits met from `assumed` on, with key k at spot at[k] (-1: not placed yet). */
static uint32_t sweep(const PeLogic *l, const int *at, uint32_t assumed) {
    uint32_t held = assumed, visited = assumed;
    for (int changed = 1; changed;) {
        uint32_t met = held & visited;
        changed = 0;
        for (unsigned k = 0; k < l->nkeys; ++k) {
            uint32_t bit = 1u << k;
            if (!(held & bit) && at[k] >= 0 && reachable(l, (unsigned)at[k], met)) { held |= bit; changed = 1; }
            if (!(visited & bit) && reachable(l, l->keys[k], met)) { visited |= bit; changed = 1; }
        }
    }
    return held & visited;
}

/* Shuffle group of a spot's item: 0 = stays vanilla. Easy keeps items on their story day. */
static unsigned bucket(const PeRandSpot *s, int difficulty) { return difficulty == PE_RAND_EASY ? s->day : 1; }

/* A spot key k may move to (besides its own), on Medium and Hard, from a day whose key spots all
 * have data: a chest with logic data, on the key's own story day or earlier. Medium: on its own day
 * reached before the key's lock was first used, on an earlier day open whenever the lock needs the key
 * (any other earlier-day chest can be walked past for good). Hard: reachable and open whenever the
 * lock asks. Any area. */
static int key_fits(const PeLogic *l, unsigned to_spot, unsigned k, int difficulty) {
    const PeRandSpot *to = &l->spots[to_spot], *from = &l->spots[l->keys[k]];
    if (difficulty == PE_RAND_EASY || to->kind != PE_RAND_CHEST || (to->flags & PE_RAND_KEY) ||
        to->needs == PE_RAND_NEEDS_UNKNOWN || from->day >= PE_RAND_DAYS || !l->day_complete[from->day] ||
        !to->day || to->day > from->day) return 0;
    if (difficulty == PE_RAND_HARD) return (to->hard >> k) & 1;
    return ((to->day == from->day ? to->before : to->late) >> k) & 1;
}

static int place_keys(const PeLogic *l, int difficulty, PeRng *rng, int *at) {
    unsigned order[PE_RAND_MAX_KEYS], cand[PE_RAND_MAX_SPOTS];
    unsigned char used[PE_RAND_MAX_SPOTS] = {0};
    for (unsigned k = 0; k < l->nkeys; ++k) { order[k] = k; at[k] = -1; }
    for (unsigned k = l->nkeys; k > 1; --k) {
        unsigned j = rng_below(rng, k), t = order[k - 1];
        order[k - 1] = order[j]; order[j] = t;
    }
    for (unsigned n = 0; n < l->nkeys; ++n) {
        unsigned k = order[n], m = 0;
        uint32_t assumed = l->all & ~(1u << k);
        for (unsigned j = 0; j < l->nkeys; ++j) if (at[j] >= 0) assumed &= ~(1u << j);
        uint32_t met = sweep(l, at, assumed);
        for (unsigned i = 0; i < l->count; ++i)
            if (!used[i] && (i == l->keys[k] || key_fits(l, i, k, difficulty)) && reachable(l, i, met))
                cand[m++] = i;
        if (!m) return 0;
        at[k] = (int)cand[rng_below(rng, m)];
        used[at[k]] = 1;
    }
    return 1;
}

static int fill(const PeRandSpot *spots, unsigned count, int difficulty, int (*gift_safe)(unsigned),
                PeRng *rng, uint16_t *out, const unsigned char *has_key) {
    uint16_t items[PE_RAND_MAX_SPOTS];
    unsigned todo[PE_RAND_MAX_SPOTS];
    for (unsigned b = 1; b <= 7; ++b) {
        unsigned ni = 0, nt = 0;
        for (unsigned i = 0; i < count; ++i) {
            if (bucket(&spots[i], difficulty) != b) continue;
            if (!(spots[i].flags & PE_RAND_KEY)) items[ni++] = spots[i].vanilla;
            if (!has_key[i]) todo[nt++] = i;
        }
        if (ni != nt) return 0;
        /* pickups and gifts first, from the items that can't be lost there */
        for (unsigned t = 0; t < nt; ++t) {
            const PeRandSpot *s = &spots[todo[t]];
            unsigned fit[PE_RAND_MAX_SPOTS], m = 0;
            if (s->kind == PE_RAND_CHEST) continue;
            for (unsigned j = 0; j < ni; ++j)
                if (items[j] == s->vanilla || gift_safe(items[j])) fit[m++] = j;
            if (!m) return 0;
            unsigned j = fit[rng_below(rng, m)];
            out[todo[t]] = items[j];
            items[j] = items[--ni];
        }
        for (unsigned t = 0; t < nt; ++t) {
            if (spots[todo[t]].kind != PE_RAND_CHEST) continue;
            unsigned j = rng_below(rng, ni);
            out[todo[t]] = items[j];
            items[j] = items[--ni];
        }
    }
    return 1;
}

static int try_layout(const PeLogic *l, int difficulty, int move_keys, int (*gift_safe)(unsigned),
                      PeRng *rng, uint16_t *out, unsigned *moved) {
    unsigned char has_key[PE_RAND_MAX_SPOTS] = {0};
    int at[PE_RAND_MAX_KEYS];
    for (unsigned i = 0; i < l->count; ++i) out[i] = l->spots[i].vanilla;
    for (unsigned k = 0; k < l->nkeys; ++k) at[k] = (int)l->keys[k];
    if (move_keys && !place_keys(l, difficulty, rng, at)) return 0;
    *moved = 0;
    for (unsigned k = 0; k < l->nkeys; ++k) {
        has_key[at[k]] = 1;
        out[at[k]] = l->spots[l->keys[k]].vanilla;
        *moved += at[k] != (int)l->keys[k];
    }
    return fill(l->spots, l->count, difficulty, gift_safe, rng, out, has_key);
}

const char *pe_rand_check(const PeRandSpot *spots, unsigned count, int difficulty,
                          int (*gift_safe)(unsigned item), const uint16_t *layout) {
    static PeLogic l;   /* big; the game and the tests call this from one thread */
    int at[PE_RAND_MAX_KEYS];
    if (count > PE_RAND_MAX_SPOTS) return "too many spots";
    if (difficulty < 0 || difficulty >= PE_RAND_DIFFICULTIES) return "unknown difficulty";
    logic_init(&l, spots, count);
    /* same items, per shuffle group: nothing made up, nothing lost, nothing doubled */
    for (unsigned b = 0; b <= 7; ++b) {
        int n[256] = {0};
        for (unsigned i = 0; i < count; ++i) {
            if (bucket(&spots[i], difficulty) != b) continue;
            if (spots[i].vanilla > 255 || layout[i] > 255 || !layout[i]) return "item id out of range";
            if (b == 0 && layout[i] != spots[i].vanilla) return "a fixed spot changed";
            if (!(spots[i].flags & PE_RAND_KEY)) ++n[spots[i].vanilla];
            if (key_of_item(&l, layout[i]) < 0) --n[layout[i]];
        }
        for (unsigned v = 0; v < 256; ++v)
            if (n[v] && b) return "items not conserved";
    }
    for (unsigned k = 0; k < l.nkeys; ++k) at[k] = -1;
    for (unsigned i = 0; i < count; ++i) {
        int k = key_of_item(&l, layout[i]);
        if (k >= 0) {
            if (at[k] >= 0) return "a key placed twice";
            at[k] = (int)i;
            if (i != l.keys[k] && !key_fits(&l, i, (unsigned)k, difficulty)) return "a key in a spot it can't use";
        } else if (spots[i].kind != PE_RAND_CHEST && layout[i] != spots[i].vanilla && !gift_safe(layout[i])) {
            return "an item a pickup or gift could lose";
        }
    }
    for (unsigned k = 0; k < l.nkeys; ++k)
        if (at[k] < 0) return "a key is missing";
    if (sweep(&l, at, 0) != l.all) return "a key can't be reached";
    return 0;
}

void pe_rand_generate(const PeRandSpot *spots, unsigned count, uint32_t seed, int difficulty,
                      int (*gift_safe)(unsigned item), uint16_t *out, PeRandResult *result) {
    static PeLogic l;
    uint64_t base = (uint64_t)seed | (uint64_t)(unsigned)difficulty << 32 | (uint64_t)PE_RAND_LOGIC_VERSION << 40;
    PeRng rng;
    int can_move = 0;
    memset(result, 0, sizeof *result);
    for (unsigned i = 0; i < count && i < PE_RAND_MAX_SPOTS; ++i) out[i] = spots[i].vanilla;
    if (count > PE_RAND_MAX_SPOTS || difficulty < 0 || difficulty >= PE_RAND_DIFFICULTIES) {
        result->fallback = 2;
        return;
    }
    logic_init(&l, spots, count);
    result->keys_locked = 1;
    for (unsigned d = 1; PE_RAND_MOVE_KEYS && d < PE_RAND_DAYS; ++d)
        for (unsigned k = 0; k < l.nkeys; ++k)
            if (l.day_complete[d] && spots[l.keys[k]].day == d) result->keys_locked = 0;
    /* phase 0 moves keys where the difficulty and logic data allow; phase 1 keeps them vanilla */
    can_move = difficulty != PE_RAND_EASY && !result->keys_locked;
    for (int phase = can_move ? 0 : 1; phase < 2; ++phase) {
        for (unsigned t = 0; t < PE_RAND_TRIES; ++t) {
            unsigned moved = 0;
            rng_init(&rng, base, (uint64_t)phase << 32 | t);
            if (try_layout(&l, difficulty, phase == 0, gift_safe, &rng, out, &moved) &&
                !pe_rand_check(spots, count, difficulty, gift_safe, out)) {
                result->attempts = t + 1 + (can_move && phase ? PE_RAND_TRIES : 0);
                result->keys_moved = moved;
                result->fallback = can_move && phase;
                return;
            }
        }
    }
    for (unsigned i = 0; i < count; ++i) out[i] = spots[i].vanilla;
    result->attempts = (can_move ? 2 : 1) * PE_RAND_TRIES;
    result->fallback = 2;
}

/* Battle: foe b's model in foe a's slot, the room's first free block and palette row so far given: 1 if it fits
 * (in a's own blocks when it's no bigger and no other slot shares them, else in the free ones, which it takes). */
static int foe_fits(const PeRandFoe *a, const PeRandFoe *b, unsigned *end, unsigned *rows_end) {
    if (b->blocks <= a->blocks && !a->shared) return 1;
    if (*end + b->blocks > 8 || *rows_end + b->blocks > 8) return 0;
    *end += b->blocks;
    *rows_end += b->blocks;
    return 1;
}

static int foe_ok(const uint32_t *ok, unsigned count, unsigned a, unsigned b) {
    return ok[a * ((count + 31) / 32) + b / 32] >> (b % 32) & 1;
}

unsigned pe_rand_foes_generate(const PeRandFoe *foes, const uint32_t *ok, unsigned count, uint32_t seed, uint16_t *out) {
    static uint16_t pick[PE_RAND_MAX_SPOTS];
    unsigned moved = 0, end = 0, rows_end = 0;
    PeRng rng;
    for (unsigned i = 0; i < count; ++i) out[i] = (uint16_t)i;
    if (count > PE_RAND_MAX_SPOTS) return 0;
    rng_init(&rng, seed, 0xBA771Eu);
    for (unsigned i = 0; i < count; ++i) {
        unsigned n = 0;
        if (!i || foes[i].room != foes[i - 1].room) { end = foes[i].end; rows_end = foes[i].rows_end; }
        if (foes[i].boss && !PE_RAND_MOVE_BOSSES) continue;
        for (unsigned b = 0; b < count; ++b) if (foe_ok(ok, count, i, b)) pick[n++] = (uint16_t)b;
        while (n) {   /* a random one of those left, until one fits */
            const unsigned k = rng_below(&rng, n), b = pick[k];
            pick[k] = pick[--n];
            if (foe_fits(&foes[i], &foes[b], &end, &rows_end)) { out[i] = (uint16_t)b; ++moved; break; }
        }
    }
    return moved;
}

const char *pe_rand_foes_check(const PeRandFoe *foes, const uint32_t *ok, unsigned count, const uint16_t *plan) {
    unsigned end = 0, rows_end = 0;
    for (unsigned i = 0; i < count; ++i) {
        if (!i || foes[i].room != foes[i - 1].room) { end = foes[i].end; rows_end = foes[i].rows_end; }
        if (i && (foes[i].room < foes[i - 1].room || (foes[i].room == foes[i - 1].room && foes[i].entity <= foes[i - 1].entity)))
            return "table not in room order";
        if (plan[i] == i) continue;
        if (plan[i] >= count || !foe_ok(ok, count, i, plan[i])) return "a slot takes a foe it may not";
        if (foes[i].boss != foes[plan[i]].boss) return "a boss and a foe swapped";
        if (foes[i].boss && !PE_RAND_MOVE_BOSSES) return "a boss moved";
        if (!foe_fits(&foes[i], &foes[plan[i]], &end, &rows_end)) return "a model doesn't fit its room";
    }
    return 0;
}
