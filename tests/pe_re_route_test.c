/* A scripted run through RE2 with the real play session (mods/pe_re_play), without the game: from
 * the street, it goes where a player can go and does what a player does. It takes doors (locks
 * included: a key opens what it opens), picks up what lies in reach, uses a held item where the
 * room arms it, and lets the room's scripts run between moves. Rooms are left only through their
 * doors. It stops when nothing new opens up, and reports how far it got: rooms reached, items
 * found, doors still locked and why, the first typewriter and the doors that lead to it.
 *
 * The route (the doors to the first typewriter, by room and door index) is written to
 * PE_TEST_RE_ROUTE when set, for the in-game replay. Needs PE_TEST_RE2_DISC (disc 1: Leon,
 * disc 2: Claire); skipped without it. No disc data is kept beyond the test's own store. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pe_imp.h"
#include "pe_imp_internal.h"
#include "pe_imp_logic.h"
#include "pe_imp_room.h"
#include "pe_re_save.h"
#include "pe_re_session.h"
#include "pe_re_zone.h"
#include "pe_src_re2.h"

static int s_fail;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); s_fail = 1; } } while (0)

static const char *s_store = "pe-re-route-test/store";

#define ROOMS 2048                      /* stage << 8 | room */
#define DOORS 32
#define EVENTS 8                        /* zones that took her to another room, per room */
#define MOVES_MAX 3000
#define SETTLE 90                       /* frames of the room's scripts after coming in */

enum { DOOR_UNTRIED, DOOR_OPENS, DOOR_LOCKED, DOOR_NOT_SET };
typedef struct {
    int seen, parent, parent_door, order;
    int door_state[DOORS], door_tried_at[DOORS], door_to[DOORS], door_zone[DOORS], door_key[DOORS];
    char door_said[DOORS][160];
    int searched_at;                    /* progress when its pickups and item spots were last done */
    int typewriter;
    int nevent, event_zone[EVENTS], event_to[EVENTS];   /* examining zone event_zone took her to event_to */
    int checkpointed;
    int unfinished;                     /* a zone took her away before search was done here */
    unsigned char door_variable[DOORS]; /* took her somewhere else than it says (an elevator): tried again when stuck */
} Room;
static Room s_room[ROOMS];
static int s_progress = 1;              /* goes up with each item taken or used: locked doors are retried */
static int s_order;
static unsigned char s_needed[256];     /* items a door or an item spot asked for: never put away */
static int s_box_trips;
static int s_tried_use[256];            /* progress when a use left the item in the pocket */                 /* times it used the item box away from a box (a shortcut) */

/* Make room in a full pocket: into the box goes what nothing has asked for, stacks first. 0, or -1. */
static int make_room(PeReSession *s) {
    PeReProgress *p = &s->progress;
    int pick = -1;
    for (int i = 0; i < p->held; i++) {
        if (s_needed[p->pocket[i].item & 0xFF]) continue;
        if (pick < 0 || p->pocket[i].count > p->pocket[pick].count) pick = i;
    }
    if (pick < 0 && p->held) pick = 0;                   /* all asked for: the oldest (the box hands it back when asked) */
    if (pick < 0 || pe_re_box_put(p, pick)) return -1;
    s_box_trips++;
    return 0;
}

/* `item` back from the box into the pocket (room made first). 0, or -1 if it isn't there. */
static int from_box(PeReSession *s, uint16_t item) {
    PeReProgress *p = &s->progress;
    for (int i = 0; i < p->boxed; i++)
        if (p->box[i].item == item) {
            if (p->held >= PE_RE_POCKET_SLOTS && make_room(s)) return -1;
            if (pe_re_box_take(p, i)) return -1;
            s_box_trips++;
            return 0;
        }
    return -1;
}

static int key_of(const PeReSession *s) { return (int)(s->stage << 8 | (s->room & 0xFF)); }
static void zone_centre(const PeImpZone *z, double *x, double *z_) {
    *x = *z_ = 0;
    for (int c = 0; c < 4; c++) *x += z->x[c] / 4.0, *z_ += z->z[c] / 4.0;
}
static FILE *s_log;
static const char *room_name(int key, char *out, size_t n);

/* A zone handed to the mod: a computer is answered yes (a card used when it asks). */
static void handed(PeReSession *s) {
    char here[8];
    if (s->zone.sce != PE_RE_ZONE_WINDOWS) return;
    PeReComputer c = pe_re_session_computer(s, 0);
    if (c == PE_RE_COMPUTER_ASK) c = pe_re_session_computer(s, 1);
    fprintf(s_log, "  %s: computer: %s\n", room_name(key_of(s), here, sizeof here), s->said);
}

static int events_running(const PeReSession *s) {
    for (int t = 2; t < PE_RE_TASKS; t++) if (s->run.task[t].status) return 1;
    return 0;
}

/* `frames` of the room's scripts, then on while an event plays (a cutscene), up to 100 s */
static void settle(PeReSession *s, int frames) {
    char why[200];
    for (int f = 0; f < frames || (events_running(s) && f < 6000); f++) {
        const PeReChange c = pe_re_session_tick(s);
        if (c == PE_RE_ZONE) handed(s);
        else if (c == PE_RE_STAY && s->moved && !getenv("PE_TEST_RE_NOSTEP")) {           /* a scene walked her: the game's every-frame check */
            s->moved = 0;                                 /* where she stands (a door that needs no button) */
            if (pe_re_session_step(s, s_store, s->x, s->z, 0, why, sizeof why) == PE_RE_ZONE) handed(s);
        }
    }
}

/* The story's flags (scenario, common, items, door locks): one set for the first time is progress,
 * and locked doors are tried again (a flag a script flips every frame isn't). Room, enemy and map flags change on every visit, so they don't count. */
