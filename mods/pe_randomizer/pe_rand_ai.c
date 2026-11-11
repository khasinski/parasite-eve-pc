/* Battle randomizer, AI: an enemy actor of one room runs another actor's script.
 *
 * An actor of type E spawns with script_base = bank_reset_944[0]->offsets[E] (frame_update.c), the room's
 * task-block RelocBlock built at each room load. Each actor type has its own block there; every jump, label
 * and data ref is script_base + v*2, so a block runs from anywhere: no relocation. So a swap, done when the
 * room's banks are filled (PE_HOOK_PLAYER_ROWS, every room load, before the actors spawn): read the source
 * room from the disc, copy the source actor's block (and the rest of its task block after it) and point
 * offsets[E] at the copy. The spawner stays this room's (its spawns, formation, win counter, story flags).
 *
 * In the copy, the entity operands (tools/enemyai.py ENTITY_ARG: the type operand of ops 8, 14, 28, 54, 69,
 * 75, 84, 94, 139, 152, 164, 197, 206, 212; ops 17/237 sub commands 2500/3000; op 234 command 0x160) that name
 * the source actor's own number (its siblings) become E, and those that name its spawner become this room's
 * spawner of E (op 28 is an entity event, how an enemy signals its spawner). Only the steps the actor can reach
 * are read, walked as the rooms ctest does (blocks keep data between their routines): a block whose walk leaves
 * the room's code is left alone.
 *
 * N vars (g_ScriptNodeActiveFlags, zeroed at each room load) are shared by all of a room's scripts. Spawners run
 * the same fight routine on different vars (M012 N0x0/4/1/2, Chrysler M391 N0x3/7/4/5; a Plant there waited on
 * N0x3 in M012 and its fight never went on): each foe's setup adds its level, its value and 1 to three of them,
 * and a spawner with the fight's opening takes the mean level and value (N / count: no guard, a count left at 0
 * would divide by zero) for Aya's opening AT gauge. Between two such spawners the vars the source shares with
 * its spawner go by what they are for, read off each spawner (pe_roles): level, value, count and the flags to
 * the slot's, and the ones the spawner only zeroes for its foes (their working vars) to E's own, in the order
 * each block first uses them, as far as E's go. The added level, value and count become E's own too (the count
 * with what the parts E spawns at home add: a Bear and its part count as 2), so the slot's fight opens as it does
 * at home, whatever the group. Any other pair (M013's scripted Rat has no opening): the
 * shared vars go k-th for k-th if the two lists are as long, else those both spawners use stay.
 * The source's others that a script of this room uses move to ones nothing here uses (mode 4 operands in the
 * copy), but N0xa (the stat scale every enemy copies from V0x50). Those aren't zeroed between two fights of a room.
 * The working vars E's own don't cover (a rich foe in a plain slot: up to 12, mostly bit words the foe sets and
 * tests, ops 40-42, such as "one of us is attacking") are zeroed by the source's spawner right before it spawns
 * its foes; here nothing would, and a second fight in the room would start with the last one's bits. So while no
 * actor of type E is alive (each frame, field or battle), they are kept at 0: the first one spawns on zeros.
 *
 * A script also carries its room's places (floor polygons, the points a foe hops or flies to, the floor's
 * height): in the copy they go from the source room's battle floor onto this room's (see "Places" below), else
 * a Frog of the sewers leaps out of a park to the sewer's coordinates and nothing can reach it.
 *
 * The script slots (PM commands, effect programs) the source room installs and this room doesn't are
 * installed too, from the source room's records as its own load does it (each slot number has one handler in
 * every room); at the next room load, each one that room doesn't install is set back as it was.
 */
#include <stdlib.h>
#include <string.h>
#include "pe_mod_host.h"
#include "pe_game.h"
#include "pe_hooks.h"
#include "pe_disc_assets.h"
#include "pe1/game_state.h"
#include "pe1/scene_room.h"
#include "pe1/field_actor.h"
#include "pe1/script_node.h"
#include "pe_randomizer.h"

#define MOD_ID "pe_randomizer"
enum { PE_RAND_AIS = 512, PE_RAND_TYPES = 10, PE_RAND_AI_SLOTS = 32, PE_RAND_N_VARS = 64 };

typedef struct PeRandAiMap {
    uint16_t room, from_room;
    uint8_t entity, from_entity;
} PeRandAiMap;
static PeRandAiMap s_map[PE_RAND_AIS];
static unsigned s_map_count;
/* The copies given at the last room load (per actor type), and the slots installed with them. */
static uint8_t *s_copy[PE_RAND_TYPES];
static unsigned s_copy_count;
static struct { unsigned slot; const void *was, *now; } s_slot[PE_RAND_AI_SLOTS];
static unsigned s_slot_count;
/* The N vars the copies given at this load use (after their moves). */
static uint64_t s_n_copies;
static uint64_t s_stale[PE_RAND_TYPES];   /* E's copy's working vars kept at 0 while no E is alive */
static uint8_t s_stale_alive[PE_RAND_TYPES], s_stale_groups[PE_RAND_TYPES];

static uint32_t pe_word(const uint8_t *p, uint32_t at) { uint32_t v; memcpy(&v, p + at, 4); return v; }

/* A room blob's task block: its bytes and length. */
static const uint8_t *pe_tasks(const uint8_t *blob, uint32_t *bytes) {
    const SceneRoomDirectory *dir = (const void *)(blob + pe_word(blob, 4));
    const uint32_t record = dir->taskBlock & 0x3FFFFF;
    *bytes = pe_word(blob, record) & 0xFFFFFF;
    return blob + (pe_word(blob, record + 4) & 0xFFFFFF);
}

/* Actor type t's entry in a task block; 0 if it has none in the code (which ends at the block's first word). */
static int pe_entry(const uint8_t *tasks, uint32_t bytes, unsigned t, uint32_t *start) {
    if (t >= pe_word(tasks, 4) || 8 + 4 * t + 4 > bytes) return 0;
    *start = pe_word(tasks, 8 + 4 * t);
    return *start < pe_word(tasks, 0) && *start % 4 == 0;
}

/* The operand of a step that names an entity type, or -1. */
static int pe_entity_arg(const uint32_t *step) {
    const uint32_t op = step[0] & 0x1FFF, count = (step[0] >> 13) & 15;
    int idx;
    switch (op) {
    case 8: case 28: case 54: case 69: case 75: case 84: case 139: case 152: case 164: case 197: case 206: case 212:
        idx = 0; break;
    case 14: case 94: idx = 1; break;
    case 17: case 237: idx = count > 1 && !(step[0] >> 17 & 7) && (step[2] == 2500 || step[2] == 3000) ? 1 : -1; break;
    case 234: idx = count > 2 && !(step[0] >> 17 & 7) && step[2] == 0x160 ? 2 : -1; break;
    default: idx = -1;
    }
    /* a constant only (mode 0); the type operands sit in the header's modes (operands 0-4) */
    return idx >= 0 && (uint32_t)idx < count && !(step[0] >> (17 + 3 * idx) & 7) ? idx : -1;
}

/* Operand k's mode, 3 bits each: operands 0-4 in the header from bit 17, the rest in the tail word. */
static unsigned pe_mode(const uint32_t *step, unsigned k) {
    return k < 5 ? step[0] >> (17 + 3 * k) & 7 : step[1] >> (3 * (k - 5)) & 7;
}

