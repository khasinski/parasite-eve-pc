/* Randomizer: the spot table is sane, and every seed on every difficulty gives a layout that passes
 * the checks, the same one each time. Golden hashes catch a change that moves items for the same
 * seed: if it's meant, raise PE_RAND_LOGIC_VERSION and update them. Built with PE_RAND_MOVE_KEYS at 1
 * (as shipped: the key logic, golden layouts) and at 0 (no key moves). */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pe_randomizer.h"
#include "pe_rand_spots.h"
#include "pe_rand_foes.h"

static int test_gift_safe(unsigned id) { return id >= 1 && id <= 0x16; }   /* medicine, ammo, Junk */

static uint32_t layout_hash(const uint16_t *layout, unsigned count) {
    uint32_t h = 2166136261u;
    for (unsigned i = 0; i < count; ++i) { h = (h ^ (layout[i] & 0xFF)) * 16777619u; h = (h ^ (layout[i] >> 8)) * 16777619u; }
    return h;
}

static void table_is_sane(void) {
    unsigned keys = 0, sites_of[PE_RAND_SPOTS] = {0}, vanilla_of[PE_RAND_SPOTS] = {0};
    for (unsigned i = 0; i < PE_RAND_SPOTS; ++i) {
        const PeRandSpot *s = &g_pe_rand_spots[i];
        assert(s->vanilla && s->vanilla <= 0xFF && s->kind <= PE_RAND_GIFT && s->day <= 6);
        if (s->flags & PE_RAND_KEY) {
            ++keys;
            for (unsigned j = 0; j < i; ++j) assert(g_pe_rand_spots[j].vanilla != s->vanilla);   /* one spot per key */
        }
    }
    assert(keys == 17 && keys <= PE_RAND_MAX_KEYS);   /* 15 + the Theater keys (Hard) */
    for (unsigned i = 0; i < PE_RAND_SITES; ++i) {
        const PeRandSite *s = &g_pe_rand_sites[i];
        assert(s->spot < PE_RAND_SPOTS && s->operand <= 1 && s->at % 4 == 0);
        /* the operand holds an item: the spot's own, or the other one of a roll */
        assert(s->expect[2 + s->operand] >= 1 && s->expect[2 + s->operand] <= 0xFF);
        vanilla_of[s->spot] += s->expect[2 + s->operand] == g_pe_rand_spots[s->spot].vanilla;
        /* AssignValue, GiveItem, its popup, or its full-bag swap (op 169, item as operand 0) */
        assert((s->expect[0] & 0x1FFF) == 10 || (s->expect[0] & 0x1FFF) == 167 || (s->expect[0] & 0x1FFF) == 232 ||
               ((s->expect[0] & 0x1FFF) == 169 && s->operand == 0));
        if (i) {
            const PeRandSite *p = &g_pe_rand_sites[i - 1];
            assert(p->map < s->map || (p->map == s->map && p->at < s->at));   /* sorted, no duplicates */
        }
        ++sites_of[s->spot];
    }
    for (unsigned i = 0; i < PE_RAND_SPOTS; ++i) assert(sites_of[i] && vanilla_of[i]);
}

/* Spots i and j have a site in the same room (map). */
static int share_room(unsigned i, unsigned j) {
    for (unsigned a = 0; a < PE_RAND_SITES; ++a)
        for (unsigned b = 0; b < PE_RAND_SITES; ++b)
            if (g_pe_rand_sites[a].spot == i && g_pe_rand_sites[b].spot == j &&
                g_pe_rand_sites[a].map == g_pe_rand_sites[b].map) return 1;
    return 0;
}

/* Spot i has a site in a room from lo to hi. */
static int in_rooms(unsigned i, unsigned lo, unsigned hi) {
    for (unsigned a = 0; a < PE_RAND_SITES; ++a)
        if (g_pe_rand_sites[a].spot == i && g_pe_rand_sites[a].map >= lo && g_pe_rand_sites[a].map <= hi) return 1;
    return 0;
}

