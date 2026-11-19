#ifndef PE_IMP_INTERNAL_H
#define PE_IMP_INTERNAL_H
/* Shared between the importer core's own files; not for mods. */
#include "pe_imp.h"
#include "pe_imp_json.h"

void pe_imp_rename(const char *game, const char *proposed, const char **id, const char **name);
void pe_imp_registry_reset(void);           /* tests */
int pe_imp_mkdirs(const char *path);        /* every missing folder along `path` */

#endif
