#include "../../re_host.h"
#include "pe_re_box.h"
#include "pe_re_stage.h"

#include "pe_game.h"
#include "pe_mod_host.h"
#include "pe1/field_movement.h"
#include "pe1/game_state.h"
#include "pe1/script_queue.h"
#include "pe1/menu_dialog.h"
#include "pe1/pad_state.h"
#include "pe1/scene_transition.h"
#include "pe1/scene_dispatch.h"
#include "pe_mod_menu.h"

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

static int native_menu_active(void) {
    return (g_GameState.flags & 0x9000) != 0;
}

enum {
    PE_TITLE_SCENE = 0xA9400048u,
    PE_PROLOGUE_SCENE = 0xA80830C8u,
    PE_OPENING_DIALOGUE_SCENE = 0xA8001048u,
    PE_CARNEGIE_FIELD_SCENE = 0xA8000148u,
    PE_STORY_VAR = 74,
    PE_CARNEGIE_FREE_FIELD_STORY = 9
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
        int target = PE_CARNEGIE_FIELD_SCENE, *args[] = {&target};
        g_ScriptVariableBank[PE_STORY_VAR] = PE_CARNEGIE_FREE_FIELD_STORY;
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
        .room_collision = pe_re_stage_collision,
        .start_from_title = start_from_title,
        .start_ready = start_ready,
    };
    return &host;
}
