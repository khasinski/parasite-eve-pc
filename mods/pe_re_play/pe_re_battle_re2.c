#include "pe_re_battle_rules.h"
#include "re_encounter_rooms.h"

static int combatant(const PeReSession *s, const PeReEnemyPlacement *foe) {
    return pe_re2_proximity_enemy(foe, pe_re2_opening_street(s->stage, s->room));
}

static int pursues(const PeReSession *s, const PeReEnemyPlacement *foe) {
    (void)s;
    return pe_re2_ground_pursuit_kind(foe->kind) && !pe_re2_scene_only_enemy(foe);
}

static double encounter_radius(const PeReSession *s) {
    return pe_re2_opening_street(s->stage, s->room) ? 1600 : 1400;
}

static int boss(const PeReSession *s, PeReBossFight *out) {
    /* Same ready-slot lookup as pe_trip_select_boss, with no adapter callbacks. */
    const int slot = pe_re_session_ready_boss(s);
    if (slot < 0) return 0;
    PeReEnemyPlacement enemies[PE_RE_ENEMIES_MAX];
    const int count = pe_re_session_enemies(s, enemies, PE_RE_ENEMIES_MAX);
    for (int i = 0; i < count; ++i) {
        if (enemies[i].slot != slot) continue;
        *out = (PeReBossFight){.lead = enemies[i], .effect = PE_RE_BOSS_STANDARD};
        /* Mirror prepare_boss's decisions. The adapter owns presenting the choice. */
        if (pe_re_session_final_tyrant(s, slot)) {
            out->effect = PE_RE_BOSS_LAUNCHER;
        } else if (pe_re_session_alligator(s, slot)) {
            if (pe_re_scd_running(&s->run, 10)) out->effect = PE_RE_BOSS_WAIT;
            else if (pe_re_session_canister_ready(s)) out->effect = PE_RE_BOSS_CANISTER;
            else out->needs_choice = !s->run.alligator_chosen;
        }
        return 1;
    }
    return 0;
}

static int joins_boss(const PeReSession *s, const PeReBossFight *fight, const PeReEnemyPlacement *foe) {
    if (foe->floor != fight->lead.floor) return 0;
    if (foe->slot == fight->lead.slot) return 1;
    return fight->lead.kind == 0x28 && fight->lead.slot == 0 &&
           pe_re2_g_attached_embryo(s->stage, s->room, foe);
}

static void chase_step(PeReSession *s, PeReEnemyPlacement *foes, int count, double x, double z, int floor) {
    PeRe2RoomCollision collision;
    uint32_t size;
    const uint8_t *data = pe_imp_logic_section(s->logic, PE_IMP_LOGIC_COLL, NULL, &size);
    if (data && !pe_imp_re2_collision_all(data, size, &collision))
        pe_re_session_chase(s, &collision, foes, count, x, z, floor);
}

const PeReBattleRules pe_re_battle_re2_rules = {
    .game = "re2",
    .max_foes = PE_RE_BATTLE_MAX_FOES,
    .floor_height = PE_RE2_FLOOR_HEIGHT,
    .combatant = combatant,
    .pursues = pursues,
    .encounter_radius = encounter_radius,
    .boss = boss,
    .joins_boss = joins_boss,
    .chase_step = chase_step,
};
