#ifndef PE_MOD_TEXT_H
#define PE_MOD_TEXT_H
/* Optional language-pack token. Operands never contain a string terminator or command.
 * The magic distinguishes it from the original rename grid's F1 glyph. */
static inline unsigned Pe_ExtendedGlyph(const unsigned char *p, unsigned *id) {
    if (p[0] != 0xF1 || p[1] != 0xC5 || p[2] >= 240 || p[3] >= 240) return 0;
    if (id) *id = p[2] * 240u + p[3];
    return 4;
}
#endif
