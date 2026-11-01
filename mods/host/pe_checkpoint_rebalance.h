/* Typed native Rebalance checkpoint sections. No normal gameplay callbacks. */
#ifndef PE_CHECKPOINT_REBALANCE_H
#define PE_CHECKPOINT_REBALANCE_H
#include "pe_checkpoint_wire.h"
#include <string.h>
#define PE_REBALANCE_STATE_BYTES 3088u
int pe_rb_atb_walk(const unsigned char*,unsigned char*,int);
int pe_rb_effects_walk(const unsigned char*,unsigned char*,int);
int pe_rb_pebalance_walk(const unsigned char*,unsigned char*,int);
int pe_rb_panacea_walk(const unsigned char*,unsigned char*,int);
int pe_rb_combine_walk(const unsigned char*,unsigned char*,int);
int pe_rb_power_walk(const unsigned char*,unsigned char*,int);
static int pe_rb_finish(PeCheckpointWire*w){if(!w->ok||w->at>512)return 0;if(w->in)for(size_t i=w->at;i<512;i++)if(w->in[i])return 0;if(w->out)memset(w->out+w->at,0,512-w->at);return 1;}
const char*pe_rb_effects_blocker(void);
const char*pe_rebalance_checkpoint_blocker(void);
int pe_rebalance_checkpoint_ready(void);
int pe_rebalance_checkpoint_walk(const unsigned char*,unsigned char*,size_t,int);
#endif
