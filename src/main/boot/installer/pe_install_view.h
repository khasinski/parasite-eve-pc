/* SPDX-License-Identifier: MIT -- UI model shared with existing overlay painter. */
#ifndef PE_INSTALL_VIEW_H
#define PE_INSTALL_VIEW_H
enum{PE_IV_FORM,PE_IV_BROWSER,PE_IV_PROGRESS,PE_IV_ERROR,PE_IV_SUCCESS};
typedef struct{char name[256];int directory;}PeInstallEntry;
static struct{
    int active,phase,row,usb,target,cursor,count,scroll,copy,hd_on,characters_on,characters_found,accept,cancel,axis_x,axis_y;
    int enumeration_failed,truncated,scanned,browser_selected;
    char disc[2][4096],destination[4096],hd[4096],characters[4096],directory[4096],status[256];
    PeInstallEntry entries[128];uint64_t done,total;
}s_install;
static int Host_InstallerEvent(const SDL_Event *);
#endif
