/* Cyrillic art has no game-state dependencies and never overwrites occupied cells. */
#include <string.h>
#include "pe_lang_cyrillic.h"
#include "pe_lang_font.h"
_Static_assert(PE_LATIN_FIRST_CODE + PE_LATIN_COUNT <= PE_CYRILLIC_FIRST_CODE, "font ranges overlap");
_Static_assert(PE_CYRILLIC_LAST_CODE < 0xF1, "glyph code would enter command range");
void pe_lang_cyrillic_compose(const PeCyrillicGlyph *g, unsigned char out[144]) {
    memset(out, 0, 144);
    for (unsigned y=0;y<12;++y) for (unsigned x=0;x<12;++x)
        if (g->rows[y] & (1u<<x)) out[y*12+x]=1;
    for (unsigned y=0;y<11;++y) for (unsigned x=0;x<11;++x)
        if (out[y*12+x]==1 && !out[(y+1)*12+x+1]) out[(y+1)*12+x+1]=2;
}
static unsigned pixel(const unsigned char *p,unsigned stride,unsigned x,unsigned y) {
    return (p[y*stride+x/2] >> ((x&1)*4)) & 15;
}
int pe_lang_cyrillic_fill(unsigned char *pixels,unsigned stride,unsigned height) {
    int written=0;
    for (unsigned k=0;k<PE_CYRILLIC_COUNT;++k) {
        const PeCyrillicGlyph *g=&kPeCyrillicGlyphs[k];
        unsigned cx=g->code%21*12,cy=g->code/21*12;
        unsigned char cell[144];int occupied=0;
        if (cy+12>height || cx+12>stride*2) continue;
        for (unsigned y=0;y<12;++y) for (unsigned x=0;x<12;++x) occupied|=pixel(pixels,stride,cx+x,cy+y)!=0;
        if (occupied) continue;
        pe_lang_cyrillic_compose(g,cell);
        for (unsigned y=0;y<12;++y) for (unsigned x=0;x<12;++x) {
            unsigned at=(cy+y)*stride+(cx+x)/2, shift=((cx+x)&1)*4;
            pixels[at]=(unsigned char)((pixels[at]&~(15u<<shift))|((unsigned)cell[y*12+x]<<shift));
        }
        ++written;
    }
    return written;
}
