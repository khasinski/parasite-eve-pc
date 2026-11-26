/* The battle space (mods/host/pe_battle_space.h): tests T1 and T2 of docs/RE_PLAY_BATTLES.md. */
#include <math.h>
#include <stdio.h>

#include "pe_battle_space.h"

static int s_fail;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #c); s_fail = 1; } } while (0)

static PeBattleBox mapped(const PeBattleSpace *s, const PeBattleBox *room) {
    PeBattleBox b;
    pe_battle_space_to_host(s, room->x0, room->z0, &b.x0, &b.z0);
    pe_battle_space_to_host(s, room->x1, room->z1, &b.x1, &b.z1);
    return b;
}

static int clear_of(const PeBattleBox *a, const PeBattleBox *b) {
    return a->x0 >= b->x1 + PE_BATTLE_SPACE_CLEARANCE || b->x0 >= a->x1 + PE_BATTLE_SPACE_CLEARANCE ||
           a->z0 >= b->z1 + PE_BATTLE_SPACE_CLEARANCE || b->z0 >= a->z1 + PE_BATTLE_SPACE_CLEARANCE;
}

static int in_space(const PeBattleBox *a) {
    const double lim = PE_BATTLE_SPACE_LIMIT - PE_BATTLE_SPACE_MARGIN;
    return a->x0 >= -lim && a->x1 <= lim && a->z0 >= -lim && a->z1 <= lim;
}

int main(void) {
    /* map 66's zones lie within about 6,000 host units of its centre */
    const PeBattleBox park = {-6000, -6000, 6000, 6000};
    /* RE2's street (1-00), and its widest room (1-03: 64,000 room units across) */
    const PeBattleBox street = {-6000, -32000, 32000, 6000}, widest = {-32000, -32000, 32000, 32000};
    const double sizes[3] = {11.46, 5.73, 2.865};          /* k at Aya's size 50, 100, 200 */

    /* T1: an anchor is found for every room and size; the mapped room is inside the 16-bit space and
     * clear of the host room; a host that fills the space is refused */
    for (int i = 0; i < 3; i++) {
        PeBattleSpace s;
        CHECK(!pe_battle_space_choose(&park, &street, 19959, -370, sizes[i], &s));
        PeBattleBox b = mapped(&s, &street);
        CHECK(in_space(&b) && clear_of(&b, &park));
        CHECK(!pe_battle_space_choose(&park, &widest, 0, 0, sizes[i], &s));
        b = mapped(&s, &widest);
        CHECK(in_space(&b) && clear_of(&b, &park));
    }
    {
        const PeBattleBox everywhere = {-30000, -30000, 30000, 30000};
        PeBattleSpace s;
        CHECK(pe_battle_space_choose(&everywhere, &widest, 0, 0, 2.865, &s) == -1);
        CHECK(pe_battle_space_choose(&park, &street, 0, 0, 0, &s) == -1);
    }

    /* T2: room -> host -> room moves a point by less than one room unit, at every size */
    for (int i = 0; i < 3; i++) {
        PeBattleSpace s;
        CHECK(!pe_battle_space_choose(&park, &street, 19959, -370, sizes[i], &s));
        for (double x = -6000; x <= 32000; x += 3713)
            for (double z = -32000; z <= 6000; z += 2917) {
                double hx, hz, rx, rz;
                pe_battle_space_to_host(&s, x, z, &hx, &hz);
                /* the engine keeps host positions in 16.16 fixed point */
                hx = floor(hx * 65536.0 + 0.5) / 65536.0, hz = floor(hz * 65536.0 + 0.5) / 65536.0;
                pe_battle_space_to_room(&s, hx, hz, &rx, &rz);
                CHECK(fabs(rx - x) < 1 && fabs(rz - z) < 1);
            }
        double hx, hz;
        pe_battle_space_to_host(&s, 19959, -370, &hx, &hz);           /* her spot is the anchor */
        CHECK(hx == s.anchor_x && hz == s.anchor_z);
    }

    printf("battle space: %s\n", s_fail ? "FAILED" : "passed");
    return s_fail;
}
