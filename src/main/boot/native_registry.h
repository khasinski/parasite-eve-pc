/* SPDX-License-Identifier: MIT -- complete view of the existing menu registry.
 * No duplicate registration or preference storage; actions use the original API. */
#ifndef PE_NATIVE_REGISTRY_H
#define PE_NATIVE_REGISTRY_H
enum { HOST_REGISTRY_MAX=513, HOST_REGISTRY_MEMORIES=32 };
enum { HR_ROOT,HR_PERSONAL,HR_COMMUNITY,HR_ACTIVATION,HR_ASSETS };
static int Host_RegistryRouteScope(const char*page,int*tab){
 *tab=HOST_PAGE_MODS;if(!page||!*page)return HR_ROOT;
 if(!strncmp(page,"Playtest assists",16)&&(page[16]==0||page[16]=='/')){*tab=HOST_PAGE_CHEATS;return HR_PERSONAL;}
 return !strcmp(page,PE_MENU_MANAGE)?HR_ACTIVATION:HR_COMMUNITY;
}
typedef struct {int row,order;char path[96];} HostRegistryEntry;
typedef struct {char path[96];HostRegistryEntry entries[HOST_REGISTRY_MAX];int scope,count,overflow;struct {char path[96];int used,scope,row,scroll;} memory[HOST_REGISTRY_MEMORIES];} HostRegistryModel;
static HostRegistryModel host_registry[2];
static int Host_RegistrySlot(int page){return page==HOST_PAGE_CHEATS?1:0;}
static const char *Host_RegistryTitle(int page){if(page!=HOST_PAGE_MODS&&page!=HOST_PAGE_CHEATS)return "Settings";HostRegistryModel*m=&host_registry[Host_RegistrySlot(page)];if(m->path[0]){const char*leaf=strrchr(m->path,'/');return leaf?leaf+1:m->path;}return m->scope==HR_COMMUNITY?"Community projects":m->scope==HR_ASSETS?"Presentation packs":page==HOST_PAGE_MODS?"Mods":"Cheats";}
static int Host_RegistryPrefix(int page){(void)page;return 0;}
static void Host_RegistryBuild(int page){
    int slot=Host_RegistrySlot(page);HostRegistryModel*m=&host_registry[slot];m->count=m->overflow=0;
    if(m->scope==HR_ROOT){if(page==HOST_PAGE_CHEATS){m->entries[m->count++]=(HostRegistryEntry){.row=-10};m->entries[m->count++]=(HostRegistryEntry){.row=-11};}else{m->entries[m->count++]=(HostRegistryEntry){.row=-12};m->entries[m->count++]=(HostRegistryEntry){.row=-11};m->entries[m->count++]=(HostRegistryEntry){.row=-13};}return;}
    if(m->scope==HR_ASSETS){m->entries[m->count++]=(HostRegistryEntry){.row=-2};m->entries[m->count++]=(HostRegistryEntry){.row=-20};m->entries[m->count++]=(HostRegistryEntry){.row=-21};m->entries[m->count++]=(HostRegistryEntry){.row=-22};m->entries[m->count++]=(HostRegistryEntry){.row=-23};return;}
    if(m->scope!=HR_ROOT){m->entries[m->count++]=(HostRegistryEntry){.row=-2,.order=-1};}
    int n=pe_menu_row_count();size_t length=strlen(m->path);int links_start=m->count;
    for(int i=0;i<n;i++){PeMenuRow*r=pe_menu_row(i);if(!r||strcmp(r->page,m->path))continue;if(m->count==HOST_REGISTRY_MAX){m->overflow=1;break;}m->entries[m->count++]=(HostRegistryEntry){.row=i};}
    links_start=m->count;
    for(int i=0;i<n;i++){
        PeMenuRow*r=pe_menu_row(i);if(!r)continue;const char*rest=r->page;
        if(length){if(strncmp(rest,m->path,length)||rest[length]!='/')continue;rest+=length+1;}
        if(!*rest)continue;if(m->scope==HR_COMMUNITY&&!length&&((!strncmp(rest,"Playtest assists",16)&&(rest[16]==0||rest[16]=='/'))||(!strncmp(rest,PE_MENU_MANAGE,strlen(PE_MENU_MANAGE))&&(rest[strlen(PE_MENU_MANAGE)]==0||rest[strlen(PE_MENU_MANAGE)]=='/'))))continue;size_t end=(size_t)(rest-r->page)+strcspn(rest,"/");if(end>=96){m->overflow=1;continue;}
        char path[96];memcpy(path,r->page,end);path[end]=0;int duplicate=0;for(int j=links_start;j<m->count;j++)if(!strcmp(m->entries[j].path,path)){duplicate=1;break;}if(duplicate)continue;
        if(m->count==HOST_REGISTRY_MAX){m->overflow=1;break;}HostRegistryEntry*e=&m->entries[m->count++];memset(e,0,sizeof *e);e->row=-1;SDL_strlcpy(e->path,path,sizeof e->path);const PeMenuPageInfo*info=pe_menu_page_info(path);e->order=info?info->order:1000+m->count;
    }
    for(int i=links_start+1;i<m->count;i++){HostRegistryEntry e=m->entries[i];int j=i;while(j>links_start&&m->entries[j-1].order>e.order){m->entries[j]=m->entries[j-1];j--;}m->entries[j]=e;}
    if(!m->count)m->entries[m->count++]=(HostRegistryEntry){.row=-3};
}
static HostRegistryEntry *Host_RegistryEntryAt(int page,int row){Host_RegistryBuild(page);row-=Host_RegistryPrefix(page);int slot=Host_RegistrySlot(page);return row>=0&&row<host_registry[slot].count?&host_registry[slot].entries[row]:NULL;}
static void Host_RegistryGoTo(HostOverlayState*state,int scope,const char*path){
    int slot=Host_RegistrySlot(state->page);HostRegistryModel*m=&host_registry[slot];char destination[96];SDL_strlcpy(destination,path,sizeof destination);
    int at=-1;for(int i=0;i<HOST_REGISTRY_MEMORIES;i++)if(m->memory[i].used&&m->memory[i].scope==m->scope&&!strcmp(m->memory[i].path,m->path)){at=i;break;}else if(at<0&&!m->memory[i].used)at=i;
    if(at<0)at=0;m->memory[at].used=1;m->memory[at].scope=m->scope;SDL_strlcpy(m->memory[at].path,m->path,96);m->memory[at].row=state->row;m->memory[at].scroll=state->scroll;
    m->scope=scope;SDL_strlcpy(m->path,destination,96);state->row=m->path[0]?1:0;state->scroll=0;
    for(int i=0;i<HOST_REGISTRY_MEMORIES;i++)if(m->memory[i].used&&m->memory[i].scope==m->scope&&!strcmp(m->memory[i].path,m->path)){state->row=m->memory[i].row;state->scroll=m->memory[i].scroll;break;}
    Host_RegistryBuild(state->page);int count=m->count+Host_RegistryPrefix(state->page);if(state->row>=count)state->row=count?count-1:0;state->status=NULL;state->dirty=1;Host_EnsureControlsRowVisible(state);
}
static void Host_RegistryGo(HostOverlayState*state,const char*path){Host_RegistryGoTo(state,host_registry[Host_RegistrySlot(state->page)].scope,path);}
static int Host_RegistryBack(HostOverlayState*state){if(state->page!=HOST_PAGE_MODS&&state->page!=HOST_PAGE_CHEATS)return 0;HostRegistryModel*m=&host_registry[Host_RegistrySlot(state->page)];if(m->scope==HR_ROOT)return 0;char parent[96];SDL_strlcpy(parent,m->path,sizeof parent);char*last=strrchr(parent,'/');if(last){*last=0;Host_RegistryGo(state,parent);}else if(m->scope==HR_COMMUNITY&&parent[0])Host_RegistryGo(state,"");else Host_RegistryGoTo(state,HR_ROOT,"");return 1;}
static int Host_RegistryCount(int page){Host_RegistryBuild(page);return host_registry[Host_RegistrySlot(page)].count+Host_RegistryPrefix(page);}
static const char *Host_RegistryLabel(int page,int row){if(page==HOST_PAGE_MODS&&Host_RegistryPrefix(page)&&row<2)return row?"Character texture pack":"Background pack";HostRegistryEntry*e=Host_RegistryEntryAt(page,row);if(!e)return "Unavailable";if(e->row>=0){PeMenuRow*r=pe_menu_row(e->row);return r?r->label:"Unavailable";}if(e->row==-2)return "Back to parent page";if(e->row==-10)return "Personal / playtest cheats";if(e->row==-11)return "Community project options";if(e->row==-12)return "Gameplay mod activation";if(e->row==-13)return "Presentation asset packs";if(e->row==-20)return "HD Backgrounds";if(e->row==-21)return "Character textures status";if(e->row==-22)return "Choose character texture folder";if(e->row==-23)return "Reload character textures";if(e->row==-3)return "Game tools not initialized";const char*s=strrchr(e->path,'/');return s?s+1:e->path;}
static int Host_RegistryKind(int page,int row){if(page==HOST_PAGE_MODS&&Host_RegistryPrefix(page)&&row<2)return 1;HostRegistryEntry*e=Host_RegistryEntryAt(page,row);if(!e||e->row==-3||e->row==-21)return 0;if(e->row<0)return 1;PeMenuRow*r=pe_menu_row(e->row);return r&&pe_menu_row_adjustable(r)?2:r&&r->kind==PE_ROW_ACTION?1:0;}
static void Host_RegistryValue(int page,int row,char*out,unsigned n){
    if(page==HOST_PAGE_MODS&&Host_RegistryPrefix(page)&&row<2){snprintf(out,n,"%s",row?Psyz_GpuHdModelsStatus():Host_HdValue(1));return;}
    HostRegistryEntry*e=Host_RegistryEntryAt(page,row);if(!e){snprintf(out,n,"Unavailable");return;}if(e->row==-20){snprintf(out,n,"%s",Host_HdValue(1));return;}if(e->row==-21){snprintf(out,n,"%s",Psyz_GpuHdModelsStatus());return;}if(e->row==-22){snprintf(out,n,"Select USB folder");return;}if(e->row==-23){snprintf(out,n,"Validate and reload");return;}if(e->row<0){snprintf(out,n,"%s",e->row==-2?"Previous page":e->row==-3?"Start the game first":"Open page");return;}
    PeMenuRow*r=pe_menu_row(e->row);if(!r){out[0]=0;return;}pe_menu_row_value_text(r,out,n);
    if(!pe_menu_row_active(r)&&strlen(out)+11<n)strcat(out," (inactive)");
}
static const char *Host_RegistryHelp(int page,int row){
    if(page==HOST_PAGE_MODS&&Host_RegistryPrefix(page)&&row<2){if(!row)return Host_HdStatus();static char help[1400];snprintf(help,sizeof help,"Character image folder: %s. %s. Geometry correction is required for Modern; native HD backgrounds remain separate.",host_options.hd_model_directory,Psyz_GpuHdModelsStatus());return help;}
    HostRegistryEntry*e=Host_RegistryEntryAt(page,row);if(!e)return "This entry is not available.";if(e->row>=0){PeMenuRow*r=pe_menu_row(e->row);if(r&&!pe_menu_row_active(r)&&r->kind!=PE_ROW_INFO){static char inactive[1400];snprintf(inactive,sizeof inactive,"%s Enable it in Mods > Gameplay mod activation and restart. %s",r->kind==PE_ROW_ACTION||r->kind==PE_ROW_LIVE_INT||r->kind==PE_ROW_LIVE_CHOICE?"Unavailable this run.":"Saved choices apply when this mod runs.",r->description?r->description:"");return inactive;}return r&&r->description?r->description:"Registered game control.";}if(e->row==-2)return "Return one level; the previous selection and scroll position are remembered.";if(e->row==-10)return "Your personal Playtest assists and every registered nested control. Saved choices remain editable when inactive; live actions retain their original activation rules.";if(e->row==-11)return "The complete original community-project hierarchy, including its nested cheats, tuning and editors. No three-option replacement list.";if(e->row==-12)return "Start-up activation and dependencies for all gameplay mods. Now and next-launch status are separate; activation changes require restarting.";if(e->row==-13)return "Independent background and character image packs. These are optional; original assets remain the fallback.";if(e->row==-20)return Host_HdStatus();if(e->row==-21){static char pack[1400];snprintf(pack,sizeof pack,"Folder: %s. %s. Replaces supported character textures, not mesh geometry. Backgrounds remain independent.",host_options.hd_model_directory,Psyz_GpuHdModelsStatus());return pack;}if(e->row==-22)return "Choose a prepared native character-texture folder on USB. Every image is checked before switching. No multi-GB emulator download, no pack files written; backgrounds and the HD models switch stay independent.";if(e->row==-23)return "Recheck the current character pack and reload its index after GPU reads finish. Previous pack stays on failure. Successful reload invalidates older checkpoints; it does not enable HD models or prove a visible match.";if(e->row==-3)return "The original mod registry is populated when the game initializes. No controls have been replaced by a preset list.";const PeMenuPageInfo*info=pe_menu_page_info(e->path);return info&&info->description?info->description:"Open all registered controls on this page. Existing values, activation rules and saved settings are retained.";
}
static int Host_RegistryAction(int page,int row,int direction,int activate){
    if(page==HOST_PAGE_MODS&&Host_RegistryPrefix(page)&&row<2){if(activate){if(!row)Host_HdAction(1);else Host_RegistryGoTo(&s_overlayState,HR_ASSETS,"");}return 0;}
    HostRegistryEntry*e=Host_RegistryEntryAt(page,row);if(!e)return 0;if(e->row<0){if(!activate)return 0;if(e->row==-2)Host_RegistryBack(&s_overlayState);else if(e->row==-10)Host_RegistryGoTo(&s_overlayState,HR_PERSONAL,"Playtest assists");else if(e->row==-11)Host_RegistryGoTo(&s_overlayState,HR_COMMUNITY,"");else if(e->row==-12)Host_RegistryGoTo(&s_overlayState,HR_ACTIVATION,PE_MENU_MANAGE);else if(e->row==-13)Host_RegistryGoTo(&s_overlayState,HR_ASSETS,"");else if(e->row==-20)Host_HdAction(1);else if(e->row==-22)Host_CharacterPackAction(1);else if(e->row==-23)Host_CharacterPackAction(0);else if(e->row==-1){char path[96];SDL_strlcpy(path,e->path,sizeof path);Host_RegistryGo(&s_overlayState,path);}return 0;}
    PeMenuRow*r=pe_menu_row(e->row);if(!r||r->kind==PE_ROW_INFO||(!activate&&!pe_menu_row_adjustable(r)))return 0;if(activate)pe_menu_row_accept(r);else if(pe_menu_row_adjustable(r))pe_menu_row_step(r,direction);pe_mod_manage_refresh();pe_menu_save_settings();SDL_Log("[Native registry] page=%s control=%s active=%d kind=%d",r->page,r->label,pe_menu_row_active(r),r->kind);return 0;
}
#endif
