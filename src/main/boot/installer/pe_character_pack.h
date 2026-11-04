/* SPDX-License-Identifier: MIT -- bounded native subset format validation. */
#ifndef PE_CHARACTER_PACK_H
#define PE_CHARACTER_PACK_H
#include <stdint.h>
typedef struct {unsigned images;uint64_t encoded_bytes,decoded_bytes;char error[256];} PeCharacterPackReport;
/* Validates the prepared native subset, not a multi-GB emulator archive.
 * Reads only, never copies/deletes/renames. No claim of provenance or game match. */
int pe_install_verify_character_pack(const char *root,
 int(*pump)(const char*,uint64_t,uint64_t,void*),void *user,PeCharacterPackReport *report);
/* Cheap bounded discovery hint only. Full verification occurs after selection. */
int pe_install_character_pack_hint(const char *root);
#endif
