/* Weapons on view in the Wardrobe screen: the gun Aya holds (Parasite Eve draws one model for every
 * weapon) and each weapon imported from another game (F1 > Import; a reader's "weapon/..." Model is
 * a player holding it, and only the weapon's part is shown). View only, by design: nothing here
 * equips, unlocks, buys or changes a weapon, and nothing here reads or writes game state, the
 * inventory, saves or the game's VRAM. Models are read through the host's disc reader and the
 * import store into private copies, drawn by the model stage's own presenter slot. The list is read
 * when the screen opens; models load when shown. Tested by pe_mod_wardrobe_menu_test.c. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pe_wardrobe.h"
#include "pe_disc_assets.h"
#include "pe_imp.h"
#include "pe_imp_fit.h"
#include "pe_mod_host.h"
#include "pe_model.h"
#include "pe_model_imp.h"

#define PE_WEAPONS_MAX 256
#define PE_RING 6                       /* imported weapons kept converted (the model cache holds 4) */

typedef struct { char id[PE_IMP_ID_MAX], name[PE_IMP_NAME_MAX]; const char *group, *note; } PeWeapon;

static const struct { const char *game, *group, *note; } k_games[] = {
    {"re1", "Resident Evil", "Imported from Resident Evil"},
    {"re2", "Resident Evil 2", "Imported from Resident Evil 2"},
    {"re3", "Resident Evil 3", "Imported from Resident Evil 3"},
};

static PeWeapon s_list[PE_WEAPONS_MAX];
static int s_count;
static char s_store[PE_IMP_PATH_MAX];
static PeModel s_gun;
static int s_gun_state;                 /* 0 not read yet, 1 read, -1 couldn't be */
static struct { char id[PE_IMP_ID_MAX]; PeModel m; unsigned used; } s_ring[PE_RING];
static unsigned s_tick;

static void pe_ring_free(int i) {
    if (!s_ring[i].used) return;
    pe_model_forget(&s_ring[i].m);
    pe_model_free(&s_ring[i].m);
    memset(&s_ring[i], 0, sizeof s_ring[i]);
}

void pe_ward_weapons_release(void) {
    for (int i = 0; i < PE_RING; i++) pe_ring_free(i);
    if (s_gun_state == 1) { pe_model_forget(&s_gun); pe_model_free(&s_gun); }
    s_gun_state = 0;
}

void pe_ward_weapons_refresh(void) {
    pe_ward_weapons_release();          /* indexes may move: no model may outlive its entry */
    memset(&s_list[0], 0, sizeof s_list[0]);
    snprintf(s_list[0].name, sizeof s_list[0].name, "Aya's gun");
    s_list[0].group = "Parasite Eve", s_list[0].note = "Every gun Aya holds";
    s_count = 1;
    if (pe_mod_path(s_store, sizeof s_store, "import/store")) return;
    for (size_t g = 0; g < sizeof k_games / sizeof k_games[0]; g++) {
        PeImpAsset *a = NULL;
        const int n = pe_imp_catalog_assets(s_store, k_games[g].game, &a);
        for (int i = 0; i < n && s_count < PE_WEAPONS_MAX; i++) {
            if (strcmp(a[i].type, "Model") || pe_imp_weapon_part(a[i].id) < 0) continue;
            PeWeapon *w = &s_list[s_count++];
            snprintf(w->id, sizeof w->id, "%s", a[i].id);
            snprintf(w->name, sizeof w->name, "%s", a[i].name);
            w->group = k_games[g].group, w->note = k_games[g].note;
        }
        free(a);
    }
}

int pe_ward_weapons_count(void) { return s_count; }
const char *pe_ward_weapon_name(int k) { return k >= 0 && k < s_count ? s_list[k].name : ""; }
const char *pe_ward_weapon_group(int k) { return k >= 0 && k < s_count ? s_list[k].group : ""; }
const char *pe_ward_weapon_note(int k) { return k >= 0 && k < s_count ? s_list[k].note : ""; }

static PeModel *pe_load_gun(void) {
    PeRoomBlobs blobs;
    if (s_gun_state) return s_gun_state > 0 ? &s_gun : NULL;
    s_gun_state = -1;
    if (!pe_disc_held_gun(&blobs)) return NULL;
    if (pe_model_decode_held_gun(&blobs, &s_gun)) s_gun_state = 1;
    else pe_mod_log("pe_wardrobe", "weapons: the held gun did not decode");
    pe_disc_room_free(&blobs);
    return s_gun_state > 0 ? &s_gun : NULL;
}

static PeModel *pe_load_imported(const PeWeapon *w) {
    PeImpModel fitted;
    PeImpFitInfo info;
    char why[200] = "";
    int slot = 0;
    for (int i = 0; i < PE_RING; i++)
        if (s_ring[i].used && !strcmp(s_ring[i].id, w->id)) { s_ring[i].used = ++s_tick; return &s_ring[i].m; }
    for (int i = 1; i < PE_RING; i++) if (s_ring[i].used < s_ring[slot].used) slot = i;   /* least recent */
    pe_ring_free(slot);
    if (pe_imp_fit(s_store, w->id, PE_IMP_USE_PROP, &fitted, &info, why, sizeof why)) {
        pe_mod_log("pe_wardrobe", "weapons: %s: %s", w->id, why);
        return NULL;
    }
    const int rc = pe_model_from_imp(&fitted, pe_imp_weapon_part(w->id), &s_ring[slot].m, why, sizeof why);
    pe_imp_model_free(&fitted);
    if (rc) { pe_mod_log("pe_wardrobe", "weapons: %s: %s", w->id, why); return NULL; }
    snprintf(s_ring[slot].id, sizeof s_ring[slot].id, "%s", w->id);
    s_ring[slot].used = ++s_tick;
    return &s_ring[slot].m;
}

static PeModel *pe_load(void *user) {
    const int k = (int)(intptr_t)user;
    if (k < 0 || k >= s_count) return NULL;
    return k == 0 ? pe_load_gun() : pe_load_imported(&s_list[k]);
}

int pe_ward_weapon_source(int k, PeModelSource *src) {
    memset(src, 0, sizeof *src);
    if (k < 0 || k >= s_count) return 0;
    src->kind = PE_MODEL_CALLBACK, src->load = pe_load, src->user = (void *)(intptr_t)k;
    return 1;
}
