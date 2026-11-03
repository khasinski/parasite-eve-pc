/* Mods' per-save data, kept by the host. See pe_save.h. */
#include <stdio.h>
#include <string.h>

#include "pe_game.h"
#include "pe_mod_host.h"
#include "pe_save.h"
#include "pe_save_plan.h"
#include "pe_save_dispatch.h"
#include "pe_save_emission.h"

#define PE_SAVE_MAGIC "PEMC"
#define PE_SAVE_FORMAT 1
/* The writer marker's revision of how this host writes saves; raise it when that changes, so later
 * builds can tell which saves they may treat differently. 1: format 1, the old blocks' window kept. */
#define PE_SAVE_WRITER 1

/* A parsed tail: who wrote it (0, 0 for no container) and what damage cost. */
typedef struct {
    unsigned api, writer;   /* writer marker: mod API version, PE_SAVE_WRITER */
    int damaged, lost;      /* container CRC failed; records dropped (failed their own CRC) */
} PeSaveInfo;

/* Registered records: the running mods' state. kept = the loaded save holds a newer layout than
 * this build reads, so its carried bytes are written back instead of the (reset) state. released =
 * its mod is off and its record was loaded for release(): it is saved from memory like a live one.
 * release_due = release() hasn't run yet: it runs on the first free field frame after the load,
 * once the room has settled the game's state (the bag's size counts the armor's bag mods only
 * then); until it runs the record is saved as loaded. */
static struct {
    const PeSaveRecord *record;
    const char *mod_id;
    uint32_t id;
    int kept, released, release_due;
    PeSaveRecordLoad load;
    int conditional, emitting, initialized;
} s_reg[PE_SAVE_MAX_RECORDS];
static int s_reg_count;
static unsigned s_reg_bytes = PE_SAVE_HEADER_SIZE;
static int s_write_dispatch, s_lifecycle_dispatch;

/* Carried: the records of the save loaded or last written, pointing into s_tail. foreign = the
 * loaded tail is a container format newer than this build: s_tail is written back as it is. */
static uint8_t s_tail[PE_SAVE_TAIL_SIZE], s_lin[PE_SAVE_ROOM];
static PeSaveEntry s_car[PE_SAVE_MAX_ENTRIES];
static int s_car_count, s_foreign;
static PeSaveSource s_loaded_source, s_carried_source;
static PeSaveInfo s_car_info;   /* the loaded or last written tail's writer and damage */

/* Saves from before: each mod's fixed block, read as a version 0 record holding the block. */
static const struct { const char *name; unsigned at, size; char magic[5]; } s_legacy[] = {
    { "pe_economy.loot", 0x000, 0x020, "PELT" },
    { "pe_economy.catalog", 0x040, 0x148, "PECT" },
    { "pe_economy.shop", 0x1C0, 0x014, "PESH" },
    { "pe_qol.keys", 0x200, 0x054, "PKEY" },
    { "pe_qol.sort", 0x260, 0x028, "SORT" },
};
#define PE_LEGACY_COUNT ((int)(sizeof s_legacy / sizeof s_legacy[0]))

/* Saved from memory: its mod runs, or it is off and its record was released. */
static int pe_save_live(int i) {
    return (!s_reg[i].conditional || s_reg[i].emitting) &&
           (pe_mod_active(s_reg[i].mod_id) || s_reg[i].released);
}

static uint32_t pe_fnv(const char *name) {
    uint32_t h = 0x811C9DC5u;
    for (; *name; ++name) h = (h ^ (uint8_t)*name) * 0x01000193u;
    return h;
}

static uint32_t pe_crc32_add(uint32_t crc, const uint8_t *p, unsigned n) {
    unsigned i, bit;
    for (i = 0; i < n; ++i) {
        crc ^= p[i];
        for (bit = 0; bit < 8; ++bit) crc = crc & 1u ? (crc >> 1) ^ 0xEDB88320u : crc >> 1;
    }
    return crc;
}
static uint32_t pe_crc32(const uint8_t *p, unsigned n) { return ~pe_crc32_add(0xFFFFFFFFu, p, n); }

