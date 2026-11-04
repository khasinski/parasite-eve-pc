/* SPDX-License-Identifier: MIT
 * First-run executor using public SDL3 file APIs. Existing configs, assets and
 * saves are never overwritten. Completion publication is atomic rename of an
 * installer-owned receipt; config publication is exclusive creation, not a
 * falsely advertised atomic no-replace rename unsupported by public SDL. */
#include "pe_bootstrap.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

static int fail(PeInstallReport *r,const char *why){SDL_strlcpy(r->error,why,sizeof(r->error));return PE_INSTALL_FAILED;}
static int path_ok(const char *p){
    if(!p||p[0]!='/'||strlen(p)>=PE_BOOT_PATH-80)return 0;
    for(const unsigned char *s=(const unsigned char *)p;*s;s++)
        if(*s<32||*s==127||*s=='"'||*s=='\\'||*s==':')return 0;
    for(const char *s=p;*s;){while(*s=='/')s++;const char *e=strchr(s,'/');size_t n=e?(size_t)(e-s):strlen(s);
        if((n==1&&s[0]=='.')||(n==2&&s[0]=='.'&&s[1]=='.'))return 0;s+=n;}
    return 1;
}
static int usb_path(const char *p){return path_ok(p)&&!strncmp(p,"/mnt/usb",8)&&p[8]>='0'&&p[8]<='7'&&(p[9]=='/'||!p[9]);}
static int join(char *out,const char *root,const char *name){int n=snprintf(out,PE_BOOT_PATH,"%s%s%s",root,root[strlen(root)-1]=='/'?"":"/",name);return n>0&&n<PE_BOOT_PATH;}
static int info(const char *p,SDL_PathInfo *i){memset(i,0,sizeof(*i));return SDL_GetPathInfo(p,i);}
static int mkdir_one(const char *p){SDL_PathInfo i;if(info(p,&i))return i.type==SDL_PATHTYPE_DIRECTORY;return SDL_CreateDirectory(p);}
static int progress(const PeInstallCallbacks *c,const char *phase,uint64_t done,uint64_t total){return !c->progress||c->progress(phase,done,total,c->user);}
static int write_new(const char *p,const char *text,PeInstallReport *r){
    SDL_IOStream *f=SDL_IOFromFile(p,"wbx");if(!f)return fail(r,"Exclusive file creation refused; nothing existing was replaced.");
    size_t n=strlen(text);int ok=SDL_WriteIO(f,text,n)==n&&SDL_FlushIO(f);if(!SDL_CloseIO(f))ok=0;
    return ok?1:fail(r,"New file write/flush/close failed; no completion receipt published.");
}
static int same_file(const char *a,const char *b,const PeInstallCallbacks *c,PeInstallReport *r){
    SDL_IOStream *x=SDL_IOFromFile(a,"rb"),*y=SDL_IOFromFile(b,"rb");unsigned char aa[32768],bb[32768];uint64_t at=0;
    if(!x||!y){if(x)SDL_CloseIO(x);if(y)SDL_CloseIO(y);return fail(r,"Cannot verify imported disc bytes.");}
    Sint64 size=SDL_GetIOSize(x);int ok=size>=0&&size==SDL_GetIOSize(y);
    while(ok&&at<(uint64_t)size){size_t n=(uint64_t)size-at<sizeof(aa)?(size_t)((uint64_t)size-at):sizeof(aa);
        if(!progress(c,"Verifying imported disc",at,(uint64_t)size)){ok=-2;break;}
        if(SDL_ReadIO(x,aa,n)!=n||SDL_ReadIO(y,bb,n)!=n||memcmp(aa,bb,n)){ok=0;break;}at+=n;
    }
    if(!SDL_CloseIO(x)&&ok>0)ok=0;if(!SDL_CloseIO(y)&&ok>0)ok=0;
    if(ok==-2)return PE_INSTALL_CANCELLED;return ok?1:fail(r,"Imported disc differs from the selected source or could not be read.");
}
static int copy_disc(const char *src,const char *dst,unsigned id,const PeInstallCallbacks *c,PeInstallReport *r){
    SDL_PathInfo pi;if(info(dst,&pi)){
        if(pi.type!=SDL_PATHTYPE_FILE)return fail(r,"Disc destination already exists and is not a file.");
        return same_file(src,dst,c,r); /* Never overwrite an unrelated existing disc. */
    }
    SDL_IOStream *in=SDL_IOFromFile(src,"rb");if(!in)return fail(r,"Cannot open selected disc.");
    Sint64 size=SDL_GetIOSize(in);if(size<=0){SDL_CloseIO(in);return fail(r,"Disc size unavailable.");}
    SDL_IOStream *out=SDL_IOFromFile(dst,"wbx");if(!out){SDL_CloseIO(in);return fail(r,"Disc destination exclusive creation refused.");}
    unsigned char buf[65536];uint64_t at=0;int ok=1;
    while(at<(uint64_t)size){size_t n=(uint64_t)size-at<sizeof(buf)?(size_t)((uint64_t)size-at):sizeof(buf);
        if(!progress(c,"Importing owner disc",at,(uint64_t)size)){ok=-2;break;}
        if(SDL_ReadIO(in,buf,n)!=n||SDL_WriteIO(out,buf,n)!=n){ok=0;break;}at+=n;
    }
    if(!SDL_CloseIO(in)&&ok>0)ok=0;if(ok>0&&!SDL_FlushIO(out))ok=0;if(!SDL_CloseIO(out)&&ok>0)ok=0;
    if(ok<=0){SDL_RemovePath(dst);return ok==-2?PE_INSTALL_CANCELLED:fail(r,"Disc import failed; newly created partial output removed.");}
    /* Verify exact bytes, then the actual game reader, not filename/size alone. */
    ok=same_file(src,dst,c,r);if(ok!=1)return ok;
    if(!c->validate_disc(dst,id,c->user))return fail(r,"Imported disc failed native game validation.");
    return 1;
}

