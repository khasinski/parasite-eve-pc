/* Canonical BP-owned F1 page. Purchases remain at Aya's BP icon and Wayne's Workshop. */
#include "pe_bp_api.h"
#include "pe_mod_host.h"
#include "pe_mod_menu.h"
#include "pe_pages.h"
#include <stdio.h>

static int guide_index;
static const char *const guide_names[]={"None","Balanced","Gun-focused","PE-focused","Survival-focused"};
static const char *const guide_advice[]={
    "Advice off. Allocate BP freely.",
    "Compare equipment, Aya's defenses and learned powers.",
    "Compare Attack, Range and Bullets in the Guns tab.",
    "Compare learned Mastery powers and their PE costs.",
    "Compare armor, Aya's resistances and protection effects."
};
static unsigned guide_count(void){return sizeof guide_names/sizeof guide_names[0];}
static const char *guide_name(unsigned i){return i<guide_count()?guide_names[i]:"";}
static void guide_text(char *out,unsigned n){snprintf(out,n,"%s",guide_index>=0&&(unsigned)guide_index<guide_count()?guide_advice[guide_index]:guide_advice[0]);}
static void status_text(char *out,unsigned n){snprintf(out,n,"%s",pe_mod_active("pe_bp")?"Enabled":"Disabled - see Manage mods");}
static void upgrades_text(char *out,unsigned n){snprintf(out,n,"Aya's menu > BP");}
static void refunds_text(char *out,unsigned n){snprintf(out,n,"Wayne > Workshop > Reset BP");}
static void rules_text(char *out,unsigned n){snprintf(out,n,"Resistance + PE Mastery");}
static void presets_text(char *out,unsigned n){snprintf(out,n,"Not available yet");}
void pe_bp_menu_register(void){
    pe_menu_begin(PE_MENU_PAGE);
    if(pe_menu_register_info(PE_PAGE_BP,"BP overhaul","Enable or disable the module in Manage mods. Restart when requested.",status_text)<0 ||
       pe_menu_register_info(PE_PAGE_BP,"Upgrade menu","Spend BP through Aya's existing BP icon. F1 does not allocate BP.",upgrades_text)<0 ||
       pe_menu_register_info(PE_PAGE_BP,"Refund menu","100% of eligible BP returned for Junk. Weapons and armor are excluded.",refunds_text)<0 ||
       pe_menu_register_info(PE_PAGE_BP,"Current rules","Both new upgrade systems are enabled while the BP module is active.",rules_text)<0 ||
       pe_menu_register_dynamic_choice(PE_PAGE_BP,"Playstyle guide","Advice only. Changes no rules, investments or purchases.",&guide_index,5,guide_count,guide_name)<0 ||
       pe_menu_register_info(PE_PAGE_BP,"Guide details","Suggestions for the selected playstyle; no automatic allocation.",guide_text)<0 ||
       pe_menu_register_info(PE_PAGE_BP,"Rules presets","Independent BP rules presets are not implemented. Preview layouts are in Debug > Bonus Points.",presets_text)<0)
        pe_mod_log("pe_bp","F1 Bonus Points page registration failed: menu registry full");
    pe_menu_begin(NULL);
}
