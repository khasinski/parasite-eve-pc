/* Numeric GTE registers/stack only. Presentation pointer caches invalidate. */
#include "pe_state.h"
#include <psyz/state_numeric.h>
static int capture_gte(void *context,unsigned char *out,size_t capacity,size_t *used) {
    (void)context;
    if(!used)return 0;
    *used=0;
    if(Psyz_GteCaptureNumericState(out,capacity))return 0;
    *used=Psyz_GteNumericStateBytes();return 1;
}
static int validate_gte(void *context,const unsigned char *data,size_t bytes) {
    (void)context;return Psyz_GteValidateNumericState(data,bytes)==0;
}
static int restore_gte(void *context,const unsigned char *data,size_t bytes) {
    (void)context;return Psyz_GteRestoreNumericState(data,bytes)==0;
}
int PeState_RegisterNumericGte(PeStateRegistry *registry) {
    PeStateModule module={PE_STATE_GTE,1,Psyz_GteNumericStateBytes(),capture_gte,0,validate_gte,restore_gte,0};
    return PeState_Register(registry,module);
}
