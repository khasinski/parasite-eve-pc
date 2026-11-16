/* Debug control: lets a test script ask the running game for things a player would need to walk
 * somewhere for. Off unless the environment variable PE_DEBUG_CONTROL names a folder; nothing
 * shows in F1 and players never meet it. A host feature, so it answers whichever mods are on or
 * off (All mods off included). docs/DEBUG_CONTROL.md has the commands and a small client.
 *
 * The script writes one command per line to <folder>/cmd (written to cmd.tmp, then renamed); the
 * game reads it every 10 frames, in any state, deletes it, and appends one line per command to
 * <folder>/out: "ok <command> ..." or "err <command> <why>". A file, not the control mode's stdin,
 * so it also works in a game someone is playing (to save a spot as a test's starting point).
 * save-block and warp refuse outside free field.
 *
 *   save-block [N] build the card file a save to slot N (1 = A, the default) would write, with the
 *                 game's own Save_BuildHeader and Save_BuildCardFile (the mods' save hook, the
 *                 host's records and the CRC included), into <folder>/block.bin: 8 KB, exactly the
 *                 card file. Only the card transfer is skipped; the script puts the file on a card
 *                 and loads it through the game's Continue screen. A save with mods' data in a
 *                 companion file (".pe-ext" beside the card) answers "ok save-block <card>
 *                 companion=<name>" and copies it to <folder>/companion.pex: the load needs it in
 *                 ".pe-ext/<name>" beside the card. The staged companion is then dropped, so
 *                 nothing is left in the real card folder.
 *   mods-tail     write the mods' data as a save made now would hold it (the host's records, read
 *                 from memory, no mod code run) to <folder>/tail.bin: 0x370 bytes, the free tail.
 *   bag           answer "ok bag <id>,<id>,..." : Aya's bag, slot by slot (0 = empty), in hex.
 *   fill-bag ID [LEAVE]  put item ID (hex) in every empty bag slot but LEAVE (0 by default), to
 *                 test a full bag; answers how many it filled.
 *   ping          answer "ok ping <frame>", to tell the game is listening.
 *
 * Getting somewhere (for a test that needs a room, a person or a spot):
 *   room          answer "ok room map=<N> name=<6 chars> token=<hex> story=<hex> day=<N> free=<0|1>":
 *                 the room, its id, story progress (script variable 74), the story day, free field.
 *   warp MAP [STORY]  go to map number MAP (decimal, as docs/ROOMS.md numbers them; or a room id
 *                 in hex, 0x...) through the field script's own room transfer, the way pe_warp
 *                 does; with STORY (hex), set story progress first. Free field only, and not the
 *                 room Aya is in (that transfer never finishes; set-var 74 instead). Answers when
 *                 the transfer starts; the script waits for "room" to name the new map. Only the
 *                 room and story progress change: who stands in it still follows the rest of the
 *                 story state (day, scene flags), so for a person or an event, start from a save
 *                 made there (save-block in a game being played).
 *   actors        answer "ok actors <i>:<type>.<sub>[*]@<x>,<z>,f<flags> ...": the room's actors in
 *                 the order of the control-mode state ("actors"); * marks Aya.
 *   find-script W [W ...]  the actors whose script holds these words (hex, consecutive), searched
 *                 over the first 32 KB from its base: "ok find-script <i>@<offset> ...". Finds a
 *                 person by what their script does, e.g. Wayne by his Junk count "1040A8 0 14".
 *   place X Z [YAW]  put Aya at X, Z (16.16 field units, as the state's "position"), facing YAW.
 *   place-near I [DIST]  put Aya DIST units (16.16, default 0x180000) from actor I, on the side
 *                 she stands now, facing them.
 *   get-var N     answer "ok get-var <N>=<hex>": script variable N (0..511).
 *   set-var N V   set script variable N to V (hex).
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pe_game.h"
#include "pe_hooks.h"
#include "pe_mod_host.h"
#include "pe_save.h"
#include "pe1/asset_table.h"
#include "pe1/field_actor.h"
#include "pe1/game_state.h"
#include "pe1/game_timers.h"
#include "pe1/inventory_state.h"
#include "pe1/memcard.h"
#include "pe1/player_entity.h"
#include "pe1/scene_dispatch.h"
#include "pe1/script_queue.h"
#include "psyz/system.h"

#define PE_VAR_STORY      74
#define PE_VAR_COUNT      512
#define PE_ROOM_TEMPLATE  0xA8000048u   /* "M" area, map 000: the map digits go in chars 2..4 */
#define PE_SCRIPT_SCAN    0x8000
#define PE_ACTORS_MAX     64

