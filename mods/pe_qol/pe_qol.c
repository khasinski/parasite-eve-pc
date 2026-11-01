/* PE Enhancements: quality of life (movement; more as they are ported), from the psxrecomp
 * build. Rows at the top of F1 > PE Enhancements. */
#include "pe_mod_host.h"
#include "pe_pages.h"
#include "pe_inventory.h"

#define MOD_ID "pe_qol"

void pe_move_register(const char *mod_id);
void pe_encounters_register(const char *mod_id);
void pe_battle_frequency_register(const char *mod_id);

static int qol_init(void) {
    pe_move_register(MOD_ID);
    pe_key_items_register(MOD_ID);
    pe_key_menu_register(MOD_ID);
    pe_bag_sort_register(MOD_ID);
    pe_inventory_menu_register(MOD_ID);
    pe_encounters_register(MOD_ID);
    pe_battle_frequency_register(MOD_ID);
    return 0;
}

const PeMod pe_mod_pe_qol = {MOD_ID, "PE Enhancements: quality of life", PE_ENHANCEMENTS_VERSION, NULL, qol_init, pe_inventory_menu_shutdown};

#if PE_NATIVE_STATE_CPU_COMPONENTS
#include "pe_checkpoint_wire.h"
#include "pe_checkpoint_qol.h"
#include <string.h>
int pe_qol_checkpoint_ready(void){unsigned char values[244];return pe_key_menu_checkpoint_ready()&&pe_sort_checkpoint_walk(0,values,0)&&pe_inventory_checkpoint_walk(0,values,0);}
int pe_qol_checkpoint_walk(const unsigned char *in,unsigned char *out,size_t bytes,int apply){if(bytes!=PE_CHECKPOINT_QOL_BYTES||(!in&&!out)||!pe_qol_checkpoint_ready())return 0;if(apply&&(!in||!pe_qol_checkpoint_walk(in,0,bytes,0)))return 0;size_t at=16;if(out){memset(out,0,bytes);memcpy(out,"PEQOL001",8);}else{if(memcmp(in,"PEQOL001",8))return 0;for(unsigned i=8;i<16;i++)if(in[i])return 0;}
#define QOL(name,n) do{if(!name(in?in+at:0,out?out+at:0,apply))return 0;at+=(n);}while(0)
QOL(pe_move_checkpoint_walk,20);QOL(pe_encounters_checkpoint_walk,4);QOL(pe_frequency_checkpoint_walk,4);QOL(pe_keys_checkpoint_walk,144);QOL(pe_sort_checkpoint_walk,244);QOL(pe_inventory_checkpoint_walk,80);
#undef QOL
if(apply)pe_key_menu_checkpoint_reset();return at==bytes;}
#endif
