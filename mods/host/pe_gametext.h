#ifndef PE_GAMETEXT_H
#define PE_GAMETEXT_H

/* Building dialogue-box messages in the game's glyph code (the recomp build's pe_gametext.h, on
 * native text tables): ASCII text, numbers, the game's own item names, line breaks, a choice of
 * n options (FB 09 n) and the red highlight (FB 04 .. FB 05). */

#include <stdio.h>
#include <string.h>

#include "pe1/text.h"

#define G_NEWLINE 0xF7u
#define G_END     0xFFu
#define G_CODE    0xFBu
#define G_CHOICE  0x09u
#define G_RED     0x04u
#define G_NORMAL  0x05u
#define PE_GAMETEXT_MAX 768

typedef struct PeText {
    unsigned char b[PE_GAMETEXT_MAX];
    unsigned n, line, widest;
    int lines;
} PeText;

static inline unsigned char pe_glyph(char c) {
    if (c >= 'A' && c <= 'Z') return (unsigned char)(0x10 + (c - 'A'));
    if (c >= 'a' && c <= 'z') return (unsigned char)(0x30 + (c - 'a'));
    if (c >= '0' && c <= '9') return (unsigned char)(c - '0');
    switch (c) {                          /* from the game's own dialogue */
    case '.': return 0x2F; case ',': return 0x4B; case ':': return 0x4A;
    case '!': return 0x2B; case '?': return 0x2C; case '\'': return 0x2E; case '/': return 0x4C;
    case '+': return 0x0A; case '-': return 0x0B; case '%': return 0x0E;   /* pe1/pe_charset.h */
    default: return 0x0F;                 /* space */
    }
}

/* ASCII text in glyph code into `out` (size bytes, 0xFF-terminated); returns out. */
static inline unsigned char *pe_text_from_ascii(unsigned char *out, unsigned size, const char *ascii) {
    unsigned n = 0;
    for (; *ascii && n + 1 < size; ++ascii) out[n++] = pe_glyph(*ascii);
    out[n] = G_END;
    return out;
}

/* A game message back to ASCII, for logs: line breaks as " / ", other codes as {XX}. */
static inline char *pe_text_to_ascii(char *out, unsigned size, const unsigned char *msg) {
    unsigned n = 0, i;
    for (i = 0; msg && msg[i] != G_END && i < 512 && n + 6 < size; ++i) {
        const unsigned char c = msg[i];
        if (c < 10) out[n++] = (char)('0' + c);
        else if (c >= 0x10 && c < 0x2A) out[n++] = (char)('A' + c - 0x10);
        else if (c >= 0x30 && c < 0x4A) out[n++] = (char)('a' + c - 0x30);
        else switch (c) {
        case 0x0F: out[n++] = ' '; break;
        case 0x2F: out[n++] = '.'; break; case 0x4B: out[n++] = ','; break;
        case 0x4A: out[n++] = ':'; break; case 0x2B: out[n++] = '!'; break;
        case 0x2E: out[n++] = '\''; break; case 0x4C: out[n++] = '/'; break;
        case 0x2C: out[n++] = '?'; break;
        case 0x0A: out[n++] = '+'; break; case 0x0B: out[n++] = '-'; break; case 0x0E: out[n++] = '%'; break;
        case G_NEWLINE: n += (unsigned)snprintf(out + n, size - n, " / "); break;
        default: n += (unsigned)snprintf(out + n, size - n, "{%02X}", c); break;
        }
    }
    out[n < size ? n : size - 1] = 0;
    return out;
}

static inline void pe_t_init(PeText *t) {
    memset(t, 0, sizeof *t);
    t->lines = 1;
}

static inline void pe_t_byte(PeText *t, unsigned char c) {
    if (t->n < PE_GAMETEXT_MAX - 2) t->b[t->n++] = c;
    if (c == G_NEWLINE) { ++t->lines; t->line = 0; }
    else if (c < 0xF0 && ++t->line > t->widest) t->widest = t->line;
}

static inline void pe_t_str(PeText *t, const char *s) { while (*s) pe_t_byte(t, pe_glyph(*s++)); }

static inline void pe_t_num(PeText *t, unsigned v) {
    char d[12];
    snprintf(d, sizeof d, "%u", v);
    pe_t_str(t, d);
}

/* The game's own name for item `id` (1-based), as its menus print it. */
static inline void pe_t_item(PeText *t, unsigned id) {
    const unsigned char *name = id ? (const unsigned char *)Text_LookupTable8(id - 1u) : NULL;
    unsigned i;
    if (!name) { pe_t_str(t, "?"); return; }
    for (i = 0; i < 24 && name[i] != G_END; ++i) pe_t_byte(t, name[i]);
    if (id == 0x02) pe_t_str(t, " x15");  /* both bullet boxes are "Bullets" */
    if (id == 0x03) pe_t_str(t, " x30");
}

static inline void pe_t_choice(PeText *t, int count) {
    pe_t_byte(t, G_CODE);
    pe_t_byte(t, G_CHOICE);
    pe_t_byte(t, (unsigned char)count);
}

/* Start option line: a new line (except the first) and the cursor's indent. */
static inline void pe_t_option(PeText *t, int first) {
    if (!first) pe_t_byte(t, G_NEWLINE);
    pe_t_str(t, "    ");
}

static inline void pe_t_red(PeText *t, int on) {
    pe_t_byte(t, G_CODE);
    pe_t_byte(t, on ? G_RED : G_NORMAL);
}

#endif