/* Where key item `key` may sit at spot i (not its own spot k): never a later day's chest or chest 84
 * (M126: it appears only after a touch in the room). Hard (tools/hardreach.py): any chest of its day or
 * earlier, Carnegie Hall and its sewer too (the moved doors lead back), except the St. Francis Key and
 * the Blue Cardkey, which stay in the power-off rooms (M320-M339: one way until both are used), and the
 * Klamp Key not in the museum's chests 156/157 (shut at the end of Day 5). Medium: a chest of the key's
 * own story day and save area (tools/keys-walk.txt), or a chest open whenever the key's lock asks
 * (tools/windows.txt): on earlier days the Klamp Key and the Chinatown charm on Days 2-4, the Elevator
 * Key on Day 2, the NYPD Special charm in chest 19. Hospital (tools/hospital-map.txt): fuses and the
 * St. Francis / Blue keys in the power-off rooms (M320-M339, the only way to the fuse box); the Green
 * Cardkey, the Elevator Key and the charm in a chest that stays reachable to the end (a site in
 * M125-M154; the story gate keeps the Green door shut until the box is done, so the Green Cardkey may
 * come early). */
static int key_may_sit(unsigned key, unsigned i, unsigned k, int difficulty) {
    const PeRandSpot *to = &g_pe_rand_spots[i], *from = &g_pe_rand_spots[k];
    if (i == 84 || to->day > from->day) return 0;
    if (difficulty == PE_RAND_HARD) switch (key) {
    case 0xD1: case 0xD2: return in_rooms(i, 320, 339);
    case 0xD5: return i != 156 && i != 157;
    default: return 1;
    }
    if (to->day < from->day) {
        switch (key) {
        case 0xD5: case 0xCB: return to->day >= 2;
        case 0xDA: return to->day == 2;
        case 0xCD: return i == 19;
        default: return 0;
        }
    }
    if (to->area != from->area) return 0;
    switch (key) {
    case 0xCE: case 0xCF: case 0xD0: case 0xD1: case 0xD2: return in_rooms(i, 320, 339);
    case 0xCC: case 0xD3: case 0xDA: return in_rooms(i, 125, 154);
    default: return 1;
    }
}

static void every_seed_passes(void) {
    static uint16_t a[PE_RAND_SPOTS], b[PE_RAND_SPOTS];
    for (int d = 0; d < PE_RAND_DIFFICULTIES; ++d) {
        unsigned changed = 0, moved = 0, other_room = 0, other_day = 0, day_one = 0;
        for (uint32_t seed = 0; seed < 2000; ++seed) {
            PeRandResult ra, rb;
            pe_rand_generate(g_pe_rand_spots, PE_RAND_SPOTS, seed, d, test_gift_safe, a, &ra);
            pe_rand_generate(g_pe_rand_spots, PE_RAND_SPOTS, seed, d, test_gift_safe, b, &rb);
            const char *why = pe_rand_check(g_pe_rand_spots, PE_RAND_SPOTS, d, test_gift_safe, a);
            if (why) { fprintf(stderr, "difficulty %d seed %u: %s\n", d, (unsigned)seed, why); assert(0); }
            assert(!memcmp(a, b, sizeof a) && !memcmp(&ra, &rb, sizeof ra));
            assert(!ra.fallback && ra.attempts >= 1);
            /* key logic (keys-by-hand.txt + keys-walk.txt): Easy keeps keys, the others move them only where
             * key_may_sit allows; with PE_RAND_MOVE_KEYS at 0 no difficulty moves them */
            assert(PE_RAND_MOVE_KEYS ? !ra.keys_locked && (d != PE_RAND_EASY || !ra.keys_moved)
                                     : ra.keys_locked && !ra.keys_moved);
            moved += ra.keys_moved;
            for (unsigned i = 0; i < PE_RAND_SPOTS; ++i) {
                changed += a[i] != g_pe_rand_spots[i].vanilla;
                for (unsigned k = 0; k < PE_RAND_SPOTS; ++k)
                    if ((g_pe_rand_spots[k].flags & PE_RAND_KEY) && a[i] == g_pe_rand_spots[k].vanilla && i != k) {
                        assert(g_pe_rand_spots[i].kind == PE_RAND_CHEST && key_may_sit(a[i], i, k, d));
                        other_room += !share_room(i, k);
                        other_day += g_pe_rand_spots[i].day != g_pe_rand_spots[k].day;
                        day_one += g_pe_rand_spots[i].day == 1;   /* Carnegie Hall and its sewer */
                    }
            }
        }
        assert(changed > 2000 * 20);   /* it does shuffle */
        assert(d == PE_RAND_EASY || (PE_RAND_MOVE_KEYS ? moved > 2000 && other_room > 2000 && other_day > 1000 : !moved));
        assert(d == PE_RAND_HARD && PE_RAND_MOVE_KEYS ? day_one > 1000 : !day_one);
    }
}

