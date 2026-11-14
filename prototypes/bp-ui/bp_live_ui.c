/* Connects the approved UI to the pe_bp mod: catalogs are rebuilt from the mod's read-only
 * snapshot, confirmations go to its execute(). No rule, price or write lives here. */
#include "bp_live_ui.h"
#include "bp_catalog.h"
#include "bp_ui_native.h"
#include "pe_bp_screens.h"
#include "pe_mod_host.h"
#include <stdio.h>
#include <string.h>

static BpSnapshot s_snap;

static const PeBpScreensApi *api(void) {
    const PeBpScreensApi *a = pe_service_get("bp_screens", 1);
    return a && a->struct_size >= sizeof *a ? a : NULL;
}
static const BpUiCatalog *build(void) {
    const PeBpScreensApi *a = api();
    const BpUiCatalog *c;
    BpCatalogUsage u;
    if (!a) return NULL;
    if (!a->snapshot(&s_snap)) memset(&s_snap, 0, sizeof s_snap);
    c = bp_catalog_build(&s_snap, a->price);
    u = bp_catalog_usage();
    if (u.overflow) pe_mod_log("pe_bp", "catalog storage overflow: pages=%zu rows=%zu lines=%zu", u.pages, u.rows, u.lines);
    return c;
}
static const BpUiCatalog *refresh(void *user) { (void)user; return build(); }
static int commit(void *user, const char *request, char *notice, size_t size) {
    const PeBpScreensApi *a = api();
    BpResult r;
    BpSnapshot after;
    (void)user;
    if (!a) { snprintf(notice, size, "BP upgrades are off - nothing changed"); return 0; }
    r = a->execute(request);
    if (!a->snapshot(&after)) memset(&after, 0, sizeof after);
    bp_result_text(&r, &after, notice, size);
    return r.error == BP_OK;
}
static const BpUiBackend s_backend = {commit, refresh, NULL};

int pe_bp_live_available(void) { return api() != NULL; }
int pe_bp_live_refunds_available(void){const PeBpScreensApi *a=api();BpSnapshot s;return a&&a->snapshot(&s)&&s.loaded&&s.economy&&(s.features&BP_FEATURE_RESPEC);}
void pe_bp_live_install(void) { pe_bp_ui_set_source(build, &s_backend); }
