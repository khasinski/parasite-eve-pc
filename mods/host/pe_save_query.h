#ifndef PE_SAVE_QUERY_H
#define PE_SAVE_QUERY_H

#include "pe_save.h"

/* Read-only, game-thread-only diagnostics, outside save/load/check/upgrade/release callbacks.
 * No callbacks, allocation, registration, RNG or save writes. Not an admission guarantee:
 * requery at the eventual commit boundary; these APIs do not veto the existing save writer. */
typedef enum PeSaveSource {
    PE_SAVE_SOURCE_NOT_READ,
    PE_SAVE_SOURCE_NEW_GAME,
    PE_SAVE_SOURCE_EMPTY,
    PE_SAVE_SOURCE_LEGACY,
    PE_SAVE_SOURCE_SUPPORTED,
    PE_SAVE_SOURCE_RECOVERED,    /* bad container CRC; individually valid records salvaged */
    PE_SAVE_SOURCE_CORRUPT,
    PE_SAVE_SOURCE_NEWER,
    PE_SAVE_SOURCE_OPAQUE       /* nonzero tail, no recognized container/fixed blocks */
} PeSaveSource;

typedef struct PeSaveStatus {
    PeSaveSource loaded_source; /* last read or New Game; writing does not erase provenance */
    PeSaveSource carried_source; /* currently carried tail: may be from a later write */
    unsigned writer_api, writer_revision;
    unsigned carried_records, lost_records;
} PeSaveStatus;

typedef enum PeSaveRecordLoad {
    PE_SAVE_RECORD_NOT_READ,
    PE_SAVE_RECORD_MISSING,
    PE_SAVE_RECORD_LOADED,
    PE_SAVE_RECORD_UPGRADED,
    PE_SAVE_RECORD_REJECTED,    /* check/upgrade/layout failed; existing loader reset it */
    PE_SAVE_RECORD_NEWER,       /* existing loader retained the newer layout */
    PE_SAVE_RECORD_UNCHECKED,   /* carried, but not checked by a running owner */
    PE_SAVE_RECORD_UNAVAILABLE  /* unknown/damaged/newer container: absence cannot be proven */
} PeSaveRecordLoad;

enum {
    PE_SAVE_RECORD_REGISTERED = 1u,
    PE_SAVE_RECORD_CARRIED = 2u,
    PE_SAVE_RECORD_LIVE = 4u,    /* owner active or release-state will be saved from memory */
    PE_SAVE_RECORD_KEPT = 8u,
    PE_SAVE_RECORD_RELEASE_PENDING = 16u,
    PE_SAVE_RECORD_CONDITIONAL = 32u,
    PE_SAVE_RECORD_EMISSION_ENABLED = 64u /* sticky policy bit, distinct from live/kept */
};
typedef struct PeSaveRecordStatus {
    uint32_t id;
    PeSaveRecordLoad load;      /* last owner's load outcome, not a new validation */
    unsigned flags;
    unsigned registered_version, registered_size;
    unsigned carried_version, carried_size;
} PeSaveRecordStatus;

/* A hypothetical active registration, never installed. Same registered name replaces its
 * layout for the projection; a new name consumes a registration slot. Payload/check/upgrade
 * are deliberately absent, so a fitting projection says nothing about semantic validity.
 * A newer carried layout still wins, just as it does in the existing loader. Known conditional
 * registrations retain their zero dormant byte reservation, but the candidate projects them
 * active. A new name still represents an ordinary always-emitted registration. */
typedef struct PeSaveCandidate {
    const char *name;
    unsigned version, size;
} PeSaveCandidate;

enum {
    PE_SAVE_CAPACITY_BYTES = 1u,
    PE_SAVE_CAPACITY_ENTRIES = 2u,
    PE_SAVE_CAPACITY_REGISTRATIONS = 4u,
    PE_SAVE_CAPACITY_NEWER_CONTAINER = 8u,
    PE_SAVE_CAPACITY_UNCERTAIN_SOURCE = 16u,
    PE_SAVE_CAPACITY_LAYOUT_CHECK = 32u,
    PE_SAVE_CAPACITY_HASH_COLLISION = 64u
};
typedef struct PeSaveCapacity {
    unsigned capacity_bytes, entry_limit, registration_limit;
    unsigned registered_records, live_records, carried_records;
    unsigned required_bytes, required_records; /* complete union, including records that won't fit */
    unsigned selected_bytes, selected_records, dropped_records; /* greedy projection; logical header
                                                               * included even for an empty union */
    unsigned remaining_bytes, reasons;
    int fits;                  /* known byte/count fit only; reasons still require review */
    int passthrough;           /* current writer retains opaque/foreign/no-record tail */
} PeSaveCapacity;

/* 0 success, -1 invalid arguments (output left unchanged). Borrow no returned pointers.
 * Unregistered names can inspect carried data, but hashed IDs alone cannot prove its name.
 * With damage and no surviving entry, UNAVAILABLE is conservative, never guessed MISSING. */
int pe_save_query_status(PeSaveStatus *out);
int pe_save_query_record(const char *name, PeSaveRecordStatus *out);
int pe_save_query_capacity(const PeSaveCandidate *candidate, PeSaveCapacity *out);

#endif
