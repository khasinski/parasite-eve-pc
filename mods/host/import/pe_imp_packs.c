/* Writing packs in the format the Wardrobe loads today (mods/pe_wardrobe/pe_packs.c, format 1):
 *   <packs>/<game>-local/pack.json   {"format": 1, "id": "<game>-local", ...} (once)
 *   <packs>/<game>-local/LICENSE     personal use only (once)
 *   <packs>/<game>-local/characters/<name>.<game>/character.json  name, group, tier, source
 * The same pack id and folder names as tools/re2-skin, so a costume's key ("re2-local/claire.re2")
 * is the same whichever builder made it. Format 1 reads only the keys it names, so the `source`
 * block (where the asset came from) is safe. */
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "pe_imp_internal.h"

static const char k_license[] =
    "These costumes were built on this computer or console by Parasite Eve PC's importer (F1 > Import)\n"
    "from the player's own copy of another game. The models and textures belong to that game's owner.\n"
    "For personal use only: do not share, upload or sell this pack.\n";

static int write_text(const char *path, const char *text) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    int bad = fputs(text, f) < 0;
    return fclose(f) || bad ? -1 : 0;
}

int pe_imp_target_place(const PeImpTarget *t, const char *packs, const PeImpAsset *a, const char *builder,
                        char *folder, size_t fn, char *why, size_t n) {
    return t->place ? t->place(a, builder, folder, fn, why, n) : pe_imp_pack_character(packs, a, builder, folder, fn, why, n);
}

int pe_imp_pack_character(const char *packs, const PeImpAsset *a, const char *builder,
                          char *folder, size_t fn, char *why, size_t n) {
    char game[16] = "", local[PE_IMP_ID_MAX] = "", name[48], pack[PE_IMP_PATH_MAX], path[PE_IMP_PATH_MAX];
    const char *colon = strchr(a->id, ':');
    if (!colon || (size_t)(colon - a->id) >= sizeof game) { if (why && n) snprintf(why, n, "bad asset ID %s", a->id); return -1; }
    memcpy(game, a->id, (size_t)(colon - a->id));
    const char *last = strrchr(colon + 1, '/');
    snprintf(local, sizeof local, "%s", last ? last + 1 : colon + 1);
    /* folder "<name>.<game>": letters, digits, . _ - $ as pe_packs.c accepts, at most 47 */
    snprintf(name, sizeof name, "%.30s.%s", local, game);
    for (char *c = name; *c; c++) if (!(isalnum((unsigned char)*c) || strchr("._-$", *c))) *c = '_';
    snprintf(pack, sizeof pack, "%s/%s-local", packs, game);
    if (snprintf(folder, fn, "%s/characters/%s", pack, name) >= (int)fn || pe_imp_mkdirs(folder)) {
        if (why && n) snprintf(why, n, "can't create %s", folder);
        return -1;
    }
    snprintf(path, sizeof path, "%s/pack.json", pack);
    FILE *probe = fopen(path, "rb");
    if (probe) fclose(probe);
    else {
        FILE *f = fopen(path, "wb");
        if (!f) { if (why && n) snprintf(why, n, "can't write %s", path); return -1; }
        fprintf(f, "{\n  \"format\": 1,\n  \"id\": \"%s-local\",\n  \"name\": ", game);
        char title[96];
        snprintf(title, sizeof title, "%s (built locally)", pe_imp_game_title(game));
        pj_write_string(f, title);
        fputs(",\n  \"version\": \"1\",\n  \"author\": \"built with Parasite Eve PC's importer\",\n"
              "  \"license\": \"personal use only, see LICENSE\",\n  \"kind\": \"assets\",\n  \"min_wardrobe\": 1\n}\n", f);
        if (fclose(f)) { if (why && n) snprintf(why, n, "can't write %s", path); return -1; }
    }
    snprintf(path, sizeof path, "%s/LICENSE", pack);
    probe = fopen(path, "rb");
    if (probe) fclose(probe);
    else if (write_text(path, k_license)) { if (why && n) snprintf(why, n, "can't write %s", path); return -1; }
    /* "Claire (RE2)" */
    char display[96], up[16];
    size_t k = 0;
    for (; game[k] && k + 1 < sizeof up; k++) up[k] = (char)toupper((unsigned char)game[k]);
    up[k] = 0;
    snprintf(display, sizeof display, "%s (%s)", a->name, up);
    snprintf(path, sizeof path, "%s/character.json", folder);
    FILE *f = fopen(path, "wb");
    if (!f) { if (why && n) snprintf(why, n, "can't write %s", path); return -1; }
    fputs("{\n  \"name\": ", f), pj_write_string(f, display);
    fputs(",\n  \"group\": ", f), pj_write_string(f, pe_imp_game_group(game));
    fputs(",\n  \"tier\": \"extra\",\n  \"source\": {\"game\": ", f), pj_write_string(f, game);
    fputs(", \"asset\": ", f), pj_write_string(f, a->id);
    fputs(", \"sha256\": ", f), pj_write_string(f, a->sha256);
    fputs(", \"platform\": ", f), pj_write_string(f, a->platform);
    fputs(", \"path\": ", f), pj_write_string(f, a->path);
    fputs(", \"builder\": ", f), pj_write_string(f, builder);
    fputs("}\n}\n", f);
    if (fclose(f)) { if (why && n) snprintf(why, n, "can't write %s", path); return -1; }
    return 0;
}
