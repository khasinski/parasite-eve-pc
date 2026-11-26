/* A battle trip from an imported room, with defeats reported back to it. See pe_trip_result.h. */
#include <stddef.h>
#include <string.h>

#include "pe_imp_enemy.h"
#include "re_encounter_rooms.h"
#include "pe_trip_result.h"

static int has_slot_api(const PeRePlayApi *api) {
    return api && api->struct_size >= offsetof(PeRePlayApi, defeated_slot) + sizeof api->defeated_slot &&
           api->room && api->defeated_slot;
}

int pe_trip_arm(PeTripResult *t, const PeReRoomEvent *room,
                int (*prepare)(const char *asset, int already_selected, const char *entrance,
                               const PeReEnemyPlacement *placement, void *ctx), void *ctx) {
    memset(t, 0, sizeof *t);
    if (!room || !room->game || strlen(room->game) >= sizeof t->game) return 0;
    strcpy(t->game, room->game);
    t->stage = room->stage, t->room = room->room;
    uint8_t kinds[PE_TRIP_FOES_MAX];
    const int re2 = !strcmp(room->game, "re2");
    for (int i = 0; i < room->count && t->count < PE_TRIP_FOES_MAX; i++) {
        const PeReEnemyPlacement *e = &room->enemies[i];
        char asset[96];
        if (re2 && pe_re2_scene_only_enemy(e)) continue;
        if (e->kind >= 0x40 || pe_imp_enemy_kind_asset(room->game, e->kind, asset, sizeof asset)) continue;
        if (pe_imp_enemy_body(asset) == PE_IMP_ENEMY_SCENE) continue;
        int duplicate = 0, already_selected = 0, adult_g_selected = 0;
        for (int j = 0; j < t->count; j++) {
            duplicate |= t->slot[j] == e->slot;
            already_selected |= kinds[j] == e->kind;
            adult_g_selected |= kinds[j] == 0x28 && t->slot[j] == 0;
        }
        if (duplicate) continue;
        if (re2 && e->kind == 0x27 && e->killed_flag == 0xff &&
            (!adult_g_selected || !pe_re2_g_attached_embryo(room->stage, room->room, e))) continue;
        const PeImpEnemyStart *start = pe_imp_enemy_starts(asset);
        while (start && start->name && (e->behaviour & start->mask) != start->value) start++;
        if (prepare && !prepare(asset, already_selected, start ? start->name : NULL, e, ctx)) continue;
        kinds[t->count] = e->kind;
        t->slot[t->count++] = e->slot;
    }
    t->armed = t->count > 0;
    return t->count;
}
void pe_trip_won(PeTripResult *t, int defeated) {
    pe_trip_defeated(t, defeated > 0 && defeated <= t->count ? (1u << defeated) - 1 : 0);
}

void pe_trip_defeated(PeTripResult *t, unsigned defeated) {
    if (t->armed) t->defeated = defeated & ((1u << t->count) - 1);
}

static const PeReEnemyPlacement *nearest_enemy(const PeReRoomEvent *room, double x, double z, int floor,
                                                double limit, unsigned selected,
                                                int (*reachable)(double, double, double, double)) {
    const PeReEnemyPlacement *nearest = NULL;
    for (int i = 0; i < room->count; i++) {
        const PeReEnemyPlacement *e = &room->enemies[i];
        if (!pe_re2_proximity_enemy(e, pe_re2_opening_street(room->stage, room->room)) || e->floor != floor ||
            (selected & (1u << e->slot))) continue;
        const double dx = e->x - x, dz = e->z - z, distance = dx * dx + dz * dz;
        if (distance > limit || (nearest && distance == limit)) continue;
        if (reachable && !reachable(x, z, e->x, e->z)) continue;
        nearest = e;
        limit = distance;
    }
    return nearest;
}

