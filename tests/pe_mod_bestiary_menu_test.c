/* Production Bestiary and host widgets; drawing, creatures and models are simulated. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../mods/host/pe_main_menu.c"
#include "../mods/pe_collection/pe_bestiary_menu.c"
static size_t frame_bytes, max_frame_bytes;
_Alignas(RenderTexturedQuad) static unsigned char packet_arena[0x4000];
static RenderNativeTag ordering;
static int force_large_icon, effect_sprites;

int g_InvItemUsableFlag;
ItemDataRecord *g_MenuActionItemData;
int pe_in_battle(void) { return 0; }
const char *pe_mod_current(void) { return NULL; }
int pe_mod_active(const char *m) { (void)m; return 1; }
int g_MenuItemContextFlag, g_MenuBattleStatusOverlayActive;
MenuWidgetNode *g_MenuWidgetActiveListHead, *g_MenuActiveWidget;
u8 g_MenuItemLabelBuffer[128];
int g_MenuItemPrimListHandles[2], g_MenuActionTextBase;
MenuConfirmCallback g_MenuConfirmCallback;
void Menu_DrawItemLabel(MenuWidgetNode *n) { (void)n; }
void Menu_DrawActionOptionList(MenuWidgetNode *n) { (void)n; }
int Menu_ConfirmDialogHandler(MenuWidgetNode *n, unsigned int f) { (void)n;(void)f;return 1; }
static int font_advance=7;
int Draw_MeasureTextWidth(u8 *t) { int n=0;while(*t!=0xFF){t+=Pe_ExtendedGlyph(t,NULL)?4:1;++n;}return n*font_advance; }
unsigned char *g_DrawPacketCursor, *g_DrawPacketArenaBase;
unsigned int g_DrawPrimColor, g_DrawColorShaded, *g_DrawOrderingTableEntry;
int g_DrawTextDimmed, g_TextCursorX, g_TextCursorY, g_DrawTextPosX, g_MenuListRowHeight;
DrawGlyphDescriptor *Draw_LookupGlyphDescriptor(int i) {
    static DrawGlyphDescriptor d={.width=25,.height=12},large={.width=200,.height=40};
    return force_large_icon&&i<0x80?&large:&d;
}
void Draw_OffsetCursor(int x,int y) { g_TextCursorX+=x;g_TextCursorY+=y; }
static int stat_sprites;
void Draw_AllocSprite(int i) {
    ++stat_sprites;if(i>=0x23&&i<=0x46)++effect_sprites;
    frame_bytes+=sizeof(RenderSpritePacket)+sizeof(RenderDrawModePacket);
}

static MenuWidgetNode pool[24], *current;
static unsigned char used[24];
static MenuWidgetNode *allocate(MenuWidgetNode *parent,MenuWidgetNode *owner) {
    for(int i=0;i<24;++i)if(!used[i]) {
        used[i]=1;MenuWidgetNode *n=&pool[i];memset(n,0,sizeof *n);n->parent=parent;
        n->next=g_MenuWidgetActiveListHead;g_MenuWidgetActiveListHead=n;
        if(owner){int k;for(k=0;k<4;++k)if(!owner->children[k]){owner->children[k]=n;break;}assert(k<4);}
        return n;
    }
    assert(!"native widget pool exhausted");return NULL;
}
MenuWidgetNode *MenuWidget_CreateSimpleNode(int mode,MenuWidgetNode *parent,MenuWidgetNode *owner,int x) {
    MenuWidgetNode *n=allocate(parent,owner);n->mode=1;n->selected_base=mode;n->cursor_x=x;n->visible_rows=20;return n;
}
MenuWidgetNode *MenuWidget_CreateNode(int mode,MenuWidgetNode *parent,MenuWidgetNode *owner) {
    MenuWidgetNode *n=allocate(parent,owner);n->mode=2;n->selected_base=mode;n->x=n->y=6;
    n->grid_width=n->x_limit=1;n->visible_rows=n->visible_rows_mirror=8;n->disabled=12;n->draw_state=68;n->layout_flags=0x10;return n;
}
void MenuWidget_DestroyNode(MenuWidgetNode *n) {
    for(int i=0;i<4;++i)if(n->children[i])MenuWidget_DestroyNode(n->children[i]);
    for(int i=0;i<24;++i)for(int j=0;j<4;++j)if(pool[i].children[j]==n)pool[i].children[j]=NULL;
    MenuWidgetNode **p=&g_MenuWidgetActiveListHead;while(*p&&*p!=n)p=&(*p)->next;if(*p)*p=n->next;
    used[n-pool]=0;
}
MenuWidgetNode *MenuWidget_GetCurrentNode(void) { return current; }
void MenuWidget_SetCurrentNode(MenuWidgetNode *n) { current=n; }
int MenuWidget_GridCellIndex(MenuWidgetNode *n) { return n->cursor_y; }
MenuWidgetSimpleDescriptor *MenuWidget_LookupSimpleDescriptor(unsigned int i) {
    /* the discs' own: 0 the icon rail, 1 the screen area, 7 the status screen's right-hand panel */
    static MenuWidgetSimpleDescriptor rail={8,32,20,132},area={28,32,252,132},status={180,32,128,132};
    return i==0?&rail:i==7?&status:&area;
}
void Draw_SetPrimCallback(MenuWidgetNode *n,int rows) {
    int old=n->visible_rows;n->y_limit=(rows+n->grid_width-1)/n->grid_width;n->visible_rows=n->y_limit<n->visible_rows_mirror?n->y_limit:n->visible_rows_mirror;
    n->parent->visible_rows+=n->disabled*(n->visible_rows-old);
    if(n->y_limit>n->visible_rows_mirror&&!n->popup_node){n->popup_node=allocate(n->parent,n->parent);n->popup_node->mode=3;}
    else if(n->y_limit<=n->visible_rows_mirror&&n->popup_node){MenuWidget_DestroyNode(n->popup_node);n->popup_node=NULL;}
}
static int inject_row=-1,inject_top,inject_adjust;
int Menu_StepScrollCursor(MenuWidgetNode *n,unsigned int b) {
    if(inject_row>=0){n->cursor_y=inject_row;n->scroll_y=inject_top;n->scroll_adjust=inject_adjust;inject_row=-1;return 1;}
    if(b&0x1000)n->cursor_y=n->cursor_y? n->cursor_y-1:n->y_limit-1;
    if(b&0x4000)n->cursor_y=(n->cursor_y+1)%n->y_limit;
    return (b&0xF000u)!=0;
}
static int print_limit,print_count,type_labels;
static u8 last_text[128];
void Draw_SetColor(int c) { g_DrawPrimColor=c; }
void Draw_SetTextDimmed(int d) { g_DrawTextDimmed=d; }
void Draw_PrintRawText(u8 *text) {
    assert(Draw_MeasureTextWidth(text)<=print_limit);++print_count;
    int copy=0;do {last_text[copy]=text[copy];}while(text[copy++]!=0xFF&&copy<127);
    for(u8 *p=text;*p!=0xFF;p+=Pe_ExtendedGlyph(p,NULL)?4:1)frame_bytes+=sizeof(RenderTexturedQuad);
    u8 word[32];pe_text_from_ascii(word,sizeof word,"Weapon");if(!memcmp(word,text,7))type_labels|=1;
    pe_text_from_ascii(word,sizeof word,"Armor");if(!memcmp(word,text,6))type_labels|=2;
}
void MenuWidget_DrawList(MenuWidgetNode *n,void (*draw)(int)) {
    for(int row=0;row<n->visible_rows;++row) {
        g_DrawTextPosX=n->draw_state;g_TextCursorX=2;g_TextCursorY=2;print_limit=n->draw_state-4;
        for(int col=0;col<n->grid_width;++col)draw((n->scroll_y+row)*n->grid_width+col);
    }
}
void Menu_DrawItemList(MenuWidgetNode *n) { (void)n; }
void Menu_PlayErrorSound(void) {}
void Menu_PlayConfirmSound(void) {}
void Menu_PlayCancelSound(void) {}
void Menu_PlayMoveSound(void) {}
int Menu_GetBattleEquipMode(void) { return 0; }
int pe_game_loaded(void) { return 1; }