static uint16_t pe_get16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t pe_get32(const uint8_t *p) { return (uint32_t)pe_get16(p) | (uint32_t)pe_get16(p + 2) << 16; }
static void pe_put16(uint8_t *p, unsigned v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void pe_put32(uint8_t *p, uint32_t v) { pe_put16(p, v & 0xFFFFu); pe_put16(p + 2, v >> 16); }

static const char *pe_name_of(uint32_t id, const char **mod_id) {
    int i;
    for (i = 0; i < s_reg_count; ++i)
        if (s_reg[i].id == id) {
            if (mod_id) *mod_id = s_reg[i].mod_id;
            return s_reg[i].record->name;
        }
    for (i = 0; i < PE_LEGACY_COUNT; ++i)
        if (pe_fnv(s_legacy[i].name) == id) {
            if (mod_id) *mod_id = NULL;
            return s_legacy[i].name;
        }
    if (mod_id) *mod_id = NULL;
    return NULL;
}

/* The container's bytes run through the tail around the reserved window (pe_save.h). */
static void pe_gather(const uint8_t *tail, uint8_t *lin) {
    memcpy(lin, tail, PE_SAVE_WINDOW_AT);
    memcpy(lin + PE_SAVE_WINDOW_AT, tail + PE_SAVE_WINDOW_END, PE_SAVE_TAIL_SIZE - PE_SAVE_WINDOW_END);
}
static void pe_scatter(const uint8_t *lin, uint8_t *tail) {
    memcpy(tail, lin, PE_SAVE_WINDOW_AT);
    memcpy(tail + PE_SAVE_WINDOW_END, lin + PE_SAVE_WINDOW_AT, PE_SAVE_TAIL_SIZE - PE_SAVE_WINDOW_END);
}
static int pe_in_window(int i) { return s_legacy[i].at >= PE_SAVE_WINDOW_AT && s_legacy[i].at < PE_SAVE_WINDOW_END; }

/* The CRC a record carries: its id, version and size fields, then its bytes. */
static uint32_t pe_record_crc(const uint8_t *header, const uint8_t *bytes, unsigned size) {
    return ~pe_crc32_add(pe_crc32_add(0xFFFFFFFFu, header, 8), bytes, size);
}

/* The records in a tail: a container (its bytes gathered into lin), else the old fixed blocks.
 * -1: a damaged container header; -2: a container in a format (or with flags) newer than this
 * build. info gets the writer's marker and how many records were lost to damage. */
static int pe_parse(const uint8_t *tail, uint8_t *lin, PeSaveEntry *out, PeSaveInfo *info) {
    unsigned total, at, size;
    uint32_t crc;
    int n = 0, i, j, whole;
    memset(info, 0, sizeof *info);
    if (!memcmp(tail, PE_SAVE_MAGIC, 4)) {
        total = pe_get16(tail + 6);
        if (tail[4] > PE_SAVE_FORMAT || (tail[4] == PE_SAVE_FORMAT && tail[5] != 0)) return -2;
        if (tail[4] != PE_SAVE_FORMAT || total < PE_SAVE_HEADER_SIZE || total > PE_SAVE_ROOM) return -1;
        pe_gather(tail, lin);
        crc = pe_get32(lin + 8);
        memset(lin + 8, 0, 4);
        whole = pe_crc32(lin, total) == crc;
        pe_put32(lin + 8, crc);
        info->api = pe_get16(lin + 16);
        info->writer = pe_get16(lin + 18);
        /* Each record carries its own CRC: when the container's doesn't match (another build wrote
         * over part of it), the records that still match are kept and only the others are lost. */
        for (at = PE_SAVE_HEADER_SIZE; at < total;) {
            const uint8_t *h = lin + at;
            if (total - at < PE_SAVE_RECORD_HEADER_SIZE || (size = pe_get16(h + 6)) > total - at - PE_SAVE_RECORD_HEADER_SIZE) {
                ++info->lost;            /* the rest can't be walked */
                break;
            }
            at += PE_SAVE_RECORD_HEADER_SIZE + size;
            for (i = 0; i < n && out[i].id != pe_get32(h); ++i) {}
            if (n == PE_SAVE_MAX_ENTRIES || i < n ||
                pe_record_crc(h, h + PE_SAVE_RECORD_HEADER_SIZE, size) != pe_get32(h + 8)) {
                ++info->lost;
                continue;
            }
            out[n].id = pe_get32(h);
            out[n].version = pe_get16(h + 4);
            out[n].size = (uint16_t)size;
            out[n].bytes = h + PE_SAVE_RECORD_HEADER_SIZE;
            ++n;
        }
        if (whole && info->lost) return -1;   /* the container's CRC matched bytes that don't parse */
        info->damaged = !whole;
        /* The window changed since this container was written: a build that still writes the old
         * blocks saved here since. Its blocks are newer than our records of them. */
        if (pe_crc32(tail + PE_SAVE_WINDOW_AT, PE_SAVE_WINDOW_SIZE) != pe_get32(lin + 12))
            for (i = 0; i < PE_LEGACY_COUNT; ++i) {
                uint32_t id = pe_fnv(s_legacy[i].name);
                if (!pe_in_window(i) || memcmp(tail + s_legacy[i].at, s_legacy[i].magic, 4)) continue;
                for (j = 0; j < n && out[j].id != id; ++j) {}
                if (j == n) {
                    if (n == PE_SAVE_MAX_ENTRIES) continue;
                    ++n;
                }
                out[j].id = id;
                out[j].version = 0;
                out[j].size = (uint16_t)s_legacy[i].size;
                out[j].bytes = tail + s_legacy[i].at;
            }
        return n;
    }
    for (i = 0; i < PE_LEGACY_COUNT; ++i)
        if (!memcmp(tail + s_legacy[i].at, s_legacy[i].magic, 4)) {
            out[n].id = pe_fnv(s_legacy[i].name);
            out[n].version = 0;
            out[n].size = (uint16_t)s_legacy[i].size;
            out[n].bytes = tail + s_legacy[i].at;
            ++n;
        }
    return n;
}

static const PeSaveEntry *pe_carried(uint32_t id) {
    int i;
    for (i = 0; i < s_car_count; ++i)
        if (s_car[i].id == id) return &s_car[i];
    return NULL;
}

/* The window a save made now holds: each old block as its owner just wrote it into live (the
 * save's own tail) while that owner runs, else as carried from the loaded save. */
static void pe_window(uint8_t *win, const uint8_t *live) {
    int i, j;
    memcpy(win, s_tail + PE_SAVE_WINDOW_AT, PE_SAVE_WINDOW_SIZE);
    if (!live) return;
    for (i = 0; i < PE_LEGACY_COUNT; ++i) {
        uint32_t id = pe_fnv(s_legacy[i].name);
        if (!pe_in_window(i)) continue;
        for (j = 0; j < s_reg_count && s_reg[j].id != id; ++j) {}
        if (j < s_reg_count && pe_save_live(j))
            memcpy(win + s_legacy[i].at - PE_SAVE_WINDOW_AT, live + s_legacy[i].at, s_legacy[i].size);
    }
}

/* One plan for normal admission, serialization and snapshots. No owner code runs here. */
static void pe_record_plan(int enable, PeSavePlan *out) {
    PeSaveEntry live[PE_SAVE_MAX_RECORDS];
    unsigned n = 0;
    int i;
    for (i = 0; i < s_reg_count; ++i) {
        if (s_reg[i].kept || (i != enable && !pe_save_live(i))) continue;
        const PeSaveRecord *r = s_reg[i].record;
        live[n++] = (PeSaveEntry){s_reg[i].id, (uint16_t)r->version,
                                 (uint16_t)r->size, r->data};
    }
    pe_save_plan_build(live, n, s_car, (unsigned)s_car_count, out);
}

static void pe_current_plan(PeSavePlan *out) { pe_record_plan(-1, out); }

int pe_save_enable_record(const PeSaveRecord *record) {
    PeSavePlan plan;
    int i;
    if (!record) return PE_SAVE_EMISSION_BAD_ARGUMENT;
    if (s_write_dispatch || s_lifecycle_dispatch) return PE_SAVE_EMISSION_BUSY;
    for (i = 0; i < s_reg_count && s_reg[i].record != record; ++i) {}
    if (i == s_reg_count) return PE_SAVE_EMISSION_BAD_ARGUMENT;
    if (!s_reg[i].conditional) return PE_SAVE_EMISSION_NOT_CONDITIONAL;
    if (!pe_mod_active(s_reg[i].mod_id)) return PE_SAVE_EMISSION_INACTIVE;
    if (!s_reg[i].initialized || s_foreign || s_reg[i].kept || s_reg[i].load == PE_SAVE_RECORD_REJECTED ||
        s_reg[i].load == PE_SAVE_RECORD_UNAVAILABLE ||
        s_loaded_source == PE_SAVE_SOURCE_NOT_READ ||
        s_loaded_source == PE_SAVE_SOURCE_RECOVERED ||
        s_loaded_source == PE_SAVE_SOURCE_CORRUPT ||
        s_loaded_source == PE_SAVE_SOURCE_OPAQUE)
        return PE_SAVE_EMISSION_SOURCE_UNAVAILABLE;
    pe_record_plan(i, &plan);
    if (plan.required_bytes > PE_SAVE_ROOM || plan.required_count > PE_SAVE_MAX_ENTRIES)
        return PE_SAVE_EMISSION_CAPACITY;
    s_reg[i].emitting = 1;
    return PE_SAVE_EMISSION_OK;
}

static int pe_admit_write(void) {
    PeSavePlan plan;
    /* This host cannot rewrite a foreign format: it preserves the complete tail verbatim,
     * just as before. Active mods' new progress is not serialized in that mode. */
    if (s_foreign) return PE_SAVE_WRITE_OK;
    pe_current_plan(&plan);
    return plan.required_bytes > PE_SAVE_ROOM || plan.required_count > PE_SAVE_MAX_ENTRIES
        ? PE_SAVE_WRITE_CAPACITY : PE_SAVE_WRITE_OK;
}

int pe_save_check_write(void) {
    return s_write_dispatch || s_lifecycle_dispatch ? PE_SAVE_WRITE_BUSY : pe_admit_write();
}

/* The records a save made now holds, into out (a whole tail): running mods' state first, then
 * carried records (mods off, newer layouts, mods not in this build) while room is left, around
 * the window (live: the save's own tail, whose window the old blocks' owners wrote; NULL: the
 * carried window). Reads memory only, so the crash report can use it. Returns the record count. */
static int pe_pack(uint8_t *out, const uint8_t *live, int *dropped) {
    PeSavePlan plan;
    uint8_t lin[PE_SAVE_ROOM], win[PE_SAVE_WINDOW_SIZE];
    unsigned total, at;
    int n, i;
    *dropped = 0;
    pe_window(win, live);            /* first: live may be out itself */
    if (s_foreign) {
        memcpy(out, s_tail, PE_SAVE_TAIL_SIZE);
        memcpy(out + PE_SAVE_WINDOW_AT, win, sizeof win);
        return 0;
    }
    pe_current_plan(&plan);
    n = (int)plan.count;
    total = plan.bytes;
    *dropped = (int)plan.dropped;
    /* No participating records: retain the original tail byte for byte,
     * including data from writers this host does not understand. */
    if (!n) {
        memcpy(out, s_tail, PE_SAVE_TAIL_SIZE);
        memcpy(out + PE_SAVE_WINDOW_AT, win, sizeof win);
        return 0;
    }
    memset(lin, 0, sizeof lin);
    memcpy(lin, PE_SAVE_MAGIC, 4);
    lin[4] = PE_SAVE_FORMAT;
    pe_put16(lin + 6, total);
    pe_put32(lin + 12, pe_crc32(win, sizeof win));
    pe_put16(lin + 16, PE_MOD_API_VERSION);   /* the writer marker */
    pe_put16(lin + 18, PE_SAVE_WRITER);
    for (i = 0, at = PE_SAVE_HEADER_SIZE; i < n; ++i) {
        pe_put32(lin + at, plan.entries[i].id);
        pe_put16(lin + at + 4, plan.entries[i].version);
        pe_put16(lin + at + 6, plan.entries[i].size);
        memcpy(lin + at + PE_SAVE_RECORD_HEADER_SIZE, plan.entries[i].bytes, plan.entries[i].size);
        pe_put32(lin + at + 8, pe_record_crc(lin + at, plan.entries[i].bytes, plan.entries[i].size));
        at += PE_SAVE_RECORD_HEADER_SIZE + plan.entries[i].size;
    }
    pe_put32(lin + 8, pe_crc32(lin, total));
    memset(out, 0, PE_SAVE_TAIL_SIZE);
    pe_scatter(lin, out);
    memcpy(out + PE_SAVE_WINDOW_AT, win, sizeof win);
    return n;
}

static PeSaveSource pe_source(int parsed, const PeSaveInfo *info) {
    unsigned i;
    if (parsed == -2) return PE_SAVE_SOURCE_NEWER;
    if (parsed < 0) return PE_SAVE_SOURCE_CORRUPT;
    if (!memcmp(s_tail, PE_SAVE_MAGIC, 4))
        return info->damaged ? PE_SAVE_SOURCE_RECOVERED : PE_SAVE_SOURCE_SUPPORTED;
    if (parsed) return PE_SAVE_SOURCE_LEGACY;
    for (i = 0; i < sizeof s_tail; ++i) if (s_tail[i]) return PE_SAVE_SOURCE_OPAQUE;
    return PE_SAVE_SOURCE_EMPTY;
}

static void pe_carry(const uint8_t *tail, int loading) {
    PeSaveInfo info;
    int n;
    if (tail != s_tail) memcpy(s_tail, tail, sizeof s_tail);
    n = pe_parse(s_tail, s_lin, s_car, &info);
    s_car_count = n < 0 ? 0 : n;
    s_foreign = n == -2;
    s_car_info = info;
    s_carried_source = pe_source(n, &info);
    if (loading) s_loaded_source = s_carried_source;
    if (loading && n >= 0 && info.api)
        pe_mod_log(NULL, "save: mods' data written by mod API %u, save writer %u; %d record(s)", info.api, info.writer, n);
    if (n >= 0 && info.damaged)
        pe_mod_log(NULL, "save: the mods' save data was partly overwritten: %d record(s) kept, %d lost (their mods "
                   "start from defaults)", n, info.lost);
    if (n == -1) pe_mod_log(NULL, "save: the mods' save data is damaged; mods start from their defaults");
    if (n == -2) {
        pe_mod_log(NULL, "save: the mods' save data is format %u, newer than this build's %u: kept as it is, "
                   "mods start from their defaults and their progress isn't saved", (unsigned)s_tail[4], PE_SAVE_FORMAT);
        pe_toast("This save's mod data is from a newer build: kept, but mods start fresh");
    }
}

static void pe_reset(int i) {
    const PeSaveRecord *r = s_reg[i].record;
    if (r->reset) r->reset(r->data);
    else memset(r->data, 0, r->size);
}

static int pe_call_check(int i) {
    const PeSaveRecord *r = s_reg[i].record;
    return r->check ? r->check(r->data) : 0;
}

static void pe_load_one(int i) {
    const PeSaveRecord *r = s_reg[i].record;
    const PeSaveEntry *e = pe_carried(s_reg[i].id);
    const char *mod = s_reg[i].mod_id;
    s_reg[i].kept = 0;
    s_reg[i].initialized = 1;
    s_reg[i].load = PE_SAVE_RECORD_MISSING;
    if (!e) {
        if (s_foreign || s_loaded_source == PE_SAVE_SOURCE_CORRUPT ||
            s_loaded_source == PE_SAVE_SOURCE_RECOVERED || s_loaded_source == PE_SAVE_SOURCE_OPAQUE)
            s_reg[i].load = PE_SAVE_RECORD_UNAVAILABLE;
        pe_reset(i);
    } else if (e->version == r->version && e->size == r->size) {
        s_reg[i].load = PE_SAVE_RECORD_LOADED;
        memcpy(r->data, e->bytes, r->size);
        if (pe_call_check(i)) {
            s_reg[i].load = PE_SAVE_RECORD_REJECTED;
            pe_mod_log(mod, "save: %s didn't pass its checks, reset", r->name);
            pe_reset(i);
        }
    } else if (e->version < r->version && r->upgrade) {
        if (r->upgrade(r->data, e->bytes, e->size, e->version) || pe_call_check(i)) {
            s_reg[i].load = PE_SAVE_RECORD_REJECTED;
            pe_mod_log(mod, "save: %s v%u couldn't be read, reset", r->name, (unsigned)e->version);
            pe_reset(i);
        } else {
            s_reg[i].load = PE_SAVE_RECORD_UPGRADED;
            pe_mod_log(mod, "save: %s upgraded from v%u", r->name, (unsigned)e->version);
        }
    } else if (e->version > r->version) {
        s_reg[i].load = PE_SAVE_RECORD_NEWER;
        pe_mod_log(mod, "save: %s is v%u, newer than this build's v%u: kept as is, mod starts from defaults",
                   r->name, (unsigned)e->version, r->version);
        pe_reset(i);
        s_reg[i].kept = 1;
    } else {
        s_reg[i].load = PE_SAVE_RECORD_REJECTED;
        pe_mod_log(mod, "save: %s v%u (%u bytes) doesn't match v%u (%u bytes), reset", r->name,
                   (unsigned)e->version, (unsigned)e->size, r->version, r->size);
        pe_reset(i);
    }
    if (s_reg[i].conditional && (s_reg[i].load == PE_SAVE_RECORD_REJECTED ||
        s_reg[i].load == PE_SAVE_RECORD_NEWER || s_reg[i].load == PE_SAVE_RECORD_UNAVAILABLE)) {
        s_reg[i].initialized = 0;
        if (e) s_reg[i].kept = 1; /* preserve valid-CRC rejected/newer optional bytes */
    }

}

static int pe_register_record(const PeSaveRecord *r, int conditional) {
    const char *mod = pe_mod_current();
    uint32_t id;
    int i;
    if (s_write_dispatch || s_lifecycle_dispatch) {
        pe_mod_log(mod, "save: registration during save/load lifecycle refused");
        return -1;
    }
    if (!r || !r->name || !*r->name || !r->data || !r->size || r->size > PE_SAVE_ROOM - PE_SAVE_HEADER_SIZE - PE_SAVE_RECORD_HEADER_SIZE || r->version < 1 || r->version > 0xFFFF) {
        pe_mod_log(mod, "save: bad record, not registered");
        return -1;
    }
    id = pe_fnv(r->name);
    if (conditional)
        for (i = 0; i < PE_LEGACY_COUNT; ++i)
            if (pe_in_window(i) && pe_fnv(s_legacy[i].name) == id) {
                pe_mod_log(mod, "save: legacy-window record %s cannot be conditional", r->name);
                return -1;
            }
    for (i = 0; i < s_reg_count; ++i)
        if (s_reg[i].id == id) {
            pe_mod_log(mod, "save: %s clashes with %s (%s), not registered", r->name, s_reg[i].record->name,
                       s_reg[i].mod_id ? s_reg[i].mod_id : "host");
            return -1;
        }
    if (s_reg_count == PE_SAVE_MAX_RECORDS ||
        (!conditional && s_reg_bytes + PE_SAVE_RECORD_HEADER_SIZE + r->size > PE_SAVE_ROOM)) {
        pe_mod_log(mod, "save: no room for %s (%u bytes; %u of %u in use), not registered", r->name, r->size,
                   s_reg_bytes, (unsigned)PE_SAVE_ROOM);
        return -1;
    }
    s_reg[s_reg_count].record = r;
    s_reg[s_reg_count].mod_id = mod;
    s_reg[s_reg_count].id = id;
    s_reg[s_reg_count].kept = s_reg[s_reg_count].released = 0;
    s_reg[s_reg_count].conditional = conditional;
    s_reg[s_reg_count].emitting = !conditional;
    ++s_reg_count;
    if (!conditional) s_reg_bytes += PE_SAVE_RECORD_HEADER_SIZE + r->size;
    return 0;
}

int pe_save_register(const PeSaveRecord *record) { return pe_register_record(record, 0); }
int pe_save_register_conditional(const PeSaveRecord *record) { return pe_register_record(record, 1); }

void pe_save_on_read(const uint8_t *block) {
    int i;
    ++s_lifecycle_dispatch;
    pe_carry(block + PE_SAVE_TAIL_OFFSET, 1);
    for (i = 0; i < s_reg_count; ++i) {
        s_reg[i].load = PE_SAVE_RECORD_NOT_READ;
        if (s_reg[i].conditional) {
            s_reg[i].emitting = pe_carried(s_reg[i].id) != NULL;
            s_reg[i].initialized = 0;
            s_reg[i].kept = 0;
        }
    }
    for (i = 0; i < s_reg_count; ++i)
        if (pe_mod_active(s_reg[i].mod_id)) {
            s_reg[i].released = s_reg[i].release_due = 0;
            pe_load_one(i);
        } else if (s_reg[i].record->release) {
            pe_load_one(i);
            s_reg[i].released = s_reg[i].release_due = !s_reg[i].kept && pe_carried(s_reg[i].id) != NULL;
        }
    --s_lifecycle_dispatch;
}

static int pe_on_frame(void *ctx, void *user) {
    int i;
    (void)ctx; (void)user;
    for (i = 0; i < s_reg_count; ++i)
        if (s_reg[i].release_due && pe_free_field()) {
            s_reg[i].release_due = 0;
            pe_mod_log(s_reg[i].mod_id, "save: %s released (the mod is off)", s_reg[i].record->name);
            ++s_lifecycle_dispatch;
            s_reg[i].record->release(s_reg[i].record->data);
            --s_lifecycle_dispatch;
        }
    return 0;
}

static int pe_commit_tail(uint8_t *block) {
    uint8_t staged[PE_SAVE_TAIL_SIZE];
    int dropped, result;
    if (!block) return PE_SAVE_WRITE_BAD_ARGUMENT;
    result = pe_admit_write();
    if (result < 0) return result;
    pe_pack(staged, block + PE_SAVE_TAIL_OFFSET, &dropped);
    /* Defense in depth: never publish a partial plan, even if selection changes later. */
    if (dropped) return PE_SAVE_WRITE_CAPACITY;
    memcpy(block + PE_SAVE_TAIL_OFFSET, staged, sizeof staged);
    if (!s_foreign) pe_carry(staged, 0);
    return PE_SAVE_WRITE_OK;
}

int pe_save_try_write(uint8_t *block) {
    if (s_write_dispatch || s_lifecycle_dispatch) return PE_SAVE_WRITE_BUSY;
    return pe_commit_tail(block);
}

int pe_save_begin_write(void) {
    int result = pe_save_check_write();
    if (result == PE_SAVE_WRITE_OK) s_write_dispatch = 1;
    return result;
}

int pe_save_finish_write(uint8_t *block) {
    int result;
    if (!s_write_dispatch) return PE_SAVE_WRITE_BUSY;
    result = pe_commit_tail(block); /* recheck after all save hooks */
    s_write_dispatch = 0;
    return result;
}

void pe_save_report_refusal(int result) {
    pe_mod_log(NULL, "save refused (%d): no partial mod tail was published", result);
    /* A rejected nested attempt must not claim the outer save failed. */
    if (result == PE_SAVE_WRITE_BUSY) return;
    if (result == PE_SAVE_WRITE_CAPACITY)
        pe_toast("Save not written: mod data won't fit. Your previous save is unchanged.");
    else
        pe_toast("Save not written: save preparation failed. Your previous save is unchanged.");
}

/* Legacy entry point for existing source consumers. Native save dispatch uses the checked
 * begin/finish path and propagates refusal; new callers must use pe_save_try_write. */
void pe_save_on_write(uint8_t *block) {
    int result = pe_save_try_write(block);
    if (result < 0) pe_save_report_refusal(result);
}

void pe_save_new_game(void) {
    int i;
    ++s_lifecycle_dispatch;
    memset(s_tail, 0, sizeof s_tail);
    s_car_count = 0;
    s_foreign = 0;
    s_loaded_source = s_carried_source = PE_SAVE_SOURCE_NEW_GAME;
    memset(&s_car_info, 0, sizeof s_car_info);
    for (i = 0; i < s_reg_count; ++i) {
        s_reg[i].kept = s_reg[i].released = s_reg[i].release_due = 0;
        s_reg[i].load = PE_SAVE_RECORD_NOT_READ;
        s_reg[i].emitting = !s_reg[i].conditional;
        s_reg[i].initialized = 0;
        if (pe_mod_active(s_reg[i].mod_id)) {
            pe_reset(i);
            s_reg[i].initialized = 1;
        }
    }
    --s_lifecycle_dispatch;
}

/* ---- read-only query snapshots; never validate, reset or pack records ---- */
int pe_save_query_status(PeSaveStatus *out) {
    if (!out) return -1;
    *out = (PeSaveStatus){s_loaded_source, s_carried_source, s_car_info.api, s_car_info.writer,
                          (unsigned)s_car_count, (unsigned)s_car_info.lost};
    return 0;
}

int pe_save_query_record(const char *name, PeSaveRecordStatus *out) {
    PeSaveRecordStatus result;
    const PeSaveEntry *e;
    int i;
    if (!name || !*name || !out) return -1;
    memset(&result, 0, sizeof result);
    result.id = pe_fnv(name);
    e = pe_carried(result.id);
    result.load = e ? PE_SAVE_RECORD_UNCHECKED : PE_SAVE_RECORD_MISSING;
    if (!e && (s_loaded_source == PE_SAVE_SOURCE_NOT_READ || s_loaded_source == PE_SAVE_SOURCE_NEW_GAME))
        result.load = PE_SAVE_RECORD_NOT_READ;
    if (!e && (s_foreign || s_loaded_source == PE_SAVE_SOURCE_CORRUPT ||
        s_loaded_source == PE_SAVE_SOURCE_RECOVERED || s_loaded_source == PE_SAVE_SOURCE_OPAQUE))
        result.load = PE_SAVE_RECORD_UNAVAILABLE;
    if (e) {
        result.flags |= PE_SAVE_RECORD_CARRIED;
        result.carried_version = e->version;
        result.carried_size = e->size;
    }
    for (i = 0; i < s_reg_count; ++i) {
        if (s_reg[i].id != result.id) continue;
        if (strcmp(s_reg[i].record->name, name)) return -1;
        result.flags |= PE_SAVE_RECORD_REGISTERED;
        result.registered_version = s_reg[i].record->version;
        result.registered_size = s_reg[i].record->size;
        if (s_reg[i].load != PE_SAVE_RECORD_NOT_READ) result.load = s_reg[i].load;
        if (s_reg[i].conditional) result.flags |= PE_SAVE_RECORD_CONDITIONAL;
        if (s_reg[i].emitting) result.flags |= PE_SAVE_RECORD_EMISSION_ENABLED;
        if (pe_save_live(i)) result.flags |= PE_SAVE_RECORD_LIVE;
        if (s_reg[i].kept) result.flags |= PE_SAVE_RECORD_KEPT;
        if (s_reg[i].release_due) result.flags |= PE_SAVE_RECORD_RELEASE_PENDING;
        break;
    }
    *out = result;
    return 0;
}

void pe_save_capacity_input(PeSaveCapacityInput *out) {
    unsigned i;
    pe_save_query_status(&out->status);
    out->registered_count = (unsigned)s_reg_count;
    out->carried_count = (unsigned)s_car_count;
    for (i = 0; i < out->registered_count; ++i) {
        const PeSaveRecord *r = s_reg[i].record;
        out->registered[i].name = r->name;
        out->registered[i].entry = (PeSaveEntry){s_reg[i].id, (uint16_t)r->version,
                                                (uint16_t)r->size, r->data};
        out->registered[i].live = pe_save_live((int)i);
        out->registered[i].kept = s_reg[i].kept;
        out->registered[i].conditional = s_reg[i].conditional;
    }
    memcpy(out->carried, s_car, sizeof(PeSaveEntry) * out->carried_count);
}

/* ---- crash report ---- */
static uint8_t s_crash_tail[PE_SAVE_TAIL_SIZE], s_crash_lin[PE_SAVE_ROOM];
static PeSaveEntry s_crash_list[PE_SAVE_MAX_ENTRIES];
static int s_crash_count = -1, s_crash_dropped;
static PeSaveInfo s_crash_info;

int pe_save_try_crash_tail(uint8_t *block) {
    int result;
    if (!block) return PE_SAVE_WRITE_BAD_ARGUMENT;
    result = pe_save_check_write();
    if (result < 0) {
        s_crash_count = -3;
        s_crash_dropped = result == PE_SAVE_WRITE_CAPACITY;
        return result;
    }
    pe_pack(s_crash_tail, NULL, &s_crash_dropped);
    if (s_crash_dropped) {
        s_crash_count = -3;
        return PE_SAVE_WRITE_CAPACITY;
    }
    memcpy(block + PE_SAVE_TAIL_OFFSET, s_crash_tail, sizeof s_crash_tail);
    s_crash_count = pe_parse(s_crash_tail, s_crash_lin, s_crash_list, &s_crash_info);
    return PE_SAVE_WRITE_OK;
}

void pe_save_crash_tail(uint8_t *block) { (void)pe_save_try_crash_tail(block); }

void pe_save_crash_describe(char *out, size_t size, size_t *used) {
    int i, j;
#define PE_OUT(...)                                                              \
    do {                                                                         \
        if (*used < size) {                                                      \
            int w_ = snprintf(out + *used, size - *used, __VA_ARGS__);           \
            if (w_ > 0) *used += (size_t)w_ < size - *used ? (size_t)w_ : size - *used - 1; \
        }                                                                        \
    } while (0)
    if (s_crash_count == -3) {
        PE_OUT("Not taken: save admission refused%s; no partial mod tail was published.\r\n",
               s_crash_dropped ? " (mod data does not fit)" : "");
        return;
    }
    if (s_crash_count == -2) {
        PE_OUT("format %u from a newer build, kept as it is (mods run from their defaults).\r\n",
               (unsigned)s_crash_tail[4]);
        return;
    }
    if (!s_crash_count && memcmp(s_crash_tail, PE_SAVE_MAGIC, 4)) {
        PE_OUT("No mod records; original save tail preserved.\r\n");
        return;
    }
    if (s_crash_count < 0) {
        PE_OUT("Not taken.\r\n");
        return;
    }
    PE_OUT("%d record(s), %u of %u bytes, writer mod API %u / save writer %u%s\r\n", s_crash_count,
           (unsigned)pe_get16(s_crash_tail + 6), (unsigned)PE_SAVE_ROOM, s_crash_info.api, s_crash_info.writer,
           s_crash_dropped ? "; some carried records didn't fit" : "");
    if (s_car_info.api || s_car_info.damaged)
        PE_OUT("  loaded save: written by mod API %u / save writer %u%s\r\n", s_car_info.api, s_car_info.writer,
               s_car_info.damaged ? ", partly overwritten" : "");
    for (i = 0; i < s_crash_count; ++i) {
        const char *mod = NULL, *name = pe_name_of(s_crash_list[i].id, &mod), *from = "carried from the loaded save";
        for (j = 0; j < s_reg_count; ++j)
            if (s_reg[j].id == s_crash_list[i].id) {
                from = s_reg[j].kept ? "kept: newer than this build" :
                       pe_mod_active(s_reg[j].mod_id) ? "live" :
                       s_reg[j].released ? "released: mod off" : "carried: mod off";
                break;
            }
        if (j == s_reg_count && name) from = "carried: mod not in this build";
        PE_OUT("  %08X %s%s%s%s v%u, %u bytes, %s\r\n", (unsigned)s_crash_list[i].id, name ? name : "(unknown)",
               mod ? " (" : "", mod ? mod : "", mod ? ")" : "", (unsigned)s_crash_list[i].version,
               (unsigned)s_crash_list[i].size, from);
    }
#undef PE_OUT
}

void pe_save_install(void) {
    pe_hook_add(PE_HOOK_FRAME, pe_on_frame, NULL, -1000, "host");
}

int pe_save_peek(const uint8_t *block,const char *name,unsigned *version,void *out,unsigned capacity,unsigned *size){
    uint8_t lin[PE_SAVE_ROOM];PeSaveEntry entries[PE_SAVE_MAX_ENTRIES];PeSaveInfo info;
    if(!block||!name||!out)return -1;
    int n=pe_parse(block+PE_SAVE_TAIL_OFFSET,lin,entries,&info);
    if(n<0||info.damaged)return -1;
    uint32_t id=pe_fnv(name);
    for(int i=0;i<n;++i)if(entries[i].id==id){
        if(entries[i].size>capacity)return -1;
        memcpy(out,entries[i].bytes,entries[i].size);
        if(version)*version=entries[i].version;if(size)*size=entries[i].size;return 1;
    }return 0;
}

void pe_save_abort_write(void){s_write_dispatch=0;}
