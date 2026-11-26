/* A play session in an imported game. See pe_re_session.h. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pe_re_session.h"
#include "pe_re_zone.h"
#include "pe_imp_rdt.h"
#include "pe_imp_pictures.h"
#include "re_encounter_rooms.h"

/* First rooms: RE2 and RE3 open in the street (room 1-00); RE1 in the mansion's main hall (1-06).
 * Only RE2 has the cameras and collision the runtime uses so far (design doc, phase 4). */
static const PeReGame k_games[PE_RE_GAMES] = {
    {"re1", "Resident Evil", 0, 0x06, 0},
    {"re2", "Resident Evil 2", 0, 0x00, 1, 0, 0x04, -0x4600, -0x54DA, 400},   /* B: 1-04 (TITLE.C) */
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
static int camera_switch_at(const PeImpRoomLogic *l, int camera, int floor, double x, double z) {
    uint32_t n = 0;
    int group = -1;
    if (!l || !pe_imp_logic_section(l, PE_IMP_LOGIC_CAMZ, &n, NULL)) return camera;
    for (uint32_t i = 0; i < n; i++) {
        PeImpCameraZone c;
        if (pe_imp_logic_camera_zone(l, i, &c)) continue;
        const int own = l->game == PE_IMP_RE1 ? c.flag == 1 : c.from != group;
        group = c.from;
        if (own || c.from != camera || c.to == camera) continue;
        /* RE2 VCUT stores Be_flg in the low byte and nFloor in the high.
         * Opposite cuts can share a footprint when they serve different floors. */
        if (l->game == PE_IMP_RE2) {
            const int zone_floor = c.flag >> 8;
            if (!(c.flag & 0xff) || (zone_floor != 0xff && zone_floor != floor)) continue;
        }
        if (pe_re_in_quad(c.x, c.z, x, z)) return c.to;
    }
    return camera;
}

int pe_re_camera_at(const PeImpRoomLogic *l, int camera, int floor, double x, double z) {
    const int next = camera_switch_at(l, camera, floor, x, z);
    /* Where both cuts apply, keep the view the player entered with until
     * they reach the part of the switch that only points one way. */
    if (next != camera && l->game == PE_IMP_RE2 && camera_switch_at(l, next, floor, x, z) == camera)
        return camera;
    return next;
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
int pe_re2_bgm_room_index(unsigned stage, unsigned room)
{
    /* RE2 SND.C's nroom[] gives each stage's first entry. */
    static const unsigned first[] = {0x00, 0x1f, 0x3d, 0x4d, 0x60, 0x6c, 0x86, PE_RE2_BGM_ROOMS};
    if (stage >= sizeof first / sizeof *first - 1 || room >= first[stage + 1] - first[stage]) return -1;
    return (int)(first[stage] + room);
}

static int set_room_bgm(void *ctx, unsigned stage, unsigned room, uint16_t cue)
{
    const int index = pe_re2_bgm_room_index(stage, room);
    if (index < 0) return -1;
    PeReSession *s = ctx;
    s->bgm_override[index] = cue;
    s->bgm_overridden[index] = 1;
    return 0;
}

static int core_effect(void *ctx, unsigned id, unsigned palette, PeImpRe2CoreEffect *out) {
    PeReSession *s = ctx;
    if (id >= 8 || !s->store) return -1;
    if (!s->core_effect_checked) {
        s->core_effect_checked = 1;
        pe_imp_catalog_pictures(s->store, "re2:effects/core-bank", &s->core_effect_bank, NULL, 0);
        pe_imp_catalog_pictures(s->store, "re2:effects/core-palette", &s->core_effect_palettes, NULL, 0);
    }
    if (!s->core_effect_bank || !s->core_effect_palettes) return -1;
    return pe_imp_re2_core_bank_effect(s->core_effect_bank, s->core_effect_palettes, id, palette, out);
}

static PeReScdHost host_of(PeReSession *s) {
    PeReScdHost h = {has_item, talking, s->key, s->key_trg, s, lose_item, give_item, set_room_bgm, core_effect};
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
    /* Set_room loads the sound bank before it runs the new room's scripts. */
    const int bgm_index = pe_re2_bgm_room_index(stage, room);
    s->entry_bgm_overridden = bgm_index >= 0 && s->bgm_overridden[bgm_index];
    if (s->entry_bgm_overridden) s->entry_bgm = s->bgm_override[bgm_index];
    ++s->room_loads;
    s->progress.item_group = stage >= 4 ? PE_RE_FG_ITEM2 : PE_RE_FG_ITEM;   /* Item_flg2 from stage 5 on */
    const PeReScdHost host = host_of(s);
    memset(s->progress.flags.bits[PE_RE_FG_USE], 0, sizeof s->progress.flags.bits[PE_RE_FG_USE]);   /* Sce_work_clr */
    pe_re_scd_run(l, &s->progress.flags, &host, &s->run);   /* what its scripts set up */
    s->auto_message = -1;
    s->at_zone = 255;
    return 0;
}

static int start(PeReSession *s, const char *store, const PeReGame *g, uint32_t player, int b, char *why, size_t n) {
    memset(s, 0, sizeof *s);
    if (!g) { snprintf(why, n, "no such game"); return -1; }
    if (b && !g->b_room) { snprintf(why, n, "%s has no second scenario", g->title); return -1; }
    s->game = g, s->player = player, s->store = store;
    pe_re_flag_set(&s->progress.flags, PE_RE_FG_STATUS, 0, player != 0);   /* who's playing (pe_re_scd.h) */
    pe_re_flag_set(&s->progress.flags, PE_RE_FG_STATUS, 1, b != 0);        /* STAT_SCENARIO_B (FLAG.H) */
    if (load(s, store, b ? g->b_stage : g->stage, b ? g->b_room : g->room, why, n)) { s->game = NULL; return -1; }
    if (b) pe_re_session_place(s, g->b_x, g->b_z, pe_re_facing_of(g->b_dir));
    char items[PE_IMP_ID_MAX];
    snprintf(items, sizeof items, "%s:items/all", g->game);
    if (pe_imp_catalog_itemtable(store, items, &s->items, NULL, 0)) s->items = NULL;   /* names fall back to numbers */
    return 0;
}

int pe_re_session_start(PeReSession *s, const char *store, const PeReGame *g, uint32_t player, char *why, size_t n) {
    return start(s, store, g, player, 0, why, n);
}

int pe_re_session_start_b(PeReSession *s, const char *store, const PeReGame *g, uint32_t player, char *why, size_t n) {
    return start(s, store, g, player, 1, why, n);
}

int pe_re_session_start_hunk(PeReSession *s, const char *store, const PeReGame *g, char *why, size_t n) {
    if (!g || strcmp(g->game, "re2")) { snprintf(why, n, "only RE2 has the 4th Survivor"); return -1; }
    if (start(s, store, g, 0, 0, why, n)) return -1;
    pe_re_flag_set(&s->progress.flags, 0, 0x1C, 1);        /* SYS_TIMER (System_flg 1 << 3: script bit 0x1C) */
    pe_re_flag_set(&s->progress.flags, PE_RE_FG_STATUS, 6, 1);   /* STAT_SPECIAL_RUN (1 << 25) */
    s->player_id = 12;                                     /* HUNK (To-fu: 13) */
    free(s->logic), s->logic = NULL;                       /* his first room instead: 3-04, as Title_game_init puts him */
    int16_t *w = s->progress.flags.work;
    w[PE_RE_WORK_STAGE] = 2, w[PE_RE_WORK_ROOM] = 4, w[PE_RE_WORK_CUT] = 3;
    if (pe_re_session_enter(s, store, 2, 4, 3, -0x1CFF, -0x5FAC, pe_re_facing_of(0x800), why, n)) { s->game = NULL; return -1; }
    return 0;
}

int pe_re_session_enter(PeReSession *s, const char *store, uint32_t stage, uint32_t room, int camera, double x,
                        double z, int facing, char *why, size_t n) {
    if (!s->game) { snprintf(why, n, "no session"); return -1; }
    int16_t *w = s->progress.flags.work;                   /* the game's room bookkeeping, as its scripts read it */
    if (s->logic) {                                        /* through a door (GAME.C); a resumed save keeps its own */
        w[PE_RE_WORK_ROOM_OLD] = (int16_t)(s->room + (s->stage + 1) * 256);
        w[PE_RE_WORK_STAGE] = (int16_t)stage, w[PE_RE_WORK_ROOM] = (int16_t)room, w[PE_RE_WORK_CUT] = (int16_t)camera;
        memset(s->progress.flags.bits[5], 0, 4);          /* Room_flg[0]: each room's own (SCE0.C Sce_work_clr_set) */
    }
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

int pe_re_session_controls_locked(const PeReSession *s) {
    return s->run.player_dest.moving || s->run.actor_stopped[0] ||
           pe_re_flag(&s->progress.flags, PE_RE_FG_STOP, PE_RE_STOP_PAD_LOCK);
}

int pe_re_session_describe_scene(const PeReSession *s, char *out, size_t n) {
    uint32_t size = 0;
    const uint8_t *rdt = s->logic ? pe_imp_logic_section(s->logic, PE_IMP_LOGIC_RDT0, NULL, &size) : NULL;
    size_t used = 0;
    int running = 0;
    if (out && n) used = (size_t)snprintf(out, n, "%s %d-%02X camera %d:", s->game ? s->game->game : "?",
                                          s->stage + 1, (unsigned)s->room, s->camera);
    for (int k = 2; k < PE_RE_TASKS; k++) {                /* 0 and 1 run the room's init and main loop */
        const PeReScdTask *t = &s->run.task[k];
        if (!t->status) continue;
        running++;
        if (out && used < n) {
            const unsigned op = rdt && t->pc < size ? rdt[t->pc] : 0xFFu;
            const int w = snprintf(out + used, n - used, " task %d proc %d at %u (op %02X, work %d);", k, t->proc,
                                   (unsigned)t->pc, op, t->work);
            used = w < 0 ? n : used + (size_t)w;
        }
    }
    if (!running && out && used < n) snprintf(out + used, n - used, " no scene running");
    return running;
}

int pe_re_session_unstick(PeReSession *s, char *report, size_t n) {
    PeReScdRun *r = &s->run;
    int ended = 0;
    pe_re_session_describe_scene(s, report, n);
    for (int k = 2; k < PE_RE_TASKS; k++) {
        PeReScdTask *t = &r->task[k];
        if (!t->status) continue;
        t->status = 0;
        ended++;
    }
    pe_re_flag_set(&s->progress.flags, PE_RE_FG_STOP, PE_RE_STOP_PAD_LOCK, 0);
    pe_re_flag_set(&s->progress.flags, PE_RE_FG_STOP, PE_RE_STOP_EVENTS, 0);
    pe_re_flag_set(&s->progress.flags, PE_RE_FG_STATUS, PE_RE_STAT_CINESCO, 0);
    r->player_dest.moving = 0;
    r->actor_stopped[0] = 0;
    r->arrived[0] = 0;
    r->pl_motion[0] = (PeReSceneMotion){0};
    r->camera_lock = 0;
    return ended;
}

uint32_t pe_re_session_scene_mark(const PeReSession *s) {
    uint32_t mark = 2166136261u;                           /* FNV-1a over the event tasks' state */
    for (int k = 2; k < PE_RE_TASKS; k++) {
        const PeReScdTask *t = &s->run.task[k];
        const uint32_t words[3] = {t->status, (uint32_t)t->proc, t->pc};
        for (int i = 0; i < 3; i++) mark = (mark ^ words[i]) * 16777619u;
    }
    return mark;
}

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

/* Sce_Event checks the event stop flag independently of the pad lock. */
static void start_event(PeReSession *s, const uint8_t data[6], int again) {
    if (pe_re_flag(&s->progress.flags, PE_RE_FG_STOP, PE_RE_STOP_EVENTS)) return;
    const unsigned level = (unsigned)data[0] | (unsigned)data[1] << 8, proc = data[3];
    if (!again && (level < PE_RE_TASKS ? s->run.task[level].status && s->run.task[level].proc == (int)proc
                                       : pe_re_scd_running(&s->run, proc)))
        return;
    pe_re_scd_exec(s->logic, &s->run, level, proc);
    s->events++;
}

static PeReChange take_pickup(PeReSession *s, const PeImpPickup *pk) {
    pe_re_take_equipment_or_item(&s->progress, s->logic, pk, s->items, s->said, sizeof s->said,
                                 s->game->game, s->pickup_weapon, s->pickup_ammo);
    return PE_RE_SAID;
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
        return take_pickup(s, &pk);
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
    /* locked (a scene walks her, or holds the pad): no examine, but the zones that need no button still
     * count, as GAME.C runs Sce_at every frame whatever the scene (6-01's lift walks her into its door) */
    if (pe_re_session_controls_locked(s)) goto every_frame;
    if (action) {
        s->said[0] = 0;
        /* an examine: the game's own check (the point in front, or underfoot, by the zone's bits) */
        if (pe_re_zone_check(s->logic, &s->run, &s->progress, x, z, s->facing, s->floor, 1, s->progress.flags.work,
                             hits, 1) == 1) {
            s->at_zone = hits[0].id;                          /* At_sce_no: the zone she set off */
            const PeReChange c = zone_act(s, store, &hits[0], 1, why, n);
            if (c != PE_RE_STAY || s->said[0]) return c;
            if (hits[0].sce == PE_RE_ZONE_EVENT || hits[0].sce == PE_RE_ZONE_FLAG) goto every_frame;   /* then the same */
        }                                                  /* frame's underfoot zones (Sce_at), before its event runs */
        /* else, more forgiving than the game: a door, pickup or message zone she stands in */
        if (pe_re_door_at(s->logic, &s->run, x, z, &d) >= 0) return take_door(s, store, &d, why, n);
        if (pe_re_pickup_at(s->logic, &s->run, &s->progress, x, z, &pk) >= 0) return take_pickup(s, &pk);
        const int message = pe_re_message_at(s->logic, &s->run, x, z);
        if (message >= 0 && !pe_re_message(s->logic, (uint32_t)message, s->said, sizeof s->said)) return PE_RE_SAID;
    }
every_frame:;                                              /* every frame: the zones that don't wait for an examine */
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
camera:
    if (s->run.ran && s->run.camera_lock) return PE_RE_STAY;   /* the scripts hold the camera */
    /* cut_replace swapped cameras in the switch zones: from the zones' own number for this camera, and
     * the camera their target now names (SCE1.C Cut_replace rewrites each zone's Fcut and Tcut) */
    const PeReScdRun *r = &s->run;
    const int from = r->cut_swapped && s->camera >= 0 && s->camera < 256 ? r->cut_from[s->camera] : s->camera;
    int camera = pe_re_camera_at(s->logic, from, s->floor, x, z);
    if (camera == from) camera = s->camera;
    else if (r->cut_swapped && camera >= 0 && camera < 256) camera = r->cut_to[camera];
    if (camera == s->camera) return PE_RE_STAY;
    s->camera = camera;
    return PE_RE_CAMERA;
}

/* RE2's partner is enemy code, not room script: in scenario B Sherry collapses in 5-05 (its scene sets
 * Common 0x6D; "She's unconscious...") and lies there, placed as type 0x4F, until Claire comes back
 * for her (Common 0x78). No script clears STAT_SUB_PLAYER (status bit 3) for that, and the lab's lift
 * (6-01) won't run while it's set, so it's kept off here for that stretch. (Inferred from the scripts:
 * the 0x4F enemy's code isn't in the bio2-ds sources read.) */
#define RE2_COMMON 4                                       /* Common_flg's group */
static void re2_partner(PeReSession *s) {
    PeReFlags *f = &s->progress.flags;
    if (!s->game || strcmp(s->game->game, "re2") || !pe_re_flag(f, PE_RE_FG_STATUS, 1)) return;
    if (pe_re_flag(f, RE2_COMMON, 0x6D) && !pe_re_flag(f, RE2_COMMON, 0x78)) pe_re_flag_set(f, PE_RE_FG_STATUS, 3, 0);
}

/* Script coordinates are authoritative: these paths may cross room collision
 * that the scene is about to toggle. Keep fractional progress until arrival. */
static void step_player_destination(PeReSession *s) {
    PeReScdRun *r = &s->run;
    if (!r->player_dest.moving || r->actor_stopped[0]) return;
    int facing = s->cdir_y;
    r->player_dest.x = r->works[0][PE_RE_MEMBER_DEST_X];
    r->player_dest.z = r->works[0][PE_RE_MEMBER_DEST_Z];
    if (pe_re_actor_destination_step(&r->player_dest, &s->x, &s->z, &facing)) {
        r->player_dest.moving = 0;
        r->arrived[0] = r->player_dest.arrival;
    }
    s->cdir_y = (int16_t)facing;
    s->facing = pe_re_facing_of(facing);
    s->moved = 1;
}

PeReChange pe_re_session_tick(PeReSession *s) {
    if (!s->game || !s->logic) return PE_RE_STAY;
    re2_partner(s);
    const PeReScdHost host = host_of(s);
    s->run.camera = s->camera;
    s->progress.flags.work[PE_RE_WORK_CUT] = (int16_t)s->camera;   /* Cut_no: the camera now (the scripts test it) */
    {   /* the player's fields, as the scripts read them (RE2's facing: 0 faces +x) */
        int16_t *me = s->run.works[0];
        if (s->player_id) me[PE_RE_MEMBER_ID] = s->player_id;
        s->run.player_row = (uint8_t)((s->player_id ? s->player_id : s->player) + 0x10);   /* SPL_TBL.H rows: Id + 0x10 */
        me[0] = 1, me[PE_RE_MEMBER_POS_X] = (int16_t)lround(s->x), me[PE_RE_MEMBER_POS_Z] = (int16_t)lround(s->z);
        me[PE_RE_MEMBER_POS_Y] = (int16_t)(-1800 * s->floor), me[PE_RE_MEMBER_NFLOOR] = (int16_t)s->floor;
        if (pe_re_facing_of(s->cdir_y) != s->facing) s->cdir_y = (int16_t)((s->facing + 1024) & 4095);
        me[PE_RE_MEMBER_CDIR_Y] = s->cdir_y;              /* the game's Cdir_y isn't wrapped */
        me[PE_RE_MEMBER_AT_SCE_NO] = (int16_t)s->at_zone;   /* last frame's zone hit, then cleared (Sce_work_clr_at) */
        s->at_zone = 255;
        s->run.player_set = 0;
    }
    pe_re_scd_tick(s->logic, &s->progress.flags, &host, &s->run);
    if (s->run.player_set) {                               /* a script put her somewhere */
        const int16_t *me = s->run.works[0];
        s->x = me[PE_RE_MEMBER_POS_X], s->z = me[PE_RE_MEMBER_POS_Z];
        s->floor = pe_re_floor_of(me[PE_RE_MEMBER_POS_Y]);   /* pos_set sets her height too: a lift's other floor */
        s->facing = pe_re_facing_of(me[PE_RE_MEMBER_CDIR_Y]);
        s->cdir_y = me[PE_RE_MEMBER_CDIR_Y];
        s->moved = 1;
    }
    step_player_destination(s);
    /* Sce_at (SCE_AT.C) after the moves: the enemies' zones (bit 0x02), then the partner's (0x08). Their
     * floors from their height; the partner's height isn't kept here, and she follows, so it's the player's. */
    for (int w = 1; w < 18; w++) {
        const int16_t *e = s->run.works[w];
        if (!e[0]) continue;
        pe_re_zone_check_actor(s->logic, &s->run, &s->progress, e[PE_RE_MEMBER_POS_X], e[PE_RE_MEMBER_POS_Z],
                               pe_re_facing_of(e[PE_RE_MEMBER_CDIR_Y]), w == 1 ? s->floor : pe_re_floor_of(e[PE_RE_MEMBER_POS_Y]),
                               w == 1 ? PE_RE_SAT_PARTNER : PE_RE_SAT_ENEMY, s->progress.flags.work);
    }
    for (int i = 0; i < s->run.naot_on; i++) {             /* aot_on: a zone set off by its script */
        PeReZoneHit h;
        if (pe_re_zone_by_id(s->logic, &s->run, &s->progress, s->run.aot_on[i], &h)) continue;
        char why[8];
        const PeReChange c = zone_act(s, s->store, &h, 1, why, sizeof why);
        if (c == PE_RE_SAID || c == PE_RE_ZONE) { s->run.naot_on = 0; return c; }
    }
    s->run.naot_on = 0;
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

/* RE2's key-item pairs (item numbers as in its ItemTable): the halves of the Jaguar Stone, and the C4
 * with its detonator. Weapon parts and herbs mix too in the game; nothing in the story waits on those. */
static const uint16_t RE2_MIX[][3] = { {0x38, 0x39, 0x37}, {0x40, 0x41, 0x42} };

uint16_t pe_re_combined(const PeReSession *s, uint16_t a, uint16_t b) {
    if (!s->game || strcmp(s->game->game, "re2")) return 0;
    for (size_t i = 0; i < sizeof RE2_MIX / sizeof *RE2_MIX; i++)
        if ((RE2_MIX[i][0] == a && RE2_MIX[i][1] == b) || (RE2_MIX[i][0] == b && RE2_MIX[i][1] == a)) return RE2_MIX[i][2];
    return 0;
}

uint16_t pe_re_session_combine(PeReSession *s, uint16_t a, uint16_t b) {
    char name[48];
    const uint16_t made = pe_re_combined(s, a, b);
    s->said[0] = 0;
    if (!made || !pe_re_has(&s->progress, a) || !pe_re_has(&s->progress, b)) return 0;
    pe_re_lose(&s->progress, a, 1), pe_re_lose(&s->progress, b, 1);   /* two slots freed: the result fits */
    pe_re_give(&s->progress, made, 1);
    pe_re_item_name(s->items, made, name, sizeof name);
    snprintf(s->said, sizeof s->said, "Made the %s.", name);
    return made;
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

/* EM28.C: the first adult G begins dormant and wakes on Sce_free0. Its Type
 * 1 re-entry placement is already awake. Wait for the room's entrance to end. */
static int adult_g_dormant(const PeReSession *s, const int16_t *work) {
    if (s->stage != 2 || (s->room != 3 && s->room != 9) || work[PE_RE_MEMBER_TYPE] != 0) return 0;
    const unsigned entrance = s->room == 3 ? 16 : 10;
    return !(work[PE_RE_MEMBER_SCE_FREE0] & 1) || pe_re_scd_running(&s->run, entrance) ||
           pe_re_scd_running(&s->run, entrance + 1);
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
        if (e[2] < 16 && s->run.em_dead[e[2]]) continue;
        PeReEnemyPlacement *p = &out[n++];
        p->slot = e[2], p->kind = e[3], p->behaviour = (uint16_t)(e[4] | e[5] << 8);
        p->floor = e[6], p->sound = e[7], p->model = e[8], p->killed_flag = e[9];
        p->x = (int16_t)(e[10] | e[11] << 8), p->y = (int16_t)(e[12] | e[13] << 8);
        p->z = (int16_t)(e[14] | e[15] << 8), p->dir = (int16_t)(e[16] | e[17] << 8);
        p->motion = (int16_t)(e[18] | e[19] << 8), p->ctr = (int16_t)(e[20] | e[21] << 8);
        if (p->slot < 16) {
            const int16_t *work = s->run.works[2 + p->slot];
            if (work[0] && work[PE_RE_MEMBER_ID] == p->kind) {
                p->behaviour = (uint16_t)work[PE_RE_MEMBER_TYPE];
                p->x = work[PE_RE_MEMBER_POS_X], p->y = work[PE_RE_MEMBER_POS_Y], p->z = work[PE_RE_MEMBER_POS_Z];
                p->dir = work[PE_RE_MEMBER_CDIR_Y], p->floor = (uint8_t)work[PE_RE_MEMBER_NFLOOR];
                if (p->kind == 0x28 && adult_g_dormant(s, work)) p->behaviour |= 0x4000;
            }
        }
        if (pe_re2_g_attached_embryo(s->stage, s->room, p) &&
            (!s->run.works[2][0] || s->run.works[2][PE_RE_MEMBER_ID] != 0x28 || s->run.em_dead[0])) n--;
    }
    return n;
}

static double walk_speed(const PeReEnemyPlacement *e, PeReZombieMode mode) {
    if (!pe_re2_ground_pursuit_kind(e->kind)) return 0;
    return e->kind == 0x20 ? 62 : e->kind == 0x22 || e->kind == 0x24 ? 44 : e->kind == 0x25 ? 54 :
           pe_re2_ivy(e->kind) ? 26 : mode == PE_RE_ZOMBIE_CRAWL ? 12 : 22;
}

double pe_re_session_walk_speed(const PeReSession *s, const PeReEnemyPlacement *e) {
    if (!s || !e || !s->game || strcmp(s->game->game, "re2") || e->slot >= 16) return 0;
    const int16_t *work = s->run.works[2 + e->slot];
    return walk_speed(e, pe_re_zombie_mode(e->behaviour & 0x3f, work[PE_RE_MEMBER_ROUTINE0]));
}

int pe_re_session_chase(PeReSession *s, const PeRe2RoomCollision *collision,
                       PeReEnemyPlacement *enemies, int count, double x, double z, int floor) {
    memset(s->enemy_state, 0, sizeof s->enemy_state);
    if (!s->game || strcmp(s->game->game, "re2") || !collision) return 0;
    const int street = pe_re2_opening_street(s->stage, s->room);
    unsigned scripted = 0;
    for (int i = 0; i < PE_RE_TASKS; i++) {
        const PeReScdTask *task = &s->run.task[i];
        if (task->status && task->work >= 2 && task->work < 18) scripted |= 1u << (task->work - 2);
    }
    int moved = 0;
    for (int i = 0; i < count; i++) {
        PeReEnemyPlacement *e = &enemies[i];
        if (e->slot >= 16 || s->run.em_dead[e->slot]) continue;
        PeReEnemyState *state = &s->enemy_state[e->slot];
        int16_t *work = s->run.works[2 + e->slot];
        const int dog = e->kind == 0x20;
        const int licker = e->kind == 0x22;
        const int enhanced_licker = e->kind == 0x24;
        const int spider = e->kind == 0x25;
        const int ivy = pe_re2_ivy(e->kind);
        const int zombie = e->kind >= 0x10 && e->kind < 0x20;
        const unsigned type = e->behaviour & 0x3f;
        const PeReZombieMode mode = pe_re_zombie_mode(type, work[PE_RE_MEMBER_ROUTINE0]);
        *state = PE_RE_ENEMY_FAMILY;
        if (!pe_re2_ground_pursuit_kind(e->kind)) continue;
        if (zombie) {
            if (mode == PE_RE_ZOMBIE_UNSUPPORTED ||
                (mode == PE_RE_ZOMBIE_SCRIPT && (work[PE_RE_MEMBER_ROUTINE1] < 0 || work[PE_RE_MEMBER_ROUTINE1] > 3))) {
                *state = PE_RE_ENEMY_VARIANT;
                continue;
            }
            if (mode == PE_RE_ZOMBIE_CORPSE || mode == PE_RE_ZOMBIE_FEEDING || mode == PE_RE_ZOMBIE_PRONE) {
                *state = mode == PE_RE_ZOMBIE_CORPSE ? PE_RE_ENEMY_CORPSE :
                         mode == PE_RE_ZOMBIE_FEEDING ? PE_RE_ENEMY_FEEDING : PE_RE_ENEMY_PRONE;
                continue;
            }
        }
        *state = PE_RE_ENEMY_SCRIPTED;
        if (s->run.camera_lock || (scripted & (1u << e->slot)) ||
            work[PE_RE_MEMBER_ROUTINE0] == 8 || s->run.scene[e->slot].active ||
            s->run.enemy_dest[e->slot].owned || s->run.actor_stopped[2 + e->slot]) continue;
        /* The field does not require a persistent battle defeat flag. */
        const int mobile = dog ? type <= 2 || type == 4 || type == 6 || type == 9 :
                           licker ? type == 0 || type == 0x10 || type == 0x11 || type == 0x13 :
                           enhanced_licker ? (type & 0xf) == 0 :
                           spider ? (type & 0xf) == 0 :
                           ivy ? (type & 0xf) <= 3 :
                           mode == PE_RE_ZOMBIE_IDLE || mode == PE_RE_ZOMBIE_PURSUIT || mode == PE_RE_ZOMBIE_CRAWL;
        const unsigned held = zombie ? (street ? 0x8000 : 0xc000) : 0xe000;
        *state = PE_RE_ENEMY_DORMANT;
        if (!mobile || (e->behaviour & held) ||
            !work[0] || work[PE_RE_MEMBER_ID] != e->kind) continue;
        *state = PE_RE_ENEMY_FLOOR;
        if (e->floor != floor || pe_re_floor_of(e->y) != floor) continue;
        const double dx = x - e->x, dz = z - e->z;
        const double distance = hypot(dx, dz);
        const int target_dir = (int)lround(atan2(-dz, dx) * (2048.0 / acos(-1.0))) & 4095;
        const int turn = ((target_dir - e->dir + 2048) & 4095) - 2048;
        uint8_t *alerted = &s->run.em_alerted[e->slot];
        *state = PE_RE_ENEMY_RANGE;
        if (zombie) {
            /* EM10: type 6 starts in pursuit. Standing type 0 notices a
             * player in its forward arc or starts after its idle check. */
            uint16_t *idle = &s->run.em_idle_ticks[e->slot];
            if (*idle < UINT16_MAX) ++*idle;
            if (!*alerted && mode == PE_RE_ZOMBIE_IDLE && !(distance < 5000 && abs(turn) < 1024) &&
                !(*idle >= 300 && distance < 7500)) continue;
        } else {
            const double awareness = dog ? 5200 : spider ? 8000 : enhanced_licker ? 3000 : ivy ? 4200 : 3900;
            /* Standard ground variants acquire a target through presentation
             * cues. The field has no RE2 room sound flags, so use a close
             * forward arc for enhanced Lickers and Ivy. */
            if ((enhanced_licker || ivy || spider) && !*alerted && distance > 1600 && abs(turn) >= 1024) continue;
            if (distance > awareness + (*alerted ? 1200 : 0)) {
                *alerted = 0;
                continue;
            }
        }
        *alerted = 1;
        *state = PE_RE_ENEMY_CLOSE;
        if (distance <= (spider ? 1500 : 1200)) continue;
        const double speed = walk_speed(e, mode);
        const double radius = dog ? 210 : spider ? 420 : ivy ? 340 : licker || enhanced_licker ? 310 : 240;
        *state = PE_RE_ENEMY_BLOCKED;
        double next_x = e->x, next_z = e->z, ground = -(double)floor * PE_RE2_FLOOR_HEIGHT;
        /* Native walking turns toward a target over successive updates and
         * applies movement along the actor's forward axis. */
        const int gradual = zombie || enhanced_licker || ivy || spider;
        const int turn_step = spider ? 48 : ivy ? 6 : distance < 3000 ? 24 : 8;
        const int dir = gradual ? (e->dir + (turn < -turn_step ? -turn_step : turn > turn_step ? turn_step : turn)) & 4095 :
                                 target_dir;
        const int turned = gradual && dir != e->dir;
        if (turned) {
            work[PE_RE_MEMBER_CDIR_Y] = e->dir = (int16_t)dir;
            moved++;
        }
        if (street && pe_imp_re2_blocked_on(collision, (unsigned)floor, next_x, next_z, radius)) {
            /* The prone street placement can overlap a wall after standing up. */
            if (!pe_re_free_spot(collision, floor, &next_x, &next_z, (int)radius, 900)) continue;
        } else {
            const double yaw = dir * (acos(-1.0) / 2048);
            pe_imp_re2_slide_on(collision, (unsigned)floor, &next_x, &next_z,
                               cos(yaw) * speed, -sin(yaw) * speed, radius);
        }
        if (next_x < INT16_MIN || next_x > INT16_MAX || next_z < INT16_MIN || next_z > INT16_MAX) continue;
        const int ix = (int)lround(next_x), iz = (int)lround(next_z);
        if (ix == e->x && iz == e->z) continue;
        if (pe_imp_re2_ground(collision, ix, iz, &ground) && pe_re_floor_of((int)lround(ground)) != floor) continue;
        /* Rounding the position for the script's word fields must still leave clearance. */
        if (pe_imp_re2_blocked_on(collision, (unsigned)floor, ix, iz, radius)) continue;
        const int move_dir = gradual ? dir :
            (int)lround(atan2(e->z - iz, ix - e->x) * (2048.0 / acos(-1.0))) & 4095;
        work[PE_RE_MEMBER_POS_X] = e->x = (int16_t)ix;
        work[PE_RE_MEMBER_POS_Y] = e->y = (int16_t)lround(ground);
        work[PE_RE_MEMBER_POS_Z] = e->z = (int16_t)iz;
        work[PE_RE_MEMBER_CDIR_Y] = e->dir = (int16_t)move_dir;
        *state = PE_RE_ENEMY_MOVING;
        if (!turned) moved++;
    }
    return moved;
}

int pe_re_session_alligator(const PeReSession *s, unsigned slot) {
    return s->game && !strcmp(s->game->game, "re2") && s->stage == 3 && s->room == 0x0a && slot == 0 &&
           s->run.works[2][0] && s->run.works[2][PE_RE_MEMBER_ID] == 0x23 && !s->run.em_dead[0];
}

int pe_re_session_final_tyrant(const PeReSession *s, unsigned slot) {
    return s->game && !strcmp(s->game->game, "re2") && s->stage == 6 && s->room == 2 && slot == 0 &&
           s->run.works[2][0] && s->run.works[2][PE_RE_MEMBER_ID] == 0x2b && !s->run.em_dead[0];
}

/* Room 4-0A's proc 10 releases the canister, then sets Room_flg bit 0. */
int pe_re_session_canister_ready(const PeReSession *s) {
    return pe_re_session_alligator(s, 0) && pe_re_flag(&s->progress.flags, 5, 0);
}

void pe_re_session_release_canister(PeReSession *s) {
    if (pe_re_session_alligator(s, 0) && !pe_re_session_canister_ready(s) && !pe_re_scd_running(&s->run, 10))
        pe_re_scd_exec(s->logic, &s->run, PE_RE_TASKS, 10);
}

int pe_re_session_canister_win(PeReSession *s, unsigned slot, uint8_t killed_flag) {
    if (slot != 0 || !pe_re_session_canister_ready(s)) return -1;
    if (pe_re_session_beaten(s, (uint8_t)slot, killed_flag)) return -1;
    /* EM23.C signals the canister grab and explosion separately. Room 4-0A
     * uses those signals to update the scene and the exit. */
    pe_re_flag_set(&s->progress.flags, 5, 0x1d, 1);
    pe_re_flag_set(&s->progress.flags, 5, 0x1e, 1);
    return 0;
}

/* The room scripts release these bosses from their parked state before the
 * battle starts. See the enemy stand-ins in pe_re_scd.c. */
int pe_re_session_ready_boss(const PeReSession *s) {
    if (!s->game || strcmp(s->game->game, "re2")) return -1;
    const int alligator = s->stage == 3 && s->room == 0x0a;
    const int sewer = s->stage == 4 && s->room == 9;
    const int lift = s->stage == 5 && s->room == 0x17;
    const int tyrant = s->stage == 6 && s->room == 2;
    const int train = s->stage == 6 && s->room == 4;
    if (!alligator && !sewer && !lift && !tyrant && !train) return -1;
    const int16_t *work = s->run.works[2];
    if (!work[0] || s->run.em_dead[0]) return -1;
    const unsigned kind = (unsigned)work[PE_RE_MEMBER_ID];
    const unsigned command = (uint16_t)work[PE_RE_MEMBER_SCE_FREE0];
    /* 4-0A calls proc 11 from proc 2 to set the entrance command. Let proc 2
     * finish releasing the player before the host takes over for the battle. */
    if (alligator && kind == 0x23 && command && !pe_re_scd_running(&s->run, 2)) return 0;
    if (sewer && kind == 0x31 && (command & 4)) return 0;
    if (sewer && kind == 0x33 && command == 3 && (work[PE_RE_MEMBER_SCE_FREE1] & 1)) return 0;
    if (lift && kind == 0x34 && s->run.em_ready[0]) return 0;
    /* EM2B.C: the platform entrance's last command hands the Tyrant over to combat. */
    if (tyrant && kind == 0x2b && command == 3 && !pe_re_scd_running(&s->run, 7)) return 0;
    if (train && kind == 0x36 && (command & 1) && !(command & 2) &&
        !(work[PE_RE_MEMBER_TYPE] & 0x8000)) return 0;
    return -1;
}

static uint32_t room_move_word(const uint8_t *p) {
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int room_move(const PeReSession *s, unsigned bit, char *asset, size_t size) {
    uint32_t bytes, at, length;
    if (!s->game || strcmp(s->game->game, "re2") || !s->logic || bit >= 32) return -1;
    const uint8_t *rdt = pe_imp_logic_section(s->logic, PE_IMP_LOGIC_RDT0, NULL, &bytes);
    if (!rdt || pe_imp_rdt_section(rdt, bytes, 22, &at, &length) || length < 8) return -1;
    const uint8_t *rbj = rdt + at;
    const uint32_t table = room_move_word(rbj), count = room_move_word(rbj + 4);
    if (table > length || count > 64 || count > (length - table) / 8) return -1;
    /* Later records supersede earlier ones for the same actor. */
    for (uint32_t i = count; i > 0; i--) {
        const uint32_t record = room_move_word(rbj + table + 8 * (i - 1));
        if (record <= length - 4 && (room_move_word(rbj + record) & (1u << bit))) {
            const int written = snprintf(asset, size, "re2:roomanim/%x%02x-%u-p%u",
                                         (unsigned)s->stage + 1, (unsigned)s->room, (unsigned)i - 1,
                                         (unsigned)s->player);
            return written >= 0 && (size_t)written < size ? 0 : -1;
        }
    }
    return -1;
}

int pe_re_session_enemy_room_move(const PeReSession *s, unsigned slot, char *asset, size_t size) {
    /* Rbj_set addresses the actor table: player, partner, then enemy slot 0. */
    return slot < 16 ? room_move(s, slot + 2, asset, size) : -1;
}

int pe_re_session_player_room_move(const PeReSession *s, char *asset, size_t size) {
    return room_move(s, 0, asset, size);
}

int pe_re_session_defeated(PeReSession *s, uint8_t killed_flag) {
    if (!s->game || killed_flag == 0xFF) return -1;
    pe_re_flag_set(&s->progress.flags, enemy_group(s), killed_flag, 1);
    return 0;
}

int pe_re_session_beaten(PeReSession *s, uint8_t slot, uint8_t killed_flag) {
    if (!s->game || slot >= 16) return -1;
    if (killed_flag != 0xFF) pe_re_flag_set(&s->progress.flags, enemy_group(s), killed_flag, 1);
    if (!s->run.em_dead[slot]) s->run.em_dead[slot] = 1;
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
    free(s->core_effect_bank);
    free(s->core_effect_palettes);
    memset(s, 0, sizeof *s);
}
