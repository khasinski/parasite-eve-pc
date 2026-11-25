#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pe_re_battle.h"
#include "re_encounter_rooms.h"

#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); exit(1); } } while (0)

static void test_anchor(void) {
    const PeReBattleBounds host = {{-32768, -32768}, {32767, 32767}};
    const PeReBattleBounds zones = {{-4000, -4000}, {4000, 4000}};
    const PeReBattleBounds room = {{-32000, -8000}, {32000, 8000}};
    PeReBattlePoint anchor;
    CHECK(pe_re_battle_anchor(room, host, zones, 2.865, 8000, &anchor));
    CHECK(anchor.x - 32000 / 2.865 >= host.min.x);
    CHECK(anchor.x + 32000 / 2.865 <= host.max.x);
    CHECK(anchor.z - 8000 / 2.865 >= host.min.z);
    CHECK(anchor.z + 8000 / 2.865 <= zones.min.z - 8000);
    CHECK(!pe_re_battle_anchor(room, host, host, 2.865, 8000, &anchor));
    CHECK(!pe_re_battle_anchor(room, host, zones, 0.5, 8000, &anchor));

    const PeReBattleBounds strip = {{-32768, -4000}, {32767, 4000}};
    CHECK(pe_re_battle_anchor(room, host, strip, 2.865, 8000, &anchor));
    CHECK(anchor.z + 8000 / 2.865 <= strip.min.z - 8000);
}

static void test_round_trip(void) {
    const double scales[] = {2.865, 5.73, 11.46};
    const PeReBattlePoint origin = {-23000, 19000}, anchor = {-18000, 100};
    const PeReBattlePoint room = {31000, -27000};
    for (unsigned i = 0; i < sizeof scales / sizeof *scales; ++i) {
        PeReBattlePoint host = pe_re_battle_to_host(room, origin, anchor, scales[i]);
        host.x = round(host.x * 65536) / 65536;
        host.z = round(host.z * 65536) / 65536;
        const PeReBattlePoint back = pe_re_battle_to_room(host, origin, anchor, scales[i]);
        CHECK(hypot(back.x - room.x, back.z - room.z) < 1);
    }
}

static void test_re2_rules(void) {
    const PeReBattleRules *rules = pe_re_battle_rules("re2");
    CHECK(rules && rules->max_foes == 6 && rules->floor_height == 1800);
    CHECK(!pe_re_battle_rules("re1") && !pe_re_battle_rules("re3") && !pe_re_battle_rules(NULL));
    PeReSession s = {0};
    for (unsigned street = 0; street < 2; ++street) {
        s.room = street ? 0 : 4;
        CHECK(rules->encounter_radius(&s) == (street ? 1600 : 1400));
        for (unsigned kind = 0; kind <= UINT8_MAX; ++kind) {
            const uint16_t types[] = {0, 1, 3, 0x4000, 0x8000};
            for (unsigned i = 0; i < sizeof types / sizeof *types; ++i) {
                PeReEnemyPlacement foe = {.kind = (uint8_t)kind, .behaviour = types[i], .killed_flag = 1};
                for (int flagged = 0; flagged < 2; ++flagged) {
                    CHECK(rules->combatant(&s, &foe) == pe_re2_proximity_enemy(&foe, street));
                    CHECK(rules->pursues(&s, &foe) ==
                          (pe_re2_ground_pursuit_kind(kind) && !pe_re2_scene_only_enemy(&foe)));
                    foe.killed_flag = 0xff;
                }
            }
        }
    }
}

static int in_view(const PeReEnemyPlacement *enemy) {
    if (enemy->slot == 9) return -1;
    return enemy->slot != 8;
}

static void test_begin(void) {
    enum { count = 13, record_size = 22 };
    const uint8_t slots[count] = {6, 4, 3, 10, 5, 2, 7, 1, 8, 9, 11, 12, 3};
    const uint8_t kinds[count] = {0x10, 0x10, 0x10, 0x47, 0x1f, 0x10, 0x10, 0x10, 0x10, 0x10, 0x2d, 0x10, 0x10};
    const uint8_t x[count] = {150, 50, 75, 98, 100, 99, 200, 240, 102, 103, 104, 105, 220};
    uint8_t rdt[1 + count * record_size] = {0};
    PeReSession s = {.game = pe_re_game_named("re2"), .room = 4, .camera = 3, .x = 0};
    s.run.ran = 1;
    s.run.nenemy = count;
    for (int i = 0; i < count; ++i) {
        const unsigned at = 1 + i * record_size;
        s.run.enemy[i] = at;
        rdt[at + 2] = slots[i];
        rdt[at + 3] = kinds[i];
        rdt[at + 6] = slots[i] == 12 ? 1 : 0;
        rdt[at + 9] = slots[i];
        rdt[at + 10] = x[i];
    }
    PeImpLogicBuilder *builder = pe_imp_logic_begin(PE_IMP_RE2, 1, 0, 4);
    CHECK(builder);
    CHECK(!pe_imp_logic_add(builder, PE_IMP_LOGIC_RDT0, 1, rdt, sizeof rdt));
    s.logic = pe_imp_logic_finish(builder);
    CHECK(s.logic);

    const PeReBattleRules *rules = pe_re_battle_rules("re2");
    PeReBattle battle = {0};
    const PeReEnemyPlacement lead = {.slot = 5, .kind = 0x1f, .killed_flag = 5, .x = 100};
    CHECK(pe_re_battle_begin(&battle, &s, NULL, &lead, in_view) == -1);
    CHECK(!pe_re_battle_begin(&battle, &s, rules, &lead, in_view));
    const uint8_t expected[] = {5, 2, 3, 6, 4, 7};
    CHECK(battle.joined == 6 && battle.initial_count == 6 && battle.camera == 3);
    CHECK(!memcmp(battle.initial_slots, expected, sizeof expected));
    CHECK(!pe_re_battle_encounter_allowed(&battle));
    CHECK(pe_re_battle_begin(&battle, &s, rules, &lead, in_view) == -1);
    CHECK(!pe_re_battle_step(&battle, &s, 300, 400, 0, in_view, NULL, 0));
    CHECK(battle.phase == PE_RE_BATTLE_RUNNING && battle.aya_x == 300 && battle.aya_z == 400);
    pe_re_battle_foe_moved(&battle, 2, 4, 5);
    CHECK(battle.foe[2].x == 4 && battle.foe[2].z == 5);
    pe_re_battle_foe_died(&battle, 2);
    pe_re_battle_foe_died(&battle, 2);
    CHECK(battle.joined == 5 && battle.foe[2].state == PE_RE_BATTLE_FOE_DEAD);
    pe_re_battle_end(&battle, &s, 0);
    CHECK(pe_re_battle_encounter_allowed(&battle));
    CHECK(!pe_re_battle_begin(&battle, &s, rules, &lead, NULL));
    CHECK(battle.joined == 1);
    free(s.logic);
}

int main(void) {
    test_anchor();
    test_round_trip();
    test_re2_rules();
    test_begin();
    puts("RE battle T1-T4 passed");
    return 0;
}
