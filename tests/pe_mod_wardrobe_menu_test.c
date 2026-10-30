/* Production Wardrobe and host widgets; game drawing and the model stage are simulated. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../mods/host/pe_main_menu.c"
#include "../mods/pe_collection/pe_wardrobe_menu.c"
static size_t frame_bytes, max_frame_bytes;
_Alignas(RenderTexturedQuad) static unsigned char packet_arena[0x4000];
static RenderNativeTag ordering;
static int force_large_icon, effect_sprites, custom_badges;

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
int Draw_MeasureTextWidth(u8 *t) { int n=0;while(*t++!=0xFF)++n;return n*font_advance; }
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
MenuWidgetSimpleDescriptor *MenuWidget_LookupSimpleDescriptor(unsigned int i) { static MenuWidgetSimpleDescriptor d={28,24,252,0};(void)i;return &d; }
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
static int name_queries,price_queries,tool_queries;
static u8 last_text[128];
void Draw_SetColor(int c) { g_DrawPrimColor=c; }
void Draw_SetTextDimmed(int d) { g_DrawTextDimmed=d; }
void Draw_PrintRawText(u8 *text) {
    assert(Draw_MeasureTextWidth(text)<=print_limit);++print_count;
    int copy=0;do {last_text[copy]=text[copy];}while(text[copy++]!=0xFF&&copy<127);
    for(u8 *p=text;*p!=0xFF;++p)frame_bytes+=sizeof(RenderTexturedQuad);
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
static PeCostume costumes[40];
static const PeCostume *worn;
static int wears, provider_loads, shows, hides, story_bank=10;
int pe_wardrobe_owned_count(void) {return 40;}
const PeCostume *pe_wardrobe_owned(int k) {return k>=0&&k<40?&costumes[k]:NULL;}
const PeCostume *pe_wardrobe_worn(void) {return worn;}
void pe_wardrobe_wear_later(const PeCostume *c) {++wears;worn=c;}
int pe_wardrobe_story_bank(void) {return story_bank;}
static void provider(int wearing) {(void)wearing;++provider_loads;}
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
    pe_draw_list(s_screen.list);pe_draw_preview(s_frame);pe_draw_info(s_info);
    if(frame_bytes>max_frame_bytes)max_frame_bytes=frame_bytes;
    assert(frame_bytes<=0x2800);  /* reserve 6 KB for native frames, title, clock and side rail */
}
int main(void) {
    for(int i=0;i<40;++i) costumes[i]=(PeCostume){.key="test",.name="A very long custom costume name that must not spill into the preview",
        .group=i<20?"First group":"Second group",.bank=-1,.bundle="harlequin",.price=999};
    costumes[0].bank=10; costumes[1].load=provider; costumes[1].bundle=NULL;
    worn=&costumes[39];
    g_DrawPacketArenaBase=g_DrawPacketCursor=packet_arena;g_DrawOrderingTableEntry=(unsigned int *)&ordering;
    pe_wardrobe_menu_register("test");assert(s_stage.slot==3);
    MenuWidgetNode from={0}; pe_open(&from);
    assert(current==s_screen.list && s_screen.list->visible_rows_mirror==6 && s_info->visible_rows_mirror==3);
    assert(s_screen.list->draw_state==112 && s_frame->draw_state==112);
    assert(s_stage.x>=156 && s_stage.w<=128 && s_info->parent->y<224);
    assert(s_members==41 && pe_current()==40 && s_screen.list->scroll_y==35);
    draw_all();assert(!wears && !provider_loads && s_stage.source.kind==PE_MODEL_SKIN);
    assert(!strcmp(s_stage.source.folder,"harlequin"));
    send(PE_TRIANGLE);assert(s_stage.fullscreen);
    send(PE_MENU_CROSS);assert(!s_stage.fullscreen && !wears); /* fullscreen Cross only exits */
    int selected=s_screen.list->cursor_y;
    assert(s_screen.list->update==pe_screen_preview_input);
    send(PE_RIGHT);assert(s_stage.yaw==96 && s_screen.list->cursor_y==selected);
    send(PE_LEFT);assert(s_stage.yaw==0 && s_screen.list->cursor_y==selected);
    send(0x1000);assert(s_screen.list->cursor_y==selected-1);draw_all();
    send(0x4000);assert(s_screen.list->cursor_y==selected);draw_all();
    send(PE_SQUARE);assert(!pe_input(s_screen.panel,PE_SQUARE|PE_REPEAT));assert(s_paused);draw_all();assert(s_stage.paused);
    send(PE_R1);assert(s_members==1 && pe_current()==0);draw_all();
    assert(s_stage.source.folder==NULL && s_stage.source.map==10);
    send(PE_MENU_CROSS);assert(wears==1 && !worn);
    send(PE_R1);assert(s_members==20);draw_all();assert(s_stage.source.map==10);
    pe_info_row(1);
    u8 wear_label[32]; pe_text_from_ascii(wear_label,sizeof wear_label,"X: Wear this outfit");
    assert(!memcmp(last_text,wear_label,20)); /* complete action label, including terminator */
    s_screen.list->cursor_y=1;draw_all();assert(!s_preview && !provider_loads);
    send(PE_TRIANGLE);assert(!s_stage.fullscreen);
    send(PE_MENU_CROSS);assert(wears==2 && worn==&costumes[1] && !provider_loads);
    s_screen.list->cursor_y=19;s_screen.list->scroll_y=14;draw_all();
    send(PE_R1);draw_all();send(PE_L1);assert(s_screen.list->cursor_y==19 && s_screen.list->scroll_y==14);
    for(int advance=4;advance<=10;++advance) {font_advance=advance;draw_all();u8 *help=NULL;pe_on_help_text(&help,NULL);assert(help && Draw_MeasureTextWidth(help)<=278);}
    for(int i=0;i<3;++i)pe_on_frame(NULL,NULL);assert(!s_stage.has_source && s_shown==-1);
    draw_all();assert(s_stage.has_source);
    send(PE_MENU_CIRCLE);assert(current==&from && !s_screen.list && !s_stage.has_source);
    for(int i=0;i<24;++i)assert(!used[i]);
    assert(!provider_loads && wears==2);
    printf("Wardrobe checks passed: owned-only, equip-only, previews, grouping, scrolling, long text, fullscreen, pause, teardown; body peak %zu bytes.\n",max_frame_bytes);
    return 0;
}