#define PE_POLL_FRAMES 10

static char s_dir[1024];
static unsigned s_frame;

static void pe_ctl_path(char *out, size_t size, const char *name) {
    snprintf(out, size, "%s/%s", s_dir, name);
}

static void pe_ctl_reply(const char *fmt, const char *command, const char *detail) {
    char path[1100];
    FILE *f;
    pe_ctl_path(path, sizeof path, "out");
    if (!(f = fopen(path, "a"))) return;
    fprintf(f, fmt, command, detail ? detail : "");
    fputc('\n', f);
    fclose(f);
}

static void pe_ctl_mods_tail(void) {
    static unsigned char block[0x2000];
    char path[1100];
    FILE *f;
    memset(block, 0, sizeof block);
    pe_save_crash_tail(block);
    pe_ctl_path(path, sizeof path, "tail.bin");
    if (!(f = fopen(path, "wb"))) {
        pe_ctl_reply("err %s %s", "mods-tail", "can't write tail.bin");
        return;
    }
    fwrite(block + PE_SAVE_TAIL_OFFSET, 1, PE_SAVE_TAIL_SIZE, f);
    fclose(f);
    pe_ctl_reply("ok %s %s", "mods-tail", "tail.bin");
}

/* The save's companion file (mods' data kept beside the card, mods/host/pe_extension_store.c),
 * staged next to the card by Save_BuildCardFile: copied to <folder>/companion.pex; its own name,
 * "<card>.<generation>.pex", goes in `out` (empty when the save has none). A load finds it in
 * ".pe-ext" beside the card. */
static int pe_ctl_companion(const char *card, char *out, size_t size) {
    unsigned char link[20], buf[8192];
    unsigned version = 0, got = 0;
    char mapped[1100], from[1300], to[1100];
    const char *base, *a, *b;
    size_t n;
    FILE *in, *dst;
    out[0] = 0;
    if (pe_save_peek(g_MemCardFileBuffer, "pe_host.extlink", &version, link, sizeof link, &got) <= 0 || got != 20)
        return 0;
    Psyz_AdjustPath(mapped, card, sizeof mapped);
    a = strrchr(mapped, '/');
    b = strrchr(mapped, '\\');
    base = !a ? b : !b ? a : a > b ? a : b;
    snprintf(out, size, "%s.%08x.pex", base ? base + 1 : mapped,
             (unsigned)(link[4] | link[5] << 8 | link[6] << 16 | (unsigned)link[7] << 24));
    snprintf(from, sizeof from, "%.*s.pe-ext/%s", base ? (int)(base - mapped + 1) : 0, mapped, out);
    pe_ctl_path(to, sizeof to, "companion.pex");
    if (!(in = fopen(from, "rb"))) return -1;
    n = fread(buf, 1, sizeof buf, in);
    fclose(in);
    if (!(dst = fopen(to, "wb"))) return -1;
    if (fwrite(buf, 1, n, dst) != n) { fclose(dst); return -1; }
    return fclose(dst) ? -1 : 0;
}

