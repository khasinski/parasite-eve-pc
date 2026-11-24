#include "../../re_host.h"
#include "pe_re_box.h"
#include "pe_re_stage.h"

#include "pe_game.h"
#include "pe_extension_section.h"
#include "pe_mod_host.h"
#include "pe1/field_movement.h"
#include "pe1/akao/spu_voice.h"
#include "pe1/game_state.h"
#include "pe1/script_queue.h"
#include "pe1/menu_dialog.h"
#include "pe1/pad_state.h"
#include "pe1/scene_transition.h"
#include "pe1/scene_dispatch.h"
#include "pe_mod_menu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <SDL3/SDL.h>
#include <psyz/video.h>

/* A tester's bug report: bug-reports/re-<date-time>/ beside the preferences, with report.txt, progress.bin,
 * screenshot.bmp (the game frame as it is now) and log-tail.txt (the end of pe_mods.log). */
static int write_file(const char *dir, const char *name, const void *bytes, size_t size) {
    char path[1300];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    const int ok = fwrite(bytes, 1, size, f) == size;
    return fclose(f) || !ok ? -1 : 0;
}
static int write_report(const char *text, const void *progress, size_t size, char *where, size_t n) {
    char name[64], dir[1100], path[1300];
    const time_t now = time(NULL);
    const struct tm *t = localtime(&now);
    if (!t || !strftime(name, sizeof name, "bug-reports/re-%Y%m%d-%H%M%S", t)) return -1;
    if (pe_mod_path(dir, sizeof dir, name) || !SDL_CreateDirectory(dir)) {
        pe_mod_log("pe_re_play", "bug report: can't make %s", name);
        return -1;
    }
    if (write_file(dir, "report.txt", text, strlen(text)) || write_file(dir, "progress.bin", progress, size)) {
        pe_mod_log("pe_re_play", "bug report: can't write into %s", dir);
        return -1;
    }
    int w = 0, h = 0;
    unsigned char *rgb = Psyz_VideoAllocRenderedFrame(&w, &h);
    if (!rgb || w <= 0 || h <= 0) {
        free(rgb);
        rgb = Psyz_VideoAllocCapturedFrame(&w, &h);
    }
    if (rgb && w > 0 && h > 0) {
        SDL_Surface *frame = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_RGB24, rgb, w * 3);
        snprintf(path, sizeof path, "%s/screenshot.bmp", dir);
        if (!frame || !SDL_SaveBMP(frame, path)) pe_mod_log("pe_re_play", "bug report: no screenshot (%s)", SDL_GetError());
        SDL_DestroySurface(frame);
    }
    free(rgb);
    if (!pe_mod_path(path, sizeof path, "pe_mods.log")) {            /* the last 64 KB of the mods' log */
        FILE *f = fopen(path, "rb");
        if (f) {
            static char tail[64 << 10];
            fseek(f, 0, SEEK_END);
            const long end = ftell(f);
            fseek(f, end > (long)sizeof tail ? end - (long)sizeof tail : 0, SEEK_SET);
            const size_t got = fread(tail, 1, sizeof tail, f);
            fclose(f);
            write_file(dir, "log-tail.txt", tail, got);
        }
    }
    snprintf(where, n, "%s", dir);
    return 0;
}

static void init(const char *mod_id, const char *store) {
    pe_re_stage_init(mod_id, store);
    pe_re_box_init(mod_id);
}

static uint16_t held_buttons(void) {
    const uint16_t raw = (uint16_t)~g_PadPackets[0].buttons;
    return (uint16_t)((raw & 0xffu) << 8 | raw >> 8);
}

static void consume_native_interact(void) {
    g_FieldPadPressed &= ~0x0100u;
}

static void open_save_screen(void) {
    Render_BeginSceneLoad();
    Menu_OpenStartupMemCardDialog();
    g_GameState.flags |= 0x9000;
    g_GameStateFlags |= 4;
}

static int native_battle_active(void) {
    return (g_GameStateBlock.flags & 2) != 0;
}

static int native_menu_active(void) {
    return (g_GameState.flags & 0x9000) != 0;
}

enum {
    PE_TITLE_SCENE = 0xA9400048u,
    PE_PROLOGUE_SCENE = 0xA80830C8u,
    PE_OPENING_DIALOGUE_SCENE = 0xA8001048u,
    /* The PE room under an imported game started from the title: a Central Park path (map 66), free
     * on Day 2 (story 0xE4). Never shown. Its area's Aya (her own outfit, model bank 12) has her
     * battle moves, which a battle started in place needs: Carnegie Hall's foyer (bank 11) has none,
     * and posed with them she folded up. The Day 1 sewer (bank 10) drew her too large, in pieces,
     * and the imported enemies' textures wrong; the park's entrance (map 59) has a police car whose
     * flashing light showed through the imported room, and map 75 has phone booths. */
    PE_HOST_FIELD_SCENE = 0xA8006348u,
    PE_STORY_VAR = 74,
    PE_HOST_FREE_FIELD_STORY = 0xE4
};

static int s_title_start, s_opening_transfer;

static int start_from_title(void) {
    if ((unsigned)g_SceneDispatchToken != PE_TITLE_SCENE) return 0;
    s_title_start = 1;
    pe_mod_panel_close();
    return 1;
}

