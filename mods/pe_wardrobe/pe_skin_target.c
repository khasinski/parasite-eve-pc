/* The skins module's builder target, registered with the importer (F1 > Import): a catalogued
 * model that a rig fits becomes a Wardrobe costume (pe_skin_build.c). Import mode only. */
#include <stdio.h>

#include "pe_imp.h"
#include "pe_imp_rig.h"
#include "pe_skin_build.h"

static int skins_build(const PeImpBuild *ctx, const char *asset_id, const char *folder, char *log, size_t logn,
                       char *why, size_t n) {
    const PeImpRig *rig = pe_imp_rig_for(asset_id);
    if (!rig) { snprintf(why, n, "no rig fits %s", asset_id); return -1; }
    PeImpModel m;
    if (pe_imp_catalog_model(ctx->store, asset_id, &m, why, n)) return -1;
    PeSkinOut f;
    int rc = pe_skin_build(ctx->pe, &m, rig, ctx->normals, ctx->progress, &f, why, n);
    pe_imp_model_free(&m);
    if (rc) return -1;
    if (log && logn)
        snprintf(log, logn, "%d vertices, %d texture groups, %d room animations", f.vertices, f.groups, f.room_rows);
    rc = pe_skin_write(&f, folder, why, n);
    pe_skin_files_free(&f);
    return rc;
}

static const PeImpTarget k_skins = {
    {PE_IMP_API_VERSION, sizeof(PeImpTarget)}, "skins", 1, PE_IMP_MODEL, "Build as a costume", 1, skins_build};

int pe_skin_target_register(void) { return pe_imp_add_target(&k_skins); }