static void golden_layouts(void) {
    static const uint32_t golden[PE_RAND_DIFFICULTIES][4] = {
        {0x95933C9B, 0x018A37DB, 0xC047D003, 0xA29F2E9B},
        {0x399EE353, 0xAB0EE85B, 0xD4785D5B, 0xC7AA8CA3},
        {0x08B527B3, 0xF7F1C2A3, 0xA80606A3, 0x0F0F7AEB},
    };
    static uint16_t out[PE_RAND_SPOTS];
    int print = !golden[0][0];
    for (int d = 0; d < PE_RAND_DIFFICULTIES; ++d)
        for (uint32_t s = 0; s < 4; ++s) {
            PeRandResult r;
            pe_rand_generate(g_pe_rand_spots, PE_RAND_SPOTS, s * 111111u, d, test_gift_safe, out, &r);
            uint32_t h = layout_hash(out, PE_RAND_SPOTS);
            if (print) printf("golden difficulty %d seed %06u: 0x%08X\n", d, (unsigned)(s * 111111u), (unsigned)h);
            else assert(h == golden[d][s]);
        }
}

#define ALL 0xFFFFFFFFu
#define UNKNOWN PE_RAND_NEEDS_UNKNOWN

/* A small table with logic data: keys move, and every layout can be finished. Key spots 0-2 hold
 * keys A, B, C (bits 0-2); chests need nothing, A, or A and B. */
