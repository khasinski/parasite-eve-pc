/* Debug-control field commands. See docs/DEBUG_CONTROL.md. */
#include "pe_debug_internal.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "pe_game.h"
#include "pe1/asset_table.h"
#include "pe1/field_actor.h"
#include "pe1/game_state.h"
#include "pe1/player_entity.h"
#include "pe1/scene_dispatch.h"
#include "pe1/script_queue.h"
#include "pe1/reloc_block.h"
#include "pe1/field_collision.h"
#include "pe1/scene_transition.h"

#define PE_VAR_STORY 74
#define PE_VAR_COUNT 512
#define PE_ROOM_TEMPLATE 0xA8000048u
#define PE_SCRIPT_SCAN 0x8000
#define PE_ACTORS_MAX 64

static FieldActor *pe_ctl_actor(int index) {
    FieldActor *a = g_FieldActorListHead;
    int i;
    for (i = 0; a && i < index && i < PE_ACTORS_MAX; ++i) a = a->next;
    return index >= 0 && i == index ? a : NULL;
}

static unsigned pe_ctl_room_id(int map) {
    unsigned id = PE_ROOM_TEMPLATE;
    int i, k, digit[3];
    if (map < 0 || map > 999) return 0;
    digit[0] = map / 100; digit[1] = map / 10 % 10; digit[2] = map % 10;
    for (i = 0; i < 3; ++i) {
        for (k = 0; k < 32 && g_Base32CharTable[k] != '0' + digit[i]; ++k) {}
        if (k == 32) return 0;
        id |= (unsigned)k << ((3 - i) * 5 + 2);   /* chars 2, 3, 4: bits 17, 12, 7 */
    }
    return id;
}

static void pe_ctl_room(const char *args) {
    (void)args;
    char name[8] = { 0 }, detail[160];
    Text_EncodeBase32(name, (unsigned)g_SceneDispatchToken);
    name[6] = 0;
    snprintf(detail, sizeof detail, "map=%d name=%s token=%08X story=%X day=%d free=%d", pe_room_number(), name,
             (unsigned)g_SceneDispatchToken, (unsigned)g_ScriptVariableBank[PE_VAR_STORY],
             (int)g_GameState.current_story_day, pe_free_field());
    pe_debug_reply("ok %s %s", "room", detail);
}

void pe_debug_room_write(FILE *f) {
    char name[8] = { 0 };
    Text_EncodeBase32(name, (unsigned)g_SceneDispatchToken);
    name[6] = 0;
    fprintf(f, "{\"map\":%d,\"name\":", pe_room_number());
    pe_debug_json_string(f, name);
    fprintf(f, ",\"token\":\"%08X\",\"story\":%u,\"day\":%d,\"free\":%s,\"flags\":%u}",
            (unsigned)g_SceneDispatchToken, (unsigned)g_ScriptVariableBank[PE_VAR_STORY],
            (int)g_GameState.current_story_day, pe_free_field() ? "true" : "false", (unsigned)g_GameStateFlags);
}

void pe_debug_actors_write(FILE *f) {
    FieldActor *a;
    int i;
    fputc('[', f);
    for (a = g_FieldActorListHead, i = 0; a && i < PE_ACTORS_MAX; a = a->next, ++i)
        fprintf(f, "%s{\"index\":%d,\"type\":%u,\"sub\":%u,\"player\":%s,\"position\":[%d,%d,%d],"
                "\"yaw\":%d,\"flags\":%u,\"script\":%s}", i ? "," : "", i, a->type_id, a->sub_id,
                a == g_PlayerEntity ? "true" : "false", a->pos_x, a->pos_y, a->pos_z, a->rot_y,
                (unsigned)a->flags, a->script_base ? "true" : "false");
    fputc(']', f);
}

