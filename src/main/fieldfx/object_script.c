#include "common.h"
#include "pe1/field_actor.h"
#include "pe1/fieldfx_state.h"
#include "pe1/fieldfx_task_pool.h"
#include "pe1/psyq_libc.h"

/* Field engine objects: clearing an object's script state and dispatch
 * slots, and the two per-frame passes over the 0x40 dispatch slots. */

FieldFxState *g_FieldFxScriptState;
FieldFxObject *g_FieldFxScriptObject;
char *g_FieldFxScriptData;
int *g_FieldFxCommandTable;
FieldFxSlot *g_FieldFxSlotTable;
char g_FieldFxServiceThreadMissingMsg[] = "error : service thread not found\n";
char g_FieldFxUpdateThreadMissingMsg[] = "error : update thread not found\n";

void **FieldFx_GetSlot(FieldFxObject *obj) {
    u8 *p;
    unsigned int i;

    p = (u8 *)&obj->state;
    do {
        *p++ = 0;
    } while (p < (u8 *)&obj->data[FIELDFX_DATA_SIZE]);

    obj->prefix.owner_kind = 0;
    obj->prefix.live_slots = 0;

    g_FieldFxScriptState = &obj->state;
    g_FieldFxSlotTable = obj->slots;
    g_FieldFxScriptObject = obj;
    g_FieldFxScriptData = obj->data;

    obj->state.wait = 0;
    obj->state.pc = 0;
    obj->state.data_next = 0;

    i = 0;
    do {
        g_FieldFxSlotTable[i].handler_id = 0;
        g_FieldFxSlotTable[i].flag = 0;
        g_FieldFxSlotTable[i].counter = 0;
        g_FieldFxSlotTable[i].data_offset = 0;
        i++;
    } while (i < 0x40);

    if (FieldFx_GetStatus(obj) == 3) {
        FieldActorState *inner = obj->prefix.actor->state;

        inner->core_flags = (inner->core_flags & 0xC0FFFFFF) | 0x01000000;
    }

    return (void **)&g_FieldFxScriptState->commands;
}

int FieldFx_Register(FieldFxObject *obj, FieldFxHandler *handlers)
{
  FieldFxObject *obj_s0 = obj;
  FieldFxHandler *handlers_s2 = handlers;
  s16 i;
  int *table;
  i = 0;
  table = obj_s0->state.commands;
  g_FieldFxScriptState = &obj_s0->state;
  g_FieldFxSlotTable = obj_s0->slots;
  g_FieldFxScriptData = obj_s0->data;
  g_FieldFxCommandTable = table;
  for (; i < 0x40; i++)
  {
    FieldFxSlotAddress entry;
    entry.slot = g_FieldFxSlotTable;
    entry.word = i * sizeof(FieldFxSlot) + entry.word;
    if (entry.slot->flag == 1)
    {
      FieldFxHandler handler = handlers_s2[entry.slot->handler_id];
      if (handler != ((void *) (-1)))
      {
        handler(obj, entry.slot, g_FieldFxScriptData + entry.slot->data_offset);
      }
      else
      {
        printf("%s", g_FieldFxServiceThreadMissingMsg);
      }
    }
  }

  return 0;
}

int FieldFx_UpdateSlots(FieldFxObject *obj, FieldFxHandler *handlers) {
    int result;
    s16 i;
    FieldActor *data;
    int *table;

    result = 0;
    table = obj->state.commands;
    data = obj->prefix.actor;

    g_FieldFxScriptState = &obj->state;
    g_FieldFxSlotTable = obj->slots;
    g_FieldFxScriptObject = obj;
    g_FieldFxScriptData = obj->data;
    g_FieldFxCommandTable = table;

    obj->prefix.live_slots = obj->state.slot_count;

    if (FieldFx_GetStatus(obj) == 3) {
        if (obj->prefix.asset_type != 0x24) {
            u8 *state = obj->prefix.actor->state->action_state;

            /* A completed animation clears its active event while the
             * effect may still have live slots. No event means no cue. */
            if (state && state[0] == 1) {
                state[0] = 2;
            }
        }
    }

    for (i = 0; i < 0x40; i++) {
        FieldFxSlotAddress entry;

        entry.slot = g_FieldFxSlotTable;
        entry.word = i * sizeof(FieldFxSlot) + entry.word;
        if (entry.slot->flag == 1) {
            FieldFxHandler handler = handlers[entry.slot->handler_id];

            if (handler != (void *)-1) {
                handler(obj, entry.slot, g_FieldFxScriptData + entry.slot->data_offset);
            } else {
                printf("%s", g_FieldFxUpdateThreadMissingMsg);
            }
            g_FieldFxSlotTable[i].counter += 1;
        }

        if (g_FieldFxSlotTable[i].flag == 2) {
            result |= FieldFx_FreeSlot((u16)i);
        }
    }

    if (FieldFx_GetStatus(obj) == 3) {
        FieldActorState *inner = data->state;
        u32 flags = inner->core_flags;
        if (flags & 0x180E) {
            const u8 *gate;
            /* Original byte at 0x1C + selector*0x10, through native fields. */
            switch ((flags >> 21) & 7) {
            case 0: gate = (const u8 *)&inner->count_limit; break;
            case 1: gate = (const u8 *)&inner->progress_step; break;
            case 2: gate = &inner->transition_timers[0]; break;
            case 3: gate = (const u8 *)&inner->flags; break;
            case 4: gate = &inner->number_desc_1[4]; break;
            case 5: gate = (const u8 *)&inner->substate; break;
            case 6: gate = &inner->reserved74[8]; break;
            default: gate = (const u8 *)&inner->script_value8c; break;
            }
            if (*gate == 0) result = -1;
        }
    }

    return result;
}

