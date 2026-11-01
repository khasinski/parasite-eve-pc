#include "pe1/boot_disc_check.h"
#include "pe1/dialogue_alpha.h"
#include "pe1/bounds_check.h"
#include "pe1/draw_buffers.h"
#include "pe1/draw_state.h"
#include "pe1/field_pad.h"
#include "pe1/menu_queue.h"
#include "pe1/menu_widget.h"
#include "pe1/psyq_gpu.h"
#include "pe1/psyq_nop.h"
#include "pe1/psyq_tim.h"
#include "pe1/render_buffers.h"

MenuQueueEntry g_MenuEventQueuePool[MENU_EVENT_QUEUE_SIZE];
MenuQueueEntry *g_MenuEventQueueFreeList;
MenuQueueEntry *g_MenuEventQueueHead;
MenuQueueEntry *g_MenuEventQueueTail;
extern int g_MenuInputActive;
int g_MenuInputPollingPaused;
int g_MenuInputHeldStatusMask;

void MenuInput_InitQueue(void) {
    MenuQueueEntry *entry;
    MenuQueueEntry *end;

    entry = g_MenuEventQueuePool;
    end = g_MenuEventQueuePool + MENU_EVENT_QUEUE_SIZE;
    if (entry < end) {
        do {
            entry->next = entry + 1;
            entry = entry->next;
        } while (entry < end);
    }

    g_MenuEventQueuePool[MENU_EVENT_QUEUE_SIZE - 1].next = 0;
    g_MenuEventQueueFreeList = g_MenuEventQueuePool;
    g_MenuEventQueueTail = 0;
    g_MenuEventQueueHead = 0;
    g_MenuInputActive = 0;
    g_MenuInputPollingPaused = 0;
    g_MenuInputHeldStatusMask = 0;
}

void Queue_Enqueue(int arg0, int arg1) {
    MenuQueueEntry *entry;
    MenuQueueEntry *tail;

    entry = g_MenuEventQueueFreeList;
    if (entry != 0) {
        g_MenuEventQueueFreeList = entry->next;
        entry->next = 0;
        tail = g_MenuEventQueueTail;
        if (tail != 0) {
            tail->next = entry;
        } else {
            if (g_MenuEventQueueHead != 0) {
                Inv_BoundsCheckAssertStub(0x1F);
            }
            g_MenuEventQueueHead = entry;
        }
        g_MenuEventQueueTail = entry;
        entry->payload.values.value0 = arg0;
        entry->payload.values.value1 = arg1;
    }
}

void Queue_DequeueByMask(int mask, MenuQueueEntry *out_arg) {
    MenuQueueEntry *out;
    MenuQueueEntry *entry;
    MenuQueueEntry *prev;
    MenuQueueEntry *head;

    out = out_arg;
    if (out == 0) {
        return;
    }

    head = g_MenuEventQueueHead;
    if (head != 0) {
        entry = head;
        prev = 0;
        while (entry != 0 && (entry->payload.values.value0 & mask) == 0) {
            prev = entry;
            entry = entry->next;
        }

        if (entry != 0) {
            if (prev != 0) {
                prev->next = entry->next;
            } else {
                g_MenuEventQueueHead = entry->next;
            }

            if (entry == g_MenuEventQueueTail) {
                g_MenuEventQueueTail = prev;
            }

            {
                MenuQueueEntry *oldFree = g_MenuEventQueueFreeList;
                g_MenuEventQueueFreeList = entry;
                entry->next = oldFree;
            }
            *out = *entry;
        } else {
            out->payload.values.value0 = 0;
            out->payload.values.value1 = 0;
        }
    } else {
        out->payload.values.value0 = 0;
        out->payload.values.value1 = 0;
    }
}

typedef MenuWidgetNode MenuInputWidget;

/* Shared native state; external variables have one storage definition. */
int g_MenuDrawPhase;
int g_MenuInputRepeatTimer;

/* Translate the field pad bits into the menu input status flags. */
int Draw_RemapStatusFlags(void) {
    u32 pad;
    u32 lower;
    int select;
    int mask;

    pad = g_FieldPadBits;
    mask = (pad << 9) & 0x1000;
    if (pad & 0x20) {
        mask |= 0x4000;
    }
    if (pad & 0x40) {
        mask |= 0x8000;
    }
    if (pad & 0x10) {
        mask |= 0x2000;
    }
    if (pad & 0x20000000) {
        mask |= 0x20;
    }
    if (pad & 0x40000000) {
        mask |= 0x40;
    }
    if (pad & 0x10000000) {
        mask |= 0x10;
    }
    if (pad & 0x80000000u) {
        mask |= 0x80;
    }
    if (pad & 0x04000000) {
        mask |= 0x4;
    }
    if (pad & 0x08000000) {
        mask |= 0x8;
    }
    lower = pad;
    if (lower & 0x01000000) {
        mask |= 0x1;
    }
    if (lower & 0x02000000) {
        mask |= 0x2;
    }
    select = lower & 0x2;
    if (select) {
        mask |= 0x100;
    }
    if (pad & 0x4) {
        lower = 0x800;
        mask |= lower;
    }
    return mask;
}

