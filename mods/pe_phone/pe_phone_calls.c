/* The call registry: the "phonecalls" v1 service, picking a call, and which calls this save has
 * heard.
 *
 * Calls come from packs (pe_calls_pe1.c is the game's own); a pack adds them at its init. Picking:
 *   1. calls allowed here: tone, story day and range, phone room, `after`, and a `once` call not
 *      heard yet;
 *   2. a set call (priority > 0) not heard yet beats the random pool, highest priority first;
 *   3. otherwise a weighted random call (priority 0), leaving out the last five played (all of
 *      them only if nothing else fits, then just the last one).
 *
 * Per save (record "pe_phone.heard" v1, 48 bytes): the last five calls played and up to 16 `once`
 * calls heard, as 16-bit hashes of their keys, so packs can come and go without breaking it. A
 * New Game resets it (the host resets every record). */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "pe_mod_host.h"
#include "pe_phone.h"
#include "pe_save.h"

#define PE_PHONE_MAX_CALLS 128
#define PE_PHONE_LAST      5
#define PE_PHONE_HEARD     16

typedef struct PePhoneRecord {
    uint16_t last[PE_PHONE_LAST];     /* newest first; 0 = empty */
    uint16_t heard[PE_PHONE_HEARD];   /* once calls heard, oldest first */
    uint8_t last_count, heard_count;
    uint8_t pad[4];
} PePhoneRecord;
_Static_assert(sizeof(PePhoneRecord) == 48, "pe_phone.heard v1 layout");

static PePhoneRecord s_rec;
static const PePhoneCall *s_calls[PE_PHONE_MAX_CALLS];
static int s_count;
static uint32_t s_rng;

static uint16_t pe_key_hash(const char *key) {
    uint32_t h = 2166136261u;
    uint16_t v;
    while (key && *key) { h ^= (uint8_t)*key++; h *= 16777619u; }
    v = (uint16_t)(h ^ (h >> 16));
    return v ? v : 1;
}

static uint32_t pe_rand(void) {
    if (!s_rng) s_rng = (uint32_t)time(NULL) ^ (uint32_t)clock() ^ 0x9E3779B9u;
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 17;
    s_rng ^= s_rng << 5;
    return s_rng;
}

/* ---- the record ---- */

static int pe_rec_check(void *data) {
    PePhoneRecord *r = data;
    return r->last_count > PE_PHONE_LAST || r->heard_count > PE_PHONE_HEARD;
}

static void pe_rec_reset(void *data) { memset(data, 0, sizeof(PePhoneRecord)); }

static const PeSaveRecord s_record = {"pe_phone.heard", 1, &s_rec, sizeof s_rec, pe_rec_check, NULL, pe_rec_reset, NULL};

static int pe_in_last(uint16_t h) {
    int i;
    for (i = 0; i < s_rec.last_count; ++i) if (s_rec.last[i] == h) return i + 1;   /* 1 = newest */
    return 0;
}

static int pe_in_heard(uint16_t h) {
    int i;
    for (i = 0; i < s_rec.heard_count; ++i) if (s_rec.heard[i] == h) return 1;
    return 0;
}

void pe_phone_calls_played(const PePhoneCall *call) {
    const uint16_t h = pe_key_hash(call->key);
    int at = pe_in_last(h), i;
    /* move to the front of the last-five list */
    if (at) {
        for (i = at - 1; i > 0; --i) s_rec.last[i] = s_rec.last[i - 1];
    } else {
        if (s_rec.last_count < PE_PHONE_LAST) ++s_rec.last_count;
        for (i = s_rec.last_count - 1; i > 0; --i) s_rec.last[i] = s_rec.last[i - 1];
    }
    s_rec.last[0] = h;
    if (call->once && !pe_in_heard(h)) {
        if (s_rec.heard_count == PE_PHONE_HEARD) {     /* full: the oldest goes */
            memmove(s_rec.heard, s_rec.heard + 1, (PE_PHONE_HEARD - 1) * sizeof s_rec.heard[0]);
            --s_rec.heard_count;
        }
        s_rec.heard[s_rec.heard_count++] = h;
    }
}

unsigned pe_phone_heard_count(void) { return s_rec.heard_count; }

/* ---- the service ---- */

static int pe_text_ok(const char *t) {
    for (; t && *t; ++t) {
        const char c = *t;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) continue;
        if (strchr(" .,:!?'-+*%/", c)) continue;
        if (c == '{' && !strncmp(t, "{name}", 6)) { t += 5; continue; }
        return 0;
    }
    return 1;
}