/* The steps reached from `entry` (code before `code_end`, offsets in `block`), each once, as the rooms ctest walks
 * them: script offsets are halfwords from the entry; code targets of op 0 goto, 5 jump if zero (not on a constant
 * nonzero), 18 child routine, 20 event routine (selector 1-3), 29 branch; a routine ends at op 0, 1 or 32, at the
 * FFFFFFFF word between routines, or at a door (op 49) followed by it. 0 if a step lies outside the code. */
static int pe_walk(const uint8_t *block, uint32_t entry, uint32_t code_end, void (*fn)(uint32_t *, uint32_t, void *), void *user) {
    uint8_t *seen = calloc(code_end / 4 + 1, 1);
    uint32_t *work = malloc((code_end / 8 + 2) * sizeof(uint32_t)), top = 0;
    int ok = seen && work;
    if (ok) work[top++] = entry;
    while (ok && top) {
        uint32_t at = work[--top];
        for (;;) {
            if (at % 4 || at + 4 > code_end) { ok = 0; break; }
            if (seen[at / 4] || pe_word(block, at) == 0xFFFFFFFFu) break;
            uint32_t *step = (uint32_t *)(block + at);
            const uint32_t op = step[0] & 0x1FFF, count = (step[0] >> 13) & 15, bytes = 8 + 4 * count;
            if (at + bytes > code_end) { ok = 0; break; }
            seen[at / 4] = 1;
            int target = -1;
            if (op == 0 || op == 18) target = 0;
            else if (op == 5 && !(pe_mode(step, 0) == 0 && count && step[2])) target = 1;
            else if (op == 29) target = 2;
            else if (op == 20 && count > 1 && pe_mode(step, 0) == 0 && step[2] >= 1 && step[2] <= 3) target = 1;
            if (target >= 0 && (uint32_t)target < count && pe_mode(step, (unsigned)target) == 0 && step[2 + target] < 0x80000000u &&
                top < code_end / 8 + 2)
                work[top++] = entry + step[2 + target] * 2;
            if (fn) fn(step, at, user);
            if (op == 0 || op == 1 || op == 32) break;
            if (op == 49 && at + bytes + 4 <= code_end && pe_word(block, at + bytes) == 0xFFFFFFFFu) break;
            at += bytes;
        }
    }
    free(seen);
    free(work);
    return ok;
}

typedef struct PeSpawner { unsigned type, found; } PeSpawner;
static void pe_spawn_step(uint32_t *step, uint32_t at, void *user) {
    PeSpawner *s = user;
    const uint32_t op = step[0] & 0x1FFF;
    (void)at;
    if ((op == 8 && pe_entity_arg(step) == 0 && step[2] == s->type) ||
        (op == 17 && pe_entity_arg(step) == 1 && step[2] == 2500 && step[3] == s->type)) s->found = 1;
}

/* The one actor that spawns type t (op 8, op 17 2500) in a task block; -1 if none or more than one. */
static int pe_spawner(const uint8_t *tasks, uint32_t bytes, unsigned t) {
    const uint32_t count = pe_word(tasks, 4);
    int who = -1;
    for (unsigned e = 0; e < count; ++e) {
        uint32_t start;
        PeSpawner s = {t, 0};
        if (!pe_entry(tasks, bytes, e, &start)) continue;
        pe_walk(tasks, start, pe_word(tasks, 0), pe_spawn_step, &s);
        if (s.found) { if (who >= 0) return -1; who = (int)e; }
    }
    return who;
}

static void pe_n_step(uint32_t *step, uint32_t at, void *user) {
    uint64_t *used = user;
    (void)at;
    for (unsigned k = 0; k < ((step[0] >> 13) & 15); ++k)
        if (pe_mode(step, k) == 4 && step[2 + k] < PE_RAND_N_VARS) *used |= 1ull << step[2 + k];
}

/* The N vars (mode 4 operands, g_ScriptNodeActiveFlags) actor type t's block uses; 0 if it has none. */
static uint64_t pe_n_used(const uint8_t *tasks, uint32_t bytes, int t) {
    uint32_t start;
    uint64_t used = 0;
    if (t >= 0 && pe_entry(tasks, bytes, (unsigned)t, &start)) pe_walk(tasks, start, pe_word(tasks, 0), pe_n_step, &used);
    return used;
}

/* The N vars actor t shares with spawner s (not N0xa), in the order t's block first uses them; their count. */
typedef struct PeFirst { uint32_t at[PE_RAND_N_VARS]; } PeFirst;
static void pe_first_step(uint32_t *step, uint32_t at, void *user) {
    PeFirst *f = user;
    for (unsigned k = 0; k < ((step[0] >> 13) & 15); ++k)
        if (pe_mode(step, k) == 4 && step[2 + k] < PE_RAND_N_VARS && at < f->at[step[2 + k]]) f->at[step[2 + k]] = at;
}
static unsigned pe_fight_vars(const uint8_t *tasks, uint32_t bytes, int t, int s, uint8_t *out) {
    uint64_t shared = pe_n_used(tasks, bytes, t) & pe_n_used(tasks, bytes, s) & ~(1ull << 10);
    PeFirst f;
    uint32_t start;
    unsigned n = 0;
    if (!shared || !pe_entry(tasks, bytes, (unsigned)t, &start)) return 0;
    memset(f.at, 0xff, sizeof f.at);
    pe_walk(tasks, start, pe_word(tasks, 0), pe_first_step, &f);
    for (; shared; ++n) {
        unsigned best = PE_RAND_N_VARS;
        for (unsigned i = 0; i < PE_RAND_N_VARS; ++i)
            if ((shared >> i & 1) && (best == PE_RAND_N_VARS || f.at[i] < f.at[best])) best = i;
        out[n] = (uint8_t)best;
        shared &= ~(1ull << best);
    }
    return n;
}

/* What a spawner's vars are for: its opening (op 206 on Aya's AT gauge, field 6) subtracts `level` from Aya's level,
 * shifts `value` up by 16 and divides both by `count`; `flags` is the var it tests bits of. `used`: the N vars it
 * uses; `other`: those it does more with than setting them to 0. */
typedef struct PeRoles { int opening, level, value, count, flags; uint64_t used, other; } PeRoles;
static void pe_roles_step(uint32_t *step, uint32_t at, void *user) {
    PeRoles *r = user;
    const uint32_t op = step[0] & 0x1FFF, count = (step[0] >> 13) & 15;
    (void)at;
    for (unsigned k = 0; k < count; ++k)
        if (pe_mode(step, k) == 4 && step[2 + k] < PE_RAND_N_VARS) {
            r->used |= 1ull << step[2 + k];
            if (!(op == 10 && count == 2 && k == 0 && pe_mode(step, 1) == 0 && step[3] == 0)) r->other |= 1ull << step[2 + k];
        }
    if (op == 206 && count >= 3 && !pe_mode(step, 0) && !pe_mode(step, 1) && !pe_mode(step, 2) && !step[2] && !step[3] && step[4] == 6)
        r->opening = 1;
    if (op != 9 || count != 4 || pe_mode(step, 0) || pe_mode(step, 1) != 3) return;
    const unsigned a = pe_mode(step, 2), b = pe_mode(step, 3);
    if (step[2] == 1 && a == 1 && b == 4) r->level = (int)step[5];
    else if (step[2] == 0x11 && a == 4 && b == 0 && step[5] == 0x10) r->value = (int)step[4];
    else if (step[2] == 0x10 && a == 4 && b == 4) r->count = (int)step[5];
    else if (step[2] == 3 && a == 4 && b == 0) r->flags = (int)step[4];
}
static void pe_roles(const uint8_t *tasks, uint32_t bytes, int spawner, PeRoles *r) {
    uint32_t start;
    *r = (PeRoles){0, -1, -1, -1, -1, 0, 0};
    if (spawner >= 0 && pe_entry(tasks, bytes, (unsigned)spawner, &start)) pe_walk(tasks, start, pe_word(tasks, 0), pe_roles_step, r);
    r->opening = r->opening && r->level >= 0 && r->level < PE_RAND_N_VARS && r->value >= 0 && r->value < PE_RAND_N_VARS &&
                 r->count >= 0 && r->count < PE_RAND_N_VARS && r->flags < PE_RAND_N_VARS;
}

