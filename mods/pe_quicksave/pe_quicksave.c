/* Quick save slots, from the Pathogen Eve port (where they were save states).
 *
 * A quick save is the game's own memory-card save, made anywhere in the free field: the same 8 KB
 * card file Save_BuildCardFile writes (with every mod's save records), kept beside the user's
 * preferences as quicksave_<slot>.mcd. Loading one is the card load (Save_LoadCardFileIntoRuntime
 * without its dialogs) followed by the field script's own room transfer to the saved room, so the
 * game restarts that room the way a loaded save does. A quick save holds what a save holds:
 * Aya, items, storage, story and the room, not her place in it or a fight in progress.
 *
 * F10 saves to the slot, F11 loads it, F12 picks the next slot; also F1 > Quick save. Both run on
 * the next free field frame: never in a battle, an event, a room change, a menu or a movie. */
#include <stdio.h>
#include <string.h>
#include <SDL3/SDL.h>

#include "pe_game.h"
#include "pe_extension_store.h"
#include "pe_checkpoint_game.h"
#include "pe_mod_host.h"
#include "pe_mod_menu.h"
#include "pe1/battle_modifiers.h"
#include "pe1/battle_saved_state.h"
#include "pe1/game_state.h"
#include "pe1/inventory.h"
#include "pe1/memcard.h"
#include "pe1/save.h"
#include "pe1/save_blob.h"
#include "pe1/scene_dispatch.h"
#include "pe1/script_queue.h"

#define MOD_ID "pe_quicksave"
#define PE_PAGE_QUICKSAVE "Quick save"
#define PE_SLOTS 10
#define PE_CARD_SIZE 0x2000
#define PE_CARD_CRC (0x100 + 0x12E4 + 0x8A8)   /* the checksum after the tail */
#define PE_RELOAD_SAME 0x800u                  /* g_GameState.flags: the same room again */

#define PE_TOKEN_TITLE    0xA9400048u
#define PE_TOKEN_NEW_GAME 0xAA108448u
#define PE_TOKEN_BOOT     0xA8000048u
#define PE_TOKEN_PROLOGUE 0xA80830C8u

enum { PE_NONE, PE_SAVE, PE_LOAD };
static int s_slot, s_pending, s_hotkeys = 1;
static bool s_keys_down[3];

static void pe_slot_path(char *out, size_t size, int slot) {
    char name[32];
    snprintf(name, sizeof name, "quicksave_%d.pec", slot);
    if (pe_mod_path(out, size, name) != 0) snprintf(out, size, "%s", name);
}

static int pe_field_ready(void) {
    const unsigned token = (unsigned)g_SceneDispatchToken;
    if (!pe_game_loaded() || !token || token == PE_TOKEN_TITLE || token == PE_TOKEN_NEW_GAME ||
        token == PE_TOKEN_BOOT || token == PE_TOKEN_PROLOGUE) return -1;
    return (unsigned)g_SceneDispatchCur == token && pe_free_field() &&
           !g_GameState.scene_audio.tracks.movie_active;
}

static u32 pe_card_crc(const u8 *card) {
    u32 crc = 0xFFFF;
    for (size_t i = 0; i < PE_CARD_SIZE; ++i) {
        const u8 value = i >= PE_CARD_CRC && i < PE_CARD_CRC + 4 ? 0 : card[i];
        crc ^= (u32)value << 8;
        for (unsigned bit = 0; bit < 8; ++bit) crc = crc & 0x8000 ? (crc << 1) ^ 0x1021 : crc << 1;
    }
    return ~crc & 0xFFFF;
}

/* The card file of a save made now, into g_MemCardFileBuffer, as the crash report's room snapshot
 * makes it: the save menu's header fields for the copy only, and Aya's two record links set back
 * to the saved records for the copy when something changed them. */
static void pe_save(int slot) {
    char path[1024], msg[64];
    int ok;
    if (pe_checkpoint_build_card() < 0) {
        pe_mod_save_cancelled();
        pe_toast("Quick save failed: the game can't be saved right now"); return;
    }
    pe_slot_path(path, sizeof path, slot);
    ok = pe_checkpoint_write(path, g_MemCardFileBuffer) == 0;
    pe_mod_save_cancelled();
    if (!ok) {
        pe_toast("Quick save failed: the previous checkpoint was kept"); return;
    }
    snprintf(msg, sizeof msg, "Quick saved to slot %d", slot);
    pe_toast(msg);
    pe_log("quicksave", "slot=%d room=%d", slot, pe_room_number());
}

