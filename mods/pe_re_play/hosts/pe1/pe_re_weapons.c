#include "../../pe_re_rules.h"
#include "../../re_host.h"
#include "pe_re_stage.h"
#include "pe_game.h"
#include "pe_mod_host.h"
#include "re_play_api.h"
#include "ammo_types_api.h"
#include "pe_mod_api.h"
#include "pe1/aya.h"
#include "pe1/battle_modifiers.h"
#include "pe1/battle_saved_state.h"
#include "pe1/inventory_slots.h"
#include "pe1/item_ids.h"
#include "pe1/weapon_list_output.h"
#include <stdio.h>
#include <string.h>

/* Rounds a weapon can load (RE2's launcher): the source ammo item, its ammo type and the native PE
 * weapon effect the loaded rounds add (0 none, 9 Heat Bullets, 12 Acid Bullets). The loaded kind is
 * the owned weapon's saved ammo_item. */
typedef struct {
    uint16_t item;
    const char *type;
    uint8_t effect;
} ReRound;
static const ReRound k_re2_launcher_rounds[] = {
    {0x18, "re2.grenade", 0}, {0x19, "re2.flame", 9}, {0x1A, "re2.acid", 12},
};

typedef struct {
    const char *game;
    uint16_t item, ammo_item;
    unsigned base;
    const char *ammo_type;
    int role;
    const ReRound *rounds;
    unsigned nrounds;
} ReWeapon;

static const ReWeapon weapons[] = {
    {"re2", 0x05, 0x16, ITEM_SAR, NULL, PE_RE_WEAPON_DEFAULT, NULL, 0},
    {"re2", 0x07, 0x15, ITEM_M870, NULL, PE_RE_WEAPON_CLOSE_SHOTGUN, NULL, 0},
    {"re2", 0x09, 0x18, ITEM_M203_5, NULL, PE_RE_WEAPON_GRENADE_SPLASH, k_re2_launcher_rounds,
     sizeof k_re2_launcher_rounds / sizeof *k_re2_launcher_rounds},
    {"re2", 0x0C, 0x1D, ITEM_MP5K, "re2.bolts", PE_RE_WEAPON_DEFAULT, NULL, 0},
    {"re2", 0x0E, 0x1C, ITEM_MP5A5, "re2.spark", PE_RE_WEAPON_SPARK_BEAM, NULL, 0},
    {"re2", 0x0F, 0x1B, ITEM_MP5K, NULL, PE_RE_WEAPON_DEFAULT, NULL, 0},
};
static PeReProgress *progress;
static const char *game_id;
static int mounted[PE_RE_OWNED_WEAPONS]; /* native inventory value, 0 when unmounted */
static int native_weapon;

static const ReWeapon *lookup(const char *game, uint16_t item) {
    for (unsigned i = 0; i < sizeof weapons / sizeof *weapons; i++)
        if (weapons[i].item == item && !strcmp(weapons[i].game, game)) return &weapons[i];
    return NULL;
}

static int mounted_index(const void *weapon) {
    if (!progress) return -1;
    for (unsigned i = 0; i < PE_RE_OWNED_WEAPONS; i++)
        if (mounted[i] && weapon == &g_InventoryRuntime.equipment[mounted[i] - 0x100]) return (int)i;
    return -1;
}

static const ReWeapon *mounted_rule(const void *weapon) {
    const int i = mounted_index(weapon);
    return i < 0 ? NULL : lookup(game_id, progress->weapons[i].item);
}

static const ReRound *loaded_round(const ReWeapon *rule, unsigned i) {
    for (unsigned k = 0; rule && k < rule->nrounds; k++)
        if (rule->rounds[k].item == progress->weapons[i].ammo_item) return &rule->rounds[k];
    return rule && rule->nrounds ? &rule->rounds[0] : NULL;
}

