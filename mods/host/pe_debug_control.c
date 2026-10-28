/* Debug control: lets a test script ask the running game for things a player would need to walk
 * somewhere for. Off unless the environment variable PE_DEBUG_CONTROL names a folder; nothing
 * shows in F1 and players never meet it. A host feature, so it answers whichever mods are on or
 * off (All mods off included).
 *
 * The script writes one command per line to <folder>/cmd (written to cmd.tmp, then renamed); the
 * game picks it up in free field, deletes it, and appends one line per command to <folder>/out:
 * "ok <command> ..." or "err <command> <why>".
 *
 *   save-block [N] build the card file a save to slot N (1 = A, the default) would write, with the
 *                 game's own Save_BuildHeader and Save_BuildCardFile (the mods' save hook, the
 *                 host's records and the CRC included), into <folder>/block.bin: 8 KB, exactly the
 *                 card file. Only the card transfer is skipped; the script puts the file on a card
 *                 and loads it through the game's Continue screen.
 *   mods-tail     write the mods' data as a save made now would hold it (the host's records, read
 *                 from memory, no mod code run) to <folder>/tail.bin: 0x370 bytes, the free tail.
 *   bag           answer "ok bag <id>,<id>,..." : Aya's bag, slot by slot (0 = empty), in hex.
 *   fill-bag ID [LEAVE]  put item ID (hex) in every empty bag slot but LEAVE (0 by default), to
 *                 test a full bag; answers how many it filled.
 *   ping          answer "ok ping <frame>", to tell the game is listening.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pe_game.h"
#include "pe_mod_host.h"
#include "pe_save.h"
#include "pe1/inventory_state.h"
#include "pe1/memcard.h"

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

static void pe_ctl_save_block(int slot) {
    char path[1100], name[64];
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
    pe_ctl_reply("ok %s %s", "save-block", strchr(name, ':') ? strchr(name, ':') + 1 : name);
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