/* n_to for the foe's shared vars (fight_from; E's: fight_to) between two spawners with the opening; the vars it maps. */
static uint64_t pe_map_roles(const PeRoles *from, const PeRoles *to, const uint8_t *fight_from, unsigned fights,
                             const uint8_t *fight_to, unsigned own_fights, uint8_t *n_to, uint64_t *left) {
    const int was[4] = {from->level, from->value, from->count, from->flags}, now[4] = {to->level, to->value, to->count, to->flags};
    const uint64_t from_zeroed = from->used & ~from->other, to_zeroed = to->used & ~to->other;
    uint64_t was_roles = 0, now_roles = 0, mapped = 0;
    unsigned k = 0;
    for (unsigned r = 0; r < 4; ++r) {
        if (was[r] >= 0) was_roles |= 1ull << was[r];
        if (now[r] >= 0) now_roles |= 1ull << now[r];
    }
    for (unsigned i = 0; i < fights; ++i)
        for (unsigned r = 0; r < 4; ++r)
            if (was[r] == fight_from[i] && now[r] >= 0) { n_to[was[r]] = (uint8_t)now[r]; mapped |= 1ull << was[r]; }
    for (unsigned i = 0; i < fights; ++i) {
        const unsigned v = fight_from[i];
        if ((was_roles >> v & 1) || !(from_zeroed >> v & 1)) continue;
        while (k < own_fights && ((now_roles >> fight_to[k] & 1) || !(to_zeroed >> fight_to[k] & 1))) ++k;
        if (k == own_fights) { *left |= 1ull << v; continue; }
        n_to[v] = fight_to[k++];
        mapped |= 1ull << v;
    }
    return mapped;
}

/* A foe's setup adding a number to N var `n`: op 9 T = N + k, then op 10 N = T. Reads k, or with `set` writes it. */
typedef struct PeAdd { unsigned n, found, set; uint32_t k, code_end; } PeAdd;
static void pe_add_step(uint32_t *step, uint32_t at, void *user) {
    PeAdd *a = user;
    const uint32_t bytes = 8 + 4 * ((step[0] >> 13) & 15);
    if ((step[0] & 0x1FFF) != 9 || bytes != 24 || pe_mode(step, 0) || step[2] || pe_mode(step, 1) != 3 ||
        pe_mode(step, 2) != 4 || step[4] != a->n || pe_mode(step, 3) || at + bytes + 16 > a->code_end) return;
    const uint32_t *next = step + bytes / 4;
    if ((next[0] & 0x1FFF) != 10 || ((next[0] >> 13) & 15) != 2 || pe_mode(next, 0) != 4 || next[2] != a->n ||
        pe_mode(next, 1) != 3 || next[3] != step[3]) return;
    if (a->set) step[5] = a->k;
    else if (!a->found) a->k = step[5];
    ++a->found;
}

/* What the parts E's own block spawns (op 8, another type with a block here: the Bear's) add to var n. A foe that
 * takes the slot spawns none of them. */
typedef struct PeParts { const uint8_t *tasks; uint32_t bytes, code_end; unsigned self, n; int spawner; uint32_t sum; } PeParts;
static void pe_parts_step(uint32_t *step, uint32_t at, void *user) {
    PeParts *p = user;
    uint32_t start;
    (void)at;
    if ((step[0] & 0x1FFF) != 8 || pe_entity_arg(step) != 0 || !step[2] || step[2] == p->self || (int)step[2] == p->spawner ||
        !pe_entry(p->tasks, p->bytes, step[2], &start)) return;
    PeAdd add = {p->n, 0, 0, 0, p->code_end};
    pe_walk(p->tasks, start, p->code_end, pe_add_step, &add);
    if (add.found) p->sum += add.k;
}

/* The copy adds E's own number (its block adds it to own_n, and so do the parts it spawns) where it adds to var n;
 * 0 if either adds none. */
static int pe_add_own(uint8_t *copy, uint32_t copy_end, unsigned n, const uint8_t *mine, uint32_t bytes, uint32_t own_at,
                      uint32_t own_end, unsigned self, int spawner, unsigned own_n, uint32_t *was, uint32_t *now) {
    PeParts parts = {mine, bytes, own_end, self, own_n, spawner, 0};
    PeAdd own = {own_n, 0, 0, 0, own_end}, theirs = {n, 0, 0, 0, copy_end};
    pe_walk(mine, own_at, own_end, pe_add_step, &own);
    pe_walk(copy, 0, copy_end, pe_add_step, &theirs);
    if (!own.found || !theirs.found) return 0;
    pe_walk(mine, own_at, own_end, pe_parts_step, &parts);
    *was = theirs.k;
    *now = theirs.k = own.k + parts.sum;
    theirs.set = 1;
    pe_walk(copy, 0, copy_end, pe_add_step, &theirs);
    return 1;
}

typedef struct PePatch {
    unsigned self, to_self; int spawner, to_spawner; unsigned done;
    uint8_t n_to[PE_RAND_N_VARS]; unsigned n_done;
} PePatch;
static void pe_patch_step(uint32_t *step, uint32_t at, void *user) {
    PePatch *p = user;
    const int idx = pe_entity_arg(step);
    (void)at;
    for (unsigned k = 0; k < ((step[0] >> 13) & 15); ++k)
        if (pe_mode(step, k) == 4 && step[2 + k] < PE_RAND_N_VARS && p->n_to[step[2 + k]] != step[2 + k]) {
            step[2 + k] = p->n_to[step[2 + k]];
            ++p->n_done;
        }
    if (idx < 0) return;
    uint32_t *v = &step[2 + idx];
    if (*v == p->self && p->self != p->to_self) { *v = p->to_self; ++p->done; }
    else if (p->spawner >= 0 && *v == (uint32_t)p->spawner && p->spawner != p->to_spawner) { *v = (uint32_t)p->to_spawner; ++p->done; }
}

/* The lead sub id: a source actor sends its spawner event 0x70 (which brings the battle in) only when its sub id
 * (op 38) is K: op 9 `T = A == K`, op 5 jump if zero T, op 28 (spawner, 0, 0x70), three steps in a row. gate:
 * those op 9 steps. */
