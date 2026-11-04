/* SPDX-License-Identifier: MIT. Host-owned experimental same-room checkpoint UI.
 * File requests run only after the modal releases clock/audio; component work
 * remains queued at the final CPU boundary. No ordinary save file is touched. */
#include "pe_state_session.h"
#include "pe_state_boundary.h"
#include "pe_state_mod_contract.h"
#include "installer/pe_pack_check.h"
#include "state_build_identity.h"
#include "psyz/state_gpu.h"
static PeCpuStateSession s_state_session;
static unsigned char *s_state_out,*s_state_scratch;
static int s_state_identity_ready,s_state_request,s_state_waiting,s_state_preparing,s_state_terminal;
static char s_state_directory[256],s_state_status[192]="No checkpoint requested";
static char s_state_names[64][35],s_state_recent[64][35];static unsigned s_state_count,s_state_scanned,s_state_recent_count;static int s_state_truncated,s_state_list_error;static int s_state_selected=-1;static char s_state_scan_name[35];static int s_state_scan_seen;
extern void pe_mod_panel_close(void);
extern void pe_toast(const char *text);
extern int pe_mod_state_panel_closed(void);
extern int CPE_StateHostIdle(void);
unsigned CPE_StateHostReasonBits(void);
const char *CPE_StateHostReasonText(unsigned bits);
/* Fixed-width time/sequence names sort newest first. Keep the newest64 among
 * bounded4096 raw entries, not whichever64 the filesystem returned first. */