static int flags_changed(const PeReSession *s) {
    static const int groups[] = {3, 4, 8, 31};               /* scenario, common, items, door locks */
    static uint8_t last[sizeof groups / sizeof *groups][32];
    int changed = 0;
    for (size_t g = 0; g < sizeof groups / sizeof *groups; g++)    /* a bit set for the first time */
        for (int b = 0; b < 32; b++) {
            const uint8_t now = s->progress.flags.bits[groups[g]][b];
            changed |= (now & ~last[g][b]) != 0;
            last[g][b] |= now;
        }
    return changed;
}

/* Examine a zone as a player does: standing 620 units out on one side, facing its middle (the
 * game's front check), else standing in it. What the step said. */
static PeReChange examine(PeReSession *s, const PeImpZone *zone, char *why, size_t n) {
    double cx, cz;
    zone_centre(zone, &cx, &cz);
    if (!(zone->floor & 0x80)) s->floor = zone->floor;      /* on its floor (the stairs walked), as Go to that door does */
    PeReChange c = PE_RE_STAY;
    const int events = s->events;
    const int is_event = pe_re_zone_kind(&s->run, zone->id, zone->sce, NULL) == PE_RE_ZONE_EVENT;
    unsigned sat = zone->sat;                             /* its check bits as the scripts left them (aot_reset) */
    for (int i = s->run.nreset - 1; i >= 0; i--)
        if (s->run.reset[i].id == zone->id) { sat = s->run.reset[i].sat; break; }
    if (getenv("PE_TEST_RE_SAT")) printf("    examine zone %u: sat %02x as left %02x, kind %d\n", (unsigned)zone->id, (unsigned)zone->sat, sat,
                                         pe_re_zone_kind(&s->run, zone->id, zone->sce, NULL));
    for (int k = 0; k < 5; k++) {
        double x = cx, z = cz;
        int facing = 0;
        if (k < 4) {                                      /* stand back, facing it (the stage's yaw) */
            facing = k * 1024;
            pe_re_front(cx, cz, facing + 2048, &x, &z);
        }
        s->facing = facing;
        s->said[0] = 0;
        if (pe_re_session_tick(s) == PE_RE_ROOM) return PE_RE_ROOM;   /* a script took her through a door */
        /* a player zone with no button: walking into it; else a press (it may be another record of this id that's live) */
        c = pe_re_session_step(s, s_store, x, z, !((sat & PE_RE_SAT_PLAYER) && !(sat & PE_RE_SAT_MANUAL)), why, n);
        if (c == PE_RE_ZONE) handed(s);
        if (c == PE_RE_ROOM || c == PE_RE_ZONE || (c == PE_RE_SAID && s->said[0])) return c;
        if (is_event && s->events != events) return c;  /* its event started: once, as one press (a switch toggles) */
    }
    return c;
}

static const char *room_name(int key, char *out, size_t n);
static const char *room_name(int key, char *out, size_t n) {
    snprintf(out, n, "%x-%02X", (unsigned)(key >> 8) + 1, (unsigned)(key & 0xFF));
    return out;
}

/* In the room: everything in reach picked up, and held items used where the room takes them. */
/* Examining zone `zone` in room `from` took her to `to`: remember it (the planner can use it) */
static void note_event(int from, int zone, int to, FILE *log) {
    Room *r = &s_room[from];
    char a[8], b[8];
    int k = 0;
    while (k < r->nevent && r->event_zone[k] != zone) k++;
    if (k == r->nevent && r->nevent < EVENTS) r->nevent++;
    if (k < EVENTS) r->event_zone[k] = zone, r->event_to[k] = to;
    if (!s_room[to].seen) {
        s_room[to].seen = 1, s_room[to].parent = from, s_room[to].parent_door = -1, s_room[to].order = ++s_order;
        fprintf(log, "%s -> %s (zone %d)\n", room_name(from, a, sizeof a), room_name(to, b, sizeof b), zone);
    }
}

/* Examine zone `zone` of the room she's in again (a way found by search): 1 when she's elsewhere after */
static int take_event(PeReSession *s, int zone) {
    const int from = key_of(s);
    uint32_t n = 0;
    char why[200];
    pe_imp_logic_section(s->logic, PE_IMP_LOGIC_TRIG, &n, NULL);
    for (uint32_t i = 0; i < n; i++) {
        PeImpTrigger t;
        if (pe_imp_logic_trigger(s->logic, i, &t) || (int)t.zone.id != zone || !pe_re_scd_live(&s->run, t.at)) continue;
        examine(s, &t.zone, why, sizeof why);
        settle(s, 300);
        if (key_of(s) != from) { settle(s, SETTLE); return 1; }
    }
    return 0;
}

/* A zone of room `r` already known to take her elsewhere: the planner's, not search's */
static int known_way(const Room *r, unsigned zone) {
    for (int k = 0; k < r->nevent; k++) if (r->event_zone[k] == (int)zone) return 1;
    return 0;
}

