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
#include "pe_re_session.h"
#include "pe_re_zone.h"
#include "pe_src_re2.h"

static int s_fail;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); s_fail = 1; } } while (0)

static const char *s_store = "pe-re-route-test/store";

#define ROOMS 2048                      /* stage << 8 | room */
#define DOORS 32
#define MOVES_MAX 3000
#define SETTLE 90                       /* frames of the room's scripts after coming in */

enum { DOOR_UNTRIED, DOOR_OPENS, DOOR_LOCKED, DOOR_NOT_SET };
typedef struct {
    int seen, parent, parent_door, order;
    int door_state[DOORS], door_tried_at[DOORS], door_to[DOORS], door_zone[DOORS];
    char door_said[DOORS][96];
    int searched_at;                    /* progress when its pickups and item spots were last done */
    int typewriter;
} Room;
static Room s_room[ROOMS];
static int s_progress = 1;              /* goes up with each item taken or used: locked doors are retried */
static int s_order;

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

static void settle(PeReSession *s, int frames) {
    for (int f = 0; f < frames; f++)
        if (pe_re_session_tick(s) == PE_RE_ZONE) handed(s);
}

/* The save data's own flags (scenario, common, room, enemy, item, map, door locks): a change there
 * is progress, and locked doors are tried again. */
static int flags_changed(const PeReSession *s) {
    static const int groups[] = {3, 4, 6, 7, 8, 9, 31};
    static uint8_t last[sizeof groups / sizeof *groups][32], room_word[4];
    int changed = memcmp(room_word, s->progress.flags.bits[5], 4) != 0;
    memcpy(room_word, s->progress.flags.bits[5], 4);
    for (size_t g = 0; g < sizeof groups / sizeof *groups; g++) {
        changed |= memcmp(last[g], s->progress.flags.bits[groups[g]], 32) != 0;
        memcpy(last[g], s->progress.flags.bits[groups[g]], 32);
    }
    return changed;
}

/* Examine a zone as a player does: standing 620 units out on one side, facing its middle (the
 * game's front check), else standing in it. What the step said. */
static PeReChange examine(PeReSession *s, const PeImpZone *zone, char *why, size_t n) {
    double cx, cz;
    zone_centre(zone, &cx, &cz);
    PeReChange c = PE_RE_STAY;
    for (int k = 0; k < 5; k++) {
        double x = cx, z = cz;
        int facing = 0;
        if (k < 4) {                                      /* stand back, facing it (the stage's yaw) */
            facing = k * 1024;
            pe_re_front(cx, cz, facing + 2048, &x, &z);
        }
        s->facing = facing;
        s->said[0] = 0;
        pe_re_session_tick(s);
        c = pe_re_session_step(s, s_store, x, z, 1, why, n);
        if (c == PE_RE_ZONE) handed(s);
        if (c == PE_RE_ROOM || c == PE_RE_ZONE || (c == PE_RE_SAID && s->said[0])) return c;
    }
    return c;
}

static const char *room_name(int key, char *out, size_t n);
static const char *room_name(int key, char *out, size_t n) {
    snprintf(out, n, "%x-%02X", (unsigned)(key >> 8) + 1, (unsigned)(key & 0xFF));
    return out;
}

