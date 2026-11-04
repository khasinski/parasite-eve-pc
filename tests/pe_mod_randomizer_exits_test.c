/* The New York map, for the randomizer's room map. A door to M0000 runs the effect-scene loop, which
 * is the city map: the player picks one of ten destinations and FxCommon_EndSequence turns that pick
 * into the room to load. This runs the real overlay code over every input it reads and checks that
 * each destination has one enable bit of V0x3 and that every pick lands on a room on the disc.
 * With a path argument the result is written there, one line per fact, for mods/pe_randomizer/tools:
 *   dest  <d>  bit=<V0x3 bit that enables it>  left=<d>  right=<d>  (the ring the pad walks, FF = end)
 *   start <V0x1>  dest=<d>                    (the room left picks the first destination; others: 7)
 *   mode  <V0x4a range>  auto=<d> | confirm=<d> | preset=<d>  (forced or fixed picks)
 *   exit  dest=<d>  night=<0|1>  V0x4a=<range>  -> M<room>
 * Night is V0x3 & 0x2000. Leaving the map sets V0x1 to 999 (FxCommon_EndSequence). */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>
#include <psyz/video.h>
#include "common.h"
#include "pe1/akao/queue.h"
#include "pe1/asset_table.h"
#include "pe1/boot_disc_check.h"
#include "pe1/boot_services.h"
#include "pe1/cdrom.h"
#include "pe1/game_state.h"
#include "pe1/scene_room.h"
#include "pe1/script_queue.h"
#include "../src/overlays/fx_common/fx_common_setup.h"
#include "../src/overlays/fx_common/fx_common_motion.h"

/* Fixture I/O: the test-disc object renames the real image reads to these. Once the map's sectors
 * are in memory, the probes that only change V0x1 / V0x4a skip the re-read of the same sectors. */
int CdRom_ImageReadSectorsFromLba(u32 lba, void *buffer, u32 count);
int CdRom_ImagePollReady(void);
static int reuse_reads;
int CdRom_ReadSectorsFromLba(u32 lba, void *buffer, u32 count) { return reuse_reads ? 0 : CdRom_ImageReadSectorsFromLba(lba, buffer, count); }
int CdRom_PollReady(void) { return reuse_reads ? 0 : CdRom_ImagePollReady(); }
int Asset_Find08w(int id, int mode, int x, int y, int z) { (void)id; (void)mode; (void)x; (void)y; (void)z; return 0; }

enum { DESTS = 10, AREA_END = 0x1000, NIGHT = 0x2000 };
static unsigned failures, exit_runs;
#define FAIL(...) do { if (++failures <= 40) { fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)

/* Room number a scene token names, or -1. */
static int token_room(u32 token) {
    char name[8] = {0};
    Text_EncodeBase32(name, token);
    if (name[0] != 'M' || name[5] != 'I') return -1;
    for (int i = 1; i < 5; ++i) if (name[i] < '0' || name[i] > '9') return -1;
    int room = atoi(name + 1);
    return room >= 1 && room <= SCENE_ROOM_COUNT && g_RoomSectorTable[room - 1].roomSectors ? room : -1;
}

static void set_vars(u32 left, u32 area, u32 flags) {
    g_ScriptVariableBank[SCRIPT_VAR_ROOM] = left; g_ScriptVariableBank[SCRIPT_VAR_AREA] = area;
    g_ScriptVariableBank[SCRIPT_VAR_FLAGS] = flags;
}

/* Which destination each V0x3 bit turns on, and the left/right ring. */
static void probe_dests(FILE *out) {
    int bit_of[DESTS]; u8 left[DESTS] = {0}, right[DESTS] = {0};
    for (int d = 0; d < DESTS; ++d) bit_of[d] = -1;
    set_vars(0, 0, 0);
    if (FxCommon_InitScene()) { FAIL("FxCommon_InitScene failed with V0x3 = 0"); return; }
    for (int d = 0; d < DESTS; ++d) if (g_FxCommonEffectSetup[d].enabled) FAIL("dest %d enabled with V0x3 = 0", d);
    for (int b = 0; b < 32; ++b) {
        if (1u << b == NIGHT) continue; /* night only swaps the model set */
        set_vars(0, 0, 1u << b);
        if (FxCommon_InitScene()) { FAIL("FxCommon_InitScene failed with V0x3 bit %d", b); continue; }
        for (int d = 0; d < DESTS; ++d) {
            left[d] = g_FxCommonEffectSetup[d].value22; right[d] = g_FxCommonEffectSetup[d].value23;
            if (g_FxCommonEffectSetup[d].enabled != 1) continue;
            if (bit_of[d] >= 0) FAIL("dest %d enabled by bits %d and %d", d, bit_of[d], b);
            bit_of[d] = b;
        }
    }
    for (int d = 0; d < DESTS; ++d) {
        if (bit_of[d] < 0) FAIL("dest %d has no enable bit", d);
        if (out) fprintf(out, "dest\t%d\tbit=0x%x\tleft=%X\tright=%X\n", d, bit_of[d] < 0 ? 0 : 1u << bit_of[d], left[d], right[d]);
    }
}

