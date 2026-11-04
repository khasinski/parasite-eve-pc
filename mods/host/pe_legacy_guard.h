#ifndef PE_LEGACY_GUARD_H
#define PE_LEGACY_GUARD_H
/* Native-independent bridge for legacy mod modules. Unbound hosts have no custom ledger.
 * Only the host installs its predicate; frontends never inspect or own identity storage. */
struct ItemDataRecord;
void pe_legacy_guard_bind(int (*record_editable)(const struct ItemDataRecord *));
int pe_legacy_record_editable(const struct ItemDataRecord *record);
#endif
