#include "common.h"
#include <SDL3/SDL.h>
#include "pe_hooks.h"
#include "pe1/game_timers.h"
#include "pe1/geom_state.h"
#include "pe1/inventory_slots.h"
#include "pe1/memcard.h"
#include "pe1/memcard_save_state.h"
#include "pe1/menu_notification_dialog.h"
#include "pe1/menu_widget.h"
#include "pe1/psyq_bios.h"
#include "pe1/resident_tables.h"
#include "pe1/save.h"
#include "pe1/save_blob.h"
#include "pe1/save_tail_state.h"
#include "pe1/script_queue.h"

void (*g_MemCardDelayedCallback)(void);
int g_MemCardDelayedCallbackTimer;

MemCardPortState g_MemCardPortStates[2];
MemCardPreviewBuffer g_MemCardPreviewBuffers[2];
MemCardPortState *g_MemCardActiveState;
int g_MemCardActivePortOneBased, g_MemCardActiveBytesRemaining;
int g_MemCardActivePromptPending, g_MemCardLoadSucceeded;
unsigned int g_MemCardServicedPort;
int g_MemCardInfoPollCountdown, g_MemCardConnectDebounce, g_MemCardConnectPending[2];
int g_McOpPending, g_MemCardSavePollTimeout, g_MemCardReadContext;
int g_SaveTitleStyleFlag, D_800A5D50;
u8 *g_SaveIoCursor;
char g_MemCardFilePath[0x20];
const char *g_MemCardFilenameFormatPtr = "bu%d0:BASLUS-00662000000%c%c";
static char g_MemCardSearchPattern[] = "bu00:*";
char *g_MemCardSearchPatternPtr = g_MemCardSearchPattern;
MemCardFileHeader g_MemCardIconHeader;
_Alignas(16) u8 g_MemCardFileBuffer[0x2000];

/* Keep the retail polling/UI states, but publish a complete native file only
 * after it has been flushed and closed. An interrupted write never owns the
 * previous save and must never erase it. */
static struct {
    SDL_IOStream *io;
    int owns_temporary;
    char target[4096], temporary[4160], previous[4096];
} card_writes[2];

static void MemCard_CancelWrite(MemCardPortState *state) {
    unsigned port = (unsigned)(state - g_MemCardPortStates);
    if (port >= 2) return;
    if (card_writes[port].io) SDL_CloseIO(card_writes[port].io);
    if (card_writes[port].owns_temporary) SDL_RemovePath(card_writes[port].temporary);
    memset(&card_writes[port], 0, sizeof(card_writes[port]));
}

static int MemCard_BeginWrite(MemCardPortState *state) {
    unsigned port = (unsigned)(state - g_MemCardPortStates);
    if (card_writes[port].io) return 1;
    Psyz_AdjustPath(card_writes[port].target, g_MemCardFilePath,
                    sizeof(card_writes[port].target));
    char *slash = strrchr(card_writes[port].target, '/');
    char *backslash = strrchr(card_writes[port].target, '\\');
    if (!slash || (backslash && backslash > slash)) slash = backslash;
    size_t prefix = slash ? (size_t)(slash + 1 - card_writes[port].target) : 0;
    int n = snprintf(card_writes[port].temporary, sizeof(card_writes[port].temporary),
        "%.*s.pe-save-%u-%llu.tmp", (int)prefix, card_writes[port].target,
        port, (unsigned long long)SDL_GetTicksNS());
    if (n < 0 || (size_t)n >= sizeof(card_writes[port].temporary)) return 0;
    card_writes[port].io = SDL_IOFromFile(card_writes[port].temporary, "wbx");
    card_writes[port].owns_temporary = card_writes[port].io != NULL;
    return card_writes[port].owns_temporary;
}

static int MemCard_CommitWrite(MemCardPortState *state) {
    unsigned port = (unsigned)(state - g_MemCardPortStates);
    SDL_IOStream *io = card_writes[port].io;
    if (!io) return 0;
    int ok = SDL_FlushIO(io);
    if (!SDL_CloseIO(io)) ok = 0;
    card_writes[port].io = NULL;
    if (!ok || !SDL_RenamePath(card_writes[port].temporary, card_writes[port].target)) {
        MemCard_CancelWrite(state);
        return 0;
    }
    /* Clear-data/normal saves use different names for the same card slot.
     * Retire the previous name only after the new image is safely published. */
    if (*card_writes[port].previous &&
        strcmp(card_writes[port].previous, card_writes[port].target))
        SDL_RemovePath(card_writes[port].previous);
    memset(&card_writes[port], 0, sizeof(card_writes[port]));
    return 1;
}

SaveSjisFormatArguments g_SaveSjisFormatArguments;
u8 g_SaveTitleBuffer[0x44];
static u8 g_SaveTitleFormatLong[] = {0x82,0x6F,0x82,0x64,0x81,0x7C,'%','d',0x81,0x5E,'%','d',0x81,0x46,'%','d',0x81,0x46,'%','d',0x81,0x5E,0x82,0x63,0x82,0x60,0x82,0x78,'%','D',0x81,0x40,'%','s',0};
static u8 g_SaveTitleFormatShort[] = {0x82,0x6F,0x82,0x64,0x81,0x7C,'%','d',0x81,0x5E,'%','d',0x81,0x46,'%','d',0x81,0x46,'%','d',0x81,0x5E,'%','s',0};
u8 *g_SaveTitleFormatLongPtr = g_SaveTitleFormatLong;
u8 *g_SaveTitleFormatShortPtr = g_SaveTitleFormatShort;

long g_MemCardEventHandles[MEMCARD_EVENT_COUNT];
int g_MemCardManagerReady;
int g_MemCardEventF400Spec0004Flag, g_MemCardRemovedEventPending, g_MemCardEventF400Spec2000Flag;
int g_MemCardEventF000Spec0004Flag, g_MemCardEventF000Spec8000Flag, g_MemCardEventF000Spec2000Flag;

/* The save tail is a packed byte stream: each record is copied as an
 * unaligned byte block at the IO cursor, which then advances past it. */
#define SAVE_PUT(type, src)                                \
    memcpy(g_SaveIoCursor, (src), sizeof(type));              \
    g_SaveIoCursor += sizeof(type)

