/* SPDX-License-Identifier: MIT -- existing CPU font surface / native GPU overlay. */
static void InstallText(OverlaySurface*s,float x,float y,float em,const char*text,Color color,TextAlign align,float max){
    char line[256];SDL_strlcpy(line,text?text:"",sizeof(line));size_t n=strlen(line);
    if(TextWidth(line,em)>max){while(n&&TextWidth(line,em)+TextWidth("...",em)>max)line[--n]=0;strcat(line,"...");}
    DrawText(s,x,y,em,line,color,align);
}
/* Installer panel is600logicalpixels, independent of840px Settings. */
static void InstallHelp(const Painter*p,float top,const char*text){
 const float width=536*p->s,em=13*p->s;char line[256];
 for(int row=0;row<2&&text&&*text;row++){size_t n=0,last_space=0;
  while(text[n]&&n<sizeof(line)-4){line[n]=text[n];line[n+1]=0;if(TextWidth(line,em)>width)break;if(text[n]==' ')last_space=n;n++;}
  if(text[n]&&!row&&last_space)n=last_space;line[n]=0;text+=n;while(*text==' ')text++;
  if(row==1&&*text){while(n&&TextWidth(line,em)+TextWidth("...",em)>width)line[--n]=0;strcat(line,"...");}
  DrawText(p->surface,32*p->s,top+(23+18*row)*p->s,em,line,kMuted,ALIGN_LEFT);
 }
}
static const char*InstallTail(const char*path){const char*p=strrchr(path,'/');return p&&p[1]?p+1:path;}
static void OverlayPaintInstaller(OverlaySurface*surface,const OverlayLayout*layout){
    const float z=layout->scale;Painter p={surface,&s_overlayState,z};
    memset(surface->pixels,0,(size_t)surface->w*surface->h*4);PaintFrame(&p,600*z,650*z);
    DrawText(surface,32*z,43*z,26*z,"Parasite Eve setup",kText,ALIGN_LEFT);
    DrawText(surface,32*z,75*z,13*z,"Your own game files. Existing settings and saves stay intact.",kMuted,ALIGN_LEFT);
    if(s_install.phase==PE_IV_FORM){
        const char*labels[]={"Disc 1","Disc 2","Disc storage","Copy destination","HD backgrounds","Native pack folder","HD character textures","Character subset folder","Install and play"};
        const char*values[]={InstallTail(s_install.disc[0]),InstallTail(s_install.disc[1]),s_install.copy?"Copy to USB":"Use USB files in place",InstallTail(s_install.destination),s_install.hd_on?"Verify local pack":"Original",InstallTail(s_install.hd),s_install.characters_on?"Verify local subset":"Original",InstallTail(s_install.characters),"Cross to begin"};
        for(int i=0;i<9;i++){float y=(116+i*40)*z;if(i==s_install.row)FillRoundRect(surface,24*z,y-23*z,552*z,36*z,6*z,WithAlpha(kAccent,.19f));
            DrawText(surface,32*z,y,16*z,labels[i],i==s_install.row?kAccent:kText,ALIGN_LEFT);
            InstallText(surface,568*z,y,14*z,values[i],kText,ALIGN_RIGHT,270*z);}
        InstallHelp(&p,490*z,s_install.characters_found?"A prepared character subset was found. Choose HD character textures to verify it, or keep Original. Backgrounds are independent; no downloads.":"USB in-place avoids copying discs. Optional character images use a small prepared native subset, not a multi-GB archive. Backgrounds are independent.");
        DrawText(surface,32*z,613*z,12*z,"D-pad / stick select   Cross choose   Left/Right change   Circle cancel",kMuted,ALIGN_LEFT);
    }else if(s_install.phase==PE_IV_BROWSER){
        InstallText(surface,32*z,104*z,13*z,s_install.directory,kMuted,ALIGN_LEFT,536*z);
        for(int slot=0;slot<9;slot++){int i=s_install.scroll+slot;if(i>=s_install.count)break;float y=(141+slot*43)*z;
            if(i==s_install.cursor)FillRoundRect(surface,24*z,y-24*z,552*z,39*z,5*z,WithAlpha(kAccent,.19f));
            char name[256];snprintf(name,sizeof(name),"%s%s",s_install.entries[i].directory?"[folder] ":"",s_install.entries[i].name);
            InstallText(surface,32*z,y,16*z,name,i==s_install.cursor?kAccent:kText,ALIGN_LEFT,530*z);}
        if(!s_install.count)DrawText(surface,32*z,153*z,15*z,s_install.enumeration_failed?"Cannot read this folder.":"No folders or BIN files here.",kMuted,ALIGN_LEFT);
        InstallText(surface,32*z,553*z,12*z,s_install.truncated==2?"Large folder: first 4096 entries scanned. Folder selection still works.":s_install.truncated?"First 128 entries shown; use a smaller folder.":"",kMuted,ALIGN_LEFT,536*z);
        DrawText(surface,32*z,588*z,12*z,"Cross open/select   Circle parent/back   L1/R1 USB drive",kMuted,ALIGN_LEFT);
        if(s_install.target>=2)DrawText(surface,32*z,613*z,12*z,"Triangle: use this folder",kAccent,ALIGN_LEFT);
    }else{
        const char*title=s_install.phase==PE_IV_PROGRESS?"Preparing your files":s_install.phase==PE_IV_SUCCESS?"Ready to play":"Setup could not finish";
        DrawText(surface,32*z,155*z,22*z,title,kText,ALIGN_LEFT);InstallHelp(&p,190*z,s_install.status);
        if(s_install.phase==PE_IV_PROGRESS){float ratio=s_install.total?(float)((double)s_install.done/(double)s_install.total):0;if(ratio<0)ratio=0;if(ratio>1)ratio=1;
            FillRoundRect(surface,32*z,285*z,536*z,14*z,7*z,WithAlpha(kText,.12f));FillRoundRect(surface,32*z,285*z,536*z*ratio,14*z,7*z,kAccent);
            char text[96];snprintf(text,sizeof(text),"%.0f%%  (%llu / %llu)",(double)(ratio*100),(unsigned long long)s_install.done,(unsigned long long)s_install.total);DrawText(surface,32*z,331*z,14*z,text,kMuted,ALIGN_LEFT);
            DrawText(surface,32*z,590*z,14*z,"Circle cancels safely",kMuted,ALIGN_LEFT);
        }else DrawText(surface,32*z,590*z,14*z,s_install.phase==PE_IV_SUCCESS?"Cross play   Circle leave":"Cross review choices   Circle leave",kAccent,ALIGN_LEFT);
    }
    unsigned char*solid=surface->pixels+(size_t)(surface->h-1)*surface->w*4;solid[3]=255;
}
