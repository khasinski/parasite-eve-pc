/* Mods' per-save data, kept by the host. See pe_save.h. */
#include <stdio.h>
#include <string.h>

#include "pe_game.h"
#include "pe_mod_host.h"
#include "pe_save.h"

#define PE_SAVE_MAX_RECORDS 32
#define PE_SAVE_MAX_ENTRIES 64      /* records in one tail: 8 header bytes each, so 108 at most */
#define PE_SAVE_MAGIC "PEMC"
#define PE_SAVE_FORMAT 1
/* The writer marker's revision of how this host writes saves; raise it when that changes, so later
 * builds can tell which saves they may treat differently. 1: format 1, the old blocks' window kept. */
#define PE_SAVE_WRITER 1

typedef struct {
    uint32_t id;
    uint16_t version, size;
    const uint8_t *bytes;
} PeSaveEntry;

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
} s_reg[PE_SAVE_MAX_RECORDS];
static int s_reg_count;
static unsigned s_reg_bytes = PE_SAVE_HEADER_SIZE;

/* Carried: the records of the save loaded or last written, pointing into s_tail. foreign = the
 * loaded tail is a container format newer than this build: s_tail is written back as it is. */
static uint8_t s_tail[PE_SAVE_TAIL_SIZE], s_lin[PE_SAVE_ROOM];
static PeSaveEntry s_car[PE_SAVE_MAX_ENTRIES];
static int s_car_count, s_foreign;
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
static int pe_save_live(int i) { return pe_mod_active(s_reg[i].mod_id) || s_reg[i].released; }

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

/* The records a save made now holds, into out (a whole tail): running mods' state first, then
 * carried records (mods off, newer layouts, mods not in this build) while room is left, around
 * the window (live: the save's own tail, whose window the old blocks' owners wrote; NULL: the
 * carried window). Reads memory only, so the crash report can use it. Returns the record count. */
static int pe_pack(uint8_t *out, const uint8_t *live, int *dropped) {
    PeSaveEntry list[PE_SAVE_MAX_ENTRIES];
    uint8_t lin[PE_SAVE_ROOM], win[PE_SAVE_WINDOW_SIZE];
    unsigned total = PE_SAVE_HEADER_SIZE, at;
    int n = 0, i, j;
    *dropped = 0;
    pe_window(win, live);            /* first: live may be out itself */
    if (s_foreign) {                 /* a newer build's container: back as it was, nothing added */
        memcpy(out, s_tail, PE_SAVE_TAIL_SIZE);
        memcpy(out + PE_SAVE_WINDOW_AT, win, sizeof win);
        return 0;
    }
    for (i = 0; i < s_reg_count; ++i) {
        if (s_reg[i].kept || !pe_save_live(i)) continue;
        list[n].id = s_reg[i].id;
        list[n].version = (uint16_t)s_reg[i].record->version;
        list[n].size = (uint16_t)s_reg[i].record->size;
        list[n].bytes = (const uint8_t *)s_reg[i].record->data;
        total += PE_SAVE_RECORD_HEADER_SIZE + list[n].size;
        ++n;
    }
    for (i = 0; i < s_car_count; ++i) {
        for (j = 0; j < n && list[j].id != s_car[i].id; ++j) {}
        if (j < n) continue;
        if (n == PE_SAVE_MAX_ENTRIES || total + PE_SAVE_RECORD_HEADER_SIZE + s_car[i].size > PE_SAVE_ROOM) {
            ++*dropped;
            continue;
        }
        list[n++] = s_car[i];
        total += PE_SAVE_RECORD_HEADER_SIZE + s_car[i].size;
    }
    for (i = 1; i < n; ++i)   /* by id: the same state always gives the same bytes */
        for (j = i; j > 0 && list[j - 1].id > list[j].id; --j) {
            PeSaveEntry t = list[j];
            list[j] = list[j - 1];
            list[j - 1] = t;
        }
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
        pe_put32(lin + at, list[i].id);
        pe_put16(lin + at + 4, list[i].version);
        pe_put16(lin + at + 6, list[i].size);
        memcpy(lin + at + PE_SAVE_RECORD_HEADER_SIZE, list[i].bytes, list[i].size);
        pe_put32(lin + at + 8, pe_record_crc(lin + at, list[i].bytes, list[i].size));
        at += PE_SAVE_RECORD_HEADER_SIZE + list[i].size;
    }
    pe_put32(lin + 8, pe_crc32(lin, total));
    memset(out, 0, PE_SAVE_TAIL_SIZE);
    pe_scatter(lin, out);
    memcpy(out + PE_SAVE_WINDOW_AT, win, sizeof win);
    return n;
}