static void StateInsertName(const char *input){char canonical[35];memcpy(canonical,input,34);canonical[34]=0;const char*name=canonical;unsigned at=0;while(at<s_state_count&&strcmp(s_state_names[at],name)>0)at++;if(at<s_state_count&&!strcmp(s_state_names[at],name))return;if(at>=64){s_state_truncated=1;return;}if(s_state_count<64)s_state_count++;else s_state_truncated=1;for(unsigned i=s_state_count-1;i>at;i--)memcpy(s_state_names[i],s_state_names[i-1],35);memcpy(s_state_names[at],name,34);s_state_names[at][34]=0;}
static int StateSelectName(const char *name){for(unsigned i=0;i<s_state_count;i++)if(!strcmp(s_state_names[i],name)){s_state_selected=(int)i;return 1;}return 0;}
static int StateSavedThisRun(const char *name){for(unsigned i=0;i<s_state_recent_count;i++)if(!strcmp(s_state_recent[i],name))return 1;return 0;}
static void StateRememberCapture(const char *name){if(!StateSavedThisRun(name)){if(s_state_recent_count<64)s_state_recent_count++;for(unsigned i=s_state_recent_count-1;i>0;i--)memcpy(s_state_recent[i],s_state_recent[i-1],35);memcpy(s_state_recent[0],name,35);}if(!StateSelectName(name)&&s_state_count==64){s_state_count--;s_state_truncated=1;}StateInsertName(name);StateSelectName(name);}
static SDL_EnumerationResult StateList(void *context,const char *dirname,const char *name){
    (void)context;(void)dirname;if(++s_state_scanned>4096){s_state_truncated=1;return SDL_ENUM_SUCCESS;}size_t n=strlen(name);if(n!=41||strcmp(name+34,".commit")||strncmp(name,"pe-state-",9))return SDL_ENUM_CONTINUE;
    for(unsigned i=9;i<34;i++)if(i!=25&&!((name[i]>='0'&&name[i]<='9')||(name[i]>='a'&&name[i]<='f')))return SDL_ENUM_CONTINUE;
    if(name[25]!='-')return SDL_ENUM_CONTINUE;if(s_state_scan_name[0]&&!strncmp(name,s_state_scan_name,34))s_state_scan_seen=1;StateInsertName(name);return SDL_ENUM_CONTINUE;
}
static void StateRefreshList(void){char selected[35]={0};if(s_state_selected>=0&&(unsigned)s_state_selected<s_state_count)memcpy(selected,s_state_names[s_state_selected],35);memcpy(s_state_scan_name,selected,35);s_state_scan_seen=0;s_state_count=s_state_scanned=0;s_state_truncated=0;s_state_list_error=0;if(!s_state_directory[0]||!SDL_EnumerateDirectory(s_state_directory,StateList,NULL)){s_state_list_error=1;snprintf(s_state_status,sizeof s_state_status,"Cannot list checkpoint files; check the save-data folder and retry Refresh");}s_state_selected=-1;if(s_state_scan_seen){if(!StateSelectName(selected)){if(s_state_count==64){s_state_count--;s_state_truncated=1;}StateInsertName(selected);StateSelectName(selected);}}else if(selected[0]&&!s_state_list_error)snprintf(s_state_status,sizeof s_state_status,"Selected file was not found in the bounded scan; choose a file again");s_state_scan_name[0]=0;}
static void StateSelectStep(int direction){if(!s_state_count){s_state_selected=-1;return;}if(s_state_selected<0||(unsigned)s_state_selected>=s_state_count)s_state_selected=direction<0?(int)s_state_count-1:0;else s_state_selected=(s_state_selected+(direction<0?-1:1)+(int)s_state_count)%(int)s_state_count;}
static void StatePrevious(void){StateSelectStep(-1);}
static void StateNext(void){StateSelectStep(1);}
static void StateFileLabel(char*out,unsigned n){if(s_state_list_error&&!s_state_count)snprintf(out,n,"Folder unavailable");else if(!s_state_count)snprintf(out,n,"No checkpoint files");else if(s_state_selected<0||(unsigned)s_state_selected>=s_state_count)snprintf(out,n,"Choose a file (%u on disk)",s_state_count);else snprintf(out,n,"File %u/%u - %s",(unsigned)s_state_selected+1,s_state_count,StateSavedThisRun(s_state_names[s_state_selected])?"this run":"on disk");}
static const char *StateSelectedHelp(void){static char text[320];if(!s_state_count)return s_state_list_error?"The checkpoint folder could not be read. Check save-data access, then choose Refresh files.":"No checkpoint files found. Save a new checkpoint in this game run first.";if(s_state_selected<0||(unsigned)s_state_selected>=s_state_count)return "Choose a checkpoint with Left/Right, or Previous/Next in this page. Files on disk may be from another run; only captures from this launch can load.";const char*name=s_state_names[s_state_selected];snprintf(text,sizeof text,"File %u of %u; %s. Newest files come first. Loading still checks this run, room, settings and retained actor layout. %s",(unsigned)s_state_selected+1,s_state_count,StateSavedThisRun(name)?"saved during this run":"other file on disk; not in recent captures from this run",s_state_truncated?"Folder scan/list limit reached.":"");return text;}
static void StateStatus(char *out,unsigned n){snprintf(out,n,"%s",s_state_status);}
static void StateScope(char *out,unsigned n){
#if PE_NATIVE_STATE_CHANGED_COHORTS
    PeStateMetadataStatus m={0};PeCpuStateSession_MetadataStatus(&s_state_session,&m);
    snprintf(out,n,"Same process / room / settings; layouts %u/%u%s%s",m.retained,m.capacity,m.stale?"; old layouts invalidated":"",s_state_truncated?"; list truncated":"");
#else
    snprintf(out,n,"Same process / room / actors / settings%s",s_state_truncated?"; list truncated":"");
#endif
}
static const char *StateScopeHelp(void){
    static char text[640];PeStateMetadataStatus m={0};PeCpuStateSession_MetadataStatus(&s_state_session,&m);
    const char*why=m.stale?(m.reason==PE_STATE_INVALIDATE_NORMAL_LOAD?" Loading a normal memory-card save invalidated earlier layouts; capture a new checkpoint.":" Room/source state changed; earlier layouts cannot be used."):"";
    snprintf(text,sizeof text,"Same launch and room, unchanged settings/resources. Recent actor layouts %u/%u; this is separate from the file list. Supported actor changes are experimental.%s No cross-launch or cross-room loading; normal saves and global Collection progress stay separate.",m.retained,m.capacity,why);return text;
}
static unsigned s_state_action_serial;static int s_state_last_operation;
static void StateCapture(void){SDL_Log("[native checkpoint] action=capture serial=%u phase=menu-request open=1 hostbits=0x%x expected_self_modal=1 particles_requested=%d particles_effective=%d",++s_state_action_serial,CPE_StateHostReasonBits(),host_options.battle_debris_3d,FieldFx_GetParticleAugment());if(s_state_terminal||s_state_session.pending||s_state_waiting)return;if(!s_state_directory[0]){snprintf(s_state_status,sizeof s_state_status,"Checkpoint folder unavailable; check save-data access before saving");pe_toast(s_state_status);return;}s_state_request=1;pe_mod_panel_close();}
static void StateLoad(void){SDL_Log("[native checkpoint] action=load serial=%u phase=menu-request open=1 hostbits=0x%x expected_self_modal=1 particles_requested=%d particles_effective=%d",++s_state_action_serial,CPE_StateHostReasonBits(),host_options.battle_debris_3d,FieldFx_GetParticleAugment());if(s_state_terminal||s_state_session.pending||s_state_waiting)return;if(!s_state_directory[0]){snprintf(s_state_status,sizeof s_state_status,"Checkpoint folder unavailable; check save-data access before loading");pe_toast(s_state_status);return;}if(!s_state_count||s_state_selected<0||(unsigned)s_state_selected>=s_state_count){snprintf(s_state_status,sizeof s_state_status,"Choose a checkpoint file first, then select Load checkpoint");pe_toast(s_state_status);return;}s_state_request=2;pe_mod_panel_close();}
static void StateRefresh(void){StateRefreshList();}
static void StateUseOriginalEffects(void){host_options.battle_debris_3d=0;FieldFx_SetParticleAugment(0);int saved=Host_SaveOptions();snprintf(s_state_status,sizeof s_state_status,"Original effects selected; extra geometry Off%s",saved?"":" (not persisted)");pe_toast(s_state_status);SDL_Log("[native checkpoint] user_action=original-effects requested=0 effective=%d persisted=%d",FieldFx_GetParticleAugment(),saved);}
static void StateRequirements(char *out,unsigned n){unsigned b=CPE_StateHostReasonBits()&~(4u|128u);snprintf(out,n,"%s",b?CPE_StateHostReasonText(b):"Checked after menu closes; normal gameplay only");}
static void StateHashU64(PeSha256 *sha,uint64_t v){unsigned char b[8];for(unsigned i=0;i<8;i++)b[i]=(unsigned char)(v>>(i*8));pe_sha256_update(sha,b,8);}
static char s_state_identity_error[192];
static int StateIdentity(PeStateIdentity *identity){
    snprintf(s_state_identity_error,sizeof s_state_identity_error,"Disc verification could not finish; check both disc paths in System");
    PeSha256 sha;pe_sha256_init(&sha);static const char domain[]="PE-native-checkpoint-disc-pair-v1";pe_sha256_update(&sha,domain,sizeof domain);
    unsigned char *buffer=malloc(65536);if(!buffer){snprintf(s_state_identity_error,sizeof s_state_identity_error,"Not enough memory for disc verification; nothing queued");return 0;}int ok=1;const int audio_paused=Psyz_AudioIsPaused();
    s_state_preparing=1;HostOverlayState previous_overlay=s_overlayState;s_overlayState=(HostOverlayState){.open=1,.installer=1,.dirty=1};s_install.active=1;s_install.phase=PE_IV_PROGRESS;s_install.cancel=0;Psyz_PauseHostClock_SDL3(true);Psyz_AudioPause();
    for(unsigned slot=0;slot<2&&ok;slot++){
        char path[4096];PeDisc disc;if(!Host_DataPath(path,sizeof path,host_options.discs[slot])||!pe_disc_open(&disc,path)){snprintf(s_state_identity_error,sizeof s_state_identity_error,"Cannot open or validate Disc %u; check its file in System",slot+1);ok=0;break;}
        uint64_t total=disc.image_size>=disc.track_offset?disc.image_size-disc.track_offset:0;
        StateHashU64(&sha,slot+1);StateHashU64(&sha,disc.sector_size);StateHashU64(&sha,disc.payload_offset);StateHashU64(&sha,total);
        if(disc.sector_size!=2352||total<2352){snprintf(s_state_identity_error,sizeof s_state_identity_error,"Disc %u is not a supported raw BIN image; check System",slot+1);ok=0;pe_disc_close(&disc);break;}
        PeDiscFile first={.sector=0,.size=2352};if(!pe_disc_read_raw(&disc,&first,0,buffer)){snprintf(s_state_identity_error,sizeof s_state_identity_error,"Could not read Disc %u; check its file and USB connection",slot+1);ok=0;pe_disc_close(&disc);break;}
        pe_sha256_update(&sha,buffer,2352);uint64_t done=2352,next=0;
        while(done<total){size_t want=(size_t)((total-done)>65536?65536:(total-done));if(fread(buffer,1,want,disc.stream)!=want){snprintf(s_state_identity_error,sizeof s_state_identity_error,"Disc %u read stopped before verification finished; check its file and USB connection",slot+1);ok=0;break;}pe_sha256_update(&sha,buffer,want);done+=want;
            if(done>=next){next=done+1024*1024;s_install.done=done;s_install.total=total;snprintf(s_install.status,sizeof s_install.status,"Checking disc %u identity; Circle cancels",slot+1);s_overlayState.dirty=1;InstallFrame();if(s_install.cancel){snprintf(s_state_identity_error,sizeof s_state_identity_error,"Disc verification cancelled; nothing queued");ok=0;break;}}
        }pe_disc_close(&disc);
    }
    if(ok){memcpy(identity->build,cpe_state_build_identity,32);pe_sha256_final(&sha,identity->disc);}
    free(buffer);s_install.active=0;s_overlayState=previous_overlay;s_overlayState.dirty=1;Host_OverlayRefreshGPU(s_overlayGpuDevice,Psyz_GetWindow_SDL3(),&s_overlayState);SDL_ResetKeyboard();Psyz_PauseHostClock_SDL3(false);if(!audio_paused)Psyz_AudioUnpause();s_state_preparing=0;return ok;
}
void CPE_StateUi_AfterModalClosed(void){
    int request=s_state_request;s_state_request=0;if(!request||s_state_terminal||s_state_session.pending)return;
    unsigned hostbits=CPE_StateHostReasonBits();SDL_Log("[native checkpoint] phase=after-modal-close serial=%u request=%d hostbits=0x%x gpu_bits=0x%x overlay=%d panel_closed=%d paused_audio=%d installer=%d particles_requested=%d particles_effective=%d",s_state_action_serial,request,hostbits,Psyz_StateGpuControlBlockers(),s_overlayState.open,pe_mod_state_panel_closed(),Psyz_AudioIsPaused(),s_install.active,host_options.battle_debris_3d,FieldFx_GetParticleAugment());
    if(hostbits){snprintf(s_state_status,sizeof s_state_status,"%s",CPE_StateHostReasonText(hostbits));pe_toast(s_state_status);return;}
    if(!s_state_identity_ready){PeStateIdentity identity;if(!StateIdentity(&identity)){snprintf(s_state_status,sizeof s_state_status,"%s",s_state_identity_error);pe_toast(s_state_status);return;}PeCpuStateSession_Init(&s_state_session,&identity);s_state_identity_ready=1;}
    s_state_out=malloc(PE_STATE_MAX_BYTES);if(request==2)s_state_scratch=malloc(PE_STATE_MAX_BYTES);
    if(!s_state_out||(request==2&&!s_state_scratch)){free(s_state_out);free(s_state_scratch);s_state_out=s_state_scratch=NULL;snprintf(s_state_status,sizeof s_state_status,"Not enough memory to prepare checkpoint; nothing queued and gameplay unchanged");pe_toast(s_state_status);SDL_Log("[native checkpoint] queue=not-attempted allocation_failed=1 operation=%d",request);return;}
    int r=request==1?PeCpuStateSession_RequestCaptureFile(&s_state_session,s_state_out,PE_STATE_MAX_BYTES,s_state_directory):PeCpuStateSession_RequestRestoreComponentsFile(&s_state_session,s_state_out,PE_STATE_MAX_BYTES,s_state_scratch,PE_STATE_MAX_BYTES,s_state_directory,s_state_names[s_state_selected]);
    if(r){free(s_state_out);free(s_state_scratch);s_state_out=s_state_scratch=NULL;snprintf(s_state_status,sizeof s_state_status,"%s",r==PE_STATE_BUSY?"Another checkpoint is still pending; wait for its result":r==PE_STATE_ROLLBACK_FAILED?"Previous load could not be recovered; restart before using checkpoints":r==PE_STATE_INVALID?"Checkpoint request is invalid; check the folder and selected file":r==PE_STATE_BOUNDS?"Checkpoint request exceeds the supported buffer size":"Checkpoint preparation was rejected; resume gameplay and try again");SDL_Log("[native checkpoint] phase=queue-rejected operation=%d result=%d",request,r);pe_toast(s_state_status);return;}
    s_state_last_operation=request;s_state_waiting=1;snprintf(s_state_status,sizeof s_state_status,"Queued; resume to reach the safe CPU boundary");SDL_Log("[native checkpoint] queued operation=%s completed=0",request==1?"capture":"load");
}
static const char *StateOwnerLabel(unsigned id){switch(id){case PE_STATE_GPU:return "graphics/textures";case PE_STATE_AUDIO:case PE_STATE_AKAO:return "audio";case PE_STATE_ACTORS:case PE_STATE_BATTLE:return "characters/battle";case PE_STATE_PM:return "room effects or gameplay mods";case PE_STATE_SCRIPTS:case PE_STATE_SCHEDULER:return "active game events";case PE_STATE_ASSETS:case PE_STATE_SCENE:return "room resources";case PE_STATE_ORIGINAL_SAVE:case PE_STATE_PERSISTENT:return "inventory/player data";case PE_STATE_MODS:return "gameplay mods";default:return "game state";}}
void CPE_StateUi_Poll(void){
    if(!s_state_waiting||s_state_session.pending)return;s_state_waiting=0;
    free(s_state_out);free(s_state_scratch);s_state_out=s_state_scratch=NULL;
    if(s_state_session.result==PE_STATE_ROLLBACK_FAILED)s_state_terminal=1;
    if(!s_state_session.result){StateRefreshList();if(s_state_last_operation==1){StateRememberCapture(s_state_session.stateFile);snprintf(s_state_status,sizeof s_state_status,"Checkpoint saved and selected for Load; same-run checks still apply%s",s_state_list_error?"; folder refresh failed":"");}else snprintf(s_state_status,sizeof s_state_status,"Checkpoint loaded%s",s_state_list_error?"; folder refresh failed":"");}
    else {const char*why=s_state_session.result==PE_STATE_ROLLBACK_FAILED?"Load recovery failed; restart before using checkpoints":s_state_session.boundaryReason==18&&s_state_last_operation==1?"File saved, but its actor layout was not retained; resume gameplay and save a new checkpoint":s_state_session.boundaryReason==16?(s_state_session.result==PE_STATE_INCOMPATIBLE?"File incompatible with this build, discs or checkpoint format; save a new checkpoint this run":s_state_session.result==PE_STATE_CHECKSUM?"Checkpoint file could not be verified; refresh or choose another file":s_state_session.result==PE_STATE_BOUNDS?(s_state_last_operation==1?"Could not create a unique checkpoint file; wait a moment and retry":"Checkpoint file size is invalid or unsupported; choose another file"):s_state_session.result==PE_STATE_INVALID?(s_state_last_operation==1?"Checkpoint folder or captured data is unsupported; nothing was saved":"Checkpoint path or file format is invalid; choose a capture made by this build"):s_state_last_operation==1?"Could not save checkpoint files; check save-data access and free space":"Could not read checkpoint files; refresh the list and choose another file"):PeCpuStateSession_RoomBoundaryText(s_state_session.boundaryReason);PeStateFailure failed=PeState_LastFailure();const char*stage=PeState_FailureStageText(failed.stage);if(s_state_session.result!=PE_STATE_ROLLBACK_FAILED&&(!strcmp(stage,"barrier-begin")||!strcmp(stage,"barrier-changed")))snprintf(s_state_status,sizeof s_state_status,"Game state was busy during checkpoint preparation; resume gameplay and retry");else if(s_state_session.result!=PE_STATE_ROLLBACK_FAILED&&!strcmp(stage,"buffer-bounds"))snprintf(s_state_status,sizeof s_state_status,"Checkpoint data exceeds the supported buffer size; choose a different capture or scene");else if(s_state_session.result!=PE_STATE_ROLLBACK_FAILED&&s_state_session.boundaryReason==PE_STATE_ROOM_OWNERS&&failed.module)snprintf(s_state_status,sizeof s_state_status,"%s blocked by %s; %s",s_state_last_operation==1?"Save":"Load",StateOwnerLabel(failed.module),s_state_last_operation==1?"resume gameplay and retry":"use matching settings/resources or a recent capture");else snprintf(s_state_status,sizeof s_state_status,"%s",why);}
    pe_toast(s_state_status);
    SDL_Log("[native checkpoint] phase=cpu-result serial=%u hostbits=0x%x gpu_bits=0x%x",s_state_action_serial,CPE_StateHostReasonBits(),Psyz_StateGpuControlBlockers());
    SDL_Log("[native checkpoint] completed=1 result=%d boundary=%d bytes=%zu file=%s terminal=%d",s_state_session.result,s_state_session.boundaryReason,s_state_session.used,s_state_session.stateFile,s_state_terminal);
    if(s_state_terminal){SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION,"Checkpoint rollback failed: restart required; checkpoint actions disabled");Host_OpenSettingsTab(HOST_PAGE_STATES);}
}
static void Host_StateUiRegister(void){
#if PE_NATIVE_STATE_CHANGED_COHORTS
    const char*fileHelp="Immutable captures; same launch/build/discs/room/settings. Supported actor changes require one of8 retained captured layouts; reloaded resources or evicted layouts remain incompatible";
#else
    const char*fileHelp="Immutable captures; load requires same build, discs, room, actors and settings";
#endif
    const char *slash=strrchr(host_options.user_file,'/');if(!slash)return;int n=snprintf(s_state_directory,sizeof s_state_directory,"%.*s/checkpoints",(int)(slash-host_options.user_file),host_options.user_file);if(n<0||(size_t)n>=sizeof s_state_directory||!SDL_CreateDirectory(s_state_directory)){s_state_directory[0]=0;snprintf(s_state_status,sizeof s_state_status,"Checkpoint directory unavailable");return;}
    pe_menu_begin("Playtest assists");pe_menu_page("Checkpoints","Experimental current-room checkpoints. Supported gameplay mods may stay On; close custom screens and finish pending transactions. Final checks run after this menu closes.",90);
    pe_menu_register_info("Checkpoints","Prerequisite","This menu is expected to be paused. No eligibility decision is made until it closes; Original disables extra geometry, independently of All mods.",StateRequirements);
    pe_menu_register_action("Checkpoints","Use Original effects","Explicitly selects Advanced > Extra particle geometry > Original (extra geometry Off); normal original effects continue.",StateUseOriginalEffects);
    pe_menu_register_info("Checkpoints","Scope","Ordinary memory-card saves remain separate; no arbitrary-frame or cross-room load",StateScope);
    pe_menu_register_info("Checkpoints","Last operation","Queued is not completed; failure never means success",StateStatus);
    pe_menu_register_info("Checkpoints","Checkpoint file",fileHelp,StateFileLabel);
    pe_menu_register_action("Checkpoints","Previous checkpoint","Choose the previous committed file; does not load or overwrite it.",StatePrevious);
    pe_menu_register_action("Checkpoints","Next checkpoint","Choose the next committed file; does not load or overwrite it.",StateNext);
    pe_menu_register_action("Checkpoints","Capture checkpoint","Closes menu; safe gameplay boundary only; listing limited to64 /4096 entries",StateCapture);
    pe_menu_register_action("Checkpoints","Load selected checkpoint","Closes this menu; validates components and rollback before changes",StateLoad);
    pe_menu_register_action("Checkpoints","Refresh file list","Newest first; preserves selected file. Lists64 files from at most4096 entries; never overwrites",StateRefresh);
    StateRefreshList();
}
static bool Host_StateChord(const SDL_Event *event){
    if(event->type!=SDL_EVENT_GAMEPAD_BUTTON_DOWN||event->gbutton.button!=SDL_GAMEPAD_BUTTON_TOUCHPAD)return false;
    SDL_Gamepad *pad=SDL_GetGamepadFromID(event->gbutton.which);if(!pad||SDL_GetGamepadAxis(pad,SDL_GAMEPAD_AXIS_LEFT_TRIGGER)<24000||SDL_GetGamepadAxis(pad,SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)<24000)return false;
    Host_OpenSettingsTab(HOST_PAGE_STATES);return true;
}
