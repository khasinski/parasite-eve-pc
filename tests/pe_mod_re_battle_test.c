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
    CHECK(battle.waiting_count == 1 && battle.waiting[0] == 1);
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

static unsigned view_mask;

static int view(const PeReEnemyPlacement *enemy) {
    return (view_mask & (1u << enemy->slot)) != 0;
}

static void battle_room(PeReSession *s) {
    enum { count = 5, record_size = 22 };
    uint8_t rdt[1 + count * record_size] = {0};
    *s = (PeReSession){.game = pe_re_game_named("re2"), .room = 4};
    s->run.ran = 1;
    s->run.nenemy = count;
    for (unsigned slot = 0; slot < count; ++slot) {
        const unsigned at = 1 + slot * record_size;
        s->run.enemy[slot] = at;
        rdt[at + 2] = (uint8_t)slot;
        rdt[at + 3] = 0x10;
        rdt[at + 9] = (uint8_t)slot;
        int16_t *work = s->run.works[2 + slot];
        work[0] = 1;
        work[PE_RE_MEMBER_ID] = 0x10;
        work[PE_RE_MEMBER_POS_X] = (int16_t)(10 + slot * 100);
        work[PE_RE_MEMBER_NFLOOR] = slot == 3 ? 1 : 0;
    }
    PeImpLogicBuilder *builder = pe_imp_logic_begin(PE_IMP_RE2, 1, 0, 4);
    CHECK(builder && !pe_imp_logic_add(builder, PE_IMP_LOGIC_RDT0, 1, rdt, sizeof rdt));
    s->logic = pe_imp_logic_finish(builder);
    CHECK(s->logic);
}

static PeReEnemyPlacement lead_at(uint8_t slot, int16_t x) {
    return (PeReEnemyPlacement){.slot = slot, .kind = 0x10, .killed_flag = slot, .x = x};
}

static void test_join_and_target(void) {
    PeReSession s;
    battle_room(&s);
    PeReBattleRules rules = *pe_re_battle_rules("re2");
    rules.max_foes = 2;
    PeReBattle battle = {0};
    const PeReEnemyPlacement lead = lead_at(0, 10);
    PeReBattleEvent ev[2];
    uint8_t slots[2];

    view_mask = 1u << 0 | 1u << 3; /* other floor excluded even if shown */
    CHECK(!pe_re_battle_begin(&battle, &s, &rules, &lead, view));
    CHECK(battle.joined == 1);
    view_mask |= 1u << 1 | 1u << 2;
    CHECK(!pe_re_battle_step(&battle, &s, 0, 0, 1, view, NULL, 0));
    CHECK(battle.joined == 1 && battle.waiting_count == 2);
    CHECK(pe_re_battle_step(&battle, &s, 0, 0, 1, view, ev, 2) == 1);
    CHECK(ev[0].kind == PE_RE_BATTLE_EV_JOIN && ev[0].slot == 1);
    CHECK(battle.joined == 2 && battle.waiting_count == 1 && battle.waiting[0] == 2);
    CHECK(battle.foe[3].state == PE_RE_BATTLE_FOE_NONE);

    view_mask |= 1u << 4;
    CHECK(!pe_re_battle_step(&battle, &s, 0, 0, 0, view, ev, 2));
    CHECK(battle.waiting_count == 2 && battle.waiting[1] == 4);
    pe_re_battle_foe_died(&battle, 0);
    CHECK(pe_re_battle_step(&battle, &s, 0, 0, 1, view, ev, 2) == 1);
    CHECK(ev[0].kind == PE_RE_BATTLE_EV_JOIN && ev[0].slot == 2);

    pe_re_battle_foe_moved(&battle, 2, 10, 0);
    view_mask &= ~(1u << 2);
    CHECK(!pe_re_battle_step(&battle, &s, 0, 0, 0, view, ev, 2));
    CHECK(pe_re_battle_target_order(&battle, slots, 2) == 2 && slots[0] == 1 && slots[1] == 2);
    view_mask |= 1u << 2;
    CHECK(!pe_re_battle_step(&battle, &s, 0, 0, 0, view, ev, 2));
    CHECK(pe_re_battle_target_order(&battle, slots, 1) == 1 && slots[0] == 2);
    view_mask &= ~((1u << 1) | (1u << 2));
    CHECK(!pe_re_battle_step(&battle, &s, 0, 0, 0, view, ev, 2));
    CHECK(pe_re_battle_target_order(&battle, slots, 2) == 2 && slots[0] == 2 && slots[1] == 1);

    pe_re_battle_foe_died(&battle, 1);
    pe_re_battle_foe_died(&battle, 2);
    CHECK(pe_re_battle_step(&battle, &s, 0, 0, 1, view, ev, 2) == 1);
    CHECK(ev[0].kind == PE_RE_BATTLE_EV_JOIN && ev[0].slot == 4);
    pe_re_battle_foe_died(&battle, 4);
    CHECK(pe_re_battle_step(&battle, &s, 0, 0, 1, view, ev, 2) == 1);
    CHECK(ev[0].kind == PE_RE_BATTLE_EV_END);
    CHECK(!pe_re_battle_step(&battle, &s, 0, 0, 1, view, ev, 2));
    free(s.logic);
}