static void pe_carry(const uint8_t *tail, int loading) {
    PeSaveInfo info;
    int n;
    if (tail != s_tail) memcpy(s_tail, tail, sizeof s_tail);
    n = pe_parse(s_tail, s_lin, s_car, &info);
    s_car_count = n < 0 ? 0 : n;
    s_foreign = n == -2;
    s_car_info = info;
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
    if (!e) {
        pe_reset(i);
    } else if (e->version == r->version && e->size == r->size) {
        memcpy(r->data, e->bytes, r->size);
        if (pe_call_check(i)) {
            pe_mod_log(mod, "save: %s didn't pass its checks, reset", r->name);
            pe_reset(i);
        }
    } else if (e->version < r->version && r->upgrade) {
        if (r->upgrade(r->data, e->bytes, e->size, e->version) || pe_call_check(i)) {
            pe_mod_log(mod, "save: %s v%u couldn't be read, reset", r->name, (unsigned)e->version);
            pe_reset(i);
        } else {
            pe_mod_log(mod, "save: %s upgraded from v%u", r->name, (unsigned)e->version);
        }
    } else if (e->version > r->version) {
        pe_mod_log(mod, "save: %s is v%u, newer than this build's v%u: kept as is, mod starts from defaults",
                   r->name, (unsigned)e->version, r->version);
        pe_reset(i);
        s_reg[i].kept = 1;
    } else {
        pe_mod_log(mod, "save: %s v%u (%u bytes) doesn't match v%u (%u bytes), reset", r->name,
                   (unsigned)e->version, (unsigned)e->size, r->version, r->size);
        pe_reset(i);
    }
}

int pe_save_register(const PeSaveRecord *r) {
    const char *mod = pe_mod_current();
    uint32_t id;
    int i;
    if (!r || !r->name || !*r->name || !r->data || !r->size || r->size > PE_SAVE_ROOM - PE_SAVE_HEADER_SIZE - PE_SAVE_RECORD_HEADER_SIZE || r->version < 1 || r->version > 0xFFFF) {
        pe_mod_log(mod, "save: bad record, not registered");
        return -1;
    }
    id = pe_fnv(r->name);
    for (i = 0; i < s_reg_count; ++i)
        if (s_reg[i].id == id) {
            pe_mod_log(mod, "save: %s clashes with %s (%s), not registered", r->name, s_reg[i].record->name,
                       s_reg[i].mod_id ? s_reg[i].mod_id : "host");
            return -1;
        }
    if (s_reg_count == PE_SAVE_MAX_RECORDS ||
        s_reg_bytes + PE_SAVE_RECORD_HEADER_SIZE + r->size > PE_SAVE_ROOM) {
        pe_mod_log(mod, "save: no room for %s (%u bytes; %u of %u in use), not registered", r->name, r->size,
                   s_reg_bytes, (unsigned)PE_SAVE_ROOM);
        return -1;
    }
    s_reg[s_reg_count].record = r;
    s_reg[s_reg_count].mod_id = mod;
    s_reg[s_reg_count].id = id;
    s_reg[s_reg_count].kept = s_reg[s_reg_count].released = 0;
    ++s_reg_count;
    s_reg_bytes += PE_SAVE_RECORD_HEADER_SIZE + r->size;
    return 0;
}

void pe_save_on_read(const uint8_t *block) {
    int i;
    pe_carry(block + PE_SAVE_TAIL_OFFSET, 1);
    for (i = 0; i < s_reg_count; ++i)
        if (pe_mod_active(s_reg[i].mod_id)) {
            s_reg[i].released = s_reg[i].release_due = 0;
            pe_load_one(i);
        } else if (s_reg[i].record->release) {
            pe_load_one(i);
            s_reg[i].released = s_reg[i].release_due = !s_reg[i].kept && pe_carried(s_reg[i].id) != NULL;
        }
}

static int pe_on_frame(void *ctx, void *user) {
    int i;
    (void)ctx; (void)user;
    for (i = 0; i < s_reg_count; ++i)
        if (s_reg[i].release_due && pe_free_field()) {
            s_reg[i].release_due = 0;
            pe_mod_log(s_reg[i].mod_id, "save: %s released (the mod is off)", s_reg[i].record->name);
            s_reg[i].record->release(s_reg[i].record->data);
        }
    return 0;
}

void pe_save_on_write(uint8_t *block) {
    int dropped;
    pe_pack(block + PE_SAVE_TAIL_OFFSET, block + PE_SAVE_TAIL_OFFSET, &dropped);
    if (dropped) {
        pe_mod_log(NULL, "save: no room for %d record(s) of mods not running; left out of this save", dropped);
        pe_toast("Mod data of a mod that is off didn't fit in this save");
    }
    if (!s_foreign) pe_carry(block + PE_SAVE_TAIL_OFFSET, 0);   /* foreign: s_tail already holds it */
}

void pe_save_new_game(void) {
    int i;
    memset(s_tail, 0, sizeof s_tail);
    s_car_count = 0;
    s_foreign = 0;
    memset(&s_car_info, 0, sizeof s_car_info);
    for (i = 0; i < s_reg_count; ++i) {
        s_reg[i].kept = s_reg[i].released = s_reg[i].release_due = 0;
        if (pe_mod_active(s_reg[i].mod_id)) pe_reset(i);
    }
}

