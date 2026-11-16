/* Debug-control save commands. See docs/DEBUG_CONTROL.md. */
#include "pe_debug_internal.h"
#include <stdlib.h>
#include <string.h>
#include "pe_game.h"
#include "pe_save.h"
#include "pe_mod_host.h"
#include "pe1/memcard.h"
#include "psyz/system.h"


static void pe_ctl_mods_tail(const char *args) {
    (void)args;
    static unsigned char block[0x2000];
    char path[1100];
    FILE *f;
    memset(block, 0, sizeof block);
    pe_save_crash_tail(block);
    pe_debug_path(path, sizeof path, "tail.bin");
    if (!(f = fopen(path, "wb"))) {
        pe_debug_reply("err %s %s", "mods-tail", "can't write tail.bin");
        return;
    }
    fwrite(block + PE_SAVE_TAIL_OFFSET, 1, PE_SAVE_TAIL_SIZE, f);
    fclose(f);
    pe_debug_reply("ok %s %s", "mods-tail", "tail.bin");
}

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
    pe_debug_path(to, sizeof to, "companion.pex");
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
        pe_debug_reply("err %s %s", "save-block", slot < 1 || slot > 15 ? "slot 1..15" : "not in free field");
        return;
    }
    /* As Save_StartWriteSlot: the header, then the file named for the slot (its letter titles it). */
    Save_BuildHeader();
    snprintf(name, sizeof name, g_MemCardFilenameFormatPtr, 0, '0', 'A' + slot - 1);
    if (Save_BuildCardFile(name) < 0) {
        pe_debug_reply("err %s %s", "save-block", "Save_BuildCardFile failed");
        return;
    }
    pe_debug_path(path, sizeof path, "block.bin");
    if (!(f = fopen(path, "wb")) || fwrite(g_MemCardFileBuffer, 1, sizeof g_MemCardFileBuffer, f) != sizeof g_MemCardFileBuffer) {
        if (f) fclose(f);
        pe_debug_reply("err %s %s", "save-block", "can't write block.bin");
        return;
    }
    fclose(f);
    detail[0] = 0;
    if (pe_ctl_companion(name, companion, sizeof companion) < 0) {
        pe_mod_save_cancelled();
        pe_debug_reply("err %s %s", "save-block", "can't copy the companion file");
        return;
    }
    pe_mod_save_cancelled();   /* a test's save is no save: the staged companion beside the card goes */
    snprintf(detail, sizeof detail, "%s%s%s", strchr(name, ':') ? strchr(name, ':') + 1 : name,
             companion[0] ? " companion=" : "", companion);
    pe_debug_reply("ok %s %s", "save-block", detail);
}

static void pe_ctl_save(const char *args) { pe_ctl_save_block(args ? atoi(args) : 1); }

static const PeDebugCommand s_commands[] = {
    {"save-block", PE_DEBUG_OPTIONAL, pe_ctl_save},
    {"mods-tail", PE_DEBUG_EXACT, pe_ctl_mods_tail},
};

const PeDebugModule pe_debug_save_module = {
    "save", s_commands, sizeof s_commands / sizeof *s_commands, NULL, NULL
};
