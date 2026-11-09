/* Sega Dreamcast entry point (KallistiOS).
 *
 * Replaces src/main/boot/main.c, which is the desktop host: command line,
 * settings overlay, HD packs, crash reports and the SDL window. This file
 * keeps only what the game needs to run: mount the two USA disc images,
 * initialise the system and run the original scene dispatch loop, the same
 * loop as main.c.
 *
 * Disc images are looked up in this order (first set where one opens). A
 * raw MODE2/2352 BIN needs no CUE: the image type is read from the ISO9660
 * header, and which disc it is from its contents, so the burnt disc can
 * carry just PE1.BIN and PE2.BIN.
 *   /cd/PE1.BIN, /cd/PE2.BIN      on the burnt disc
 *   /cd/PE1.CUE, /cd/PE2.CUE      the same with CUE sheets
 *   /pc/pe1.bin, /pc/pe2.bin      over dc-tool (dcload-ip / dcload-serial)
 *   /pc/pe1.cue, /pc/pe2.cue
 *   /sd/pe1.bin, /sd/pe2.bin      on an SD card adapter
 *   /sd/pe1.cue, /sd/pe2.cue
 *
 * The two images do not fit on one CD-R, so the burnt set is two discs:
 * 1ST_READ.BIN with PE1.BIN, and 1ST_READ.BIN with PE2.BIN. When the game
 * needs the disc that is not in the drive it asks through
 * g_CdImageRequest, and RequestDisc shows a prompt until that disc is in.
 */
#include "pe1/port_features.h"
#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <kos.h>
#include <psyz/audio.h>
#include <psyz/gpu.h>
#include <psyz/system.h>
#include <psyz/video.h>
#include "pe1/boot.h"
#include "pe1/boot_disc_check.h"
#include "pe1/boot_display.h"
#include "pe1/boot_services.h"
#include "pe1/cdrom.h"
#include "pe1/game_state.h"
#include "pe1/game_state_types.h"
#include "pe1/game_timers.h"
#include "pe1/memory_budget.h"
#include "pe1/pe_image.h"
#include "pe1/scene_dispatch.h"
#include "pe_hooks.h"

KOS_INIT_FLAGS(INIT_DEFAULT);

/* same values as main.c */
#define STATE_FLAG_IMAGE_BUSY 0x00100000U
#define STATE_FLAG_READY 0x00000100U

static const char *const disc_paths[][2] = {
    {"/cd/PE1.BIN", "/cd/PE2.BIN"},
    {"/cd/PE1.CUE", "/cd/PE2.CUE"},
    {"/pc/pe1.bin", "/pc/pe2.bin"},
    {"/pc/pe1.cue", "/pc/pe2.cue"},
    {"/sd/pe1.bin", "/sd/pe2.bin"},
    {"/sd/pe1.cue", "/sd/pe2.cue"},
};

static PeDisc images[2];
static int mounted_set = -1; /* row of disc_paths the images came from */

/* DcGpu_ShowMessage in PsyZ's Dreamcast renderer */
void DcGpu_ShowMessage(const char *const *lines, int count);

static int MountImages(void) {
    for (unsigned set = 0; set < sizeof(disc_paths) / sizeof(disc_paths[0]); ++set) {
        int mounted_any = 0;
        for (unsigned slot = 0; slot < 2; ++slot) {
            const char *path = disc_paths[set][slot];
            if (!pe_disc_open(&images[slot], path)) {
                continue;
            }
            CdRom_BindImage(&images[slot], NULL);
            int status = Asset_OpenPeImage();
            unsigned identity = g_PeImageMountFlags;
            if (status || (identity != 1 && identity != 2) || g_CdImageVolumes[identity - 1]) {
                fprintf(stderr, "%s: not a USA Parasite Eve disc (status %d)\n", path, status);
                pe_disc_close(&images[slot]);
                continue;
            }
            g_CdImageVolumes[identity - 1] = &images[slot];
            printf("Disc %u: %s\n", identity, path);
            mounted_any = 1;
        }
        if (mounted_any) {
            mounted_set = (int)set;
            CdRom_BindImage(g_CdImageVolumes[0] ? g_CdImageVolumes[0] : g_CdImageVolumes[1], NULL);
            return 1;
        }
    }
    CdRom_BindImage(NULL, NULL);
    return 0;
}

/* Mounts disc `identity` from the drive if it is in: either name, BIN or CUE. */
static int MountFromDrive(unsigned identity) {
    for (unsigned set = 0; set < 2; ++set) {
        for (unsigned slot = 0; slot < 2; ++slot) {
            PeDisc *disc = &images[identity - 1];
            unsigned found = 0;
            if (!pe_disc_open(disc, disc_paths[set][slot])) {
                continue;
            }
            if (Asset_ValidatePeImage(disc, &found) == 0 && found == identity) {
                g_CdImageVolumes[identity - 1] = disc;
                printf("Disc %u: %s\n", identity, disc_paths[set][slot]);
                return 1;
            }
            pe_disc_close(disc);
        }
    }
    return 0;
}

/* g_CdImageRequest: the game needs disc `identity`, which is not mounted.
 * With the images on the burnt discs, ask for that disc and wait for it;
 * over dc-tool or SD both images are mounted from the start, or never. */