void pe_log(const char *t,const char *f,...) {(void)t;(void)f;}
void pe_mod_log(const char *t,const char *f,...) {(void)t;(void)f;}
int pe_hook_add(int i,PeHookFn f,void *u,int p,const char *m) {(void)i;(void)f;(void)u;(void)p;(void)m;return 0;}
int pe_hook_dispatch(int i,void *ctx) {(void)i;(void)ctx;return 0;}

static PeCreatureInfo creatures[8] = {
 {.name="EX Monster Box",.section="Day 1",.hp=40000,.defense=130,.weak=31,.strong=31,
  .drop="Medicine 4 50%, Full Cure 25%, Super Tool 25%",.steal="Offense 1 50%, Defense 1 29 50%"},
 {.name="Rat",.section="Day 1",.hp=17,.defense=4,.drop="Ammo 6",.steal="nothing"},
 {.name="A longer creature",.section="Day 1",.hp=99},
 {.name="Another creature",.section="Day 1",.hp=99},
 {.name="A third creature",.section="Day 1",.hp=99},
 {.name="A fourth creature",.section="Day 1",.hp=99},
 {.name="A fifth creature",.section="Day 1",.hp=99},
 {.name="Eve",.section="Day 2",.boss=1,.hp=92,.defense=5,.drop="nothing",.steal="nothing"}
};
static unsigned known=PE_BESTIARY_MET|PE_BESTIARY_WEAK|PE_BESTIARY_LOOT;
static const PeCreatureInfo *info(int h) {return h>=0&&h<8?&creatures[h]:NULL;}
static unsigned state(int h) {(void)h;return known;}
static unsigned kills(int h) {(void)h;return 999999;}
static int model(int h,int f,PeModelSource *out) {(void)h;if(f)return 0;if(out)*out=(PeModelSource){.kind=PE_MODEL_ROOM};return 1;}
const PeBestiaryApi g_pe_bestiary_api={.info=info,.state=state,.kills=kills,.model=model};
unsigned pe_bestiary_new(int h) {(void)h;return known;}
void pe_bestiary_seen(int h) {(void)h;}
unsigned pe_bestiary_next_price(int h,unsigned *bit) {(void)h;*bit=0;return 0;}
int pe_bestiary_buy(int h) {(void)h;return 0;}
int pe_col_junk_ready(char *why,unsigned size) {(void)why;(void)size;return 1;}
unsigned pe_col_junk(void) {return 1000;}
const char *pe_col_last_said(void) {return "";}
static int extended;
static unsigned encode(unsigned char *out,unsigned size,const char *text) {
 unsigned n=0;for(;*text && n+4<size;++text) {if(*text==' '){out[n++]=0x0F;}else{out[n++]=0xF1;out[n++]=0xC5;out[n++]=1;out[n++]=1;}}
 out[n]=G_END;return n;
}
const void *pe_service_get(const char *name,int version) {
 static const PeInterfaceTextApi api={.encode=encode};(void)version;
 return extended&&!strcmp(name,"interface_text")?&api:NULL;
}
static int shows, hides;
void pe_stage_init(PeModelStage *s,int slot,uint32_t rgb) {memset(s,0,sizeof *s);s->slot=slot;s->box_rgb=rgb;}
void pe_stage_place(PeModelStage *s,int x,int y,int w,int h) {s->x=x;s->y=y;s->w=w;s->h=h;}
int pe_stage_show(PeModelStage *s,const PeModelSource *src,int silhouette) {
    (void)silhouette;++shows;s->has_source=src!=NULL;if(src)s->source=*src;return src!=NULL;
}
void pe_stage_pause(PeModelStage *s,int p) {s->paused=p;}
void pe_stage_draw(PeModelStage *s) {(void)s;}
void pe_stage_fullscreen(PeModelStage *s,int on) {s->fullscreen=on;}
void pe_stage_turn(PeModelStage *s,int delta) {s->yaw+=delta;}
void pe_stage_hide(PeModelStage *s) {++hides;s->has_source=0;s->fullscreen=0;}
static void send(unsigned b) {
    MenuWidgetNode *node=current;
    while(node) { if(node->update && node->update(node,b))return; node=node->parent; }
    assert(!"input was not handled");
}

