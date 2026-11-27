/* What pe_econ_store.c and the opt-in path need from the host, for fixtures that include the whole
 * wallet and rewards modules with no host behind them. The preview does the real arithmetic. */
#include "pe_extension_store.h"
int pe_extension_register(const PeExtensionSection *d) { (void)d; return 0; }
PeRegResult pe_extension_enable(const PeExtensionSection *d) { (void)d; return PE_REG_OK; }
int pe_extension_available(const PeExtensionSection *d) { (void)d; return 1; }
uint32_t pe_reg_generation(void) { return 1; }
static int s_test_notices;
PeRegResult pe_economy_policy_preview(const PePayoutContext *c, PePayoutResult *out) {
    uint64_t v = (uint64_t)c->nominal_10000 * (c->final_factor_10000 ? c->final_factor_10000 : 10000u) + c->remainder;
    out->junk = (uint32_t)(v / 100000000u);
    out->remainder = (uint32_t)(v % 100000000u);
    return PE_REG_OK;
}
PeRegResult pe_economy_policy_notify(const PePayoutContext *c, const PePayoutResult *v) {
    (void)c; (void)v;
    ++s_test_notices;
    return PE_REG_OK;
}