static void search(PeReSession *s, FILE *log) {
    Room *r = &s_room[key_of(s)];
    char why[200], name[48], here[8];
    if (r->searched_at == s_progress) return;
    r->searched_at = s_progress;
    r->unfinished = 0;
    room_name(key_of(s), here, sizeof here);
    for (int pass = 0, took = 1; took && pass < 8; pass++) {
        uint32_t n = 0;
        took = 0;
        pe_imp_logic_section(s->logic, PE_IMP_LOGIC_PICK, &n, NULL);
        for (uint32_t i = 0; i < n; i++) {
            PeImpPickup pk;
            if (pe_imp_logic_pickup(s->logic, i, &pk) || !pe_re_scd_live(&s->run, pk.at) ||
                pe_re_pickup_gone(s->logic, &s->run, &s->progress, &pk))
                continue;
            {   /* a placeholder (no area): its script hands it over (aot_on), nobody stands there */
                int minx = pk.zone.x[0], maxx = minx, minz = pk.zone.z[0], maxz = minz;
                for (int c = 1; c < 4; c++) {
                    if (pk.zone.x[c] < minx) minx = pk.zone.x[c];
                    if (pk.zone.x[c] > maxx) maxx = pk.zone.x[c];
                    if (pk.zone.z[c] < minz) minz = pk.zone.z[c];
                    if (pk.zone.z[c] > maxz) maxz = pk.zone.z[c];
                }
                if (maxx - minx <= 1 || maxz - minz <= 1) continue;
            }
            if (examine(s, &pk.zone, why, sizeof why) == PE_RE_SAID && !strncmp(s->said, "Picked up", 9)) {
                fprintf(log, "  %s: %s (item 0x%02X x%u, flag %u)\n", here, s->said, (unsigned)pk.item,
                        (unsigned)pk.count, (unsigned)pk.flag);
                took = 1, s_progress++;
            } else if (!strncmp(s->said, "No room", 7) && s->progress.held && !make_room(s)) {
                fprintf(log, "  %s: pocket full: something nothing needs went to the box\n", here);
                took = 1;
            }
        }
    }
    /* item spots: a walk-up flag zone arms an item (Use_flg); held, it's used */
    uint32_t n = 0;
    pe_imp_logic_section(s->logic, PE_IMP_LOGIC_TRIG, &n, NULL);
    for (uint32_t i = 0; i < n; i++) {
        PeImpTrigger t;
        if (pe_imp_logic_trigger(s->logic, i, &t) || !pe_re_scd_live(&s->run, t.at)) continue;
        const int kind = pe_re_zone_kind(&s->run, t.zone.id, t.zone.sce, t.data);
        if (kind == PE_RE_ZONE_SAVE) r->typewriter = 1;
        if (kind != PE_RE_ZONE_FLAG || (t.data[0] | t.data[1] << 8) != PE_RE_FG_USE || !t.data[4]) continue;
        const uint16_t item = (uint16_t)(t.data[2] | t.data[3] << 8);
        s_needed[item & 0xFF] = 1;
        if (!pe_re_has(&s->progress, item) && from_box(s, item)) continue;
        if (s_tried_use[item & 0xFF] == s_progress) continue;
        double cx, cz, x, z;
        zone_centre(&t.zone, &cx, &cz);
        if (!(t.zone.floor & 0x80)) s->floor = t.zone.floor;  /* on its floor */
        for (int k = 0; k < 4; k++) {
            s->facing = k * 1024;
            pe_re_front(cx, cz, s->facing + 2048, &x, &z);
            pe_re_session_tick(s);
            pe_re_session_step(s, s_store, x, z, 0, why, sizeof why);
            if (pe_re_flag(&s->progress.flags, PE_RE_FG_USE, item)) break;
        }
        pe_re_item_name(s->items, item, name, sizeof name);
        if (pe_re_session_use(s, item) == PE_RE_STAY) {
            settle(s, 300);
            fprintf(log, "  %s: used the %s%s\n", here, name, pe_re_has(&s->progress, item) ? " (still held)" : "");
            if (getenv("PE_TEST_RE_VERBOSE"))
                for (int k = 0; k < PE_RE_TASKS; k++)
                    if (s->run.task[k].status) printf("    task %d: proc %d at %u\n", k, s->run.task[k].proc, (unsigned)s->run.task[k].pc);
            if (pe_re_has(&s->progress, item)) s_tried_use[item & 0xFF] = s_progress;   /* not again until something changes */
            else s_progress++;
        }
    }
    if (s->progress.held >= PE_RE_POCKET_SLOTS) make_room(s);   /* a scene may hand her something */
    /* walk-on events: step into the middle of each (the game checks underfoot every frame) */
    for (uint32_t i = 0; i < n; i++) {
        PeImpTrigger t;
        if (pe_imp_logic_trigger(s->logic, i, &t) || !pe_re_scd_live(&s->run, t.at) || (t.zone.sat & PE_RE_SAT_MANUAL) ||
            !(t.zone.sat & PE_RE_SAT_UNDER))
            continue;
        if (pe_re_zone_kind(&s->run, t.zone.id, t.zone.sce, t.data) != PE_RE_ZONE_EVENT || known_way(r, t.zone.id)) continue;
        const int events = s->events, from = key_of(s);
        double cx, cz;
        zone_centre(&t.zone, &cx, &cz);
        if (!(t.zone.floor & 0x80)) s->floor = t.zone.floor;
        pe_re_session_tick(s);
        const PeReChange c = pe_re_session_step(s, s_store, cx, cz, 0, why, sizeof why);
        settle(s, 300);
        if (s->events != events) fprintf(log, "  %s: walked onto zone %u, its event played\n", here, (unsigned)t.zone.id);
        else if (getenv("PE_TEST_RE_VERBOSE"))
            printf("    walked onto %s zone %u at %.0f %.0f (floor %d): step %d, %s\n", here, (unsigned)t.zone.id, cx, cz, s->floor,
                   (int)c, s->said[0] ? s->said : why);
        if (key_of(s) != from) { note_event(from, (int)t.zone.id, key_of(s), log); r->searched_at = 0, r->unfinished = 1; return; }
    }
    /* a zone only an object sets off (a statue pushed onto it): run its event as the push would. A test-bot
     * step, like winning fights: pushing objects isn't built (in game either). Once per zone per room. */
    for (uint32_t i = 0; i < n; i++) {
        PeImpTrigger t;
        static uint32_t pushed[256];
        static int npushed;
        if (pe_imp_logic_trigger(s->logic, i, &t) || !pe_re_scd_live(&s->run, t.at) || (t.zone.sat & PE_RE_SAT_PLAYER) ||
            !(t.zone.sat & 0x04) || pe_re_zone_kind(&s->run, t.zone.id, t.zone.sce, t.data) != PE_RE_ZONE_EVENT)
            continue;
        const uint32_t key = (uint32_t)key_of(s) << 8 | t.zone.id;
        int k = 0;
        while (k < npushed && pushed[k] != key) k++;
        if (k < npushed || npushed >= 256) continue;
        pushed[npushed++] = key;
        for (int w = 18; w < 50; w++) s->run.works[w][PE_RE_MEMBER_AT_SCE_NO] = (int16_t)t.zone.id;   /* "it's on the zone" */
        pe_re_scd_exec(s->logic, &s->run, (unsigned)t.data[0] | (unsigned)t.data[1] << 8, t.data[3]);
        pe_re_session_tick(s);
        for (int w = 18; w < 50; w++) s->run.works[w][PE_RE_MEMBER_AT_SCE_NO] = 255;    /* cleared, as every frame */
        settle(s, 300);
        fprintf(log, "  %s: pushed something onto zone %u (its event played)\n", here, (unsigned)t.zone.id);
        s_progress++;
    }
    /* everything a player can examine: events, messages, computers */
    for (uint32_t i = 0; i < n; i++) {
        PeImpTrigger t;
        if (pe_imp_logic_trigger(s->logic, i, &t) || !pe_re_scd_live(&s->run, t.at) || !(t.zone.sat & PE_RE_SAT_MANUAL))
            continue;
        const int kind = pe_re_zone_kind(&s->run, t.zone.id, t.zone.sce, t.data);
        if ((kind != PE_RE_ZONE_EVENT && kind != PE_RE_ZONE_MESSAGE && kind != PE_RE_ZONE_WINDOWS) || known_way(r, t.zone.id))
            continue;
        const int events = s->events, from = key_of(s);
        examine(s, &t.zone, why, sizeof why);
        settle(s, 300);
        if (s->events != events) fprintf(log, "  %s: examined zone %u, its event played\n", here, (unsigned)t.zone.id);
        if (key_of(s) != from) {                          /* it took her somewhere: a way there, like a door */
            note_event(from, (int)t.zone.id, key_of(s), log);
            r->searched_at = 0, r->unfinished = 1;        /* the rest next time she's here */
            return;                                       /* this room's zones aren't the ones loaded now */
        }
    }
}

