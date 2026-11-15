#ifndef PE_LANG_BUNDLE_H
#define PE_LANG_BUNDLE_H

/* A .pelang file as a player gets it (docs/LOCALIZATION.md, "Translation files"): either the
 * compiled pack itself, or a zip holding it as PE_LANG_BUNDLE_TEXT and, if it has any, its
 * pictures as PE_LANG_BUNDLE_PICTURES. The zip is told apart by its first bytes, not its name, so both kinds keep
 * the one extension. Files in the zip this reader doesn't know are left alone. */

#include <stddef.h>

#include "pe_lang_pack.h"

#define PE_LANG_BUNDLE_TEXT "text.bin"       /* the compiled pack inside a zipped .pelang */
#define PE_LANG_BUNDLE_PICTURES "pictures.bin" /* optional: its pictures (pe_lang_pictures.h) */
#define PE_LANG_BUNDLE_MAX (256u << 20)      /* largest .pelang file read, zipped or not */
#define PE_LANG_PACK_MAX (64u << 20)         /* largest pack, as pe_lang_pack_load allows */

typedef enum PeLangOpen {
    PE_LANG_OPEN_OK = 0,
    PE_LANG_OPEN_UNREADABLE,   /* missing, empty, too big or not readable */
    PE_LANG_OPEN_NO_ZIP,       /* a zipped file, in a build without the zip reader */
    PE_LANG_OPEN_BAD_ZIP,      /* a damaged zip, or none of it is PE_LANG_BUNDLE_TEXT */
    PE_LANG_OPEN_BAD_PACK,     /* the pack itself fails pe_lang_pack_parse */
    PE_LANG_OPEN_BAD_PICTURES, /* its pictures.bin is damaged or in a newer format */
} PeLangOpen;

/* The pack in `bytes` (`size` bytes, borrowed): a plain pack or a zipped .pelang. */
PeLangOpen pe_lang_bundle_parse(PeLangPack *pack, const unsigned char *bytes, size_t size);
/* The same for a file on disk. */
PeLangOpen pe_lang_bundle_load(PeLangPack *pack, const char *path);
/* A short reason a player can read, for a failed open. */
const char *pe_lang_bundle_error(PeLangOpen result);

#endif