typedef struct PeLead { uint64_t subs; int lead; uint32_t at[2]; const uint32_t *step[2]; uint32_t *gate[4]; unsigned gates; } PeLead;
static void pe_sub_step(uint32_t *step, uint32_t at, void *user) {
    PeLead *l = user;
    (void)at;
    if ((step[0] & 0x1FFF) == 38 && (step[0] >> 13 & 15) && pe_mode(step, 0) == 1 && step[2] < 64) l->subs |= 1ull << step[2];
}
static void pe_lead_step(uint32_t *step, uint32_t at, void *user) {
    PeLead *l = user;
    const uint32_t op = step[0] & 0x1FFF, count = (step[0] >> 13) & 15;
    const uint32_t *a = l->step[0], *b = l->step[1];
    if (op == 28 && count > 2 && pe_mode(step, 2) == 0 && step[4] == 0x70 && a && b &&
        l->at[1] + 8 + 4 * ((b[0] >> 13) & 15) == at && l->at[0] + 8 + 4 * ((a[0] >> 13) & 15) == l->at[1] &&
        (a[0] & 0x1FFF) == 9 && ((a[0] >> 13) & 15) == 4 && pe_mode(a, 0) == 0 && a[2] == 0xb && pe_mode(a, 2) == 1 &&
        a[4] < 64 && (l->subs >> a[4] & 1) && pe_mode(a, 3) == 0 &&
        (b[0] & 0x1FFF) == 5 && ((b[0] >> 13) & 15) && pe_mode(b, 0) == pe_mode(a, 1) && b[2] == a[3]) {
        l->lead = (int)a[5];
        if (l->gates < 4) l->gate[l->gates++] = (uint32_t *)a;
    }
    l->at[0] = l->at[1]; l->step[0] = l->step[1];
    l->at[1] = at; l->step[1] = step;
}

/* An actor's spawns of type t: op 8 (SpawnEntityAt: type, sub id, x, y, z) and op 17 2500; `set` >= 0 sets the
 * op 8 ones' sub id. */
typedef struct PeSpawn { unsigned type, by8, by17, sub; int set; } PeSpawn;
static void pe_op8_step(uint32_t *step, uint32_t at, void *user) {
    PeSpawn *s = user;
    const uint32_t op = step[0] & 0x1FFF;
    (void)at;
    if (op == 8 && ((step[0] >> 13) & 15) > 1 && pe_mode(step, 0) == 0 && pe_mode(step, 1) == 0 && step[2] == s->type) {
        ++s->by8;
        s->sub = step[3];
        if (s->set >= 0) step[3] = (uint32_t)s->set;
    } else if ((op == 8 && pe_entity_arg(step) == 0 && step[2] == s->type) ||
               (op == 17 && pe_entity_arg(step) == 1 && step[2] == 2500 && step[3] == s->type)) ++s->by17;
}

/* Places. A foe's script is written for its own room: polygons in its block's data (op 226: x, z, vertex count,
 * offset: the floor it may stand on, sectors of it, an inner ring), points it goes to (two op 10 `A = constant` in
 * a row into a pair of A vars some step takes as x and z; constant operands of op 11 selectors 0 and 1, of op 107
 * commands 17 and 22 and of op 119's quad; a moved point's number again in an op 9 sum) and its floor's height
 * (the y of op 107 command 17, the target of a hop or a flight, and of op 11). All of it lies on the floor polygon
 * the room's scripts give its battles (op 227). In a copy, what lies in the source floor's box goes into a box
 * inside this room's floor, each axis by its own scale, so a polygon stays around its points; the height becomes
 * this room's (where the slot's spawner puts the foe), or -1 (command 17: the height the actor is at) when the
 * spawner uses more than one. Polygons and points off the source floor (other rooms' that the scripts carry
 * along) and points within 16 units of 0,0 (vars set to 0) stay. */
enum { PE_RAND_POLY_MAX = 64, PE_RAND_PLACE_PAIRS = 16, PE_RAND_PLACES = 48 };
typedef struct PeBox { int32_t x0, z0, x1, z1; } PeBox;

static int32_t pe_coord(const uint8_t *vertex, unsigned at) { int16_t v; memcpy(&v, vertex + at, 2); return v; }

/* (x, z) inside a polygon of n 8-byte vertices (16.16 x and z: whole units at +2 and +6). */
static int pe_in_poly(const uint8_t *v, unsigned n, int32_t x, int32_t z) {
    int in = 0;
    for (unsigned i = 0, j = n - 1; i < n; j = i++) {
        const int32_t xi = pe_coord(v, 8 * i + 2), zi = pe_coord(v, 8 * i + 6), xj = pe_coord(v, 8 * j + 2), zj = pe_coord(v, 8 * j + 6);
        const int64_t a = (int64_t)(x - xi) * (zj - zi), b = (int64_t)(xj - xi) * (z - zi);
        if ((zi > z) != (zj > z) && (zj > zi ? a < b : a > b)) in = !in;
    }
    return in;
}

static void pe_poly_box(const uint8_t *v, unsigned n, PeBox *b) {
    *b = (PeBox){pe_coord(v, 2), pe_coord(v, 6), pe_coord(v, 2), pe_coord(v, 6)};
    for (unsigned i = 1; i < n; ++i) {
        const int32_t x = pe_coord(v, 8 * i + 2), z = pe_coord(v, 8 * i + 6);
        if (x < b->x0) b->x0 = x;
        if (x > b->x1) b->x1 = x;
        if (z < b->z0) b->z0 = z;
        if (z > b->z1) b->z1 = z;
    }
}

static int pe_box_in_poly(const uint8_t *v, unsigned n, const PeBox *t) {
    for (int i = 0; i <= 16; ++i) {
        const int32_t x = t->x0 + (t->x1 - t->x0) * i / 16, z = t->z0 + (t->z1 - t->z0) * i / 16;
        if (!pe_in_poly(v, n, x, t->z0) || !pe_in_poly(v, n, x, t->z1) || !pe_in_poly(v, n, t->x0, z) || !pe_in_poly(v, n, t->x1, z))
            return 0;
    }
    return 1;
}

/* The largest box that lies inside the polygon (its sides, 17 points each): centred on one of 9 x 9 places of the
 * polygon's box, the middle first, each side 1 to 15 sixteenths of that box's and 128 units or more; 0 if none
 * (a floor can be a bent corridor: no box in its own proportions fits Central Park's M067). */
static int pe_inner_box(const uint8_t *v, unsigned n, PeBox *out) {
    PeBox all;
    int64_t best = 0;
    pe_poly_box(v, n, &all);
    const int32_t w = all.x1 - all.x0, d = all.z1 - all.z0;
    for (int c = 0; c < 82; ++c) {
        /* 0 is the middle, then the rest in rows */
        const int col = c ? (c - 1) % 9 : 4, row = c ? (c - 1) / 9 : 4;
        const int32_t cx = all.x0 + w * (col + 1) / 10, cz = all.z0 + d * (row + 1) / 10;
        if (!pe_in_poly(v, n, cx, cz)) continue;
        for (int kx = 15; kx > 0; --kx)
            for (int kz = 15; kz > 0; --kz) {
                const PeBox t = {cx - w * kx / 32, cz - d * kz / 32, cx + w * kx / 32, cz + d * kz / 32};
                const int64_t area = (int64_t)(t.x1 - t.x0) * (t.z1 - t.z0);
                if (area <= best || t.x1 - t.x0 < 128 || t.z1 - t.z0 < 128) break;
                if (pe_box_in_poly(v, n, &t)) { best = area; *out = t; break; }
            }
    }
    return best > 0;
}

/* The floor polygon a room's scripts give its battles (op 227: offset in the actor's block, vertex count): its
 * vertices in the task block; NULL unless there is one. */