const char *pe_re_weapon_ammo_type(const ItemDataRecord *weapon) {
    const int i = mounted_index(weapon);
    const ReWeapon *rule = i < 0 ? NULL : lookup(game_id, progress->weapons[i].item);
    if (rule && rule->nrounds) return loaded_round(rule, (unsigned)i)->type;
    return rule ? rule->ammo_type : NULL;
}

/* The mounted record's effects: its PE base's own, plus the loaded rounds' effect. Rebuilt from
 * the base, so changing rounds never stacks effects. */
static void apply_round_effect(unsigned i, const ReWeapon *rule) {
    if (!mounted[i] || !rule->nrounds) return;
    ItemDataRecord *record = &g_InventoryRuntime.equipment[mounted[i] - 0x100];
    const ItemDataRecord *base = pe_base_item(rule->base);
    if (!base) return;
    record->tailCount = base->tailCount;
    memcpy(record->tailData, base->tailData, sizeof record->tailData);
    const uint8_t effect = loaded_round(rule, i)->effect;
    if (!effect) return;
    for (unsigned k = 0; k < record->tailCount && k < 10; k++)
        if ((record->tailData[k] & 31) == effect) return;
    if (record->tailCount < 10) record->tailData[record->tailCount++] = effect;
    else record->tailData[9] = effect;
}

int pe_re_weapon_role(const void *weapon) {
    const ReWeapon *rule = mounted_rule(weapon);
    return rule ? rule->role : PE_RE_WEAPON_DEFAULT;
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
    mounted[i] = 0x100 + (int)(record - g_InventoryRuntime.equipment);
    if (progress && progress->weapons[i].item == rule->item) apply_round_effect(i, rule);
    return mounted[i];
}

static void remove_mounted(unsigned i) {
    const int value = mounted[i];
    if (!value) return;
    for (int j = 0; j < 50; j++) if (g_AyaInventoryItems[j] == value) g_AyaInventoryItems[j] = 0;
    for (int j = 0; j < 100; j++) if (g_InventoryRuntime.storage[j] == value) g_InventoryRuntime.storage[j] = 0;
    for (int j = 0; j < 82; j++) if (g_InventoryRuntime.specialStorage[j] == value) g_InventoryRuntime.specialStorage[j] = 0;
    memset(&g_InventoryRuntime.equipment[value - 0x100], 0, sizeof(ItemDataRecord));
    mounted[i] = 0;
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
    for (unsigned i = 0; i < PE_RE_OWNED_WEAPONS; i++) remove_mounted(i);
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
    if (!base || slot == PE_RE_OWNED_WEAPONS) return -1;
    const int capacity = base->baseStats[2] + base->bonusStats[2];
    if (capacity <= 0) return -1;
    const unsigned loaded = count < (unsigned)capacity ? count : (unsigned)capacity;
    if (!mount(slot, rule, loaded)) return -1;
    if (count > loaded && pe_re_ammo_pickup(game, rule->ammo_item, count - loaded) != 1) {
        remove_mounted(slot);
        return -1;
    }
    progress->weapons[slot] = (PeReOwnedWeapon){item, (uint16_t)loaded, rule->ammo_item};
    apply_round_effect(slot, rule);
    return 1;
}

/* The ammo service when it holds per-type reserves (F1 > Ammo on): rounds can be counted and taken. */
static const PeAmmoTypesApi *typed_ammo(const ReWeapon *rule) {
    const PeAmmoTypesApi *api = pe_service_get(PE_AMMO_TYPES_SERVICE, PE_AMMO_TYPES_SERVICE_VERSION);
    const int ok = api && api->struct_size >= offsetof(PeAmmoTypesApi, take) + sizeof api->take && api->take &&
                   api->rounds && api->add && api->rounds(rule->rounds[0].type) >= 0;
    return ok ? api : NULL;
}

