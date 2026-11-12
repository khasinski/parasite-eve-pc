#include "pe_bp_settings.h"
#include "pe_bp_api.h"
#include "pe_mod_menu.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
struct Row{const char *label;PeMenuGet get;PeMenuSet set;PeMenuAction action;PeMenuText text;int *value;};
static struct Row rows[32];static int nrows,profile=BP_FEATURE_ALL,*bound,free_field=1,module_on=1,answer=1,questions,saves;static BpSnapshot game;
void pe_menu_begin(const char *p){(void)p;}
int pe_menu_bind_setting(const char *k,int *v,int min,int max){(void)k;assert(profile>=min&&profile<=max);bound=v;*v=profile;return 0;}
void pe_menu_save_settings(void){profile=*bound;++saves;}
int pe_menu_confirm(const char *q,const char *help){assert(strstr(q,"Apply")&&help[0]);++questions;return answer;}
int pe_free_field(void){return free_field;}
int pe_mod_active(const char *m){(void)m;return module_on;}
int pe_mod_data_available(const char *m){(void)m;return module_on;}
void pe_mod_log(const char *m,const char *f,...){(void)m;(void)f;}
void pe_toast(const char *s){(void)s;}
static int snap(BpSnapshot *s){*s=game;return 1;}
static const PeBpApi api={sizeof(PeBpApi),snap,NULL,NULL};
const void *pe_service_get(const char *n,int v){(void)n;(void)v;return &api;}
static struct Row *add(const char *label){assert(nrows<32);struct Row *r=&rows[nrows++];memset(r,0,sizeof *r);r->label=label;return r;}
int pe_menu_register_info(const char *p,const char *l,const char *d,PeMenuText f){(void)p;(void)d;add(l)->text=f;return 0;}
int pe_menu_register_action(const char *p,const char *l,const char *d,PeMenuAction f){(void)p;(void)d;add(l)->action=f;return 0;}
int pe_menu_register_choice(const char *p,const char *l,const char *d,int *v,const char *const *names,unsigned c){(void)p;(void)d;(void)names;(void)c;add(l)->value=v;return 0;}
int pe_menu_register_live_choice(const char *p,const char *l,const char *d,const char *const *names,unsigned c,PeMenuGet g,PeMenuSet s){(void)p;(void)d;(void)names;(void)c;struct Row *r=add(l);r->get=g;r->set=s;return 0;}
int pe_menu_register_live_dynamic_choice(const char *p,const char *l,const char *d,unsigned c,PeMenuChoiceCount count,PeMenuChoiceName name,PeMenuGet g,PeMenuSet s){(void)count;(void)name;return pe_menu_register_live_choice(p,l,d,NULL,c,g,s);}
static struct Row *row(const char *name){for(int i=0;i<nrows;i++)if(!strcmp(rows[i].label,name))return &rows[i];assert(0);return NULL;}
int main(void){
 game.loaded=1;game.bp=450;game.junk=900;game.rec.defense[0]=3;game.rec.mastery[0]=2;game.rec.full_resets=2;game.rec.active_points=20;game.rec.active_steps=2;
 BpSnapshot original=game;pe_bp_menu_register();assert(!strcmp(rows[0].label,"Current BP preset")&&!strcmp(rows[1].label,"Choose BP preset"));
 struct Row *pick=row("Choose BP preset"),*apply=row("Apply BP preset");
 assert(pick->set(BP_PRESET_VANILLA_QOL)&&pe_bp_settings_flags()==BP_FEATURE_ALL);
 answer=0;apply->action();assert(pe_bp_settings_flags()==BP_FEATURE_ALL&&!saves&&questions==1);
 answer=1;apply->action();assert(pe_bp_settings_flags()==BP_FEATURE_RESPEC&&profile==BP_FEATURE_RESPEC&&saves==1);
 int q=questions;apply->action();assert(questions==q&&saves==1);
 assert(pick->set(BP_PRESET_ENHANCED));free_field=0;apply->action();assert(pe_bp_settings_flags()==BP_FEATURE_RESPEC&&questions==q);
 free_field=1;module_on=0;apply->action();assert(pe_bp_settings_flags()==BP_FEATURE_RESPEC&&questions==q);module_on=1;
 answer=-1;apply->action();assert(pe_bp_settings_flags()==BP_FEATURE_RESPEC);answer=1;apply->action();assert(pe_bp_settings_flags()==BP_FEATURE_ALL);
 assert(row("Resistance")->set(0));assert(pick->get()==BP_PRESET_CUSTOM);assert(pe_bp_settings_flags()==BP_FEATURE_ALL);apply->action();assert(!(pe_bp_settings_flags()&BP_FEATURE_RESISTANCE));
 int match=pick->get();*row("Playstyle guide")->value=3;assert(pick->get()==match);
 for(int i=0;i<20;i++){assert(pick->set(i%2?BP_PRESET_ENHANCED:BP_PRESET_VANILLA_QOL));apply->action();assert(!memcmp(&game,&original,sizeof game));}
 unsigned before=pe_bp_settings_flags();nrows=0;pe_bp_menu_register();assert(pe_bp_settings_flags()==before&&pick->get()>=0);
 assert(!memcmp(&game,&original,sizeof game));puts("PASS: F1 presets, draft isolation, confirmation, busy guards, persistence, guides and retained investments");return 0;
}
