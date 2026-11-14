#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../mods/pe_challenges/pe_ch_run.c"
#include "../mods/pe_challenges/pe_ch_start.c"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Deliberately use catalog order different from save ids. */
static const char *const test_levels[] = {"Off", "1.5x", "2x"};
static const PeChallengeDef defs[CH_COUNT] = {
    {CH_HARDCORE, "Hardcore", "Hardcore", "Permadeath; locks the run", NULL, 0,
     0, CH_DEF_NEW_GAME_ONLY | CH_DEF_LOCKS_RUN},
    {CH_TOUGH, "Tough crowd", "Tough", "More durable enemies", test_levels, 3},
    {CH_NO_PE, "No PE", "No PE", "No Parasite Energy"},
    {CH_NO_ITEMS, "No items", "No items", "No items"},
    {CH_NO_ESCAPE, "No escape", "No escape", "No escape"},
    {CH_GLASS, "Glass", "Glass", "Glass"},
    {CH_LOCKED_LOADOUT, "Loadout", "Loadout", "Loadout"},
    {CH_ONE_HIT, "One hit", "One hit", "One hit"},
    {CH_NO_EXP, "No EXP", "No EXP", "No EXP"},
    {CH_BLOOD_PRICE, "Blood", "Blood", "Blood"},
    {CH_FAST_ENEMIES, "Fast", "Fast", "Fast"},
    {CH_CLUB_ONLY, "Club", "Club", "Club"},
    {CH_HALF_HEALTH, "Half", "Half", "Half"},
};
const PeChallengeDef *const g_ch_defs[] = {
    &defs[0], &defs[1], &defs[2], &defs[3],  &defs[4],  &defs[5], &defs[6],
    &defs[7], &defs[8], &defs[9], &defs[10], &defs[11], &defs[12]};
const unsigned g_ch_def_count = CH_COUNT;
const PeChallengeCategory g_ch_categories[CH_CATEGORIES] = {
    {"Restrictions", "Restrictions"},
    {"Enemies", "Enemies"},
    {"Aya", "Aya"},
    {"Run", "Run"}};
const PeChallengeDef *pe_ch_def(int id) {
  for (unsigned i = 0; i < g_ch_def_count; ++i)
    if (g_ch_defs[i]->id == id)
      return g_ch_defs[i];
  return NULL;
}
static int answers[128], answer_count, asked, saved, changed, loaded;
static int expect_hardcore_summary, expect_tough_label;
static PeHookFn new_game_hook;
static int new_game_priority;
int pe_menu_choose_title(const char *title, const char *keys,
                         const char *const *names,
                         const char *const *descriptions, unsigned count,
                         int initial) {
  assert(title && keys && names && descriptions && initial >= 0 &&
         initial < (int)count);
  assert(asked < answer_count);
  if (!strcmp(title, "Choose your challenges")) {
    assert(count == CH_COUNT + 2 && !strcmp(names[0], "Start game"));
    if (expect_hardcore_summary && strstr(names[2], "On"))
      assert(strstr(descriptions[0], "permanently") &&
             strstr(descriptions[0], "cannot be lowered"));
    if (expect_tough_label && strstr(names[3], "2x"))
      expect_tough_label = 0;
  }
  return answers[asked++];
}
static void script(const int *choices, int n) {
  if (n)
    memcpy(answers, choices, n * sizeof *choices);
  answer_count = n;
  asked = 0;
}
#define SCRIPT(...)                                                            \
  do {                                                                         \
    const int a[] = {__VA_ARGS__};                                             \
    script(a, sizeof a / sizeof *a);                                           \
  } while (0)