/* Through door `i` of the room she's in: 1 when she came out in another room. */
static int take(PeReSession *s, int i, FILE *log) {
    const int from = key_of(s);
    Room *r = &s_room[from];
    PeImpDoor d;
    char why[200] = "", a[8], b[8];
    if (pe_imp_logic_door(s->logic, (uint32_t)i, &d)) return 0;
    r->door_tried_at[i] = s_progress;
    r->door_zone[i] = d.zone.id;
    r->door_key[i] = d.lock_type && d.lock_type < 254 ? d.lock_type : 0;
    if (r->door_state[i] != DOOR_OPENS) r->door_to[i] = (int)((d.stage == PE_RE_STAGE_HERE ? s->stage : d.stage) << 8 | d.room);
    const int kind = pe_re_zone_kind(&s->run, d.zone.id, d.zone.sce, NULL);
    if (pe_re_scd_live(&s->run, d.at) && (kind == PE_RE_ZONE_EVENT || kind == PE_RE_ZONE_MESSAGE)) {
        examine(s, &d.zone, why, sizeof why);             /* a door its script made a scene (aot_reset) */
        settle(s, 300);
        if (key_of(s) != from) {                          /* a way on, like a door: take() finds it again */
            r->door_state[i] = DOOR_OPENS, r->door_to[i] = key_of(s);
            if (!s_room[key_of(s)].seen) {
                s_room[key_of(s)].seen = 1, s_room[key_of(s)].parent = from, s_room[key_of(s)].parent_door = i;
                s_room[key_of(s)].order = ++s_order;
                fprintf(log, "%s -> %s (door %d, a scene)\n", room_name(from, a, sizeof a), room_name(key_of(s), b, sizeof b), i);
            }
            settle(s, SETTLE);
            return 1;
        }
    }
    if (!pe_re_scd_live(&s->run, d.at) || kind != d.zone.sce) {
        r->door_state[i] = DOOR_NOT_SET;                  /* the scripts haven't put it there (yet) */
        return 0;
    }
    PeReChange c = examine(s, &d.zone, why, sizeof why);
    if (key_of(s) != from) c = PE_RE_ROOM;                /* she's somewhere else, whatever the step said */
    if (c != PE_RE_ROOM && d.lock_type && d.lock_type < 254) {     /* a key: remember it, fetch it from the box */
        s_needed[d.lock_type] = 1;
        if (!pe_re_has(&s->progress, d.lock_type) && !from_box(s, d.lock_type)) c = examine(s, &d.zone, why, sizeof why);
        if (key_of(s) != from) c = PE_RE_ROOM;
    }
    if (c != PE_RE_ROOM) settle(s, 30);                   /* a scene the door started may take her out */
    if (key_of(s) != from) c = PE_RE_ROOM;
    if (getenv("PE_TEST_RE_VERBOSE"))
        printf("    try %s door %d (progress %d): %s\n", room_name(from, a, sizeof a), i, s_progress,
               c == PE_RE_ROOM ? "through" : s->said);
    if (c != PE_RE_ROOM) {
        r->door_state[i] = DOOR_LOCKED;
        snprintf(r->door_said[i], sizeof r->door_said[i], "%s (lock %u/%u, zone sat %02x floor %u, she's on %d)",
                 s->said[0] ? s->said : why[0] ? why : "nothing happened", (unsigned)d.lock_id, (unsigned)d.lock_type,
                 (unsigned)d.zone.sat, (unsigned)d.zone.floor, s->floor);
        return 0;
    }
    r->door_state[i] = DOOR_OPENS;
    if (key_of(s) != (int)((d.stage == PE_RE_STAGE_HERE ? (unsigned)(from >> 8) : d.stage) << 8 | d.room)) r->door_variable[i] = 1;
    r->door_to[i] = key_of(s);
    Room *to = &s_room[key_of(s)];
    if (!to->seen) {
        to->seen = 1, to->parent = from, to->parent_door = i, to->order = ++s_order;
        fprintf(log, "%s -> %s (door %d)%s%s\n", room_name(from, a, sizeof a), room_name(key_of(s), b, sizeof b), i,
                s->said[0] ? ": " : "", s->said);
    }
    settle(s, SETTLE);
    return 1;
}