typedef struct PeFloor { uint32_t base, at, count; unsigned found; } PeFloor;
static void pe_floor_step(uint32_t *step, uint32_t at, void *user) {
    PeFloor *f = user;
    (void)at;
    if ((step[0] & 0x1FFF) != 227 || ((step[0] >> 13) & 15) < 2 || pe_mode(step, 0) || pe_mode(step, 1) || f->found++) return;
    f->at = f->base + step[2] * 2;
    f->count = step[3];
}
static const uint8_t *pe_floor(const uint8_t *tasks, uint32_t bytes, unsigned *n) {
    PeFloor f = {0, 0, 0, 0};
    for (unsigned e = 0; e < pe_word(tasks, 4); ++e)
        if (pe_entry(tasks, bytes, e, &f.base)) pe_walk(tasks, f.base, pe_word(tasks, 0), pe_floor_step, &f);
    if (f.found != 1 || f.count < 3 || f.count > PE_RAND_POLY_MAX || f.at > bytes || 8 * f.count > bytes - f.at) return NULL;
    *n = f.count;
    return tasks + f.at;
}

/* The height a spawner's op 8 steps put type t at (operand 3, 16.16); 0 if none, not a constant or not the same. */
typedef struct PeHeight { unsigned type, found, differ; int32_t y; } PeHeight;
static void pe_height_step(uint32_t *step, uint32_t at, void *user) {
    PeHeight *h = user;
    (void)at;
    if ((step[0] & 0x1FFF) != 8 || ((step[0] >> 13) & 15) < 5 || pe_mode(step, 0) || step[2] != h->type) return;
    if (pe_mode(step, 3) || (h->found && h->y != (int32_t)step[5])) h->differ = 1;
    h->y = (int32_t)step[5];
    ++h->found;
}
static int pe_height(const uint8_t *tasks, uint32_t bytes, int spawner, unsigned t, int32_t *y) {
    PeHeight h = {t, 0, 0, 0};
    uint32_t start;
    if (spawner < 0 || !pe_entry(tasks, bytes, (unsigned)spawner, &start)) return 0;
    pe_walk(tasks, start, pe_word(tasks, 0), pe_height_step, &h);
    *y = h.y;
    return h.found && !h.differ;
}

typedef struct PeGeo {
    PeBox from, to;                 /* the source room's floor's box; the box inside this room's floor */
    int32_t y_from, y_to;           /* the floor's height there and here */
    int y_known, y_to_known;
    uint8_t *copy, *done;           /* done: the vertices moved, by address / 2 */
    uint32_t bytes;
    uint16_t pair[PE_RAND_PLACE_PAIRS];   /* A vars a step takes as x and z */
    int32_t xs[PE_RAND_PLACES], zs[PE_RAND_PLACES];   /* the constant points moved, as they were */
    unsigned pairs, places;
    uint32_t *last, last_at, *paired;
    unsigned polygons, points, heights;
} PeGeo;

static int32_t pe_geo_axis(int32_t v, int32_t from0, int32_t from1, int32_t to0, int32_t to1) {
    return (int32_t)(to0 * 65536ll + (v - from0 * 65536ll) * (to1 - to0) / (from1 - from0));
}
static int32_t pe_geo_x(const PeGeo *g, int32_t x) { return pe_geo_axis(x, g->from.x0, g->from.x1, g->to.x0, g->to.x1); }
static int32_t pe_geo_z(const PeGeo *g, int32_t z) { return pe_geo_axis(z, g->from.z0, g->from.z1, g->to.z0, g->to.z1); }

/* A 16.16 point on the source floor: in its box or within an eighth of it (+ 64) around. */
static int pe_geo_in(const PeGeo *g, int32_t x, int32_t z) {
    const int32_t mx = (g->from.x1 - g->from.x0) / 8 + 64, mz = (g->from.z1 - g->from.z0) / 8 + 64;
    x /= 65536;
    z /= 65536;
    return x >= g->from.x0 - mx && x <= g->from.x1 + mx && z >= g->from.z0 - mz && z <= g->from.z1 + mz;
}
static int pe_geo_small(int32_t x, int32_t z) { return x > -0x100000 && x < 0x100000 && z > -0x100000 && z < 0x100000; }

static void pe_geo_note(PeGeo *g, int32_t x, int32_t z) {
    if (g->places < PE_RAND_PLACES) { g->xs[g->places] = x; g->zs[g->places++] = z; }
    ++g->points;
}

/* A step's point (operands x, y, z; y < 0: none): constants on the source floor move; a constant height that is
 * the source floor's becomes this room's (`hop`: or -1), unless the point is a constant that stays. */
static void pe_geo_point(PeGeo *g, uint32_t *step, unsigned x, int y, unsigned z, int hop) {
    if (z >= ((step[0] >> 13) & 15)) return;
    if (!pe_mode(step, x) && !pe_mode(step, z)) {
        const int32_t px = (int32_t)step[2 + x], pz = (int32_t)step[2 + z];
        if (pe_geo_small(px, pz) || !pe_geo_in(g, px, pz)) return;
        step[2 + x] = (uint32_t)pe_geo_x(g, px);
        step[2 + z] = (uint32_t)pe_geo_z(g, pz);
        pe_geo_note(g, px, pz);
    }
    if (y < 0 || pe_mode(step, (unsigned)y) || !g->y_known || (int32_t)step[2 + y] != g->y_from) return;
    if (g->y_to_known ? g->y_to == g->y_from : !hop) return;
    step[2 + y] = g->y_to_known ? (uint32_t)g->y_to : 0xFFFFFFFFu;
    ++g->heights;
}

static void pe_geo_pair_step(uint32_t *step, uint32_t at, void *user) {
    PeGeo *g = user;
    const uint32_t op = step[0] & 0x1FFF, count = (step[0] >> 13) & 15;
    unsigned x, z;
    (void)at;
    if (op == 226 && count >= 2) { x = 0; z = 1; }
    else if (op == 11 && count >= 4 && !pe_mode(step, 0) && step[2] <= 1) { x = 1; z = 3; }
    else if (op == 107 && count >= 5 && !pe_mode(step, 1) && step[3] == 17) { x = 2; z = 4; }
    else if (op == 119 && count >= 10) { x = 8; z = 9; }
    else return;
    if (pe_mode(step, x) != 1 || pe_mode(step, z) != 1 || step[2 + x] > 255 || step[2 + z] > 255) return;
    const uint16_t pair = (uint16_t)(step[2 + x] << 8 | step[2 + z]);
    for (unsigned i = 0; i < g->pairs; ++i) if (g->pair[i] == pair) return;
    if (g->pairs < PE_RAND_PLACE_PAIRS) g->pair[g->pairs++] = pair;
}