static void pe_ctl_warp(const char *args) {
    char target[32], detail[64];
    unsigned id, story = 0;
    int arg, n = sscanf(args, "%31s %x", target, &story);
    int *argv[1];
    if (n < 1) { pe_debug_reply("err %s %s", "warp", "warp MAP [STORY]"); return; }
    id = !strncmp(target, "0x", 2) || !strncmp(target, "0X", 2) ? (unsigned)strtoul(target, NULL, 16)
                                                                   : pe_ctl_room_id(atoi(target));
    if (!id || (id >> 24) != (PE_ROOM_TEMPLATE >> 24)) { pe_debug_reply("err %s %s", "warp", "no such room"); return; }
    if (!pe_free_field() || (unsigned)g_SceneDispatchCur != (unsigned)g_SceneDispatchToken) {
        pe_debug_reply("err %s %s", "warp", "not in free field");
        return;
    }
    if (id == (unsigned)g_SceneDispatchToken) {      /* the transfer never finishes into its own room */
        pe_debug_reply("err %s %s", "warp", "already in that room");
        return;
    }
    if (n >= 2) g_ScriptVariableBank[PE_VAR_STORY] = story;
    arg = (int)id;
    argv[0] = &arg;
    (void)Script_EncodeBarcode(argv);
    snprintf(detail, sizeof detail, "token=%08X", id);
    pe_debug_reply("ok %s %s", "warp", detail);
}

static void pe_ctl_actors(const char *args) {
    (void)args;
    static char list[PE_ACTORS_MAX * 96];
    FieldActor *a;
    int i, at = 0;
    list[0] = 0;
    for (a = g_FieldActorListHead, i = 0; a && i < PE_ACTORS_MAX; a = a->next, ++i) {
        int written = snprintf(list + at, sizeof list - at, "%s%d:%u.%u%s@%d,%d,f%X", i ? " " : "", i, a->type_id, a->sub_id,
                       a == g_PlayerEntity ? "*" : "", a->pos_x, a->pos_z, a->flags);
        if (written < 0 || (size_t)written >= sizeof list - at) break;
        at += written;
    }
    pe_debug_reply("ok %s %s", "actors", list);
}

static void pe_ctl_find_script(const char *args) {
    static char found[512];
    unsigned words[8];
    int n = 0, used = 0, at = 0, i;
    FieldActor *a;
    while (n < 8 && sscanf(args, "%x%n", &words[n], &used) == 1) { args += used; ++n; }
    if (!n) { pe_debug_reply("err %s %s", "find-script", "find-script WORD [WORD ...]"); return; }
    found[0] = 0;
    for (a = g_FieldActorListHead, i = 0; a && i < PE_ACTORS_MAX; a = a->next, ++i) {
        size_t off, bytes;
        if (!a->script_base) continue;
        bytes = Script_TaskBytesAvailable(a->script_base);
        if (bytes > PE_SCRIPT_SCAN) bytes = PE_SCRIPT_SCAN;
        for (off = 0; off + (size_t)n * 4 <= bytes; off += 4)
            if (!memcmp(a->script_base + off, words, (size_t)n * 4)) {
                int written = snprintf(found + at, sizeof found - at, "%s%d@%X", at ? " " : "", i, (unsigned)off);
                if (written < 0 || (size_t)written >= sizeof found - at) break;
                at += written;
                break;
            }
    }
    pe_debug_reply("ok %s %s", "find-script", at ? found : "none");
}

/* Aya to x, z facing yaw, on the floor triangle there. A spot with no floor (off the room's walkable
 * mesh) is refused and everything put back: the field's movement reads the floor triangle, and a
 * null one crashed it (Geo_ClipToFloorBoundary). 1: placed; 0: no floor there. */
static int pe_ctl_set_player(int x, int z, int yaw) {
    FieldActor *p = g_PlayerEntity;
    const int px = p->pos_x, py = p->pos_y, pz = p->pos_z, bx = p->base_x, bz = p->base_z;
    const short ry = p->rot_y, sry = p->saved_rot_y;
    void *const face = p->collision_face, *const mirror = p->collision_face_mirror;
    p->pos_x = p->base_x = x;
    p->pos_z = p->base_z = z;
    p->rot_y = p->saved_rot_y = (short)(yaw & 0xFFF);
    p->collision_face = p->collision_face_mirror = NULL;
    Entity_FindFloor((struct BattleEntity *)p);
    if (p->collision_face) return 1;
    p->pos_x = px; p->pos_y = py; p->pos_z = pz;
    p->base_x = bx; p->base_z = bz;
    p->rot_y = ry; p->saved_rot_y = sry;
    p->collision_face = face; p->collision_face_mirror = mirror;
    return 0;
}

