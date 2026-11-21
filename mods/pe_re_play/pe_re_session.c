/* A play session in an imported game. See pe_re_session.h. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pe_re_session.h"
#include "pe_re_zone.h"

/* First rooms: RE2 and RE3 open in the street (room 1-00); RE1 in the mansion's main hall (1-06).
 * Only RE2 has the cameras and collision the runtime uses so far (design doc, phase 4). */
static const PeReGame k_games[PE_RE_GAMES] = {
    {"re1", "Resident Evil", 0, 0x06, 0},
    {"re2", "Resident Evil 2", 0, 0x00, 1},
    {"re3", "Resident Evil 3", 0, 0x00, 0},
};

const PeReGame *pe_re_game(int i) { return i >= 0 && i < PE_RE_GAMES ? &k_games[i] : NULL; }
const PeReGame *pe_re_game_named(const char *game) {
    for (int i = 0; game && i < PE_RE_GAMES; i++) if (!strcmp(k_games[i].game, game)) return &k_games[i];
    return NULL;
}

void pe_re_logic_id(const char *game, uint32_t stage, uint32_t room, uint32_t player, char *out, size_t n) {
    if (!strcmp(game, "re3")) snprintf(out, n, "re3:logic/%x%02x", (unsigned)stage + 1, (unsigned)room);
    else snprintf(out, n, "%s:logic/%x%02x-%u", game, (unsigned)stage + 1, (unsigned)room, (unsigned)player);
}

void pe_re_room_id(const char *game, uint32_t stage, uint32_t room, char *out, size_t n) {
    snprintf(out, n, "%s:room/%x%02x", game, (unsigned)stage + 1, (unsigned)room);
}

int pe_re_imported(const char *store, const PeReGame *g) {
    char logic[PE_IMP_ID_MAX], claire[PE_IMP_ID_MAX] = "", room[PE_IMP_ID_MAX];
    PeImpAsset *a = NULL;
    int found = 0;
    if (!store || !g) return 0;
    pe_re_logic_id(g->game, g->stage, g->room, 0, logic, sizeof logic);
    if (!strcmp(g->game, "re2")) pe_re_logic_id(g->game, g->stage, g->room, 1, claire, sizeof claire);
    pe_re_room_id(g->game, g->stage, g->room, room, sizeof room);
    const int n = pe_imp_catalog_assets(store, g->game, &a);
    for (int i = 0; i < n; i++) {
        if (!strcmp(a[i].id, logic) || (*claire && !strcmp(a[i].id, claire))) found |= 1;
        if (!strcmp(a[i].id, room)) found |= 2;
    }
    free(a);
    return found == 3;
}

/* ---- zones ---- */
int pe_re_in_quad(const int16_t x[4], const int16_t z[4], double px, double pz) {
    int inside = 0;
    for (int i = 0, j = 3; i < 4; j = i++)               /* even-odd: any four-sided zone */
        if ((z[i] > pz) != (z[j] > pz) && px < (double)(x[j] - x[i]) * (pz - z[i]) / (double)(z[j] - z[i]) + x[i])
            inside = !inside;
    return inside;
}

/* Switch zones come grouped by the camera they leave. Each group opens with that camera's own area,
 * not a switch: RE2 and RE3 store it as the group's first record ("from 1 to 0" in room 1-00, a
 * zone covering cameras 0 and 1, which taken as a switch flips 0 and 1 for ever); RE1 flags it 1. */
int pe_re_camera_at(const PeImpRoomLogic *l, int camera, double x, double z) {
    uint32_t n = 0;
    int group = -1;
    if (!l || !pe_imp_logic_section(l, PE_IMP_LOGIC_CAMZ, &n, NULL)) return camera;
    for (uint32_t i = 0; i < n; i++) {
        PeImpCameraZone c;
        if (pe_imp_logic_camera_zone(l, i, &c)) continue;
        const int own = l->game == PE_IMP_RE1 ? c.flag == 1 : c.from != group;
        group = c.from;
        if (own || c.from != camera || c.to == camera) continue;
        if (pe_re_in_quad(c.x, c.z, x, z)) return c.to;
    }
    return camera;
}

