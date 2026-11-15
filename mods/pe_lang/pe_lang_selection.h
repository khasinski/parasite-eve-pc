#ifndef PE_LANG_SELECTION_H
#define PE_LANG_SELECTION_H

#include "pe_lang_pack.h"

/* Identity is the pack's language code AND edition/display name, never its
 * filename or position. Different translations can share a language code. */
typedef struct PeLangSelection { char lang[16], name[32]; } PeLangSelection;
int pe_lang_selection_find(const PeLangSelection *id, const PeLangPack *packs, unsigned count);
int pe_lang_selection_load(const char *path, PeLangSelection *id);
int pe_lang_selection_save(const char *path, const PeLangSelection *id);
/* Whether to ask the player for a language at start: only while nothing is saved yet (`saved` is
 * 0, so the answer, once saved, ends it), with a pack to offer, and with someone there to answer
 * (`can_ask`: a window, and the language mod set to run). */
int pe_lang_selection_ask(int saved, unsigned packs, int can_ask);

#endif
