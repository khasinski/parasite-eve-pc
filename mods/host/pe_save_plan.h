#ifndef PE_SAVE_PLAN_H
#define PE_SAVE_PLAN_H

/* Host-private, allocation-free selection shared by packing and capacity inspection.
 * Inputs have unique IDs in each list, as guaranteed by registration/parser. */
#include "pe_save_query.h"
#include <string.h>

#define PE_SAVE_MAX_RECORDS 32
#define PE_SAVE_MAX_ENTRIES 64

typedef struct PeSaveEntry {
    uint32_t id;
    uint16_t version, size;
    const uint8_t *bytes;
} PeSaveEntry;

typedef struct PeSavePlan {
    PeSaveEntry entries[PE_SAVE_MAX_ENTRIES];
    unsigned count, bytes, required_count, required_bytes, dropped;
} PeSavePlan;

static inline void pe_save_plan_build(const PeSaveEntry *live, unsigned live_count,
                                     const PeSaveEntry *carried, unsigned carried_count,
                                     PeSavePlan *out) {
    unsigned i, j;
    memset(out, 0, sizeof *out);
    out->bytes = out->required_bytes = PE_SAVE_HEADER_SIZE;
    for (i = 0; i < live_count; ++i) {
        out->entries[out->count++] = live[i]; /* registered live layouts already fit */
        out->bytes += PE_SAVE_RECORD_HEADER_SIZE + live[i].size;
    }
    out->required_count = live_count;
    out->required_bytes = out->bytes;
    for (i = 0; i < carried_count; ++i) {
        for (j = 0; j < live_count && live[j].id != carried[i].id; ++j) {}
        if (j < live_count) continue;
        ++out->required_count;
        out->required_bytes += PE_SAVE_RECORD_HEADER_SIZE + carried[i].size;
        if (out->count == PE_SAVE_MAX_ENTRIES ||
            out->bytes + PE_SAVE_RECORD_HEADER_SIZE + carried[i].size > PE_SAVE_ROOM) {
            ++out->dropped;
            continue;
        }
        out->entries[out->count++] = carried[i];
        out->bytes += PE_SAVE_RECORD_HEADER_SIZE + carried[i].size;
    }
    for (i = 1; i < out->count; ++i)
        for (j = i; j > 0 && out->entries[j - 1].id > out->entries[j].id; --j) {
            PeSaveEntry t = out->entries[j];
            out->entries[j] = out->entries[j - 1];
            out->entries[j - 1] = t;
        }
}

typedef struct PeSaveCapacityInput {
    PeSaveStatus status;
    unsigned registered_count, carried_count;
    struct {
        const char *name;
        PeSaveEntry entry;
        int live, kept, conditional;
    } registered[PE_SAVE_MAX_RECORDS];
    PeSaveEntry carried[PE_SAVE_MAX_ENTRIES];
} PeSaveCapacityInput;

/* Internal read-only snapshot of selection metadata; borrowed entry pointers never escape
 * the game-thread query. No owner validators/resetters are invoked. */
void pe_save_capacity_input(PeSaveCapacityInput *out);

#endif