static void pe_ctl_place(const char *args) {
    char detail[64];
    int x, z, yaw;
    int n = sscanf(args, "%d %d %d", &x, &z, &yaw);
    if (n < 2 || !g_PlayerEntity) { pe_debug_reply("err %s %s", "place", g_PlayerEntity ? "place X Z [YAW]" : "no Aya"); return; }
    if (!pe_ctl_set_player(x, z, n >= 3 ? yaw : g_PlayerEntity->rot_y)) {
        pe_debug_reply("err %s %s", "place", "no floor at that spot (off the room's walkable area); Aya not moved");
        return;
    }
    snprintf(detail, sizeof detail, "%d,%d", x, z);
    pe_debug_reply("ok %s %s", "place", detail);
}

static void pe_ctl_place_near(const char *args) {
    char detail[64];
    int index = -1, dist = 0x180000;
    FieldActor *a = NULL, *p = g_PlayerEntity;
    double dx, dz, len;
    if (sscanf(args, "%d %i", &index, &dist) < 1 || !(a = pe_ctl_actor(index)) || !p || a == p) {
        pe_debug_reply("err %s %s", "place-near", !p ? "no Aya" : "place-near I [DIST]");
        return;
    }
    dx = (double)p->pos_x - a->pos_x;
    dz = (double)p->pos_z - a->pos_z;
    len = sqrt(dx * dx + dz * dz);
    if (len < 1.0) { dx = 0; dz = 1; len = 1; }
    dx /= len; dz /= len;
    if (!pe_ctl_set_player(a->pos_x + (int)(dx * dist), a->pos_z + (int)(dz * dist),
                           (int)lround(atan2(-dx, -dz) * 4096.0 / (2.0 * 3.14159265358979)))) {
        pe_debug_reply("err %s %s", "place-near", "no floor at that spot (try a smaller DIST); Aya not moved");
        return;
    }
    snprintf(detail, sizeof detail, "%d,%d,%d", p->pos_x, p->pos_z, p->rot_y);
    pe_debug_reply("ok %s %s", "place-near", detail);
}

static void pe_ctl_var(const char *args, int set) {
    char detail[48];
    int n = -1;
    unsigned v = 0;
    if (sscanf(args, "%d %x", &n, &v) < 1 + set || n < 0 || n >= PE_VAR_COUNT) {
        pe_debug_reply("err %s %s", set ? "set-var" : "get-var", set ? "set-var N V" : "get-var N");
        return;
    }
    if (set) g_ScriptVariableBank[n] = v;
    snprintf(detail, sizeof detail, "%d=%X", n, (unsigned)g_ScriptVariableBank[n]);
    pe_debug_reply("ok %s %s", set ? "set-var" : "get-var", detail);
}

/* The story day as the game's own day change sets it (Scene_SetStoryDay, which day-change scripts
 * call): current and pending day, and the day's character bank reloads at the next room load. */
static void pe_ctl_set_day(const char *args) {
    char detail[48], extra;
    int day = 0;
    if (sscanf(args, "%d %c", &day, &extra) != 1 || day < 1 || day > 8) {
        pe_debug_reply("err %s %s", "set-day", "set-day N (1..8)");
        return;
    }
    if (!pe_free_field()) { pe_debug_reply("err %s %s", "set-day", "not in free field"); return; }
    Scene_SetStoryDay(day);
    snprintf(detail, sizeof detail, "day=%d", (int)g_GameState.current_story_day);
    pe_debug_reply("ok %s %s", "set-day", detail);
}

static void pe_ctl_get_var(const char *args) { pe_ctl_var(args, 0); }
static void pe_ctl_set_var(const char *args) { pe_ctl_var(args, 1); }

static const PeDebugCommand s_commands[] = {
    {"room", PE_DEBUG_EXACT, pe_ctl_room},
    {"warp", PE_DEBUG_ARGS, pe_ctl_warp},
    {"actors", PE_DEBUG_EXACT, pe_ctl_actors},
    {"find-script", PE_DEBUG_ARGS, pe_ctl_find_script},
    {"place-near", PE_DEBUG_ARGS, pe_ctl_place_near},
    {"place", PE_DEBUG_ARGS, pe_ctl_place},
    {"get-var", PE_DEBUG_ARGS, pe_ctl_get_var},
    {"set-var", PE_DEBUG_ARGS, pe_ctl_set_var},
    {"set-day", PE_DEBUG_ARGS, pe_ctl_set_day},
};

const PeDebugModule pe_debug_field_module = {
    "field", s_commands, sizeof s_commands / sizeof *s_commands, NULL, NULL
};