static int api_add(const PePhoneCall *call) {
    int i;
    const char *who = pe_mod_current() ? pe_mod_current() : PE_PHONE_MOD_ID;
    if (!call || !call->key || !call->caller || !call->lines || call->line_count <= 0) {
        pe_mod_log(who, "phone call refused: missing fields");
        return -1;
    }
    if (s_count >= PE_PHONE_MAX_CALLS) { pe_mod_log(who, "phone call %s refused: registry full", call->key); return -1; }
    for (i = 0; i < s_count; ++i)
        if (!strcmp(s_calls[i]->key, call->key)) { pe_mod_log(who, "phone call %s refused: key taken", call->key); return -1; }
    if (!pe_text_ok(call->caller)) { pe_mod_log(who, "phone call %s refused: caller name has characters the game can't show", call->key); return -1; }
    for (i = 0; i < call->line_count; ++i)
        if (!call->lines[i].text || !pe_text_ok(call->lines[i].text) ||
            (call->lines[i].speaker != PE_CALL_AYA && call->lines[i].speaker != PE_CALL_CALLER)) {
            pe_mod_log(who, "phone call %s refused: line %d has characters the game can't show", call->key, i + 1);
            return -1;
        }
    s_calls[s_count++] = call;
    return 0;
}

static const PePhoneCall *api_find(const char *key) {
    int i;
    for (i = 0; key && i < s_count; ++i) if (!strcmp(s_calls[i]->key, key)) return s_calls[i];
    return NULL;
}

static int api_heard(const char *key) {
    const uint16_t h = pe_key_hash(key);
    return pe_in_heard(h) || pe_in_last(h);
}

static void api_reset(void) { pe_rec_reset(&s_rec); }

static int pe_fits(const PePhoneCall *c, const PePhoneContext *x) {
    int i;
    if (!(x->tone_mask & (1 << c->tone))) return 0;
    if (c->day_from && (x->day < c->day_from || x->day > (c->day_to ? c->day_to : c->day_from))) return 0;
    if ((c->story_from || c->story_to) && (x->story < c->story_from || x->story > c->story_to)) return 0;
    if (c->rooms && c->room_count > 0) {
        for (i = 0; i < c->room_count && c->rooms[i] != x->room; ++i) {}
        if (i == c->room_count) return 0;
    }
    if (c->after && !api_heard(c->after)) return 0;
    if (c->once && pe_in_heard(pe_key_hash(c->key))) return 0;
    return 1;
}

static const PePhoneCall *pe_weighted(const PePhoneCall *const *list, int n) {
    unsigned total = 0, roll;
    int i;
    for (i = 0; i < n; ++i) total += list[i]->weight > 0 ? (unsigned)list[i]->weight : 1u;
    if (!total) return NULL;
    roll = pe_rand() % total;
    for (i = 0; i < n; ++i) {
        const unsigned w = list[i]->weight > 0 ? (unsigned)list[i]->weight : 1u;
        if (roll < w) return list[i];
        roll -= w;
    }
    return list[n - 1];
}

static const PePhoneCall *api_pick(const PePhoneContext *x) {
    const PePhoneCall *fit[PE_PHONE_MAX_CALLS], *pool[PE_PHONE_MAX_CALLS];
    const PePhoneCall *best = NULL;
    int n = 0, m = 0, i, pass;
    if (!x) return NULL;
    for (i = 0; i < s_count; ++i) if (pe_fits(s_calls[i], x)) fit[n++] = s_calls[i];
    /* set calls first: the highest priority not heard yet */
    for (i = 0; i < n; ++i)
        if (fit[i]->priority > 0 && !api_heard(fit[i]->key) && (!best || fit[i]->priority > best->priority)) best = fit[i];
    if (best) return best;
    /* the pool, leaving out the last five; failing that, all but the newest; failing that, any */
    for (pass = 0; pass < 3 && !m; ++pass)
        for (i = 0; i < n; ++i) {
            const int at = pe_in_last(pe_key_hash(fit[i]->key));
            if (fit[i]->priority > 0) continue;
            if (pass == 0 && at) continue;
            if (pass == 1 && at == 1) continue;
            pool[m++] = fit[i];
        }
    return m ? pe_weighted(pool, m) : NULL;
}

static const PePhoneCallsApi s_api = {api_add, api_find, api_pick, api_heard, api_reset};

const PePhoneCallsApi *pe_phone_calls(void) { return &s_api; }
int pe_phone_calls_count(void) { return s_count; }
const PePhoneCall *pe_phone_call_at(int index) { return index >= 0 && index < s_count ? s_calls[index] : NULL; }

void pe_phone_calls_register(const char *mod_id) {
    if (pe_save_register(&s_record) < 0)
        pe_mod_log(mod_id, "no room for the pe_phone.heard record: heard calls won't be kept in saves");
    pe_service_publish("phonecalls", 1, &s_api, mod_id);
}

int Save_ClassifyPlaytime(int story);   /* menu/status_panels.c: the save screen's Day */

int pe_phone_story_day(unsigned story) {
    /* 1 below 0x80, 2 below 0x138, 3 below 0x1B0, 4 below 0x218, 5 below 0x2D3, else 6 */
    return Save_ClassifyPlaytime((int)story);
}
