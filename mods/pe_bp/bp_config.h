#ifndef BP_CONFIG_H
#define BP_CONFIG_H
#include <stddef.h>
enum { BP_FEATURE_RESISTANCE=1, BP_FEATURE_FORTITUDE=2, BP_FEATURE_MASTERY=4, BP_FEATURE_RESPEC=8, BP_FEATURE_ALL=15 };
enum { BP_PRESET_VANILLA_QOL, BP_PRESET_ENHANCED, BP_PRESET_RESISTANCE, BP_PRESET_MASTERY, BP_PRESET_CUSTOM, BP_PRESET_COUNT };
int bp_config_valid(unsigned flags);
unsigned bp_preset_flags(int preset);
int bp_config_preset(unsigned flags);
const char *bp_preset_name(unsigned preset);
void bp_config_changes(unsigned before,unsigned after,char *out,size_t size);
#endif
