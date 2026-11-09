#ifndef PE_STATE_PORTABLE_H
#define PE_STATE_PORTABLE_H
#include "pe_state.h"
#include <string.h>
#define PE_PORTABLE_HEADER 80u
static uint32_t pe_port_get(const unsigned char *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void pe_port_put(unsigned char *p,uint32_t v){for(unsigned j=0;j<4;j++)p[j]=(unsigned char)(v>>(j*8));}
static uint32_t pe_port_crc(const unsigned char *p,size_t n){uint32_t c=~0u;for(size_t i=0;i<n;i++){c^=(i>=56&&i<60)?0:p[i];for(unsigned j=0;j<8;j++)c=(c>>1)^(0xedb88320u&-(int32_t)(c&1));}return ~c;}
static int pe_port_finish(unsigned char *p,size_t cap,unsigned portable,size_t exact,const PeStateIdentity *id,size_t *used){
    if(!p||!id||!used||portable<8224||portable>16416||exact>PE_STATE_MAX_BYTES||cap<80+portable||exact>cap-80-portable)return PE_STATE_BOUNDS;
    memset(p,0,80);memcpy(p,"PEHYB001",8);pe_port_put(p+8,1);pe_port_put(p+12,(uint32_t)(80+portable+exact));pe_port_put(p+16,portable);pe_port_put(p+20,(uint32_t)exact);memcpy(p+24,id->disc,32);pe_port_put(p+56,pe_port_crc(p,80+portable+exact));*used=80+portable+exact;return 0;
}
static int pe_port_inspect(const unsigned char *p,size_t n,const PeStateIdentity *id,unsigned *portable,unsigned *exact){
    if(!p||!id||n<80||n>PE_STATE_MAX_BYTES||memcmp(p,"PEHYB001",8)||pe_port_get(p+8)!=1||pe_port_get(p+12)!=n)return PE_STATE_INVALID;
    unsigned a=pe_port_get(p+16),b=pe_port_get(p+20);
    if(a<8224||a>16416||a>n-80||b!=n-80-a)return PE_STATE_BOUNDS;
    for(unsigned i=60;i<80;i++)if(p[i])return PE_STATE_INVALID;
    if(pe_port_get(p+56)!=pe_port_crc(p,n))return PE_STATE_CHECKSUM;
    /* Build belongs to the optional exact component; disc/game identity is required. */
    if(memcmp(p+24,id->disc,32))return PE_STATE_INCOMPATIBLE;
    if(memcmp(p+80,"PECP",4))return PE_STATE_INVALID;
    *portable=a;*exact=b;return 0;
}
#endif
