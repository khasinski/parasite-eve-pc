/* Debug-control setup commands. See docs/DEBUG_CONTROL.md. */
#include "pe_debug_internal.h"
#include <stdlib.h>
#include <string.h>
#include "pe_game.h"
#include "pe1/battle_modifiers.h"
#include "pe1/battle_saved_state.h"
#include "pe1/inventory_state.h"
#include "pe1/inventory_queries.h"


static void pe_ctl_bag(const char *args) {
    (void)args;
    char list[50 * 6 + 1];
    int i, n = Inv_GetAyaSlotLimit(), at = 0;
    list[0] = 0;
    for (i = 0; i < n && i < 50; i++)
        at += snprintf(list + at, sizeof list - at, "%s%X", i ? "," : "", (unsigned)(uint16_t)g_AyaInventoryItems[i]);
    pe_debug_reply("ok %s %s", "bag", list);
}

static void pe_ctl_fill_bag(const char *args) {
    char detail[32];
    unsigned id = 0, leave = 0;
    int i, n = Inv_GetAyaSlotLimit(), empty = 0, filled = 0;
    if (sscanf(args, "%x %u", &id, &leave) < 1 || !id || id > 0xFFFF) {
        pe_debug_reply("err %s %s", "fill-bag", "fill-bag ID [LEAVE]");
        return;
    }
    for (i = 0; i < n && i < 50; i++) empty += g_AyaInventoryItems[i] == 0;
    for (i = 0; i < n && i < 50 && empty > (int)leave; i++)
        if (!g_AyaInventoryItems[i]) { g_AyaInventoryItems[i] = (s16)id; --empty; ++filled; }
    snprintf(detail, sizeof detail, "%d", filled);
    pe_debug_reply("ok %s %s", "fill-bag", detail);
}

static void pe_ctl_give(const char *args) {
    unsigned id;
    char extra, detail[32];
    if (sscanf(args, "%x %c", &id, &extra) != 1 || !id || id > 255 || !pe_base_item(id)) {
        pe_debug_reply("err %s %s", "give", "give ITEM (hex base item id 1..FF)"); return;
    }
    if (!pe_free_field()) { pe_debug_reply("err %s %s", "give", "not in free field"); return; }
    Inv_SelectActiveList(0);
    if (Inv_AddItem(id) != 0) { pe_debug_reply("err %s %s", "give", "inventory rejected item"); return; }
    snprintf(detail, sizeof detail, "%X", id);
    pe_debug_reply("ok %s %s", "give", detail);
}

static void pe_ctl_equip(const char *args) {
    int slot, value;
    char extra, detail[40];
    ItemDataRecord *r;
    if (sscanf(args, "%d %c", &slot, &extra) != 1 || slot < 0 || slot >= Inv_GetAyaSlotLimit() || slot >= 50) {
        pe_debug_reply("err %s %s", "equip", "equip BAG_SLOT (decimal)"); return;
    }
    if (!pe_free_field()) { pe_debug_reply("err %s %s", "equip", "not in free field"); return; }
    value = g_AyaInventoryItems[slot];
    if (value < 0x100 || value >= 0x180 || !(r = &g_InventoryRuntime.equipment[value - 0x100])->iconId || r->kind < 1 || r->kind > 9) {
        pe_debug_reply("err %s %s", "equip", "slot does not hold equipment"); return;
    }
    if (r->kind == 9) g_AyaSaveState.equipped_armor_slot = slot;
    else g_AyaSaveState.equipped_weapon_slot = slot;
    BattleCmd_LoadWeaponModifiers();
    Inv_RecalcSlotStats();
    Inv_BuildWeaponList(0, &g_SavedBattleStateTail.weapon);
    Inv_BuildArmorList(&g_BattleEquipStateBlock);
    snprintf(detail, sizeof detail, "slot=%d id=%X", slot, r->itemId);
    pe_debug_reply("ok %s %s", "equip", detail);
}

static void pe_ctl_stat(const char *args) {
    char name[24], extra, detail[64];
    int value;
    Combatant *p = g_PlayerEntity ? (Combatant *)g_PlayerEntity->state : NULL;
    if (!pe_free_field() || !p) { pe_debug_reply("err %s %s", "set-stat", "not in free field"); return; }
    if (sscanf(args, "%23s %d %c", name, &value, &extra) != 2 || value < 0) goto invalid;
    if (!strcmp(name, "bp") && value <= 99999) g_AyaSaveState.bonus_points = value;
    else if (!strcmp(name, "hp") && value <= p->maxHP) {
        p->curHP = value; g_AyaSaveState.current_hp = value;
    } else if (!strcmp(name, "pe-raw") && value <= p->maxPe) p->currentPe = value;
    else if (!strcmp(name, "level") && value >= 1 && value <= 99) {
        const u32 *levels = Aya_GetLevelExpTable();
        if (!levels) goto invalid;
        g_AyaSaveState.level = value - 1;
        g_AyaSaveState.total_exp = levels[value - 1];
        Inv_RecalcSlotStats();
    } else goto invalid;
    snprintf(detail, sizeof detail, "%s=%d", name, value);
    pe_debug_reply("ok %s %s", "set-stat", detail); return;
invalid:
    pe_debug_reply("err %s %s", "set-stat", "set-stat level|hp|pe-raw|bp VALUE (decimal, within limits)");
}


/* item ID: a base item's kind and stats (hex id 1..FF), and the pools' rounds (records 0..8). */
static void pe_ctl_item(const char *args) {
    unsigned id;
    char extra, detail[160];
    const ItemDataRecord *r;
    if (sscanf(args, "%x %c", &id, &extra) != 1 || !(r = pe_base_item(id))) {
        pe_debug_reply("err %s %s", "item", "item ID (hex base item id 1..FF)"); return;
    }
    snprintf(detail, sizeof detail, "id=%X kind=%u stats=%u,%u,%u ammo=%u", id, (unsigned)r->kind,
             (unsigned)r->baseStats[0], (unsigned)r->baseStats[1], (unsigned)r->baseStats[2], (unsigned)r->ammo);
    pe_debug_reply("ok %s %s", "item", detail);
}

/* pools: the rounds in each of the nine ammo pool records, and the equipped weapon's loaded rounds. */
static void pe_ctl_pools(const char *args) {
    char detail[160];
    const ItemDataRecord *gun = pe_equipped(0);
    int at = 0;
    (void)args;
    for (int i = 0; i < 9; ++i)
        at += snprintf(detail + at, sizeof detail - (size_t)at, "%s%u", i ? "," : "", (unsigned)g_KeyItemRecords[i].ammo);
    snprintf(detail + at, sizeof detail - (size_t)at, " gun=%X kind=%u loaded=%u", gun ? gun->itemId : 0,
             gun ? (unsigned)gun->kind : 0, gun ? (unsigned)gun->ammo : 0);
    pe_debug_reply("ok %s %s", "pools", detail);
}

static const PeDebugCommand s_commands[] = {
    {"bag", PE_DEBUG_EXACT, pe_ctl_bag},
    {"fill-bag", PE_DEBUG_ARGS, pe_ctl_fill_bag},
    {"give", PE_DEBUG_ARGS, pe_ctl_give},
    {"equip", PE_DEBUG_ARGS, pe_ctl_equip},
    {"set-stat", PE_DEBUG_ARGS, pe_ctl_stat},
    {"item", PE_DEBUG_ARGS, pe_ctl_item},
    {"pools", PE_DEBUG_EXACT, pe_ctl_pools},
};

const PeDebugModule pe_debug_setup_module = {
    "setup", s_commands, sizeof s_commands / sizeof *s_commands, NULL, NULL
};
