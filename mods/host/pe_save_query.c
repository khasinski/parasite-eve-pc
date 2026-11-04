/* Read-only capacity projection. State/lifecycle remain in pe_save.c; packing and this query
 * use the same private selector, including the legacy greedy carried-record policy. */
#include "pe_save_plan.h"

static uint32_t pe_query_id(const char *name) {
    uint32_t id = 0x811C9DC5u;
    for (; *name; ++name) id = (id ^ (uint8_t)*name) * 0x01000193u;
    return id;
}

int pe_save_query_capacity(const PeSaveCandidate *candidate, PeSaveCapacity *out) {
    PeSaveCapacityInput input;
    PeSaveCapacity result;
    PeSaveEntry live[PE_SAVE_MAX_RECORDS + 1];
    PeSavePlan plan;
    uint32_t id = 0;
    unsigned i, n = 0;
    int found = 0, keep_newer = 0;
    if (!out || (candidate && (!candidate->name || !*candidate->name ||
        !candidate->version || candidate->version > 0xFFFFu || !candidate->size ||
        candidate->size > PE_SAVE_ROOM - PE_SAVE_HEADER_SIZE - PE_SAVE_RECORD_HEADER_SIZE))) return -1;
    pe_save_capacity_input(&input);
    memset(&result, 0, sizeof result);
    result.capacity_bytes = PE_SAVE_ROOM;
    result.entry_limit = PE_SAVE_MAX_ENTRIES;
    result.registration_limit = PE_SAVE_MAX_RECORDS;
    result.registered_records = input.registered_count;
    result.carried_records = input.carried_count;
    if (candidate) id = pe_query_id(candidate->name);
    for (i = 0; i < input.registered_count; ++i) {
        if (candidate && input.registered[i].entry.id == id) {
            found = 1;
            if (strcmp(input.registered[i].name, candidate->name))
                result.reasons |= PE_SAVE_CAPACITY_HASH_COLLISION;
            if (input.registered[i].entry.version != candidate->version ||
                input.registered[i].entry.size != candidate->size)
                result.reasons |= PE_SAVE_CAPACITY_LAYOUT_CHECK;
            continue; /* hypothetical active layout replaces this registered ID */
        }
        if (input.registered[i].live && !input.registered[i].kept)
            live[n++] = input.registered[i].entry;
    }
    if (candidate) {
        if (!found) ++result.registered_records;
        for (i = 0; i < input.carried_count; ++i) {
            if (input.carried[i].id != id) continue;
            result.reasons |= PE_SAVE_CAPACITY_LAYOUT_CHECK;
            if (input.carried[i].version > candidate->version) keep_newer = 1;
        }
        /* The existing loader keeps a newer carried layout even when its owner is active.
         * Do not replace it with the proposed older/default-sized memory in this projection. */
        if (!keep_newer)
            live[n++] = (PeSaveEntry){id, (uint16_t)candidate->version, (uint16_t)candidate->size, NULL};
    }
    /* Hypothetical registration can exceed the registered-byte limit. The shared selector
     * still counts those bytes safely (at most 33 entries); this is not a writable plan. */
    pe_save_plan_build(live, n, input.carried, input.carried_count, &plan);
    result.live_records = n;
    result.required_bytes = plan.required_bytes;
    result.required_records = plan.required_count;
    result.selected_bytes = plan.bytes;
    result.selected_records = plan.count;
    result.dropped_records = plan.dropped;
    if (plan.required_bytes > PE_SAVE_ROOM) result.reasons |= PE_SAVE_CAPACITY_BYTES;
    if (plan.required_count > PE_SAVE_MAX_ENTRIES) result.reasons |= PE_SAVE_CAPACITY_ENTRIES;
    if (result.registered_records > PE_SAVE_MAX_RECORDS)
        result.reasons |= PE_SAVE_CAPACITY_REGISTRATIONS;
    if (candidate) {
        unsigned registered_bytes = PE_SAVE_HEADER_SIZE;
        for (i = 0; i < input.registered_count; ++i) {
            if (input.registered[i].conditional) continue;
            registered_bytes += PE_SAVE_RECORD_HEADER_SIZE +
                (input.registered[i].entry.id == id ? candidate->size : input.registered[i].entry.size);
        }
        if (!found) registered_bytes += PE_SAVE_RECORD_HEADER_SIZE + candidate->size;
        if (registered_bytes > PE_SAVE_ROOM) result.reasons |= PE_SAVE_CAPACITY_REGISTRATIONS;
    }
    if (input.status.carried_source == PE_SAVE_SOURCE_NEWER)
        result.reasons |= PE_SAVE_CAPACITY_NEWER_CONTAINER;
    if (input.status.loaded_source == PE_SAVE_SOURCE_CORRUPT ||
        input.status.loaded_source == PE_SAVE_SOURCE_RECOVERED ||
        input.status.loaded_source == PE_SAVE_SOURCE_OPAQUE ||
        input.status.carried_source == PE_SAVE_SOURCE_CORRUPT ||
        input.status.carried_source == PE_SAVE_SOURCE_RECOVERED ||
        input.status.carried_source == PE_SAVE_SOURCE_OPAQUE)
        result.reasons |= PE_SAVE_CAPACITY_UNCERTAIN_SOURCE;
    result.passthrough = input.status.carried_source == PE_SAVE_SOURCE_NEWER || !plan.count;
    result.fits = !(result.reasons & (PE_SAVE_CAPACITY_BYTES | PE_SAVE_CAPACITY_ENTRIES |
        PE_SAVE_CAPACITY_REGISTRATIONS | PE_SAVE_CAPACITY_NEWER_CONTAINER |
        PE_SAVE_CAPACITY_UNCERTAIN_SOURCE | PE_SAVE_CAPACITY_HASH_COLLISION));
    if (result.fits) result.remaining_bytes = PE_SAVE_ROOM - plan.required_bytes;
    /* A foreign container is passed through, not replaced by the hypothetical layouts.
     * Its unrecognized records cannot be counted, even if its header advertises a length. */
    if (result.reasons & PE_SAVE_CAPACITY_NEWER_CONTAINER) {
        result.required_bytes = result.required_records = 0;
        result.selected_bytes = result.selected_records = result.dropped_records = 0;
    }
    *out = result;
    return 0;
}
