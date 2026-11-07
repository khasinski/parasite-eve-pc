#ifndef PE_LANG_TEXT_H
#define PE_LANG_TEXT_H
#include "pe_lang_pack.h"
/* Plain labels/templates only. Returned UTF-8 belongs to the pack until it is freed. */
const char *pe_lang_text_utf8(PeLangPack *, const char *key);
unsigned pe_lang_text_encode(const PeLangPack *, unsigned char *, unsigned, const char *);
#endif
