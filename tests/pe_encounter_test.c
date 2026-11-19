/* pe_encounter (mods/host): finding random-encounter spots in room script bytecode. The blobs
 * are written here from the instruction layout (Script_RunQueue); nothing comes from a disc. */
#ifdef NDEBUG
#undef NDEBUG /* These runtime checks must also execute in release CI builds. */
#endif
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "pe_encounter.h"

static uint32_t s_blob[4096];
static size_t s_at;

/* header: opcode, argument count, modes of arguments 0..4; tail: modes of arguments 5.. */
static void emit(unsigned op, unsigned argc, uint32_t modes, uint32_t tail, const uint32_t *args) {
    s_blob[s_at++] = op | (argc << 13) | (modes << 17);
    s_blob[s_at++] = tail;
    for (unsigned i = 0; i < argc; ++i) s_blob[s_at++] = args[i];
}
static void filler(unsigned words) {   /* AssignValue L1 #0, as many as fit */
    const uint32_t a[2] = {1, 0};
    while (words >= 4) { emit(10, 2, 1, 0, a); words -= 4; }
    while (words--) s_blob[s_at++] = 0;
}
static void zone(int x0, int z0, int x1, int z1) {
    const uint32_t a[11] = {(uint32_t)x0, (uint32_t)z0, (uint32_t)x0, (uint32_t)z1, (uint32_t)x1, (uint32_t)z1,
                            (uint32_t)x1, (uint32_t)z0, 5, 6, 8};
    emit(119, 11, 0, (1u << 9) | (1u << 12) | (1u << 15), a);   /* Aya's x, z and the result: locals */
}
/* Random L23 #0 #256; EvalExpr #10 T0 L23 L24; JumpIfZero T0 #skip; ...; battle start #2. */
static size_t roll(int battle) {
    const uint32_t r[3] = {0x17, 0, 0x100}, c[4] = {10, 0, 0x17, 0x18}, j[2] = {0, 0x350}, b[1] = {2};
    size_t at = s_at * 4;
    emit(26, 3, 1, 0, r);
    emit(9, 4, 0 | (3u << 3) | (1u << 6) | (1u << 9), 0, c);
    emit(5, 2, 3, 0, j);
    filler(12);
    if (battle) emit(0x55, 1, 0, 0, b);
    else filler(4);
    return at;
}

int main(void) {
    PeEncounterZone found[4];
    size_t first, second, decoy;
    int x, z;

    filler(64);
    zone(283 << 16, 4454 << 16, 1363 << 16, 4567 << 16);
    filler(200);                         /* M030's zones are 0x374 bytes before their rolls */
    first = roll(1);
    filler(40);
    decoy = roll(0);                     /* an enemy's own random roll: no battle start after it */
    filler(40);
    zone(-1249 * 65536, 4454 << 16, -284 * 65536, 4567 << 16);
    filler(180);
    second = roll(1);
    filler(1100);                        /* a roll with no zone within the search window: not a spot */
    roll(1);
    filler(32);

    assert(pe_encounter_roll((const unsigned char *)s_blob + first, sizeof s_blob - first));
    assert(!pe_encounter_roll((const unsigned char *)s_blob + decoy, sizeof s_blob - decoy));
    assert(pe_encounter_find((const unsigned char *)s_blob, s_at * 4, found, 4) == 2);
    assert(found[0].roll == first && found[1].roll == second);
    assert(found[0].corner[0] == 283 << 16 && found[0].corner[3] == 4567 << 16);
    pe_encounter_center(&found[0], &x, &z);
    assert(x == (283 + 1363) / 2 << 16 && z == ((4454 + 4567) << 16) / 2);
    pe_encounter_center(&found[1], &x, &z);
    assert(x == (-1249 - 284) * 65536 / 2);
    assert(pe_encounter_find((const unsigned char *)s_blob, s_at * 4, found, 1) == 2);   /* writes 1, counts 2 */
    assert(found[0].roll == first);
    assert(pe_encounter_find((const unsigned char *)s_blob, 40, found, 4) == 0);
    assert(pe_encounter_find(NULL, 0, found, 4) == 0);
    puts("pe_encounter: ok");
    return 0;
}