int pe_game_loaded(void) { return loaded; }
void pe_menu_save_settings(void) { saved++; }
void pe_ch_assist_reset(void) {}
void pe_ch_emit_changed(void) { changed++; }
void pe_ch_emit_assisted(const char *s) { (void)s; }
void pe_toast(const char *s) { (void)s; }
void pe_log(const char *tag, const char *fmt, ...) {
  (void)tag;
  (void)fmt;
}
void pe_mod_log(const char *mod, const char *fmt, ...) {
  (void)mod;
  (void)fmt;
}
Uint64 SDL_GetPerformanceCounter(void) { return 123456; }
Uint64 SDL_GetTicksNS(void) { return 987654; }
int pe_save_register(const PeSaveRecord *r) {
  assert(r == &s_record);
  return 0;
}
int pe_hook_add(int id, PeHookFn fn, void *user, int priority,
                const char *mod) {
  (void)user;
  assert(!strcmp(mod, "pe_challenges"));
  if (id == PE_HOOK_NEW_GAME) {
    new_game_hook = fn;
    new_game_priority = priority;
  }
  return 0;
}
void pe_menu_alias_setting(const char *key, const char *old) {
  (void)key;
  (void)old;
}
int pe_menu_register_choice(const char *sub, const char *label,
                            const char *desc, int *value,
                            const char *const *names, unsigned count) {
  (void)sub;
  (void)label;
  (void)desc;
  (void)value;
  (void)names;
  (void)count;
  return 0;
}
int pe_menu_register_toggle(const char *sub, const char *label,
                            const char *desc, int *value, void (*fn)(void)) {
  (void)sub;
  (void)label;
  (void)desc;
  (void)value;
  (void)fn;
  return 0;
}
int pe_menu_page(const char *sub, const char *desc, int order) {
  (void)sub;
  (void)desc;
  (void)order;
  return 0;
}

int main(void) {
  int levels[CH_COUNT] = {0};
  /* Selecting multiple challenges and levels uses save ids, not catalog
   * positions. */
  expect_hardcore_summary = expect_tough_label = 1;
  SCRIPT(2, 1, 3, 2, 4, 1, 0);
  assert(pe_ch_start_pick(levels));
  assert(levels[CH_HARDCORE] == 1 && levels[CH_TOUGH] == 2 &&
         levels[CH_NO_PE] == 1);
  assert(!expect_tough_label && asked == answer_count);
  /* Back keeps a level, and clearing never changes the input until Start. */
  SCRIPT(3, 3, 0);
  assert(pe_ch_start_pick(levels) && levels[CH_TOUGH] == 2);
  SCRIPT(1, -1);
  assert(!pe_ch_start_pick(levels) && levels[CH_TOUGH] == 2);
  SCRIPT(3, 0, -1);
  assert(!pe_ch_start_pick(levels) && levels[CH_TOUGH] == 2);
  SCRIPT(-1);
  assert(!pe_ch_start_pick(levels) && levels[CH_HARDCORE] == 1);
  SCRIPT(1, 0);
  assert(pe_ch_start_pick(levels));
  for (int i = 0; i < CH_COUNT; ++i)
    assert(!levels[i]);
  /* Every catalog entry can be selected in one new game. */
  int all[CH_COUNT * 2 + 1];
  for (int i = 0; i < CH_COUNT; ++i) {
    all[i * 2] = i + 2;
    all[i * 2 + 1] = 1;
  }
  all[CH_COUNT * 2] = 0;
  script(all, CH_COUNT * 2 + 1);
  assert(pe_ch_start_pick(levels));
  for (int i = 0; i < CH_COUNT; ++i)
    assert(levels[i] == 1);
  /* Run setup is before normal hooks, after the optional style picker (-100).
   */
  assert(pe_ch_run_register("pe_challenges") == 0);
  assert(new_game_hook && new_game_priority > PE_PRIORITY_EARLY &&
         new_game_priority < PE_PRIORITY_NORMAL);
  PeHookNewGame title = {PE_NEW_GAME_TITLE}, ex = {PE_NEW_GAME_EX};
  pe_ch_rows_clear();
  saved = changed = 0;
  SCRIPT(2, 1, 3, 2, 0);
  new_game_hook(&title, NULL);
  assert(saved == 1 && changed == 1 && pe_ch_on(CH_HARDCORE) &&
         pe_ch_level(CH_TOUGH) == 2);
  assert(g_ch_run.run_id && !g_ch_run.dropped && !g_ch_run.flags);
  loaded = 1;
  s_on[CH_TOUGH] = 0;
  pe_sync();
  assert(s_on[CH_TOUGH] == 2 &&
         pe_ch_level(CH_TOUGH) == 2); /* Hardcore locks apply immediately. */
  loaded = 0;
  script(NULL, 0);
  new_game_hook(&ex, NULL);
  assert(!asked && saved == 1); /* EX: no question. */
  new_game_hook(NULL, NULL);
  assert(!asked);
  SCRIPT(-1);
  new_game_hook(&title, NULL);
  assert(saved == 1 && pe_ch_level(CH_TOUGH) == 2);
  SCRIPT(1, 0);
  new_game_hook(&title, NULL);
  assert(saved == 2 && !g_ch_run.active && !g_ch_run.run_id);
  puts("challenge startup: selection, levels, Back, clear, unavailable, run "
       "locks, EX and hook order passed");
  return 0;
}
