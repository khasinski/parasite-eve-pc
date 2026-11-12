#ifndef PE_BP_LIVE_UI_H
#define PE_BP_LIVE_UI_H
/* The live BP screens: installs the pe_bp-backed catalog source into the native UI. */
int pe_bp_live_available(void);   /* the pe_bp mod is running and publishes `bp` */
void pe_bp_live_install(void);
#endif