void MenuInput_SetPollingPaused(int paused) {
    g_MenuInputPollingPaused = paused;
}

int MenuInput_GetRepeatStep(void) {
    return g_MenuDrawPhase;
}

void MenuInput_EnqueueStatusChanges(int flags) {
    int mapped;
    int changed;
    int timer;
    int repeat_reset;

    repeat_reset = 0;
    mapped = Draw_RemapStatusFlags();
    if (g_MenuInputActive != 0) {
        changed = ~mapped & g_MenuInputHeldStatusMask;
        if (changed != 0) {
            Queue_Enqueue(4, changed);
        }

        if (g_MenuInputHeldStatusMask != mapped) {
            g_MenuInputRepeatTimer = 0x10;
        }

        timer = g_MenuInputRepeatTimer - 2;
        g_MenuInputRepeatTimer = timer;
        if (timer < 0) {
            if ((flags != 0 && timer < -0x5A) || (timer & 3) == 0) {
                g_MenuInputHeldStatusMask = 0;
                repeat_reset = 1;
            }
        }

        g_MenuDrawPhase = g_MenuInputRepeatTimer >= -0x12B ? 1 : 8;

        changed = mapped & ~g_MenuInputHeldStatusMask;
        if (changed != 0) {
            if (repeat_reset == 0 || (changed & 0x40) == 0) {
                Queue_Enqueue(repeat_reset != 0 ? 2 : 1, changed);
            }
        }
        g_MenuInputHeldStatusMask = mapped;
    } else if (mapped == 0) {
        g_MenuInputActive = 1;
    }
}

void MenuInput_DispatchQueuedEvents(void) {
    MenuInputWidget *node;
    MenuInputQueuedEvent local;
    int type;
    int flags;
    int handled;

    node = MenuWidget_GetCurrentNode();
    if (g_MenuInputPollingPaused == 0) {
        MenuInput_EnqueueStatusChanges(node ? node->flags : 0);
    }

    Queue_DequeueByMask(-1, &local);

    type = local.payload.input.type;
    if (type <= 0) {
        return;
    }

    if (type >= 3 && type != 4) {
        return;
    }
    if (type < 3) {
        if (node == 0) {
            return;
        }
        do {
            flags = local.payload.input.flags;
            if (local.payload.input.type == 2) {
                flags |= 0x20000;
            }
            handled = node->update ? node->update(node, flags) : 0;
            if (handled != 0) {
                return;
            }
            node = node->parent;
        } while (node != 0);
    } else {
        if ((local.payload.input.flags & 0x20) == 0) {
            return;
        }
        node = MenuWidget_GetCurrentNode();
        if (node == 0) {
            return;
        }
        do {
            handled = node->update ? node->update(node, 0x10000) : 0;
            if (handled != 0) {
                return;
            }
            node = node->parent;
        } while (node != 0);
    }
}

int MenuInput_HasConfirm(void) {
    int ret;
    int mask;

    ret = 0;
    if (g_MenuInputActive != 0) {
        mask = Draw_RemapStatusFlags() & 0x20;
        ret = mask != 0;
    }
    return ret;
}

int MenuInput_HasNavigationRepeat(void) {
    int ret;
    int mask;

    ret = 0;
    if (g_MenuInputActive != 0) {
        mask = Draw_RemapStatusFlags() & 0x5000;
        ret = mask != 0;
    }
    return ret;
}

int g_DrawPresentEnabled;

int MenuInput_GetStatusFlags(void) {
    if (g_MenuInputActive != 0) {
        return Draw_RemapStatusFlags();
    }
    return 0;
}

void Draw_SetPresentEnabled(int arg0) {
    g_DrawPresentEnabled = arg0;
}