int Save_SerializeTail(void) {
    if ((g_AyaBattleState.action && g_AyaBattleState.action != &g_SavedBattleStateTail) ||
        (g_AyaBattleState.attributes && g_AyaBattleState.attributes != &g_BattleEquipStateBlock)) return -1;
    /* The file contains the original 0x70-byte record, not host padding or
     * native addresses. Its two links name the two fixed saved records. */
    u8 aya[0x70];
    memcpy(aya, g_AyaBattleState.words, 0x1C);
    memcpy(aya + 0x1C, (u8 *)g_AyaBattleState.words + PE1_OFFSETOF(Combatant, maxHP), 0x4C);
    u32 links[2] = {g_AyaBattleState.action ? 0x800B0CB0u : 0,
                    g_AyaBattleState.attributes ? 0x8009D1B0u : 0};
    memcpy(aya + 0x68, links, sizeof(links));
    SAVE_PUT(SaveBytes800, g_ScriptVariableBank);
    SAVE_PUT(SaveBytes4, &D_8009D2E8);
    SAVE_PUT(SaveBytes4, &g_SceneDispatchToken);
    SAVE_PUT(SaveBytes4, &g_GameStateFlags);
    SAVE_PUT(SaveBytes4, &g_GameState.saved_state_004);
    SAVE_PUT(SaveBytes2, &g_GameState.room_type);
    SAVE_PUT(SaveBytes2, &g_GameState.requested_entity_bank);
    SAVE_PUT(SaveBytes2, &g_GameState.current_story_day);
    SAVE_PUT(SaveU8, &g_GameState.story_day_flags);
    SAVE_PUT(SaveU8, &g_ScreenTint.fadeMode);
    SAVE_PUT(SaveBytes70, aya);
    SAVE_PUT(SaveBytes18, &g_SavedBattleStateTail);
    SAVE_PUT(SaveBytes8, &g_BattleEquipStateBlock);
    return 0;
}

#define SAVE_GET(type, dst)                                \
    memcpy((dst), g_SaveIoCursor, sizeof(type));              \
    g_SaveIoCursor += sizeof(type)

int Save_DeserializeTail(void) {
    u32 links[2];
    memcpy(links, g_SaveIoCursor + 0x818 + 0x68, sizeof(links));
    if ((links[0] && links[0] != 0x800B0CB0u) ||
        (links[1] && links[1] != 0x8009D1B0u)) return -1;
    SAVE_GET(SaveBytes800, g_ScriptVariableBank);
    SAVE_GET(SaveBytes4, &D_8009D2E8);
    SAVE_GET(SaveBytes4, &g_SceneDispatchToken);
    SAVE_GET(SaveBytes4, &g_GameStateFlags);
    SAVE_GET(SaveBytes4, &g_GameState.saved_state_004);
    SAVE_GET(SaveBytes2, &g_GameState.room_type);
    SAVE_GET(SaveBytes2, &g_GameState.requested_entity_bank);
    SAVE_GET(SaveBytes2, &g_GameState.current_story_day);
    SAVE_GET(SaveU8, &g_GameState.story_day_flags);
    SAVE_GET(SaveU8, &g_ScreenTint.fadeMode);
    memset(&g_AyaBattleState, 0, sizeof(g_AyaBattleState));
    memcpy(g_AyaBattleState.words, g_SaveIoCursor, 0x1C);
    memcpy((u8 *)g_AyaBattleState.words + PE1_OFFSETOF(Combatant, maxHP), g_SaveIoCursor + 0x1C, 0x4C);
    g_AyaBattleState.action = links[0] ? &g_SavedBattleStateTail : NULL;
    g_AyaBattleState.attributes = links[1] ? &g_BattleEquipStateBlock : NULL;
    g_SaveIoCursor += 0x70;
    SAVE_GET(SaveBytes18, &g_SavedBattleStateTail);
    SAVE_GET(SaveBytes8, &g_BattleEquipStateBlock);
    g_GameStateFlags &= 0xFFFF2679;
    D_8009D2E8 &= ~9;
    return 0;
}

void Save_SetTitleStyleFlag(int value) {
    g_SaveTitleStyleFlag = value;
}

int Save_GetTitleStyleFlag(void) {
    return g_SaveTitleStyleFlag;
}

int MemCard_HasAnyPresentFile(void) {
    for (unsigned port = 0; port < 2; ++port) {
        const MemCardPortState *state = &g_MemCardPortStates[port];
        if (state->managerState != 15) continue;
        for (unsigned slot = 0; slot < 15; ++slot) {
            if (state->slots[slot].state == MEMCARD_SLOT_OCCUPIED &&
                state->slots[slot].titleStyleFlag != 0) return 1;
        }
    }
    return 0;
}

void Save_SprintfSjis(unsigned char *dst, unsigned char *fmt)
{
    SaveSjisFormatArgument *args;
    int ch; /* also holds each Shift-JIS digit ch */
    unsigned char *out;
    int value;
    unsigned char *text;
    int byte;

    out = dst;
    ch = *fmt;
    SaveSjisFormatArgument arguments[6] = {
        {.number = g_SaveSjisFormatArguments.chapter}, {.number = g_SaveSjisFormatArguments.hours},
        {.number = g_SaveSjisFormatArguments.minutes}, {.number = g_SaveSjisFormatArguments.seconds},
        g_SaveSjisFormatArguments.primary, g_SaveSjisFormatArguments.secondary
    };
    args = arguments;
    while (ch != 0) {
        ch &= 0xFF;
        fmt++;
        if (ch == '%') {
            ch = *fmt++;
            switch (ch) {
            case 'd':
                value = args++->number;
                ch = (value / 10) % 10 + 0x824Fu;
                *out++ = (unsigned int)ch >> 8;
                *out++ = (unsigned char)ch;
                ch = value % 10 + 0x824Fu;
                *out++ = (unsigned int)ch >> 8;
                *out++ = (unsigned char)ch;
                break;
            case 'D':
                value = args++->number;
                ch = value % 10 + 0x824Fu;
                *out++ = (unsigned int)ch >> 8;
                *out++ = (unsigned char)ch;
                break;
            case 's':
                text = args++->text;
                if (text == 0) break;
                byte = *text;
                if (byte == 0) break;
                do {
                    text++;
                    *out++ = byte;
                    byte = *text;
                } while (byte != 0);
                break;
            default:
                break;
            }
        } else {
            *out++ = *(fmt - 1);
        }
        ch = *fmt;
    }
    *out = 0;
}

typedef unsigned char u8_1;

u8 *Save_FormatTitle(int chapter, int playtime_seconds) {
    int minutes_total;
    int hours;
    int minutes;
    int seconds;
    u8 *value;

    bzero(g_SaveTitleBuffer, 0x44);

    hours = playtime_seconds / 3600;
    minutes_total = playtime_seconds / 60;
    minutes = minutes_total - (hours * 60);
    seconds = playtime_seconds - (minutes_total * 60);

    g_SaveSjisFormatArguments.chapter = chapter;
    g_SaveSjisFormatArguments.hours = hours;
    g_SaveSjisFormatArguments.minutes = minutes;
    g_SaveSjisFormatArguments.seconds = seconds;

    if (g_SaveTitleStyleFlag != 0) {
        value = Text_GetMapLocationName(-1);
        g_SaveSjisFormatArguments.primary.text = value;
        Save_SprintfSjis(g_SaveTitleBuffer, g_SaveTitleFormatShortPtr);
    } else {
        g_SaveSjisFormatArguments.primary.number =
            Save_ClassifyPlaytime(g_PlayTimeFrameCounter);
        value = Text_GetMapLocationName(Save_GetCurrentMapNumber());
        g_SaveSjisFormatArguments.secondary.text = value;
        Save_SprintfSjis(g_SaveTitleBuffer, g_SaveTitleFormatLongPtr);
    }

    return g_SaveTitleBuffer;
}

