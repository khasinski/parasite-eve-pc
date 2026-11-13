/* BP-owned profile settings. Draft edits never touch save investments or another mod. */
#include "pe_bp_api.h"
#include "pe_bp_settings.h"
#include "pe_mod_host.h"
#include "pe_mod_menu.h"
#include "pe_pages.h"
#include "pe_game.h"
#include <stdio.h>
#include <string.h>

static int active_flags=BP_FEATURE_ALL,guide_index,ready;
/* EX Game carryover: independent of the preset (BP-Q15 default: ranks carry). */
static int carry_mode=BP_CARRY_KEEP;
static const char *const carry_names[BP_CARRY_MODES]={"Off","Keep purchased skills","Convert skills to BP"};
int pe_bp_settings_carry_mode(void){return carry_mode>=0&&carry_mode<BP_CARRY_MODES?carry_mode:BP_CARRY_OFF;}
static void carry_text(char *out,unsigned n){
    static const char *const what[BP_CARRY_MODES]={
        "Ranks reset. Unspent BP carries (the game's own rule).",
        "Ranks carry; refunding them later gives upgrade-only BP.",
        "Ranks reset; their BP value becomes upgrade-only BP."};
    snprintf(out,n,"%s",what[pe_bp_settings_carry_mode()]);
}
static unsigned draft_flags=BP_FEATURE_ALL,custom_flags=BP_FEATURE_ALL;
static int custom_selected;
static const char *const off_on[]={"Off","On"};
static const char *const guide_names[]={"None","Balanced","Gun-focused","PE-focused","Survival-focused"};
static const char *const guide_advice[]={"Advice off. Allocate BP freely.","Compare equipment, Aya's defenses and learned powers.","Compare Attack, Range and Bullets in the Guns tab.","Compare learned Mastery powers and their PE costs.","Compare armor, Aya's resistances and protection effects."};
unsigned pe_bp_settings_flags(void){return (unsigned)active_flags;}
static unsigned preset_count(void){return BP_PRESET_COUNT;}
static int get_preset(void){return custom_selected?BP_PRESET_CUSTOM:bp_config_preset(draft_flags);}
static int set_preset(int v){if(v<0||v>=BP_PRESET_COUNT)return 0;custom_selected=v==BP_PRESET_CUSTOM;draft_flags=custom_selected?custom_flags:bp_preset_flags(v);return 1;}
#define FEATURE(name,flag) \
static int get_##name(void){return (draft_flags&(flag))!=0;} \
static int set_##name(int v){if(v!=0&&v!=1)return 0;draft_flags=v?draft_flags|(flag):draft_flags&~(unsigned)(flag);custom_flags=draft_flags;custom_selected=0;return 1;}
FEATURE(resistance,BP_FEATURE_RESISTANCE)
FEATURE(fortitude,BP_FEATURE_FORTITUDE)
FEATURE(mastery,BP_FEATURE_MASTERY)
FEATURE(respec,BP_FEATURE_RESPEC)
static void current_text(char *out,unsigned n){snprintf(out,n,"%s%s",bp_preset_name((unsigned)bp_config_preset((unsigned)active_flags)),pe_mod_active("pe_bp")?"":" (paused)");}
static void changes_text(char *out,unsigned n){snprintf(out,n,"Resistance %s > %s; Mastery %s > %s",active_flags&1?"On":"Off",draft_flags&1?"On":"Off",active_flags&4?"On":"Off",draft_flags&4?"On":"Off");}
static void extras_text(char *out,unsigned n){snprintf(out,n,"Fortitude %s > %s; Respec %s > %s",active_flags&2?"On":"Off",draft_flags&2?"On":"Off",active_flags&8?"On":"Off",draft_flags&8?"On":"Off");}
static void retained_text(char *out,unsigned n){
    const PeBpApi *api=pe_service_get("bp",1);BpSnapshot s;unsigned retained=0;
    if(api&&api->snapshot(&s)){
        for(int i=0;i<BP_DEFENSE_TRACKS;++i)if(!(draft_flags&(i==BP_FORTITUDE?BP_FEATURE_FORTITUDE:BP_FEATURE_RESISTANCE)))retained+=bp_track_spent(i,s.rec.defense[i]);
        if(!(draft_flags&BP_FEATURE_MASTERY))for(int i=0;i<BP_MASTERY_LINES;++i)retained+=bp_line_spent(i,s.rec.mastery[i]);
    }
    snprintf(out,n,"%u BP retained; no refund",retained);
}
static void scope_text(char *out,unsigned n){snprintf(out,n,"Profile rules; investments stay in each save");}
static void policy_text(char *out,unsigned n){snprintf(out,n,"Off pauses effects; ranks and BP investment stay");}
static void guide_text(char *out,unsigned n){
    const char *text=guide_index>=0&&guide_index<5?guide_advice[guide_index]:guide_advice[0];
    if(guide_index==3&&!(active_flags&BP_FEATURE_MASTERY))text="PE Mastery is off. Enable it to use Mastery advice.";
    snprintf(out,n,"%s",text);
}
static void upgrades_text(char *out,unsigned n){snprintf(out,n,"Aya's menu > BP");}
static void refunds_text(char *out,unsigned n){snprintf(out,n,"Wayne > Workshop > Reset BP");}
static void apply_now(void){
    char question[96],summary[192];unsigned requested=draft_flags;
    if(!ready||!pe_mod_data_available("pe_bp")){pe_toast("BP settings are unavailable; nothing changed");return;}
    if(!pe_free_field()){pe_toast("Close battle, dialogue or the game menu before applying BP rules");return;}
    if(!bp_config_valid(requested)){pe_toast("Invalid BP settings; nothing changed");return;}
    if(requested==(unsigned)active_flags){pe_toast("BP rules already in use");return;}
    snprintf(question,sizeof question,"Apply %s BP rules?",bp_preset_name((unsigned)bp_config_preset(requested)));
    bp_config_changes((unsigned)active_flags,requested,summary,sizeof summary);
    if(pe_menu_confirm(question,summary)!=1){pe_toast("Cancelled. BP rules unchanged");return;}
    if(!pe_free_field()||!pe_mod_data_available("pe_bp")){pe_toast("Context changed. BP rules unchanged");return;}
    active_flags=(int)requested;draft_flags=custom_flags=requested;custom_selected=0;
    pe_menu_save_settings();
    pe_mod_log("pe_bp","rules applied flags=%u; BP, ranks, gear and Economy unchanged",requested);
    pe_toast("BP rules applied. Investments retained; no BP moved");
}
void pe_bp_menu_register(void){
    ready=pe_menu_bind_setting(PE_MENU_PAGE "/" PE_PAGE_BP "/Applied feature flags",&active_flags,0,BP_FEATURE_ALL)==0;
    draft_flags=custom_flags=(unsigned)active_flags;
    pe_menu_begin(PE_MENU_PAGE);
    if(pe_menu_register_info(PE_PAGE_BP,"Current BP preset","Applied profile rules, separate from the pending selection.",current_text)<0 ||
       pe_menu_register_live_dynamic_choice(PE_PAGE_BP,"Choose BP preset","Vanilla + QoL keeps vanilla upgrades and paid respec. Browsing changes nothing.",BP_PRESET_COUNT,preset_count,bp_preset_name,get_preset,set_preset)<0 ||
       pe_menu_register_info(PE_PAGE_BP,"Changes","Resistance and Mastery before and after Apply.",changes_text)<0 ||
       pe_menu_register_info(PE_PAGE_BP,"Other changes","Fortitude and paid respec before and after Apply.",extras_text)<0 ||
       pe_menu_register_info(PE_PAGE_BP,"Inactive investments","Purchased ranks are retained, never refunded by a preset.",retained_text)<0 ||
       pe_menu_register_action(PE_PAGE_BP,"Apply BP preset","Confirm Yes/No to apply only BP-owned rules. Requires free field.",apply_now)<0 ||
       pe_menu_register_choice(PE_PAGE_BP,"EX Game carryover","Applies when an EX Game starts; a New Game always starts fresh. Gear follows the game's own rules.",&carry_mode,carry_names,BP_CARRY_MODES)<0 ||
       pe_menu_register_info(PE_PAGE_BP,"Carryover details","Upgrade-only BP buys Resistance, Fortitude and Mastery, never gear or Aya stats.",carry_text)<0 ||
       pe_menu_register_choice(PE_PAGE_BP,"Playstyle guide","Advice only. Changes no rules or purchases.",&guide_index,guide_names,5)<0 ||
       pe_menu_register_info(PE_PAGE_BP,"Guide details","Suggestions adapt to the enabled BP systems.",guide_text)<0 ||
       pe_menu_register_info(PE_PAGE_BP,"Settings scope","These rules affect the profile; each save retains its own investments.",scope_text)<0 ||
       pe_menu_register_info(PE_PAGE_BP,"Disable behavior","No automatic refunds or deductions when effects are paused.",policy_text)<0 ||
       pe_menu_register_info(PE_PAGE_BP,"Upgrade menu","Vanilla upgrades remain available in every preset.",upgrades_text)<0 ||
       pe_menu_register_info(PE_PAGE_BP,"Refund menu","Paid respec requires Junk economy. Weapon and armor BP is excluded.",refunds_text)<0 ||
       pe_menu_register_live_choice(PE_PAGE_BP_ADVANCED,"Resistance","Draft only. Disabling retains ranks and suspends their effects.",off_on,2,get_resistance,set_resistance)<0 ||
       pe_menu_register_live_choice(PE_PAGE_BP_ADVANCED,"Fortitude","Draft only. Included with Resistance in its named preset.",off_on,2,get_fortitude,set_fortitude)<0 ||
       pe_menu_register_live_choice(PE_PAGE_BP_ADVANCED,"PE Mastery","Draft only. Disabling retains all purchased Mastery ranks.",off_on,2,get_mastery,set_mastery)<0 ||
       pe_menu_register_live_choice(PE_PAGE_BP_ADVANCED,"Paid respec","Draft only. Refunds eligible investments at Wayne; no equipment refunds.",off_on,2,get_respec,set_respec)<0 ||
       pe_menu_register_action(PE_PAGE_BP_ADVANCED,"Apply BP settings","Uses the same preview, safety checks and Yes/No confirmation.",apply_now)<0)
        pe_mod_log("pe_bp","F1 Bonus Points page registration failed: menu registry full");
    pe_menu_begin(NULL);
}