/* ---- crash report ---- */
static uint8_t s_crash_tail[PE_SAVE_TAIL_SIZE], s_crash_lin[PE_SAVE_ROOM];
static PeSaveEntry s_crash_list[PE_SAVE_MAX_ENTRIES];
static int s_crash_count = -1, s_crash_dropped;
static PeSaveInfo s_crash_info;

void pe_save_crash_tail(uint8_t *block) {
    pe_pack(s_crash_tail, NULL, &s_crash_dropped);
    memcpy(block + PE_SAVE_TAIL_OFFSET, s_crash_tail, sizeof s_crash_tail);
    s_crash_count = pe_parse(s_crash_tail, s_crash_lin, s_crash_list, &s_crash_info);
}

void pe_save_crash_describe(char *out, size_t size, size_t *used) {
    int i, j;
#define PE_OUT(...)                                                              \
    do {                                                                         \
        if (*used < size) {                                                      \
            int w_ = snprintf(out + *used, size - *used, __VA_ARGS__);           \
            if (w_ > 0) *used += (size_t)w_ < size - *used ? (size_t)w_ : size - *used - 1; \
        }                                                                        \
    } while (0)
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

#if PE_NATIVE_STATE_CPU_COMPONENTS
/* No deferred release callback or live mod-save payload may escape the barrier.
 * Only-assists has no save records; inactive carried metadata is left unchanged. */
#include "pe_checkpoint_wire.h"
#define PE_MOD_SAVE_STATE_BYTES 1536u
static int pe_save_checkpoint_owned(int i){const PeSaveRecord *r=s_reg[i].record;if(r->version!=1)return 0;
#if PE_NATIVE_STATE_QOL
 if(!strcmp(s_reg[i].mod_id,"pe_qol")&&((!strcmp(r->name,"pe_qol.keys")&&r->size==68)||(!strcmp(r->name,"pe_qol.sort")&&r->size==24)))return 1;
#endif
#if PE_NATIVE_STATE_ECONOMY
 if(!strcmp(s_reg[i].mod_id,"pe_economy")&&((!strcmp(r->name,"pe_economy.loot")&&r->size==36)||(!strcmp(r->name,"pe_economy.catalog")&&r->size==312)||(!strcmp(r->name,"pe_economy.shop")&&r->size==4)))return 1;
#endif
 return 0;
}
int pe_save_state_idle(void){for(int i=0;i<s_reg_count;i++)if(s_reg[i].release_due||((pe_mod_active(s_reg[i].mod_id)||s_reg[i].released)&&!pe_save_checkpoint_owned(i)))return 0;return 1;}
int pe_save_checkpoint_walk(const unsigned char *in,unsigned char *out,size_t bytes,int apply){
 if(bytes!=PE_MOD_SAVE_STATE_BYTES||(!in&&!out)||!pe_save_state_idle()||s_reg_count<0||s_reg_count>PE_SAVE_MAX_RECORDS)return 0;
 if(apply&&(!in||!pe_save_checkpoint_walk(in,0,bytes,0)))return 0;
 if(out){memset(out,0,bytes);memcpy(out,"PEMDS001",8);pe_put32(out+8,(unsigned)s_reg_count);}
 else if(memcmp(in,"PEMDS001",8)||pe_get32(in+8)!=(unsigned)s_reg_count||pe_get32(in+12))return 0;
 size_t at=16;
 for(int i=0;i<PE_SAVE_MAX_RECORDS;i++,at+=20){
  if(i>=s_reg_count){if(in)for(unsigned j=0;j<20;j++)if(in[at+j])return 0;continue;}
  const PeSaveRecord *r=s_reg[i].record;uint32_t values[5]={s_reg[i].id,r->version,r->size,(unsigned)s_reg[i].kept,(unsigned)s_reg[i].released};
  for(unsigned j=0;j<5;j++){uint32_t v=out?values[j]:pe_get32(in+at+j*4);if((j<3&&v!=values[j])||(j>=3&&v>1)||(j==4&&v&&!pe_save_checkpoint_owned(i)))return 0;if(out)pe_put32(out+at+j*4,v);else if(apply&&j==3)s_reg[i].kept=(int)v;else if(apply&&j==4)s_reg[i].released=(int)v;}
 }
 /* Reparse known bounded host metadata, never dispatch save/reset/release hooks.
  * Record payloads live in their explicit feature owners, not this tail copy. */
 if(out)memcpy(out+at,s_tail,sizeof s_tail);
 else{uint8_t linear[PE_SAVE_ROOM];PeSaveEntry entries[PE_SAVE_MAX_ENTRIES];PeSaveInfo info;(void)pe_parse(in+at,linear,entries,&info);if(apply){memcpy(s_tail,in+at,sizeof s_tail);int n=pe_parse(s_tail,s_lin,s_car,&s_car_info);s_car_count=n<0?0:n;s_foreign=n== -2;}}
 return at+sizeof s_tail==bytes;
}
#endif