u32 Save_CalcCrc16(u32 size, u8_1 *data) {
    u32 crc = 0xFFFF;
    u32 i = 0;

    size &= 0xFFFF;
    if (size != 0) {
        do {
            u32 bit = 0;

            crc ^= data[i & 0xFFFF] << 8;
            do {
                if (crc & 0x8000) {
                    crc = (crc << 1) ^ 0x1021;
                } else {
                    crc <<= 1;
                }
                bit++;
            } while ((bit & 0xFFFF) < 8);
            i++;
        } while ((i & 0xFFFF) < size);
    }

    return ~crc & 0xFFFF;
}

void MemCard_ClearPort1Events(void) {
    TestEvent(g_MemCardEventHandles[MEMCARD_EVENT_HW_IO_END]);
    TestEvent(g_MemCardEventHandles[MEMCARD_EVENT_HW_ERROR]);
    TestEvent(g_MemCardEventHandles[MEMCARD_EVENT_HW_TIMEOUT]);
    TestEvent(g_MemCardEventHandles[MEMCARD_EVENT_HW_NEW_CARD]);
    g_MemCardEventF400Spec2000Flag = 0;
    g_MemCardRemovedEventPending = 0;
    g_MemCardEventF400Spec0004Flag = 0;
}

void MemCard_ClearEvents(void) {
    TestEvent(g_MemCardEventHandles[MEMCARD_EVENT_SW_IO_END]);
    TestEvent(g_MemCardEventHandles[MEMCARD_EVENT_SW_ERROR]);
    TestEvent(g_MemCardEventHandles[MEMCARD_EVENT_SW_TIMEOUT]);
    TestEvent(g_MemCardEventHandles[MEMCARD_EVENT_SW_NEW_CARD]);
    g_MemCardEventF000Spec2000Flag = 0;
    g_MemCardEventF000Spec8000Flag = 0;
    g_MemCardEventF000Spec0004Flag = 0;
}

int MemCard_PollTransferDelay(void) {
    int *pending;
    int count;
    int one;
    int reset;
    int value;
    int clamped;
    int final_value;

    count = 0;
    reset = 12;
    one = 1;
    pending = g_MemCardConnectPending;
    value = g_MemCardConnectDebounce;
    g_MemCardConnectDebounce = value - (value > 0);

    do {
        if (g_MemCardPortStates[count].cardState - 2U < 2U) {
            g_MemCardConnectDebounce = reset;
            *pending = one;
        }
        pending++;
        count++;
    } while (count < 2);

    pending = g_MemCardConnectPending;
    if (pending[0] != 0 && g_MemCardPortStates[0].cardState != 4) {
    } else if (*(pending + 1) != 0 && g_MemCardPortStates[1].cardState != 4) {
    } else {
        final_value = g_MemCardConnectDebounce;
        clamped = 4;
        if (final_value < 5) {
            clamped = final_value;
        }
        g_MemCardConnectDebounce = clamped;
        *(pending + 1) = 0;
        pending[0] = 0;
    }
    return g_MemCardConnectDebounce > 0;
}

void _card_clear(int arg0);
void _card_info(int arg0);
void _card_load(int arg0);
int Menu_IsMemCardDialogOpen(void);
void MemCard_StartRead(int port, int arg1);

void MemCard_StepPortState(int port) {
    MemCardPortState *state;
    int value;

    state = &g_MemCardPortStates[port];

    switch (state->cardState) {
    case 0:
        state->present = 0;
        break;

    case 1:
        if (g_MemCardEventF400Spec0004Flag != 0) {
            g_MemCardEventF400Spec0004Flag = 0;
            if ((state->present & 1) == 0) {
                TestEvent(g_MemCardEventHandles[MEMCARD_EVENT_SW_IO_END]);
                TestEvent(g_MemCardEventHandles[MEMCARD_EVENT_SW_ERROR]);
                TestEvent(g_MemCardEventHandles[MEMCARD_EVENT_SW_TIMEOUT]);
                TestEvent(g_MemCardEventHandles[MEMCARD_EVENT_SW_NEW_CARD]);
                g_MemCardEventF000Spec2000Flag = 0;
                g_MemCardEventF000Spec8000Flag = 0;
                g_MemCardEventF000Spec0004Flag = 0;
                _card_clear(port << 4);
                state->cardState = 2;
                return;
            }
            state->cardState = 4;
            g_MemCardInfoPollCountdown = 0;
            g_MemCardServicedPort = g_MemCardServicedPort < 1;
            return;
        }
        if (g_MemCardRemovedEventPending != 0) {
            g_MemCardRemovedEventPending = 0;
            state->cardState = 0;
            state->present &= ~4;
            g_MemCardInfoPollCountdown = 0;
            g_MemCardServicedPort = g_MemCardServicedPort < 1;
            return;
        }
        if (g_MemCardEventF400Spec2000Flag == 0) {
            return;
        }
        g_MemCardEventF400Spec2000Flag = 0;
        TestEvent(g_MemCardEventHandles[MEMCARD_EVENT_SW_IO_END]);
        TestEvent(g_MemCardEventHandles[MEMCARD_EVENT_SW_ERROR]);
        TestEvent(g_MemCardEventHandles[MEMCARD_EVENT_SW_TIMEOUT]);
        TestEvent(g_MemCardEventHandles[MEMCARD_EVENT_SW_NEW_CARD]);
        g_MemCardEventF000Spec2000Flag = 0;
        g_MemCardEventF000Spec8000Flag = 0;
        g_MemCardEventF000Spec0004Flag = 0;
        _card_clear(port << 4);
        state->cardState = 2;
        return;

    case 2:
        if (g_MemCardEventF000Spec0004Flag != 0) {
            g_MemCardEventF000Spec0004Flag = 0;
            TestEvent(g_MemCardEventHandles[MEMCARD_EVENT_HW_IO_END]);
            TestEvent(g_MemCardEventHandles[MEMCARD_EVENT_HW_ERROR]);
            TestEvent(g_MemCardEventHandles[MEMCARD_EVENT_HW_TIMEOUT]);
            TestEvent(g_MemCardEventHandles[MEMCARD_EVENT_HW_NEW_CARD]);
            g_MemCardEventF400Spec2000Flag = 0;
            g_MemCardRemovedEventPending = 0;
            g_MemCardEventF400Spec0004Flag = 0;
            _card_load(port << 4);
            state->cardState = 3;
            return;
        }
        if (g_MemCardEventF000Spec8000Flag == 0 && g_MemCardEventF000Spec2000Flag == 0) {
            return;
        }
        g_MemCardEventF000Spec8000Flag = 0;
        g_MemCardEventF000Spec2000Flag = 0;
        state->cardState = 0;
        g_MemCardInfoPollCountdown = 0;
        g_MemCardServicedPort = g_MemCardServicedPort < 1;
        return;

    case 3:
        if (g_MemCardEventF400Spec0004Flag != 0) {
            g_MemCardEventF400Spec0004Flag = 0;
            state->cardState = 4;
            if (Menu_IsMemCardDialogOpen() != 0) {
                g_MemCardInfoPollCountdown = 0;
                g_MemCardServicedPort = g_MemCardServicedPort < 1;
                return;
            }
            state->present |= 1;
            MemCard_StartRead(port, 0);
            g_MemCardInfoPollCountdown = 0;
            g_MemCardServicedPort = g_MemCardServicedPort < 1;
            return;
        }
        if (g_MemCardRemovedEventPending != 0) {
            g_MemCardRemovedEventPending = 0;
            state->cardState = 0;
            g_MemCardInfoPollCountdown = 0;
            g_MemCardServicedPort = g_MemCardServicedPort < 1;
            return;
        }
        if (g_MemCardEventF400Spec2000Flag == 0) {
            return;
        }
        g_MemCardEventF400Spec2000Flag = 0;
        state->cardState = 4;
        state->present |= 4;
        g_MemCardInfoPollCountdown = 0;
        g_MemCardServicedPort = g_MemCardServicedPort < 1;
        return;

    case 4:
        state->present |= 1;
        break;

    default:
        return;
    }

    if (g_McOpPending != 0) {
        return;
    }
    if (port != g_MemCardServicedPort) {
        return;
    }

    value = g_MemCardInfoPollCountdown;
    g_MemCardInfoPollCountdown = value - 1;
    if (value > 0) {
        return;
    }

    TestEvent(g_MemCardEventHandles[MEMCARD_EVENT_HW_IO_END]);
    TestEvent(g_MemCardEventHandles[MEMCARD_EVENT_HW_ERROR]);
    TestEvent(g_MemCardEventHandles[MEMCARD_EVENT_HW_TIMEOUT]);
    TestEvent(g_MemCardEventHandles[MEMCARD_EVENT_HW_NEW_CARD]);
    g_MemCardEventF400Spec2000Flag = 0;
    g_MemCardRemovedEventPending = 0;
    g_MemCardEventF400Spec0004Flag = 0;
    _card_info(port << 4);
    state->cardState = 1;
}

