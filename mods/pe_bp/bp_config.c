#include "bp_config.h"
#include <stdio.h>
int bp_config_valid(unsigned flags){return (flags&~BP_FEATURE_ALL)==0;}
unsigned bp_preset_flags(int preset){static const unsigned flags[]={BP_FEATURE_RESPEC,BP_FEATURE_ALL,BP_FEATURE_RESISTANCE|BP_FEATURE_FORTITUDE|BP_FEATURE_RESPEC,BP_FEATURE_MASTERY|BP_FEATURE_RESPEC};return preset>=0&&preset<BP_PRESET_CUSTOM?flags[preset]:BP_FEATURE_ALL;}
int bp_config_preset(unsigned flags){for(int i=0;i<BP_PRESET_CUSTOM;i++)if(bp_preset_flags(i)==flags)return i;return BP_PRESET_CUSTOM;}
const char *bp_preset_name(unsigned preset){static const char *const names[]={"Vanilla + QoL","Enhanced","Resistance Only","Mastery Only","Custom"};return preset<BP_PRESET_COUNT?names[preset]:"Custom";}
void bp_config_changes(unsigned before,unsigned after,char *out,size_t n){
 if(before==after){snprintf(out,n,"No changes");return;}
 snprintf(out,n,"Resist %s, Fort %s, Mastery %s, Respec %s",after&1?"On":"Off",after&2?"On":"Off",after&4?"On":"Off",after&8?"On":"Off");
}
