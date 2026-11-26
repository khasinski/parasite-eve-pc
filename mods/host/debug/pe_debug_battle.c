/* Debug-control battle commands. See docs/DEBUG_CONTROL.md. */
#include "pe_debug_internal.h"
#include <stdlib.h>
#include <string.h>
#include "pe_game.h"
#include "pe1/battle_runtime.h"
#include "pe1/battle_start.h"
#include "pe1/inventory_state.h"

#define PE_ACTORS_MAX 64

void pe_debug_battle_write(FILE *f) {
    FieldActor *a;
    Combatant *p = g_PlayerEntity ? (Combatant *)g_PlayerEntity->state : NULL;
    ItemDataRecord *w = pe_equipped(0);
    int i, comma = 0, active = !!(g_GameStateFlags & 2);
    fprintf(f, "{\"active\":%s,\"frame\":%d,\"mode\":%d,\"target_index\":%d,"
            "\"queue_count\":%u,\"player\":", active ? "true" : "false", g_BattleFrameCounter,
            g_BattleModeState, g_BattleTargetIndex, g_BattleActionQueueCount);
    if (p) fprintf(f, "{\"hp\":%u,\"max_hp\":%u,\"atb\":%u,\"pe_raw\":%d,\"max_pe_raw\":%d,"
                  "\"status\":%u,\"bp\":%u,\"level\":%u}", p->curHP, p->maxHP, p->atb,
                  p->currentPe, p->maxPe, (unsigned)p->stateFlags, g_AyaSaveState.bonus_points,
                  g_AyaSaveState.level + 1);
    else fputs("null", f);
    fputs(",\"weapon\":", f);
    if (w) {
        fprintf(f, "{\"id\":%u,\"kind\":%u,\"ammo\":%u,\"base\":[%u,%u,%u],\"bonus\":[%d,%d,%d],\"mods\":[",
                w->itemId, w->kind, w->ammo, w->baseStats[0], w->baseStats[1], w->baseStats[2],
                w->bonusStats[0], w->bonusStats[1], w->bonusStats[2]);
        for (i = 0; i < w->tailCount && i < 10; ++i) fprintf(f, "%s%u", i ? "," : "", w->tailData[i]);
        fputs("]}", f);
    } else fputs("null", f);
    fputs(",\"enemies\":[", f);
    /* Only battle slots are enemy cores. Room NPCs have different state layouts. */
    for (a = g_FieldActorListHead, i = 0; active && a && i < PE_ACTORS_MAX; a = a->next, ++i) {
        int slot;
        EnemyCombatant *e = NULL;
        for (slot = 0; slot < 7; ++slot)
            if (g_BattleEnemySlots[slot].active && (void *)a->state == &g_BattleEnemySlots[slot].combatant)
                e = &g_BattleEnemySlots[slot].combatant;
        if (!e || a == g_PlayerEntity) continue;
        fprintf(f, "%s{\"actor\":%d,\"type\":%u,\"sub\":%u,\"id\":%u,\"hp\":%d,\"max_hp\":%d,"
                "\"status\":%u,\"status2\":%u,\"position\":[%d,%d,%d],\"steal_item\":%u}",
                comma++ ? "," : "", i, a->type_id, a->sub_id, (unsigned)(u16)e->field04.fieldId04,
                e->hp, e->maxHp, e->stateFlags, e->statusFlags2, a->pos_x, a->pos_y, a->pos_z, e->stealItemId);
    }
    fputs("]}", f);
}

static void pe_ctl_battle_state(const char *args) {
    char path[1100];
    FILE *f;
    (void)args;
    pe_debug_path(path, sizeof path, "out");
    if (!(f = fopen(path, "a"))) return;
    fputs("ok battle-state ", f);
    pe_debug_battle_write(f);
    fputc('\n', f);
    fclose(f);
}


static const PeDebugCommand s_commands[] = {
    {"battle-state", PE_DEBUG_EXACT, pe_ctl_battle_state},
};

const PeDebugModule pe_debug_battle_module = {
    "battle", s_commands, sizeof s_commands / sizeof *s_commands, NULL, NULL
};
