#ifndef PE_MODEL_IMP_H
#define PE_MODEL_IMP_H

/* An imported Model (host/import/pe_imp.h) as a PeModel (pe_model.h), for the viewers (the
 * Wardrobe's weapons). Data only: nothing is drawn or uploaded to the game's VRAM. */

#include <stddef.h>
#include "pe_imp.h"
#include "pe_model.h"

/* The Model's rest pose baked into one part (rigid parts at their bones' rest positions, skinned
 * corners as stored), no animation. Its texture goes to a private VRAM as the import addresses it
 * (texel x = (page - the model's first page) * 128 + u; palette = the face's palette row's rank
 * among the model's rows): 8-bit pages from x 0, palettes from row 256. `part` >= 0 keeps only that
 * part (a weapon in a hand). 0, or -1 with the reason in `why`. Free with pe_model_free. */
int pe_model_from_imp(const PeImpModel *m, int part, PeModel *out, char *why, size_t n);

#endif
