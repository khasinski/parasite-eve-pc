/* Cheats, from the Pathogen Eve port (its warp menu is pe_warp). Everything is off until chosen in
 * F1 > Cheats; while every cheat is off nothing here writes game state.
 *
 *   God mode        Aya's HP is held at its maximum, in the field and in battle.
 *   Infinite ammo   the loaded rounds and the three ammo pools never go down, and Parasite
 *                   Energy stays full.
 *   Win this battle every living enemy's HP goes to 0; the game's own victory and rewards run.
 *   PE powers       learns every power a level-up can teach (not the unused entries); Restore
 *                   gives back the set learned before.
 *   Test arsenal    for testing: the strongest weapon of each kind from the game's own item table,
 *                   and the developers' DEBUGSMG, equipped with a full magazine. While the DEBUGSMG
 *                   is equipped, every hit it lands kills.
 */
#include <stdio.h>

#include "pe_game.h"
#include "pe_mod_host.h"
#include <stddef.h>

#include "pe_mod_menu.h"
#include "pe_services.h"
#include "re_play_api.h"
#include "pe1/aya.h"
#include "pe1/battle.h"
#include "pe1/battle_runtime.h"
#include "pe1/game_state.h"
#include "pe1/game_timers.h"
#include "pe1/inventory.h"
#include "pe1/inventory_queries.h"
#include "pe1/inventory_state.h"
#include "pe1/battle_modifiers.h"
#include "pe1/battle_saved_state.h"
#include "pe1/item_ids.h"
#include "pe1/weapon_list_output.h"

#define MOD_ID "pe_cheats"
#define PE_PAGE_CHEATS "Cheats"
#define PE_ALL_POWERS 0xFFFFFu

static int s_god, s_ammo, s_powers;   /* s_powers: Learn all ran this game */
static int s_arsenal_on_start = 1;    /* give the test arsenal when an imported game starts */
static unsigned s_games_seen, s_arsenal_due;
static int s_pending_win;
static unsigned s_powers_before;
static const BattleAction *s_ammo_action;
static const ItemDataRecord *s_ammo_gun;
static unsigned s_ammo_loaded, s_ammo_held, s_ammo_pool[3];

static void pe_god(void) {
    Combatant *aya = pe_aya_core();
    if (!s_god || !pe_game_loaded()) return;
    if (aya && aya->maxHP && aya->curHP != aya->maxHP) aya->curHP = aya->maxHP;
    if (g_AyaSaveState.max_hp && g_AyaSaveState.current_hp != g_AyaSaveState.max_hp)
        g_AyaSaveState.current_hp = g_AyaSaveState.max_hp;
}

/* Each count is held at the highest value seen, so reloading and picking up ammo still work. */
static void pe_ammo(void) {
    Combatant *aya = pe_aya_core();
    ItemDataRecord *gun = pe_equipped(0);
    if (!s_ammo || !pe_game_loaded()) return;
    if (pe_in_battle() && aya && aya->action) {
        BattleAction *a = aya->action;
        const unsigned loaded = a->attackWord & 0x3FFu;
        if (a != s_ammo_action || loaded > s_ammo_loaded) { s_ammo_action = a; s_ammo_loaded = loaded; }
        if (loaded < s_ammo_loaded) a->attackWord = (a->attackWord & ~0x3FFu) | s_ammo_loaded;
    }
    if (gun) {
        if (gun != s_ammo_gun || gun->ammo > s_ammo_held) { s_ammo_gun = gun; s_ammo_held = gun->ammo; }
        if (gun->ammo < s_ammo_held) gun->ammo = (u16)s_ammo_held;
    }
    for (int k = 0; k < 3; ++k) {
        const unsigned v = g_KeyItemRecords[k].ammo;
        if (v > s_ammo_pool[k]) s_ammo_pool[k] = v;
        if (v < s_ammo_pool[k]) g_KeyItemRecords[k].ammo = (u16)s_ammo_pool[k];
    }
    if (aya && aya->maxPe > 0 && aya->currentPe != aya->maxPe) aya->currentPe = aya->maxPe;
}

static void pe_win(void) {
    int n = 0;
    char msg[48];
    s_pending_win = 0;
    if (!pe_in_battle()) { pe_toast("Win this battle: not in a battle"); return; }
    for (int i = 0; i < 7; ++i) {
        BattleEnemySlot *enemy = &g_BattleEnemySlots[i];
        if (enemy->active && enemy->combatant.hpAlive > 0) { enemy->combatant.hpAlive = 0; ++n; }
    }
    snprintf(msg, sizeof msg, "Battle won: %d enem%s", n, n == 1 ? "y" : "ies");
    pe_toast(msg);
    pe_log("cheat_win", "enemies=%d", n);
}