/* The owned, carried weapon that loads several kinds of rounds (RE2's launcher), or -1. */
static int round_weapon(void) {
    for (unsigned k = 0; progress && k < PE_RE_OWNED_WEAPONS; k++) {
        const ReWeapon *rule = mounted[k] ? lookup(game_id, progress->weapons[k].item) : NULL;
        if (rule && rule->nrounds > 1) return (int)k;
    }
    return -1;
}

/* Load kind `next` into owned weapon i. The loaded rounds go back to their own reserve first. With
 * per-type ammo off every kind shares Bullets, so only the kind and its effect change. */
static int load_kind(unsigned i, const ReWeapon *rule, const ReRound *next) {
    ItemDataRecord *record = &g_InventoryRuntime.equipment[mounted[i] - 0x100];
    const PeAmmoTypesApi *api = typed_ammo(rule);
    const ReRound *now = loaded_round(rule, i);
    if (now == next) return 0;
    if (api) {
        const int capacity = record->baseStats[2] + record->bonusStats[2];
        if (api->rounds(next->type) <= 0) return 0;
        const int back = record->ammo ? api->add(now->type, record->ammo) : 0;
        record->ammo = (u16)(record->ammo - (back > 0 ? back : 0));
        if (record->ammo) return 0;   /* the old kind's reserve is full: keep it loaded */
        record->ammo = (u16)api->take(next->type, capacity > 0 ? capacity : 0);
    }
    progress->weapons[i].ammo_item = next->item;
    progress->weapons[i].loaded = record->ammo;
    apply_round_effect(i, rule);
    Inv_RecalcSlotStats();
    if (mounted[i] == selected()) Inv_BuildWeaponList(0, &g_SavedBattleStateTail.weapon);
    pe_mod_log("pe_re_play", "launcher: loaded %u rounds of %s (effect %u)", (unsigned)record->ammo, next->type,
               (unsigned)next->effect);
    return 1;
}

/* The status screen: the carried launcher's kinds of rounds, those with rounds and the loaded one. */
int pe_re_weapon_round_entries(const char *game, ReRoundEntry *out, int max) {
    const int i = round_weapon();
    if (i < 0 || !game || !game_id || strcmp(game, game_id)) return 0;
    const ReWeapon *rule = lookup(game_id, progress->weapons[i].item);
    const PeAmmoTypesApi *api = typed_ammo(rule);
    const ReRound *now = loaded_round(rule, (unsigned)i);
    int n = 0;
    for (unsigned k = 0; k < rule->nrounds && n < max; k++) {
        const ReRound *r = &rule->rounds[k];
        const int count = api ? api->rounds(r->type) : (int)g_KeyItemRecords[0].ammo;
        if (r != now && count <= 0) continue;
        out[n++] = (ReRoundEntry){r->item, (uint16_t)(r == now), (unsigned)(count > 0 ? count : 0)};
    }
    return n;
}

/* The status screen's Use on rounds: load that kind into the carried launcher. */
int pe_re_weapon_load_round(const char *game, uint16_t item, char *say, size_t n) {
    const int i = round_weapon();
    if (i < 0 || !game || !game_id || strcmp(game, game_id)) {
        snprintf(say, n, "There's no weapon to load them into.");
        return 0;
    }
    const ReWeapon *rule = lookup(game_id, progress->weapons[i].item);
    const ReRound *r = NULL;
    for (unsigned k = 0; k < rule->nrounds; k++)
        if (rule->rounds[k].item == item) r = &rule->rounds[k];
    if (!r) { snprintf(say, n, "Those don't fit the Grenade Launcher."); return 0; }
    if (r == loaded_round(rule, (unsigned)i)) { snprintf(say, n, "The Grenade Launcher already has those loaded."); return 0; }
    const PeAmmoTypesApi *api = typed_ammo(rule);
    if (api && api->rounds(r->type) <= 0) { snprintf(say, n, "You have none of those."); return 0; }
    if (!load_kind((unsigned)i, rule, r)) { snprintf(say, n, "There's no room for the rounds in the Grenade Launcher now."); return 0; }
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