static void pe_ctl_save_block(int slot) {
    char path[1100], name[64], companion[96], detail[200];
    FILE *f;
    if (slot < 1 || slot > 15 || !pe_free_field()) {
        pe_ctl_reply("err %s %s", "save-block", slot < 1 || slot > 15 ? "slot 1..15" : "not in free field");
        return;
    }
    /* As Save_StartWriteSlot: the header, then the file named for the slot (its letter titles it). */
    Save_BuildHeader();
    snprintf(name, sizeof name, g_MemCardFilenameFormatPtr, 0, '0', 'A' + slot - 1);
    if (Save_BuildCardFile(name) < 0) {
        pe_ctl_reply("err %s %s", "save-block", "Save_BuildCardFile failed");
        return;
    }
    pe_ctl_path(path, sizeof path, "block.bin");
    if (!(f = fopen(path, "wb")) || fwrite(g_MemCardFileBuffer, 1, sizeof g_MemCardFileBuffer, f) != sizeof g_MemCardFileBuffer) {
        if (f) fclose(f);
        pe_ctl_reply("err %s %s", "save-block", "can't write block.bin");
        return;
    }
    fclose(f);
    detail[0] = 0;
    if (pe_ctl_companion(name, companion, sizeof companion) < 0) {
        pe_mod_save_cancelled();
        pe_ctl_reply("err %s %s", "save-block", "can't copy the companion file");
        return;
    }
    pe_mod_save_cancelled();   /* a test's save is no save: the staged companion beside the card goes */
    snprintf(detail, sizeof detail, "%s%s%s", strchr(name, ':') ? strchr(name, ':') + 1 : name,
             companion[0] ? " companion=" : "", companion);
    pe_ctl_reply("ok %s %s", "save-block", detail);
}

static void pe_ctl_bag(void) {
    char list[50 * 6 + 1];
    int i, n = Inv_GetAyaSlotLimit(), at = 0;
    list[0] = 0;
    for (i = 0; i < n && i < 50; i++)
        at += snprintf(list + at, sizeof list - at, "%s%X", i ? "," : "", (unsigned)(uint16_t)g_AyaInventoryItems[i]);
    pe_ctl_reply("ok %s %s", "bag", list);
}

static void pe_ctl_fill_bag(const char *args) {
    char detail[32];
    unsigned id = 0, leave = 0;
    int i, n = Inv_GetAyaSlotLimit(), empty = 0, filled = 0;
    if (sscanf(args, "%x %u", &id, &leave) < 1 || !id || id > 0xFFFF) {
        pe_ctl_reply("err %s %s", "fill-bag", "fill-bag ID [LEAVE]");
        return;
    }
    for (i = 0; i < n && i < 50; i++) empty += g_AyaInventoryItems[i] == 0;
    for (i = 0; i < n && i < 50 && empty > (int)leave; i++)
        if (!g_AyaInventoryItems[i]) { g_AyaInventoryItems[i] = (s16)id; --empty; ++filled; }
    snprintf(detail, sizeof detail, "%d", filled);
    pe_ctl_reply("ok %s %s", "fill-bag", detail);
}

/* ---- getting somewhere ---- */
static FieldActor *pe_ctl_actor(int index) {
    FieldActor *a = g_FieldActorListHead;
    int i;
    for (i = 0; a && i < index && i < PE_ACTORS_MAX; ++i) a = a->next;
    return index >= 0 && i == index ? a : NULL;
}

/* Map number -> room id: the template with the three digits in chars 2..4 (g_Base32CharTable). */
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

static void pe_ctl_room(void) {
    char name[8] = { 0 }, detail[160];
    Text_EncodeBase32(name, (unsigned)g_SceneDispatchToken);
    name[6] = 0;
    snprintf(detail, sizeof detail, "map=%d name=%s token=%08X story=%X day=%d free=%d", pe_room_number(), name,
             (unsigned)g_SceneDispatchToken, (unsigned)g_ScriptVariableBank[PE_VAR_STORY],
             (int)g_GameState.current_story_day, pe_free_field());
    pe_ctl_reply("ok %s %s", "room", detail);
}

