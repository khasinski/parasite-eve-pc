/* Recomp battle frequency, adapted to the native script RNG hook. Match the
 * same Random -> less-than -> JumpIfZero -> battle-start sequence, but adjust
 * the local RNG bound for this call instead of patching loaded bytecode.
 * No stored script pointers, frame scans or changes to unrelated RNG rolls. */
#include <stdint.h>
#include "pe_game.h"
#include "pe_mod_host.h"
#include "pe_mod_menu.h"
#include "pe_pages.h"
#include "pe_encounter.h"
#include "pe_combat_presets.h"
#include "pe1/game_state.h"
#include "pe1/game_timers.h"
#include "pe1/scene_dispatch.h"
#include "pe1/scene_room.h"

enum { PE_ENC_NORMAL, PE_ENC_MORE, PE_ENC_LOTS, PE_ENC_ALWAYS };
static int s_mode;
static const char *const s_names[] = { "Normal", "More (2x)", "Lots (4x)", "Every check" };

static int pe_on_random(void *ctx, void *user) {
    PeHookScriptRandom *roll = (PeHookScriptRandom *)ctx;
    SceneAssetView *room;
    char name[8];
    int map, end;
    size_t bytes, offset;
    uintptr_t base, next;
    (void)user;
    if (s_mode == PE_ENC_NORMAL || !pe_game_loaded() || pe_in_battle() ||
        (g_ScriptVariableBank[10] & 0x80000000u) || !roll->next ||
        *roll->start != 0 || *roll->end < 2 || *roll->end > 0x1000) return 0;
    room = g_GameState.loaded_scene_assets;
    if (!room) return 0;
    Text_EncodeBase32(name, (unsigned)g_SceneDispatchCur);
    map = Text_ParseMapNumber((signed char *)name) - 1;
    if (map < 0 || map >= SCENE_ROOM_COUNT) return 0;
    bytes = (size_t)g_RoomSectorTable[map].roomSectors * 2048;
    base = (uintptr_t)room;
    next = (uintptr_t)roll->next;
    if (next < base || next - base > bytes || next - base < 20) return 0;
    offset = (size_t)(next - base) - 20;
    if (!pe_encounter_roll((const unsigned char *)room + offset, bytes - offset)) return 0;
    end = *roll->end;
    if (s_mode == PE_ENC_ALWAYS) end = 1;
    else if (s_mode == PE_ENC_MORE) end /= 2;
    else if (s_mode == PE_ENC_LOTS) end /= 4;
    if (end < 1) end = 1;
    *roll->end = end;
    return 0;
}

static void pe_combat_battle_frequency_apply(int preset)
{
    (void)preset;
    s_mode = PE_ENC_NORMAL;
}

static int pe_combat_battle_frequency_matches(int preset)
{
    (void)preset;
    return s_mode == PE_ENC_NORMAL;
}

void pe_battle_frequency_register(const char *mod_id) {
    pe_combat_preset_add(mod_id, pe_combat_battle_frequency_apply, pe_combat_battle_frequency_matches);
    pe_menu_register_choice(PE_PAGE_COMBAT, "Battle frequency",
        "More random encounter checks succeed. No random battles overrides this.",
        &s_mode, s_names, 4);
    pe_hook_add(PE_HOOK_SCRIPT_RANDOM, pe_on_random, NULL, PE_PRIORITY_NORMAL, mod_id);
}

#if PE_NATIVE_STATE_CPU_COMPONENTS
#include "pe_checkpoint_wire.h"
#include "pe_checkpoint_qol.h"
int pe_frequency_checkpoint_walk(const unsigned char *in,unsigned char *out,int apply){if((!in&&!out)||0)return 0;if(apply&&(!in||!pe_frequency_checkpoint_walk(in,0,0)))return 0;PeCheckpointWire w={in,out,0,apply,1};PE_CK_U(w,s_mode,0,3,1);return w.ok&&w.at==4u;}
#endif