int pe_re_door_at(const PeImpRoomLogic *l, const PeReScdRun *run, double x, double z, PeImpDoor *door) {
    uint32_t n = 0;
    if (!l || !pe_imp_logic_section(l, PE_IMP_LOGIC_DOOR, &n, NULL)) return -1;
    for (uint32_t i = 0; i < n; i++) {
        PeImpDoor d;
        if (pe_imp_logic_door(l, i, &d)) continue;
        if (run && (!pe_re_scd_live(run, d.at) || pe_re_zone_kind(run, d.zone.id, d.zone.sce, NULL) != d.zone.sce)) continue;
        if (pe_re_in_quad(d.zone.x, d.zone.z, x, z)) { if (door) *door = d; return (int)i; }
    }
    return -1;
}

int pe_re_free_spot(const PeRe2RoomCollision *c, int floor, double *x, double *z, int radius, int reach) {
    if (!pe_imp_re2_blocked_on(c, (unsigned)floor, *x, *z, radius)) return 1;
    for (int r = 50; r <= reach; r += 50)                  /* rings of 16 points, nearest ring first */
        for (int k = 0; k < 16; k++) {
            static const double dir[16][2] = {{1, 0}, {.924, .383}, {.707, .707}, {.383, .924}, {0, 1}, {-.383, .924},
                                              {-.707, .707}, {-.924, .383}, {-1, 0}, {-.924, -.383}, {-.707, -.707},
                                              {-.383, -.924}, {0, -1}, {.383, -.924}, {.707, -.707}, {.924, -.383}};
            const double nx = *x + r * dir[k][0], nz = *z + r * dir[k][1];
            if (!pe_imp_re2_blocked_on(c, (unsigned)floor, nx, nz, radius)) { *x = nx, *z = nz; return 1; }
        }
    return 0;
}

/* ---- the session ---- */
static int has_item(void *ctx, uint16_t item) { return pe_re_has(&((PeReSession *)ctx)->progress, item); }
static int talking(void *ctx) { const PeReSession *s = ctx; return s->talking && s->talking(); }
static void lose_item(void *ctx, uint16_t item, uint16_t count) { pe_re_lose(&((PeReSession *)ctx)->progress, item, count); }
static void give_item(void *ctx, uint16_t item, uint16_t count) { pe_re_give(&((PeReSession *)ctx)->progress, item, count); }
static PeReScdHost host_of(PeReSession *s) {
    PeReScdHost h = {has_item, talking, s->key, s->key_trg, s, lose_item, give_item};
    return h;
}

static int load(PeReSession *s, const char *store, uint32_t stage, uint32_t room, char *why, size_t n) {
    char id[PE_IMP_ID_MAX];
    PeImpRoomLogic *l = NULL;
    pe_re_logic_id(s->game->game, stage, room, s->player, id, sizeof id);
    if (pe_imp_catalog_roomlogic(store, id, &l, why, n)) {
        if (why && n && !*why) snprintf(why, n, "%s isn't imported", id);
        return -1;
    }
    free(s->logic);
    s->logic = l, s->stage = stage, s->room = room;
    const PeReScdHost host = host_of(s);
    memset(s->progress.flags.bits[PE_RE_FG_USE], 0, sizeof s->progress.flags.bits[PE_RE_FG_USE]);   /* Sce_work_clr */
    pe_re_scd_run(l, &s->progress.flags, &host, &s->run);   /* what its scripts set up */
    s->auto_message = -1;
    s->at_zone = 255;
    return 0;
}

int pe_re_session_start(PeReSession *s, const char *store, const PeReGame *g, uint32_t player, char *why, size_t n) {
    memset(s, 0, sizeof *s);
    if (!g) { snprintf(why, n, "no such game"); return -1; }
    s->game = g, s->player = player, s->store = store;
    pe_re_flag_set(&s->progress.flags, PE_RE_FG_STATUS, 0, player != 0);   /* who's playing (pe_re_scd.h) */
    if (load(s, store, g->stage, g->room, why, n)) { s->game = NULL; return -1; }
    char items[PE_IMP_ID_MAX];
    snprintf(items, sizeof items, "%s:items/all", g->game);
    if (pe_imp_catalog_itemtable(store, items, &s->items, NULL, 0)) s->items = NULL;   /* names fall back to numbers */
    return 0;
}

