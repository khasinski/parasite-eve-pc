#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../mods/host/pe_mod_panel.c"
#include <assert.h>
#include <stdio.h>

bool SDL_PushEvent(SDL_Event *ev) {
  (void)ev;
  return 1;
}
static void key(SDL_Scancode code) {
  SDL_Event e = {0};
  e.type = SDL_EVENT_KEY_DOWN;
  e.key.scancode = code;
  pe_choose_event(&e);
  assert(s_choose.cursor >= s_choose.scroll);
  assert(s_choose.cursor < s_choose.scroll + pe_choose_visible());
  assert(s_choose.scroll >= 0 &&
         s_choose.scroll + pe_choose_visible() <= s_choose.count);
}

/* The rest of the panel's host is idle in this isolated input/render fixture.
 */
int Psyz_AudioIsPaused(void) { return false; }
void Psyz_AudioPause(void) {}
void Psyz_AudioUnpause(void) {}
void Psyz_PauseHostClock_SDL3(bool paused) { (void)paused; }
int Psyz_VideoVSync(int mode) {
  (void)mode;
  return 0;
}
int Psyz_VideoSetSpeed(unsigned speed) {
  (void)speed;
  return 0;
}
PsyzOverlayEventFilter_SDL3
Psyz_SetOverlayEventFilter_SDL3(PsyzOverlayEventFilter_SDL3 f) {
  return f;
}
void SDL_ResetKeyboard(void) {}
int pe_mod_interactive(void) { return 0; }
void pe_mod_log(const char *id, const char *fmt, ...) {
  (void)id;
  (void)fmt;
}
void pe_toast_block(int blocked) { (void)blocked; }
void pe_toast_install(void) {}
void pe_menu_save_settings(void) {}
int pe_menu_row_count(void) { return 0; }
PeMenuRow *pe_menu_row(int i) {
  (void)i;
  return NULL;
}
const PeMenuPageInfo *pe_menu_page_info(const char *p) {
  (void)p;
  return NULL;
}
int pe_menu_row_active(const PeMenuRow *r) {
  (void)r;
  return 0;
}
int pe_menu_row_adjustable(const PeMenuRow *r) {
  (void)r;
  return 0;
}
void pe_menu_row_value_text(PeMenuRow *r, char *out, unsigned size) {
  (void)r;
  if (size)
    out[0] = 0;
}
void pe_menu_row_step(PeMenuRow *r, int d) {
  (void)r;
  (void)d;
}
void pe_menu_row_accept(PeMenuRow *r) { (void)r; }
void pe_mod_manage_refresh(void) {}
const void *pe_service_get(const char *name, int version) {
  (void)name;
  (void)version;
  return NULL;
}
int pe_ui_gpu_ready(void) { return 0; }
void pe_ui_gpu_install(void) {}
void pe_ui_gpu_hide(void) {}
int pe_ui_gpu_window_size(int *w, int *h) {
  (void)w;
  (void)h;
  return 0;
}
int pe_ui_gpu_show(const PeSurface *s, int x, int y, float dim) {
  (void)s;
  (void)x;
  (void)y;
  (void)dim;
  return 0;
}
int pe_ui_font_ready(void) { return 0; }
int pe_ui_surface_begin(PeSurface *s, int w, int h) {
  (void)s;
  (void)w;
  (void)h;
  return 0;
}
void pe_ui_surface_finish(PeSurface *s) { (void)s; }
void pe_ui_fill_round_rect(PeSurface *s, float x, float y, float w, float h,
                           float r, PeColor c) {
  (void)s;
  (void)x;
  (void)y;
  (void)w;
  (void)h;
  (void)r;
  (void)c;
}
PeColor pe_ui_alpha(PeColor c, float a) {
  c.a *= a;
  return c;
}
float pe_ui_text_width(const char *s, float em) {
  return (float)strlen(s) * em * 0.5f;
}
static int painted_rows;
static char painted[6][32];
void pe_ui_draw_text(PeSurface *s, float x, float y, float em, const char *text,
                     PeColor c, PeAlign align) {
  (void)s;
  (void)x;
  (void)y;
  (void)c;
  (void)align;
  if (em == 27) {
    assert(painted_rows < 6);
    snprintf(painted[painted_rows++], 32, "%s", text);
  }
}

int main(void) {
  static const char *const names[15] = {"0",  "1",  "2",  "3",  "4",
                                        "5",  "6",  "7",  "8",  "9",
                                        "10", "11", "12", "13", "14"};
  static const char *const descriptions[15] = {"Description"};
  memset(&s_choose, 0, sizeof s_choose);
  s_choose.names = names;
  s_choose.descriptions = descriptions;
  s_choose.count = 15;
  s_choose.open = 1;
  assert(pe_choose_visible() == 6);
  for (int i = 0; i < 15; ++i)
    key(SDL_SCANCODE_DOWN);
  assert(!s_choose.cursor && !s_choose.scroll);
  key(SDL_SCANCODE_UP);
  assert(s_choose.cursor == 14 && s_choose.scroll == 9);
  key(SDL_SCANCODE_HOME);
  assert(!s_choose.scroll);
  key(SDL_SCANCODE_END);
  assert(s_choose.scroll == 9);
  s_scale = 1;
  s_choose.title = "Challenges";
  s_choose.help = "";
  pe_paint_title(700, pe_title_design_h(6));
  assert(painted_rows == 6 && !strcmp(painted[0], "9") &&
         !strcmp(painted[5], "14"));
  SDL_Event e = {0};
  e.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
  e.gbutton.button = SDL_GAMEPAD_BUTTON_SOUTH;
  pe_choose_event(&e);
  assert(!s_choose.open && s_choose.result == 14);
  s_choose.count = 5;
  s_choose.cursor = s_choose.scroll = 0;
  assert(pe_choose_visible() ==
         5); /* The styles list keeps its five-row geometry. */
  s_choose.count = 15;
  s_choose.descriptions = NULL;
  assert(pe_choose_visible() ==
         VISIBLE_ROWS); /* Other panel questions keep their viewport. */
  puts("title choices: scrolling, wrap, Home/End, controller acceptance and "
       "style geometry passed");
  return 0;
}
