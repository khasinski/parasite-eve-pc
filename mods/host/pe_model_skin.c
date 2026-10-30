/* Wardrobe preview assets. Private copies only: no gameplay skin loader, live rows or GPU writes. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pe_model.h"
#include "pe_disc_assets.h"
#include "pe_mod_host.h"

static int pe_read(const char *folder, const char *name, uint8_t **data, uint32_t *size) {
    char rel[240], path[1200];
    FILE *f;
    long n;
    if (snprintf(rel, sizeof rel, "pe_skins/%s/%s", folder, name) >= (int)sizeof rel ||
        pe_mod_path(path, sizeof path, rel) != 0 || !(f = fopen(path, "rb"))) return 0;
    if (fseek(f, 0, SEEK_END) || (n = ftell(f)) < 8 || n > 0x400000 || fseek(f, 0, SEEK_SET)) { fclose(f); return 0; }
    *data = malloc((size_t)n);
    if (!*data) { fclose(f); return 0; }
    *size = (uint32_t)n;
    if (fread(*data, 1, (size_t)n, f) != (size_t)n) { free(*data); *data = NULL; *size = 0; fclose(f); return 0; }
    fclose(f);
    return 1;
}

int pe_model_skin_load(const PeModelSource *src, PeModel *out) {
    PeRoomBlobs assets = {0};
    int ok;
    memset(out, 0, sizeof *out);
    if (src->folder && *src->folder) {
        ok = pe_read(src->folder, "model.bin", &assets.data, &assets.data_size) &&
             pe_read(src->folder, "texture.bin", &assets.texture, &assets.texture_size);
    } else ok = pe_disc_player(src->map, &assets);
    if (ok) ok = pe_model_decode_player(&assets, out);
    pe_disc_room_free(&assets);
    if (!ok) pe_mod_log(NULL, "wardrobe: preview unavailable for %s (bank %u)", src->folder ? src->folder : "Aya", src->map);
    return ok;
}