static void keys_move_with_logic_data(void) {
    static const PeRandSpot t[] = {
        {0xC8, PE_RAND_GIFT, 3, 6, PE_RAND_KEY, 0, 0, 0, 0},
        {0xC9, PE_RAND_GIFT, 3, 6, PE_RAND_KEY, 1, 0, 0, 0},
        {0xCA, PE_RAND_GIFT, 3, 6, PE_RAND_KEY, 3, 0, 0, 0},
        {0x01, PE_RAND_CHEST, 3, 6, 0, 0, ALL, 0, ALL}, {0x02, PE_RAND_CHEST, 3, 6, 0, 0, ALL, 0, ALL}, {0x03, PE_RAND_CHEST, 3, 6, 0, 1, ALL, 0, ALL},
        {0x04, PE_RAND_CHEST, 3, 6, 0, 1, ALL, 0, ALL}, {0x05, PE_RAND_CHEST, 3, 6, 0, 3, ALL, 0, ALL}, {0x06, PE_RAND_CHEST, 3, 6, 0, 3, ALL, 0, ALL},
        {0x40, PE_RAND_CHEST, 3, 7, 0, 0, ALL, 0, ALL},       /* another area */
        {0x07, PE_RAND_PICKUP, 3, 6, 0, 0, ALL, 0, ALL},
        {0x08, PE_RAND_CHEST, 3, 6, 0, 0, 0, 0, 0},           /* reached only after every lock: no key */
        {0x09, PE_RAND_CHEST, 3, 6, 0, UNKNOWN, ALL, 0, ALL}, /* no run reached it: no key */
        {0x0A, PE_RAND_CHEST, 2, 4, 0, 0, ALL, 0, 0},         /* an earlier day, no late or hard bits: no key */
        {0x0B, PE_RAND_CHEST, 2, 4, 0, 0, 0, 4, 4},           /* an earlier day open while C's lock asks: C only */
        {0x0C, PE_RAND_CHEST, 4, 7, 0, 0, ALL, ALL, ALL},     /* a later day: no key */
        {0x0D, PE_RAND_CHEST, 3, 7, 0, 0, 0, 0, 2},           /* its own day, past every lock but reached while B's asks: B, Hard only */
    };
    enum { N = sizeof t / sizeof t[0] };
    uint16_t out[N];
    unsigned moved = 0, other_area = 0, earlier_day = 0, same_day_late = 0;
    for (int d = PE_RAND_MEDIUM; d <= PE_RAND_HARD; ++d)
        for (uint32_t seed = 0; seed < 3000; ++seed) {
            PeRandResult r;
            pe_rand_generate(t, N, seed, d, test_gift_safe, out, &r);
            assert(!r.keys_locked && !r.fallback && !pe_rand_check(t, N, d, test_gift_safe, out));
            moved += r.keys_moved;
            for (unsigned i = 0; i < N; ++i) {
                if (out[i] >= 0xC8) assert(t[i].kind == PE_RAND_CHEST || t[i].vanilla == out[i]);
                if (out[i] == 0x40) assert(t[i].kind == PE_RAND_CHEST);   /* not gift-safe */
            }
            assert(out[11] < 0xC8 && out[12] < 0xC8 && out[13] < 0xC8 && out[15] < 0xC8);
            assert(out[14] < 0xC8 || out[14] == 0xCA);
            assert(out[16] < 0xC8 || (d == PE_RAND_HARD && out[16] == 0xC9));
            other_area += out[9] >= 0xC8;   /* Medium and Hard alike */
            earlier_day += out[14] == 0xCA;
            same_day_late += out[16] == 0xC9;
        }
    assert(moved > 1000 && other_area > 0 && earlier_day > 0 && same_day_late > 0);
    /* the checks catch a key that can't be reached: A behind A */
    uint16_t bad[N];
    for (unsigned i = 0; i < N; ++i) bad[i] = t[i].vanilla;
    bad[0] = 0x03; bad[5] = 0xC8;
    assert(pe_rand_check(t, N, PE_RAND_MEDIUM, test_gift_safe, bad));
    /* and one that moves on Easy, or a lost item */
    bad[0] = 0x01; bad[3] = 0xC8; bad[5] = 0x03;
    assert(!pe_rand_check(t, N, PE_RAND_MEDIUM, test_gift_safe, bad));
    assert(pe_rand_check(t, N, PE_RAND_EASY, test_gift_safe, bad));
    for (unsigned i = 0; i < N; ++i) bad[i] = t[i].vanilla;
    bad[4] = 0x01;
    assert(pe_rand_check(t, N, PE_RAND_HARD, test_gift_safe, bad));
    /* a key in a chest without its before bit, unknown needs or another day */
    for (unsigned i = 0; i < N; ++i) bad[i] = t[i].vanilla;
    bad[0] = 0x08; bad[11] = 0xC8;
    assert(pe_rand_check(t, N, PE_RAND_HARD, test_gift_safe, bad));
    bad[0] = 0x09; bad[11] = 0x08; bad[12] = 0xC8;
    assert(pe_rand_check(t, N, PE_RAND_HARD, test_gift_safe, bad));
    /* an earlier day's chest takes only its late keys */
    for (unsigned i = 0; i < N; ++i) bad[i] = t[i].vanilla;
    bad[0] = 0x0B; bad[14] = 0xC8;
    assert(pe_rand_check(t, N, PE_RAND_MEDIUM, test_gift_safe, bad));
    bad[0] = 0xC8; bad[2] = 0x0B; bad[14] = 0xCA;
    assert(!pe_rand_check(t, N, PE_RAND_MEDIUM, test_gift_safe, bad));
    assert(pe_rand_check(t, N, PE_RAND_EASY, test_gift_safe, bad));
}

/* A needs bit is met by holding the key AND reaching the key's own spot (its pickup may move the
 * story on). Key A's spot needs B; chest X was first reached after A's pickup, so it needs A. With A
 * moved into chest C and B into X, holding A isn't enough: X needs A's spot, which needs B. */