/* A locked door whose key she now has (pocket or box): go back to it before anything new, since the
 * way on may be one way. */
static int key_door(const PeReSession *s, const Room *r, int i) {
    const int key = r->door_key[i];
    if (!key || r->door_state[i] != DOOR_LOCKED || r->door_tried_at[i] >= s_progress) return 0;
    if (pe_re_has(&s->progress, (uint16_t)key)) return 1;
    for (int k = 0; k < s->progress.boxed; k++) if (s->progress.box[k].item == key) return 1;
    return 0;
}

/* The nearest room (through doors known to open) with a door worth trying (one a held key opens
 * first); its first step. */
static int plan(const PeReSession *s, int from, int *first_door, int *goal) {
    static int prev[ROOMS], prev_door[ROOMS], queue[ROOMS];
    static unsigned char seen[ROOMS];
    for (int keys = 1; keys >= 0; keys--) {
    int head = 0, tail = 0;
    memset(seen, 0, sizeof seen);
    queue[tail++] = from, seen[from] = 1, prev[from] = -1;
    while (head < tail) {
        const int k = queue[head++];
        const Room *r = &s_room[k];
        for (int i = 0; i < DOORS; i++)
            if (keys ? key_door(s, r, i)
                     : (r->unfinished && k != from) ? 1
                     : r->door_state[i] == DOOR_UNTRIED ? r->searched_at > 0
                                                       : r->door_state[i] != DOOR_OPENS && r->door_tried_at[i] < s_progress) {
                *goal = k, *first_door = -1;
                if (k != from) {                              /* back to the first door out of `from` */
                    int at = k;
                    while (prev[at] != from) at = prev[at];
                    *first_door = prev_door[at];
                }
                return 1;
            }
        for (int i = 0; i < DOORS + r->nevent; i++) {         /* doors that open, then zones that took her */
            const int to = i < DOORS ? (r->door_state[i] == DOOR_OPENS ? r->door_to[i] : -1) : r->event_to[i - DOORS];
            if (to >= 0 && !seen[to] && tail < ROOMS) seen[to] = 1, prev[to] = k, prev_door[to] = i, queue[tail++] = to;
        }
    }
    }
    return 0;
}

/* A scene waiting for her to walk somewhere: a running task on the player (work_set 1) near a
 * member_cmp of her x (member 11) or z (13). Put her where the comparison holds, as a player walking
 * there would, and let the scene go on. 1 if it moved her. (A test-bot step: it reads the bytes
 * around the task, so it may misread; it only runs when the route is stuck.) */
static int walk_where_waited(PeReSession *s, FILE *log) {
    uint32_t size = 0;
    const uint8_t *rdt = pe_imp_logic_section(s->logic, PE_IMP_LOGIC_RDT0, NULL, &size);
    char a[8];
    if (!rdt) return 0;
    for (int k = 2; k < PE_RE_TASKS; k++) {
        const PeReScdTask *t = &s->run.task[k];
        if (!t->status || t->work != 0) continue;
        for (uint32_t at = t->pc > 96 ? t->pc - 96 : 0; at + 6 <= size && at < t->pc + 32; at++) {
            const uint8_t *m = rdt + at;
            if (m[0] != 0x3E || m[1] != 0 || (m[2] != 11 && m[2] != 13) || m[3] < 1 || m[3] > 4) continue;
            const int v = (int16_t)(m[4] | m[5] << 8);
            const int want = m[3] == 1 ? v + 50 : m[3] == 2 ? v + 1 : m[3] == 3 ? v - 50 : v - 1;
            const double before = m[2] == 11 ? s->x : s->z;
            if ((m[3] == 1 && before > v) || (m[3] == 2 && before >= v) || (m[3] == 3 && before < v) || (m[3] == 4 && before <= v))
                continue;                                  /* already holds */
            if (m[2] == 11) s->x = want; else s->z = want;
            fprintf(log, "  %s: walked to %s %d, where a scene waits for her\n", room_name(key_of(s), a, sizeof a),
                    m[2] == 11 ? "x" : "z", want);
            char why[200];
            pe_re_session_step(s, s_store, s->x, s->z, 0, why, sizeof why);
            settle(s, 300);
            return 1;
        }
    }
    return 0;
}

static int door_count(const PeReSession *s) {
    uint32_t n = 0;
    pe_imp_logic_section(s->logic, PE_IMP_LOGIC_DOOR, &n, NULL);
    return n > DOORS ? DOORS : (int)n;
}

