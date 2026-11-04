/* Exact BIOS RNG value only: does not advance the generator. Not a full-game RNG snapshot. */
#include "pe_state.h"
#include <psyz/system.h>
static int capture_bios_rng(void *context,unsigned char *out,size_t capacity,size_t *used) {
    (void)context;
    if(!out||!used||capacity<4)return 0;
    unsigned int value=Psyz_RandState();
    for(unsigned i=0;i<4;i++)out[i]=(unsigned char)(value>>(8*i));
    *used=4;
    return 1;
}
static int validate_bios_rng(void *context,const unsigned char *data,size_t bytes) {
    (void)context;return data&&bytes==4;
}
static int restore_bios_rng(void *context,const unsigned char *data,size_t bytes) {
    if(!validate_bios_rng(context,data,bytes))return 0;
    unsigned int value=0;for(unsigned i=0;i<4;i++)value|=(unsigned int)data[i]<<(8*i);
    Psyz_Srand(value);return 1;
}
int PeState_RegisterBiosRng(PeStateRegistry *registry) {
    PeStateModule module={PE_STATE_RNG,1,4,capture_bios_rng,0,validate_bios_rng,restore_bios_rng,0};
    return PeState_Register(registry,module);
}