/* First destination per room left, and the forced picks per V0x4a. Same sectors each time. */
static void probe_starts(FILE *out) {
    reuse_reads = 1;
    for (u32 left = 0; left <= SCENE_ROOM_COUNT; ++left) {
        set_vars(left, 0, 0);
        if (FxCommon_SelectRoomVariant() < 0) { FAIL("SelectRoomVariant failed for V0x1 = %u", left); continue; }
        int d = g_FxCommonRoomVariant;
        if (d < 0 || d >= DESTS) FAIL("V0x1 = %u starts at dest %d", left, d);
        else if (d != 7 && out) fprintf(out, "start\tM%03u\tdest=%d\n", left, d);
    }
    int run_lo = 0; s32 run[3] = {-2, -2, -2};
    for (int area = 0; area <= AREA_END; ++area) {
        s32 now[3] = {-1, -1, -1};
        if (area < AREA_END) {
            set_vars(0, (u32)area, 0);
            if (FxCommon_SelectRoomVariant() < 0) { FAIL("SelectRoomVariant failed for V0x4a = 0x%x", area); continue; }
            now[0] = g_FxCommonAutoPlay == 1 ? g_FxCommonAutoVariant : -1;
            now[1] = g_FxCommonConfirmExit == 1 ? g_FxCommonConfirmExitVariant : -1;
            now[2] = g_FxCommonStartPreset;
        }
        if (area && !memcmp(now, run, sizeof(now))) continue;
        if (area && out && (run[0] >= 0 || run[1] >= 0 || run[2] >= 0)) {
            fprintf(out, "mode\t0x%x-0x%x", run_lo, area - 1);
            if (run[0] >= 0) fprintf(out, "\tauto=%d", run[0]);
            if (run[1] >= 0) fprintf(out, "\tconfirm=%d", run[1]);
            if (run[2] >= 0) fprintf(out, "\tpreset=%d", run[2]);
            fputc('\n', out);
        }
        run_lo = area; memcpy(run, now, sizeof(now));
    }
    reuse_reads = 0;
}

/* The room each pick loads, over every V0x4a and both times of day. */
static void probe_exits(FILE *out) {
    Psyz_VideoSetVsyncMode(PSYZ_VSYNC_LIMITLESS); /* the fade-out waits 30 blanks per call */
    for (int d = 0; d < DESTS; ++d) for (int night = 0; night < 2; ++night) {
        int run_lo = 0, run = -2;
        for (int area = 0; area <= AREA_END; ++area) {
            int room = -1;
            if (area < AREA_END) {
                set_vars(0, (u32)area, night ? NIGHT : 0);
                g_FxCommonExitVariant = d; /* EndSequence sets g_SceneDispatchToken first thing */
                FxCommon_EndSequence();
                /* Its music fade-out queues sound commands; nothing consumes them here, and a full
                 * queue makes the next call wait for a consumer forever. */
                g_AkaoMessageQueueCount = 0;
                room = token_room(g_SceneDispatchToken);
                if (room < 0) FAIL("dest %d night %d V0x4a 0x%x: token 0x%08X is not a room", d, night, area, (unsigned)g_SceneDispatchToken);
                if (g_ScriptVariableBank[SCRIPT_VAR_ROOM] != 999) FAIL("dest %d: V0x1 not set to 999", d);
            }
            if (area && room == run) continue;
            if (area) ++exit_runs;
            if (area && out) fprintf(out, "exit\tdest=%d\tnight=%d\tV0x4a=0x%x-0x%x\t-> M%03d\n", d, night, run_lo, area - 1, run);
            run_lo = area; run = room;
        }
    }
}

int main(int argc, char **argv) {
    const char *path = getenv("PE_TEST_DISC");
    if (!path || !*path) return getenv("PE_REQUIRE_GAME_ASSETS") ? 1 : 77;
    PeDisc disc = {0};
    PeDiscFile archive, exe;
    /* The map presents frames: software renderer, no window, no audio device, no pacing. A window
     * would also let a close event end the run through psyz's exit(0) before the file is written. */
    if (!SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "PSYZ_HEADLESS", "1", true) ||
        !SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "SDL_AUDIODRIVER", "dummy", true)) return 1;
    if (!SDL_Init(SDL_INIT_EVENTS) || !pe_disc_open(&disc, path)) return 1;
    ResetGraph(0); CdRom_BindImage(&disc, NULL);
    if (Asset_OpenPeImage() != 0) return 1; /* the room table, to check exits against */
    Boot_InitGameState(); D_8009CDC8 = 'm';
    /* The map's sector entries and image base, as the effect-scene loop has them. */
    if (!pe_disc_find(&disc, "PE.IMG", &archive) || !pe_disc_find(&disc, "SLUS_006.62", &exe) ||
        !pe_disc_read(&disc, &exe, 0x930d8 - 0x10000 + 0x800 + 76 * 2, g_PeImageSectorTable + 76, 10)) return 1;
    Boot_InitMemoryLayout();
    CdRom_BindImage(&disc, &archive); g_GameState.pe_image_base_lba = archive.sector;
    FILE *out = argc > 1 ? fopen(argv[1], "w") : NULL;
    if (argc > 1 && !out) return 1;
    probe_dests(out);
    probe_starts(out);
    probe_exits(out);
    if (exit_runs < 2 * DESTS) FAIL("only %u exit ranges for %d destinations by day and night", exit_runs, DESTS);
    if (out && fclose(out)) FAIL("writing %s failed", argv[1]);
    CdRom_BindImage(NULL, NULL); pe_disc_close(&disc);
    if (failures) { fprintf(stderr, "%u failures\n", failures); return 1; }
    printf("Randomizer exits: %d map destinations with one enable bit each, every pick lands on a room\n", DESTS);
    return 0;
}