PeInstallResult pe_bootstrap_install(const PeInstallRequest *q,const PeInstallCallbacks *c,PeInstallReport *r){
    if(!r)return PE_INSTALL_FAILED;memset(r,0,sizeof(*r));r->result=PE_INSTALL_FAILED;
    if(!q||!c||!c->validate_disc||!path_ok(q->data_root)||
       q->mode<PE_INSTALL_USE_USB||q->mode>PE_INSTALL_COPY_DISCS)
        return fail(r,"Invalid first-run request or native preferences root.");
    join(r->config,q->data_root,"parasite-eve.toml");join(r->receipt,q->data_root,"install-complete-v1.txt");
    SDL_PathInfo pi;char pending[PE_BOOT_PATH];join(pending,q->data_root,"install-config-pending-v1.txt");
    if(info(pending,&pi)){r->interrupted_config_possible=1;return fail(r,"Interrupted first-install config needs owner review; existing files were preserved.");}
    if(info(r->config,&pi)){
        if(pi.type!=SDL_PATHTYPE_FILE)return fail(r,"Configuration destination is not a regular file.");
        SDL_IOStream*existing=SDL_IOFromFile(r->config,"rb");Sint64 bytes=existing?SDL_GetIOSize(existing):-1;if(existing)SDL_CloseIO(existing);
        if(bytes<=0){r->interrupted_config_possible=1;return fail(r,"Existing configuration is empty or unreadable. Preserve and review it before setup; it was not overwritten.");}
        /* Existing settings belong to the owner. Normal game startup validates
         * its selected paths. Never replace them with new first-install inputs. */
        r->result=PE_INSTALL_EXISTING;return r->result;
    }
    if(info(r->receipt,&pi))return fail(r,"Completion receipt exists without configuration; owner review required.");
    if(strcmp(q->data_root,"/data/parasite-eve")&&strncmp(q->data_root,"/data/parasite-eve/",sizeof("/data/parasite-eve/")-1)&&!usb_path(q->data_root))return fail(r,"New preferences must be inside the game data root or a selected USB folder.");
    if(!usb_path(q->disc[0])||!usb_path(q->disc[1])||!strcmp(q->disc[0],q->disc[1]))return fail(r,"Choose two separate owner BIN files on USB.");
    for(unsigned i=0;i<2;i++){
        const char *ext=strrchr(q->disc[i],'.');if(!ext||SDL_strcasecmp(ext,".bin"))return fail(r,"Select raw BIN files; CUE imports require a separate multi-file importer.");
        if(!progress(c,"Validating owner discs",i,2)){r->result=PE_INSTALL_CANCELLED;return r->result;}
        if(!c->validate_disc(q->disc[i],i+1,c->user))return fail(r,"Selected disc is not the expected supported raw game disc.");
    }
    if(q->native_hd&&*q->native_hd&&(!usb_path(q->native_hd)||!c->validate_native_hd||!c->validate_native_hd(q->native_hd,c->user)))return fail(r,"Native HD pack verification failed or was cancelled. Choose the supplied complete native pack, or Original.");
    if(q->native_characters&&*q->native_characters&&(!usb_path(q->native_characters)||!c->validate_characters||!c->validate_characters(q->native_characters,c->user)))return fail(r,"Character texture subset verification failed or was cancelled. Choose the prepared native subset, or Original.");
    if(q->mode==PE_INSTALL_COPY_DISCS&&(!usb_path(q->copy_root)||strstr(q->copy_root,"/memory-cards")))return fail(r,"Select a USB asset destination, not a memory-card directory.");
    if(!mkdir_one(q->data_root))return fail(r,"Cannot create native data directory.");
    char lock[PE_BOOT_PATH];join(lock,q->data_root,"install-in-progress.lock");
    SDL_IOStream *lf=SDL_IOFromFile(lock,"wbx");if(!lf)return fail(r,"Installer lock exists or cannot be created. Another run or interrupted setup needs owner review.");
    SDL_CloseIO(lf);int result=PE_INSTALL_FAILED,own_tmp=0;char tmp[PE_BOOT_PATH];tmp[0]=0;
    char folders[4][PE_BOOT_PATH];const char *sub[]={"logs","memory-cards","memory-cards/bu00","memory-cards/bu10"};
    for(unsigned i=0;i<4;i++){join(folders[i],q->data_root,sub[i]);if(!mkdir_one(folders[i])){fail(r,"Required data/log/card directory unavailable.");goto done;}}
    char selected[2][PE_BOOT_PATH];for(unsigned i=0;i<2;i++)SDL_strlcpy(selected[i],q->disc[i],PE_BOOT_PATH);
    if(q->mode==PE_INSTALL_COPY_DISCS){
        if(!mkdir_one(q->copy_root)){fail(r,"USB asset directory unavailable; choose an existing parent.");goto done;}
        for(unsigned i=0;i<2;i++){char name[24];snprintf(name,sizeof(name),"disc%u.bin",i+1);join(selected[i],q->copy_root,name);
            int rc=copy_disc(q->disc[i],selected[i],i+1,c,r);if(rc!=1){result=rc;goto done;}}
    }
    if(!progress(c,"Preparing first-install configuration",2,2)){result=PE_INSTALL_CANCELLED;goto done;}
    char text[PE_BOOT_PATH*5+1024];int n=snprintf(text,sizeof(text),"# Original first-run configuration; existing preferences are never replaced.\n[graphics]\ninternal_resolution = 2\n[discs]\ndisc1 = \"%s\"\ndisc2 = \"%s\"\n[mods]\nhd_backgrounds = false\n",selected[0],selected[1]);
    if(n<0||(size_t)n>=sizeof(text)){fail(r,"Configuration paths exceed bounded template.");goto done;}
    if(q->native_hd&&*q->native_hd){n=snprintf(text,sizeof(text),"# Verified owner-supplied USB files; original settings never replaced.\n[graphics]\ninternal_resolution = 2\n[discs]\ndisc1 = \"%s\"\ndisc2 = \"%s\"\n[mods]\nhd_backgrounds = true\nhd_field_directory = \"%s\"\nhd_title_directory = \"%s\"\n",selected[0],selected[1],q->native_hd,q->native_hd);if(n<0||(size_t)n>=sizeof(text)){fail(r,"HD configuration paths exceed template.");goto done;}}
    {size_t used=strlen(text);int extra=q->native_characters&&*q->native_characters?snprintf(text+used,sizeof(text)-used,"hd_models = true\nhd_model_directory = \"%s\"\n",q->native_characters):snprintf(text+used,sizeof(text)-used,"hd_models = false\n");if(extra<0||(size_t)extra>=sizeof(text)-used){fail(r,"Character texture configuration path exceeds template.");goto done;}}
    if(write_new(pending,"PE-CONFIG-PENDING-1\nReview an interrupted configuration; never overwrite existing preferences.\n",r)!=1)goto done;
    /* Exclusive create closes the external-writer race: no rename can replace
     * an owner's concurrently created config. Incomplete write is not retried. */
    if(write_new(r->config,text,r)!=1){r->interrupted_config_possible=1;goto done;}r->config_created=1;
    join(tmp,q->data_root,"install-complete-v1.tmp");
    char receipt[256];snprintf(receipt,sizeof(receipt),"PE-INSTALL-1\nconfig=parasite-eve.toml\nvalidated_discs=1,2\nmode=%s\nassets=owner-supplied\n",q->mode==PE_INSTALL_COPY_DISCS?"copied-and-byte-verified":"USB-in-place");
    if(write_new(tmp,receipt,r)!=1)goto done;own_tmp=1;
    /* Receipt belongs only to this installer; never publishes before required
     * files and config were successfully flushed/closed. SDL gives atomic
     * same-directory rename, not a promise of power-loss durable fsync. */
    if(info(r->receipt,&pi)){fail(r,"Existing install receipt requires owner review; it was not replaced.");goto done;}
    if(!SDL_RenamePath(tmp,r->receipt)){fail(r,"Completion receipt publication failed.");goto done;}own_tmp=0;
    if(!SDL_RemovePath(pending)){fail(r,"Install receipt committed but pending marker could not be cleared; owner review required.");goto done;}
    result=PE_INSTALL_READY;
done:
    if(own_tmp)SDL_RemovePath(tmp);SDL_RemovePath(lock);r->result=(PeInstallResult)result;return r->result;
}