int pe_re_session_enter(PeReSession *s, const char *store, uint32_t stage, uint32_t room, int camera, double x,
                        double z, int facing, char *why, size_t n) {
    if (!s->game) { snprintf(why, n, "no session"); return -1; }
    if (load(s, store, stage, room, why, n)) return -1;
    s->camera = camera;
    pe_re_session_place(s, x, z, facing);
    return 0;
}

void pe_re_door_sound_id(const char *game, const PeImpDoor *d, unsigned wave, char *out, size_t n) {
    snprintf(out, n, "%s:sound/door%02x/wave%02u", game, d->texture < 55 ? (unsigned)d->texture : 0u, wave);
}

int pe_re_floor_of(int y) {
    const int f = (y - PE_RE2_FLOOR_HEIGHT / 2) / -PE_RE2_FLOOR_HEIGHT;
    return f < 0 ? 0 : f > 31 ? 31 : f;
}

int pe_re_facing_of(int game_dir) { return (game_dir - 1024) & 4095; }

void pe_re_session_place(PeReSession *s, double x, double z, int facing) {
    s->x = x, s->z = z, s->facing = facing & 4095;
}

/* Through door `d`: locked says why; else the next room, placed as the door says. */
static PeReChange take_door(PeReSession *s, const char *store, const PeImpDoor *d, char *why, size_t n) {
    if (pe_re_door_try(&s->progress, d, s->items, s->said, sizeof s->said) == PE_RE_DOOR_LOCKED) return PE_RE_SAID;
    const uint32_t stage = d->stage == PE_RE_STAGE_HERE ? s->stage : d->stage;
    if (!pe_re_session_enter(s, store, stage, d->room, d->camera, d->next_x, d->next_z, pe_re_facing_of(d->next_dir), why, n)) {
        s->floor = pe_re_floor_of(d->next_y);
        s->door = *d;
        s->doors_taken++;
        return PE_RE_ROOM;
    }
    return s->said[0] ? PE_RE_SAID : PE_RE_STAY;
}

static PeReChange say_message(PeReSession *s, const uint8_t data[6]) {
    const uint32_t m = (uint32_t)data[0] | (uint32_t)data[1] << 8;
    return pe_re_message(s->logic, m, s->said, sizeof s->said) ? PE_RE_STAY : PE_RE_SAID;
}

/* Sce_Event: Event_exec(data word 0, data byte 3). The game stops the scheduler's zone checks while
 * an event plays (Stop_flg); that stop isn't kept here, so a zone set off every frame doesn't
 * restart an event that's still running (`again` 0). */
static void start_event(PeReSession *s, const uint8_t data[6], int again) {
    const unsigned level = (unsigned)data[0] | (unsigned)data[1] << 8, proc = data[3];
    if (!again && (level < PE_RE_TASKS ? s->run.task[level].status && s->run.task[level].proc == (int)proc
                                       : pe_re_scd_running(&s->run, proc)))
        return;
    pe_re_scd_exec(s->logic, &s->run, level, proc);
    s->events++;
}