static void pe_ctl_warp(const char *args) {
    char target[32], detail[64];
    unsigned id, story = 0;
    int arg, n = sscanf(args, "%31s %x", target, &story);
    int *argv[1];
    if (n < 1) { pe_ctl_reply("err %s %s", "warp", "warp MAP [STORY]"); return; }
    id = !strncmp(target, "0x", 2) || !strncmp(target, "0X", 2) ? (unsigned)strtoul(target, NULL, 16)
                                                                   : pe_ctl_room_id(atoi(target));
    if (!id || (id >> 24) != (PE_ROOM_TEMPLATE >> 24)) { pe_ctl_reply("err %s %s", "warp", "no such room"); return; }
    if (!pe_free_field() || (unsigned)g_SceneDispatchCur != (unsigned)g_SceneDispatchToken) {
        pe_ctl_reply("err %s %s", "warp", "not in free field");
        return;
    }
    if (id == (unsigned)g_SceneDispatchToken) {      /* the transfer never finishes into its own room */
        pe_ctl_reply("err %s %s", "warp", "already in that room");
        return;
    }
    if (n >= 2) g_ScriptVariableBank[PE_VAR_STORY] = story;
    arg = (int)id;
    argv[0] = &arg;
    (void)Script_EncodeBarcode(argv);
    snprintf(detail, sizeof detail, "token=%08X", id);
    pe_ctl_reply("ok %s %s", "warp", detail);
}

static void pe_ctl_actors(void) {
    static char list[PE_ACTORS_MAX * 48];
    FieldActor *a;
    int i, at = 0;
    list[0] = 0;
    for (a = g_FieldActorListHead, i = 0; a && i < PE_ACTORS_MAX; a = a->next, ++i)
        at += snprintf(list + at, sizeof list - at, "%s%d:%u.%u%s@%d,%d,f%X", i ? " " : "", i, a->type_id, a->sub_id,
                       a == g_PlayerEntity ? "*" : "", a->pos_x, a->pos_z, a->flags);
    pe_ctl_reply("ok %s %s", "actors", list);
}

static void pe_ctl_find_script(const char *args) {
    static char found[512];
    unsigned words[8];
    int n = 0, used = 0, at = 0, i;
    FieldActor *a;
    while (n < 8 && sscanf(args, "%x%n", &words[n], &used) == 1) { args += used; ++n; }
    if (!n) { pe_ctl_reply("err %s %s", "find-script", "find-script WORD [WORD ...]"); return; }
    found[0] = 0;
    for (a = g_FieldActorListHead, i = 0; a && i < PE_ACTORS_MAX; a = a->next, ++i) {
        int off;
        if (!a->script_base) continue;
        for (off = 0; off + n * 4 <= PE_SCRIPT_SCAN; off += 4)
            if (!memcmp(a->script_base + off, words, (size_t)n * 4)) {
                at += snprintf(found + at, sizeof found - at, "%s%d@%X", at ? " " : "", i, off);
                break;
            }
    }
    pe_ctl_reply("ok %s %s", "find-script", at ? found : "none");
}

static void pe_ctl_set_player(int x, int z, int yaw) {
    FieldActor *p = g_PlayerEntity;
    p->pos_x = p->base_x = x;
    p->pos_z = p->base_z = z;
    p->rot_y = p->saved_rot_y = (short)(yaw & 0xFFF);
}

static void pe_ctl_place(const char *args) {
    char detail[64];
    int x, z, yaw;
    int n = sscanf(args, "%d %d %d", &x, &z, &yaw);
    if (n < 2 || !g_PlayerEntity) { pe_ctl_reply("err %s %s", "place", g_PlayerEntity ? "place X Z [YAW]" : "no Aya"); return; }
    pe_ctl_set_player(x, z, n >= 3 ? yaw : g_PlayerEntity->rot_y);
    snprintf(detail, sizeof detail, "%d,%d", x, z);
    pe_ctl_reply("ok %s %s", "place", detail);
}

