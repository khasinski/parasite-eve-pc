/* The call registry: the "phonecalls" v1 service packs add their calls through, and this save's
 * heard record ("pe_phone.heard" v1, kept by the host). Choosing and the record's rules are the
 * pure core's (pe_phone_pick.c, pe_phone_record.c). */
#include <string.h>
#include <time.h>

#include "pe_mod_host.h"
#include "pe_phone.h"
#include "pe_save.h"

#define PE_PHONE_MAX_CALLS 128

static PePhoneRecord s_rec;
static const PePhoneCall *s_calls[PE_PHONE_MAX_CALLS];
static int s_count;
static uint32_t s_rng;

static uint32_t pe_rand(void) {
    if (!s_rng) s_rng = (uint32_t)time(NULL) ^ (uint32_t)clock() ^ 0x9E3779B9u;
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 17;
    s_rng ^= s_rng << 5;
    return s_rng;
}

/* ---- the record ---- */

static int pe_rec_check(void *data) { return !pe_phone_record_valid(data); }
static void pe_rec_reset(void *data) { memset(data, 0, sizeof(PePhoneRecord)); }
static const PeSaveRecord s_record = {"pe_phone.heard", 1, &s_rec, sizeof s_rec, pe_rec_check, NULL, pe_rec_reset, NULL};

void pe_phone_calls_played(const PePhoneCall *call) { pe_phone_record_played(&s_rec, call); }
unsigned pe_phone_heard_count(void) { return s_rec.heard_count; }

/* ---- the service ---- */

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
    if (call->tone != PE_CALL_SERIOUS && call->tone != PE_CALL_FUNNY) { pe_mod_log(who, "phone call %s refused: tone", call->key); return -1; }
    if (!pe_phone_text_ok(call->caller)) { pe_mod_log(who, "phone call %s refused: caller name has characters the game can't show", call->key); return -1; }
    for (i = 0; i < call->line_count; ++i)
        if (!pe_phone_text_ok(call->lines[i].text) ||
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

static const PePhoneCall *api_pick(const PePhoneContext *x) {
    return pe_phone_pick(s_calls, s_count, x, &s_rec, pe_rand());
}

static int api_heard(const char *key) { return pe_phone_record_heard(&s_rec, key); }
static void api_reset(void) { pe_rec_reset(&s_rec); }

static const PePhoneCallsApi s_api = {api_add, api_find, api_pick, api_heard, api_reset};

const PePhoneCallsApi *pe_phone_calls(void) { return &s_api; }
int pe_phone_calls_count(void) { return s_count; }
const PePhoneCall *pe_phone_call_at(int index) { return index >= 0 && index < s_count ? s_calls[index] : NULL; }

void pe_phone_service_register(const char *mod_id) {
    if (pe_save_register(&s_record) < 0)
        pe_mod_log(mod_id, "no room for the pe_phone.heard record: heard calls won't be kept in saves");
    pe_service_publish("phonecalls", 1, &s_api, mod_id);
}

int Save_ClassifyPlaytime(int story);   /* menu/status_panels.c: the save screen's Day */

int pe_phone_story_day(unsigned story) {
    /* 1 below 0x80, 2 below 0x138, 3 below 0x1B0, 4 below 0x218, 5 below 0x2D3, else 6 */
    return Save_ClassifyPlaytime((int)story);
}