static int RequestDisc(unsigned identity) {
    if (identity < 1 || identity > 2 || mounted_set < 0 || mounted_set > 1) {
        return 0;
    }
    /* the disc in the drive is going away: nothing may read its image */
    CdRom_BindImage(NULL, NULL);
    for (unsigned i = 0; i < 2; ++i) {
        if (g_CdImageVolumes[i]) {
            pe_disc_close(g_CdImageVolumes[i]);
            g_CdImageVolumes[i] = NULL;
        }
    }
    printf("Waiting for disc %u\n", identity);
    char title[48];
    snprintf(title, sizeof(title), "Please insert Disc %u", identity);
    const char *lines[] = {title, "", "Open the lid, change the disc",
                           "and close the lid."};
    for (unsigned frame = 1;; ++frame) {
        DcGpu_ShowMessage(lines, 4);
        vid_waitvbl();
        if (frame % 30) {
            continue;
        }
        int status = 0, type = 0;
        if (cdrom_get_status(&status, &type) < 0 || status == CD_STATUS_OPEN ||
            status == CD_STATUS_NO_DISC || status == CD_STATUS_BUSY) {
            continue;
        }
        if (MountFromDrive(identity)) {
            return 1;
        }
    }
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    int mode = 0;
    int token;
    Pe1GameState *state;

    printf("Parasite Eve for Dreamcast (PVR renderer, AICA sound)\n");
    if (!MountImages()) {
        fprintf(stderr, "No disc image found; see platform/dreamcast/README.md\n");
        return EXIT_FAILURE;
    }

    g_BootHostActive = 1;
    (void)PE_HOOK(PE_HOOK_INIT, (&(PeHookInit){0, ""}));
    /* No PsyZ VRAM copy: mesh_list.c reads it only for widescreen, which
     * the Dreamcast build does not have, and keeping it costs 3 MB and a
     * second fill of every cleared area. */
    g_CdImageRequest = RequestDisc;
    Boot_InitSystem();

    for (;;) {
        state = &g_GameState;
        Boot_SyncShutdown();

        int image_status = Asset_OpenPeImage();
        if (image_status != 0) {
            fprintf(stderr, "Game image mount failed: %d\n", image_status);
            break;
        }

        Boot_InitGameState();
        Boot_InitSubsystems();
        int startup_status = Scene_LoadSceneData();
        if (startup_status < 0) {
            fprintf(stderr, "Scene startup failed: %d\n", startup_status);
            break;
        }

        g_SceneDispatchToken = 0xA9400048;

        do {
            if (g_GameState.flags & STATE_FLAG_IMAGE_BUSY) {
                if (Render_InitDisplayLists(mode) < 0) {
                    fprintf(stderr, "Game image selection failed\n");
                    goto shutdown;
                }
                g_GameState.flags &= ~STATE_FLAG_IMAGE_BUSY;
            }

            Scene_LoadFieldBg();
            token = g_SceneDispatchToken;
            g_SceneDispatchCur = token;

            switch (token) {
            case 0xA8000048:
                if (Boot_LoadOverlayTables() < 0 || FxCommon_RunFrameLoop() < 0) {
                    fprintf(stderr, "Effect scene startup failed\n");
                    goto shutdown;
                }
                g_GameState.flags |= 1;
                break;
            case 0xAA108448:
                if ((g_GameState.display_list_modes & 2) == 0) {
                    mode = 2;
                    g_GameState.flags |= STATE_FLAG_IMAGE_BUSY;
                    break;
                }
                if (CdRom_LoadBootAudio() < 0 || BootDisplay_RunDisplay() < 0) {
                    fprintf(stderr, "Boot display failed\n");
                    goto shutdown;
                }
                g_SceneDispatchToken = 0xA80830C8;
                g_GameState.flags |= 1;
                break;
            case 0xA9400048: {
                if (CdRom_LoadInitialImage() < 0) {
                    fprintf(stderr, "Title assets failed\n");
                    goto shutdown;
                }
                int title_choice = MenuCard_RunListScreen();
                if (title_choice == PE_NEW_GAME_TITLE || title_choice == PE_NEW_GAME_TUTORIAL)
                    (void)PE_HOOK(PE_HOOK_NEW_GAME, (&(PeHookNewGame){title_choice}));
                Draw_InitDisplay(title_choice);
                g_GameState.flags |= 3;
                break;
            }
            default:
                if (Boot_RunFrame() < 0) {
                    fprintf(stderr, "Scene frame startup failed\n");
                    goto shutdown;
                }
                break;
            }

            /* same display-list mode switch as main.c */
            if (g_SceneDispatchToken != 0xA80651C8 && g_SceneDispatchToken != 0xA8065248 &&
                g_SceneDispatchToken != 0xA80652C8 && g_SceneDispatchToken != 0xA80660C8 &&
                g_SceneDispatchToken != 0xA8066148 && g_SceneDispatchToken != 0xA80661C8 &&
                g_SceneDispatchToken != 0xA8066348) {
                if (g_PlayTimeFrameCounter < 600) {
                    if ((g_GameState.display_list_modes & 1) == 0) {
                        mode = 1;
                        g_GameState.flags |= STATE_FLAG_IMAGE_BUSY;
                    }
                } else if ((g_GameState.display_list_modes & 2) == 0) {
                    mode = 2;
                    g_GameState.flags |= STATE_FLAG_IMAGE_BUSY;
                }
            }
        } while ((state->flags & STATE_FLAG_READY) == 0);

        VSync(0);
        SetDispMask(0);
        state->flags &= ~STATE_FLAG_READY;
    }

shutdown:
    Psyz_AudioDestroy();
    CdRom_BindImage(NULL, NULL);
    g_CdImageVolumes[0] = g_CdImageVolumes[1] = NULL;
    pe_disc_close(&images[0]);
    pe_disc_close(&images[1]);
    return EXIT_FAILURE;
}
