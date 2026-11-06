#include "pe_fonts_names.h"

#include <string.h>

#define GLYPH_SPACE 0x0F
#define GLYPH_COLON 0x4A
#define CODE_EXTENDED 0xF1
#define CODE_LINE 0xF7
#define CODE_WAIT 0xF8
#define CODE_END 0xF9
#define CODE_PLAYER 0xFA
#define CODE_CONTROL 0xFB
#define CODE_KANJI_A 0xFC
#define CODE_KANJI_B 0xFD
#define CODE_STOP 0xFF

static const char *const kNames[] = {
    "Actress", "Admiral", "Audience", "Baker", "Ben", "Captain", "Cathy", "Cop", "Cop A", "Cop B",
    "Cop C", "Daniel", "Doctor", "Doorman", "Eddie", "Eve", "GSIs", "GSls", "Guard", "Japanese man",
    "Klamp", "Lab Tech", "Little girl", "Lynch", "Maeda", "Man", "Maya", "Naval Officer", "Nix",
    "Nurse", "Over radio", "Paramedic", "Parrot", "Patient", "Pilot", "Reporter", "Soldier",
    "Torres", "Warner", "Wayne", "Williams",
};

static int glyph_of(char c) {
    if (c >= 'A' && c <= 'Z') return 0x10 + (c - 'A');
    if (c >= 'a' && c <= 'z') return 0x30 + (c - 'a');
    return GLYPH_SPACE;
}

/* Length of "Name:" at `p`, or 0. */
static size_t name_at(const unsigned char *p, size_t left) {
    for (size_t k = 0; k < sizeof kNames / sizeof *kNames; ++k) {
        const char *name = kNames[k];
        size_t n = strlen(name), i = 0;
        if (n + 1 > left || p[n] != GLYPH_COLON) continue;
        while (i < n && p[i] == glyph_of(name[i])) ++i;
        if (i == n) return n + 1;
    }
    return 0;
}

/* How many bytes the code at `p` takes in a page; 0 if it runs past the end. */
static size_t step_of(const unsigned char *p, size_t left) {
    size_t n = 1;
    if (*p == CODE_KANJI_A || *p == CODE_KANJI_B) n = 2;
    else if (*p == CODE_CONTROL) n = left > 1 && (p[1] == 7 || p[1] == 9) ? 3 : 2;
    return n > left ? 0 : n;
}

size_t pe_fonts_color_names(const unsigned char *in, size_t in_max, unsigned char *out, size_t out_size) {
    size_t i = 0, o = 0;
    int line_start = 1, coloured = 0;
    while (i < in_max) {
        const unsigned char c = in[i];
        size_t run = 0, step;
        unsigned char open = 0;
        if (c == CODE_EXTENDED) return 0;
        if (line_start) {
            if (c == CODE_PLAYER && i + 1 < in_max && in[i + 1] == GLYPH_COLON) {
                run = 2;
                open = 4;                               /* the game's own red */
            } else if ((run = name_at(in + i, in_max - i)) != 0) {
                open = PE_TEXT_SPEAKER_COLOR;
            }
        }
        if (run) {
            if (o + run + 4 > out_size) return 0;
            out[o++] = CODE_CONTROL;
            out[o++] = open;
            memcpy(out + o, in + i, run);
            o += run;
            out[o++] = CODE_CONTROL;
            out[o++] = 5;
            i += run;
            line_start = 0;
            coloured = 1;
            continue;
        }
        step = step_of(in + i, in_max - i);
        if (!step || o + step > out_size) return 0;
        memcpy(out + o, in + i, step);
        o += step;
        i += step;
        if (c == CODE_STOP || c == CODE_END) return coloured ? o : 0;
        if (c == CODE_LINE || c == CODE_WAIT || c == CODE_CONTROL) line_start = 1;
        else if (c != GLYPH_SPACE) line_start = 0;
    }
    return 0;
}
