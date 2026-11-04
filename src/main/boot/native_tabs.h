/* Native settings rows. Original game menus and bindings remain separate. */
#ifndef PE_NATIVE_TABS_H
#define PE_NATIVE_TABS_H
#include "native_registry.h"
static int Host_ModernRowCount(int page){if(page==HOST_PAGE_MODS||page==HOST_PAGE_CHEATS)return Host_RegistryCount(page);if(page==HOST_PAGE_STATES)return 8;if(page==HOST_PAGE_IMAGES)return 9;return 0;}

static const char *Host_ModernRowLabel(int page,int row){
 if(page==HOST_PAGE_MODS||page==HOST_PAGE_CHEATS)return Host_RegistryLabel(page,row);
 static const char *const states[]={"Last operation","Requirement preview","Save checkpoint","Checkpoint file","Load checkpoint","Refresh files","Use Original effects","Checkpoint scope"};
 static const char *const system[]={"Menu appearance","Menu text","Focus pointer","Disc 1","Disc 2","File setup","Save data","Error reports","Build"};
 if(page==HOST_PAGE_STATES&&row>=0&&row<8)return states[row];if(page==HOST_PAGE_IMAGES&&row>=0&&row<9)return system[row];return "";
}

static int Host_ModernRowKind(int page,int row){if(page==HOST_PAGE_MODS||page==HOST_PAGE_CHEATS)return Host_RegistryKind(page,row);if(page==HOST_PAGE_STATES)return row==3?2:row==2||row==4||row==5||row==6?1:0;if(page==HOST_PAGE_IMAGES)return row<3?2:row<5&&!g_CdImageVolumes[row-3]?1:0;return 0;}

static void Host_ModernRowValue(int page,int row,char *out,unsigned size){
 if(page==HOST_PAGE_MODS||page==HOST_PAGE_CHEATS){Host_RegistryValue(page,row,out,size);return;}const char *value="";
if(page==HOST_PAGE_STATES){
#if PE_NATIVE_STATE_CPU_COMPONENTS
        if(row==0){if(s_state_waiting)value="Queued; resume";else if(strlen(s_state_status)>35)value="See operation details below";else value=s_state_status;}
        else if(row==1){unsigned bits=CPE_StateHostReasonBits()&~(4u|128u);value=(bits&32768u)?"Both disc images needed":(bits&16384u)?"Character pack index pending":(bits&64u)?"Particle mode pending":(bits&4096u)?"Mod setting or job pending":bits?"Host condition pending":"Final check after resume";}
        else if(row==2)value=s_state_terminal?"Restart required":"Save a new file";
        else if(row==3){StateFileLabel(out,size);return;}
        else if(row==4)value=s_state_terminal?"Restart required":"Load selected file";
        else if(row==5)value="Scan checkpoint folder";
        else if(row==6)value=FieldFx_GetParticleAugment()?"Select original effects":"Already Original";
        else {
#if PE_NATIVE_STATE_CHANGED_COHORTS
            PeStateMetadataStatus m={0};PeCpuStateSession_MetadataStatus(&s_state_session,&m);snprintf(out,size,"Layouts %u/%u; same launch",m.retained,m.capacity);return;
#else
            value="Same run / room / actors";
#endif
        }
#else
        value="Not built";
#endif
    }else if(page==HOST_PAGE_IMAGES){
        if(row==3||row==4)value=g_CdImageVolumes[row-3]?"Validated and mounted":"Choose from USB";
        else if(row==5)value="First-run chooser built in";
        else if(row==6)value="Normal memory cards";
        else if(row==7)value="Separate launch logs";
        else if(row==8)value="Native development build";
    }
    snprintf(out,size,"%s",value);
}
static const char *Host_ModernRowHelp(int page,int row){
 if(page==HOST_PAGE_MODS||page==HOST_PAGE_CHEATS)return Host_RegistryHelp(page,row);
    if(page==HOST_PAGE_STATES){
#if PE_NATIVE_STATE_CPU_COMPONENTS
        if(row==0)return s_state_status;
        if(row==1){unsigned bits=CPE_StateHostReasonBits()&~(4u|128u);return bits?CPE_StateHostReasonText(bits):"Supported default gameplay mods may stay On. Close custom mod screens and finish any named pending job. Save/Load closes this menu before the final gameplay check; no options are disabled automatically.";}
        if(row==2)return "Save a unique experimental checkpoint after closing this menu. Queued is not completed; the actual outcome is reported afterward.";
        if(row==3)return StateSelectedHelp();
        if(row==4){
#if PE_NATIVE_STATE_CHANGED_COHORTS
            return "Experimental load within this launch and room. Supported actor changes can be rebuilt; game resources and settings must still match. Eight captured actor layouts are retained, so older files may become unavailable. Normal saves remain separate.";
#else
            return "Load only within this same run, room, actor/resource cohort and unchanged settings. Compatibility is checked before loading; recovery is attempted if a load fails. Normal card saves are separate.";
#endif
        }
        if(row==5)return "Newest files first; Refresh keeps the selected filename. Shows up to64 files from4096 entries. A file on disk is not proof it can load in this game run.";
        if(row==6)return "Optional: select Original particle geometry. Supported native effects may stay enabled when their actual CPU-boundary owner proves idle; no automatic reset occurs.";
#if PE_NATIVE_STATE_CHANGED_COHORTS
        return StateScopeHelp();
#else
        return "Same game run, room, actors and settings. Collection unlocks remain global; your worn outfit must match. Custom skins and retained preview resources may block loading. Normal memory-card saves stay separate.";
#endif
#else
        return "This build does not contain the guarded checkpoint module.";
#endif
    }
    if(page==HOST_PAGE_IMAGES){if(row==3||row==4)return g_CdImageVolumes[row-3]?"This USA image is validated and mounted. Existing mounted images are not replaced while playing.":"Choose your own USA BIN image with the controller USB browser. Only a missing disc is added; existing files and saves are preserved.";if(row==5)return "A missing configuration opens the controller file chooser at startup. Existing configuration is preserved; supplied discs and optional native HD files can remain on USB.";if(row==6)return "Normal saves use memory-cards/bu00 and bu10 below the preferences folder. Experimental checkpoints never overwrite them.";if(row==7)return "Each launch has its own build-stamped log and toggle/error records. Restart does not overwrite an earlier report.";if(row==8)return "Native test build. Compilation and source reviews do not certify every scene; human playtesting remains authoritative.";}
    return "";
}
static int Host_ModernAction(int page,int row,int direction,int activate){
 if(page==HOST_PAGE_MODS||page==HOST_PAGE_CHEATS)return Host_RegistryAction(page,row,direction,activate);
if(page==HOST_PAGE_STATES){
#if PE_NATIVE_STATE_CPU_COMPONENTS
        if(row==3&&s_state_count){StateSelectStep(direction);return 0;}
        if(!activate)return 0;
        if(row==2){StateCapture();return s_state_request!=0;}
        if(row==4){StateLoad();return s_state_request!=0;}
        if(row==5){StateRefresh();return 0;}
        if(row==6){StateUseOriginalEffects();return 0;}
#endif
    }else if(page==HOST_PAGE_IMAGES&&(row==3||row==4)&&activate&&!g_CdImageVolumes[row-3]){Host_NativePickMissingDisc((unsigned)(row-2));return 0;}
    return 0;
}
#endif
