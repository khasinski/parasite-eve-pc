/* A call as dialogue pages in the game's glyphs, and spotting the phone's save question.
 * Pure: the glyph widths come from the caller (the game's font in play, fixed ones in tests). */
#include <string.h>

#include "pe_gametext.h"
#include "pe_phone_core.h"

#define PE_GLYPH_STAR 0x0Du   /* pe1/pe_charset.h; pe_glyph() has no '*' */
#define PE_GLYPH_END_PAGE 0xF9u

static uint8_t pe_glyph_of(char c) { return c == '*' ? PE_GLYPH_STAR : pe_glyph(c); }

int pe_phone_text_ok(const char *t) {
    for (; t && *t; ++t) {
        const char c = *t;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) continue;
        if (strchr(" .,:!?'-+*%/", c)) continue;
        if (c == '{' && !strncmp(t, "{name}", 6)) { t += 5; continue; }
        return 0;
    }
    return t != NULL;
}

int pe_phone_encode(uint8_t *out, int cap, const char *s) {
    int n = 0;
    while (s && *s && n < cap) {
        if (!strncmp(s, "{name}", 6)) { out[n++] = PE_PHONE_GLYPH_NAME; s += 6; continue; }
        out[n++] = pe_glyph_of(*s++);
    }
    return n;
}

static int pe_width(PePhoneAdvance adv, const uint8_t *g, int n) {
    int w = 0, i;
    for (i = 0; i < n; ++i) w += adv(g[i]);
    return w;
}

static PePhonePage *pe_page_new(PePhoneScript *s, int style) {
    PePhonePage *p;
    if (s->count >= PE_PHONE_MAX_PAGES) return NULL;
    p = &s->pages[s->count++];
    memset(p, 0, sizeof *p);
    p->style = (uint8_t)style;
    p->lines = 1;
    return p;
}

/* One speaker's line as boxes: the prefix, then the words, wrapped; the prefix again on each box.
 * Returns the boxes made, -1 when out of pages (or a word fits no box). */
static int pe_add_line(PePhoneScript *s, PePhoneAdvance adv, const char *caller, int aya, const char *text) {
    const int style = aya ? PE_PHONE_BAND : PE_PHONE_WINDOW;
    const int margin = style == PE_PHONE_WINDOW ? adv(PE_PHONE_GLYPH_SPACE) : 0;   /* a space each side */
    const int max = PE_PHONE_TEXT_MAX_W - 2 * margin;
    const int space = adv(PE_PHONE_GLYPH_SPACE);
    uint8_t prefix[48], body[512];
    int np, nb, i = 0, boxes = 0;
    if (aya) { prefix[0] = PE_PHONE_GLYPH_NAME; np = 1; }
    else np = pe_phone_encode(prefix, (int)sizeof prefix - 1, caller);
    prefix[np++] = pe_glyph(':');
    nb = pe_phone_encode(body, (int)sizeof body, text);
    while (i < nb && body[i] == PE_PHONE_GLYPH_SPACE) ++i;
    while (i < nb || boxes == 0) {
        PePhonePage *p = pe_page_new(s, style);
        int line_w, at = 0, k, words = 1;                 /* the prefix counts as the first word */
        if (!p) return -1;
        ++boxes;
        if (margin) p->text[at++] = PE_PHONE_GLYPH_SPACE;
        memcpy(p->text + at, prefix, (size_t)np);
        at += np;
        line_w = pe_width(adv, prefix, np);
        p->width = (uint16_t)line_w;
        while (i < nb) {
            int j = i, ww;
            while (j < nb && body[j] != PE_PHONE_GLYPH_SPACE) ++j;   /* the word body[i..j) */
            ww = pe_width(adv, body + i, j - i);
            if (at + (j - i) + 4 >= PE_PHONE_PAGE_BYTES) break;     /* no room: the next box */
            if (words && line_w + space + ww > max) {
                if (p->lines >= PE_PHONE_MAX_LINES) break;          /* the next box */
                p->text[at++] = G_NEWLINE;
                ++p->lines;
                if (margin) p->text[at++] = PE_PHONE_GLYPH_SPACE;
                line_w = 0;
                words = 0;
            }
            if (words) { p->text[at++] = PE_PHONE_GLYPH_SPACE; line_w += space; }
            for (k = i; k < j; ++k) p->text[at++] = body[k];
            line_w += ww;
            ++words;
            if (line_w > p->width) p->width = (uint16_t)line_w;
            i = j;
            while (i < nb && body[i] == PE_PHONE_GLYPH_SPACE) ++i;
        }
        p->width = (uint16_t)(p->width + 2 * margin);
        p->text[at] = G_END;
        if (words == 1 && p->lines == 1 && i < nb) return -1;      /* a word that fits no box */
    }
    return boxes;
}