int main(void) {
    char why[200] = "", a[8], b[8];
    const char *re2 = getenv("PE_TEST_RE2_DISC");
    if (!re2 || !*re2) { printf("PE_TEST_RE2_DISC not set: the route skipped\n"); return 77; }
    PeImpScan sc;
    pe_imp_registry_reset();
    CHECK(!pe_imp_add_reader(&g_re2_logic_ps1) && !pe_imp_add_reader(&g_re2_rooms_ps1) && !pe_imp_add_reader(&g_re2_items_ps1) &&
          !pe_imp_add_reader(&g_re2_door_ps1));
    CHECK(!pe_imp_set_naming("re2", g_re2_naming));
    CHECK(!pe_imp_scan(s_store, re2, NULL, &sc));
    const PeReGame *g = pe_re_game_named("re2");
    PeReSession probe;
    unsigned scenario = pe_re_session_start(&probe, s_store, g, 0, why, sizeof why) != 0;   /* disc 2: Claire */
    if (getenv("PE_TEST_RE_PLAYER")) scenario = (unsigned)atoi(getenv("PE_TEST_RE_PLAYER"));   /* 0 Leon, 1 Claire */
    pe_re_session_end(&probe);

    static PeReSession s;
    const char *ckpt = getenv("PE_TEST_RE_CHECKPOINTS"), *start = getenv("PE_TEST_RE_START");
    if (start && *start) {                                       /* from a checkpoint an earlier run saved */
        static uint8_t bytes[PE_RE_SAVE_SIZE];
        PeReSaved saved;
        FILE *f = fopen(start, "rb");
        CHECK(f && fread(bytes, 1, sizeof bytes, f) == sizeof bytes);
        if (f) fclose(f);
        CHECK(!pe_re_save_decode(bytes, &saved) && !pe_re_session_resume(&s, s_store, &saved, why, sizeof why));
        if (!s_fail) printf("started from %s\n", start);
    } else {
        CHECK(!pe_re_session_start(&s, s_store, g, scenario, why, sizeof why));
    }
    if (s_fail) { printf("start: %s\n", why); return 1; }
    FILE *log = stdout;
    s_log = log;
    printf("RE2 %s, from the street:\n", scenario ? "Claire" : "Leon");
    s_room[key_of(&s)].seen = 1, s_room[key_of(&s)].parent = -1, s_room[key_of(&s)].order = ++s_order;
    settle(&s, SETTLE);
    int moves = 0, first_typewriter = -1, ended = -1;
    static unsigned char researched[256];                   /* searches of a room in a row (bounded) */
    while (moves < MOVES_MAX) {
        const int here = key_of(&s);
        if (flags_changed(&s)) s_progress++;
        if (pe_re_flag(&s.progress.flags, 0, 0x0A)) {           /* SYS_ENDING (script bit 0x0A of System_flg): the end */
            ended = here;
            fprintf(log, "  %s: the scenario's ending starts here\n", room_name(here, a, sizeof a));
            break;
        }
        if (ckpt && *ckpt && !s_room[here].checkpointed) {       /* PE_TEST_RE_CHECKPOINTS: this room's first visit */
            static uint8_t bytes[PE_RE_SAVE_SIZE];
            char path[600];
            s_room[here].checkpointed = 1;
            snprintf(path, sizeof path, "%s/%s.resave", ckpt, room_name(here, a, sizeof a));
            pe_re_save_encode(&s, 1, bytes);
            FILE *f = fopen(path, "wb");
            if (f) fwrite(bytes, 1, sizeof bytes, f), fclose(f);
        }
        const int before = s_progress;
        search(&s, log);
        if (key_of(&s) != here) continue;                       /* a zone took her elsewhere: start over there */
        if (s_progress != before) {                             /* something changed here: an item may have appeared */
            s_room[here].searched_at = 0;
            if (++researched[here & 0xFF] < 8) continue;
        }
        if (events_running(&s) && walk_where_waited(&s, log)) {  /* a scene here waits for her to walk somewhere */
            s_progress++;
            continue;
        }
        if (s_room[here].typewriter && first_typewriter < 0) first_typewriter = here;
        for (int i = door_count(&s); i < DOORS; i++) s_room[here].door_state[i] = DOOR_OPENS, s_room[here].door_to[i] = here;
        int door, goal;
        if (!plan(&s, here, &door, &goal)) {
            /* Stuck: every way out of here looked shut. Try this room's doors again, once per bit of progress. */
            static int stuck_at = -1;
            if (stuck_at == s_progress) {                        /* nothing new since the last time: done */
                fprintf(log, "  stuck in %s again: the run ends here\n", room_name(here, a, sizeof a));
                for (int k = 0; k < PE_RE_TASKS; k++)
                    if (s.run.task[k].status) fprintf(log, "    task %d: proc %d at %u (work %d)\n", k, s.run.task[k].proc,
                                                      (unsigned)s.run.task[k].pc, s.run.task[k].work);
                break;
            }
            stuck_at = s_progress;
            {   /* a fight the way on waits for (a boss): she wins it, as the game's battle would */
                PeReEnemyPlacement foes[32];
                const int nfoes = pe_re_session_enemies(&s, foes, 32);
                int beaten = 0;
                for (int k = 0; k < nfoes; k++)                   /* each once (a boss has no killed flag) */
                    if (foes[k].slot < 16 && foes[k].kind < 0x40 && !s.run.em_dead[foes[k].slot])
                        beaten += !pe_re_session_beaten(&s, foes[k].slot, foes[k].killed_flag);
                if (beaten) {
                    fprintf(log, "  %s: fought and beat %d enemies\n", room_name(here, a, sizeof a), beaten);
                    settle(&s, 300);
                    s_progress++;
                }
            }
            if (walk_where_waited(&s, log)) { s_progress++; continue; }
            for (int k = 0; k < ROOMS; k++)                       /* rooms seen before: search them again */
                if (s_room[k].seen && k != here) s_room[k].unfinished = 1, s_room[k].searched_at = 0;
            for (int i = 0; i < DOORS; i++)
                if (s_room[here].door_state[i] != DOOR_OPENS) s_room[here].door_tried_at[i] = 0;
            for (int k = 0; k < ROOMS; k++)                       /* elevators: where they go may have changed */
                for (int i = 0; i < DOORS; i++)
                    if (s_room[k].door_variable[i]) s_room[k].door_state[i] = DOOR_UNTRIED, s_room[k].door_variable[i] = 0;
            fprintf(log, "  stuck in %s: its doors again (came from %04x)\n", room_name(here, a, sizeof a),
                    (unsigned)(uint16_t)s.progress.flags.work[PE_RE_WORK_ROOM_OLD]);
            for (const char *f = getenv("PE_TEST_RE_FLAGS"); f && *f;) {   /* PE_TEST_RE_FLAGS="4:88 4:89": those now */
                unsigned g = 0, bit = 0;
                int used = 0;
                if (sscanf(f, "%u:%x%n", &g, &bit, &used) != 2) break;
                printf("    flag %u:%02x = %d\n", g, bit, pe_re_flag(&s.progress.flags, g, bit));
                f += used;
                while (*f == ' ') f++;
            }
            if (getenv("PE_TEST_RE_VERBOSE"))                    /* the map as the planner sees it */
                for (int k = 0; k < ROOMS; k++) {
                    if (!s_room[k].seen) continue;
                    printf("    map %s (searched %d%s):", room_name(k, a, sizeof a), s_room[k].searched_at,
                           s_room[k].unfinished ? ", unfinished" : "");
                    for (int i = 0; i < 8; i++)
                        printf(" %d:%c>%s", i, "uolN"[s_room[k].door_state[i]], room_name(s_room[k].door_to[i], b, sizeof b));
                    for (int e = 0; e < s_room[k].nevent; e++)
                        printf(" z%d>%s", s_room[k].event_zone[e], room_name(s_room[k].event_to[e], b, sizeof b));
                    printf("\n");
                }
            for (int k = 0; k < PE_RE_TASKS; k++)                 /* what its scripts are waiting on */
                if (s.run.task[k].status) fprintf(log, "    task %d: proc %d at %u\n", k, s.run.task[k].proc, (unsigned)s.run.task[k].pc);
            uint32_t nt = 0;
            pe_imp_logic_section(s.logic, PE_IMP_LOGIC_TRIG, &nt, NULL);
            for (uint32_t k = 0; k < nt; k++) {                   /* and its zones */
                PeImpTrigger t;
                if (pe_imp_logic_trigger(s.logic, k, &t)) continue;
                fprintf(log, "    zone %u: kind %d, sce %u, sat %02x, floor %u, live %d, at %u, data %02x %02x %02x %02x\n",
                        (unsigned)t.zone.id, pe_re_zone_kind(&s.run, t.zone.id, t.zone.sce, t.data), (unsigned)t.zone.sce,
                        (unsigned)t.zone.sat, (unsigned)t.zone.floor, pe_re_scd_live(&s.run, t.at), (unsigned)t.at,
                        t.data[0], t.data[1], t.data[2], t.data[3]);
            }
            continue;
        }
        if (goal == here) {                                     /* try the next untried or retriable door here */
            int i = 0;
            while (i < DOORS && !key_door(&s, &s_room[here], i)) i++;
            if (i >= DOORS) i = 0;
            else goto chosen;
            while (i < DOORS && !(s_room[here].door_state[i] == DOOR_UNTRIED ||
                                  (s_room[here].door_state[i] != DOOR_OPENS && s_room[here].door_tried_at[i] < s_progress)))
                i++;
            if (i >= DOORS) break;
        chosen:
            moves++;
            if (take(&s, i, log)) continue;
        } else {
            moves++;
            if (door >= DOORS) {                                  /* a zone that took her before */
                if (!take_event(&s, s_room[here].event_zone[door - DOORS])) s_room[here].nevent = door - DOORS;   /* not now */
            } else if (!take(&s, door, log)) {
                s_room[here].door_state[door] = DOOR_LOCKED;      /* it opened before: now it won't */
            }
        }
    }

    /* PE_TEST_RE_FIND_ITEM=5f: every room where that item can be picked up (where a missing key lies) */
    if (getenv("PE_TEST_RE_FIND_ITEM")) {
        const unsigned want = (unsigned)strtoul(getenv("PE_TEST_RE_FIND_ITEM"), NULL, 16);
        for (unsigned st = 0; st < 8; st++)
            for (unsigned rm = 0; rm < 0x40; rm++) {
                char id[PE_IMP_ID_MAX];
                PeImpRoomLogic *rl = NULL;
                uint32_t np = 0;
                pe_re_logic_id("re2", st, rm, scenario, id, sizeof id);
                if (pe_imp_catalog_roomlogic(s_store, id, &rl, NULL, 0)) continue;
                pe_imp_logic_section(rl, PE_IMP_LOGIC_PICK, &np, NULL);
                for (uint32_t k = 0; k < np; k++) {
                    PeImpPickup pk;
                    if (!pe_imp_logic_pickup(rl, k, &pk) && pk.item == want)
                        printf("  item %02x: room %u-%02X, pickup %u (flag %u, set by the script at %u)\n", want, st + 1, rm,
                               (unsigned)k, (unsigned)pk.flag, (unsigned)pk.at);
                }
                free(rl);
            }
    }
    /* PE_TEST_RE_DOORS="606": that room's doors as imported (id, target, the script offset that sets each) */
    for (const char *t = getenv("PE_TEST_RE_DOORS"); t && *t;) {
        unsigned code = 0;
        int used = 0;
        if (sscanf(t, "%x%n", &code, &used) != 1) break;
        t += used;
        while (*t == ' ') t++;
        char id[PE_IMP_ID_MAX];
        PeImpRoomLogic *rl = NULL;
        uint32_t nd = 0;
        pe_re_logic_id("re2", (code >> 8) - 1, code & 0xFF, scenario, id, sizeof id);
        if (pe_imp_catalog_roomlogic(s_store, id, &rl, NULL, 0)) continue;
        pe_imp_logic_section(rl, PE_IMP_LOGIC_DOOR, &nd, NULL);
        for (uint32_t k = 0; k < nd; k++) {
            PeImpDoor dd;
            if (!pe_imp_logic_door(rl, k, &dd))
                printf("  %x door %u: zone %u to stage %u room %02X, lock %u/%u, at %u\n", code, (unsigned)k, (unsigned)dd.zone.id,
                       (unsigned)dd.stage, (unsigned)dd.room, (unsigned)dd.lock_id, (unsigned)dd.lock_type, (unsigned)dd.at);
        }
        free(rl);
    }
    /* PE_TEST_RE_TEXT="200 10b": those rooms' own messages, to tell rooms apart while debugging a route */
    const char *text_rooms = getenv("PE_TEST_RE_TEXT");
    for (const char *t = text_rooms; t && *t;) {
        unsigned code = 0;
        int used = 0;
        if (sscanf(t, "%x%n", &code, &used) != 1) break;
        t += used;
        while (*t == ' ') t++;
        char id[PE_IMP_ID_MAX], line[200];
        PeImpRoomLogic *rl = NULL;
        pe_re_logic_id("re2", (code >> 8) - 1, code & 0xFF, scenario, id, sizeof id);
        if (pe_imp_catalog_roomlogic(s_store, id, &rl, NULL, 0)) continue;
        for (uint32_t m = 0; m < 40 && !pe_re_message(rl, m, line, sizeof line); m++) printf("  %x text %u: %s\n", code, m, line);
        free(rl);
    }

    /* what it found */
    int rooms = 0, locked = 0, not_set = 0;
    for (int k = 0; k < ROOMS; k++) {
        if (!s_room[k].seen) continue;
        rooms++;
        for (int i = 0; i < DOORS; i++) {
            if (s_room[k].door_state[i] == DOOR_LOCKED) {
                locked++;
                printf("  locked: %s door %d: %s\n", room_name(k, a, sizeof a), i, s_room[k].door_said[i]);
            }
            if (s_room[k].door_state[i] == DOOR_NOT_SET) {
                not_set++;
                printf("  not set up: %s door %d (to %s, zone %d)\n", room_name(k, a, sizeof a), i,
                       room_name(s_room[k].door_to[i], b, sizeof b), s_room[k].door_zone[i]);
            }
        }
    }
    char pocket[700] = "";
    for (int i = 0; i < s.progress.held; i++) {
        char name[48];
        pe_re_item_name(s.items, s.progress.pocket[i].item, name, sizeof name);
        snprintf(pocket + strlen(pocket), sizeof pocket - strlen(pocket), "%s%s (0x%02X)", i ? ", " : "", name,
                 (unsigned)s.progress.pocket[i].item);
    }
    if (ended >= 0) printf("ending: reached in %s\n", room_name(ended, a, sizeof a));
    else printf("ending: not reached\n");
    printf("route: %d rooms reached in %d moves, %d doors still locked, %d not set up by their scripts; pocket: %s; "
           "item box used away from a box %d times\n", rooms, moves, locked, not_set, pocket[0] ? pocket : "empty", s_box_trips);
    if (first_typewriter >= 0) {
        int path[64], n = 0;
        for (int k = first_typewriter; k >= 0 && n < 64; k = s_room[k].parent) path[n++] = k;
        printf("first typewriter: %s, %d doors from the street:", room_name(first_typewriter, a, sizeof a), n - 1);
        const char *out = getenv("PE_TEST_RE_ROUTE");
        FILE *f = out && *out ? fopen(out, "w") : NULL;
        for (int i = n - 1; i > 0; i--) {
            printf(" %s>%s", room_name(path[i], a, sizeof a), room_name(path[i - 1], b, sizeof b));
            if (f) fprintf(f, "%d %d %d\n", path[i] >> 8, path[i] & 0xFF, s_room[path[i - 1]].parent_door);
        }
        printf("\n");
        if (f) fclose(f);
    } else {
        printf("first typewriter: not reached\n");
    }
    CHECK(start && *start ? rooms >= 1 : rooms >= 5 && first_typewriter >= 0);   /* a checkpoint may be past the typewriters */
    {   /* what the room scripts still skip, most met first (what events the runtime doesn't play yet) */
        int order[256];
        for (int i = 0; i < 256; i++) order[i] = i;
        for (int i = 0; i < 256; i++)
            for (int j = i + 1; j < 256; j++)
                if (pe_re_scd_skips[order[j]] > pe_re_scd_skips[order[i]]) { const int t = order[i]; order[i] = order[j], order[j] = t; }
        printf("skipped instructions:");
        for (int i = 0; i < 24 && pe_re_scd_skips[order[i]]; i++) printf(" %02X:%u", order[i], (unsigned)pe_re_scd_skips[order[i]]);
        printf("\n");
    }
    pe_re_session_end(&s);
    printf(s_fail ? "re route FAILED\n" : "re route passed\n");
    return s_fail;
}
