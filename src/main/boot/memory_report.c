#include "pe1/memory_budget.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const s_counter_names[PE_MEM_COUNTER_COUNT] = {
    "transform stack depth",
    "room texture read",
    "room scratch read",
    "room asset read",
    "mesh packet set",
};

static size_t s_peaks[PE_MEM_COUNTER_COUNT];
static int s_room;
static int s_enabled = -1;

static int PeMemory_Enabled(void) {
    if (s_enabled < 0) {
        const char *value = getenv("PE_MEMORY_REPORT");
        s_enabled = value && *value && strcmp(value, "0") != 0;
    }
    return s_enabled;
}

void PeMemory_SetRoom(int room) {
    s_room = room;
}

void PeMemory_Note(PeMemoryCounter counter, size_t value, size_t capacity) {
    if ((unsigned)counter >= PE_MEM_COUNTER_COUNT || value <= s_peaks[counter]) return;
    s_peaks[counter] = value;
    if (!PeMemory_Enabled()) return;
    char room[16] = "";
    if (s_room > 0) snprintf(room, sizeof(room), " (M%03d)", s_room);
    if (capacity) {
        fprintf(stderr, "[mem] new peak: %s = %zu of %zu%s%s\n",
                s_counter_names[counter], value, capacity, room,
                value > capacity ? " OVER CAPACITY" : "");
    } else {
        fprintf(stderr, "[mem] new peak: %s = %zu%s\n", s_counter_names[counter], value, room);
    }
    fflush(stderr);
}