static void pe_geo_step(uint32_t *step, uint32_t at, void *user) {
    PeGeo *g = user;
    const uint32_t op = step[0] & 0x1FFF, count = (step[0] >> 13) & 15;
    uint32_t *last = g->last;
    g->last = NULL;
    if (op == 226 && count >= 4 && !pe_mode(step, 2) && !pe_mode(step, 3)) {
        const uint32_t n = step[4], data = step[5] * 2;
        unsigned on = 0, zero = 0, moved = 0;
        if (n < 3 || n > PE_RAND_POLY_MAX || step[5] >= 0x40000000u || data > g->bytes || 8 * n > g->bytes - data) return;
        for (unsigned i = 0; i < n; ++i) {
            int32_t x, z;
            memcpy(&x, g->copy + data + 8 * i, 4);
            memcpy(&z, g->copy + data + 8 * i + 4, 4);
            if (g->done[(data + 8 * i) / 2]) { ++on; continue; }
            on += (unsigned)pe_geo_in(g, x, z);
            zero += (unsigned)pe_geo_small(x, z);
        }
        if (on < n || zero == n) return;
        for (unsigned i = 0; i < n; ++i) {
            int32_t x, z;
            if (g->done[(data + 8 * i) / 2]) continue;
            memcpy(&x, g->copy + data + 8 * i, 4);
            memcpy(&z, g->copy + data + 8 * i + 4, 4);
            x = pe_geo_x(g, x);
            z = pe_geo_z(g, z);
            memcpy(g->copy + data + 8 * i, &x, 4);
            memcpy(g->copy + data + 8 * i + 4, &z, 4);
            g->done[(data + 8 * i) / 2] = 1;
            ++moved;
        }
        g->polygons += moved != 0;
    } else if (op == 119 && count >= 8) {
        unsigned on = 0, zero = 0;
        for (unsigned i = 0; i < 4; ++i) {
            if (pe_mode(step, 2 * i) || pe_mode(step, 2 * i + 1)) return;
            on += (unsigned)pe_geo_in(g, (int32_t)step[2 + 2 * i], (int32_t)step[3 + 2 * i]);
            zero += (unsigned)pe_geo_small((int32_t)step[2 + 2 * i], (int32_t)step[3 + 2 * i]);
        }
        if (on < 4 || zero == 4) return;
        for (unsigned i = 0; i < 4; ++i) {
            step[2 + 2 * i] = (uint32_t)pe_geo_x(g, (int32_t)step[2 + 2 * i]);
            step[3 + 2 * i] = (uint32_t)pe_geo_z(g, (int32_t)step[3 + 2 * i]);
        }
        ++g->polygons;
    } else if (op == 11 && count >= 4 && !pe_mode(step, 0) && step[2] <= 1) pe_geo_point(g, step, 1, 2, 3, 0);
    else if (op == 107 && count >= 5 && !pe_mode(step, 1) && step[3] == 17) pe_geo_point(g, step, 2, 3, 4, 1);
    else if (op == 107 && count >= 5 && !pe_mode(step, 1) && step[3] == 22) pe_geo_point(g, step, 2, -1, 3, 0);
    else if (op == 10 && count == 2 && pe_mode(step, 0) == 1 && !pe_mode(step, 1) && step[2] < 256) {
        if (last && last != g->paired && g->last_at + 16 == at) {
            const uint16_t pair = (uint16_t)(last[2] << 8 | step[2]);
            const int32_t x = (int32_t)last[3], z = (int32_t)step[3];
            unsigned known = 0;
            for (unsigned i = 0; i < g->pairs; ++i) known |= g->pair[i] == pair;
            if (known && !pe_geo_small(x, z) && pe_geo_in(g, x, z)) {
                last[3] = (uint32_t)pe_geo_x(g, x);
                step[3] = (uint32_t)pe_geo_z(g, z);
                pe_geo_note(g, x, z);
                g->paired = step;
            }
        }
        g->last = step;
        g->last_at = at;
    }
}

/* A moved point's x or z again, in a sum or a difference (op 9 selectors 0 and 1): the middle a foe circles. */
static void pe_geo_sum_step(uint32_t *step, uint32_t at, void *user) {
    PeGeo *g = user;
    (void)at;
    if ((step[0] & 0x1FFF) != 9 || ((step[0] >> 13) & 15) != 4 || pe_mode(step, 0) || step[2] > 1) return;
    for (unsigned k = 2; k < 4; ++k) {
        const int32_t v = (int32_t)step[2 + k];
        int is_x = 0, is_z = 0;
        if (pe_mode(step, k) || pe_geo_small(v, 0)) continue;
        for (unsigned i = 0; i < g->places; ++i) { is_x |= g->xs[i] == v; is_z |= g->zs[i] == v; }
        if (is_x == is_z) continue;
        step[2 + k] = (uint32_t)(is_x ? pe_geo_x(g, v) : pe_geo_z(g, v));
        ++g->points;
    }
}

/* The slots a room blob's directory installs. */
static int pe_room_has_slot(const uint8_t *blob, unsigned slot) {
    const SceneRoomDirectory *dir = (const void *)(blob + pe_word(blob, 4));
    const uint8_t *r = blob + (dir->scripts & 0x3FFFFF);
    for (unsigned i = 0; i < dir->scripts >> 22; ++i, r += 12) if (r[7] == slot) return 1;
    return 0;
}

int pe_rand_ai_force(unsigned room, unsigned entity, unsigned from_room, unsigned from_entity) {
    if (s_map_count == PE_RAND_AIS || entity >= PE_RAND_TYPES || from_entity >= PE_RAND_TYPES) return 0;
    s_map[s_map_count++] = (PeRandAiMap){(uint16_t)room, (uint16_t)from_room, (uint8_t)entity, (uint8_t)from_entity};
    return 1;
}

void pe_rand_ai_clear(void) { s_map_count = 0; }

/* A new load: the copies go (the last room's actors are gone), and each slot installed for them that this
 * room doesn't install is set back. */
static void pe_rand_ai_release(const uint8_t *assets) {
    for (unsigned i = 0; i < PE_RAND_TYPES; ++i) { free(s_copy[i]); s_copy[i] = NULL; s_stale[i] = 0; s_stale_alive[i] = 0; s_stale_groups[i] = 0; }
    s_copy_count = 0;
    s_n_copies = 0;
    for (unsigned i = s_slot_count; i-- > 0;)
        if (SceneRoom_ScriptSlot(s_slot[i].slot) == s_slot[i].now && !pe_room_has_slot(assets, s_slot[i].slot))
            SceneRoom_SetScriptSlot(s_slot[i].slot, s_slot[i].was);
    s_slot_count = 0;
}

