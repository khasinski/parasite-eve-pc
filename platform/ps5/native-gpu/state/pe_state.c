#include "pe_state.h"
static PeStateFailure failure;
void PeState_ClearFailure(void){failure=(PeStateFailure){0};}
PeStateFailure PeState_LastFailure(void){return failure;}
static void failed(uint32_t id,uint32_t stage){if(stage>=9&&stage<=11){if(!failure.rollbackStage){failure.rollbackModule=id;failure.rollbackStage=stage;}}else if(!failure.stage){failure.module=id;failure.stage=stage;}}
const char*PeState_FailureStageText(uint32_t stage){static const char*const labels[]={"none","capture","capture-constructor-preflight","target-validate","rollback-capture","target-constructor-preflight","target-construct","post-construction-validate","target-restore","rollback-construct","rollback-validate","rollback-restore","barrier-begin","barrier-changed","buffer-bounds","rollback-constructor-preflight"};return stage<sizeof labels/sizeof*labels?labels[stage]:"unknown";}
const char*PeState_FailureOwnerText(uint32_t id){static const char*const names[]={"none","original-save","rng","scene","scripts","actor-render-graph","battle","scheduler","audio","gpu-and-texture-owners","events","disc","mods","host","gte","asset-identity","akao","persistent-inventory","game-globals","pm-and-mod-private-state","geometry"};return id<sizeof names/sizeof*names?names[id]:"unknown";}
#include <string.h>
static uint32_t rd(const unsigned char *p){
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static void wr(unsigned char *p,uint32_t x){
    for(unsigned i=0;
    i<4;
    i++)p[i]=(unsigned char)(x>>(i*8));
}
static uint32_t crc(const unsigned char *p,size_t n){
    uint32_t v=~0u;
    for(size_t i=0;
    i<n;
    i++){
        v^=p[i];
        for(unsigned j=0;
        j<8;
        j++)v=(v>>1)^(0xedb88320u&-(int32_t)(v&1));
    }
    return ~v;
}
static int identity_valid(const PeStateIdentity *identity) {
    unsigned build=0,disc=0;
    if(!identity)return 0;
    for(unsigned i=0;i<32;i++){build|=identity->build[i];disc|=identity->disc[i];}
    return build!=0&&disc!=0;
}
static int idvalid(uint32_t id){
    return id>=1&&id<=20;
}
int PeState_Register(PeStateRegistry *r,PeStateModule m){
    if(!r||r->count>=16||!idvalid(m.id)||!m.schema||!m.capture||!m.maximumBytes||m.maximumBytes>PE_STATE_MAX_BYTES)return PE_STATE_INVALID;
    for(unsigned i=0;
    i<r->count;
    i++)if(r->modules[i].id==m.id)return PE_STATE_INVALID;
    r->modules[r->count++]=m;
    return 0;
}
uint32_t PeState_MissingRuntime(const PeStateRegistry *r){
    uint32_t mask=0;
    if(r&&r->count<=16)for(unsigned i=0;
    i<r->count;
    i++)if(idvalid(r->modules[i].id))mask|=1u<<(r->modules[i].id-1);
    return PE_STATE_ALL_RUNTIME&~mask;
}
int PeState_Encode(unsigned char *out,size_t cap,size_t *used,uint32_t kind,uint64_t epoch,const PeStateIdentity *identity,const PeStateChunk *chunks,unsigned count){
    size_t total=100;
    uint32_t mask=0;
    if(used)*used=0;
    if(!out||!used||!identity_valid(identity)||!chunks||!count||count>16||(kind!=PE_STATE_CHECKPOINT&&kind!=PE_STATE_PARTIAL))return PE_STATE_INVALID;
    for(unsigned i=0;
    i<count;
    i++){
        const PeStateChunk *x=&chunks[i];
        if(!idvalid(x->id)||!x->schema||!x->data||!x->bytes||(mask&(1u<<(x->id-1))))return PE_STATE_INVALID;
        mask|=1u<<(x->id-1);
        if(x->bytes>PE_STATE_MAX_BYTES-16||total>PE_STATE_MAX_BYTES-16-x->bytes)return PE_STATE_BOUNDS;
        total+=16+x->bytes;
    }
    if(kind==PE_STATE_CHECKPOINT&&(count!=1||chunks[0].id!=1))return PE_STATE_INVALID;
    if(total>cap)return PE_STATE_BOUNDS;
    /* Alias only at exact coordinator chunk locations;
    reject destructive overlap. */size_t checkOff=100;
    uintptr_t start=(uintptr_t)out;
    if(start>UINTPTR_MAX-total)return PE_STATE_BOUNDS;
    for(unsigned i=0;
    i<count;
    i++){
        uintptr_t data=(uintptr_t)chunks[i].data;
        if(data>UINTPTR_MAX-chunks[i].bytes)return PE_STATE_BOUNDS;
        if(data<start+total&&data+chunks[i].bytes>start&&data!=start+checkOff+16)return PE_STATE_INVALID;
        checkOff+=16+chunks[i].bytes;
    }
    memset(out,0,100);
    memcpy(out,"PEQST001",8);
    wr(out+8,1);
    wr(out+12,kind);
    wr(out+16,(uint32_t)total);
    wr(out+20,count);
    wr(out+24,(uint32_t)epoch);
    wr(out+28,(uint32_t)(epoch>>32));
    memcpy(out+32,identity->build,32);
    memcpy(out+64,identity->disc,32);
    wr(out+96,crc(out,96));
    size_t off=100;
    for(unsigned i=0;
    i<count;
    i++){
        const PeStateChunk *x=&chunks[i];
        wr(out+off,x->id);
        wr(out+off+4,x->schema);
        wr(out+off+8,(uint32_t)x->bytes);
        wr(out+off+12,crc(x->data,x->bytes));
        memmove(out+off+16,x->data,x->bytes);
        off+=16+x->bytes;
    }
    *used=total;
    return 0;
}
int PeState_Inspect(const unsigned char *in,size_t bytes,const PeStateIdentity *identity,PeStateInfo *info){
    size_t off=100;
    uint32_t mask=0;
    if(!in||!identity_valid(identity)||!info)return PE_STATE_INVALID;
    memset(info,0,sizeof *info);
    if(bytes<100||bytes>PE_STATE_MAX_BYTES||memcmp(in,"PEQST001",8)||rd(in+8)!=1||rd(in+16)!=bytes)return PE_STATE_INVALID;
    if(rd(in+96)!=crc(in,96))return PE_STATE_CHECKSUM;
    if(memcmp(in+32,identity->build,32)||memcmp(in+64,identity->disc,32))return PE_STATE_INCOMPATIBLE;
    uint32_t kind=rd(in+12),count=rd(in+20);
    if((kind!=1&&kind!=2)||!count||count>16)return PE_STATE_INVALID;
    for(unsigned i=0;
    i<count;
    i++){
        if(off>bytes||bytes-off<16)return PE_STATE_BOUNDS;
        uint32_t id=rd(in+off),schema=rd(in+off+4),n=rd(in+off+8);
        if(!idvalid(id)||!schema||!n||(mask&(1u<<(id-1))))return PE_STATE_INVALID;
        if(n>bytes-off-16)return PE_STATE_BOUNDS;
        if(rd(in+off+12)!=crc(in+off+16,n))return PE_STATE_CHECKSUM;
        mask|=1u<<(id-1);
        off+=16+n;
    }
    if(off!=bytes||(kind==1&&(count!=1||mask!=1)))return PE_STATE_INVALID;
    if(kind==1){
        PeStateChunk checkpoint;
        int valid=PeState_OriginalCheckpoint(in+116,bytes-116,&checkpoint);
        if(valid)return valid;
    }
    info->kind=kind;
    info->count=count;
    info->presentMask=mask;
    info->missingRuntimeMask=PE_STATE_ALL_RUNTIME&~mask;
    info->epoch=(uint64_t)rd(in+24)|(uint64_t)rd(in+28)<<32;
    info->arbitraryFrameLoadAvailable=0;
    return 0;
}
int PeState_CapturePartial(const PeStateRegistry *r,const PeStateBarrier *barrier,const PeStateIdentity *identity,unsigned char *out,size_t cap,size_t *used){
    PeState_ClearFailure();
    uint64_t epoch;
    size_t off=100;
    PeStateChunk chunks[16];
    int result=PE_STATE_OK;
    if(used)*used=0;
    if(!r||!r->count||r->count>16||!barrier||!barrier->begin||!barrier->unchanged||!barrier->end||!identity||!out||!used||cap>PE_STATE_MAX_BYTES)return PE_STATE_INVALID;
    for(unsigned i=0;
    i<r->count;
    i++){
        const PeStateModule *m=&r->modules[i];
        if(!idvalid(m->id)||!m->schema||!m->capture||!m->maximumBytes||m->maximumBytes>PE_STATE_MAX_BYTES)return PE_STATE_INVALID;
        for(unsigned j=0;
        j<i;
        j++)if(r->modules[j].id==m->id)return PE_STATE_INVALID;
    }
    if(!barrier->begin(barrier->context,&epoch)){failed(0,12);return PE_STATE_BUSY;}
    for(unsigned i=0;
    i<r->count;
    i++){
        const PeStateModule *m=&r->modules[i];
        size_t n=0;
        if(off>cap||cap-off<16||m->maximumBytes>cap-off-16){
            failed(m->id,14);result=PE_STATE_BOUNDS;
            break;
        }
        if(!m->capture(m->context,out+off+16,m->maximumBytes,&n)||!n||n>m->maximumBytes){
            failed(m->id,1);result=PE_STATE_INCOMPLETE;
            break;
        }
        if(m->prepare&&m->prepare(m->context,out+off+16,n,-1)!=1){failed(m->id,2);result=PE_STATE_INCOMPATIBLE;break;}
        chunks[i]=(PeStateChunk){
            m->id,m->schema,out+off+16,n}
            ;
            off+=16+n;
        }
        if(!result&&!barrier->unchanged(barrier->context,epoch)){failed(0,13);result=PE_STATE_BUSY;}
        if(!result)result=PeState_Encode(out,cap,used,PE_STATE_PARTIAL,epoch,identity,chunks,r->count);
        barrier->end(barrier->context);
        if(result)*used=0;
        return result;
    }
    int PeState_OriginalCheckpoint(const unsigned char *b,size_t bytes,PeStateChunk *chunk){
        uint32_t v=65535;
        if(!b||!chunk||bytes!=8192||b[0]!='S'||b[1]!='C'||b[2]!=0x11||b[3]!=1)return PE_STATE_INVALID;
        for(size_t i=0;
        i<8192;
        i++){
            unsigned x=(i>=0x1c8c&&i<0x1c90)?0:b[i];
            v^=x<<8;
            for(unsigned j=0;
            j<8;
            j++)v=((v&32768)?(v<<1)^0x1021:v<<1)&65535;
        }
        if(rd(b+0x1c8c)!=(~v&65535))return PE_STATE_CHECKSUM;
        *chunk=(PeStateChunk){
            1,1,b,8192}
            ;
            return 0;
        }


/* Prepare all component payloads and rollback images before the first write.
 * Only modules whose restore can reverse their own partial failure may register.
 * This remains a partial COMPONENT transaction; no full-frame loader exists. */
int PeState_RestorePairedComponents(const PeStateRegistry *r,const PeStateRegistry *current,const PeStateBarrier *barrier,
    const PeStateIdentity *identity,const unsigned char *input,size_t bytes,
    unsigned char *scratch,size_t scratchBytes) {
    PeState_ClearFailure();
    PeStateInfo info;
    const unsigned char *payload[16];size_t lengths[16];
    unsigned char *rollback[16];size_t rollbackBytes[16];
    size_t offset=100,used=0;uint64_t epoch;int result;
    if(!r||!current||current->count!=r->count||!r->count||r->count>16||!barrier||!barrier->begin||!barrier->unchanged||!barrier->end||!scratch||scratchBytes>PE_STATE_MAX_BYTES)return PE_STATE_INVALID;
    result=PeState_Inspect(input,bytes,identity,&info);if(result)return result;
    if(info.kind!=PE_STATE_PARTIAL||info.count!=r->count)return PE_STATE_INCOMPLETE;
    uintptr_t a=(uintptr_t)input,b=(uintptr_t)scratch;
    if(a>UINTPTR_MAX-bytes||b>UINTPTR_MAX-scratchBytes)return PE_STATE_BOUNDS;
    if(a<b+scratchBytes&&b<a+bytes)return PE_STATE_INVALID;
    for(unsigned i=0;i<r->count;i++) {
        const PeStateModule *module=&r->modules[i];int found=0;
        const PeStateModule *previous=&current->modules[i];
        if(previous->id!=module->id||previous->schema!=module->schema||previous->maximumBytes!=module->maximumBytes||previous->capture!=module->capture||previous->restore!=module->restore||previous->prepare!=module->prepare||previous->validate!=module->validate||!previous->validate)return PE_STATE_INCOMPATIBLE;
        if(!idvalid(module->id)||!module->schema||!module->capture||!module->validate||!module->restore||!module->maximumBytes||module->maximumBytes>PE_STATE_MAX_BYTES)return PE_STATE_INCOMPLETE;
        for(unsigned j=0;j<i;j++)if(r->modules[j].id==module->id)return PE_STATE_INVALID;
        offset=100;
        for(unsigned j=0;j<info.count;j++) {
            uint32_t id=rd(input+offset),schema=rd(input+offset+4),length=rd(input+offset+8);
            if(id==module->id) {
                if(schema!=module->schema||length>module->maximumBytes)return PE_STATE_INCOMPATIBLE;
                payload[i]=input+offset+16;lengths[i]=length;found=1;break;
            }
            offset+=16+length;
        }
        if(!found)return PE_STATE_INCOMPLETE;
    }
    if(!barrier->begin(barrier->context,&epoch)){failed(0,12);return PE_STATE_BUSY;}
    result=PE_STATE_OK;
    for(unsigned i=0;i<r->count;i++) {
        const PeStateModule *module=&r->modules[i];
        if(!module->validate(module->context,payload[i],lengths[i])){failed(module->id,3);result=PE_STATE_INVALID;break;}
        if(used>scratchBytes||module->maximumBytes>scratchBytes-used){failed(module->id,14);result=PE_STATE_BOUNDS;break;}
        rollback[i]=scratch+used;rollbackBytes[i]=0;
        const PeStateModule *previous=&current->modules[i];
        if(!previous->capture(previous->context,rollback[i],previous->maximumBytes,&rollbackBytes[i])||!rollbackBytes[i]||rollbackBytes[i]>previous->maximumBytes||!previous->validate(previous->context,rollback[i],rollbackBytes[i])){failed(module->id,4);result=PE_STATE_INCOMPLETE;break;}
        used+=rollbackBytes[i];
    }
    if(!result&&!barrier->unchanged(barrier->context,epoch)){failed(0,13);result=PE_STATE_BUSY;}
    int graphTouched=0;
    if(!result&&current!=r)for(unsigned i=0;i<current->count;i++){const PeStateModule *module=&current->modules[i];if(module->prepare&&module->prepare(module->context,rollback[i],rollbackBytes[i],-1)!=1){failed(module->id,15);result=PE_STATE_INCOMPATIBLE;break;}}
    if(!result)for(unsigned i=0;i<r->count;i++){const PeStateModule *module=&r->modules[i];if(module->prepare&&module->prepare(module->context,payload[i],lengths[i],-1)!=1){failed(module->id,5);result=PE_STATE_INCOMPATIBLE;break;}}
    if(!result)for(unsigned i=0;i<r->count;i++){const PeStateModule *module=&r->modules[i];if(module->prepare){int prepared=module->prepare(module->context,payload[i],lengths[i],0);if(prepared!=0)graphTouched=1;if(prepared!=1){failed(module->id,6);result=PE_STATE_INCOMPLETE;break;}}}
    if(!result&&!barrier->unchanged(barrier->context,epoch)){failed(0,13);result=PE_STATE_BUSY;}
    if(!result)for(unsigned i=0;i<r->count;i++){const PeStateModule *module=&r->modules[i];if(!module->validate(module->context,payload[i],lengths[i])){failed(module->id,7);result=PE_STATE_INCOMPATIBLE;break;}}
    if(!result)for(unsigned i=0;i<r->count;i++){const PeStateModule *module=&r->modules[i];graphTouched=1;if(!module->restore(module->context,payload[i],lengths[i])){failed(module->id,8);result=PE_STATE_INCOMPLETE;break;}}
    if(!result&&!barrier->unchanged(barrier->context,epoch)){failed(0,13);result=PE_STATE_BUSY;}
    if(result&&graphTouched){
        /* A graph constructor may touch pools/equipment beyond its own chunk.
         * Reconstruct and restore ALL prior owners, never only a written prefix. */
        int prepared=1;
        for(unsigned i=0;i<current->count;i++){const PeStateModule *module=&current->modules[i];if(module->prepare&&module->prepare(module->context,rollback[i],rollbackBytes[i],1)!=1){failed(module->id,9);prepared=0;break;}}
        if(prepared)for(unsigned i=0;i<current->count;i++){const PeStateModule *module=&current->modules[i];if(!module->validate(module->context,rollback[i],rollbackBytes[i])){failed(module->id,10);prepared=0;break;}}
        if(prepared){for(unsigned i=0;i<current->count;i++){const PeStateModule *module=&current->modules[i];if(!module->restore(module->context,rollback[i],rollbackBytes[i])){failed(module->id,11);result=PE_STATE_ROLLBACK_FAILED;}}}
        else result=PE_STATE_ROLLBACK_FAILED;
    }
    barrier->end(barrier->context);return result;
}

/* Original API preserves identical-context behavior and wire format. */
int PeState_RestoreComponents(const PeStateRegistry*r,const PeStateBarrier*b,const PeStateIdentity*i,const unsigned char*in,size_t n,unsigned char*scratch,size_t cap){return PeState_RestorePairedComponents(r,r,b,i,in,n,scratch,cap);}