#define SCRIPT g_FieldFxScriptState

/* Runs the field script of obj until a wait or end command. */
int FieldFx_RunScript(FieldFxObject *obj, FieldFxHandler *handlers, u16 *sizes) {
    u8 stopped = 0;
    int result = 0;
    s16 wait;

    g_FieldFxScriptState = &obj->state;
    g_FieldFxSlotTable = obj->slots;
    g_FieldFxScriptObject = obj;
    g_FieldFxScriptData = obj->data;
    g_FieldFxCommandTable = obj->state.commands;

    wait = obj->state.wait;
    if (wait == 0) {
        do {
            int command = g_FieldFxCommandTable[SCRIPT->pc];

            if (command == -3) {
                result |= FieldFx_FreeAllSlots();
            }
            if (command == -2) {
                command = 0;
            }
            if (command != -1) {
                int high = (unsigned int)command >> 16;
                u16 op = high;
                    s16 operand = command;
                u8 index;

                if (op == 1) {
                    FieldFx_StartSlot(obj, (u8)operand, sizes, handlers);
                }
                if (op == 2) {
                    stopped = 1;
                    SCRIPT->wait = operand;
                }
                if ((unsigned int)(high - 0x10) < 0x10) {
                    index = op - 0x10;
                    SCRIPT->regs[index] = (s16)command;
                }
                if ((unsigned int)(high - 0x20) < 0x10) {
                    index = op - 0x20;
                    SCRIPT->regs[index] += (s16)command;
                }
                if ((unsigned int)(high - 0x30) < 0x10) {
                    index = op - 0x30;
                    SCRIPT->regs[index] -= (s16)command;
                }
                if ((unsigned int)(high - 0x40) < 0x10) {
                    index = op - 0x40;
                    SCRIPT->regs[index] = SCRIPT->vars[(u16)operand];
                }
                if ((unsigned int)(high - 0x50) < 0x10) {
                    index = op - 0x50;
                    SCRIPT->vars[(u16)operand] = SCRIPT->regs[index];
                }
                if ((unsigned int)(high - 0x1000) < 0x1000) {
                    s8 jump = op;

                    if (SCRIPT->regs[(high & 0xF00) >> 8] == (s16)command) {
                        SCRIPT->pc += jump;
                    }
                }
                if ((unsigned int)(op - 0x2000) < 0x1000) {
                    s8 jump = op;

                    if (SCRIPT->regs[(op & 0xF00) >> 8] != operand) {
                        SCRIPT->pc += jump;
                    }
                }
                if ((unsigned int)(op - 0x3000) < 0x1000) {
                    SCRIPT->pc += (s8)op;
                }
            } else {
                SCRIPT->halted = 1;
                stopped = 1;
            }

            if (SCRIPT->halted == 0) {
                SCRIPT->pc++;
            } else if (SCRIPT->slot_count == 0) {
                result = -1;
            }
        } while (!stopped);
    } else {
        obj->state.wait = wait - 1;
    }

    return result;
}

int FieldFx_Spawn6(intptr_t owner, int unused, int index, intptr_t value, int unused4, int unused5) {
    FieldFxObject *obj = (FieldFxObject *)owner;
    if (!obj || index < 0 || index >= 9) return -1;
    FieldFxState *state = &obj->state;

    g_FieldFxScriptState = state;
    state->vars[index] = value;
    return 0;
}