int pe_rand_ai_apply(int room) {
    const uint8_t *assets = (const uint8_t *)g_GameState.loaded_scene_assets;
    RelocBlock *tasks = g_GameState.bank_reset_944[0];
    unsigned done = 0;
    if (!assets || !tasks) return 0;
    /* Still the same load (the hook runs twice a load): done already. */
    for (unsigned e = 0; e < PE_RAND_TYPES; ++e)
        if (s_copy[e]) { if (e < tasks->count && tasks->offsets[e] == s_copy[e]) return 0; break; }
    pe_rand_ai_release(assets);
    for (unsigned m = 0; m < s_map_count; ++m) {
        const PeRandAiMap *map = &s_map[m];
        uint32_t bytes, from_bytes, start, task_bytes;
        const uint8_t *mine;
        PeRoomBlobs src;
        if (map->room != room || s_copy[map->entity] || map->entity >= tasks->count) continue;
        mine = pe_tasks(assets, &bytes);
        if (!pe_disc_room(map->from_room, &src)) continue;
        const uint8_t *theirs = pe_tasks(src.data, &from_bytes);
        const int spawner = pe_spawner(theirs, from_bytes, map->from_entity), to_spawner = pe_spawner(mine, bytes, map->entity);
        if (!pe_entry(theirs, from_bytes, map->from_entity, &start) || !pe_walk(theirs, start, pe_word(theirs, 0), NULL, NULL) ||
            (spawner >= 0) != (to_spawner >= 0)) {
            pe_log("rand_ai", "map=%d e%u: M%03u e%u doesn't fit (block, spawner e%d / e%d)", room, map->entity,
                   map->from_room, map->from_entity, spawner, to_spawner);
            pe_disc_room_free(&src);
            continue;
        }
        task_bytes = from_bytes - start;
        uint8_t *copy = malloc(task_bytes);
        if (!copy) { pe_disc_room_free(&src); continue; }
        memcpy(copy, theirs + start, task_bytes);
        PePatch patch = {map->from_entity, map->entity, spawner, to_spawner, 0, {0}, 0};
        /* N vars: the fight routine's (shared with the spawner) as the file's top says; the source's others that
         * this room's scripts use (not E's own block, which no longer runs) move to ones no script here uses.
         * N0xa stays: every enemy sets it to the stat scale V0x50 (pe_rand_battle.c reads it). */
        uint8_t fight_from[PE_RAND_N_VARS], fight_to[PE_RAND_N_VARS];
        const unsigned fights = pe_fight_vars(theirs, from_bytes, map->from_entity, spawner, fight_from);
        const unsigned own_fights = pe_fight_vars(mine, bytes, map->entity, to_spawner, fight_to);
        uint64_t room_n = s_n_copies, keep = 1ull << 10, left = 0;
        PeRoles from_roles, to_roles;
        uint32_t own_block, level_was = 0, level_now = 0, value_was = 0, value_now = 0, count_was = 0, count_now = 0;
        int level_own = 0, value_own = 0;
        pe_roles(theirs, from_bytes, spawner, &from_roles);
        pe_roles(mine, bytes, to_spawner, &to_roles);
        for (unsigned i = 0; i < PE_RAND_N_VARS; ++i) patch.n_to[i] = (uint8_t)i;
        if (from_roles.opening && to_roles.opening) {
            const uint32_t copy_end = pe_word(theirs, 0) - start, own_end = pe_word(mine, 0);
            keep |= pe_map_roles(&from_roles, &to_roles, fight_from, fights, fight_to, own_fights, patch.n_to, &left);
            if (pe_entry(mine, bytes, map->entity, &own_block)) {
                level_own = pe_add_own(copy, copy_end, (unsigned)from_roles.level, mine, bytes, own_block, own_end, map->entity,
                                       to_spawner, (unsigned)to_roles.level, &level_was, &level_now);
                value_own = pe_add_own(copy, copy_end, (unsigned)from_roles.value, mine, bytes, own_block, own_end, map->entity,
                                       to_spawner, (unsigned)to_roles.value, &value_was, &value_now);
                pe_add_own(copy, copy_end, (unsigned)from_roles.count, mine, bytes, own_block, own_end, map->entity,
                           to_spawner, (unsigned)to_roles.count, &count_was, &count_now);
            }
            if (!level_own || !value_own)
                pe_log("rand_ai", "map=%d e%u: M%03u e%u or this slot adds no level or value", room, map->entity, map->from_room, map->from_entity);
        } else if (fights && fights == own_fights)
            for (unsigned i = 0; i < fights; ++i) { patch.n_to[fight_from[i]] = fight_to[i]; keep |= 1ull << fight_from[i]; }
        else {
            keep |= pe_n_used(mine, bytes, to_spawner) & pe_n_used(theirs, from_bytes, spawner);
            if (fights || own_fights)
                pe_log("rand_ai", "map=%d e%u: fight vars don't line up (M%03u e%u %u, this slot %u): the shared ones stay",
                       room, map->entity, map->from_room, map->from_entity, fights, own_fights);
        }
        const uint64_t their_n = pe_n_used(theirs, from_bytes, map->from_entity);
        for (unsigned e = 0; e < pe_word(mine, 4); ++e)
            if (e != map->entity) room_n |= pe_n_used(mine, bytes, (int)e);
        uint64_t free_n = ~(room_n | their_n | keep), moved = 0;
        for (unsigned i = 0; i < PE_RAND_N_VARS; ++i) {
            if (!(their_n & room_n & ~keep & (1ull << i)) || !free_n) continue;
            unsigned to = 0;
            while (!(free_n & (1ull << to))) ++to;
            free_n &= ~(1ull << to);
            patch.n_to[i] = (uint8_t)to;
            ++moved;
        }
        pe_walk(copy, 0, pe_word(theirs, 0) - start, pe_patch_step, &patch);
        /* places: the source room's floor onto this room's */
        PeGeo geo;
        unsigned from_n = 0, to_n = 0, placed = 0;
        memset(&geo, 0, sizeof geo);
        if (map->from_room != room) {
            const uint8_t *from_floor = pe_floor(theirs, from_bytes, &from_n), *to_floor = pe_floor(mine, bytes, &to_n);
            if (from_floor) pe_poly_box(from_floor, from_n, &geo.from);
            geo.done = calloc(task_bytes / 2 + 1, 1);
            /* the same floor (Chrysler floors share theirs): every point stays, only the height goes by this room */
            const int same = from_floor && to_floor && from_n == to_n && !memcmp(from_floor, to_floor, 8 * from_n);
            if (same) geo.to = geo.from;
            if (from_floor && to_floor && geo.done && geo.from.x1 > geo.from.x0 && geo.from.z1 > geo.from.z0 &&
                (same || pe_inner_box(to_floor, to_n, &geo.to))) {
                geo.copy = copy;
                geo.bytes = task_bytes;
                geo.y_known = pe_height(theirs, from_bytes, spawner, map->from_entity, &geo.y_from);
                geo.y_to_known = pe_height(mine, bytes, to_spawner, map->entity, &geo.y_to);
                pe_walk(copy, 0, pe_word(theirs, 0) - start, pe_geo_pair_step, &geo);
                pe_walk(copy, 0, pe_word(theirs, 0) - start, pe_geo_step, &geo);
                pe_walk(copy, 0, pe_word(theirs, 0) - start, pe_geo_sum_step, &geo);
                placed = 1;
            } else
                pe_log("rand_ai", "map=%d e%u: no floor polygon to place M%03u e%u's points on (there %u, here %u vertices)",
                       room, map->entity, map->from_room, map->from_entity, from_n, to_n);
            free(geo.done);
        }
        /* the working vars left over, where they landed: only those no other script here uses */
        uint64_t stale = 0;
        for (unsigned i = 0; i < PE_RAND_N_VARS; ++i)
            if ((left >> i & 1) && !(room_n >> patch.n_to[i] & 1) && patch.n_to[i] != 10) stale |= 1ull << patch.n_to[i];
        for (unsigned i = 0; i < PE_RAND_N_VARS; ++i)
            if (their_n & (1ull << i)) s_n_copies |= 1ull << patch.n_to[i];
        /* the source room's slots this room lacks */
        const SceneRoomDirectory *dir = (const void *)(src.data + pe_word(src.data, 4));
        const uint8_t *r = src.data + (dir->scripts & 0x3FFFFF);
        unsigned slots = 0;
        for (unsigned i = 0; i < dir->scripts >> 22 && s_slot_count < PE_RAND_AI_SLOTS; ++i, r += 12) {
            const void *was = SceneRoom_ScriptSlot(r[7]), *now;
            if (pe_room_has_slot(assets, r[7])) continue;
            unsigned seen = 0;
            for (unsigned k = 0; k < s_slot_count; ++k) seen |= s_slot[k].slot == r[7];
            if (seen || !(now = SceneRoom_InstallRoomScript(map->from_room, r[7], pe_word(r, 8)))) continue;
            s_slot[s_slot_count].slot = r[7]; s_slot[s_slot_count].was = was; s_slot[s_slot_count++].now = now;
            ++slots;
        }
        /* A slot whose own actor sends 0x70 at any sub id and that its spawner places with one op 8 (M013's
         * scripted Rat) gets the sub id the copy needs to bring its battle in (the room's live task block is
         * rebuilt at each load). A slot with a lead of its own keeps its spawns: the copy's lead gates take
         * that sub id instead (M012 places its pair with sub ids 0 and 1, a Chrysler Plant's lead is another:
         * no 0x70, the battle came in with nobody able to move). */
        PeLead lead = {0, -1, {0, 0}, {0, 0}, {0}, 0}, own = {0, -1, {0, 0}, {0, 0}, {0}, 0};
        uint32_t spawner_at, own_at;
        int lead_set = -1, lead_was = -1;
        pe_walk(copy, 0, pe_word(theirs, 0) - start, pe_sub_step, &lead);
        pe_walk(copy, 0, pe_word(theirs, 0) - start, pe_lead_step, &lead);
        if (pe_entry(mine, bytes, map->entity, &own_at)) {
            pe_walk(mine, own_at, pe_word(mine, 0), pe_sub_step, &own);
            pe_walk(mine, own_at, pe_word(mine, 0), pe_lead_step, &own);
        }
        if (lead.lead >= 0 && own.lead < 0 && to_spawner >= 0 && !s_copy[to_spawner] && pe_entry(mine, bytes, (unsigned)to_spawner, &spawner_at)) {
            PeSpawn spawn = {map->entity, 0, 0, 0, -1};
            uint8_t *live = tasks->offsets[to_spawner] - spawner_at;
            pe_walk(live, spawner_at, pe_word(mine, 0), pe_op8_step, &spawn);
            /* one plain op 8 spawn (any other op 8 or op 17 2500 for E: a group, leave it) */
            if (spawn.by8 == 1 && !spawn.by17 && spawn.sub != (uint32_t)lead.lead) {
                lead_set = (int)spawn.sub;
                spawn.set = lead.lead;
                pe_walk(live, spawner_at, pe_word(mine, 0), pe_op8_step, &spawn);
            }
        }
        if (lead.lead >= 0 && own.lead >= 0 && lead.lead != own.lead) {
            for (unsigned k = 0; k < lead.gates; ++k) lead.gate[k][5] = (uint32_t)own.lead;
            lead_was = lead.lead;
        }
        if (lead_was >= 0)
            pe_log("rand_ai", "map=%d e%u lead sub id %#x (the slot's; was %#x)", room, map->entity, own.lead, lead_was);
        if (lead_set >= 0)
            pe_log("rand_ai", "map=%d e%u spawned by e%d's op 8 with sub id %#x (its lead; was %#x)", room, map->entity,
                   to_spawner, lead.lead, lead_set);
        tasks->offsets[map->entity] = s_copy[map->entity] = copy;
        s_stale[map->entity] = stale;
        if (stale)
            pe_log("rand_ai", "map=%d e%u: %u working vars zeroed while no e%u is alive (%#llx)", room, map->entity,
                   (unsigned)__builtin_popcountll(stale), map->entity, (unsigned long long)stale);
        ++s_copy_count;
        pe_log("rand_ai", "map=%d e%u script of M%03u e%u, %u numbers patched (spawner e%d > e%d), %u slots installed, "
               "%u N vars moved (%u operands)", room, map->entity, map->from_room, map->from_entity, patch.done, spawner,
               to_spawner, slots, (unsigned)moved, patch.n_done);
        for (unsigned i = 0; i < PE_RAND_N_VARS; ++i)
            if (patch.n_to[i] != i) pe_log("rand_ai", "map=%d e%u N%#x > N%#x", room, map->entity, i, patch.n_to[i]);
        if (level_own && value_own)
            pe_log("rand_ai", "map=%d e%u adds level %u, value %u (the slot's; was %u, %u)", room, map->entity, level_now,
                   value_now, level_was, value_was);
        if (count_now != count_was)
            pe_log("rand_ai", "map=%d e%u counts as %u (the slot's foe with its parts; was %u)", room, map->entity, count_now, count_was);
        if (placed)
            pe_log("rand_ai", "map=%d e%u places: floor box %d..%d,%d..%d > %d..%d,%d..%d, %u polygons, %u points, %u heights (%d > %d)",
                   room, map->entity, (int)geo.from.x0, (int)geo.from.x1, (int)geo.from.z0, (int)geo.from.z1, (int)geo.to.x0,
                   (int)geo.to.x1, (int)geo.to.z0, (int)geo.to.z1, geo.polygons, geo.points, geo.heights,
                   geo.y_known ? (int)(geo.y_from / 65536) : 0, geo.y_to_known ? (int)(geo.y_to / 65536) : -1);
        pe_disc_room_free(&src);
        ++done;
    }
    return (int)done;
}

