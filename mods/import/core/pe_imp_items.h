#ifndef PE_IMP_ITEMS_H
#define PE_IMP_ITEMS_H
/* A game's items (names, icons) as an ItemTable record, cataloged as "items/all". */
#include <stdlib.h>

#include "pe_imp_logic.h"
#include "pe_imp_room.h"                          /* PE_IMP_RE1, PE_IMP_RE2, PE_IMP_RE3 */

/* names[i] is item number i (NULL or "" for an unused number). icons_by_number: icon k belongs to
 * item k + 1 (RE1: item 0 is "nothing"); otherwise the icons are kept but not matched to items yet.
 * 0, or -1 with a reason. */
int pe_imp_emit_item_table(PeImpSink *sink, unsigned game, const char *const *names, uint32_t count,
                           int icons_by_number, const uint8_t *icons, uint32_t icon_bytes, char *why, size_t n);

#endif
