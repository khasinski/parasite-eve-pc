#include "../../pe_re_rules.h"
#include "pe_game.h"
#include "pe_mod_host.h"
#include "pe1/aya.h"
#include "pe1/battle_modifiers.h"
#include "pe1/battle_saved_state.h"
#include "pe1/inventory_slots.h"
#include "pe1/item_ids.h"
#include "pe1/weapon_list_output.h"
#include <string.h>

typedef struct {
    const char *game;
    uint16_t item, ammo_item;
    unsigned base;
} ReWeapon;

static const ReWeapon weapons[] = {{"re2", 0x07, 0x15, ITEM_M870}};
static PeReProgress *progress;
static const char *game_id;
static int mounted[PE_RE_OWNED_WEAPONS]; /* native inventory value, 0 when unmounted */
static int native_weapon;

static const ReWeapon *lookup(const char *game, uint16_t item) {
    for (unsigned i = 0; i < sizeof weapons / sizeof *weapons; i++)
        if (weapons[i].item == item && !strcmp(weapons[i].game, game)) return &weapons[i];
    return NULL;
}

static int selected(void) {
    const int slot = g_AyaSaveState.equipped_weapon_slot;
    return slot >= 0 && slot < 50 ? g_AyaInventoryItems[slot] : 0;
}

static void equip(int value) {
    for (int i = 0; value && i < Inv_GetAyaSlotLimit() && i < 50; i++) {
        if (g_AyaInventoryItems[i] != value) continue;
        g_AyaSaveState.equipped_weapon_slot = i;
        BattleCmd_LoadWeaponModifiers();
        Inv_RecalcSlotStats();
        Inv_BuildWeaponList(0, &g_SavedBattleStateTail.weapon);
        return;
    }
}

static int mount(unsigned i, const ReWeapon *rule, unsigned loaded) {
    const int override = Inv_IsActiveListOverrideSelected();
    Inv_SelectActiveList(0);
    ItemDataRecord *record = Inv_FindSlotByIndex(rule->base);
    Inv_SelectActiveList(override);
    if (!record) return 0;
    record->ammo = (u16)loaded;
    return mounted[i] = 0x100 + (int)(record - g_InventoryRuntime.equipment);
}

static void sync(void) {
    if (!progress) return;
    const int value = selected();
    int chosen = 0;
    for (unsigned i = 0; i < PE_RE_OWNED_WEAPONS; i++) {
        if (!mounted[i]) continue;
        progress->weapons[i].loaded = g_InventoryRuntime.equipment[mounted[i] - 0x100].ammo;
        if (mounted[i] == value) chosen = progress->weapons[i].item;
    }
    if (chosen || value) progress->equipped_weapon = (uint16_t)chosen;
    if (!chosen && value) native_weapon = value;
}

static void unmount(void) {
    sync();
    const int restore_native = progress->equipped_weapon != 0;
    for (unsigned i = 0; i < PE_RE_OWNED_WEAPONS; i++) {
        const int value = mounted[i];
        if (!value) continue;
        for (int j = 0; j < 50; j++) if (g_AyaInventoryItems[j] == value) g_AyaInventoryItems[j] = 0;
        for (int j = 0; j < 100; j++) if (g_InventoryRuntime.storage[j] == value) g_InventoryRuntime.storage[j] = 0;
        for (int j = 0; j < 82; j++) if (g_InventoryRuntime.specialStorage[j] == value) g_InventoryRuntime.specialStorage[j] = 0;
        memset(&g_InventoryRuntime.equipment[value - 0x100], 0, sizeof(ItemDataRecord));
        mounted[i] = 0;
    }
    if (restore_native) equip(native_weapon);
}

void pe_re_weapon_unload(int inventory_replaced) {
    if (progress && !inventory_replaced) unmount();
    memset(mounted, 0, sizeof mounted);
    progress = NULL;
    game_id = NULL;
    native_weapon = 0;
}

static void restore(void) {
    for (unsigned i = 0; i < PE_RE_OWNED_WEAPONS; i++) {
        const PeReOwnedWeapon *saved = &progress->weapons[i];
        if (mounted[i] || !saved->item) continue;
        const ReWeapon *rule = lookup(game_id, saved->item);
        if (rule && mount(i, rule, saved->loaded) && progress->equipped_weapon == saved->item)
            equip(mounted[i]);
    }
}

void pe_re_weapon_loadout(const char *game, PeReProgress *state) {
    if (!progress) {
        progress = state;
        game_id = game;
        native_weapon = selected();
    } else {
        sync();
    }
    restore();
}

int pe_re_weapon_pickup(const char *game, uint16_t item, unsigned count) {
    const ReWeapon *rule = lookup(game, item);
    if (!rule) return 0;
    if (!progress || strcmp(game, game_id)) return -1;
    unsigned slot = PE_RE_OWNED_WEAPONS;
    for (unsigned i = 0; i < PE_RE_OWNED_WEAPONS; i++) {
        if (progress->weapons[i].item == item) return -1;
        if (!progress->weapons[i].item && slot == PE_RE_OWNED_WEAPONS) slot = i;
    }
    const ItemDataRecord *base = pe_base_item(rule->base);
    if (!base || count > (unsigned)(base->baseStats[2] + base->bonusStats[2]) ||
        slot == PE_RE_OWNED_WEAPONS || !mount(slot, rule, count)) return -1;
    progress->weapons[slot] = (PeReOwnedWeapon){item, (uint16_t)count, rule->ammo_item};
    return 1;
}

static int save_inventory(void *ctx, void *user) {
    (void)user;
    if (!progress) return 0;
    if (*(const int *)ctx) restore();
    else unmount();
    return 0;
}

static int is_mounted(int value) {
    if (value < 0x100 || value >= 0x180) return 0;
    for (unsigned i = 0; i < PE_RE_OWNED_WEAPONS; i++) if (mounted[i] == value) return 1;
    return 0;
}

static int guard_equipment(void *ctx, void *user) {
    (void)user;
    const PeHookEquipmentGuard *guard = ctx;
    if (guard->kind == 0 || guard->kind == 3) return is_mounted(guard->value);
    if (guard->kind == 1)
        for (unsigned i = 0; i < PE_RE_OWNED_WEAPONS; i++)
            if (mounted[i] && guard->record == &g_InventoryRuntime.equipment[mounted[i] - 0x100]) return 1;
    return 0;
}

void pe_re_weapon_hooks(void) {
    pe_hook_add(PE_HOOK_SAVE_INVENTORY, save_inventory, NULL, PE_PRIORITY_NORMAL, "pe_re_play");
    pe_hook_add(PE_HOOK_EQUIPMENT_GUARD, guard_equipment, NULL, PE_PRIORITY_NORMAL, "pe_re_play");
}