uint64_t pe_rand_ai_stale(unsigned entity) { return entity < PE_RAND_TYPES ? s_stale[entity] : 0; }

/* Each frame: a moved type's left-over working vars back to 0 while none of its actors is alive. */
static int pe_rand_ai_frame(void *ctx, void *user) {
    (void)ctx;
    (void)user;
    if (!s_copy_count) return 0;
    unsigned alive = 0;
    for (FieldActor *a = g_FieldActorListHead; a; a = a->next)
        if (a->type_id < PE_RAND_TYPES && !(a->flags & 0x10)) alive |= 1u << a->type_id;   /* 0x10: dead */
    for (unsigned e = 0; e < PE_RAND_TYPES; ++e) {
        if (!s_stale[e]) continue;
        if (alive >> e & 1) {
            if (!s_stale_alive[e] && s_stale_groups[e] < 255 && ++s_stale_groups[e] > 1)
                pe_log("rand_ai", "map=%d e%u: group %u alive, its working vars at 0 again", pe_room_number(), e, s_stale_groups[e]);
            s_stale_alive[e] = 1;
            continue;
        }
        unsigned was = 0;
        for (unsigned i = 0; i < PE_RAND_N_VARS; ++i)
            if ((s_stale[e] >> i & 1) && g_ScriptNodeActiveFlags[i]) { g_ScriptNodeActiveFlags[i] = 0; ++was; }
        if (was && s_stale_alive[e])
            pe_log("rand_ai", "map=%d e%u: none alive, %u working vars back to 0", pe_room_number(), e, was);
        s_stale_alive[e] = 0;
    }
    return 0;
}

static int pe_rand_ai_rows(void *ctx, void *user) {
    (void)ctx;
    (void)user;
    if (s_map_count || s_slot_count || s_copy_count) pe_rand_ai_apply(pe_room_number());
    return 0;
}

void pe_rand_ai_register(void) {
    pe_hook_add(PE_HOOK_PLAYER_ROWS, pe_rand_ai_rows, NULL, PE_PRIORITY_NORMAL, MOD_ID);
    pe_hook_add(PE_HOOK_FRAME, pe_rand_ai_frame, NULL, PE_PRIORITY_NORMAL, MOD_ID);
    pe_hook_add(PE_HOOK_BATTLE_UPDATE, pe_rand_ai_frame, NULL, PE_PRIORITY_NORMAL, MOD_ID);
}
