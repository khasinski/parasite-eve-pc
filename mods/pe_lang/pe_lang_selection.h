#ifndef PE_LANG_SELECTION_H
#define PE_LANG_SELECTION_H

#include "pe_lang_pack.h"

/* Identity is the pack's language code AND edition/display name, never its
 * filename or position. Different translations can share a language code. */
typedef struct PeLangSelection { char lang[16], name[32]; } PeLangSelection;
int pe_lang_selection_find(const PeLangSelection *id, const PeLangPack *packs, unsigned count);
int pe_lang_selection_load(const char *path, PeLangSelection *id);
int pe_lang_selection_save(const char *path, const PeLangSelection *id);

#endif
