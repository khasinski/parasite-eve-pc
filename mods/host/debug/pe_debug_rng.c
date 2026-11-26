/* Debug-control rng commands. See docs/DEBUG_CONTROL.md. */
#include "pe_debug_internal.h"
#include <stdlib.h>
#include <string.h>
#include "pe1/random.h"
#include "pe1/battle_start.h"
#include "psyz/system.h"


static void pe_ctl_rng(const char *args, int set) {
    char detail[1200], hex[1043], extra;
    unsigned state, frame;
    int index, i;
    if (set) {
        if (sscanf(args, "%u %u %d %1042s %c", &state, &frame, &index, hex, &extra) != 4 ||
            index < -1 || index >= 521 || strlen(hex) != 1042 || strspn(hex, "0123456789abcdefABCDEF") != 1042) {
            pe_debug_reply("err %s %s", "rng-set", "rng-set STATE FRAME INDEX HEX1042 (from rng-get)"); return;
        }
        for (i = 0; i < 521; ++i) {
            unsigned byte;
            sscanf(hex + i * 2, "%2x", &byte);
            g_ItemDropRngTable[i] = (u8)byte;
        }
        g_ItemDropRngIndex = index;
        g_FrameRngCounter = frame;
        Psyz_Srand(state);
    }
    for (i = 0; i < 521; ++i) snprintf(hex + i * 2, 3, "%02x", g_ItemDropRngTable[i]);
    snprintf(detail, sizeof detail, "%u %u %d %s", Psyz_RandState(), g_FrameRngCounter, g_ItemDropRngIndex, hex);
    pe_debug_reply("ok %s %s", set ? "rng-set" : "rng-get", detail);
}

static void pe_ctl_rng_get(const char *args) { (void)args; pe_ctl_rng("", 0); }
static void pe_ctl_rng_set(const char *args) { pe_ctl_rng(args, 1); }

static const PeDebugCommand s_commands[] = {
    {"rng-get", PE_DEBUG_EXACT, pe_ctl_rng_get},
    {"rng-set", PE_DEBUG_ARGS, pe_ctl_rng_set},
};

const PeDebugModule pe_debug_rng_module = {
    "rng", s_commands, sizeof s_commands / sizeof *s_commands, NULL, NULL
};
