/* SPDX-License-Identifier: MIT -- same native renderer/device, no SDL_Renderer. */
#include "pe_bootstrap.h"
#include "pe_pack_check.h"
#include "pe_character_pack.h"
static int InstallUsbPath(const char*p){return p&&!strncmp(p,"/mnt/usb",8)&&p[8]>='0'&&p[8]<='7'&&(p[9]=='/'||!p[9])&&!strstr(p,"/../");}
static SDL_EnumerationResult InstallEnumerate(void*unused,const char*dirname,const char*fname){
    (void)unused;if(!strcmp(fname,".")||!strcmp(fname,"..")||strlen(fname)>=256)return SDL_ENUM_CONTINUE;
    if(++s_install.scanned>4096){s_install.truncated=2;return SDL_ENUM_SUCCESS;}
    char path[4096];int n=snprintf(path,sizeof(path),"%s%s%s",dirname,dirname[strlen(dirname)-1]=='/'?"":"/",fname);if(n<0||(size_t)n>=sizeof(path))return SDL_ENUM_CONTINUE;
    SDL_PathInfo info;if(!SDL_GetPathInfo(path,&info))return SDL_ENUM_CONTINUE;const char*ext=strrchr(fname,'.');
    if(info.type!=SDL_PATHTYPE_DIRECTORY&&(!ext||SDL_strcasecmp(ext,".bin")))return SDL_ENUM_CONTINUE;
    if(s_install.count==128){s_install.truncated=1;return SDL_ENUM_SUCCESS;}
    PeInstallEntry*entry=&s_install.entries[s_install.count++];SDL_strlcpy(entry->name,fname,sizeof(entry->name));entry->directory=info.type==SDL_PATHTYPE_DIRECTORY;return SDL_ENUM_CONTINUE;
}
static void InstallList(void){s_install.count=s_install.cursor=s_install.scroll=s_install.truncated=s_install.scanned=0;s_install.enumeration_failed=!SDL_EnumerateDirectory(s_install.directory,InstallEnumerate,NULL);s_overlayState.dirty=1;}
static void InstallBrowse(int target){s_install.browser_selected=0;s_install.target=target;s_install.phase=PE_IV_BROWSER;
    const char*p=target<2?s_install.disc[target]:target==2?s_install.destination:target==3?s_install.hd:s_install.characters;
    if(InstallUsbPath(p)){s_install.usb=p[8]-'0';SDL_strlcpy(s_install.directory,p,sizeof(s_install.directory));if(target<2){char*slash=strrchr(s_install.directory,'/');if(slash)*slash=0;}}
    else snprintf(s_install.directory,sizeof(s_install.directory),"/mnt/usb%d",s_install.usb);InstallList();
}
static void InstallSelectFolder(void){s_install.browser_selected=1;if(s_install.target==2)SDL_strlcpy(s_install.destination,s_install.directory,sizeof(s_install.destination));else if(s_install.target==3){SDL_strlcpy(s_install.hd,s_install.directory,sizeof(s_install.hd));s_install.hd_on=1;}else{SDL_strlcpy(s_install.characters,s_install.directory,sizeof(s_install.characters));s_install.characters_on=1;}s_install.phase=PE_IV_FORM;}
static void InstallAction(int button){
    if(button==SDL_GAMEPAD_BUTTON_EAST){if(s_install.phase==PE_IV_BROWSER){char root[32];snprintf(root,sizeof(root),"/mnt/usb%d",s_install.usb);
        if(!strcmp(s_install.directory,root))s_install.phase=PE_IV_FORM;else{char*p=strrchr(s_install.directory,'/');if(p)*p=0;if(strlen(s_install.directory)<strlen(root))SDL_strlcpy(s_install.directory,root,sizeof(s_install.directory));InstallList();}}
        else s_install.cancel=1;return;}
    if(s_install.phase==PE_IV_PROGRESS)return;
    if(s_install.phase==PE_IV_ERROR){if(button==SDL_GAMEPAD_BUTTON_SOUTH)s_install.phase=PE_IV_FORM;return;}
    if(s_install.phase==PE_IV_SUCCESS){if(button==SDL_GAMEPAD_BUTTON_SOUTH)s_install.accept=1;return;}
    if(s_install.phase==PE_IV_BROWSER){
        if(button==SDL_GAMEPAD_BUTTON_LEFT_SHOULDER||button==SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER){s_install.usb=(s_install.usb+(button==SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER?1:7))%8;snprintf(s_install.directory,sizeof(s_install.directory),"/mnt/usb%d",s_install.usb);InstallList();}
        else if(button==SDL_GAMEPAD_BUTTON_DPAD_UP&&s_install.count)s_install.cursor=(s_install.cursor+s_install.count-1)%s_install.count;
        else if(button==SDL_GAMEPAD_BUTTON_DPAD_DOWN&&s_install.count)s_install.cursor=(s_install.cursor+1)%s_install.count;
        else if(button==SDL_GAMEPAD_BUTTON_NORTH&&s_install.target>=2)InstallSelectFolder();
        else if(button==SDL_GAMEPAD_BUTTON_SOUTH&&s_install.count){PeInstallEntry*e=&s_install.entries[s_install.cursor];char path[4096];int n=snprintf(path,sizeof(path),"%s/%s",s_install.directory,e->name);if(n>0&&(size_t)n<sizeof(path)){
            if(e->directory){SDL_strlcpy(s_install.directory,path,sizeof(s_install.directory));InstallList();}
            else if(s_install.target<2){SDL_strlcpy(s_install.disc[s_install.target],path,4096);s_install.browser_selected=1;s_install.phase=PE_IV_FORM;}}}
        if(s_install.cursor<s_install.scroll)s_install.scroll=s_install.cursor;if(s_install.cursor>=s_install.scroll+9)s_install.scroll=s_install.cursor-8;return;
    }
    if(button==SDL_GAMEPAD_BUTTON_DPAD_UP)s_install.row=(s_install.row+8)%9;
    else if(button==SDL_GAMEPAD_BUTTON_DPAD_DOWN)s_install.row=(s_install.row+1)%9;
    else if(button==SDL_GAMEPAD_BUTTON_DPAD_LEFT||button==SDL_GAMEPAD_BUTTON_DPAD_RIGHT){if(s_install.row==2)s_install.copy=!s_install.copy;if(s_install.row==4)s_install.hd_on=!s_install.hd_on;if(s_install.row==6)s_install.characters_on=!s_install.characters_on;}
    else if(button==SDL_GAMEPAD_BUTTON_SOUTH){if(s_install.row<2)InstallBrowse(s_install.row);else if(s_install.row==2)s_install.copy=!s_install.copy;else if(s_install.row==3)InstallBrowse(2);else if(s_install.row==4)s_install.hd_on=!s_install.hd_on;else if(s_install.row==5)InstallBrowse(3);else if(s_install.row==6)s_install.characters_on=!s_install.characters_on;else if(s_install.row==7)InstallBrowse(4);else s_install.accept=1;}
}
static int Host_InstallerEvent(const SDL_Event*event){
    int button=-1;if(event->type==SDL_EVENT_QUIT||event->type==SDL_EVENT_WINDOW_CLOSE_REQUESTED){s_install.cancel=1;return 1;}
    if(event->type==SDL_EVENT_GAMEPAD_BUTTON_DOWN)button=event->gbutton.button;
    else if(event->type==SDL_EVENT_KEY_DOWN&&!event->key.repeat){switch(event->key.scancode){case SDL_SCANCODE_UP:button=SDL_GAMEPAD_BUTTON_DPAD_UP;break;case SDL_SCANCODE_DOWN:button=SDL_GAMEPAD_BUTTON_DPAD_DOWN;break;case SDL_SCANCODE_LEFT:button=SDL_GAMEPAD_BUTTON_DPAD_LEFT;break;case SDL_SCANCODE_RIGHT:button=SDL_GAMEPAD_BUTTON_DPAD_RIGHT;break;case SDL_SCANCODE_RETURN:button=SDL_GAMEPAD_BUTTON_SOUTH;break;case SDL_SCANCODE_ESCAPE:button=SDL_GAMEPAD_BUTTON_EAST;break;case SDL_SCANCODE_SPACE:button=SDL_GAMEPAD_BUTTON_NORTH;break;default:break;}}
    else if(event->type==SDL_EVENT_GAMEPAD_AXIS_MOTION&&(event->gaxis.axis==SDL_GAMEPAD_AXIS_LEFTX||event->gaxis.axis==SDL_GAMEPAD_AXIS_LEFTY)){
        int vertical=event->gaxis.axis==SDL_GAMEPAD_AXIS_LEFTY;int*p=vertical?&s_install.axis_y:&s_install.axis_x;int v=event->gaxis.value;int next=v>16000?1:v< -16000?-1:(v>-8000&&v<8000?0:*p);
        if(next!=*p){*p=next;if(next)button=vertical?(next>0?SDL_GAMEPAD_BUTTON_DPAD_DOWN:SDL_GAMEPAD_BUTTON_DPAD_UP):(next>0?SDL_GAMEPAD_BUTTON_DPAD_RIGHT:SDL_GAMEPAD_BUTTON_DPAD_LEFT);}}
    if(button>=0){InstallAction(button);s_overlayState.dirty=1;return 1;}return 0;
}
static void InstallFrame(void){SDL_Window*w=Psyz_GetWindow_SDL3();if(!w||!s_overlayGpuDevice){s_install.cancel=1;return;}Host_OverlayRefreshGPU(s_overlayGpuDevice,w,&s_overlayState);Psyz_VideoVSync(0);if(Psyz_GpuFailed())s_install.cancel=1;}
static int InstallProgress(const char*phase,uint64_t done,uint64_t total,void*unused){
    (void)unused;static Uint64 last;Uint64 now=SDL_GetTicks();if(now-last>=16||done==total){last=now;SDL_strlcpy(s_install.status,phase,sizeof(s_install.status));s_install.done=done;s_install.total=total;s_overlayState.dirty=1;InstallFrame();}return !s_install.cancel;
}
static int InstallCheckPack(const char*path,void*unused){(void)unused;return InstallUsbPath(path)&&pe_install_verify_native_pack(path,InstallProgress,NULL);}
static int InstallCheckCharacters(const char*path,void*unused){(void)unused;PeCharacterPackReport r={0};int ok=InstallUsbPath(path)&&pe_install_verify_character_pack(path,InstallProgress,NULL,&r);if(!ok&&r.error[0])SDL_strlcpy(s_install.status,r.error,sizeof s_install.status);return ok;}
static void InstallFindCharacters(void){
 static const char*const suffix[]={"parasite-eve-data/mods/characters/duck-models","parasite-eve-data/mods/duck-models","duck-models","parasite-eve-data/mods/characters/duck-models-67"};
 /* At most32 exact header reads; no recursive scan, no implicit activation. */
 for(unsigned usb=0;usb<8;usb++)for(unsigned j=0;j<4;j++){char path[4096];snprintf(path,sizeof path,"/mnt/usb%u/%s",usb,suffix[j]);if(pe_install_character_pack_hint(path)){SDL_strlcpy(s_install.characters,path,sizeof s_install.characters);s_install.characters_found=1;SDL_Log("[Native installer] character-subset-hint=%s selected=0 verification=pending",path);return;}}
}
static int Host_NativeFirstRun(void){
    PeInstallCallbacks cb={.validate_disc=pe_bootstrap_validate_game_disc,.progress=InstallProgress,.validate_native_hd=InstallCheckPack,.validate_characters=InstallCheckCharacters};PeInstallReport report;
    char preferences_root[4096];SDL_strlcpy(preferences_root,host_options.user_file,sizeof(preferences_root));char*preferences_slash=strrchr(preferences_root,'/');
    if(!preferences_slash||preferences_slash==preferences_root){SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"Installer preferences root is unsupported");return -1;}*preferences_slash=0;
    PeInstallRequest q={.data_root=preferences_root,.mode=PE_INSTALL_USE_USB};
    if(pe_bootstrap_install(&q,&cb,&report)==PE_INSTALL_EXISTING)return 1;
    memset(&s_install,0,sizeof(s_install));s_install.active=1;SDL_strlcpy(s_install.disc[0],"/mnt/usb0/parasite-eve-data/discs/disc1.bin",4096);SDL_strlcpy(s_install.disc[1],"/mnt/usb0/parasite-eve-data/discs/disc2.bin",4096);
    SDL_strlcpy(s_install.destination,"/mnt/usb0/parasite-eve-data/discs",4096);SDL_strlcpy(s_install.hd,"/mnt/usb0/parasite-eve-data/mods/upscale/0.1-ded1557f6c1e",4096);
    SDL_strlcpy(s_install.characters,"/mnt/usb0/parasite-eve-data/mods/characters/duck-models",sizeof s_install.characters);InstallFindCharacters();
    s_overlayState=(HostOverlayState){.open=1,.installer=1,.dirty=1};Host_InstallSettingsOverlay();
    /* Exactly the original GPU control path, without game memory/audio init. */
    {DISPENV env;ResetGraph(0);SetDefDispEnv(&env,0,0,320,240);PutDispEnv(&env);SetDispMask(0);}
    Psyz_VideoVSync(0);
    /* Existing idempotent pad path opens current controllers and handles hotplug. */
    {extern void PadInit(int);PadInit(0);}
    if(!SDL_WasInit(SDL_INIT_GAMEPAD)){SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"Installer controller subsystem failed: %s",SDL_GetError());s_install.cancel=1;}
    if(!Psyz_PS5PlatformReady()||!s_overlayGpuDevice){SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"Installer uses existing GPU backend, initialization failed: %s",Psyz_PS5PlatformInitError());s_install.cancel=1;}
    if(report.interrupted_config_possible){s_install.phase=PE_IV_ERROR;SDL_strlcpy(s_install.status,report.error,256);}
    int ready=0;while(!s_install.cancel){s_install.accept=0;while(!s_install.accept&&!s_install.cancel)InstallFrame();if(s_install.cancel)break;
        q.disc[0]=s_install.disc[0];q.disc[1]=s_install.disc[1];q.copy_root=s_install.destination;q.mode=s_install.copy?PE_INSTALL_COPY_DISCS:PE_INSTALL_USE_USB;q.native_hd=s_install.hd_on?s_install.hd:NULL;q.native_characters=s_install.characters_on?s_install.characters:NULL;
        s_install.phase=PE_IV_PROGRESS;s_overlayState.dirty=1;InstallFrame();PeInstallResult result=pe_bootstrap_install(&q,&cb,&report);
        SDL_Log("[Native installer] result=%d config_created=%d pending=%d error=%s",result,report.config_created,report.interrupted_config_possible,report.error);
        if(result==PE_INSTALL_READY||result==PE_INSTALL_EXISTING){s_install.phase=PE_IV_SUCCESS;s_overlayState.dirty=1;SDL_strlcpy(s_install.status,"Files are ready. Your normal game settings, saves and controls will be used.",256);s_install.accept=0;while(!s_install.accept&&!s_install.cancel)InstallFrame();ready=s_install.cancel?0:2;break;}
        if(result==PE_INSTALL_CANCELLED||s_install.cancel)break;s_install.phase=PE_IV_ERROR;SDL_strlcpy(s_install.status,report.error,256);s_overlayState.dirty=1;
    }
    s_install.active=0;s_overlayState.open=s_overlayState.installer=0;SDL_ResetKeyboard();if(!Psyz_PS5PlatformReady()||Psyz_GpuFailed()||!SDL_WasInit(SDL_INIT_GAMEPAD))return -1;return ready;
}



/* Existing GPU/controller browser for adding only a missing image, never replacing a mounted volume. */
static int Host_NativePickMissingDisc(unsigned identity){
    if(identity<1||identity>2||g_CdImageVolumes[identity-1]||s_install.active)return 0;
    HostOverlayState previous=s_overlayState;memset(&s_install,0,sizeof s_install);s_install.active=1;
    SDL_strlcpy(s_install.disc[identity-1],host_options.discs[identity-1],4096);InstallBrowse((int)identity-1);
    s_overlayState.installer=1;s_overlayState.dirty=1;
    while(!s_install.cancel&&s_install.phase==PE_IV_BROWSER)InstallFrame();
    char selected[4096];SDL_strlcpy(selected,s_install.disc[identity-1],sizeof selected);int picked=s_install.browser_selected&&!s_install.cancel;
    s_install.active=0;s_overlayState=previous;s_overlayState.dirty=1;SDL_ResetKeyboard();
    if(!picked)return 0;
    int installed=Host_InstallImage(identity,selected);pe_toast(installed?"Disc validated and added":"This is not the missing USA disc image");return installed;
}