/* What a zone does, by its kind (SCE_AT.C's handlers). */
static PeReChange zone_act(PeReSession *s, const char *store, const PeReZoneHit *h, int examine, char *why, size_t n) {
    s->zone = *h;
    switch (h->sce) {
    case PE_RE_ZONE_DOOR: {
        PeImpDoor d;
        if (h->from != PE_RE_FROM_DOOR || pe_imp_logic_door(s->logic, h->index, &d)) return PE_RE_STAY;
        return take_door(s, store, &d, why, n);
    }
    case PE_RE_ZONE_ITEM: {
        PeImpPickup pk;
        if (h->from != PE_RE_FROM_PICKUP || pe_imp_logic_pickup(s->logic, h->index, &pk)) return PE_RE_STAY;
        pe_re_take(&s->progress, s->logic, &pk, s->items, s->said, sizeof s->said);
        return PE_RE_SAID;
    }
    case PE_RE_ZONE_MESSAGE: return say_message(s, h->data);
    case PE_RE_ZONE_EVENT: start_event(s, h->data, examine); return PE_RE_STAY;
    case PE_RE_ZONE_FLAG:                                  /* Sce_Flg_chg: group, bit, on/off */
        pe_re_flag_set(&s->progress.flags, (unsigned)h->data[0] | (unsigned)h->data[1] << 8,
                       ((unsigned)h->data[2] | (unsigned)h->data[3] << 8) & 0xFF, h->data[4] | h->data[5]);
        return PE_RE_STAY;
    case PE_RE_ZONE_SAVE: case PE_RE_ZONE_ITEMBOX: case PE_RE_ZONE_STATUS: case PE_RE_ZONE_WINDOWS:
        /* for the mod: a typewriter, an item box, the pocket, a computer (pe_re_session_computer) */
        return PE_RE_ZONE;
    default: return PE_RE_STAY;                            /* water, damage, drawers, ...: not yet */
    }
}

PeReChange pe_re_session_step(PeReSession *s, const char *store, double x, double z, int action, char *why, size_t n) {
    PeImpDoor d;
    PeImpPickup pk;
    PeReZoneHit hits[16];
    if (!s->game || !s->logic) return PE_RE_STAY;
    s->x = x, s->z = z;
    if (action) {
        s->said[0] = 0;
        /* an examine: the game's own check (the point in front, or underfoot, by the zone's bits) */
        if (pe_re_zone_check(s->logic, &s->run, &s->progress, x, z, s->facing, s->floor, 1, s->progress.flags.work,
                             hits, 1) == 1) {
            s->at_zone = hits[0].id;                          /* At_sce_no: the zone she set off */
            const PeReChange c = zone_act(s, store, &hits[0], 1, why, n);
            if (c != PE_RE_STAY || s->said[0] || hits[0].sce == PE_RE_ZONE_EVENT || hits[0].sce == PE_RE_ZONE_FLAG)
                return c;
        }
        /* else, more forgiving than the game: a door, pickup or message zone she stands in */
        if (pe_re_door_at(s->logic, &s->run, x, z, &d) >= 0) return take_door(s, store, &d, why, n);
        if (pe_re_pickup_at(s->logic, &s->run, &s->progress, x, z, &pk) >= 0) {
            pe_re_take(&s->progress, s->logic, &pk, s->items, s->said, sizeof s->said);
            return PE_RE_SAID;
        }
        const int message = pe_re_message_at(s->logic, &s->run, x, z);
        if (message >= 0 && !pe_re_message(s->logic, (uint32_t)message, s->said, sizeof s->said)) return PE_RE_SAID;
    }
    /* every frame: the zones that don't wait for an examine */
    const int nh = pe_re_zone_check(s->logic, &s->run, &s->progress, x, z, s->facing, s->floor, 0,
                                    s->progress.flags.work, hits, 16);
    if (nh > 0) s->at_zone = hits[nh - 1].id;              /* the last one handled, as Sce_at_check leaves it */
    int message_zone = -1;
    for (int i = 0; i < nh; i++) {
        if (hits[i].sce == PE_RE_ZONE_MESSAGE) {           /* once each time she walks into it */
            message_zone = hits[i].id;
            if (s->auto_message == hits[i].id || (s->talking && s->talking())) continue;
            s->auto_message = hits[i].id;
            if (say_message(s, hits[i].data) == PE_RE_SAID) return PE_RE_SAID;
            continue;
        }
        if (hits[i].sce == PE_RE_ZONE_ITEM || hits[i].sce == PE_RE_ZONE_SAVE || hits[i].sce == PE_RE_ZONE_ITEMBOX ||
            hits[i].sce == PE_RE_ZONE_STATUS || hits[i].sce == PE_RE_ZONE_WINDOWS)
            continue;                                      /* those only ever answer an examine */
        const PeReChange c = zone_act(s, store, &hits[i], 0, why, n);
        if (c != PE_RE_STAY) return c;
    }
    if (message_zone < 0) s->auto_message = -1;
    if (s->run.ran && s->run.camera_lock) return PE_RE_STAY;   /* the scripts hold the camera */
    const int camera = pe_re_camera_at(s->logic, s->camera, x, z);
    if (camera == s->camera) return PE_RE_STAY;
    s->camera = camera;
    return PE_RE_CAMERA;
}