static void pe_add_plain(PePhoneScript *s, PePhoneAdvance adv, const char *text) {
    PePhonePage *p = pe_page_new(s, PE_PHONE_BAND);
    int n;
    if (!p) return;
    n = pe_phone_encode(p->text, PE_PHONE_PAGE_BYTES - 1, text);
    p->text[n] = G_END;
    p->width = (uint16_t)pe_width(adv, p->text, n);
}

void pe_phone_script_build(PePhoneScript *s, const PePhoneCall *call, PePhoneAdvance adv) {
    int i;
    s->count = 0;
    pe_add_plain(s, adv, "*dial... ring... ring...*");
    if (!call) {
        pe_add_plain(s, adv, "*ring... ring...*");
        pe_add_plain(s, adv, "Nobody's picking up.");
    } else {
        for (i = 0; i < call->line_count; ++i)
            if (pe_add_line(s, adv, call->caller, call->lines[i].speaker == PE_CALL_AYA, call->lines[i].text) < 0) break;
    }
    if (s->count >= PE_PHONE_MAX_PAGES) s->count = PE_PHONE_MAX_PAGES - 1;   /* room for the click */
    s->click = s->count;
    pe_add_plain(s, adv, "*click*");
}

int pe_phone_script_problems(const PePhoneScript *s, const PePhoneCall *call, int *too_wide, int *ran_on) {
    int k, wide = 0;
    const int ran = call ? s->count - 2 - call->line_count : 0;
    for (k = 0; k < s->count; ++k) if (s->pages[k].width > PE_PHONE_TEXT_MAX_W) ++wide;
    if (too_wide) *too_wide = wide;
    if (ran_on) *ran_on = ran > 0 ? ran : 0;
    return wide || ran > 0;
}

int pe_phone_use_option(const uint8_t *m) {
    char ascii[400];
    int i, choice = -1, option = 0;
    if (!m) return -1;
    for (i = 0; i < 380 && m[i] != G_END && m[i] != PE_GLYPH_END_PAGE; ++i)
        if (m[i] == G_CODE && m[i + 1] == G_CHOICE) { choice = i + 3; break; }
    if (choice < 0) return -1;
    for (i = choice; i < 380 && m[i] != G_END && m[i] != PE_GLYPH_END_PAGE; ++option) {   /* option by option */
        uint8_t line[64];
        int n = 0;
        while (i < 380 && m[i] != G_NEWLINE && m[i] != G_END && m[i] != PE_GLYPH_END_PAGE && n < 62) line[n++] = m[i++];
        line[n] = G_END;
        pe_text_to_ascii(ascii, sizeof ascii, line);
        if (strstr(ascii, "Use the phone")) return option;
        if (m[i] == G_NEWLINE) ++i;
    }
    return -1;
}

int pe_phone_save_question(const uint8_t *m) {
    char ascii[400];
    int i, choice2 = 0;
    if (!m) return 0;
    for (i = 0; i < 380 && m[i] != G_END && m[i] != PE_GLYPH_END_PAGE; ++i)
        if (m[i] == G_CODE && m[i + 1] == G_CHOICE && m[i + 2] == 2) choice2 = 1;
    if (!choice2) return 0;
    pe_text_to_ascii(ascii, sizeof ascii, m);
    if (!strstr(ascii, "Will you save your progress?")) return 0;
    return m[0] == PE_PHONE_GLYPH_NAME ? 2 : 1;
}
