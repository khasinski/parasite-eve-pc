#ifndef PE1_PE_COST_H
#define PE1_PE_COST_H

#include "common.h"
#include "pe_hooks.h"
#include "pe1/aya.h"
#include "pe1/battle_cmd.h"
#include "pe1/inventory_slots.h"

/* Shared by the native menu entry point and its isolated cost fixture. */
static inline int Inv_ParasitePowerCost(int spell, int available) {
    int remaining, cost, i;
    ItemDataRecord *armor;
    ParasiteSpellEntry *table;
    if (spell == 6 || spell == 19) {
        return available;
    }
    if (spell == 5) {
        BattleCmd_GetRemainingAmmo(&remaining);
        return remaining / 3;
    }
    table = Aya_GetParasiteSpellUnlockTable();
    cost = table[spell].cost;
    PE_HOOK(PE_HOOK_POWER_COST, (&(PeHookPowerCost){spell, &cost}));
    int armorSlot = g_InventoryRuntime.tracked[2];
    armor = NULL;
    if (armorSlot >= 0 && armorSlot < g_InvSlotLimit)
        armor = Inv_LookupItemRecordScaled(g_InvItemPtr[armorSlot]);
    if (armor != 0) {
        for (i = 0; i < armor->tailCount; i++) {
            if (armor->tailData[i] == 0x0E) {
                break;
            }
        }
        if (i < armor->tailCount) {
            cost = cost * 2 / 3;
        }
    }
    return cost;
}

#endif