PeReChange pe_re_session_tick(PeReSession *s) {
    if (!s->game || !s->logic) return PE_RE_STAY;
    const PeReScdHost host = host_of(s);
    s->run.camera = s->camera;
    {   /* the player's fields, as the scripts read them (RE2's facing: 0 faces +x) */
        int16_t *me = s->run.works[0];
        me[0] = 1, me[PE_RE_MEMBER_POS_X] = (int16_t)lround(s->x), me[PE_RE_MEMBER_POS_Z] = (int16_t)lround(s->z);
        me[PE_RE_MEMBER_POS_Y] = (int16_t)(-1800 * s->floor), me[PE_RE_MEMBER_NFLOOR] = (int16_t)s->floor;
        me[PE_RE_MEMBER_CDIR_Y] = (int16_t)((s->facing + 1024) & 4095);
        me[PE_RE_MEMBER_AT_SCE_NO] = (int16_t)s->at_zone;   /* last frame's zone hit, then cleared (Sce_work_clr_at) */
        s->at_zone = 255;
        s->run.player_set = 0;
    }
    pe_re_scd_tick(s->logic, &s->progress.flags, &host, &s->run);
    if (s->run.player_set) {                               /* a script put her somewhere */
        const int16_t *me = s->run.works[0];
        s->x = me[PE_RE_MEMBER_POS_X], s->z = me[PE_RE_MEMBER_POS_Z];
        s->facing = pe_re_facing_of(me[PE_RE_MEMBER_CDIR_Y]);
        s->moved = 1;
    }
    for (int i = 0; i < s->run.naot_on; i++) {             /* aot_on: a zone set off by its script */
        PeReZoneHit h;
        if (pe_re_zone_by_id(s->logic, &s->run, &s->progress, s->run.aot_on[i], &h)) continue;
        char why[8];
        const PeReChange c = zone_act(s, s->store, &h, 1, why, sizeof why);
        if (c == PE_RE_SAID || c == PE_RE_ZONE) { s->run.naot_on = 0; return c; }
    }
    s->run.naot_on = 0;
    if (s->run.dest_moved) {                               /* plc_dest: she's moved for the cutscene */
        s->x = s->run.dest_x, s->z = s->run.dest_z, s->run.dest_moved = 0, s->moved = 1;
    }
    if (s->run.ran && s->run.camera != s->camera) {        /* a script cut to another camera */
        s->camera = s->run.camera;
        return PE_RE_CAMERA;
    }
    const int m = s->run.message;
    s->run.message = -1;
    if (m >= 0 && !pe_re_message(s->logic, (uint32_t)m, s->said, sizeof s->said)) return PE_RE_SAID;
    return PE_RE_STAY;
}

/* RE2's computers are engine code (PC_EVENT.C), not room scripts. The main hall's (Computer200,
 * room 2-00): its door-lock service releases the hall's side doors with the Blue Card (item 0x35),
 * setting Common flag 0x46 and taking the card; the room's script then opens the doors. */
#define RE2_BLUE_CARD 0x35
#define RE2_COMMON_FLAGS 4
#define RE2_HALL_DOORS_RELEASED 0x46

static int main_hall(const PeReSession *s) {
    return s->game && !strcmp(s->game->game, "re2") && s->stage == 1 && s->room == 0;
}

