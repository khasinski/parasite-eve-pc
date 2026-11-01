#ifndef PE_UTF8_H
#define PE_UTF8_H

/* Read one Unicode scalar from a NUL-terminated UI string. Invalid/truncated
 * sequences become U+FFFD; consume at least the lead byte without reading past NUL. */
static inline unsigned pe_utf8_next(const char **text) {
    const unsigned char *p = (const unsigned char *)*text;
    unsigned c = *p, value, minimum, count;
    if (!c) return 0;
    ++p; *text = (const char *)p;
    if (c < 0x80) return c;
    if (c >= 0xC2 && c <= 0xDF) { value = c & 0x1F; minimum = 0x80; count = 1; }
    else if (c >= 0xE0 && c <= 0xEF) { value = c & 0x0F; minimum = 0x800; count = 2; }
    else if (c >= 0xF0 && c <= 0xF4) { value = c & 7; minimum = 0x10000; count = 3; }
    else return 0xFFFD;
    for (unsigned i = 0; i < count; ++i) {
        if (*p < 0x80 || *p > 0xBF) return 0xFFFD;
        value = (value << 6) | (*p++ & 0x3F);
        *text = (const char *)p;
    }
    return value < minimum || value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF) ? 0xFFFD : value;
}
#endif