int pe_trip_approach(const PeReRoomEvent *room, double x, double z, int floor,
                     int (*reachable)(double, double, double, double), PeReEnemyPlacement *out) {
    if (strcmp(room->game, "re2")) return 0;
    const int street = pe_re2_opening_street(room->stage, room->room);
    if (!street && !reachable) return 0;
    const double radius = street ? 1600 : 1400;
    const double group_radius = street ? 6500 : 3900;
    const PeReEnemyPlacement *lead = nearest_enemy(room, x, z, floor, radius * radius, 0, reachable);
    if (!lead) return 0;
    out[0] = *lead;
    int count = 1;
    unsigned selected = 1u << lead->slot;
    while (count < PE_TRIP_GROUP_MAX) {
        const PeReEnemyPlacement *neighbor = nearest_enemy(room, lead->x, lead->z, floor,
                                                            group_radius * group_radius, selected, reachable);
        if (!neighbor) break;
        out[count++] = *neighbor;
        selected |= 1u << neighbor->slot;
    }
    if (count < PE_TRIP_GROUP_MAX && lead->kind == 0x28 && lead->slot == 0) {
        for (int i = 0; i < room->count; i++) {
            const PeReEnemyPlacement *e = &room->enemies[i];
            if (e->floor != floor || !pe_re2_g_attached_embryo(room->stage, room->room, e)) continue;
            out[count++] = *e;
            break;
        }
    }
    return count;
}

int pe_trip_report(PeTripResult *t, const PeRePlayApi *api) {
    PeReRoomEvent room = {sizeof room};
    if (!t->armed || !t->defeated) { t->armed = 0; return -1; }
    if (api && api->struct_size >= offsetof(PeRePlayApi, apply_battle_result) + sizeof api->apply_battle_result &&
        api->apply_battle_result) {
        PeReBattleResult result = {.game = t->game, .stage = t->stage, .room = t->room};
        for (int i = 0; i < t->count; ++i)
            if ((t->defeated & (1u << i)) && t->slot[i] < 16)
                result.defeated_slots |= (uint16_t)(1u << t->slot[i]);
        if (t->canister_detonated) result.canister_slots = result.defeated_slots;
        const int applied = api->apply_battle_result(&result);
        if (applied) t->armed = 0;
        return applied;
    }
    if (!has_slot_api(api)) { t->armed = 0; return -1; }
    if (api->room(&room)) return 0;                        /* not running yet: the trip back, a door */
    t->armed = 0;
    if (!room.game || strcmp(room.game, t->game) || room.stage != t->stage || room.room != t->room) return -1;
    int reported = 0;
    for (int i = 0; i < t->count; i++) {
        if (!(t->defeated & (1u << i))) continue;
        if (t->canister_detonated && api->struct_size >= offsetof(PeRePlayApi, advance_enemies) && api->defeated_by_canister) {
            if (!api->defeated_by_canister(t->slot[i])) { reported++; continue; }
        }
        reported += !api->defeated_slot(t->slot[i]);
    }
    return reported ? reported : -1;
}

int pe_trip_select_approach(const PeRePlayApi *api, const PeReRoomEvent *room, double x, double z, int floor,
                              PeReEnemyPlacement *close) {
    int count = pe_trip_approach(room, x, z, floor,
                                 api->struct_size >= offsetof(PeRePlayApi, encounter_boss) ? api->encounter_reachable : NULL, close);
    if (count < 1 || api->struct_size < offsetof(PeRePlayApi, in_view) + sizeof api->in_view || !api->in_view ||
        api->in_view(&close[0]) < 0)
        return count;
    /* the one that reached her starts it; the battle takes in every enemy she can see (on her
     * floor, in the camera's picture), nearest to it first, as RE's own fights are what's on screen */
    const PeReEnemyPlacement lead = close[0];
    unsigned selected = 1u << lead.slot;
    count = 1;
    while (count < PE_TRIP_FOES_MAX) {
        const PeReEnemyPlacement *next = NULL;
        double best = 0;
        for (int i = 0; i < room->count; i++) {
            const PeReEnemyPlacement *e = &room->enemies[i];
            if ((selected & (1u << e->slot)) || e->floor != floor ||
                !pe_re2_proximity_enemy(e, pe_re2_opening_street(room->stage, room->room)) || api->in_view(e) != 1)
                continue;
            const double dx = e->x - lead.x, dz = e->z - lead.z, d = dx * dx + dz * dz;
            if (!next || d < best) next = e, best = d;
        }
        if (!next) break;
        close[count++] = *next;
        selected |= 1u << next->slot;
    }
    return count;
}

int pe_trip_select_boss(const PeRePlayApi *api, const PeReRoomEvent *room, PeReEnemyPlacement *out) {
    if (api->struct_size < offsetof(PeRePlayApi, prepare_boss) || !api->encounter_boss) return 0;
    const int slot = api->encounter_boss();
    for (int i = 0; slot >= 0 && i < room->count; i++)
        if (room->enemies[i].slot == slot) { *out = room->enemies[i]; return 1; }
    return 0;
}