/* Powers a level-up can teach: the unlock key of each of the 20 entries is the level that teaches
 * it, so the unused entries are those outside 1..99. */
static unsigned pe_learnable(void) {
    const ParasiteSpellEntry *table = Aya_GetParasiteSpellUnlockTable();
    unsigned mask = 0;
    if (!table) return PE_ALL_POWERS;
    for (unsigned i = 0; i < 20; ++i)
        if (table[i].unlockKey >= 1 && table[i].unlockKey <= 99) mask |= 1u << i;
    return mask ? mask : PE_ALL_POWERS;
}
static void pe_powers_learn(void) {
    u32 *flags = g_AyaParasiteSpellFlags;
    if (!pe_save_loaded()) { pe_toast("Load a game first"); return; }
    if (!s_powers) { s_powers_before = *flags; s_powers = 1; }
    *flags |= pe_learnable();
    pe_toast("All Parasite Energy powers learned");
}
static void pe_powers_restore(void) {
    u32 *flags = g_AyaParasiteSpellFlags;
    if (!pe_save_loaded() || !s_powers) { pe_toast("Nothing to restore"); return; }
    *flags = (*flags & ~PE_ALL_POWERS) | (s_powers_before & PE_ALL_POWERS);
    s_powers = 0;
    pe_toast("Parasite Energy powers restored");
}

static void pe_arsenal(void);

/* An imported game (RE play) started: give the test arsenal once Aya is free in the field. */
static void pe_arsenal_on_start(void) {
    const PeRePlayApi *api = pe_service_get(RE_PLAY_SERVICE, RE_PLAY_SERVICE_VERSION);
    if (!api || api->struct_size < offsetof(PeRePlayApi, games_started) + sizeof api->games_started ||
        !api->games_started)
        return;
    const unsigned started = api->games_started();
    if (started != s_games_seen) {
        s_games_seen = started;
        s_arsenal_due = s_arsenal_on_start;
    }
    if (s_arsenal_due && pe_save_loaded() && pe_free_field()) {
        s_arsenal_due = 0;
        pe_arsenal();
    }
}

static int pe_on_frame(void *ctx, void *user) {
    (void)ctx; (void)user;
    pe_god();
    pe_ammo();
    pe_arsenal_on_start();
    return 0;
}
static int pe_on_battle(void *ctx, void *user) {
    (void)ctx; (void)user;
    pe_god();
    pe_ammo();
    if (s_pending_win) pe_win();
    return 0;
}
static int pe_on_new_game(void *ctx, void *user) {
    (void)ctx; (void)user;
    s_powers = 0;
    s_pending_win = 0;
    s_ammo_action = NULL; s_ammo_gun = NULL;
    s_ammo_loaded = s_ammo_held = s_ammo_pool[0] = s_ammo_pool[1] = s_ammo_pool[2] = 0;
    return 0;
}

/* ---- the test arsenal ---- */
enum { ARSENAL_KINDS = 9 };   /* weapon kinds are 1..8; 9 is armor */

static int pe_bag_slot_of(unsigned item_id) {
    const int n = Inv_GetAyaSlotLimit();
    for (int i = 0; i < n && i < 50; ++i) {
        const int value = g_AyaInventoryItems[i];
        if (value >= 0x100 && value < 0x180 && g_InventoryRuntime.equipment[value - 0x100].iconId &&
            g_InventoryRuntime.equipment[value - 0x100].itemId == item_id)
            return i;
    }
    return -1;
}

static void pe_equip_slot(int slot) {
    g_AyaSaveState.equipped_weapon_slot = slot;
    BattleCmd_LoadWeaponModifiers();
    Inv_RecalcSlotStats();
    Inv_BuildWeaponList(0, &g_SavedBattleStateTail.weapon);
    Inv_BuildArmorList(&g_BattleEquipStateBlock);
}

