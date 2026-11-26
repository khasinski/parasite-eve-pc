/* Debug-control trace commands. See docs/DEBUG_CONTROL.md. */
#include "pe_debug_internal.h"
#include <stdlib.h>
#include <string.h>
#include "pe_game.h"
#include "pe_mod_host.h"
#include "pe_hooks.h"
#include "pe1/battle_start.h"
#include "pe1/game_state.h"
#include "pe1/inventory_state.h"

static int s_trace;

static int pe_ctl_trace_event(void *ctx, void *user) {
    char path[1100];
    FILE *f;
    int kind = (int)(intptr_t)user;
    static int hp_before;
    static BattleEntity *hit;
    if (!s_trace) return 0;
    if (kind == PE_HOOK_ENEMY_HIT) {
        hit = ctx;
        hp_before = hit && hit->core ? ((EnemyCombatant *)hit->core)->hp : 0;
        return 0;
    }
    pe_debug_path(path, sizeof path, "battle.jsonl");
    if (!(f = fopen(path, "a"))) return 0;
    fprintf(f, "{\"frame\":%u,\"battle_frame\":%d,", pe_debug_frame(), g_BattleFrameCounter);
    if (kind == PE_HOOK_ENEMY_HIT_END && ctx == hit && hit && hit->core) {
        int hp = ((EnemyCombatant *)hit->core)->hp;
        EnemyCombatant *enemy = hit->core;
        unsigned reaction = (enemy->coreFlags >> 15) & 7;
        fprintf(f, "\"event\":\"enemy_hp_commit\",\"type\":%u,\"sub\":%u,\"before\":%d,\"after\":%d,\"loss\":%d,\"reaction\":%u,\"critical\":%s",
                ((FieldActor *)hit)->type_id, ((FieldActor *)hit)->sub_id, hp_before, hp, hp_before - hp,
                reaction, reaction == 3 && g_ActiveActor && !(g_ActiveActor->stateFlags & 0x180000) ? "true" : "false");
    } else if (kind == PE_HOOK_HP_LOSS) {
        PeHookHpLoss *q = ctx;
        /* Admission, not a commit: native callers can clamp or defer the loss. */
        fprintf(f, "\"event\":\"hp_loss_admitted\",\"before\":%d,\"loss\":%d,\"source\":%u",
                q->current_hp, *q->loss, q->source);
    } else if (kind == PE_HOOK_BATTLE_REWARDS) {
        PeHookRewards *q = ctx;
        int i;
        fprintf(f, "\"event\":\"rewards_offered\",\"exp\":%d,\"drops\":[", q->exp);
        for (i = 0; q->list && i < 10 && q->list[i].id; ++i)
            fprintf(f, "%s{\"id\":%d,\"extra\":%d}", i ? "," : "", q->list[i].id, q->list[i].extra);
        fputc(']', f);
    } else if (kind == PE_HOOK_STEAL_RESULT) {
        PeHookStealResult *q = ctx;
        fprintf(f, "\"event\":\"steal\",\"item\":%d,\"success\":%s", q->item, q->success ? "true" : "false");
    } else {
        fprintf(f, "\"event\":\"%s\"", kind == PE_HOOK_PLAYER_ACTION ? "player_action" : "spell");
    }
    fputs("}\n", f); fclose(f);
    return 0;
}

static void pe_ctl_trace_frame(void) {
    static unsigned bp, exp, level;
    static int was_battle, initialized;
    static int previous_hp;
    int active = !!(g_GameStateFlags & 2);
    Combatant *p = g_PlayerEntity ? (Combatant *)g_PlayerEntity->state : NULL;
    char path[1100];
    FILE *f;
    if (!s_trace) { initialized = 0; return; }
    if (initialized && active && was_battle && p && previous_hp != p->curHP) {
        pe_debug_path(path, sizeof path, "battle.jsonl");
        if ((f = fopen(path, "a"))) {
            fprintf(f, "{\"frame\":%u,\"event\":\"player_hp_changed\",\"before\":%d,\"after\":%u}\n",
                    pe_debug_frame(), previous_hp, p->curHP);
            fclose(f);
        }
    }
    previous_hp = p ? p->curHP : 0;
    if (!initialized || active != was_battle || bp != g_AyaSaveState.bonus_points ||
        exp != g_AyaSaveState.total_exp || level != g_AyaSaveState.level) {
        pe_debug_path(path, sizeof path, "battle.jsonl");
        if ((f = fopen(path, "a"))) {
            fprintf(f, "{\"frame\":%u,\"event\":\"resources\",\"battle\":%s,\"bp\":%u,\"exp\":%u,\"level\":%u}\n",
                    pe_debug_frame(), active ? "true" : "false", g_AyaSaveState.bonus_points,
                    g_AyaSaveState.total_exp, g_AyaSaveState.level + 1);
            fclose(f);
        }
        bp = g_AyaSaveState.bonus_points; exp = g_AyaSaveState.total_exp;
        level = g_AyaSaveState.level; was_battle = active; initialized = 1;
    }
}

static void pe_ctl_trace_on(const char *args) {
    (void)args;
    s_trace = 1;
    pe_debug_reply("ok %s %s", "trace", "on");
}
static void pe_ctl_trace_off(const char *args) {
    (void)args;
    s_trace = 0;
    pe_debug_reply("ok %s %s", "trace", "off");
}
static void pe_ctl_trace_install(void) {
    static const int events[] = {PE_HOOK_ENEMY_HIT, PE_HOOK_ENEMY_HIT_END, PE_HOOK_HP_LOSS,
        PE_HOOK_BATTLE_REWARDS, PE_HOOK_STEAL_RESULT, PE_HOOK_PLAYER_ACTION, PE_HOOK_SPELL};
    unsigned i;
    for (i = 0; i < sizeof events / sizeof *events; ++i)
        pe_hook_add(events[i], pe_ctl_trace_event, (void *)(intptr_t)events[i], 10000, "host");
}

static const PeDebugCommand s_commands[] = {
    {"trace on", PE_DEBUG_EXACT, pe_ctl_trace_on},
    {"trace off", PE_DEBUG_EXACT, pe_ctl_trace_off},
};

const PeDebugModule pe_debug_trace_module = {
    "trace", s_commands, sizeof s_commands / sizeof *s_commands, pe_ctl_trace_install, pe_ctl_trace_frame
};
