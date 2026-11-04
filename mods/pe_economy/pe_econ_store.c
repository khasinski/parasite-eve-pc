#include "pe_econ_store.h"

#include "pe_extension_store.h"
#include "pe_game.h"
#include "pe_mod_host.h"

#define MOD_ID "pe_economy"

static uint8_t s_bytes[PE_ECON_RECORD_SIZE];
static const PeExtensionSection s_section = {"pe_economy/policy", 1, 1, sizeof s_bytes, s_bytes};
static PeEconState s_state;
static PeEconStoreStatus s_status;

static int enable_section(void) { return pe_extension_enable(&s_section) == PE_REG_OK; }

const PeEconState *pe_econ_store_state(void) { return &s_state; }
PeEconStoreStatus pe_econ_store_status(void) { return s_status; }

PeEconResult pe_econ_store_apply(const PeEconConfig *target) {
    PeEconResult r;
    if (s_status == PE_ECON_STORE_DAMAGED) return PE_ECON_BAD_STATE;
    r = pe_econ_apply(&s_state, target, pe_free_field(), enable_section);
    if (r == PE_ECON_OK) pe_econ_encode(&s_state, s_bytes);   /* crash reports copy it as it stands */
    return r;
}

/* The host has already put this save's section, or zeros, in s_bytes. */
static int on_save_read(void *ctx, void *user) {
    PeEconResult r;
    (void)ctx; (void)user;
    pe_econ_state_reset(&s_state);
    s_status = PE_ECON_STORE_READY;
    r = pe_econ_decode(s_bytes, &s_state);
    if (r != PE_ECON_OK) {
        s_status = PE_ECON_STORE_DAMAGED;
        pe_mod_log(MOD_ID, "save: policy section refused (%s); staying on Enhanced, section kept", pe_econ_reason(r));
        pe_toast("This save's Economy settings can't be read. Using Enhanced; they're kept unchanged.");
    } else if (!pe_extension_available(&s_section)) {
        s_status = PE_ECON_STORE_UNSUPPORTED;
        pe_mod_log(MOD_ID, "save: policy section unavailable (newer build or unusable companion); staying on Enhanced");
    }
    return 0;
}

/* The host has already cleared the section. */
static int on_new_game(void *ctx, void *user) {
    (void)ctx; (void)user;
    pe_econ_state_reset(&s_state);
    s_status = PE_ECON_STORE_READY;
    return 0;
}

void pe_econ_store_register(const char *mod_id) {
    pe_econ_state_reset(&s_state);
    if (pe_extension_register(&s_section) < 0) {
        s_status = PE_ECON_STORE_UNSUPPORTED;
        pe_mod_log(MOD_ID, "policy section not registered: the new Economy path is unavailable");
    }
    pe_hook_add(PE_HOOK_SAVE_READ, on_save_read, NULL, PE_PRIORITY_NORMAL, mod_id);
    pe_hook_add(PE_HOOK_NEW_GAME, on_new_game, NULL, PE_PRIORITY_NORMAL, mod_id);
}

#ifdef PE_ECON_STORE_TEST
uint8_t *pe_econ_store_test_bytes(void) { return s_bytes; }
#endif