/* In the room: everything in reach picked up, and held items used where the room takes them. */
static void search(PeReSession *s, FILE *log) {
    Room *r = &s_room[key_of(s)];
    char why[200], name[48], here[8];
    if (r->searched_at == s_progress) return;
    r->searched_at = s_progress;
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
            if (examine(s, &pk.zone, why, sizeof why) == PE_RE_SAID && !strncmp(s->said, "Picked up", 9)) {
                fprintf(log, "  %s: %s (item 0x%02X x%u, flag %u)\n", here, s->said, (unsigned)pk.item,
                        (unsigned)pk.count, (unsigned)pk.flag);
                took = 1, s_progress++;
            } else if (!strncmp(s->said, "No room", 7) && s->progress.held) {
                pe_re_box_put(&s->progress, 0);               /* a full pocket: into the box (counted) */
                fprintf(log, "  %s: pocket full, one item to the box\n", here);
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
        if (!pe_re_has(&s->progress, item)) continue;
        double cx, cz, x, z;
        zone_centre(&t.zone, &cx, &cz);
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
            fprintf(log, "  %s: used the %s\n", here, name);
            s_progress++;
        }
    }
    /* everything a player can examine: events, messages, computers */
    for (uint32_t i = 0; i < n; i++) {
        PeImpTrigger t;
        if (pe_imp_logic_trigger(s->logic, i, &t) || !pe_re_scd_live(&s->run, t.at) || !(t.zone.sat & PE_RE_SAT_MANUAL))
            continue;
        const int kind = pe_re_zone_kind(&s->run, t.zone.id, t.zone.sce, t.data);
        if (kind != PE_RE_ZONE_EVENT && kind != PE_RE_ZONE_MESSAGE && kind != PE_RE_ZONE_WINDOWS) continue;
        const int events = s->events;
        examine(s, &t.zone, why, sizeof why);
        settle(s, 300);
        if (s->events != events) fprintf(log, "  %s: examined zone %u, its event played\n", here, (unsigned)t.zone.id);
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
    if (r->door_state[i] != DOOR_OPENS) r->door_to[i] = (int)((d.stage == PE_RE_STAGE_HERE ? s->stage : d.stage) << 8 | d.room);
    if (!pe_re_scd_live(&s->run, d.at) || pe_re_zone_kind(&s->run, d.zone.id, d.zone.sce, NULL) != d.zone.sce) {
        r->door_state[i] = DOOR_NOT_SET;                  /* the scripts haven't put it there (yet) */
        return 0;
    }
    const PeReChange c = examine(s, &d.zone, why, sizeof why);
    if (c != PE_RE_ROOM) {
        r->door_state[i] = DOOR_LOCKED;
        snprintf(r->door_said[i], sizeof r->door_said[i], "%s", s->said[0] ? s->said : why[0] ? why : "nothing happened");
        return 0;
    }
    r->door_state[i] = DOOR_OPENS;
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

/* The nearest room (through doors known to open) with a door worth trying; its first step. */
static int plan(int from, int *first_door, int *goal) {
    static int prev[ROOMS], prev_door[ROOMS], queue[ROOMS];
    static unsigned char seen[ROOMS];
    int head = 0, tail = 0;
    memset(seen, 0, sizeof seen);
    queue[tail++] = from, seen[from] = 1, prev[from] = -1;
    while (head < tail) {
        const int k = queue[head++];
        const Room *r = &s_room[k];
        for (int i = 0; i < DOORS; i++)
            if (r->door_state[i] == DOOR_UNTRIED ? r->searched_at > 0
                                                 : r->door_state[i] != DOOR_OPENS && r->door_tried_at[i] < s_progress) {
                *goal = k, *first_door = -1;
                if (k != from) {                              /* back to the first door out of `from` */
                    int at = k;
                    while (prev[at] != from) at = prev[at];
                    *first_door = prev_door[at];
                }
                return 1;
            }
        for (int i = 0; i < DOORS; i++)
            if (r->door_state[i] == DOOR_OPENS && !seen[r->door_to[i]] && tail < ROOMS) {
                const int to = r->door_to[i];
                seen[to] = 1, prev[to] = k, prev_door[to] = i, queue[tail++] = to;
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
    const unsigned scenario = pe_re_session_start(&probe, s_store, g, 0, why, sizeof why) != 0;   /* disc 2: Claire */
    pe_re_session_end(&probe);

    static PeReSession s;
    CHECK(!pe_re_session_start(&s, s_store, g, scenario, why, sizeof why));
    if (s_fail) return 1;
    FILE *log = stdout;
    s_log = log;
    printf("RE2 %s, from the street:\n", scenario ? "Claire" : "Leon");
    s_room[key_of(&s)].seen = 1, s_room[key_of(&s)].parent = -1, s_room[key_of(&s)].order = ++s_order;
    settle(&s, SETTLE);
    int moves = 0, first_typewriter = -1;
    while (moves < MOVES_MAX) {
        const int here = key_of(&s);
        if (flags_changed(&s)) s_progress++;
        search(&s, log);
        if (s_room[here].typewriter && first_typewriter < 0) first_typewriter = here;
        for (int i = door_count(&s); i < DOORS; i++) s_room[here].door_state[i] = DOOR_OPENS, s_room[here].door_to[i] = here;
        int door, goal;
        if (!plan(here, &door, &goal)) break;
        if (goal == here) {                                     /* try the next untried or retriable door here */
            int i = 0;
            while (i < DOORS && !(s_room[here].door_state[i] == DOOR_UNTRIED ||
                                  (s_room[here].door_state[i] != DOOR_OPENS && s_room[here].door_tried_at[i] < s_progress)))
                i++;
            if (i >= DOORS) break;
            moves++;
            if (take(&s, i, log)) continue;
        } else {
            moves++;
            if (!take(&s, door, log)) s_room[here].door_state[door] = DOOR_LOCKED;   /* it opened before: now it won't */
        }
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
    char pocket[400] = "";
    for (int i = 0; i < s.progress.held; i++) {
        char name[48];
        pe_re_item_name(s.items, s.progress.pocket[i].item, name, sizeof name);
        snprintf(pocket + strlen(pocket), sizeof pocket - strlen(pocket), "%s%s", i ? ", " : "", name);
    }
    printf("route: %d rooms reached in %d moves, %d doors still locked, %d not set up by their scripts; pocket: %s\n",
           rooms, moves, locked, not_set, pocket[0] ? pocket : "empty");
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
    CHECK(rooms >= 5 && first_typewriter >= 0);
    pe_re_session_end(&s);
    printf(s_fail ? "re route FAILED\n" : "re route passed\n");
    return s_fail;
}