static int chase_calls;

static void test_chase(PeReSession *s, PeReEnemyPlacement *enemies, int count, double x, double z, int floor) {
    CHECK(x == 0 && z == 0 && floor == 0);
    ++chase_calls;
    for (int i = 0; i < count; ++i) {
        CHECK(enemies[i].slot != 0 && enemies[i].slot != 3);
        enemies[i].x -= 20;
        s->run.works[2 + enemies[i].slot][PE_RE_MEMBER_POS_X] = enemies[i].x;
    }
}

static void test_pursuit_and_grace(void) {
    PeReSession s;
    battle_room(&s);
    PeReBattleRules rules = *pe_re_battle_rules("re2");
    rules.chase_step = test_chase;
    PeReBattle battle;
    pe_re_battle_init(&battle);
    CHECK(battle.settings.seconds == 3 && battle.settings.distance == 4000 && battle.settings.halve_on_repeat);
    battle.settings = (PeReBattleSettings){.seconds = 0.1, .distance = 1000, .halve_on_repeat = 1};
    PeReEnemyPlacement lead = lead_at(0, 10);
    const PeReEnemyPlacement other = lead_at(1, 110);
    view_mask = 1;
    CHECK(!pe_re_battle_begin(&battle, &s, &rules, &lead, view));
    CHECK(!pe_re_battle_step(&battle, &s, 0, 0, 0, view, NULL, 0));
    CHECK(!chase_calls && s.run.works[3][PE_RE_MEMBER_POS_X] == 110);
    CHECK(!pe_re_battle_step(&battle, &s, 0, 0, 1, view, NULL, 0));
    CHECK(chase_calls == 1 && s.run.works[3][PE_RE_MEMBER_POS_X] == 90);
    pe_re_battle_foe_moved(&battle, 0, 55.6, -25);
    pe_re_battle_end(&battle, &s, 1);
    CHECK(s.run.works[2][PE_RE_MEMBER_POS_X] == 56 && s.run.works[2][PE_RE_MEMBER_POS_Z] == -25);
    CHECK(s.run.works[3][PE_RE_MEMBER_POS_X] == 90);
    CHECK(battle.phase == PE_RE_BATTLE_GRACE && battle.grace_frames == 6);
    CHECK(!pe_re_battle_encounter_allowed_for(&battle, &s, 0));
    CHECK(pe_re_battle_encounter_allowed_for(&battle, &s, 1));
    CHECK(pe_re_battle_begin(&battle, &s, &rules, &lead, view) == -1);
    CHECK(!pe_re_battle_begin(&battle, &s, &rules, &other, NULL));
    pe_re_battle_foe_died(&battle, 1);
    pe_re_battle_end(&battle, &s, 0);
    CHECK(battle.phase == PE_RE_BATTLE_GRACE);
    for (int frame = 0; frame < 5; ++frame)
        CHECK(!pe_re_battle_step(&battle, &s, 0, 0, 1, view, NULL, 0));
    CHECK(!pe_re_battle_encounter_allowed_for(&battle, &s, 0));
    CHECK(!pe_re_battle_step(&battle, &s, 0, 0, 1, view, NULL, 0));
    CHECK(pe_re_battle_encounter_allowed_for(&battle, &s, 0) && chase_calls == 1);

    lead.x = 56;
    CHECK(!pe_re_battle_begin(&battle, &s, &rules, &lead, NULL));
    pe_re_battle_end(&battle, &s, 1);
    CHECK(battle.grace_frames == 3 && battle.grace[0].distance == 500);
    CHECK(!pe_re_battle_step(&battle, &s, 556, 0, 0, view, NULL, 0));
    CHECK(pe_re_battle_encounter_allowed_for(&battle, &s, 0));

    battle.settings.halve_on_repeat = 0;
    CHECK(!pe_re_battle_begin(&battle, &s, &rules, &lead, NULL));
    pe_re_battle_end(&battle, &s, 1);
    CHECK(battle.grace_frames == 6 && battle.grace[0].distance == 1000);
    s.room++;
    CHECK(pe_re_battle_encounter_allowed_for(&battle, &s, 0));
    CHECK(!pe_re_battle_step(&battle, &s, 0, 0, 0, view, NULL, 0));
    CHECK(battle.phase == PE_RE_BATTLE_IDLE && !battle.grace[0].escapes);
    free(s.logic);
}

int main(void) {
    test_anchor();
    test_round_trip();
    test_re2_rules();
    test_begin();
    test_join_and_target();
    test_pursuit_and_grace();
    puts("RE battle T1-T4 and T6-T10 passed");
    return 0;
}
