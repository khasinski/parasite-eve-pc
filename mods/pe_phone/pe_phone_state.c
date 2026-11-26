/* Phone Calls' live state for test tools (pe_mod_state.h): the settings in effect, whether a
 * question or call is up, the phone save waiting for its question, and the calls this save
 * heard. Reads only. */
#include <stddef.h>
#include "pe_mod_state.h"
#include "pe_phone.h"

static const char *pe_phone_mode_name(int mode) {
    return mode == PE_PHONE_ALWAYS ? "always" : mode == PE_PHONE_OFF ? "off" : "ask";
}

static void pe_phone_state(PeStateWriter *w, void *user) {
    const PePhoneRecord *record = pe_phone_heard_record();
    PePhoneSpot spot;
    char last[128];
    int i, n = pe_phone_calls_count();
    (void)user;
    pe_state_text(w, "mode", pe_phone_mode_name(pe_phone_mode()));
    pe_state_bool(w, "ask_unsaved", pe_phone_unsaved_on());
    pe_state_int(w, "tone_mask", pe_phone_tone_mask());
    pe_state_bool(w, "busy", pe_phone_flow_busy());
    pe_state_bool(w, "box_open", pe_phone_box_open());
    pe_state_bool(w, "holding", pe_phone_hold_active());
    if (pe_phone_spot_ready(&spot)) {
        pe_state_begin_object(w, "saved_at");
        pe_state_int(w, "map", spot.room);
        pe_state_int(w, "story", spot.story);
        pe_state_end(w);
    } else {
        pe_state_text(w, "saved_at", NULL);
    }
    pe_phone_spot_status(last, sizeof last);
    pe_state_text(w, "last_phone", last);
    pe_state_int(w, "calls", n);
    pe_state_int(w, "heard_count", pe_phone_heard_count());
    pe_state_begin_list(w, "heard");
    for (i = 0; i < n; ++i) {
        const PePhoneCall *call = pe_phone_call_at(i);
        if (call && call->key && pe_phone_record_heard(record, call->key)) pe_state_text(w, NULL, call->key);
    }
    pe_state_end(w);
}

void pe_phone_state_register(void) {
    (void)pe_mod_state_provider("calls", pe_phone_state, NULL);
}
