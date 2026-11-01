/* PE Enhancements: economy, ported from the psxrecomp build. F1 > PE Enhancements > Economy.
 * One file per feature; each registers its own rows, hooks and services. */
#include "pe_mod_host.h"
#include "pe_pages.h"
#include "pe_requisition.h"

#define MOD_ID "pe_economy"

void pe_loot_register(const char *mod_id);    /* pe_loot.c: Junk, battle rewards, rare drops */
void pe_catalog_register(const char *mod_id); /* pe_loot_catalog.c: Mod Catalog, Dismantle, Rebuild */
void pe_catalog_menu_register(const char *mod_id); /* pe_catalog_menu.c: the Catalog in Aya's menu */
void pe_discard_dismantle_register(const char *mod_id); /* pe_discard_dismantle.c: Discard on gear dismantles */

void pe_wayne_refund_register(const char *mod_id); /* failed vanilla crafts */

void pe_armor_effect_display_register(const char *mod_id);
void pe_junk_counter_register(const char *mod_id);
void pe_smoke_register(const char *mod_id);
void pe_wayne_menu_register(const char *mod_id); /* pe_wayne_menu.c: the Workshop in Wayne's menu */
void pe_field_wayne_register(const char *mod_id); /* pe_field_wayne.c: Warner's services */
void pe_smoke_shutdown(void);

static int economy_init(void) {
    pe_loot_register(MOD_ID);
    pe_armor_effect_display_register(MOD_ID);
    pe_junk_counter_register(MOD_ID);
    pe_catalog_register(MOD_ID);
    pe_requisition_register(MOD_ID);
    pe_requisition_menu_register(MOD_ID);
    pe_catalog_menu_register(MOD_ID);
    pe_discard_dismantle_register(MOD_ID);
    pe_wayne_refund_register(MOD_ID);
    pe_smoke_register(MOD_ID);
    pe_wayne_menu_register(MOD_ID);
    pe_field_wayne_register(MOD_ID);
    return 0;
}

const PeMod pe_mod_pe_economy = {MOD_ID, "PE Enhancements: economy", PE_ENHANCEMENTS_VERSION, NULL, economy_init, pe_smoke_shutdown};

#if PE_NATIVE_STATE_CPU_COMPONENTS
#include "pe_checkpoint_economy.h"
#include "pe_game.h"
#include <string.h>
int pe_economy_checkpoint_ready(void){if(!pe_game_loaded())return 0;unsigned char b[1024];if(!pe_loot_checkpoint_walk(0,b,0))return 0;if(!pe_catalog_checkpoint_walk(0,b,0))return 0;if(!pe_requisition_checkpoint_walk(0,b,0))return 0;if(!pe_refund_checkpoint_walk(0,b,0))return 0;if(!pe_junk_counter_checkpoint_walk(0,b,0))return 0;if(!pe_catalog_pages_checkpoint_walk(0,b,0))return 0;if(!pe_dismantle_checkpoint_walk(0,b,0))return 0;if(!pe_requisition_menu_checkpoint_walk(0,b,0))return 0;if(!pe_catalog_menu_checkpoint_walk(0,b,0))return 0;if(!pe_wayne_menu_checkpoint_walk(0,b,0))return 0;if(!pe_field_wayne_checkpoint_walk(0,b,0))return 0;if(!pe_smoke_checkpoint_walk(0,b,0))return 0;if(!pe_discard_checkpoint_walk(0,b,0))return 0;return 1;}
int pe_economy_checkpoint_walk(const unsigned char *in,unsigned char*out,size_t bytes,int apply){if(bytes!=PE_ECONOMY_STATE_BYTES||(!in&&!out)||!pe_economy_checkpoint_ready())return 0;if(apply&&(!in||!pe_economy_checkpoint_walk(in,0,bytes,0)))return 0;if(out){memset(out,0,bytes);memcpy(out,"PEECO001",8);}else{if(memcmp(in,"PEECO001",8))return 0;for(unsigned i=8;i<16;i++)if(in[i])return 0;}size_t at=16;
if(!pe_loot_checkpoint_walk(in?in+at:0,out?out+at:0,apply))return 0;at+=260u;
if(!pe_catalog_checkpoint_walk(in?in+at:0,out?out+at:0,apply))return 0;at+=760u;
if(!pe_requisition_checkpoint_walk(in?in+at:0,out?out+at:0,apply))return 0;at+=40u;
if(!pe_refund_checkpoint_walk(in?in+at:0,out?out+at:0,apply))return 0;at+=20u;
if(!pe_junk_counter_checkpoint_walk(in?in+at:0,out?out+at:0,apply))return 0;at+=4u;
if(!pe_catalog_pages_checkpoint_walk(in?in+at:0,out?out+at:0,apply))return 0;at+=24u;
if(!pe_dismantle_checkpoint_walk(in?in+at:0,out?out+at:0,apply))return 0;at+=216u;
if(!pe_requisition_menu_checkpoint_walk(in?in+at:0,out?out+at:0,apply))return 0;at+=224u;
if(!pe_catalog_menu_checkpoint_walk(in?in+at:0,out?out+at:0,apply))return 0;at+=0u;
if(!pe_wayne_menu_checkpoint_walk(in?in+at:0,out?out+at:0,apply))return 0;at+=0u;
if(!pe_field_wayne_checkpoint_walk(in?in+at:0,out?out+at:0,apply))return 0;at+=4u;
if(!pe_smoke_checkpoint_walk(in?in+at:0,out?out+at:0,apply))return 0;at+=24u;
if(!pe_discard_checkpoint_walk(in?in+at:0,out?out+at:0,apply))return 0;at+=4u;
if(at>bytes)return 0;if(in)for(;at<bytes;at++)if(in[at])return 0;return 1;}
#endif