static void pe_load(int slot) {
    char path[1024], msg[64];
    FILE *f;
    size_t n;
    u32 crc;
    int token, *args[1] = {&token};

    pe_slot_path(path, sizeof path, slot);
    f = fopen(path, "rb");
    const int have_checkpoint = f != NULL;
    if (f) fclose(f);
    if (pe_checkpoint_read(path, "BASLUS-00662PEQUICKSAVE000A", g_MemCardFileBuffer)) {
        if (have_checkpoint) {
            pe_toast("Quick load refused: checkpoint is damaged or incompatible"); return;
        }
        /* Explicit legacy import: legacy companion links still require their source. */
        char name[32]; snprintf(name, sizeof name, "quicksave_%d.mcd", slot);
        if (pe_mod_path(path, sizeof path, name)) {
            pe_toast("Quick load failed: checkpoint unavailable"); return;
        }
        f = fopen(path, "rb");
        if (!f) { pe_toast("Quick load failed: checkpoint unavailable or damaged"); return; }
        n = fread(g_MemCardFileBuffer, 1, PE_CARD_SIZE, f);
        int complete = n == PE_CARD_SIZE && fgetc(f) == EOF;
        fclose(f);
        if (!complete || pe_mod_load_admit(path, g_MemCardFileBuffer)) {
            pe_toast("Legacy quick save is damaged or its companion is missing"); return;
        }
    }
    n = PE_CARD_SIZE;
    memcpy(&crc, g_MemCardFileBuffer + PE_CARD_CRC, sizeof crc);
    if (n != PE_CARD_SIZE || !pe_checkpoint_native_valid(g_MemCardFileBuffer) || pe_card_crc(g_MemCardFileBuffer) != crc ||
        !Inv_ValidateSavedInventory((const SaveRuntimeState *)&g_MemCardSaveStateBuffer)) {
        snprintf(msg, sizeof msg, "Slot %d is damaged", slot);
        pe_toast(msg);
        return;
    }
    if(pe_checkpoint_commit_portable())return;
    token=g_SceneDispatchToken;
    snprintf(msg, sizeof msg, "Quick loaded slot %d", slot);
    pe_toast(msg);
    pe_log("quickload", "slot=%d token=%08X", slot, (unsigned)token);
}

static void pe_keys(void) {
    static const SDL_Scancode keys[3] = {SDL_SCANCODE_F10, SDL_SCANCODE_F11, SDL_SCANCODE_F12};
    const bool *state = SDL_GetKeyboardState(NULL);
    if (!state) return;
    for (int i = 0; i < 3; ++i) {
        const bool down = state[keys[i]];
        if (down && !s_keys_down[i] && s_hotkeys) {
            if (i == 0) s_pending = PE_SAVE;
            else if (i == 1) s_pending = PE_LOAD;
            else {
                char msg[32];
                s_slot = (s_slot + 1) % PE_SLOTS;
                snprintf(msg, sizeof msg, "Quick save slot %d", s_slot);
                pe_toast(msg);
            }
        }
        s_keys_down[i] = down;
    }
}

static int pe_on_frame(void *ctx, void *user) {
    int ready;
    (void)ctx; (void)user;
    pe_keys();
    if (!s_pending) return 0;
    ready = pe_field_ready();
    if (ready < 0) { pe_toast("Quick save: only in the field"); s_pending = PE_NONE; return 0; }
    if (!ready) return 0;                   /* a battle, an event or a room change: wait */
    if (s_pending == PE_SAVE) pe_save(s_slot); else pe_load(s_slot);
    s_pending = PE_NONE;
    return 0;
}
static int pe_on_new_game(void *ctx, void *user) {
    (void)ctx; (void)user;
    s_pending = PE_NONE;
    return 0;
}

static void pe_ask_save(void) { s_pending = PE_SAVE; }
static void pe_ask_load(void) { s_pending = PE_LOAD; }
static void pe_slot_info(char *out, unsigned size) {
    char path[1024];
    FILE *f;
    pe_slot_path(path, sizeof path, s_slot);
    f = fopen(path, "rb");
    snprintf(out, size, "%s", f ? "Saved" : "Empty");
    if (f) fclose(f);
}

static int quicksave_init(void) {
    pe_hook_add(PE_HOOK_FRAME, pe_on_frame, NULL, PE_PRIORITY_LATE, MOD_ID);
    pe_hook_add(PE_HOOK_NEW_GAME, pe_on_new_game, NULL, PE_PRIORITY_NORMAL, MOD_ID);

    pe_menu_begin(PE_PAGE_QUICKSAVE);
    pe_menu_top_page(PE_PAGE_QUICKSAVE, "Save anywhere in the field, 10 slots.", 40);
    pe_menu_register_int(0, "Slot", "Which of the 10 slots F10 / F11 and the rows below use.",
                         &s_slot, 0, PE_SLOTS - 1, 1);
    pe_menu_register_info(0, "This slot", "Whether the slot holds a quick save.", pe_slot_info);
    pe_menu_register_action(0, "Save", "Save to the slot (once the panel closes, from the field).", pe_ask_save);
    pe_menu_register_action(0, "Load", "Load the slot: the saved room starts again, as a loaded save does.",
                            pe_ask_load);
    pe_menu_register_toggle(0, "Hotkeys", "F10 saves, F11 loads, F12 picks the next slot.", &s_hotkeys, NULL);
    pe_menu_begin(0);
    return 0;
}

const PeMod pe_mod_pe_quicksave = {MOD_ID, "Quick save", "0.1", NULL, quicksave_init, NULL};
