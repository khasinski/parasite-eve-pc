/* SPDX-License-Identifier: MIT. Native visual presets do not own gameplay preferences. */
#ifndef PE_NATIVE_EXPERIENCE_H
#define PE_NATIVE_EXPERIENCE_H
#define VISUAL(key,field,min,max,classic) {key,&host_options.field,min,max,classic}
typedef struct {const char*key;void*value;unsigned min,max,classic;} HostVisualField;
static const HostVisualField host_visuals[]={
 VISUAL("interpolation",frame_interpolation,0,1,0),VISUAL("scale",scale,1,8,1),VISUAL("aspect",aspect,0,1,0),VISUAL("dither",dither,0,1,0),
 VISUAL("antialiasing",antialiasing,0,1,0),VISUAL("model_filtering",model_filtering,0,1,0),
 VISUAL("dynamic_lighting",dynamic_lighting,0,1,0),VISUAL("effect_precision",effect_geometry_correction,0,1,0),
 VISUAL("widescreen",widescreen,0,1,0),VISUAL("backgrounds",hd_backgrounds,0,1,0),VISUAL("models",hd_models,0,1,0),
 VISUAL("fill",gameplay_fill,0,1,0),VISUAL("fsr",fsr1,0,1,0),VISUAL("sharpness",fsr1_sharpness,0,2,1),
 VISUAL("model_fill",model_fill_lighting,0,2,0),VISUAL("movie_mode",movie_presentation,0,1,1),
 VISUAL("movie_cleanup",movie_cleanup,0,3,0),VISUAL("particles",battle_debris_3d,0,2,0),
 VISUAL("particle_lights",particle_lighting,0,1,0),VISUAL("dialogue",dialogue_text,0,1,0),VISUAL("menu_font",triangle_menu_font,0,1,0),VISUAL("player_shadow",player_shadow,0,1,0)
};
#undef VISUAL
#define HOST_VISUAL_COUNT (sizeof(host_visuals)/sizeof(host_visuals[0]))
_Static_assert(HOST_VISUAL_COUNT<32,"Native visual profile uses a bounded32-bit presence mask");
static unsigned host_modern_values[HOST_VISUAL_COUNT];
static unsigned host_modern_seen,host_presentation=1,host_movie_policy;
static void Host_VisualSafety(void){host_options.sprite_filtering=0;host_options.geometry_correction=1;host_options.movie_blur=host_options.movie_presentation==1;}
static void Host_ModernRemember(void){for(unsigned i=0;i<HOST_VISUAL_COUNT;i++)memcpy(&host_modern_values[i],host_visuals[i].value,4);host_modern_seen=(1u<<HOST_VISUAL_COUNT)-1;}
static int GetPresentation(void){return (int)host_presentation;}
static void SetPresentation(int mode){
 if((unsigned)mode==host_presentation)return;
 if(!mode){Host_ModernRemember();for(unsigned i=0;i<HOST_VISUAL_COUNT;i++)memcpy(host_visuals[i].value,&host_visuals[i].classic,4);}
 else for(unsigned i=0;i<HOST_VISUAL_COUNT;i++)memcpy(host_visuals[i].value,&host_modern_values[i],4);
 host_presentation=!!mode;Host_VisualSafety();
 SDL_Log("[Presentation] mode=%s modern_saved=1 geometry_precision=correctness sprite_smoothing=disabled",mode?"Modern":"Classic");
}
static int Host_VisualParse(const char*section,const char*key,int value){
 if(!strcmp(section,"experience")){
  if(!strcmp(key,"presentation")){if(value>1)return 0;host_presentation=value;}
  else if(!strcmp(key,"movie_policy")){if(value>1)return 0;host_movie_policy=value;}
  return 1;
 }
 if(strcmp(section,"modern_visuals"))return -1;
 for(unsigned i=0;i<HOST_VISUAL_COUNT;i++)if(!strcmp(key,host_visuals[i].key)){
  if((unsigned)value<host_visuals[i].min||(unsigned)value>host_visuals[i].max)return 0;
  host_modern_values[i]=(unsigned)value;host_modern_seen|=1u<<i;return 1;
 }return 1;
}
static void Host_VisualLoaded(void){
 if(!host_movie_policy){host_options.movie_presentation=1;host_movie_policy=1;SDL_Log("[Movie policy] migrated=1 default=original-proportions-blurred-sides");}
 if(host_options.movie_presentation>1)host_options.movie_presentation=1;
 Host_VisualSafety();
 /* Missing snapshot fields inherit the loaded user's choices, never unrelated defaults. */
 for(unsigned i=0;i<HOST_VISUAL_COUNT;i++)if(!(host_modern_seen&(1u<<i)))memcpy(&host_modern_values[i],host_visuals[i].value,4);
 host_modern_seen=(1u<<HOST_VISUAL_COUNT)-1;
}
static void Host_VisualWrite(FILE*file){
 Host_VisualSafety();if(host_presentation)Host_ModernRemember();
 fprintf(file,"[experience]\npresentation = %u\nmovie_policy = 1\n\n[modern_visuals]\n",host_presentation);
 for(unsigned i=0;i<HOST_VISUAL_COUNT;i++)fprintf(file,"%s = %u\n",host_visuals[i].key,host_modern_values[i]);
 fputc('\n',file);
}
#endif