int *FieldFx_RegPtr(int index) {
    return &g_FieldFxScriptState->regs[index];
}

intptr_t *FieldFx_VarPtr(int index) {
    return &g_FieldFxScriptState->vars[index];
}

void FieldFx_SetScriptContext(void *context) {
    g_FieldFxScriptState->current_context = context;
}

void *FieldFx_GetScriptContext(void) {
    return g_FieldFxScriptState->current_context;
}

int FieldFx_IsLastSlotHalted(void) {
    return g_FieldFxScriptState->slot_count == 1 && g_FieldFxScriptState->halted == 1;
}

void *FieldFx_StartSlot(void *obj, u8 id, u16 *sizes, FieldFxHandler *handlers) {
    s16 slot = -1;
    s16 i;
    FieldFxSlot *base;
    void *entry;
    void *data;
    FieldFxHandler handler;

    if (!obj || !sizes || !handlers || !g_FieldFxScriptState ||
        !g_FieldFxSlotTable || !g_FieldFxScriptData || sizes[id] > FIELDFX_DATA_SIZE ||
        g_FieldFxScriptState->data_next < 0 ||
        g_FieldFxScriptState->data_next > FIELDFX_DATA_SIZE) return NULL;
    base = g_FieldFxSlotTable;
    for (i = 0; i < 0x40; i++) {
        if (base[i].flag == 0) {
            slot = i;
            break;
        }
    }

    if (slot == -1) {
        g_FieldFxScriptState->regs[14] = 1;
        return 0;
    }

    if (FieldFx_GetStatus(obj) == 3 || FieldFx_GetStatus(obj) == 4 || FieldFx_GetStatus(obj) == 5) {
        FieldFx_ActivateSlot(slot, id, sizes[id]);

        entry = &g_FieldFxSlotTable[slot];
        if (((FieldFxSlot *)entry)->flag != 1) return NULL;
        data = g_FieldFxScriptData + ((FieldFxSlot *)entry)->data_offset;
        handler = handlers[id];
        if (handler != (void *)-1) {
            handler(obj, entry, data);
        }
        return data;
    }

    return 0;
}

/* Activate slot `slot` with handler `id` and reserve `size` bytes of script
 * data for it, wrapping to offset 0 when the 0x80C-byte area would overflow. */
void FieldFx_ActivateSlot(u16 slot, u8 id, u16 size) {
    FieldFxState *state;
    s16 offset;

    if (!g_FieldFxScriptState || !g_FieldFxSlotTable || slot >= 64 ||
        g_FieldFxSlotTable[slot].flag != 0 || size > FIELDFX_DATA_SIZE ||
        g_FieldFxScriptState->data_next < 0 ||
        g_FieldFxScriptState->data_next > FIELDFX_DATA_SIZE ||
        g_FieldFxScriptState->slot_count < 0 || g_FieldFxScriptState->slot_count >= 64) return;
    g_FieldFxSlotTable[slot].flag = 1;
    g_FieldFxSlotTable[slot].handler_id = id;
    g_FieldFxSlotTable[slot].counter = 0;
    state = g_FieldFxScriptState;
    offset = state->data_next;
    if ((unsigned int)(offset + size) >= 0x80C) {
        offset = 0;
    }
    g_FieldFxSlotTable[slot].data_offset = offset;
    offset += size;
    state->data_next = offset;
    state->slot_count++;
}

int FieldFx_FreeSlot(u16 slot) {
    if (!g_FieldFxScriptState || !g_FieldFxSlotTable || slot >= 64 ||
        g_FieldFxSlotTable[slot].flag == 0 || g_FieldFxScriptState->slot_count <= 0) return 0;
    g_FieldFxSlotTable[slot].flag = 0;
    g_FieldFxScriptState->slot_count--;

    return -(g_FieldFxScriptState->slot_count == 0 && g_FieldFxScriptState->halted == 1);
}

int FieldFx_FreeAllSlots(void) {
    int i;
    int result;

    result = 0;
    i = 0;
    for (; i < 0x40; i++) {
        if (g_FieldFxSlotTable[i].flag != 0) {
            u32 check;
            g_FieldFxSlotTable[(u16)i].flag = 0;
            g_FieldFxScriptState->slot_count--;
            check = g_FieldFxScriptState->slot_count == 0 && g_FieldFxScriptState->halted == 1;
            result |= -check;
        }
    }

    return result;
}
