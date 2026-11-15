#ifndef PE_TRANSLATE_H
#define PE_TRANSLATE_H

/* Optional translations for mod-owned game text. Providers publish translations v1;
 * callers keep stable mod/<mod-id>/<key> keys and their own English fallback. No dependency
 * on pe_lang: an absent/inactive provider or missing entry leaves the original bytes intact.
 * Returned text is in the game's glyph encoding, not UTF-8 or a printf format string. */
#include "pe_gametext.h"
#include "pe1/mod_text.h"
#include "pe_mod_host.h"
#include "pe_localize.h"

typedef struct PeTranslationsApi {
    const unsigned char *(*find)(const char *key);
} PeTranslationsApi;

static inline const unsigned char *pe_tr(const char *key) {
    const PeTranslationsApi *api = pe_service_get("translations", 1);
    return api && api->find && key ? api->find(key) : NULL;
}

/* One plain label/fragment inside a caller-owned message. Translation cannot inject another
 * choice, line break or other command, and cannot partially fill the destination. The compiler
 * enforces the same single-line contract; this guard also covers another provider's output. */
static inline void pe_t_tr(PeText *t, const char *key, const char *english, unsigned max_glyphs) {
    const unsigned char *text = pe_tr(key);
    unsigned n = 0;
    if (text) {
        unsigned glyphs = 0;
        while (glyphs <= max_glyphs && n < PE_GAMETEXT_MAX && text[n] != G_END) {
            if (text[n] < 0xF1u) ++n;
            else if (Pe_ExtendedGlyph(text+n, NULL)) n += 4;
            else break;
            ++glyphs;
        }
        if (!glyphs || glyphs > max_glyphs || text[n] != G_END || n > PE_GAMETEXT_MAX - 2u - t->n) text = NULL;
    }
    if (!text) { pe_t_str(t, english); return; }
    for (unsigned i = 0; i < n; ++i) pe_t_byte(t, text[i]);
}
/* Player-facing UTF-8 builders opt in here. The old ASCII helpers stay dependency-free,
 * and v1 pe_tr callers retain their existing ABI and explicit-key contracts. */
static inline unsigned char *pe_tr_ascii(unsigned char *out, unsigned size, const char *source) {
    const PeInterfaceTextApi *api = pe_interface_text();
    if (!size) return out;
    const char *text = pe_l10n_ui(source);
    if (api && api->encode) { api->encode(out, size, text); return out; }
    return pe_text_from_ascii(out, size, text);
}
static inline void pe_t_ui(PeText *t, const char *source) {
    unsigned char text[PE_GAMETEXT_MAX];
    pe_tr_ascii(text, sizeof text, source);
    for (unsigned i=0; text[i] != G_END; ++i) pe_t_byte(t,text[i]);
}
#endif
