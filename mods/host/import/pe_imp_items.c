/* A game's item table as an ItemTable record (pe_imp_logic.h). */
#include <stdio.h>
#include <string.h>

#include "pe_imp_items.h"

int pe_imp_emit_item_table(PeImpSink *sink, unsigned game, const char *const *names, uint32_t count,
                           int icons_by_number, const uint8_t *icons, uint32_t icon_bytes, char *why, size_t n) {
    uint32_t icon_count = icon_bytes / (PE_IMP_ITEM_ICON_W * PE_IMP_ITEM_ICON_H);
    if (!count || count > 0xFFFF) { if (why && n) snprintf(why, n, "no items"); return -1; }
    uint8_t *records = calloc(count, PE_IMP_ITEM_NAME_BYTES);
    if (!records) { if (why && n) snprintf(why, n, "out of memory"); return -1; }
    uint32_t named = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint8_t *r = records + named * PE_IMP_ITEM_NAME_BYTES;
        if (!names[i] || !*names[i]) continue;
        unsigned icon = icons_by_number && i < icon_count ? i : 0xFFFF;
        r[0] = (uint8_t)i, r[1] = (uint8_t)(i >> 8), r[2] = (uint8_t)icon, r[3] = (uint8_t)(icon >> 8);
        snprintf((char *)r + 4, 44, "%s", names[i]);
        named++;
    }
    PeImpLogicBuilder *b = pe_imp_logic_begin(game, 0, 0, 0);
    pe_imp_logic_add(b, PE_IMP_ITEM_NAME, named, records, named * PE_IMP_ITEM_NAME_BYTES);
    if (icon_count) pe_imp_logic_add(b, PE_IMP_ITEM_ICN8, icon_count, icons, icon_count * PE_IMP_ITEM_ICON_W * PE_IMP_ITEM_ICON_H);
    free(records);
    PeImpRoomLogic *l = pe_imp_logic_finish(b);
    if (!l) { if (why && n) snprintf(why, n, "the item table is too large"); return -1; }
    if (pe_imp_emit(sink, PE_IMP_ITEMTABLE, "items/all", "Items", l)) {
        if (why && n) snprintf(why, n, "couldn't catalog the items");
        return -1;
    }
    return 0;
}