extern int g_TextCursorX;
extern int g_TextCursorY;
extern int g_DrawGradientBlendColor;
extern int *g_TextCursorStackPtr;
DrawFrameBuffer *g_DrawCurrentFrameBuffer;
DrawFrameBuffer g_DrawFrameBuffers[2];
extern u8 *g_DrawPacketCursor;
extern u8 *g_DrawPacketArenaBase;
RenderNativeTag *g_DrawOrderingTableBase;
extern u32 *g_DrawOrderingTableEntry;
void Draw_InitBuffers(void) {
    g_DrawFrameBuffers[0].frontBufferBase = g_RenderFrontBufferBase[0];
    g_DrawFrameBuffers[1].frontBufferBase = g_RenderBackBufferBase[0];
    g_DrawFrameBuffers[0].orderingTableBase = (RenderNativeTag *)g_OtBufferTable[0];
    g_DrawFrameBuffers[1].orderingTableBase = (RenderNativeTag *)g_RenderOtBufferBaseAlt[0];
    SetDefDrawEnv(&g_DrawFrameBuffers[0].draw, 0, 0, 0x140, 0xE0);
    SetDefDrawEnv(&g_DrawFrameBuffers[1].draw, 0, 0xE0, 0x140, 0xE0);
    g_DrawFrameBuffers[0].draw.isbg = g_DrawFrameBuffers[1].draw.isbg = 1;
    g_DrawFrameBuffers[0].draw.r0 = 0;
    g_DrawFrameBuffers[0].draw.g0 = 0;
    g_DrawFrameBuffers[0].draw.b0 = 0;
    g_DrawFrameBuffers[1].draw.r0 = 0;
    g_DrawFrameBuffers[1].draw.g0 = 0;
    g_DrawFrameBuffers[1].draw.b0 = 0;
    SetDefDispEnv(&g_DrawFrameBuffers[0].display, 0, 0xE0, 0x140, 0xE0);
    SetDefDispEnv(&g_DrawFrameBuffers[1].display, 0, 0, 0x140, 0xE0);
    g_DrawFrameBuffers[0].display.screen.y = 8;
    g_DrawFrameBuffers[1].display.screen.y = 8;
    g_DrawFrameBuffers[0].display.screen.h = 0xE0;
    g_DrawFrameBuffers[1].display.screen.h = 0xE0;
    g_TextCursorY = 0;
    g_TextCursorX = 0;
    g_TextCursorStackPtr = g_TextCursorStackBottom;
    Draw_SetColor(0x808080);
    g_DrawGradientBlendColor = 0;
    Draw_SetFontVariant(0);
    g_DrawPresentImage = 0;
}

void *g_DrawPresentImage;

extern int g_DrawBufferIndex;

void Draw_SetPresentImage(void *arg0) {
    g_DrawPresentImage = arg0;
}

void Draw_SelectBuffer(void) {
#ifdef PE_PS5_GPU_NATIVE
    PeMenuFont_Prepare();
#endif
    int index;
    DrawFrameBuffer *frame;
    u8 *packets;
    RenderNativeTag *ordering;

    if (g_DrawPresentEnabled != 0) {
        index = g_DrawBufferIndex == 0;
    } else {
        index = g_ActiveDrawSlot;
    }

    g_DrawBufferIndex = index;
    frame = &g_DrawFrameBuffers[index];
    packets = g_DrawFrameBuffers[index].frontBufferBase;
    ordering = g_DrawFrameBuffers[index].orderingTableBase;
    g_DrawCurrentFrameBuffer = frame;
    g_DrawPacketArenaBase = packets;
    g_DrawPacketCursor = packets;
    g_DrawOrderingTableBase = ordering;
    g_DrawOrderingTableEntry = (u32 *)(ordering + 1);

    if (g_DrawPresentEnabled != 0) {
        ClearOTagR(ordering, 0x1000);
    }
}

static inline int NormalizeSyncMode(int mode) {
    if (mode == 1) {
        mode = 0;
    }
    return mode;
}

void Draw_PresentFrame(int arg0) {
    int index;
    RECT rect;
    int mode;
    void *image;
    int y;

    index = arg0;
    if (g_DrawPresentEnabled == 0) {
        return;
    }

    VSync(1);
    DrawSync(0);
    mode = index;
    index = 0xFFF;
    VSync(NormalizeSyncMode(mode));
    ResetGraph(1);
    PutDrawEnv(&g_DrawCurrentFrameBuffer->draw);
    PutDispEnv(&g_DrawCurrentFrameBuffer->display);

    image = g_DrawPresentImage;
    if (image != 0) {
        y = 0xB;
        rect.x = 0;
        if (g_DrawBufferIndex != 0) {
            y = 0xEB;
        }
        rect.y = y;
        rect.w = 0x140;
        rect.h = 0xCC;
        LoadImage(&rect, (void *)image);
    }

    DrawOTag(&g_DrawOrderingTableBase[index]);
}