void _bu_init(void);

long MemCard_OnEventF400Spec0004(void);
long MemCard_OnEventF400Spec8000(void);
long MemCard_OnEventF400Spec0100(void);
long MemCard_OnEventF400Spec2000(void);
long MemCard_OnEventF000Spec0004(void);
long MemCard_OnEventF000Spec8000(void);
long MemCard_OnEventF000Spec0100(void);
long MemCard_OnEventF000Spec2000(void);

void MemCard_InitManager(void) {
    int i;
    long *events;
    int value;
    long event;
    if (g_MemCardManagerReady == 0) {
        g_MemCardManagerReady = 1;
        i = 0;
        EnterCriticalSection();

        event = OpenEvent(0xF4000001, 0x0004, 0x1000, MemCard_OnEventF400Spec0004);
        events = g_MemCardEventHandles;
        events[0] = event;
        g_MemCardEventHandles[MEMCARD_EVENT_HW_ERROR] = OpenEvent(0xF4000001, 0x8000, 0x1000, MemCard_OnEventF400Spec8000);
        g_MemCardEventHandles[MEMCARD_EVENT_HW_TIMEOUT] = OpenEvent(0xF4000001, 0x0100, 0x1000, MemCard_OnEventF400Spec0100);
        g_MemCardEventHandles[MEMCARD_EVENT_HW_NEW_CARD] = OpenEvent(0xF4000001, 0x2000, 0x1000, MemCard_OnEventF400Spec2000);
        g_MemCardEventHandles[MEMCARD_EVENT_SW_IO_END] = OpenEvent(0xF0000011, 0x0004, 0x1000, MemCard_OnEventF000Spec0004);
        g_MemCardEventHandles[MEMCARD_EVENT_SW_ERROR] = OpenEvent(0xF0000011, 0x8000, 0x1000, MemCard_OnEventF000Spec8000);
        g_MemCardEventHandles[MEMCARD_EVENT_SW_TIMEOUT] = OpenEvent(0xF0000011, 0x0100, 0x1000, MemCard_OnEventF000Spec0100);
        g_MemCardEventHandles[MEMCARD_EVENT_SW_NEW_CARD] = OpenEvent(0xF0000011, 0x2000, 0x1000, MemCard_OnEventF000Spec2000);

        _bu_init();

        do {
            EnableEvent(events[i]);
            i++;
        } while (i < 8);

        ExitCriticalSection();
    }

    /* Clear each port's present flag, walking byte offsets downwards. */
    value = sizeof(MemCardPortState);
    do {
        ((u8 *)g_MemCardPortStates)[value] = 0;
        value -= sizeof(MemCardPortState);
    } while (value >= 0);
}

const MemCardSaveSupportData g_MemCardSaveSupportData __attribute__((used)) = {
    0x4353,
    0,
    "bu%ld0:%s",
    "bu 0:*",
    "bu%ld0:",
    {0, 0, 0, 0},
};

int Save_BuildCardFile(char *path) {
    u8 *resolved;
    MemCardFileHeader *file;
    u32 crc;

    resolved = Text_GetSaveIconData();
    file = &g_MemCardIconHeader;

    D_800A5D50 = 0x2000;
    bzero(file, sizeof(*file));

    file->productCode = g_MemCardSaveSupportData.cardProductCode;
    file->iconDisplay = 0x11;
    file->blockCount = 1;
    /* A full 32-character Shift-JIS title occupies all 64 bytes. The
     * already cleared reserved area supplies the following zero, as in
     * the original card image, without writing past the title subobject. */
    memcpy(file->title, Save_FormatTitle(path[0x18] - 0x40,
           g_SaveRuntimeState.windows.header.playTimeMinutes), sizeof(file->title));

    __builtin_memcpy(g_MemCardIconPalette, resolved + 0x14, 0x20);
    __builtin_memcpy(file->icon, resolved + 0x40, sizeof(file->icon));

    bzero(g_MemCardFileBuffer, 0x2000);
    g_SaveIoCursor = g_MemCardFileBuffer;
    __builtin_memcpy(g_SaveIoCursor, &g_MemCardIconHeader, sizeof(g_MemCardIconHeader));
    g_SaveIoCursor += 0x100;
    __builtin_memcpy(g_SaveIoCursor, (u8 *)&g_SaveRuntimeState, 0x12E4);
    g_SaveIoCursor += 0x12E4;

    if (Save_SerializeTail() < 0) return -1;

    {
        u8 *data = g_MemCardFileBuffer;
        u32 checksum = 0xFFFF;
        u32 i = 0;
        do {
            u32 bit = 0;
            checksum ^= data[i & 0xFFFF] << 8;
            do {
                if (checksum & 0x8000) {
                    checksum = (checksum << 1) ^ 0x1021;
                } else {
                    checksum <<= 1;
                }
                bit++;
            } while ((bit & 0xFFFF) < 8);
            i++;
        } while ((i & 0xFFFF) < 0x2000);
        crc = ~checksum & 0xFFFF;
    }
    __builtin_memcpy(g_SaveIoCursor, &crc, 4);
    g_SaveIoCursor += 4;

    return 0;
}

