#include <string.h>
#include "pe_hooks.h"
#include "pe_mod_host.h"
#include "pe_registry.h"
#include "pe_save.h"
#include "pe_save_admission.h"
#include "pe_save_recovery.h"
#include "pe_extension_store.h"
#include "pe_inventory_internal.h"
#include "pe_recovery_snapshot.h"

static uint32_t add(uint32_t c,const unsigned char*p,unsigned n){
    while(n--){c^=*p++;for(int j=0;j<8;++j)c=c&1?(c>>1)^0xedb88320u:c>>1;}return c;
}
static void put(unsigned char*p,unsigned v){for(unsigned i=0;i<4;++i)p[i]=v>>(8*i);}
int pe_recovery_snapshot_capture(const unsigned char*native,unsigned n,PeModRecoverySnapshot*out){
    if(!native||!out||n!=PE_RECOVERY_NATIVE_SIZE||pe_reg_access()!=PE_REG_OK||pe_save_check_write()<0)return -1;
    /* The caller publishes only on success. These buffers are its unpublished room slot. */
    unsigned char ledger[2048],link[20];unsigned companion=0;
    if(pe_inventory_snapshot_ledger(ledger,sizeof ledger,native,n)<0)return -1;
    memset(out,0,sizeof *out);
    if(pe_extension_snapshot(out->companion,sizeof out->companion,&companion,link,"pe_host/content",ledger,sizeof ledger)<0)return -1;
    if(pe_save_try_crash_tail(out->card)<0)return -1;
    if(companion){if(pe_save_recovery_patch(out->card,"pe_host.extlink",1,link,sizeof link)<0)return -1;}
    else{unsigned v,size;unsigned char old[20];if(pe_save_peek(out->card,"pe_host.extlink",&v,old,sizeof old,&size)!=0)return -1;}
    unsigned char header[20];memcpy(header,"PECR",4);put(header+4,1);put(header+8,n);put(header+12,PE_SAVE_TAIL_SIZE);put(header+16,companion);
    uint32_t c=add(~0u,header,sizeof header);c=add(c,native,n);c=add(c,out->card+PE_SAVE_TAIL_OFFSET,PE_SAVE_TAIL_SIZE);c=add(c,out->companion,companion);
    out->version=1;out->companion_size=companion;out->checksum=~c;return 0;
}
