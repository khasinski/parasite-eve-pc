#include "pe_state.h"
#include "psyz/state_producers.h"
static int cd_capture(void *c,unsigned char *out,size_t cap,size_t *used){(void)c;if(used)*used=0;size_t n=Psyz_StateCdBytes();if(!used||!out||cap<n||!Psyz_StateCdCapture(out,n))return 0;*used=n;return 1;}
static int cd_validate(void *c,const unsigned char *in,size_t n){(void)c;return Psyz_StateCdValidate(in,n);}
static int cd_restore(void *c,const unsigned char *in,size_t n){(void)c;return Psyz_StateCdRestore(in,n);}
static int checkpoint_kernel_capture(void *c,unsigned char *out,size_t cap,size_t *used){(void)c;if(used)*used=0;size_t n=Psyz_StateKernelBytes();if(!used||!out||cap<n||!Psyz_StateKernelCapture(out,n))return 0;*used=n;return 1;}
static int checkpoint_kernel_validate(void *c,const unsigned char *in,size_t n){(void)c;return Psyz_StateKernelValidate(in,n);}
static int checkpoint_kernel_restore(void *c,const unsigned char *in,size_t n){(void)c;return Psyz_StateKernelRestore(in,n);}
int PeState_RegisterProducerState(PeStateRegistry *r){PeStateModule cd={PE_STATE_DISC,1,100000,cd_capture,0,cd_validate,cd_restore,0},kernel={PE_STATE_SCHEDULER,1,2048,checkpoint_kernel_capture,0,checkpoint_kernel_validate,checkpoint_kernel_restore,0};int result=PeState_Register(r,cd);return result?result:PeState_Register(r,kernel);}