void MemCard_AbortActiveOperation(MemCardPortState *state) {
    int state_index;
    MemCard_CancelWrite(state);

    if (g_MemCardLoadSucceeded != 0) {
        Save_CancelUiFlow();
        return;
    }

    if (state->fileDescriptor >= 0) {
        close(state->fileDescriptor);
        state->fileDescriptor = -1;
    }


    state_index = state - g_MemCardPortStates;
    Menu_CloseSaveSlotListView(state_index);
    state->managerState = 0;
    state->hasFiles = 0;
    g_MemCardActiveState = 0;
    g_McOpPending = 0;
    MenuWidget_RestoreSavedCurrentNode();
}

static inline void MemCard_FormatSlotFileName(MemCardPortState *state,
                                              int slot) {
    sprintf(g_MemCardFilePath, g_MemCardFilenameFormatPtr, state > g_MemCardPortStates,
                    state->slots[slot].titleStyleFlag + '0', slot + 'A');
}

/* After the retry budget runs out, drop the operation and report it. */
#define MEMCARD_RETRY_OR_FAIL(state)                                         \
    if ((state)->retryCount-- <= 0) {                                        \
        if ((state)->present & 1) {                                          \
            MemCard_AbortActiveOperation(state);                             \
            switch ((state)->pendingError) {                                 \
            case 1:                                                          \
                Menu_CreateTwoLineDialog(0x3D, 0x3F);                        \
                break;                                                       \
            case 2:                                                          \
                Menu_CreateNotificationDialog(0x3E, 0);                      \
                break;                                                       \
            }                                                                \
            (state)->pendingError = 0;                                       \
            (state)->managerState = 12;                                      \
        } else {                                                             \
            MemCard_AbortActiveOperation(state);                             \
        }                                                                    \
    }

static inline void MemCard_NoteSaveFile(MemCardPortState *state,
                                        MemCardDirEntry *entry) {
    MemCardSaveSlot *slot;

    if (!strncmp(entry->name, ".pe-save-", 9)) return;
    if (strncmp(entry->name, &g_MemCardFilenameFormatPtr[6], 12) == 0 &&
        entry->name[19] >= 'A' && entry->name[19] <= 'O') {
        slot = &state->slots[entry->name[19] - 'A'];
        slot->state = MEMCARD_SLOT_OCCUPIED;
        slot->metadataReady = 0;
        slot->titleStyleFlag = entry->name[18] != '0';
        state->hasFiles = 1;
    }
    state->sequence += entry->size >> 13;
}

static inline void MemCard_ApplySavePreview(MemCardSaveSlot *slot,
                                            MemCardSavePreview *preview) {
    if (slot->state == MEMCARD_SLOT_OCCUPIED) {
        slot->currentHp = preview->currentHp;
        slot->maxHp = preview->maxHp;
        slot->levelIndex = preview->levelIndex;
        slot->playTimeMinutes = preview->playTimeMinutes;
        slot->gameTimeMinutes = preview->gameTimeMinutes;
        slot->blendColor = preview->blendColor;
        slot->exGameIndex = preview->exGameIndex;
        slot->progressStage = preview->progressStage;
        slot->mapNumber = preview->mapNumber;
        slot->primaryTitle = preview->primaryTitle;
        slot->alternateTitle = preview->alternateTitle;
    }
    slot->metadataReady = 1;
}

static inline int MemCard_CanStartSave(int port) {
    MemCardPortState *card = &g_MemCardPortStates[port];
    int i;
    int ok;

    for (i = 0; i < 15; i++) {
        if (card->slots[i].state == MEMCARD_SLOT_OCCUPIED) {
            break;
        }
    }
    ok = 0;
    if (i < 15 || (card->sequence < 15 && MemCard_GetDialogMode() != 0)) {
        ok = 1;
    }
    return ok;
}

