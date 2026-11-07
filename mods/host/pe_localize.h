#ifndef PE_LOCALIZE_H
#define PE_LOCALIZE_H
/* Optional display-only UTF-8 text. Source strings and settings identities stay English;
 * mod/ui/<FNV1a(source)> entries supply translated labels and printf templates. */
#include <string.h>
#include "pe_mod_host.h"
typedef struct PeInterfaceTextApi {
    const char *(*localize)(const char *source);
    unsigned (*encode)(unsigned char *out, unsigned size, const char *utf8);
    const char *(*item_name)(unsigned id);
} PeInterfaceTextApi;
/* Compare complete printf directives, including widths/lengths; never accept a new %n,
 * conversion type, star operand or changed argument order from a pack. Literal %% is ignored. */
static inline const char *pe_format_next(const char *p, unsigned *length) {
    while ((p = strchr(p, '%')) != NULL) {
        const char *start = p++;
        if (*p == '%') { ++p; continue; }
        while (*p && strchr("-+ #0", *p)) ++p;
        if (*p == '*') ++p; else while (*p >= '0' && *p <= '9') ++p;
        if (*p == '.') { ++p; if (*p == '*') ++p; else while (*p >= '0' && *p <= '9') ++p; }
        while (*p && strchr("hljztL", *p)) ++p;
        if (*p && strchr("diuoxXfFeEgGaAcspn", *p)) {
            *length = (unsigned)(p + 1 - start); return start;
        }
        if (!*p) return NULL; /* a label may end in a literal percent */
    }
    return NULL;
}
static inline int pe_format_compatible(const char *source, const char *translated) {
    for (;;) {
        unsigned a = 0, b = 0;
        source = pe_format_next(source, &a); translated = pe_format_next(translated, &b);
        if (!source || !translated) return source == translated;
        if (a != b || memcmp(source, translated, a)) return 0;
        source += a; translated += b;
    }
}
static inline const PeInterfaceTextApi *pe_interface_text(void) {
#ifdef PE_INTERFACE_TEXT_FALLBACK
    return NULL; /* isolated legacy fixtures have no mod service host */
#else
    return pe_service_get("interface_text", 1);
#endif
}
static inline const char *pe_l10n_item(unsigned id, const char *fallback) {
    const PeInterfaceTextApi *api=pe_interface_text();
    const char *text=api && api->item_name ? api->item_name(id) : NULL;
    return text ? text : fallback;
}
static inline const char *pe_l10n_ui(const char *source) {
    const PeInterfaceTextApi *api = pe_interface_text();
    if (!source || !api || !api->localize) return source;
    const char *translated = api->localize(source);
    return translated && pe_format_compatible(source, translated) ? translated : source;
}
#endif