/* Yaw as the field keeps it: 0x1000 a full turn. Aya is turned toward the actor. */
static void pe_ctl_place_near(const char *args) {
    char detail[64];
    int index = -1, dist = 0x180000;
    FieldActor *a = NULL, *p = g_PlayerEntity;
    double dx, dz, len;
    if (sscanf(args, "%d %i", &index, &dist) < 1 || !(a = pe_ctl_actor(index)) || !p || a == p) {
        pe_ctl_reply("err %s %s", "place-near", !p ? "no Aya" : "place-near I [DIST]");
        return;
    }
    dx = (double)p->pos_x - a->pos_x;
    dz = (double)p->pos_z - a->pos_z;
    len = sqrt(dx * dx + dz * dz);
    if (len < 1.0) { dx = 0; dz = 1; len = 1; }
    dx /= len; dz /= len;
    pe_ctl_set_player(a->pos_x + (int)(dx * dist), a->pos_z + (int)(dz * dist),
                      (int)lround(atan2(-dx, -dz) * 4096.0 / (2.0 * 3.14159265358979)));
    snprintf(detail, sizeof detail, "%d,%d,%d", p->pos_x, p->pos_z, p->rot_y);
    pe_ctl_reply("ok %s %s", "place-near", detail);
}

static void pe_ctl_var(const char *args, int set) {
    char detail[48];
    int n = -1;
    unsigned v = 0;
    if (sscanf(args, "%d %x", &n, &v) < 1 + set || n < 0 || n >= PE_VAR_COUNT) {
        pe_ctl_reply("err %s %s", set ? "set-var" : "get-var", set ? "set-var N V" : "get-var N");
        return;
    }
    if (set) g_ScriptVariableBank[n] = v;
    snprintf(detail, sizeof detail, "%d=%X", n, (unsigned)g_ScriptVariableBank[n]);
    pe_ctl_reply("ok %s %s", set ? "set-var" : "get-var", detail);
}

static void pe_ctl_run(char *line) {
    char detail[32];
    line[strcspn(line, "\r\n")] = 0;
    if (!*line) return;
    pe_log("debug_control", "command %s", line);
    if (!strcmp(line, "ping")) {
        snprintf(detail, sizeof detail, "%u", s_frame);
        pe_ctl_reply("ok %s %s", "ping", detail);
    } else if (!strncmp(line, "save-block", 10) && (!line[10] || line[10] == ' ')) {
        pe_ctl_save_block(line[10] ? atoi(line + 11) : 1);
    } else if (!strncmp(line, "fill-bag ", 9)) {
        pe_ctl_fill_bag(line + 9);
    } else if (!strcmp(line, "room")) {
        pe_ctl_room();
    } else if (!strncmp(line, "warp ", 5)) {
        pe_ctl_warp(line + 5);
    } else if (!strcmp(line, "actors")) {
        pe_ctl_actors();
    } else if (!strncmp(line, "find-script ", 12)) {
        pe_ctl_find_script(line + 12);
    } else if (!strncmp(line, "place-near ", 11)) {
        pe_ctl_place_near(line + 11);
    } else if (!strncmp(line, "place ", 6)) {
        pe_ctl_place(line + 6);
    } else if (!strncmp(line, "get-var ", 8)) {
        pe_ctl_var(line + 8, 0);
    } else if (!strncmp(line, "set-var ", 8)) {
        pe_ctl_var(line + 8, 1);
    } else if (!strcmp(line, "bag")) {
        pe_ctl_bag();
    } else if (!strcmp(line, "mods-tail")) {
        pe_ctl_mods_tail();
    } else {
        pe_ctl_reply("err %s %s", line, "unknown command");
    }
}

static void pe_ctl_poll(void) {
    char path[1100], line[256];
    FILE *f;
    pe_ctl_path(path, sizeof path, "cmd");
    if (!(f = fopen(path, "r"))) return;
    while (fgets(line, sizeof line, f)) pe_ctl_run(line);
    fclose(f);
    remove(path);
}

static int pe_ctl_frame(void *ctx, void *user) {
    (void)ctx; (void)user;
    ++s_frame;
    if (s_frame % PE_POLL_FRAMES == 0) pe_ctl_poll();
    return 0;
}

void pe_debug_control_install(void) {
    const char *dir = getenv("PE_DEBUG_CONTROL");
    if (!dir || !*dir || strlen(dir) >= sizeof s_dir) return;
    snprintf(s_dir, sizeof s_dir, "%s", dir);
    pe_hook_add(PE_HOOK_FRAME, pe_ctl_frame, NULL, PE_PRIORITY_NORMAL, "host");
    pe_mod_log(NULL, "debug control on: %s", s_dir);
}
