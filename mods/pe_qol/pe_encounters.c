/* Recomp's No random battles switch, using the native script variable bank.
 * PS1 halfword 0x800A781A is the upper half of script variable 10. This is
 * game save data, so the live row follows loads rather than an ini setting.
 * Only a switch enabled this session is held against script writes. */
#include "pe_game.h"
#include "pe_mod_host.h"
#include "pe_mod_menu.h"
#include "pe_pages.h"
#include "pe1/game_timers.h"

#define PE_SKIP_BATTLES 0x80000000u
static int s_force;
static const char *const s_names[] = { "Off", "On" };

static int pe_get_skip(void) {
    return pe_game_loaded() && (g_ScriptVariableBank[10] & PE_SKIP_BATTLES) != 0;
}

static int pe_set_skip(int value) {
    if (!pe_game_loaded()) return 0;
    if (value) g_ScriptVariableBank[10] |= PE_SKIP_BATTLES;
    else g_ScriptVariableBank[10] &= ~PE_SKIP_BATTLES;
    s_force = value != 0;
    return 1;
}

static void pe_encounters_tick(void) {
    if (!pe_game_loaded()) { s_force = 0; return; }
    if (s_force) g_ScriptVariableBank[10] |= PE_SKIP_BATTLES;
}

static int pe_encounters_reset(void *ctx, void *user) {
    (void)ctx;
    (void)user;
    s_force = 0;
    return 0;
}

static int pe_encounters_command(void *ctx, void *user) {
    (void)ctx;
    pe_encounters_reset(NULL, user);
    return 0;
}

void pe_encounters_register(const char *mod_id) {
    pe_menu_register_live_choice(PE_PAGE_COMBAT, "No random battles",
        "Stops random battles. This switch is saved with the game.",
        s_names, 2, pe_get_skip, pe_set_skip);
    pe_menu_register_tick(pe_encounters_tick);
    pe_hook_add(PE_HOOK_SAVE_READ, pe_encounters_reset, NULL, PE_PRIORITY_NORMAL, mod_id);
    pe_hook_add(PE_HOOK_NEW_GAME, pe_encounters_command, NULL, PE_PRIORITY_NORMAL, mod_id);
}

#if PE_NATIVE_STATE_CPU_COMPONENTS
#include "pe_checkpoint_wire.h"
#include "pe_checkpoint_qol.h"
int pe_encounters_checkpoint_walk(const unsigned char *in,unsigned char *out,int apply){if((!in&&!out)||0)return 0;if(apply&&(!in||!pe_encounters_checkpoint_walk(in,0,0)))return 0;PeCheckpointWire w={in,out,0,apply,1};PE_CK_U(w,s_force,0,1,0);return w.ok&&w.at==4u;}
#endif