static int on_title(void *ctx, void *user) {
    (void)user;
    int *request = ctx;
    if (s_title_start && *request == PE_TITLE_STAY) {
        *request = PE_TITLE_START_IMPORTED;
        s_opening_transfer = 1;
    }
    s_title_start = 0;
    return 0;
}

static int start_ready(void) {
    const unsigned token = (unsigned)g_SceneDispatchToken;
    if (token == PE_TITLE_SCENE || token == PE_PROLOGUE_SCENE ||
        token != (unsigned)g_SceneDispatchCur || !pe_game_loaded()) return 0;
    if (s_opening_transfer && token == PE_OPENING_DIALOGUE_SCENE) {
        /* New Game has created Aya and her inventory. Use PE's normal room transfer
         * to give the imported room an initialized field without its opening dialogue. */
        int target = PE_HOST_FIELD_SCENE, *args[] = {&target};
        g_ScriptVariableBank[PE_STORY_VAR] = PE_HOST_FREE_FIELD_STORY;
        Script_EncodeBarcode(args);
        s_opening_transfer = 0;
        return 0;
    }
    s_opening_transfer = 0;
    return pe_free_field();
}

static void bind_lifecycle(const char *mod_id, ReHostCallback frame, ReHostCallback save_read,
                           ReHostCallback new_game) {
    pe_hook_add(PE_HOOK_TITLE_FRAME, on_title, NULL, PE_PRIORITY_LATE, mod_id);
    pe_hook_add(PE_HOOK_FRAME, frame, NULL, PE_PRIORITY_NORMAL, mod_id);
    pe_hook_add(PE_HOOK_SAVE_READ, save_read, NULL, PE_PRIORITY_NORMAL, mod_id);
    pe_hook_add(PE_HOOK_NEW_GAME, new_game, NULL, PE_PRIORITY_NORMAL, mod_id);
}

static PeExtensionSection s_progress;

static int register_progress(const ReHostProgress *progress, ReHostCallback committed)
{
    s_progress = (PeExtensionSection){progress->key, progress->version, progress->min_reader,
                                      progress->size, progress->bytes, progress->upgrade, NULL, NULL};
    if (pe_extension_register(&s_progress) < 0) return -1;
    return pe_hook_add(pe_event_id(PE_EVENT_SAVE_COMMITTED), committed, NULL, PE_PRIORITY_NORMAL, "pe_re_play") < 0 ? -1 : 0;
}

static int enable_progress(void)
{
    return pe_extension_enable(&s_progress) == PE_REG_OK ? 0 : -1;
}

const ReHost *re_host(void) {
    static const ReHost host = {
        .struct_size = sizeof host,
        .init = init,
        .shutdown = pe_re_stage_shutdown,
        .show_room = pe_re_stage_show,
        .hide_room = pe_re_stage_hide,
        .room_active = pe_re_stage_active,
        .room_fade = pe_re_stage_fade,
        .player_pose = pe_re_stage_player,
        .player_floor = pe_re_stage_floor,
        .place_player = pe_re_stage_place,
        .face_player = pe_re_stage_face,
        .position_blocked = pe_re_stage_blocked,
        .set_collision_id = pe_re_stage_collision_id,
        .place_at_ramp = pe_re_stage_to_ramp,
        .in_view = pe_re_stage_in_view,
        .set_masks = pe_re_stage_set_masks,
        .set_player_size = pe_re_stage_set_size,
        .player_scale = pe_re_stage_scale,
        .set_placement_markers = pe_re_stage_set_placement_markers,
        .set_placement_meshes = pe_re_stage_set_placement_meshes,
        .set_placement_idle = pe_re_stage_set_placement_idle,
        .set_placements = pe_re_stage_set_placements,
        .say = pe_re_box_say,
        .choose = pe_re_box_choose,
        .dialogue_choice = pe_re_box_choice,
        .dialogue_busy = pe_re_box_busy,
        .forget_dialogue = pe_re_box_forget,
        .field_free = pe_free_field,
        .held_buttons = held_buttons,
        .consume_native_interact = consume_native_interact,
        .open_save_screen = open_save_screen,
        .native_menu_active = native_menu_active,
        .arm_backdrop = pe_re_stage_backdrop_arm,
        .release_backdrop = pe_re_stage_backdrop_release,
        .bind_lifecycle = bind_lifecycle,
        .set_scene_actors = pe_re_stage_set_scene_actors,
        .set_effects = pe_re_stage_set_effects,
        .climb = pe_re_stage_climb,
        .place_at_ledge = pe_re_stage_to_ledge,
        .scene_controls = pe_re_stage_scene_controls,
        .set_mask_groups = pe_re_stage_set_mask_groups,
        .set_native_music_muted = Akao_SetMusicMuted,
        .native_battle_active = native_battle_active,
        .register_progress = register_progress,
        .enable_progress = enable_progress,
        .movie_start = pe_re_stage_movie_start,
        .movie_tick = pe_re_stage_movie_tick,
        .movie_stop = pe_re_stage_movie_stop,
        .write_report = write_report,
        .room_collision = pe_re_stage_collision,
        .start_from_title = start_from_title,
        .start_ready = start_ready,
    };
    return &host;
}
