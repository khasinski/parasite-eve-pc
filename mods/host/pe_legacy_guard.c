#include "pe_legacy_guard.h"
static int (*s_guard)(const struct ItemDataRecord *);
void pe_legacy_guard_bind(int (*fn)(const struct ItemDataRecord *)) {
    s_guard = fn;
}
int pe_legacy_record_editable(const struct ItemDataRecord *r) {
    return !s_guard || s_guard(r);
}