static void pe_arsenal(void) {
    unsigned best[ARSENAL_KINDS] = {0};
    int added = 0, full = 0;
    char msg[64];
    if (!pe_save_loaded() || !pe_free_field()) { pe_toast("Test arsenal: walk in the field first"); return; }
    /* the strongest base weapon of each kind (attack), leaving out the special guns */
    for (unsigned id = ITEM_CLUB_1; id <= ITEM_M92F; ++id) {
        const ItemDataRecord *base = pe_base_item(id);
        if (!base || !base->iconId || id == ITEM_DEBUGSMG || id == ITEM_MAEDAS_GUN || id == ITEM_MAEDAS_GUN_93) continue;
        if (base->kind < 1 || base->kind >= ARSENAL_KINDS) continue;
        const ItemDataRecord *held = best[base->kind] ? pe_base_item(best[base->kind]) : NULL;
        if (!held || base->baseStats[0] > held->baseStats[0]) best[base->kind] = id;
    }
    Inv_SelectActiveList(0);
    /* the DEBUGSMG first: an early bag (10 slots) can't hold every kind */
    if (pe_bag_slot_of(ITEM_DEBUGSMG) < 0) {
        if (Inv_AddItem(ITEM_DEBUGSMG) != 0) ++full;
        else ++added;
    }
    for (unsigned kind = 1; kind < ARSENAL_KINDS; ++kind) {
        if (!best[kind] || pe_bag_slot_of(best[kind]) >= 0) continue;
        if (Inv_AddItem((int)best[kind]) != 0) { ++full; continue; }
        ++added;
    }
    const int slot = pe_bag_slot_of(ITEM_DEBUGSMG);
    if (slot >= 0) {
        ItemDataRecord *gun = &g_InventoryRuntime.equipment[g_AyaInventoryItems[slot] - 0x100];
        const unsigned capacity = gun->baseStats[2] + gun->bonusStats[2];
        if (capacity) gun->ammo = (u16)(capacity < 1000 ? capacity : 999);
        pe_equip_slot(slot);
    }
    snprintf(msg, sizeof msg, "Test arsenal: %d added%s%s", added, full ? ", bag full for some" : "",
             slot >= 0 ? "; DEBUGSMG equipped" : "");
    pe_toast(msg);
    pe_log("cheat_arsenal", "added=%d full=%d debug_slot=%d", added, full, slot);
}

/* The DEBUGSMG kills whatever it hits (the hit's damage becomes the target's remaining HP). */
static int pe_on_enemy_damage(void *ctx, void *user) {
    PeHookEnemyDamage *hit = ctx;
    const ItemDataRecord *gun = pe_equipped(0);
    (void)user;
    if (!gun || gun->itemId != ITEM_DEBUGSMG || !hit->target || !hit->damage || *hit->damage <= 0) return 0;
    const BattleEntity *target = hit->target;
    const EnemyCombatant *enemy = target->core;
    if (enemy && enemy->hpAlive > *hit->damage) *hit->damage = enemy->hpAlive;
    return 0;
}

static void pe_ask_win(void) {
    if (!pe_in_battle()) { pe_toast("Win this battle: not in a battle"); return; }
    s_pending_win = 1;
}
static void pe_ammo_changed(void) {
    s_ammo_action = NULL; s_ammo_gun = NULL;
    s_ammo_loaded = s_ammo_held = s_ammo_pool[0] = s_ammo_pool[1] = s_ammo_pool[2] = 0;
}
/* Fresh counts when the row changes (the toggle's per-frame callback sees the flip). */
static void pe_ammo_tick(void) {
    static int last;
    if (s_ammo != last) { last = s_ammo; pe_ammo_changed(); }
}

static int cheats_init(void) {
    pe_hook_add(PE_HOOK_FRAME, pe_on_frame, NULL, PE_PRIORITY_LATE, MOD_ID);
    pe_hook_add(PE_HOOK_BATTLE_UPDATE, pe_on_battle, NULL, PE_PRIORITY_LATE, MOD_ID);
    pe_hook_add(PE_HOOK_NEW_GAME, pe_on_new_game, NULL, PE_PRIORITY_NORMAL, MOD_ID);
    pe_hook_add(PE_HOOK_ENEMY_DAMAGE, pe_on_enemy_damage, NULL, PE_PRIORITY_LATE, MOD_ID);

    pe_menu_begin(PE_PAGE_CHEATS);
    pe_menu_top_page(PE_PAGE_CHEATS, "God mode, infinite ammo, instant wins and Parasite Energy powers.", 50);
    pe_menu_register_toggle(0, "God mode", "Aya's HP stays at its maximum.", &s_god, NULL);
    pe_menu_register_toggle(0, "Infinite ammo and PE", "Rounds and ammo never run out; Parasite Energy stays full.",
                            &s_ammo, pe_ammo_tick);
    pe_menu_register_action(0, "Win this battle", "Every enemy's HP to 0; the game's own victory and rewards follow.",
                            pe_ask_win);
    pe_menu_register_action(0, "Learn all PE powers", "Every power a level-up can teach.", pe_powers_learn);
    pe_menu_register_action(0, "Restore PE powers", "Back to the powers learned before Learn all PE powers.",
                            pe_powers_restore);
    pe_menu_register_toggle(0, "Test arsenal in imported games",
                            "Give the test arsenal whenever an imported game (RE play) starts a New Game.",
                            &s_arsenal_on_start, NULL);
    pe_menu_register_action(0, "Give test arsenal",
                            "For testing: the strongest weapon of each kind, and the DEBUGSMG equipped, whose hits kill.",
                            pe_arsenal);
    pe_menu_begin(0);
    return 0;
}

const PeMod pe_mod_pe_cheats = {MOD_ID, "Cheats", "0.1", NULL, cheats_init, NULL};