static void needs_mean_the_key_spot_too(void) {
    static const PeRandSpot t[] = {
        {0xC8, PE_RAND_GIFT, 3, 6, PE_RAND_KEY, 2, 0, 0, 0},   /* A, needs B */
        {0xC9, PE_RAND_GIFT, 3, 6, PE_RAND_KEY, 0, 0, 0, 0},   /* B */
        {0x01, PE_RAND_CHEST, 3, 6, 0, 0, ALL, 0, ALL},        /* C */
        {0x02, PE_RAND_CHEST, 3, 6, 0, 1, ALL, 0, ALL},        /* X, needs A */
    };
    enum { N = sizeof t / sizeof t[0] };
    uint16_t layout[N] = {0x01, 0x02, 0xC8, 0xC9};
    uint16_t out[N];
    assert(pe_rand_check(t, N, PE_RAND_HARD, test_gift_safe, layout));
    for (uint32_t seed = 0; seed < 2000; ++seed) {
        PeRandResult r;
        pe_rand_generate(t, N, seed, PE_RAND_HARD, test_gift_safe, out, &r);
        assert(!pe_rand_check(t, N, PE_RAND_HARD, test_gift_safe, out));
        assert(!(out[2] == 0xC8 && out[3] == 0xC9));
    }
}

/* Day 3 has data for all its key spots, day 4 doesn't: day 4's key stays, day 3's move. The key spot
 * without data counts as needing day 3's keys. */
static void partial_logic_data(void) {
    static const PeRandSpot t[] = {
        {0xC8, PE_RAND_GIFT, 3, 6, PE_RAND_KEY, 0, 0, 0, 0},
        {0xC9, PE_RAND_GIFT, 3, 6, PE_RAND_KEY, 1, 0, 0, 0},
        {0xCA, PE_RAND_GIFT, 4, 7, PE_RAND_KEY, UNKNOWN, 0, 0, 0},
        {0x01, PE_RAND_CHEST, 3, 6, 0, 0, ALL, 0, ALL}, {0x02, PE_RAND_CHEST, 3, 6, 0, 0, ALL, 0, ALL},
        {0x03, PE_RAND_CHEST, 4, 7, 0, 3, ALL, 0, ALL}, {0x04, PE_RAND_CHEST, 4, 7, 0, 3, ALL, 0, ALL},
    };
    enum { N = sizeof t / sizeof t[0] };
    uint16_t out[N];
    unsigned moved = 0;
    for (uint32_t seed = 0; seed < 2000; ++seed) {
        PeRandResult r;
        pe_rand_generate(t, N, seed, PE_RAND_MEDIUM, test_gift_safe, out, &r);
        assert(!r.keys_locked && !r.fallback && !pe_rand_check(t, N, PE_RAND_MEDIUM, test_gift_safe, out));
        assert(out[2] == 0xCA);
        moved += r.keys_moved;
    }
    assert(moved > 500);
}

/* --layouts N: the key moves of seeds 1..N on Medium and Hard, one line each (seed, difficulty, then
 * spot=item for every spot that gives a key not its own), for tools/keysolve.py --layouts. */
static void print_key_layouts(unsigned n) {
    static uint16_t out[PE_RAND_SPOTS];
    for (uint32_t seed = 1; seed <= n; ++seed)
        for (int d = PE_RAND_MEDIUM; d <= PE_RAND_HARD; ++d) {
            PeRandResult r;
            pe_rand_generate(g_pe_rand_spots, PE_RAND_SPOTS, seed, d, test_gift_safe, out, &r);
            printf("%u %d", (unsigned)seed, d);
            for (unsigned i = 0; i < PE_RAND_SPOTS; ++i)
                for (unsigned j = 0; j < PE_RAND_SPOTS; ++j)
                    if ((g_pe_rand_spots[j].flags & PE_RAND_KEY) && out[i] == g_pe_rand_spots[j].vanilla && i != j)
                        printf(" %u=%02X", i, out[i]);
            putchar('\n');
        }
}

/* Battle: the foe table is sane, and every seed's plan passes the checks, the same one each time; most slots
 * that can move do, bosses take bosses (or stay, PE_RAND_MOVE_BOSSES 0), and a slot no foe may take stays. */