void MemCard_UpdateSaveState(int port) {
    MemCardPortState *state;
    char device[8];
    MemCardDirEntry entry;
    int i;
    int blocks;
    int fd;
    int count;
    unsigned int candidate;
    u8 cursor;
    u8 next;
    u8 slot;
    s16 size;
    u8 other;
    u8 current;
    MemCardSavePreview *preview;

    state = &g_MemCardPortStates[port];
    switch (state->managerState) {
    case 0:
    case 15:
        break;

    case 1:
        if (state->present != 1) {
            MemCard_AbortActiveOperation(state);
            break;
        }
        current = g_MemCardPortStates[port].cardState;
        if (current == 4) {
            other = g_MemCardPortStates[port == 0].cardState;
            if (other == 0 || other == current) {
                g_McOpPending = 1;
                state->managerState = state->nextState;
            }
        }
        break;

    case 13:
        if (state->present == 5) {
            current = g_MemCardPortStates[port].cardState;
            if (current == 4) {
                other = g_MemCardPortStates[port == 0].cardState;
                if (other == 0 || other == current) {
                    g_McOpPending = 1;
                    state->managerState = 14;
                }
            }
        } else {
            g_MemCardSavePollTimeout = -1;
            state->managerState = 0;
        }
        break;

    case 14:
        sprintf(device, g_MemCardSaveSupportData.deviceFormat, (long)port);
        g_MemCardSavePollTimeout = format(device) ? 12 : -1;
        state->present &= ~4;
        g_McOpPending = 0;
        state->managerState = 0;
        break;

    case 2:
        if (state->present != 1) {
            MemCard_AbortActiveOperation(state);
            break;
        }
        g_MemCardSearchPatternPtr[2] = port + '0';
        state->fileCount = 0;
        state->selectedSlot = 0;
        state->scanCursor = 0;
        state->hasFiles = 0;
        state->pendingError = 0;
        state->sequence = 0;
        current = MEMCARD_SLOT_EMPTY;
        for (i = 14; i >= 0; i--) {
            state->slots[i].state = current;
        }
        if (firstfile(g_MemCardSearchPatternPtr, &entry) != 0) {
            MemCard_NoteSaveFile(state, &entry);
            while (nextfile(&entry) != 0) {
                MemCard_NoteSaveFile(state, &entry);
            }
            state->retryCount = 0;
        } else {
            state->retryCount--;
        }
        if (state->retryCount > 0) {
            break;
        }
        blocks = state->sequence;
        for (i = 0; i < 15; i++) {
            if (state->slots[i].state == MEMCARD_SLOT_EMPTY) {
                state->slots[i].metadataReady = 1;
            } else {
                blocks--;
            }
        }
        for (i = 14; i >= 0; i--) {
            if (blocks == 0) {
                break;
            }
            if (state->slots[i].state == MEMCARD_SLOT_EMPTY) {
                state->slots[i].state = MEMCARD_SLOT_UNAVAILABLE;
                blocks--;
            }
        }
        state->fileCount = 15;
        if (g_MemCardReadContext == 0) {
            state->managerState = 15;
            g_McOpPending = 0;
            break;
        }
        if (MemCard_CanStartSave(port)) {
            state->managerState = 3;
            state->retryCount = 10;
            MenuWidget_RestoreSavedCurrentNode();
            state->listCursor = Menu_CreateSaveSlotListView(
                state - g_MemCardPortStates, state->fileCount);
            break;
        }
        MenuWidget_RestoreSavedCurrentNode();
        Menu_ShowMemCardErrorDialog(port);
        state->managerState = 12;
        g_McOpPending = 0;
        break;

    case 3:
        if (state->present != 1) {
            MemCard_AbortActiveOperation(state);
            break;
        }
        if (state->scanCursor < state->fileCount * 2) {
            for (; state->scanCursor < state->fileCount * 2;
                 state->scanCursor++) {
                cursor = state->scanCursor;
                candidate = state->listCursor +
                            ((cursor & 1) * 2 - 1) * ((cursor + 1) >> 1);
                if (candidate < 15 &&
                    state->slots[candidate].metadataReady == 0) {
                    break;
                }
            }
            next = state->scanCursor;
            if (next < state->fileCount * 2) {
                slot = state->listCursor +
                       ((next & 1) * 2 - 1) * ((next + 1) >> 1);
            } else {
                slot = 0xFF;
            }
        } else {
            slot = 0xFF;
        }
        state->selectedSlot = slot;
        if (slot != 0xFF) {
            MemCard_FormatSlotFileName(state, slot);
            if ((state->fileDescriptor = open(g_MemCardFilePath, 1)) >= 0) {
                state->managerState = 8;
                state->transferData = g_MemCardPreviewBuffers[port].bytes;
                state->transferSize = 0x80;
                state->retryCount = 30;
                if (state->slots[state->selectedSlot].state ==
                    MEMCARD_SLOT_OCCUPIED) {
                    lseek(state->fileDescriptor, 0x100, 0);
                }
            } else {
                MEMCARD_RETRY_OR_FAIL(state);
            }
        } else {
            state->managerState = 12;
            g_McOpPending = 0;
        }
        break;

    case 4:
        if (state->present != 1) {
            MemCard_AbortActiveOperation(state);
            break;
        }
        state->slots[state->selectedSlot].titleStyleFlag = g_SaveTitleStyleFlag;
        MemCard_FormatSlotFileName(state, state->selectedSlot);
        if (MemCard_BeginWrite(state)) {
            state->fileDescriptor = -1;
            state->managerState = 6;
            state->retryCount = 10;
        } else {
            MEMCARD_RETRY_OR_FAIL(state);
        }
        break;

    case 5:
        if (state->present != 1) {
            MemCard_AbortActiveOperation(state);
            break;
        }
        MemCard_FormatSlotFileName(state, state->selectedSlot);
        if ((state->fileDescriptor = open(g_MemCardFilePath, 1)) >= 0) {
            state->retryCount = 30;
            state->managerState = 7;
        } else {
            MEMCARD_RETRY_OR_FAIL(state);
        }
        break;

    case 6:
        if (state->present != 1) {
            MemCard_AbortActiveOperation(state);
            break;
        }
        MemCard_FormatSlotFileName(state, state->selectedSlot);
        if (card_writes[port].io) {
            state->managerState = 9;
            state->transferData = g_MemCardFileBuffer;
            state->retryCount = 30;
        } else {
            MEMCARD_RETRY_OR_FAIL(state);
        }
        break;

    case 8:
        if (state->present != 1) {
            MemCard_AbortActiveOperation(state);
            break;
        }
        size = state->transferSize;
        if (state->transferSize > 0x80) {
            size = 0x80;
        }
        count = read(state->fileDescriptor, state->transferData, size);
        if (count > 0) {
            state->transferData += count;
            state->transferSize -= count;
            if (state->transferSize <= 0) {
                state->managerState = 10;
            }
        } else {
            MEMCARD_RETRY_OR_FAIL(state);
        }
        break;

    case 7:
        if (state->present != 1) {
            MemCard_AbortActiveOperation(state);
            break;
        }
        size = state->transferSize;
        if (state->transferSize > 0x400) {
            size = 0x400;
        }
        count = read(state->fileDescriptor, state->transferData, size);
        if (count > 0) {
            state->transferData += count;
            state->transferSize -= count;
            if (state->transferSize <= 0) {
                close(state->fileDescriptor);
                state->fileDescriptor = -1;
                state->managerState = 12;
                state->pendingError = 0;
                g_MemCardActiveState = 0;
                g_McOpPending = 0;
                Save_LoadCardFileIntoRuntime();
            }
        } else {
            MEMCARD_RETRY_OR_FAIL(state);
        }
        break;

    case 9:
        if (state->present != 1) {
            MemCard_AbortActiveOperation(state);
            break;
        }
        size = state->transferSize;
        if (state->transferSize > 0x400) {
            size = 0x400;
        }
        count = card_writes[port].io ?
            (int)SDL_WriteIO(card_writes[port].io, state->transferData, size) : -1;
        if (count > 0) {
            state->transferData += count;
            state->transferSize -= count;
            if (state->transferSize <= 0) {
                if (!MemCard_CommitWrite(state)) {
                    state->retryCount = 0;
                    MEMCARD_RETRY_OR_FAIL(state);
                    break;
                }
                state->fileDescriptor = -1;
                state->managerState = 3;
                state->pendingError = 0;
                g_MemCardActiveState = 0;
                Menu_DestroyMemCardProgressWidget();
                Menu_CreateNotificationDialog(0x53, 0);
            }
        } else {
            MEMCARD_RETRY_OR_FAIL(state);
        }
        break;

    case 10:
        if (state->present != 1) {
            MemCard_AbortActiveOperation(state);
            break;
        }
        close(state->fileDescriptor);
        state->fileDescriptor = -1;
        MemCard_ApplySavePreview(&state->slots[state->selectedSlot],
                                 &g_MemCardPreviewBuffers[port].preview);
        state->scanCursor++;
        state->managerState =
            state->scanCursor < state->fileCount * 2 ? 3 : 12;
        if (state->managerState == 12) {
            g_McOpPending = 0;
        }
        break;

    case 11:
        if (state->present != 1) {
            MemCard_AbortActiveOperation(state);
            break;
        }
        MemCard_CancelWrite(state);
        MemCard_FormatSlotFileName(state, state->selectedSlot);
        Psyz_AdjustPath(card_writes[port].previous, g_MemCardFilePath,
                        sizeof(card_writes[port].previous));
        state->managerState = 4;
        state->retryCount = 10;
        break;

    case 12:
        if (state->present != 1) {
            MemCard_AbortActiveOperation(state);
        }
        break;
    }
}

