#include "../../pe_re_play_runtime.h"
#include "../../pe_re_session.h"

#include "pe_mod_api.h"
#include "pe_mod_menu.h"

#define PAGE "Play imported games"

void pe_re_weapon_next_round_action(void);   /* pe_re_weapons.c */

static void open_title(void)
{
    pe_mod_panel_close();
    pe_re_play_controls()->open_title();
}

static int init(void)
{
    char store[1100];
    if (pe_mod_path(store, sizeof store, "import/store") || pe_re_play_init(store)) return 1;
    const PeRePlayControls *controls = pe_re_play_controls();
    pe_menu_begin(PAGE);
    pe_menu_top_page(PAGE, "Walk the rooms of a game you imported, as Aya.", 63);
    pe_menu_register_dynamic_choice(0, "Game", "A game imported from your own disc (F1 > Import).", controls->pick,
                                    PE_RE_GAMES, controls->game_count, controls->game_name);
    pe_menu_register_int(0, "Scenario", "RE2: 0 Leon, 1 Claire.", controls->player, 0, 1, 1);
    pe_menu_register_toggle(0, "Foreground", "Let the room's foreground cover Aya.", controls->masks, NULL);
    pe_menu_register_int(0, "Aya's size", "Percent of an RE person's height (100: as tall as Leon).", controls->size, 50, 200, 5);
    pe_menu_register_action(0, "RE2 title menu", "Choose a new RE2 scenario or continue the current save.", open_title);
    pe_menu_register_action(0, "Start", "Starts in the game's first room. Walk with the pad; Cross in a doorway goes through.", controls->start);
    pe_menu_register_action(0, "Continue", "Back where you left this save's imported game.", controls->resume);
    pe_menu_register_action(0, "Stop", "Back to the Parasite Eve room and camera; Continue picks it up again.", controls->stop);
    pe_menu_register_action(0, "Write a bug report", "Saves a folder with a screenshot, where you are and what the room's scripts are doing, for the developers.", controls->bug_report);
    pe_menu_register_action("Testing", "Load the next rounds",
                            "The launcher in hand loads its next kind of rounds that has any (explosive, flame, acid).",
                            pe_re_weapon_next_round_action);
    pe_menu_register_toggle("Testing", "Placement markers", "Mark enemy positions using the room camera.", controls->placement_markers, NULL);
    pe_menu_register_toggle("Testing", "Placement meshes", "Draw imported enemy meshes at their placements.", controls->placement_meshes, NULL);
    pe_menu_register_toggle("Testing", "Placement idle animation", "Animate supported placement meshes in place.", controls->placement_idle, NULL);
    pe_menu_register_action("Testing", "Go to the next door", "Puts Aya in the room's doorways, one after another.", controls->to_door);
    pe_menu_register_action("Testing", "Go to the next pickup", "Puts Aya on the room's items, one after another.", controls->to_pickup);
    pe_menu_register_action("Testing", "Go to the next ramp", "Puts Aya at the foot of the room's slopes and stairs, one after another.", controls->to_ramp);
    pe_menu_register_action("Testing", "Go to a climbable ledge", "Puts Aya in front of a one-level ledge in this room.", controls->to_ledge);
    pe_menu_register_action("Testing", "Go to the next message", "Puts Aya where the room's messages are read, one after another.", controls->to_message);
    pe_menu_register_action("Testing", "Go to the next place to use an item", "Puts Aya facing where the room takes an item (a jewel in a statue), one after another.", controls->to_item_spot);
    pe_menu_register_action("Testing", "Give the item used there", "Puts the item that place takes in the pocket.", controls->give_use_here);
    pe_menu_register_action("Testing", "Go to the next typewriter", "Puts Aya at the room's typewriter: press Cross.", controls->to_typewriter);
    pe_menu_register_action("Testing", "Give an Ink Ribbon", "One Ink Ribbon into the pocket, for a typewriter.", controls->give_ribbon);
    pe_menu_register_action("Testing", "Go to the next item box", "Puts Aya at the room's item box: press Cross.", controls->to_item_box);
    pe_menu_register_action("Testing", "Go to the next event zone", "Puts Aya where the room's events start (examined: press Cross), one after another.", controls->to_event);
    pe_menu_register_int("Testing", "Stage", "The room to jump to: its stage.", controls->jump_stage, 1, 7, 1);
    pe_menu_register_int("Testing", "Room", "The room to jump to: its number in the stage (1-05 is 5).", controls->jump_room, 0, 63, 1);
    pe_menu_register_action("Testing", "Go to that room", "Jumps there, camera 0, keeping what's been picked up.", controls->to_room);
    pe_menu_register_int("Testing", "Door", "A door of this room by its number (0 first), for Go to that door.", controls->door_pick, 0, 31, 1);
    pe_menu_register_action("Testing", "Go to that door", "Puts Aya in that doorway (or the next one the scripts set up).", controls->to_that_door);
    pe_menu_register_action(0, "Refresh the list", "Looks for games imported since the game started.", controls->refresh);
    pe_menu_register_info("Testing", "Scene control", "Who controls Aya while a room script plays.", controls->text_scene);
    pe_menu_register_info(0, "Status", "Where you are, or the last thing said.", controls->text_status);
    pe_menu_register_info(0, "Pocket", "What you've picked up in this game.", controls->text_pocket);
    pe_menu_register_info(0, "Scripts", "What this room's own scripts ran when you came in.", controls->text_scripts);
    pe_menu_register_info(0, "Enemies", "Live enemy placements reported to the encounter mod.", controls->text_enemies);
    pe_menu_register_int("Testing", "Enemy slot", "Choose a slot for movement details. -1 shows the room summary.",
                         controls->enemy_debug_slot, -1, 15, 1);
    pe_menu_register_info("Testing", "Enemy movement", "Enemy position, script routine and movement status in this room.", controls->text_enemy_movement);
    return 0;
}

const PeMod pe_mod_pe_re_play = {"pe_re_play", PAGE, "0.1", NULL, init, pe_re_play_shutdown};
