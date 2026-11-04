#include <string.h>
#include "pe_profile_transactions.h"
#include "pe_extension_store.h"
#include "pe_mod_host.h"
#include "pe_game.h"
#include "pe_api_native.h"
/* Twelve bounded, idempotent receipts. No global unlock reaches the profile before its wallet
 * payment is in a published native save. The companion receipt remains replayable if the later
 * profile publication fails or the process stops at that boundary. Unknown adapters are retained.
 */
static unsigned char s_receipts[1408];
static PeExtensionSection s_section = {"pe_host/transactions", 1, 1, sizeof s_receipts, s_receipts};
static struct {
    char key[48], owner[48];
    PeProfileTransactionAdapter api;
} s_adapters[8];
static int s_count, s_busy;
static int key_ok(const char *s, unsigned max) {
    if (!s || !*s || !memchr(s, 0, max)) return 0;
    for (; *s; ++s)
        if (!(*s >= 'a' && *s <= 'z') && !(*s >= 'A' && *s <= 'Z') && !(*s >= '0' && *s <= '9') &&
            !strchr("_-/.", *s))
            return 0;
    return 1;
}
static int valid(void) {
    for (int i = 0; i < 12; ++i) {
        const unsigned char *p = s_receipts + i * 116;
        if (!p[0]) continue;
        if (!key_ok((const char *)p, 48) || !key_ok((const char *)p + 48, 64) || !p[112] || p[113] ||
            p[114] || p[115])
            return 0;
    }
    return 1;
}
static int adapter(const char *key) {
    if (!key) return -1;
    for (int i = 0; i < s_count; ++i)
        if (!strcmp(s_adapters[i].key, key) && pe_mod_is_running(s_adapters[i].owner)) return i;
    return -1;
}
static int add(const PeProfileTransactionAdapter *a) {
    if (pe_reg_registration_access() != PE_REG_OK) return 0;
    const char *owner = pe_mod_current();
    if (!owner || !a || a->struct_size != sizeof *a || !a->key || strlen(a->key) > 47 || !a->supported_bits ||
        a->supported_bits > 255 || !a->admit || !a->owned || !a->promote || s_count == 8)
        return 0;
    size_t n = strlen(owner);
    if (strncmp(owner, a->key, n) || a->key[n] != '/') return 0;
    for (int i = 0; i < s_count; ++i)
        if (!strcmp(s_adapters[i].key, a->key)) return 0;
    strcpy(s_adapters[s_count].owner, owner);
    strcpy(s_adapters[s_count].key, a->key);
    s_adapters[s_count].api = *a;
    s_adapters[s_count].api.key = s_adapters[s_count].key;
    ++s_count;
    return 1;
}
static unsigned pending(const char *a, const char *key) {
    if (pe_reg_access() != PE_REG_OK || !a || !key || !valid() || !pe_extension_available(&s_section))
        return 0;
    unsigned bits = 0;
    for (int i = 0; i < 12; ++i) {
        unsigned char *p = s_receipts + i * 116;
        if (p[0] && !strcmp((char *)p, a) && !strcmp((char *)p + 48, key)) bits |= p[112];
    }
    return bits;
}
static int buy(const char *a, const char *key, unsigned bits, uint32_t price) {
    if (pe_reg_access() != PE_REG_OK || s_busy || !pe_api_native_loaded() || pe_api_native_in_battle() ||
        !key_ok(key, 64) || price > 99999 || !bits || !valid())
        return 0;
    int at = adapter(a);
    if (at < 0 || (bits & ~s_adapters[at].api.supported_bits)) return 0;
    const PeJunkApi *j = pe_service_get("junk", 1);
    if (!j || !j->enabled || !j->junk || !j->spend) return 0;
    s_busy = 1;
    int ok = 0;
    PeProfileTransactionAdapter *api = &s_adapters[at].api;
    if (!pe_reg_readonly_begin()) goto done;
    int on = j->enabled(), admitted = api->admit(key, bits);
    uint32_t balance = j->junk();
    unsigned owned = api->owned(key);
    pe_reg_readonly_end();
    if (!on || !admitted || balance < price || ((owned | pending(a, key)) & bits) == bits) goto done;
    int row = -1, empty = -1;
    for (int i = 0; i < 12; ++i) {
        unsigned char *p = s_receipts + i * 116;
        if (!p[0] && empty < 0) empty = i;
        else if (p[0] && !strcmp((char *)p, a) && !strcmp((char *)p + 48, key)) row = i;
    }
    if (row < 0) row = empty;
    if (row < 0 || pe_extension_enable(&s_section) != PE_REG_OK) goto done;
    if (!pe_reg_readonly_begin()) goto done;
    int paid = j->junk() == balance && j->spend(price);
    pe_reg_readonly_end();
    if (!paid) goto done;
    /* No callback or I/O after debit: one bounded memory update, later saved with the wallet. */
    unsigned char *p = s_receipts + row * 116;
    strcpy((char *)p, a);
    strcpy((char *)p + 48, key);
    p[112] |= bits;
    ok = 1;
done:
    s_busy = 0;
    return ok;
}
const PeProfileTransactionsApi g_pe_profile_transactions_api = {sizeof(PeProfileTransactionsApi), add,
                                                                pending, buy};
void pe_profile_transactions_install(void) {
    (void)pe_extension_register(&s_section);
}
void pe_profile_transactions_committed(void) {
    if (pe_reg_access() != PE_REG_OK || s_busy || !valid()) return;
    s_busy = 1;
    for (int i = 0; i < 12; ++i) {
        unsigned char *p = s_receipts + i * 116;
        if (!p[0]) continue;
        int at = adapter((char *)p);
        if (at < 0) continue;
        /* A failing/absent provider keeps its receipt. Promote may write but must be idempotent. */
        if (!pe_reg_readonly_begin()) continue;
        unsigned owned=s_adapters[at].api.owned((char *)p + 48);
        int ok=(owned&p[112])==p[112] || s_adapters[at].api.promote((char *)p + 48, p[112]);
        pe_reg_readonly_end();
        if (ok) memset(p, 0, 116);
    }
    s_busy = 0;
}
void pe_profile_transactions_loaded(void) {
    s_busy = 0;
    pe_profile_transactions_committed();
}
