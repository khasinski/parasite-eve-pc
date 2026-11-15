/* BP overhaul (design: PE_BP_Overhaul_Revised_2026-11-12.md): mod entry. Registers the save
 * store, the battle effects and the F1 page, and publishes two services:
 *   "bp"          v1  public, for other mods (host/pe_services.h)
 *   "bp_screens"  v1  the BP screens' full snapshot (pe_bp_screens.h; this mod's own UI)
 * How the parts fit: pe_bp_internal.h. */
#include <string.h>

#include "pe_bp_internal.h"
#include "pe_bp_screens.h"
#include "pe_bp_settings.h"
#include "pe_bp_battle.h"
#include "pe_game.h"
#include "pe_mod_host.h"

#define MOD_ID "pe_bp"

/* ---- events ---- */
static int s_ev_changed = -1, s_ev_effect = -1;
static void emit(const char *event, const void *ctx) {
    const int id = !strcmp(event, "bp.changed") ? s_ev_changed : !strcmp(event, "bp.effect") ? s_ev_effect : -1;
    if (id >= 0) pe_event_emit(id, (void *)ctx);
}
static void log_line(const char *line) { pe_mod_log(MOD_ID, "%s", line); }
static int readable(void) { return bp_store_readable(); }
static const BpApiHost s_host = {
    NULL, bp_store_record, readable, pe_bp_settings_flags, pe_bp_settings_carry_mode, bp_game_bp, emit, log_line
};

/* ---- "bp_screens" ---- */
static BpResult screens_execute(const char *request) { return bp_api_execute(request, NULL); }
static const PeBpScreensApi s_screens = {sizeof(PeBpScreensApi), bp_game_snapshot, bp_game_price, screens_execute};

static int bp_init(void) {
    static BpApiHost host;
    host = s_host;
    host.ops = bp_game_ops();
    bp_api_bind(&host);
    s_ev_changed = pe_event_id("bp.changed");
    s_ev_effect = pe_event_id("bp.effect");
    pe_bp_menu_register();
    bp_store_init(MOD_ID);
    pe_service_publish("bp", 1, bp_api_public(), MOD_ID);
    pe_service_publish("bp_screens", 1, &s_screens, MOD_ID);
    pe_bp_battle_init(MOD_ID, bp_store_record);
    return 0;
}

const PeMod pe_mod_pe_bp = {MOD_ID, "BP Overhaul (preview)", "0.2", NULL, bp_init, NULL};
