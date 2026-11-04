#include "pe_lang_cjk.h"
#include <string.h>
void pe_cjk_reset(PeCjkCache *c,const PeLangPack *pack,const unsigned char *original) {
    memset(c,0,sizeof *c); c->pack=pack; c->need_sync=1;
    if (!pack || !pack->cjk_count || !original) return;
    /* Menu glyph bytes below FA, including the rename grid's F0..F6, must stay
     * untouched even though those cells are blank in the USA font. */
    for (unsigned cell=250;cell<441;++cell) {
        unsigned x=cell%21*12,y=cell/21*12;
        int empty=1;
        for (unsigned row=0;row<12;++row)
            for (unsigned b=0;b<6;++b) if (original[(y+row)*128+x/2+b]) empty=0;
        if (empty) c->slots[c->count++].cell=(unsigned short)cell;
    }
}
void pe_cjk_frame(PeCjkCache *c) {
    for (unsigned i=0;i<c->count;++i) c->slots[i].pinned=0;
    c->need_sync=1;
}
int pe_cjk_resolve(PeCjkCache *c,unsigned id,unsigned *cell,PeCjkSync sync,PeCjkUpload upload,void *user) {
    if (!c->pack || id>=c->pack->cjk_count) return 0;
    for (unsigned i=0;i<c->count;++i)
        if (c->slots[i].valid && c->slots[i].id==id) {
            c->slots[i].pinned=1; *cell=c->slots[i].cell; return 1;
        }
    for (unsigned i=0;i<c->count;++i) {
        unsigned k=(c->hand+i)%c->count;
        PeCjkSlot *s=&c->slots[k];
        if (s->pinned) continue;
        if (c->need_sync) { sync(user); c->need_sync=0; }
        if (upload(s->cell,c->pack->data+c->pack->cjk_offset+id*76+4,user)) return 0;
        s->id=id; s->valid=1; s->pinned=1; c->hand=(k+1)%c->count; *cell=s->cell; return 1;
    }
    c->overflow=1;
    return 0;
}
