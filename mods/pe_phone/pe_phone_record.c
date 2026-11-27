/* The per-save record of calls heard: the last five played and up to 16 once calls, as 16-bit
 * hashes of their keys. Pure: the record itself lives in pe_phone_service.c. */
#include <string.h>

#include "pe_phone_core.h"

uint16_t pe_phone_key_hash(const char *key) {
    uint32_t h = 2166136261u;                 /* FNV-1a 32, folded */
    uint16_t v;
    while (key && *key) { h ^= (uint8_t)*key++; h *= 16777619u; }
    v = (uint16_t)(h ^ (h >> 16));
    return v ? v : 1;                         /* 0 never names a call */
}

int pe_phone_record_valid(const PePhoneRecord *r) {
    return r->last_count <= PE_PHONE_LAST && r->heard_count <= PE_PHONE_HEARD;
}

static int pe_recent_hash(const PePhoneRecord *r, uint16_t h) {
    int i;
    for (i = 0; i < r->last_count && i < PE_PHONE_LAST; ++i) if (r->last[i] == h) return i + 1;
    return 0;
}

static int pe_once_hash(const PePhoneRecord *r, uint16_t h) {
    int i;
    for (i = 0; i < r->heard_count && i < PE_PHONE_HEARD; ++i) if (r->heard[i] == h) return 1;
    return 0;
}

int pe_phone_record_recent(const PePhoneRecord *r, const char *key) { return pe_recent_hash(r, pe_phone_key_hash(key)); }
int pe_phone_record_once(const PePhoneRecord *r, const char *key) { return pe_once_hash(r, pe_phone_key_hash(key)); }
int pe_phone_record_heard(const PePhoneRecord *r, const char *key) {
    const uint16_t h = pe_phone_key_hash(key);
    return pe_once_hash(r, h) || pe_recent_hash(r, h);
}

void pe_phone_record_played(PePhoneRecord *r, const PePhoneCall *call) {
    const uint16_t h = pe_phone_key_hash(call->key);
    const int at = pe_recent_hash(r, h);
    int i;
    if (!at && r->last_count < PE_PHONE_LAST) ++r->last_count;
    for (i = (at ? at : r->last_count) - 1; i > 0; --i) r->last[i] = r->last[i - 1];   /* to the front */
    r->last[0] = h;
    if (call->once && !pe_once_hash(r, h)) {
        if (r->heard_count >= PE_PHONE_HEARD) {   /* full: the oldest goes */
            memmove(r->heard, r->heard + 1, (PE_PHONE_HEARD - 1) * sizeof r->heard[0]);
            r->heard_count = PE_PHONE_HEARD - 1;
        }
        r->heard[r->heard_count++] = h;
    }
}
