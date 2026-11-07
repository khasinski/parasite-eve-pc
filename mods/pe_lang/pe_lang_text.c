/* Plain game-glyph/UTF-8 bridge for optional mod interfaces; no SDL or game state. */
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "pe_lang_text.h"
#include "pe_lang_text_data.h"
#include "pe1/mod_text.h"
#include "ui/pe_utf8.h"
static unsigned rd32(const unsigned char *p) {
    return p[0]|(unsigned)p[1]<<8|(unsigned)p[2]<<16|(unsigned)p[3]<<24;
}
static unsigned emit(char *out, unsigned cp) {
    if (cp<0x80) { out[0]=(char)cp;return 1; }
    if (cp<0x800) { out[0]=(char)(0xC0|(cp>>6));out[1]=(char)(0x80|(cp&63));return 2; }
    if (cp<0x10000) {out[0]=(char)(0xE0|(cp>>12));out[1]=(char)(0x80|((cp>>6)&63));out[2]=(char)(0x80|(cp&63));return 3;}
    out[0]=(char)(0xF0|(cp>>18));out[1]=(char)(0x80|((cp>>12)&63));out[2]=(char)(0x80|((cp>>6)&63));out[3]=(char)(0x80|(cp&63));return 4;
}
const char *pe_lang_text_utf8(PeLangPack *pack,const char *key) {
    const unsigned char *raw=pe_lang_pack_find(pack,key);
    if (!raw) return NULL;
    unsigned lo=0,hi=pack->count,want=pe_lang_hash(key);
    while (lo<hi) {
        unsigned mid=lo+(hi-lo)/2;
        unsigned hash=rd32(pack->data+pack->entries_at+mid*8);
        if (hash<want) lo=mid+1; else hi=mid;
    }
    unsigned at=lo;
    if (at==pack->count) return NULL;
    if (!pack->utf8) pack->utf8=calloc(pack->count,sizeof *pack->utf8);
    if (!pack->utf8) return NULL;
    if (pack->utf8[at]) return pack->utf8[at];
    char text[4097];unsigned n=0;
    while (*raw!=0xFF) {
        unsigned cp,id;
        if (Pe_ExtendedGlyph(raw,&id) && id<pack->cjk_count) {
            cp=rd32(pack->data+pack->cjk_offset+id*76);raw+=4;
        } else if (*raw<241 && kPeTextUnicode[*raw]) cp=kPeTextUnicode[*raw++];
        else return NULL; /* a label cannot inject native text commands */
        if (n+4>=sizeof text) return NULL;
        n+=emit(text+n,cp);
    }
    text[n]=0;
    pack->utf8[at]=malloc(n+1);
    if (pack->utf8[at]) memcpy(pack->utf8[at],text,n+1);
    return pack->utf8[at];
}
unsigned pe_lang_text_encode(const PeLangPack *pack,unsigned char *out,unsigned size,const char *text) {
    unsigned n=0;
    if (!size) return 0;
    while (*text && n+1<size) {
        unsigned cp=pe_utf8_next(&text),code=241;
        for (unsigned i=0;i<241;++i) if (kPeTextUnicode[i]==cp) {code=i;break;}
        if (code<241) out[n++]=(unsigned char)code;
        else {
            unsigned lo=0,hi=pack?pack->cjk_count:0;
            while (lo<hi) {
                unsigned mid=lo+(hi-lo)/2,scalar=rd32(pack->data+pack->cjk_offset+mid*76);
                if (scalar<cp) lo=mid+1; else hi=mid;
            }
            if (pack && lo<pack->cjk_count && rd32(pack->data+pack->cjk_offset+lo*76)==cp) {
                if (n+4>=size) break;
                out[n++]=0xF1;out[n++]=0xC5;out[n++]=(unsigned char)(lo/240);out[n++]=(unsigned char)(lo%240);
            } else out[n++]=cp<128?0x0F:0x2C; /* match original unsupported ASCII spacing */
        }
    }
    out[n]=0xFF;return n;
}