void Save_StartWriteSlot(int port, int arg_slot) {
    MemCardPortState *state;
    MemCardSaveSlot *slot_state;
    int slot;
    u8 formattedSlot;
    int status;
    int sequence;
    int next_state;
    char *path;
    char *formatBuffer;
    int firstSlotLetter;

    slot = arg_slot;
    state = &g_MemCardPortStates[port];
    status = state->present;
    if (status != 1) {
        return;
    }

    slot_state = &state->slots[slot];
    firstSlotLetter = 0x41;
    MemCard_CloseAll();
    formatBuffer = g_MemCardFilePath;
    Save_BuildHeader();

    sequence = state->sequence;
    {
        int next_sequence;

        if (slot_state->state == MEMCARD_SLOT_OCCUPIED) {
            next_sequence = sequence;
        } else {
            next_sequence = sequence + 1;
        }
        path = formatBuffer;
        state->selectedSlot = slot;
        state->sequence = next_sequence;
    }

    formattedSlot = (u8)slot;
    sprintf(path, g_MemCardFilenameFormatPtr,
                    state > g_MemCardPortStates,
                    state->slots[formattedSlot].titleStyleFlag + 0x30,
                    formattedSlot + firstSlotLetter);
    /* Recompute the path address after formatting it. */
    path = g_MemCardFilePath;
    if (Save_BuildCardFile(path) < 0) {
        state->sequence = sequence;
        return;
    }

    state->listCursor = slot;
    state->scanCursor = 0;
    slot_state->metadataReady = 0;
    state->managerState = 1;
    state->pendingError = 1;
    state->transferSize = 0x2000;
    state->retryCount = 10;
    g_MemCardActiveState = state;
    g_MemCardActiveBytesRemaining = 0x2000;

    next_state = 4;
    if (slot_state->state == MEMCARD_SLOT_OCCUPIED) {
        next_state = 0x0B;
    }
    state->nextState = next_state;
    slot_state->state = MEMCARD_SLOT_OCCUPIED;
}

int Save_StartReadSlot(int port, int slot)
{
  u8 *buffer;
  MemCardPortState *state;
  int saved_slot;
  state = &g_MemCardPortStates[port];
  saved_slot = slot;
  if (state->present == 1)
  {
    MemCard_CloseAll();
    buffer = g_MemCardFileBuffer;
    state->transferData = buffer;
    state->transferSize = 0x2000;
    state->retryCount = 10;
    state->pendingError = 2;
    state->managerState = 1;
    state->nextState = 5;
    state->selectedSlot = saved_slot;
    g_MemCardActiveState = state;
    g_MemCardActiveBytesRemaining = 0x2000;
    bzero(buffer, 0x2000);
  }
  return 0;
}

void Save_CancelUiFlow(void) {
    MenuWidget_NavScrollTo(0x26);
    MenuWidget_NavScrollTo(0x25);
    MenuWidget_NavScrollTo(0x24);
    Inv_SetActiveList(0xC, 0);
}

void Menu_CreateNotificationDialog(int arg0, int arg1);
void Menu_SetDeferredCallback(void (*callback)(void));

void Save_LoadCardFileIntoRuntime(void) {
    /* Validate the unchanged file before committing any runtime fields. */
    const size_t crc_offset = 0x100 + 0x12E4 + 0x8A8;
    u32 expected_crc;
    memcpy(&expected_crc, g_MemCardFileBuffer + crc_offset, sizeof(expected_crc));
    u32 crc = 0xFFFF;
    g_MemCardLoadSucceeded = 0;
    for (size_t i = 0; i < sizeof(g_MemCardFileBuffer); ++i) {
        u8 value = i >= crc_offset && i < crc_offset + 4 ? 0 : g_MemCardFileBuffer[i];
        crc ^= (u32)value << 8;
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = crc & 0x8000 ? (crc << 1) ^ 0x1021 : crc << 1;
    }
    if ((~crc & 0xFFFF) == expected_crc &&
        Inv_ValidateSavedInventory((const SaveRuntimeState *)&g_MemCardSaveStateBuffer)) {
        g_SaveIoCursor = g_MemCardFileBuffer + 0x100 + 0x12E4;
        if (Save_DeserializeTail() == 0) {
            memcpy(&g_SaveRuntimeState, &g_MemCardSaveStateBuffer, sizeof(g_SaveRuntimeState));
            g_SaveIoCursor += 4;
            Save_RestoreHeader();
            g_MemCardLoadSucceeded = 1;
            PE_HOOK(PE_HOOK_SAVE_READ, g_MemCardFileBuffer);
            Menu_DestroyMemCardProgressWidget();
            Menu_CreateNotificationDialog(0x54, 0);
            Menu_SetDeferredCallback(Save_CancelUiFlow);
            return;
        }
    }
    Menu_DestroyMemCardProgressWidget();
    Menu_CreateTwoLineDialog(0x55, 0x56);
}

int MemCard_GetActiveProgressBlocks(void) {
    int blocks;
    int result;
    MemCardPortState *active = g_MemCardActiveState;

    if (active != 0) {
        blocks = (g_MemCardActiveBytesRemaining - active->transferSize + 0x400) >> 10;
        result = 8;
        if (blocks < 9) {
            result = blocks;
        }
    } else {
        result = 0;
    }

    return result;
}

/* Byte aliases map fileCount and the first slot fields in MemCardPortState. */

MemCardSaveSlot *MemCard_GetSlot(unsigned int port, int slot) {
    if (port >= 2U) {
        return 0;
    }
    if (slot < 0) {
        return 0;
    }
    if (slot >= g_MemCardPortStates[port].fileCount) {
        return 0;
    }
    if (g_MemCardPortStates[port].slots[slot].metadataReady == 0) {
        return 0;
    }
    return &g_MemCardPortStates[port].slots[slot];
}

void MemCard_InitState(void) {
    for (unsigned port = 0; port < 2; ++port) MemCard_CancelWrite(&g_MemCardPortStates[port]);
    bzero(g_MemCardPortStates, sizeof(MemCardPortState) * 2);
    g_MemCardPortStates[1].fileDescriptor = -1;
    g_MemCardPortStates[0].fileDescriptor = -1;
    g_MemCardServicedPort = 0;
    g_MemCardInfoPollCountdown = 0;
    g_MemCardConnectDebounce = 0;
    g_MemCardConnectPending[1] = 0;
    g_MemCardConnectPending[0] = 0;
    g_McOpPending = 0;
    g_MemCardActiveState = 0;
    g_MemCardActivePortOneBased = 0;
    g_MemCardSavePollTimeout = 0;
    g_MemCardActivePromptPending = 0;
    g_SaveTitleStyleFlag = 0;
    g_MemCardLoadSucceeded = 0;
    g_MemCardReadContext = 0;
}