static void foe_plans(void) {
    static uint16_t plan[PE_RAND_FOES], again[PE_RAND_FOES];
    unsigned movable = 0, moved = 0, seeds = 2000, sources[PE_RAND_FOES] = {0}, used = 0;
    for (unsigned i = 0; i < PE_RAND_FOES; ++i) plan[i] = (uint16_t)i;   /* vanilla passes (and the table's order) */
    assert(!pe_rand_foes_check(g_pe_rand_foes, g_pe_rand_foe_ok[0], PE_RAND_FOES, plan));
    for (unsigned i = 0; i < PE_RAND_FOES; ++i) {
        const PeRandFoe *f = &g_pe_rand_foes[i];
        unsigned any = 0;
        assert(f->entity < 10 && f->block >= 0 && f->blocks >= 1 && f->block + f->blocks <= 8 && f->end <= 8 && f->rows_end <= 8);
        for (unsigned w = 0; w < PE_RAND_FOE_WORDS; ++w) any |= g_pe_rand_foe_ok[i][w];
        assert(!(g_pe_rand_foe_ok[i][i / 32] >> (i % 32) & 1));
        movable += any != 0 && (PE_RAND_MOVE_BOSSES || !f->boss);
        if (f->room == 13 && f->entity == 2) {   /* M013's scripted Rat: its slot takes random foes, it goes nowhere */
            assert(any);
            for (unsigned j = 0; j < PE_RAND_FOES; ++j) assert(!(g_pe_rand_foe_ok[j][i / 32] >> (i % 32) & 1));
        }
    }
    for (uint32_t seed = 0; seed < seeds; ++seed) {
        unsigned n = pe_rand_foes_generate(g_pe_rand_foes, g_pe_rand_foe_ok[0], PE_RAND_FOES, seed, plan);
        assert(!pe_rand_foes_check(g_pe_rand_foes, g_pe_rand_foe_ok[0], PE_RAND_FOES, plan));
        assert(pe_rand_foes_generate(g_pe_rand_foes, g_pe_rand_foe_ok[0], PE_RAND_FOES, seed, again) == n);
        assert(!memcmp(plan, again, sizeof plan));
        for (unsigned i = 0; i < PE_RAND_FOES; ++i) {
            if (plan[i] != i) ++sources[plan[i]];
            assert(PE_RAND_MOVE_BOSSES || !g_pe_rand_foes[i].boss || plan[i] == i);
        }
        moved += n;
    }
    for (unsigned i = 0; i < PE_RAND_FOES; ++i) used += sources[i] != 0;
    printf("foes: %u in the table, %u can move, %.1f moved a seed on average, %u used as a source\n", PE_RAND_FOES,
           movable, moved / (double)seeds, used);
    assert(moved >= seeds * (movable - movable / 10));   /* VRAM keeps a few slots home, not many */
}

/* --plan SEED: the battle plan of that seed, one line per foe in table order (index, room, entity, boss, then
 * the foe whose script and model its slot takes), for tools/foeruns.py. */
static void print_foe_plan(uint32_t seed) {
    static uint16_t plan[PE_RAND_FOES];
    pe_rand_foes_generate(g_pe_rand_foes, g_pe_rand_foe_ok[0], PE_RAND_FOES, seed, plan);
    for (unsigned i = 0; i < PE_RAND_FOES; ++i) {
        const PeRandFoe *f = &g_pe_rand_foes[i], *b = &g_pe_rand_foes[plan[i]];
        printf("foe\t%u\tM%03u\t%u\t%u\tM%03u\t%u\t%s\t%s\n", i, f->room, f->entity, f->boss, b->room, b->entity, f->name, b->name);
    }
}

int main(int argc, char **argv) {
    if (argc == 3 && !strcmp(argv[1], "--layouts")) { print_key_layouts((unsigned)atoi(argv[2])); return 0; }
    if (argc == 3 && !strcmp(argv[1], "--plan")) { print_foe_plan((uint32_t)strtoul(argv[2], NULL, 10)); return 0; }
    table_is_sane();
    every_seed_passes();
    foe_plans();
    if (PE_RAND_MOVE_KEYS) {
        keys_move_with_logic_data();
        needs_mean_the_key_spot_too();
        partial_logic_data();
        golden_layouts();
    }
    puts("pe_randomizer: ok");
    return 0;
}
