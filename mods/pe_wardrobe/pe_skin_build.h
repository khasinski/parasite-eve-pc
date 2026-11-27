#ifndef PE_SKIN_BUILD_H
#define PE_SKIN_BUILD_H
/* The skins module's builder target: a source model fitted onto Aya's skeleton, written as the
 * skin loader's files. Import mode only. Ported from tools/re2-skin/build_re2_skin.py (PR #4);
 * every file but texture.bin comes out byte for byte the same (texture.bin differs only in the
 * colour reduction: ours replaces Pillow's median cut). */
#include "pe_imp.h"
#include "pe_imp_pe.h"
#include "pe_imp_rig.h"

typedef struct {
    PeImpBuf model, texture, day[8], rooms, meta;
    int vertices, quads, tris, groups, room_rows;
    char log[512];
} PeSkinOut;

/* normals: Aya's 240-entry normal table (g_RenderNormalTable: x, y, z, pad). prog may be NULL:
 * it hears each step and the room scan, the slow part. */
int pe_skin_build(PeImpPe *pe, const PeImpModel *src, const PeImpRig *rig, const int16_t (*normals)[4],
                  const PeImpProgress *prog, PeSkinOut *out, char *why, size_t n);
void pe_skin_files_free(PeSkinOut *f);
/* The files in a folder (model.bin, texture.bin, day1-8.bin, rooms.bin, meta.bin). */
int pe_skin_write(const PeSkinOut *f, const char *folder, char *why, size_t n);
/* Register the skins builder target with the importer (the Wardrobe's init). */
int pe_skin_target_register(void);

#endif
