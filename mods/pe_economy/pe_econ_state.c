/* The economy's live state for test tools (pe_mod_state.h): the Junk wallet, Wayne's hand-in
 * and the saved Economy policy with its pending rewards. Reads only; called by debug control. */
#include <stddef.h>
#include "pe_mod_state.h"
#include "pe_economy.h"
#include "pe_econ_store.h"

static const char *pe_econ_status_name(PeEconStoreStatus status) {
    return status == PE_ECON_STORE_READY ? "ready" : status == PE_ECON_STORE_DAMAGED ? "damaged" : "unsupported";
}

static void pe_econ_state(PeStateWriter *w, void *user) {
    const PeEconState *state = pe_econ_store_state();
    const PeEconConfig *active = pe_econ_store_active();
    int i;
    (void)user;
    pe_state_bool(w, "wallet_enabled", pe_loot_enabled());
    pe_state_int(w, "junk", pe_loot_junk());
    pe_state_bool(w, "wayne_waiting", pe_loot_wayne_waiting());
    pe_state_bool(w, "handin_blocked", pe_loot_handin_blocked());
    pe_state_text(w, "store", pe_econ_status_name(pe_econ_store_status()));
    pe_state_text(w, "path", active ? "configurable" : "enhanced");
    if (!state) return;
    pe_state_begin_object(w, "policy");
    pe_state_text(w, "key", state->config.key[0] ? state->config.key : "custom");
    pe_state_int(w, "engine", state->config.engine);
    pe_state_text(w, "route", state->config.route == PE_ECON_ROUTE_ITEMS ? "items" : "counter");
    pe_state_int(w, "flags", state->config.flags);
    pe_state_int(w, "price_pct", state->config.price_pct);
    pe_state_end(w);
    pe_state_begin_object(w, "progress");
    pe_state_bool(w, "initialized", state->initialized);
    pe_state_int(w, "remainder", state->progress.remainder);
    pe_state_int(w, "next_sequence", state->progress.next_sequence);
    pe_state_begin_list(w, "pending");
    for (i = 0; i < PE_ECON_SLOTS; ++i) {
        const PeEconPending *p = &state->progress.pending[i];
        if (!p->kind) continue;
        pe_state_begin_object(w, NULL);
        pe_state_int(w, "slot", i);
        pe_state_int(w, "kind", p->kind);
        pe_state_int(w, "content", p->content);
        pe_state_int(w, "amount", p->amount);
        pe_state_int(w, "sequence", p->sequence);
        pe_state_end(w);
    }
    pe_state_end(w);
    pe_state_end(w);
}

void pe_econ_state_register(void) {
    (void)pe_mod_state_provider("economy", pe_econ_state, NULL);
}