void MemCard_StepPortState(int arg0);
void Menu_CloseNotificationDialogs(void);
void Menu_NavToSaveConfirmDialog(void);
void MemCard_ClearActivePrompt(void);
void MemCard_StartActivePortRead(void);

void MemCard_UpdateSavePolling(void)
{
    int port;
    MemCardPortState *state;
    int timeout;
    int activeState;

    MemCard_StepPortState(1);
    MemCard_StepPortState(0);
    if (g_MemCardSavePollTimeout != 0) {
        activeState = g_MemCardPortStates[g_MemCardActivePortOneBased - 1].cardState;
        if (activeState != 4 && activeState != 1) {
            g_MemCardSavePollTimeout = -2;
        }
        timeout = g_MemCardSavePollTimeout;
        timeout -= timeout > 0;
        g_MemCardSavePollTimeout = timeout;
        if (timeout <= 0) {
            Menu_DestroyMemCardProgressWidget();
            switch (g_MemCardSavePollTimeout) {
            case 0:
                /* Timed out without an error: offer to read the card again. */
                Menu_CreateNotificationDialog(0x52, 0);
                Menu_SetDeferredCallback(MemCard_StartActivePortRead);
                g_MemCardActivePromptPending = 1;
                break;
            case -1:
                Menu_CreateNotificationDialog(0x3C, 0);
                Menu_SetDeferredCallback(MemCard_ClearActivePrompt);
                g_MemCardActivePromptPending = 1;
                break;
            default:
                MemCard_ClearActivePrompt();
                break;
            }
            g_MemCardSavePollTimeout = 0;
        }
    }

    port = 1;
    state = &g_MemCardPortStates[1];
    do {
        if (g_MemCardActivePortOneBased == port + 1 && !(state->present & 1)) {
            Menu_NavToSaveConfirmDialog();
            Menu_CloseNotificationDialogs();
            MemCard_ClearActivePrompt();
        }
        MemCard_UpdateSaveState(port);
        port--;
        state--;
    } while (port >= 0);
}

int MemCard_IsPortPresent(int port) {
    return g_MemCardPortStates[port].present & 1;
}

void MemCard_CloseAll(void) {
    MemCardPortState *state;

    for (state = g_MemCardPortStates; state < &g_MemCardPortStates[2]; state++) {
        if (state->managerState == 8 || state->managerState == 10) {
            close(state->fileDescriptor);
            state->fileDescriptor = -1;
            state->managerState = 12;
        }
    }
}

void Menu_StepItemGrid2(void);

int MemCard_CheckPresent(int port) {
    int present;

    present = g_MemCardPortStates[port].present & 4;
    if (present != 0) {
        if (g_MemCardActivePortOneBased == 0) {
            g_MemCardActivePortOneBased = port + 1;
            Menu_StepItemGrid2();
        }
    }
    return present == 0;
}

int MemCard_GetActivePort(void)
{
    return g_MemCardActivePortOneBased - 1;
}

void MemCard_MarkActivePortState13(void) {
    int portIndex = g_MemCardActivePortOneBased - 1;
    g_MemCardPortStates[portIndex].managerState = 0xD;
}

void MemCard_ClearActivePrompt(void) {
    g_MemCardActivePortOneBased = 0;
    g_MemCardActivePromptPending = 0;
}

void MenuWidget_SaveAndSetCurrentNode(MenuWidgetNode *arg0);

void MemCard_StartActivePortRead(void) {
    MemCard_StartRead(g_MemCardActivePortOneBased - 1, 1);
    g_MemCardActivePortOneBased = 0;
    g_MemCardActivePromptPending = 0;
}

int MemCard_GetPortSequence(int arg0) {
    return g_MemCardPortStates[arg0].sequence;
}

void MemCard_StartRead(int port, int arg1) {
    MemCardPortState *state = &g_MemCardPortStates[port];

    if (state->managerState == 0 || state->managerState == 12) {
        state->managerState = 1;
        state->nextState = 2;
        state->retryCount = 10;
        MenuWidget_SaveAndSetCurrentNode(0);
        g_MemCardReadContext = arg1;
    }
}

void MemCard_CloseAllAndResetState(void) {
    MemCardPortState *state;

    for (state = g_MemCardPortStates; state < &g_MemCardPortStates[2]; state++) {
        if (state->managerState == 8 || state->managerState == 10) {
            close(state->fileDescriptor);
            state->fileDescriptor = -1;
            state->managerState = 12;
        }
    }

    g_MemCardPortStates[1].managerState = 0;
    g_MemCardPortStates[0].managerState = 0;
    g_McOpPending = 0;
}

int MemCard_IsPortTransferState(int arg0) {
    u8 value = g_MemCardPortStates[arg0].managerState;

    return (value == 3) || (value == 8) || (value == 10);
}

int MemCard_IsOperationPending(void) {
    return g_McOpPending;
}

void MemCard_ClearDelayedCallback(void) {
    g_MemCardDelayedCallback = 0;
    g_MemCardDelayedCallbackTimer = 0;
}

void MemCard_SetDelayedCallback(void (*callback)(void)) {
    g_MemCardDelayedCallback = callback;
    g_MemCardDelayedCallbackTimer = 1;
}

void MemCard_DelayedCallback(void) {
    void (*callback)(void);

    callback = g_MemCardDelayedCallback;
    if (callback != 0) {
        g_MemCardDelayedCallbackTimer++;
        if (g_MemCardDelayedCallbackTimer == 4) {
            callback();
            g_MemCardDelayedCallback = 0;
            g_MemCardDelayedCallbackTimer = 0;
        }
    }
}

int MemCard_HasDelayedCallback(void) {
    return g_MemCardDelayedCallback != 0;
}

long MemCard_OnEventF400Spec0004(void)
{
    g_MemCardEventF400Spec0004Flag = 1;
    return 0;
}

long MemCard_OnEventF400Spec8000(void)
{
    g_MemCardRemovedEventPending = 1;
    return 0;
}

long MemCard_OnEventF400Spec0100(void)
{
    g_MemCardRemovedEventPending = 1;
    return 0;
}

long MemCard_OnEventF400Spec2000(void)
{
    g_MemCardEventF400Spec2000Flag = 1;
    return 0;
}

long MemCard_OnEventF000Spec0004(void)
{
    g_MemCardEventF000Spec0004Flag = 1;
    return 0;
}

long MemCard_OnEventF000Spec8000(void)
{
    g_MemCardEventF000Spec8000Flag = 1;
    return 0;
}

long MemCard_OnEventF000Spec0100(void)
{
    g_MemCardEventF000Spec8000Flag = 1;
    return 0;
}

long MemCard_OnEventF000Spec2000(void)
{
    g_MemCardEventF000Spec2000Flag = 1;
    return 0;
}