static void draw_all(void) {
 frame_bytes=0;g_DrawPacketCursor=packet_arena;
 pe_draw_list(s_screen.list);pe_draw_info(s_info);
 if(frame_bytes>max_frame_bytes)max_frame_bytes=frame_bytes;
 if(frame_bytes>0x2800)fprintf(stderr,"advance %d bytes %zu quad %zu\n",font_advance,frame_bytes,sizeof(RenderTexturedQuad));
 assert(frame_bytes<=0x2800);
}
/* Whether the title bar ever offers L2 R2 over a full cycle of its pages. */
static int help_offers_scroll(void) {
 u8 want[8];int seen=0;pe_text_from_ascii(want,sizeof want,"L2 R2");
 s_said_tick=-100000;
 for(s_tick=0;s_tick<4*PE_HELP_TICKS;s_tick+=PE_HELP_TICKS) {u8 *text=NULL;pe_on_help_text(&text,NULL);assert(text);seen|=!memcmp(text,want,5);}
 return seen;
}
static void check_all_lines(void) {
 pe_build_info();
 assert(s_info_count>PE_INFO_ROWS);
 unsigned offsets[4]={0};
 for(int i=0;i<s_info_count;++i) {
  const PeInfoLine *line=&s_info_lines[i];
  unsigned *offset=&offsets[line->source];
  while(s_info_text[line->source][*offset]==0x0F)++*offset;
  assert(*offset==line->start); /* no lost text between wrapped lines */
  *offset+=line->length;
  memcpy(s_text,s_info_text[line->source]+line->start,line->length);s_text[line->length]=G_END;
  assert(Draw_MeasureTextWidth(s_text)<=s_info->draw_state-4);
 }
 for(int i=0;i<4;++i) {while(s_info_text[i][offsets[i]]==0x0F)++offsets[i];assert(s_info_text[i][offsets[i]]==G_END);}
 int selected=s_screen.list->cursor_y;
 while(s_info_top<s_info_count-PE_INFO_ROWS) {send(PE_MENU_R2|PE_MENU_REPEAT);draw_all();}
 assert(s_screen.list->cursor_y==selected);
 int last=s_info_top;send(PE_MENU_R2);assert(s_info_top==last);
 while(s_info_top)send(PE_MENU_L2|PE_MENU_REPEAT);
 send(PE_MENU_L2);assert(s_info_top==0);
}
int main(void) {
 g_DrawPacketArenaBase=g_DrawPacketCursor=packet_arena;g_DrawOrderingTableEntry=(unsigned int *)&ordering;
 pe_bestiary_menu_register("test");MenuWidgetNode from={0};pe_open(&from);
 /* right of the icon rail, under the title bar, out to the status screen's right edge */
 assert(s_screen.panel->x==28 && s_screen.panel->y==32 && s_info->parent->x==28 && s_info->parent->grid_width==280);
 assert(s_info->parent->x+s_info->parent->grid_width<=308);
 assert(s_screen.list->visible_rows_mirror==6 && s_info->visible_rows_mirror==4);
 for(font_advance=4;font_advance<=10;++font_advance) {draw_all();check_all_lines();}
 send(PE_MENU_R2);assert(s_info_top==1);
 send(0x4000);draw_all();assert(s_info_top==0 && pe_current()==1);
 send(PE_MENU_R1);draw_all();assert(s_info_top==0 && pe_current()==7);
 send(PE_MENU_TRIANGLE);assert(s_stage.fullscreen);
 send(PE_MENU_CROSS);assert(!s_stage.fullscreen);
 int yaw=s_stage.yaw;send(PE_MENU_RIGHT);assert(s_stage.yaw==yaw+96);
 send(PE_MENU_LEFT);assert(s_stage.yaw==yaw);
 send(PE_MENU_SQUARE);assert(s_paused);
 known=PE_BESTIARY_MET;draw_all();check_all_lines();
 known=0;draw_all();assert(s_info_top==0);
 assert(s_info_count<=PE_INFO_ROWS && !help_offers_scroll());   /* one page: no Info page in the title bar */
 known=7;draw_all();assert(s_info_count>PE_INFO_ROWS && help_offers_scroll());
 known=0;draw_all();
 known=7;send(PE_MENU_L1);draw_all();
 { /* the glyph that overflows is a space: the word before it stays on the line */
  const int saved_advance=font_advance;font_advance=14;draw_all();   /* the width, not the glyph budget, binds */
  const int fit=(s_info->draw_state-4)/font_advance;
  static char edge[128];const PeCreatureInfo *c=info(pe_current());const char *saved_drop=c->drop;
  assert(fit>8 && fit+4<(int)sizeof edge);
  memset(edge,'A',fit-6);strcpy(edge+fit-6," BB");     /* "Drop: " + word = exactly the width */
  creatures[pe_current()].drop=edge;pe_build_info();
  int first=-1;for(int i=0;i<s_info_count;++i)if(s_info_lines[i].source==2){first=i;break;}
  assert(first>=0 && s_info_lines[first].length==fit);
  assert(s_info_lines[first+1].source==2 && s_info_lines[first+1].length==2);
  creatures[pe_current()].drop=saved_drop;font_advance=saved_advance;draw_all();
 }
 static char long_loot[400];memset(long_loot,'A',sizeof long_loot-1);creatures[1].drop=long_loot;
 draw_all();check_all_lines();
 extended=1;draw_all();check_all_lines();
 for(int i=0;i<s_info_count;++i) {const PeInfoLine *l=&s_info_lines[i];assert(s_info_text[l->source][l->start]!=0xC5);}
 extended=0;
 send(PE_MENU_CIRCLE);assert(current==&from && !s_screen.list);
 pe_open(&from);draw_all();assert(s_info_top==0);
 send(PE_MENU_CIRCLE);for(int i=0;i<24;++i)assert(!used[i]);
 printf("Bestiary checks passed: full-width panel, complete wrapped details, overflowing-space breaks, bounded scrolling, Info page only when it scrolls, selection reset, locked tiers, viewer controls, teardown; peak %zu bytes.\n",max_frame_bytes);
 return 0;
}
