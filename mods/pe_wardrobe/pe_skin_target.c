/* The skins module's builder target, registered with the importer (F1 > Import): a catalogued
 * model fitted as a costume (pe_imp_fit.h: the RE human skeleton, rigid, its texture on two pages,
 * placeholder parts and zero-area faces dropped) becomes a Wardrobe costume (pe_skin_build.c). The
 * rig: the one registered for its id (a game's players), else the game's human rig when the fit
 * finds a person (RE2's people among its enemies). Import mode only. */
#include <stdio.h>
#include <string.h>

#include "pe_imp.h"
#include "pe_imp_fit.h"
#include "pe_imp_rig.h"
#include "pe_skin_build.h"

static int skins_build(const PeImpBuild *ctx, const char *asset_id, const char *folder, char *log, size_t logn,
                       char *why, size_t n) {
    PeImpModel m;
    PeImpFitInfo fit;
    if (pe_imp_fit(ctx->store, asset_id, PE_IMP_USE_COSTUME, &m, &fit, why, n)) return -1;   /* the reason is in why */
    const PeImpRig *rig = pe_imp_rig_for(asset_id);
    if (!rig && fit.person) {
        const char *colon = strchr(asset_id, ':');
        char game[16];
        snprintf(game, sizeof game, "%.*s", colon ? (int)(colon - asset_id) : 0, asset_id);
        rig = pe_imp_rig_for_game(game);
    }
    if (!rig) { pe_imp_model_free(&m); snprintf(why, n, "no rig fits %s", asset_id); return -1; }
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
    {PE_IMP_API_VERSION, sizeof(PeImpTarget)}, "skins", 2, PE_IMP_MODEL, "Build as a costume", 1, skins_build};

int pe_skin_target_register(void) { return pe_imp_add_target(&k_skins); }
