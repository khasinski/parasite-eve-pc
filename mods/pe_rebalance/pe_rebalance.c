/* PE Enhancements: rebalance (mod effects, Parasite Energy, weapon power), ported from the
 * psxrecomp build. Rows under F1 > PE Enhancements. */
#include "pe_mod_host.h"
#include "pe_pages.h"
#include "pe_atb_items.h"
#include "pe_panacea.h"

#define MOD_ID "pe_rebalance"

void pe_mod_effects_register(const char *mod_id);
void pe_pe_balance_register(const char *mod_id);
void pe_weapon_power_register(const char *mod_id);
void pe_mod_effects_shutdown(void);

static int rebalance_init(void) {
    pe_atb_items_register(MOD_ID);
    pe_panacea_register(MOD_ID);
    pe_combine_register(MOD_ID);
    pe_weapon_power_register(MOD_ID);
    pe_mod_effects_register(MOD_ID);
    pe_pe_balance_register(MOD_ID);
    return 0;
}

static void rebalance_shutdown(void) {
    pe_atb_items_shutdown();
    pe_panacea_shutdown();
    pe_mod_effects_shutdown();
}

const PeMod pe_mod_pe_rebalance = {MOD_ID, "PE Enhancements: rebalance", PE_ENHANCEMENTS_VERSION, NULL, rebalance_init, rebalance_shutdown};

#if PE_NATIVE_STATE_CPU_COMPONENTS
#include "pe_checkpoint_rebalance.h"
#include "pe_game.h"
const char*pe_rebalance_checkpoint_blocker(void){unsigned char b[512];if(!pe_rb_effects_walk(0,b,0))return pe_rb_effects_blocker();if(!pe_rb_combine_walk(0,b,0))return "Finish the live Combine item-action list first";return "Rebalance settings or typed battle bookkeeping are invalid";}
int pe_rebalance_checkpoint_ready(void){if(!pe_game_loaded())return 0;unsigned char b[512];if(!pe_rb_atb_walk(0,b,0))return 0;if(!pe_rb_effects_walk(0,b,0))return 0;if(!pe_rb_pebalance_walk(0,b,0))return 0;if(!pe_rb_panacea_walk(0,b,0))return 0;if(!pe_rb_combine_walk(0,b,0))return 0;if(!pe_rb_power_walk(0,b,0))return 0;return 1;}
int pe_rebalance_checkpoint_walk(const unsigned char*in,unsigned char*out,size_t bytes,int apply){if(bytes!=PE_REBALANCE_STATE_BYTES||(!in&&!out)||!pe_rebalance_checkpoint_ready())return 0;if(apply&&(!in||!pe_rebalance_checkpoint_walk(in,0,bytes,0)))return 0;if(out){memset(out,0,bytes);memcpy(out,"PERBL001",8);}else{if(memcmp(in,"PERBL001",8))return 0;for(unsigned i=8;i<16;i++)if(in[i])return 0;}size_t at=16;if(!pe_rb_atb_walk(in?in+at:0,out?out+at:0,apply))return 0;at+=512;if(!pe_rb_effects_walk(in?in+at:0,out?out+at:0,apply))return 0;at+=512;if(!pe_rb_pebalance_walk(in?in+at:0,out?out+at:0,apply))return 0;at+=512;if(!pe_rb_panacea_walk(in?in+at:0,out?out+at:0,apply))return 0;at+=512;if(!pe_rb_combine_walk(in?in+at:0,out?out+at:0,apply))return 0;at+=512;if(!pe_rb_power_walk(in?in+at:0,out?out+at:0,apply))return 0;at+=512;return at==bytes;}
#endif
