/* Requisition's live state for test tools (pe_mod_state.h): the board as the player sees it now,
 * each row's price and why it can't be bought. Reads only. */
#include <stddef.h>
#include "pe_mod_state.h"
#include "pe_requisition.h"

static void pe_shops_state(PeStateWriter *w, void *user) {
    int i, n = pe_shop_count();
    (void)user;
    pe_state_bool(w, "rescue_needed", pe_shop_rescue_needed());
    pe_state_begin_list(w, "board");
    for (i = 0; i < n && i < 64; ++i) {
        uint16_t id = pe_shop_item(i);
        if (!id) continue;
        pe_state_begin_object(w, NULL);
        pe_state_int(w, "id", id);
        pe_state_text(w, "name", pe_shop_name(id));
        pe_state_int(w, "price", pe_shop_price(id));
        pe_state_text(w, "blocked", pe_shop_blocked(id));
        pe_state_end(w);
    }
    pe_state_end(w);
}

void pe_shops_state_register(void) {
    (void)pe_mod_state_provider("requisition", pe_shops_state, NULL);
}