PeReComputer pe_re_session_computer(PeReSession *s, int use) {
    s->said[0] = 0;
    if (!main_hall(s)) {
        snprintf(s->said, sizeof s->said, "The computer doesn't respond.");
        return PE_RE_COMPUTER_SAID;
    }
    if (pe_re_flag(&s->progress.flags, RE2_COMMON_FLAGS, RE2_HALL_DOORS_RELEASED)) {
        snprintf(s->said, sizeof s->said, "Door lock service: the hall's side doors are released.");
        return PE_RE_COMPUTER_SAID;
    }
    if (!pe_re_has(&s->progress, RE2_BLUE_CARD)) {
        snprintf(s->said, sizeof s->said, "Door lock service: the hall's side doors are locked. A card key releases them.");
        return PE_RE_COMPUTER_SAID;
    }
    if (!use) return PE_RE_COMPUTER_ASK;                   /* "Use the Blue Card?" is the mod's to ask */
    pe_re_flag_set(&s->progress.flags, RE2_COMMON_FLAGS, RE2_HALL_DOORS_RELEASED, 1);
    pe_re_lose(&s->progress, RE2_BLUE_CARD, 0);
    snprintf(s->said, sizeof s->said, "Door lock service: the hall's side doors are released.");
    return PE_RE_COMPUTER_DONE;
}

PeReChange pe_re_session_use(PeReSession *s, uint16_t item) {
    char name[48];
    s->said[0] = 0;
    if (!s->game || !pe_re_has(&s->progress, item)) return PE_RE_STAY;
    if (!pe_re_flag(&s->progress.flags, PE_RE_FG_USE, item)) {   /* the status screen's "can't use it here" */
        pe_re_item_name(s->items, item, name, sizeof name);
        snprintf(s->said, sizeof s->said, "You can't use the %s here.", name);
        return PE_RE_SAID;
    }
    s->progress.flags.work[PE_RE_WORK_USE_ID] = (int16_t)item;  /* the room's script takes it from here */
    s->uses++;
    return PE_RE_STAY;
}

static unsigned enemy_group(const PeReSession *s) {        /* Sce_em_set's flag set */
    return s->stage >= 3 || pe_re_flag(&s->progress.flags, PE_RE_FG_STATUS, 6) ? PE_RE_FG_ENEMY + 1 : PE_RE_FG_ENEMY;
}

int pe_re_session_enemies(const PeReSession *s, PeReEnemyPlacement *out, int max) {
    uint32_t size = 0;
    int n = 0;
    if (!s->logic || !s->run.ran) return 0;
    const uint8_t *rdt = pe_imp_logic_section(s->logic, PE_IMP_LOGIC_RDT0, NULL, &size);
    for (int i = 0; rdt && i < s->run.nenemy && n < max; i++) {
        const uint32_t at = s->run.enemy[i];
        if (at + 22 > size) continue;
        const uint8_t *e = rdt + at;                        /* SCE_EM_SET_WORK, 22 bytes */
        if (e[9] != 0xFF && pe_re_flag(&s->progress.flags, enemy_group(s), e[9])) continue;   /* killed */
        PeReEnemyPlacement *p = &out[n++];
        p->slot = e[2], p->kind = e[3], p->behaviour = (uint16_t)(e[4] | e[5] << 8);
        p->floor = e[6], p->sound = e[7], p->model = e[8], p->killed_flag = e[9];
        p->x = (int16_t)(e[10] | e[11] << 8), p->y = (int16_t)(e[12] | e[13] << 8);
        p->z = (int16_t)(e[14] | e[15] << 8), p->dir = (int16_t)(e[16] | e[17] << 8);
        p->motion = (int16_t)(e[18] | e[19] << 8), p->ctr = (int16_t)(e[20] | e[21] << 8);
    }
    return n;
}

int pe_re_session_defeated(PeReSession *s, uint8_t killed_flag) {
    if (!s->game || killed_flag == 0xFF) return -1;
    pe_re_flag_set(&s->progress.flags, enemy_group(s), killed_flag, 1);
    return 0;
}

int pe_re_enemy_lists_equal(const PeReEnemyPlacement *a, const PeReEnemyPlacement *b, int count) {
    for (int i = 0; i < count; i++, a++, b++) {
        if (a->slot != b->slot || a->kind != b->kind || a->behaviour != b->behaviour ||
            a->floor != b->floor || a->sound != b->sound || a->model != b->model ||
            a->killed_flag != b->killed_flag || a->x != b->x || a->y != b->y ||
            a->z != b->z || a->dir != b->dir || a->motion != b->motion || a->ctr != b->ctr)
            return 0;
    }
    return 1;
}

void pe_re_session_end(PeReSession *s) {
    free(s->logic);
    free(s->items);
    memset(s, 0, sizeof *s);
}
